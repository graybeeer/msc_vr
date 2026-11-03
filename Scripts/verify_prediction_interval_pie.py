"""Verify frame-bounded prediction / 1s scans and real native workflows; never drives body poses.

-PredictionCase=transport/remote/layout/elevator; transport accepts the existing
work-speed fixture arguments. Use -SkipCapture -nullrhi for the elevator case.
An optional -PredictionRun=name preserves a separate run beneath that case.
-PredictionRates=2,50 repeats identical transport cases at both target rates.
"""
from pathlib import Path
import json,re,traceback,unreal

root=Path(unreal.Paths.project_dir()).resolve()
command=unreal.SystemLibrary.get_command_line()
match=re.search(r'-PredictionCase=(transport|remote|layout|elevator)',command,re.I)
case=match.group(1).lower() if match else 'transport'
rate_match=re.search(r'-PredictionRates=([0-9.,]+)',command,re.I)
rates=[float(v) for v in rate_match.group(1).split(',')] if rate_match else [50.]
assert rates and all(1<=v<=200 for v in rates) and len(set(rates))==len(rates)
active_rate=rates[0]
unreal.SystemLibrary.execute_console_command(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),
    'Warehouse.Localization.PredictionHz '+str(active_rate))
out=root/'Saved/Testing/PredictionRate_20261101'/case
run=re.search(r'-PredictionRun=([A-Za-z0-9_-]+)',command,re.I)
if run:out=out/run.group(1)
out.mkdir(parents=True,exist_ok=True)
filename=('verify_work_speed_pie.py' if case=='transport' else
          'verify_mapped_elevator_transport_pie.py' if case=='elevator' else 'verify_estimated_navigation_pie.py')
source=root/'Scripts'/filename
script=source.read_text(encoding='utf-8-sig')
script=script.replace("root/'Saved/Testing/EstimatedNavigation_20261031'/case",
                      '_prediction_output')
script=script.replace("root/'Saved/Testing/WorkSpeed_20261031/LoadedFloor32Final'",'_prediction_output')
scope=dict(__file__=str(source),__name__='__prediction_interval__',_prediction_output=out)
exec(compile(script,str(source),'exec'),scope)
if case=='transport':
    scope['out']=out;scope['diag']['out']=out;scope['bench']['out']=out
    scope['bench']['scope']['out']=out;scope['bench']['scope']['scope']['OUT']=out
native=scope['ns'] if case=='elevator' else scope['env']
if case=='transport':native['output']=out/'NativeReport.json'
if case=='transport':
    native['CASES']=[dict(c,id=c['id']+1000000*i,prediction_hz=rate,name=c['name']+'_prediction'+str(rate))
                     for i,rate in enumerate(rates) for c in native['CASES']]
    old_setup=native['setup']
    def setup(definition):
        global active_rate
        active_rate=definition['prediction_hz']
        unreal.SystemLibrary.execute_console_command(native['editor'].get_editor_world(),
            'Warehouse.Localization.PredictionHz '+str(active_rate))
        return old_setup(definition)
    native['setup']=setup
else:assert len(rates)==1,'Only transport can compare multiple rates in one process'
original_finish=native['finish']
rows={};previous={};errors=[];world=None;world_index=0;done=False

