"""Read-only sensor, odometry, lift and load diagnostics on native transport fixtures.

-BenchGroup=flat -BenchRepeats=2 -CalibrationRun=name; -CalibrationLoads=50
optionally selects masses. Keeps production settings, 1Hz scans and finite
force/contact. Never forces runtime body motion.
Rejected candidate results are documented in AUTONOMY.md, 2026-10-31.
"""
from pathlib import Path
import csv,json,re,traceback,unreal

root=Path(unreal.Paths.project_dir()).resolve()
command=unreal.SystemLibrary.get_command_line()
source=root/'Scripts/verify_transport_benchmark_pie.py'
bench=dict(__file__=str(source),__name__='__sensor_calibration__')
exec(compile(source.read_text(encoding='utf-8-sig'),str(source),'exec'),bench)
env=bench['env']
match=re.search(r'-CalibrationRun=([A-Za-z0-9_-]+)',command,re.I)
out=root/'Saved/Testing/SensorCalibration_20261031'/(match.group(1) if match else 'Compare')
out.mkdir(parents=True,exist_ok=True)
bench['out']=out;bench['scope']['out']=out;bench['scope']['scope']['OUT']=out
env['output']=out/'NativeReport.json'
match=re.search(r'-CalibrationLoads=([0-9,]+)',command,re.I)
if match:
    loads={int(n) for n in match.group(1).split(',')}
    env['CASES']=[c for c in env['CASES'] if c.get('load_kg') in loads]
    assert env['CASES'],'No matching authored load conditions'
env['CASES']=[dict(c,wall_limit=1800) for c in env['CASES']]
old_setup,old_complete,old_finish=env['setup'],env['complete_case'],env['finish']
current={};measurements={};rows=[];errors=[];parts={}

def setup(definition):
    global current,rows,parts
    record=old_setup(definition)
    current=measurements[str(definition['id'])]=dict(settings='production',errors=[],sensors=[],last_snapshot={},last_t=-1)
    record['calibration']=current
    rows=[];parts={};errors.clear()
    return record

def observe(dt):
    try:
        if bench['scope']['scope'].get('in_tick') or not env.get('live') or not current or current.get('finished'):return
        game=env['editor'].get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if now-current['last_t']<.099:return
        current['last_t']=now
        for name in env['runtime']['vehicles']:
            v=env['live'][name]
            if name not in parts:parts[name]={c.get_name():c for c in v.get_components_by_class(unreal.SceneComponent)}
            components=parts[name];body=v.root_component
            if name not in current.setdefault('rig',{}):
                frame=v.get_actor_transform();metadata={}
                vector=lambda point:[point.x,point.y,point.z]
                for key in ('Body','DriveWheel','LoadWheelL','LoadWheelR','Carriage'):
                    c=components.get(key)
                    if c:
                        lo,hi=c.get_local_bounds()
                        metadata[key]=dict(origin=vector(frame.inverse_transform_location(c.get_world_location())),
                            com=vector(frame.inverse_transform_location(c.get_center_of_mass())),
                            geometric_center=vector(frame.inverse_transform_location(c.get_world_transform().transform_location((lo+hi)*.5))),
                            local_bounds=[vector(lo),vector(hi)],mass_kg=c.get_mass())
                current['rig'][name]=metadata
            loc=v.get_component_by_class(unreal.AgvForkliftLocalizerComponent)
            rotation=v.get_actor_rotation();pallet=v.active_job.pallet
            bounds=unreal.WarehouseDamageSystem.get_physical_bounds(pallet) if pallet else None
            pallet_rotation=pallet.get_actor_rotation() if pallet else unreal.Rotator()
            body_spin=body.get_physics_angular_velocity_in_radians()
            spins=[]
            for wheel in ('DriveWheel','LoadWheelL','LoadWheelR'):
                c=components[wheel];delta=c.get_physics_angular_velocity_in_radians()-body_spin
                axis=c.get_right_vector();spins.append(delta.x*axis.x+delta.y*axis.y+delta.z*axis.z)
            force=None
            try:
                f,_=components['CarriageHydraulicJoint'].get_constraint_force();force=f.z
            except Exception:pass # Optional raw force diagnostic; not an input to control or a pass condition.
            rows.append([now,name,str(v.ai_state),v.status,v.current_speed_cm,rotation.pitch,rotation.roll,
                v.get_fork_height_cm(),bounds.min.z if bounds and bounds.is_valid else None,
                pallet_rotation.pitch,pallet_rotation.roll,loc.error_cm,loc.travel_since_match_cm,
                loc.last_match_points,loc.last_inlier_ratio,*spins,force])
            kit=v.get_component_by_class(unreal.AgvShadowSensorsComponent)
            if kit and current['last_snapshot'].get(name)!=kit.snapshot_count:
                current['last_snapshot'][name]=kit.snapshot_count
                current.setdefault('snapshot_costs',[]).append(dict(t=now,vehicle=name,
                    milliseconds=kit.last_sensor_milliseconds,beams=kit.last_beam_count))
                for sensor in v.get_components_by_class(unreal.AgvLidarComponent):
                    current['sensors'].append(dict(t=now,vehicle=name,sensor=sensor.get_name(),
                        beams=sensor.last_scan_beams,returns=sensor.last_scan_points,self_blocked=sensor.last_scan_self_blocked,
                        max_range_cm=sensor.max_range_cm))
    except Exception:
        error=traceback.format_exc()
        if error not in errors:errors.append(error)

def complete(ok,detail):
    current['finished']=True;current['errors']=list(errors)
    if errors:ok=False;detail+='; calibration monitor failed: '+str(errors)
    with (out/(str(env['case']['id'])+'_Mechanics.csv')).open('w',encoding='utf-8',newline='') as f:
        writer=csv.writer(f);writer.writerow(['t_sim_s','vehicle','state','status','forward_cm_s','body_pitch_deg','body_roll_deg',
            'fork_height_cm','pallet_min_z_cm','pallet_pitch_deg','pallet_roll_deg','estimated_error_cm','unmatched_odom_cm',
            'match_points','inlier_ratio','drive_spin_rad_s','left_spin_rad_s','right_spin_rad_s','joint_force_z_raw']);writer.writerows(rows)
    (out/'Calibration.json').write_text(json.dumps(measurements,ensure_ascii=False,indent=2),encoding='utf-8')
    old_complete(ok,detail)

def finish():
    unreal.unregister_slate_post_tick_callback(observer)
    old_finish()

env.update(setup=setup,complete_case=complete,finish=finish)
observer=unreal.register_slate_post_tick_callback(observe)
