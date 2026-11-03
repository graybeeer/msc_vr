from pathlib import Path
import json,traceback,unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
root=Path(unreal.Paths.project_dir()).resolve()
source=root/'Scripts/verify_elevator_route_visuals_pie.py'
ns=dict(__file__=str(source),__name__='__loaded_floor_benchmark__')
exec(compile(source.read_text(encoding='utf-8-sig'),str(source),'exec'),ns)
output=root/'Saved/Testing/WorkSpeed_20261031/LoadedFloor32Final'
output.mkdir(parents=True,exist_ok=True);ns['out']=output
for label,point,size in [('W',(-2100,0,700),(20,4220,1400)),('E',(2100,0,700),(20,4220,1400)),('S',(0,-2100,700),(4200,20,1400)),('N',(0,2100,700),(4200,20,1400))]:
    a=ns['spawn'](unreal.StaticMeshActor,'BenchmarkWall'+label,point)
    a.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    a.static_mesh_component.set_collision_profile_name('BlockAll')
    a.set_actor_scale3d(unreal.Vector(*size)/100);a.tags=['WarehouseMapStructure']
original_finish=ns['finish']
def finish():
    v=ns.get('vehicle');lift=ns.get('lift')
    failure=next((r.get('error') for r in ns['report'] if r.get('passed') is False),None)
    ok=not failure and v is not None and ns['JOB_ID'] in v.completed_job_ids and lift.completed_transfers==2
    data=dict(passed=ok,detail=failure or 'Native empty L1 to L3, 37kg loaded L3 to L2, actual unload/fork withdrawal and two transfers',runtime_motion_forced=False)
    if v:
        loc=v.get_component_by_class(unreal.AgvForkliftLocalizerComponent)
        data['localization']=dict(accepted=loc.accepted_matches,rejected=loc.rejected_matches,map_builds=loc.map_builds,max_error_cm=loc.max_error_cm,status=loc.status_text)
        reports=[r for r in v.job_reports if r.job_id==ns['JOB_ID']]
        data['events']=[dict(t=r.simulation_seconds,result=r.result,detail=r.detail) for r in reports]
        accepted=next((r.simulation_seconds for r in reports if r.result=='ACCEPTED'),None)
        done=next((r.simulation_seconds for r in reports if r.result=='COMPLETED'),None)
        data['accepted_to_complete_s']=done-accepted if done is not None and accepted is not None else None
        if ok and loc.accepted_matches==0:data.update(passed=False,detail='No mapped localization matches')
    (output/'Status.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
    original_finish()
ns['finish']=finish
import csv,time
trace_rows=[];trace_stamps={};trace_written=0;trace_errors=[]
trace_path=output/'Motion.csv'
with trace_path.open('w',encoding='utf-8',newline='') as f:
    csv.writer(f).writerow(['t_sim_s','vehicle','state','x_cm','y_cm','z_cm','forward_cm_s','planar_cm_s','fork_height_cm','loaded'])
def trace_motion(dt):
    global trace_written
    try:
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        for v in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseForklift):
            name=v.get_actor_label();last=trace_stamps.get(name,-1)
            if now>=last and now-last<.099:continue
            trace_stamps[name]=now;p=v.get_actor_location();velocity=v.root_component.get_physics_linear_velocity()
            trace_rows.append([now,name,str(v.ai_state),p.x,p.y,p.z,abs(v.current_speed_cm),(velocity.x**2+velocity.y**2)**.5,v.get_fork_height_cm(),v.get_supported_pallet() is not None])
        if time.monotonic()-trace_written>1:
            with trace_path.open('a',encoding='utf-8',newline='') as f:csv.writer(f).writerows(trace_rows)
            trace_rows.clear();trace_written=time.monotonic()
    except Exception:
        error=traceback.format_exc()
        if error not in trace_errors:
            trace_errors.append(error);(output/'MeasureErrors.json').write_text(json.dumps(trace_errors),encoding='utf-8')
trace_observer=unreal.register_slate_post_tick_callback(trace_motion)

# Read-only per-frame deck/vehicle diagnostics; never an input to control.
deck_rows=[];deck_parts={};deck_stamp=-1
previous_finish=ns['finish']
def deck_observer(dt):
 global deck_parts,deck_stamp
 game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 if not game:return
 now=unreal.GameplayStatics.get_time_seconds(game)
 if now==deck_stamp:return
 v=ns.get('vehicle');e=ns.get('lift')
 if not v or not e:return
 deck_stamp=now
 if not deck_parts:
  deck_parts={c.get_name():c for c in v.get_components_by_class(unreal.SceneComponent)}
  deck_parts['deck']=next(c for c in e.get_components_by_class(unreal.StaticMeshComponent) if c.get_name()=='LiftPlatform')
 body=v.root_component;deck=deck_parts['deck'];point=v.get_actor_location();rotation=v.get_actor_rotation()
 linear=body.get_physics_linear_velocity();angular=body.get_physics_angular_velocity_in_degrees()
 guide=v.get_actor_transform().inverse_transform_location(deck_parts['Carriage'].get_world_location())
 deck_rows.append([now,str(v.ai_state),point.z,rotation.pitch,rotation.yaw,rotation.roll,linear.x,linear.y,linear.z,angular.z,deck.get_world_location().z,deck.get_physics_linear_velocity().z,guide.x,guide.y,guide.z])
def finish_deck():
 unreal.unregister_slate_post_tick_callback(deck_handle)
 with (output/'Deck.csv').open('w',encoding='utf-8',newline='') as f:
  writer=csv.writer(f);writer.writerow(['t','state','vehicle_z','pitch','yaw','roll','vx','vy','vz','yaw_rate','deck_z','deck_vz','guide_x','guide_y','guide_z']);writer.writerows(deck_rows)
 previous_finish()
ns['finish']=finish_deck
deck_handle=unreal.register_slate_post_tick_callback(deck_observer)
