"""Observe production transport, without changing AI/physics or forcing runtime motion.

-BenchGroup=flat/matrix (default matrix), -BenchRepeats=2, -BenchRun=name.
Flat trials compare total pallet+payload masses of 50/500/1000 kg, two seeds.
-BenchSeed=202 selects the first seed; it is shared between load conditions.
Other arguments from verify_forklift_handling_pie.py remain available, including
-StressCases and -NarrowWidth. CSV samples use simulation time, not wall time.
"""
from pathlib import Path
import csv,json,math,re,time,traceback,unreal

root=Path(unreal.Paths.project_dir()).resolve()
command=unreal.SystemLibrary.get_command_line()
source=root/'Scripts/verify_forklift_handling_pie.py'
scope=dict(__file__=str(source),__name__='__transport_benchmark__')
exec(compile(source.read_text(encoding='utf-8-sig'),str(source),'exec'),scope)
env=scope['env']
match=re.search(r'-BenchRun=([A-Za-z0-9_-]+)',command,re.I)
out=root/'Saved/Testing/TransportBenchmark_20261031'/(match.group(1) if match else 'Matrix')
out.mkdir(parents=True,exist_ok=True)
scope['out']=out;scope['scope']['OUT']=out;env['output']=out/'NativeReport.json'
if '-benchgroup=flat' in command.lower():
    match=re.search(r'-BenchRepeats=([12])',command,re.I)
    repeats=int(match.group(1)) if match else 2
    match=re.search(r'-BenchSeed=([0-9]+)',command,re.I)
    seed=int(match.group(1)) if match else 202
    env['CASES']=[dict(id=21+i+repeat*100,name=f'flat_14m_{mass}kg_seed{seed+repeat}',
        kind='parallel',vehicles=1,seed=seed+repeat,limit=360,handling_stage=1,load_kg=mass)
        for repeat in range(repeats) for i,mass in enumerate((50,500,1000))]
elif not re.search(r'-StressCases=',command,re.I):
    # Old saved-map fixtures order uninspected stock. Current end-to-end receiving
    # and saved geometry are tested through verify_estimated_navigation_pie.py.
    env['CASES']=[c for c in env['CASES'] if c['kind']!='saved']
old_setup,old_complete,old_finish=env['setup'],env['complete_case'],env['finish']
measurements={};current={};traces=[];errors=[]

def setup(definition):
    global current,traces
    record=old_setup(definition)
    if 'load_kg' in definition:
        authored={a.get_actor_label():a for a in env['actors'].get_all_level_actors()}
        cargo=authored['StressCargo0_0'];point=cargo.get_actor_location()
        bottom=unreal.WarehouseDamageSystem.get_physical_bounds(cargo).min.z
        cargo.set_gross_mass_kg(definition['load_kg']-25)
        env['fit'](cargo,(100,90,80),[point.x,point.y,bottom+40])
        assert abs(unreal.WarehouseDamageSystem.get_physical_bounds(cargo).min.z-bottom)<.1
        damage=authored[env['runtime']['strength']];specs=list(damage.objects)
        for spec in specs:
            if cargo in spec.members:spec.mass_kg=definition['load_kg']-25
        damage.set_editor_property('objects',specs)
        env['runtime']['expected_load_masses']=env['expected_load_masses'](authored,env['runtime']['goods'])
    current=measurements[str(definition['id'])]=dict(id=definition['id'],name=definition['name'],
        seed=definition['seed'],authored_load_kg=definition.get('load_kg'),vehicles={})
    traces=[]
    return record

