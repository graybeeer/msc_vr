"""Native handling timing and narrow-opening regression; reuses the mapped fleet fixtures.
-HandlingStages=0,1 compares original and load-relative floor docking. -HandlingRun=name keeps output.
-Clearance=0/1 controls adaptive padding; -NarrowWidth=130 creates a known-map opening.
Actual AGV transforms, velocities, payload physics and work-complete flags are never assigned.
"""
from pathlib import Path
import json,re,time,traceback,unreal
root=Path(unreal.Paths.project_dir()).resolve()
command=unreal.SystemLibrary.get_command_line()
parent=root/'Scripts/verify_localization_improvements_pie.py'
scope=dict(__file__=str(parent),__name__='__handling_fleet__')
exec(compile(parent.read_text(encoding='utf-8-sig'),str(parent),'exec'),scope)
env=scope['env']
name=re.search(r'-HandlingRun=([A-Za-z0-9_-]+)',command,re.I)
out=root/'Saved/Testing/HandlingImprovements_20261031'/(name.group(1) if name else 'Final')
out.mkdir(parents=True,exist_ok=True)
scope['OUT']=out;env['output']=out/'NativeReport.json'
stages=re.search(r'-HandlingStages=([0-9,]+)',command,re.I)
stages=[int(x) for x in stages.group(1).split(',')] if stages else [1]
assert stages and all(n in (0,1) for n in stages),'Handling stages must be 0 or 1'
clearance=re.search(r'-Clearance=([01])',command,re.I)
clearance=int(clearance.group(1)) if clearance else 1
width=re.search(r'-NarrowWidth=([0-9.]+)',command,re.I)
width=float(width.group(1)) if width else None
if width:
    env['CASES']=[dict(id=20,name='known_narrow_opening',kind='parallel',vehicles=1,seed=202,limit=260,expect='transport' if width>=120 else 'safe_halt')]
env['CASES']=[dict(c,handling_stage=n) for n in stages for c in env['CASES']]
old_setup,old_complete,old_finish=env['setup'],env['complete_case'],env['finish']
profiles={};current={};errors=[];last_write=0

def setup(definition):
    global current
    stage=definition['handling_stage']
    world=env['editor'].get_editor_world()
    unreal.SystemLibrary.execute_console_command(world,'Warehouse.Handling.Stage '+str(stage))
    unreal.SystemLibrary.execute_console_command(world,'Warehouse.AdaptiveClearance '+str(clearance))
    record=old_setup(definition)
    key=str(stage)+'-'+str(definition['id'])
    scope['statistics'].pop(str(scope['level'])+'-'+str(definition['id']),None)
    scope['statistics'][key]=scope['current']
    scope['current']['handling_stage']=stage
    current=profiles[key]=dict(stage=stage,case=definition['id'],clearance=clearance,opening_width_cm=width,vehicles={})
    if width:
        # Fixture authoring only: leave the template's stocked racks in the
        # left room instead of embedding independent physical stock in the divider.
        for a in env['actors'].get_all_level_actors():
            if a.get_actor_label().startswith('StressRack'):
                a.set_actor_location(a.get_actor_location()+unreal.Vector(-2000,0,0),False,True)
        for p in record['rack_origins']:p[0]-=2000
        vehicle=next(a for a in env['actors'].get_all_level_actors() if a.get_actor_label()=='StressAGV0')
        y=vehicle.get_actor_location().y
        for label,low,high in [('North',y+width/2,4400),('South',-4400,y-width/2)]:
            a=env['block']('HandlingNarrow'+label,(0,(low+high)/2,150),(200,high-low,300))
            a.tags=['WarehouseMapStructure']
    return record

def sample(dt):
    global last_write
    try:
        if scope.get('in_tick'):return
        game=env['editor'].get_game_world()
        if not game or not current or not env.get('live'):return
        now=unreal.GameplayStatics.get_time_seconds(game)
        for n in env['runtime'].get('vehicles',[]):
            v=env['live'].get(n)
            if not v:continue
            state=str(v.ai_state)
            row=current['vehicles'].setdefault(n,dict(seconds={},entries=[],last_time=now,last_state=state,min_margin_cm=25,max_speed_in_opening_cm=0,max_speed_after_opening_cm=0,opening_frames=0))
            elapsed=max(0,now-row['last_time'])
            row['seconds'][row['last_state']]=row['seconds'].get(row['last_state'],0)+elapsed
            if state!=row['last_state']:
                row['entries'].append(dict(t=now,state=state,status=v.status,speed_cm=v.current_speed_cm))
            row.update(last_time=now,last_state=state,recoveries=v.recovery_count,insertion_retries=v.fork_insertion_retries)
            margin=v.route_clearance_cm
            if margin>0:row['min_margin_cm']=min(row['min_margin_cm'],margin)
            p=v.get_actor_location()
            if width and v.ai_state==unreal.WarehouseAIState.NAVIGATE_DELIVERY:
                if -180<p.x<180:
                    row['opening_frames']+=1
                    row['max_speed_in_opening_cm']=max(row['max_speed_in_opening_cm'],abs(v.current_speed_cm))
                if p.x>300:row['max_speed_after_opening_cm']=max(row['max_speed_after_opening_cm'],abs(v.current_speed_cm))
            if width and width<120 and v.ai_state==unreal.WarehouseAIState.FAULT and abs(v.current_speed_cm)<2:
                assert not v.completed_job_ids and v.root_component.is_simulating_physics()
                current.setdefault('halted_at',now)
                if now-current['halted_at']>=3:
                    env['complete_case'](True,'Physically undersized known opening: explicit stop, native load/collision retained, no false completion')
        if time.monotonic()-last_write>1:
            (out/'Handling.json').write_text(json.dumps(profiles,ensure_ascii=False,indent=2),encoding='utf-8');last_write=time.monotonic()
    except Exception:
        error=traceback.format_exc()
        if error not in errors:errors.append(error)

def complete(ok,detail):
    if errors:ok=False;detail+='; '+str(errors)
    if ok and env['case'].get('expectation')=='transport':
        current['placement_errors']=[]
        for j in env['jobs']:
            pallet=env['live'][j['pallet']]
            point=pallet.get_actor_location();goal=j['destination']
            current['placement_errors'].append(dict(job=j['job_id'],planar_cm=((point.x-goal[0])**2+(point.y-goal[1])**2)**.5,
                yaw_deg=abs((pallet.get_actor_rotation().yaw-j['yaw']+180)%360-180)))
    if ok and width and width>=120:
        row=current['vehicles'].get('StressAGV0',{})
        expected_margin={130:3,150:10}.get(width)
        if expected_margin is not None and row.get('min_margin_cm')!=expected_margin:
            ok=False;detail+='; expected planning margin '+str(expected_margin)
        if not(row.get('min_margin_cm',25)<25 and row.get('opening_frames',0)>0 and row.get('max_speed_in_opening_cm',1000)<=35 and row.get('max_speed_after_opening_cm',0)>40):
            ok=False;detail+='; adaptive margin/slow opening/cruise restoration failed: '+str(row)
    env['case']['handling_profile']=current
    current.update(passed=ok,detail=detail)
    old_complete(ok,detail)

def finish():
    unreal.unregister_slate_post_tick_callback(observer)
    (out/'Handling.json').write_text(json.dumps(profiles,ensure_ascii=False,indent=2),encoding='utf-8')
    old_finish()

env.update(setup=setup,complete_case=complete,finish=finish)
observer=unreal.register_slate_post_tick_callback(sample)