def observe(dt):
    global world,world_index
    if done:return
    try:
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        if game!=world:world=game;world_index+=1
        now=unreal.GameplayStatics.get_time_seconds(game)
        for v in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseForklift):
            key=str(world_index)+':'+v.get_actor_label()
            before=previous.get(key)
            if before and before['now']==now:continue
            loc=v.get_component_by_class(unreal.AgvForkliftLocalizerComponent)
            kit=v.get_component_by_class(unreal.AgvShadowSensorsComponent)
            count=loc.prediction_updates;stamp=loc.last_prediction_seconds
            state=str(v.ai_state);pose=v.navigation_transform()
            point=pose.translation;planar=[point.x,point.y,pose.rotation.rotator().yaw]
            row=rows.setdefault(key,dict(target_hz=active_rate,updates=[],held_frames=0,remote_frames=0,max_error_cm=0,odometry_only_updates=0))
            row['max_error_cm']=max(row['max_error_cm'],loc.max_error_cm)
            remote=v.is_remote_controlled()
            precise=any(s in state for s in ('ALIGN_UNLOAD','ALIGN_VEHICLE','ELEVATOR_APPROACH','NAVIGATE_CHARGE','DOCK_CHARGE','ELEVATOR_RIDE'))
            held=not remote and not precise and loc.map_builds>0 and not loc.status_text.startswith('NO MAP')
            reset=before and (loc.reinitializations!=before['reinit'] or stamp<0<=before['stamp'])
            if count!=(before['count'] if before else 0):
                assert not before or count==before['count']+1 or count==0,('Prediction burst',key,count,before['count'])
                if count and stamp>=0:
                    if row['updates'] and not reset and before and before['stamp']>=0:
                        gap=stamp-before['stamp']
                        assert gap>0,('Nonpositive prediction interval',key,gap)
                        if not remote and not before['remote'] and 'ELEVATOR_RIDE' not in state+before['state']:
                            assert gap<=1/active_rate+2*(now-before['now'])+.001,('Prediction missed its deadline plus frame rounding',key,gap,active_rate)
                    assert loc.last_prediction_delta_seconds>0,('Nonpositive prediction window',key)
                    has_scan=bool(kit and kit.snapshot_count and abs(kit.last_snapshot_seconds-now)<.001)
                    row['odometry_only_updates']+=int(not has_scan)
                    row['updates'].append(dict(at=stamp,window_s=loc.last_prediction_delta_seconds,has_new_scan=has_scan))
            elif before and held and before['held'] and state==before['state'] and not reset:
                delta=((planar[0]-before['pose'][0])**2+(planar[1]-before['pose'][1])**2)**.5
                yaw=abs((planar[2]-before['pose'][2]+180)%360-180)
                assert delta<.001 and yaw<.001,('Estimate changed between updates',key,delta,yaw,state)
                row['held_frames']+=1
            if remote and loc.status_text=='REMOTE - LOCALIZATION SUSPENDED':
                counters=[count,kit.snapshot_count if kit else 0,loc.accepted_matches,loc.rejected_matches]
                baseline=row.setdefault('remote_baseline',counters)
                assert counters==baseline,('Remote prediction/sensing active',key,counters,baseline)
                row['remote_frames']+=1
            else:row.pop('remote_baseline',None)
            if (kit and before and kit.snapshot_count!=before['scans'] and before['scan_time']>=0 and not reset
                    and before['scan_valid'] and kit.map_revision==before['map_revision']
                    and abs(kit.snapshot_floor_z_cm-before['scan_floor'])<50):
                assert kit.last_snapshot_seconds-before['scan_time']>=.999,('Scans faster than 1s',key,kit.last_snapshot_seconds,before['scan_time'])
            previous[key]=dict(now=now,count=count,stamp=stamp,reinit=loc.reinitializations,
                               pose=planar,state=state,held=held,remote=remote,
                               scans=kit.snapshot_count if kit else 0,scan_time=kit.last_snapshot_seconds if kit else -1,
                               scan_valid=bool(kit and kit.snapshot_valid),map_revision=kit.map_revision if kit else -1,
                               scan_floor=kit.snapshot_floor_z_cm if kit else 0)
    except Exception:
        error=traceback.format_exc()
        if error not in errors:
            errors.append(error)
            (out/'PredictionErrors.json').write_text(json.dumps(errors,ensure_ascii=False,indent=2),encoding='utf-8')

def finish(*args,**kwargs):
    global done
    if not done:
        done=True;unreal.unregister_slate_post_tick_callback(handle)
        if not any(row['updates'] for row in rows.values()):errors.append('No prediction updates observed')
        if not any(row['odometry_only_updates']>1 for row in rows.values()):errors.append('No prediction between one-second scans observed')
        if case=='remote' and not any(row['remote_frames'] for row in rows.values()):errors.append('No remote suspension frames observed')
        if case=='transport' and any(row['target_hz']<=2 for row in rows.values()) and not any(row['held_frames']>30 for row in rows.values()):errors.append('No held-pose interval observed at 2Hz')
        for row in rows.values():
            windows=[u['window_s'] for u in row['updates']]
            row['observed_hz']=len(windows)/sum(windows) if windows else 0
            if windows and row['observed_hz']<min(row['target_hz'],30)*.8:errors.append('Prediction rate below frame-bounded target: '+str(row['observed_hz']))
        (out/'PredictionTiming.json').write_text(json.dumps(dict(passed=not errors,case=case,
            errors=errors,vehicles=rows,runtime_motion_forced=False),ensure_ascii=False,indent=2),encoding='utf-8')
    return original_finish(*args,**kwargs)

native['finish']=finish
handle=unreal.register_slate_post_tick_callback(observe)