def observe(dt):
    try:
        if scope['scope'].get('in_tick') or not env.get('live') or not current or 'passed' in current:return
        game=env['editor'].get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        for name in env['runtime']['vehicles']:
            vehicle=env['live'][name];body=vehicle.root_component
            point=vehicle.get_actor_location();height=vehicle.get_fork_height_cm()
            state=str(vehicle.ai_state);forward=abs(vehicle.current_speed_cm)
            velocity=body.get_physics_linear_velocity();speed=math.hypot(velocity.x,velocity.y)
            loaded=vehicle.get_supported_pallet() is not None
            phase='traffic_wait' if vehicle.traffic_waiting or vehicle.traffic_yielding else state
            row=current['vehicles'].setdefault(name,dict(last_t=now,last_position=[point.x,point.y],last_height=height,
                last_state=state,last_phase=phase,seconds={},phase_seconds={},distance_cm=0,peak_empty_cm_s=0,peak_loaded_cm_s=0,
                moving_speeds_cm_s=[],lift_up_cm_s=[],lift_down_cm_s=[],cruise_empty=[],cruise_loaded=[],last_trace=-1,
                jobs={}))
            elapsed=now-row['last_t']
            if elapsed<=0:continue
            distance=math.hypot(point.x-row['last_position'][0],point.y-row['last_position'][1])
            row['seconds'][row['last_state']]=row['seconds'].get(row['last_state'],0)+elapsed
            row['phase_seconds'][row['last_phase']]=row['phase_seconds'].get(row['last_phase'],0)+elapsed
            row['distance_cm']+=distance
            key='peak_loaded_cm_s' if loaded else 'peak_empty_cm_s'
            row[key]=max(row[key],speed)
            if speed>5:row['moving_speeds_cm_s'].append(speed)
            if 'NAVIGATE_' in state:
                row['cruise_loaded' if loaded else 'cruise_empty'].append(forward)
            lift_speed=(height-row['last_height'])/elapsed
            if speed<5 and .5<abs(lift_speed)<100:
                row['lift_up_cm_s' if lift_speed>0 else 'lift_down_cm_s'].append(abs(lift_speed))
            for job in env['jobs']:
                if job['vehicle']!=name or job.get('submitted') is None or job.get('completed') is not None:continue
                j=row['jobs'].setdefault(job['job_id'],dict(seconds={},phase_seconds={},distance_cm=0,moving_seconds=0,stopped_seconds=0,
                    time_to_90pct_empty_s=None,first_empty_move=None))
                j['seconds'][row['last_state']]=j['seconds'].get(row['last_state'],0)+elapsed
                j['phase_seconds'][row['last_phase']]=j['phase_seconds'].get(row['last_phase'],0)+elapsed
                j['distance_cm']+=distance
                j['moving_seconds' if speed>5 else 'stopped_seconds']+=elapsed
                if 'NAVIGATE_PICKUP' in state and not loaded:
                    if speed>5 and j['first_empty_move'] is None:j['first_empty_move']=now
                    if forward>=162 and j['time_to_90pct_empty_s'] is None and j['first_empty_move'] is not None:
                        j['time_to_90pct_empty_s']=now-j['first_empty_move']
            if now-row['last_trace']>=.099:
                traces.append([round(now,5),name,state,point.x,point.y,point.z,forward,speed,
                    body.get_physics_angular_velocity_in_degrees().z,height,loaded,vehicle.route_clearance_cm,
                    vehicle.traffic_waiting,vehicle.traffic_yielding,vehicle.traffic_status])
                row['last_trace']=now
            row.update(last_t=now,last_position=[point.x,point.y],last_height=height,last_state=state,last_phase=phase)
    except Exception:
        error=traceback.format_exc()
        if error not in errors:errors.append(error)

def complete(ok,detail):
    if errors:ok=False;detail+='; benchmark observer failed: '+str(errors)
    for row in current['vehicles'].values():
        for key in ('moving_speeds_cm_s','lift_up_cm_s','lift_down_cm_s','cruise_empty','cruise_loaded'):
            if not isinstance(row[key],list):continue
            values=sorted(row.pop(key))
            row[key]=dict(count=len(values),median=values[len(values)//2] if values else None,
                p95=values[min(len(values)-1,int((len(values)-1)*.95))] if values else None,
                maximum=values[-1] if values else None)
    current.update(passed=ok,detail=detail,jobs=env['jobs'])
    env['case']['transport_benchmark']=current
    with (out/(str(current['id'])+'_Trace.csv')).open('w',encoding='utf-8',newline='') as f:
        writer=csv.writer(f);writer.writerow(['t_sim_s','vehicle','state','x_cm','y_cm','z_cm','forward_cm_s',
            'planar_cm_s','yaw_rate_deg_s','fork_height_cm','supported_pallet','planning_margin_cm',
            'traffic_waiting','traffic_yielding','traffic_status']);writer.writerows(traces)
    (out/'Benchmark.json').write_text(json.dumps(measurements,ensure_ascii=False,indent=2),encoding='utf-8')
    old_complete(ok,detail)

def finish():
    unreal.unregister_slate_post_tick_callback(observer)
    old_finish()

env.update(setup=setup,complete_case=complete,finish=finish)
observer=unreal.register_slate_post_tick_callback(observe)
