"""Native pickup alternatives and explicit blocker/defer regression.

Run with -BenchGroup=flat -WorkLoads=50 -BenchRepeats=1 -SpeedProfiles=6
-WorkTopChannels=32 -WorkTopRangeCm=3000 -WorkGraceCm=0 -HandlingStages=1
-Clearance=1 -PickupRun=name. All scene edits precede PIE; real motors, contacts,
fork engagement, dynamic freight and final unload perform each transport.
"""
from pathlib import Path
import json,math,re,traceback,unreal

root=Path(unreal.Paths.project_dir()).resolve()
command=unreal.SystemLibrary.get_command_line()
path=root/'Scripts/verify_work_speed_pie.py'
work=dict(__file__=str(path),__name__='__pickup_reposition__')
exec(compile(path.read_text(encoding='utf-8-sig'),str(path),'exec'),work)
env=work['env']
run=re.search(r'-PickupRun=([A-Za-z0-9_-]+)',command,re.I)
out=root/'Saved/Testing/PickupApproach_20261103'/(run.group(1) if run else 'Transport')
out.mkdir(parents=True,exist_ok=True)
work['out']=out;work['diag']['out']=out;work['bench']['out']=out
work['bench']['scope']['out']=out;work['bench']['scope']['scope']['OUT']=out
env['output']=out/'NativeReport.json'
base=next(c for c in env['CASES'] if c.get('load_kg')==50)
scenarios=['normal','closer_standoff','blocked_goal','rotated_pallet']
env['CASES']=[dict(base,id=base['id']+20000*i,name=name,scenario=name,
                   expect='deferred' if name=='blocked_goal' else 'transport')
              for i,name in enumerate(scenarios)]
old_setup,old_complete,old_finish=env['setup'],env['complete_case'],env['finish']
rows=[];defer_at=None;defer_position=None;last_time=-1


def setup(definition):
    global defer_at,defer_position,last_time
    record=old_setup(definition)
    defer_at=defer_position=None;last_time=-1
    actors=env['actors'];lookup={a.get_actor_label():a for a in actors.get_all_level_actors()}
    job=env['jobs'][0];pallet=lookup[job['pallet']]
    source=pallet.get_actor_location()
    if definition['scenario'] in ('closer_standoff','blocked_goal'):
        tight=definition['scenario']=='closer_standoff'
        offset=-340 if tight else -235
        label='ApproachTestColumn' if tight else 'ApproachTestWall'
        a=actors.spawn_actor_from_class(unreal.StaticMeshActor,source+unreal.Vector(offset,0,120))
        a.set_actor_label(label);a.tags=['WarehouseMapStructure']
        a.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
        a.static_mesh_component.set_collision_profile_name('BlockAll')
        a.set_actor_scale3d(unreal.Vector(.20,.40,2.40) if tight else unreal.Vector(1.90,4,2.40))
    if definition['scenario']=='rotated_pallet':
        angle=math.radians(35)
        for name in [job['pallet']]+job['cargo']:
            actor=lookup[name];delta=actor.get_actor_location()-source;rotation=actor.get_actor_rotation()
            actor.set_actor_location(source+unreal.Vector(delta.x*math.cos(angle)-delta.y*math.sin(angle),
                delta.x*math.sin(angle)+delta.y*math.cos(angle),delta.z),False,True)
            rotation.yaw+=35;actor.set_actor_rotation(rotation,True)
    return record


def complete(ok,detail):
    definition=env['CASES'][env['index']]
    vehicle=(env.get('live') or {}).get(env['jobs'][0]['vehicle'])
    reports=[dict(result=r.result,detail=r.detail,t=r.simulation_seconds) for r in vehicle.job_reports] if vehicle else []
    if ok and definition['scenario']=='closer_standoff' and not any(r['result']=='APPROACH_REPOSITIONED' for r in reports):
        ok=False;detail+='; alternate standoff was not exercised'
    rows.append(dict(scenario=definition['scenario'],passed=bool(ok),detail=detail,reports=reports,
                     recovery_count=vehicle.recovery_count if vehicle else None,
                     fork_insertion_retries=vehicle.fork_insertion_retries if vehicle else None))
    (out/'RepositionResults.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    old_complete(ok,detail)


def observe(dt):
    global defer_at,defer_position,last_time
    if not env.get('live') or not env.get('case') or env['case'].get('expectation')!='deferred':return
    if work['bench']['scope']['scope'].get('in_tick'):return
    try:
        game=env['editor'].get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if now==last_time:return
        last_time=now
        vehicle=env['live'][env['jobs'][0]['vehicle']]
        deferred=next((r for r in vehicle.job_reports if r.result=='DEFERRED'),None)
        if not deferred:return
        assert 'ApproachTestWall' in deferred.detail,('Blocker name missing',deferred.detail)
        if defer_at is None:defer_at=now;defer_position=vehicle.get_actor_location()
        assert (vehicle.get_actor_location()-defer_position).length()<3,'Vehicle moved into a blocked goal'
        assert vehicle.root_component.is_simulating_physics() and vehicle.get_actor_enable_collision()
        assert not vehicle.completed_job_ids,'Impossible task was reported completed'
        if now-defer_at>=3:
            env['complete_case'](True,'Named fixed wall deferred; real brake held 3s; no false completion')
    except Exception:
        env['complete_case'](False,traceback.format_exc())


def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    old_finish()


env.update(setup=setup,complete_case=complete,finish=finish)
handle=unreal.register_slate_post_tick_callback(observe)
