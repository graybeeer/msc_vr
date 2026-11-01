"""Compare autonomous work speeds on existing native, mapped transport fixtures.

-SpeedProfiles=0,6 -SpeedRun=name; normal benchmark arguments apply.
-BenchGroup=flat -WorkLoads=50,100,200 -BenchRepeats=2 shares shape and seeds.
Profiles 0..5 compare increasing speeds; 6/7 add bounded floor-docking
tolerance to 3/4. Upper LiDAR rows/range and optional odometry allowance use
-WorkTopChannels=16,32 -WorkTopRangeCm=3000 -WorkGraceCm=0 before PIE.
Fixture setup precedes PIE. Runtime AGV/load poses, velocities, completion,
physics, sensor state and damage limits are never assigned by this driver.
"""
from pathlib import Path
import json,re,unreal

root=Path(unreal.Paths.project_dir()).resolve()
command=unreal.SystemLibrary.get_command_line()
source=root/'Scripts/verify_sensor_calibration_pie.py'
diag=dict(__file__=str(source),__name__='__work_speed__')
exec(compile(source.read_text(encoding='utf-8-sig'),str(source),'exec'),diag)
env=diag['env'];bench=diag['bench']
name=re.search(r'-SpeedRun=([A-Za-z0-9_-]+)',command,re.I)
out=root/'Saved/Testing/WorkSpeed_20261031'/(name.group(1) if name else 'Compare')
out.mkdir(parents=True,exist_ok=True)
diag['out']=out;bench['out']=out;bench['scope']['out']=out;bench['scope']['scope']['OUT']=out
env['output']=out/'NativeReport.json'
match=re.search(r'-SpeedProfiles=([0-7,]+)',command,re.I)
profiles=[int(n) for n in match.group(1).split(',')] if match else [0,6]
assert profiles and len(set(profiles))==len(profiles)
match=re.search(r'-WorkLoads=([0-9,]+)',command,re.I)
if match:
    loads=[int(n) for n in match.group(1).split(',')]
    assert loads and all(25<n<=1400 for n in loads)
    templates=[c for c in env['CASES'] if c.get('load_kg')==50]
    assert templates,'WorkLoads requires the flat benchmark fixtures'
    env['CASES']=[dict(c,id=c['id']+i,load_kg=mass,name=f"flat_14m_{mass}kg_seed{c['seed']}")
                  for c in templates for i,mass in enumerate(loads)]
env['CASES']=[dict(c,id=c['id']+1000*p,speed_profile=p,
                  name=c['name']+'_speed'+str(p)) for p in profiles for c in env['CASES']]
match=re.search(r'-WorkGraceCm=([0-9,]+)',command,re.I)
if match:
    grace=[int(n) for n in match.group(1).split(',')]
    assert grace and all(0<=n<=2000 for n in grace)
    env['CASES']=[dict(c,id=c['id']+10000*i,straight_grace_cm=n,name=c['name']+'_grace'+str(n))
                  for i,n in enumerate(grace) for c in env['CASES']]
match=re.search(r'-WorkTopChannels=([0-9,]+)',command,re.I)
if match:
    channels=[int(n) for n in match.group(1).split(',')]
    assert channels and all(8<=n<=64 for n in channels)
    env['CASES']=[dict(c,id=c['id']+100000*i,top_channels=n,name=c['name']+'_channels'+str(n))
                  for i,n in enumerate(channels) for c in env['CASES']]
old_setup,old_complete=env['setup'],env['complete_case']
results=[]
match=re.search(r'-WorkTopRangeCm=([0-9]+)',command,re.I)
top_range=int(match.group(1)) if match else None
assert top_range is None or 500<=top_range<=7500

def setup(definition):
    unreal.SystemLibrary.execute_console_command(env['editor'].get_editor_world(),
        'Warehouse.WorkSpeed '+str(definition['speed_profile']))
    if 'straight_grace_cm' in definition:
        unreal.SystemLibrary.execute_console_command(env['editor'].get_editor_world(),
            'Warehouse.Localization.StraightGraceCm '+str(definition['straight_grace_cm']))
    if 'top_channels' in definition:
        unreal.SystemLibrary.execute_console_command(env['editor'].get_editor_world(),
            'Warehouse.WorkTopChannels '+str(definition['top_channels']))
    if top_range is not None:
        unreal.SystemLibrary.execute_console_command(env['editor'].get_editor_world(),
            'Warehouse.WorkTopRangeCm '+str(top_range))
    record=old_setup(definition)
    diag['current']['settings']='work-speed comparison'
    record['speed_profile']=definition['speed_profile']
    record['straight_grace_cm']=definition.get('straight_grace_cm')
    record['top_channels']=definition.get('top_channels')
    return record

def complete(ok,detail):
    record=env['case']
    if diag['errors']:ok=False;detail+='; monitor errors: '+str(diag['errors'])
    if top_range is not None:
        actual={s['max_range_cm'] for s in diag['current']['sensors'] if s['sensor']=='AgvShadowTopLidar' and s['beams']>0}
        if actual!={top_range}:ok=False;detail+='; incorrect sensor hardware range: '+str(actual)
    if record.get('top_channels') is not None:
        actual={s['beams'] for s in diag['current']['sensors'] if s['sensor']=='AgvShadowTopLidar' and s['beams']>0}
        if actual!={450*record['top_channels']}:ok=False;detail+='; incorrect sensor beam count: '+str(actual)
    carrying=[r[8] for r in diag['rows'] if 'NAVIGATE_DELIVERY' in r[2] and r[8] is not None]
    # Flat fixtures have a zero floor datum. Reject dragging as a speed win,
    # while keeping the unmodified native completion/failure report separate.
    clearance=min(carrying) if carrying and 'load_kg' in env['CASES'][env['index']] else None
    results.append(dict(id=record['id'],profile=record['speed_profile'],ok=bool(ok),detail=detail,
        requested_top_range_cm=top_range,
        requested_straight_grace_cm=record.get('straight_grace_cm'),
        requested_top_channels=record.get('top_channels'),
        flat_carry_min_z_cm=clearance,qualified=bool(ok) and (clearance is None or clearance>=2),
        jobs=[dict(vehicle=j['vehicle'],submitted=j.get('submitted'),completed=j.get('completed'),
            duration_s=j['completed']-j['submitted'] if j.get('completed') is not None else None)
            for j in env['jobs']]))
    (out/'SpeedResults.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    old_complete(ok,detail)

old_finish=env['finish']
def observe_failure(dt):
    if not env.get('live') or diag['current'].get('finished') or bench['scope']['scope'].get('in_tick'):return
    if env['case'].get('expectation')!='transport':return
    for job in env['jobs']:
        if not job.get('job_id') or job.get('completed') is not None:continue
        v=env['live'].get(job['vehicle'])
        if not v:continue
        deferred=next((r for r in v.job_reports if r.job_id==job['job_id'] and r.result=='DEFERRED'),None)
        if deferred:
            env['complete_case'](False,'Native task deferred: '+deferred.detail)
            return
def finish():
    unreal.unregister_slate_post_tick_callback(failure_observer)
    old_finish()
env.update(setup=setup,complete_case=complete,finish=finish)
failure_observer=unreal.register_slate_post_tick_callback(observe_failure)
