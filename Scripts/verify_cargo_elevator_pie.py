"""Production AGV/model in an unsaved native three-floor test. No runtime teleporting."""
import json,time,traceback
from pathlib import Path
import unreal
try:
    import warehouse_elevator_tools
except ModuleNotFoundError:
    from types import SimpleNamespace
    warehouse_elevator_tools=SimpleNamespace(_test=None)
if warehouse_elevator_tools._test:
    try:unreal.unregister_slate_post_tick_callback(warehouse_elevator_tools._test['handle'])
    except Exception:pass
ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'Saved/Testing/CargoElevator';OUT.mkdir(parents=True,exist_ok=True)
case_path=OUT/'TestCase.json'
case=json.loads(case_path.read_text(encoding='utf-8')) if case_path.exists() else {'from_floor':0,'to_floor':1}
from_floor,to_floor=case['from_floor'],case['to_floor']
floor_count=max(3,from_floor+1,to_floor+1)
assert floor_count<=4
saved_map=case.get('saved_map',False)
capture='-nullrhi' not in unreal.SystemLibrary.get_command_line().lower()
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not editor.get_game_world()
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
source=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WH_AutonomousForklift')
parts={p.get_name():(p.static_mesh,[p.get_material(i) for i in range(p.get_num_materials())]) for p in source.get_components_by_class(unreal.StaticMeshComponent)}
poses={p.get_name():unreal.MathLibrary.make_relative_transform(p.get_world_transform(),source.get_actor_transform()) for p in source.get_components_by_class(unreal.SceneComponent) if p!=source.root_component}
properties={k:source.get_editor_property(k) for k in ('empty_travel_speed_cm','loaded_travel_speed_cm','fork_leading_speed_cm','rated_load_kg','max_fork_height_cm','vehicle_mass_kg','sensor_mode')}
saved_lift=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseElevator))
if not saved_map:
    assert level.load_level('/Engine/Maps/Entry')
    for a in list(actors.get_all_level_actors()):
        if not isinstance(a,(unreal.WorldSettings,unreal.LevelScriptActor)):actors.destroy_actor(a)
    settings=unreal.GameplayStatics.get_all_actors_of_class(editor.get_editor_world(),unreal.WorldSettings)[0]
    settings.set_editor_property('default_game_mode',unreal.GameModeBase)
    # GameModeBase otherwise spawns its default pawn at the cabin origin.
    actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(-1600,-800,120))
else:
    for a in actors.get_all_level_actors():
        if isinstance(a,unreal.WarehouseForklift):
            a.set_editor_property('auto_start',False);a.set_editor_property('pending_jobs',[])
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
if not any(isinstance(a,unreal.WarehouseSimulationClock) for a in actors.get_all_level_actors()):
    actors.spawn_actor_from_class(unreal.WarehouseSimulationClock,unreal.Vector(-1800,-900,0))
floors=[]
def block(label,point,size):
    a=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*point));a.set_actor_label(label.replace('LiftTest','LiftTestFloor'))
    a.static_mesh_component.set_static_mesh(cube);a.static_mesh_component.set_collision_profile_name('BlockAll')
    a.set_actor_scale3d(unreal.Vector(*(v/100 for v in size)))
    a.tags=[unreal.Name('WarehouseMapStructure')];floors.append(a)
    return a
# Match production holes, including 6 cm front clearance for Chaos contacts.
for floor in range(0 if saved_map else floor_count):
    lo,hi=(-181,181) if floor==0 else (-181,200)
    z=floor*400-10
    block('LiftTestWest'+str(floor),((-2000+lo)/2,0,z),(2000+lo,2000,20))
    block('LiftTestEast'+str(floor),((hi+1000)/2,0,z),(1000-hi,2000,20))
    for side in (-1,1):block('LiftTestSide'+str(floor)+str(side),((lo+hi)/2,side*570.5,z),(hi-lo,859,20))
if not saved_map:block('LiftTestPit',(0,0,-30),(352,282,20))
lift=saved_lift if saved_map else actors.spawn_actor_from_class(unreal.WarehouseElevator,unreal.Vector())
assert lift.set_floor_count(floor_count)
lift.set_actor_label('CargoLiftTest');lift.set_editor_property('use_cargo_model',case.get('model',True));lift.apply_cargo_model()
frame=lift.get_actor_transform()
v=actors.spawn_actor_from_class(unreal.WarehouseForklift,frame.transform_location(unreal.Vector(-500,0,from_floor*400)),lift.get_actor_rotation())
v.set_actor_label('CargoLiftAGV')
for c in v.get_components_by_class(unreal.StaticMeshComponent):
    mesh,mats=parts[c.get_name()];c.set_static_mesh(mesh)
    for i,m in enumerate(mats):c.set_material(i,m)
def depth(c):
    n=0
    while c.get_attach_parent():c=c.get_attach_parent();n+=1
    return n
for c in sorted(v.get_components_by_class(unreal.SceneComponent),key=depth):
    if c!=v.root_component:c.set_world_transform(unreal.MathLibrary.compose_transforms(poses[c.get_name()],v.get_actor_transform()),False,True)
for k,value in properties.items():v.set_editor_property(k,value)
for k,value in dict(auto_start=False,autonomous_mode=True,idle_parking_enabled=False,pending_jobs=[],target_pallet=None,charging_station=None,elevator=lift,battery_percent=100).items():v.set_editor_property(k,value)
pallet=actors.spawn_actor_from_class(unreal.WarehousePallet,frame.transform_location(unreal.Vector(-900,0,to_floor*400+.1)),lift.get_actor_rotation())
pallet.set_actor_label('CargoLiftPallet')
eye=frame.transform_location(unreal.Vector(-800,-650,340));target=frame.transform_location(unreal.Vector(-70,0,150))
camera=actors.spawn_actor_from_class(unreal.CameraActor,eye,unreal.MathLibrary.find_look_at_rotation(eye,target))
camera.set_actor_label('CargoLiftCamera');camera.camera_component.set_field_of_view(58)
if not saved_map:
    sun=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,1600),unreal.Rotator(pitch=-45,yaw=35))
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(4)
    actors.spawn_actor_from_class(unreal.SkyAtmosphere,unreal.Vector())
    actors.spawn_actor_from_class(unreal.WarehouseDamageSystem,unreal.Vector(-1800,-900,0))
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
report={'date':'2026-10-22','status':'starting','from_floor':from_floor+1,'to_floor':to_floor+1,'scope':'Empty production AGV boards, rides and exits in unsaved Entry PIE; native drives, original sensors, physics and PLC. Pickup after arrival is outside this case.',
        'runtime_teleports':False,'trips':[],'events':[]}
report['saved_map_geometry']=saved_map
report['cargo_model']=case.get('model',True)
report['rendered_capture']=capture
report['test_revision']='merged-four-floor-model-20261022'
report['floor_count']=floor_count
report['fixture_front_clearance_cm']=6 if not saved_map else None
report['boarding_samples']=[]
report['riding_samples']=[]
if saved_map:report['scope']='Saved warehouse PIE with an extra copy of the production AGV; existing jobs paused only in the unsaved editor test setup. Empty-vehicle boarding, native ride and exit; no runtime teleporting.'
started=time.monotonic();lastwrite=0;phase=0;vehicle=rig=load=None;trip=0;laststate=None;lastz=None
captured=set()
destinations=[to_floor*400]
deck_errors=[]
def write():
    (OUT/'TransferReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
write()
def finish(error=None):
    report['status']='failed' if error else 'passed'
    if error:report['error']=error
    prefix='SavedMap_' if saved_map else ''
    (OUT/f'{prefix}Transfer_L{from_floor+1}_L{to_floor+1}.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    write();unreal.unregister_slate_post_tick_callback(handle)
    if editor.get_game_world():unreal.SystemLibrary.execute_console_command(editor.get_game_world(),'Log LogTemp Log')
    level.editor_request_end_play()
    # Return to the saved warehouse after PIE ends; never save the test world.
    def restore(dt):
        if editor.get_game_world():return
        unreal.unregister_slate_post_tick_callback(restore_handle)
        level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    restore_handle=unreal.register_slate_post_tick_callback(restore)
def submit():
    job='CARGO-LIFT-'+str(trip)
    order=unreal.WarehouseWorkOrder(job_id=job,source_system='CARGO-LIFT-VALIDATION',pallet=load,destination=unreal.Transform(location=frame.transform_location(unreal.Vector(-600,0,destinations[trip])),rotation=lift.get_actor_rotation()))
    assert vehicle.submit_job(order),('Job rejected',vehicle.get_editor_property('status'))
    report['current_job']=job
def deck_overlaps(platform):
    hits=unreal.SystemLibrary.component_overlap_components(platform,platform.get_world_transform(),
        [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1,unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2],unreal.PrimitiveComponent,[vehicle])
    if isinstance(hits,tuple):hits=hits[-1]
    return [c.get_owner().get_actor_label()+'.'+c.get_name() for c in (hits or []) if c.get_name() not in ('LiftPlatform','Cargo_CabinShell')]
def tick(dt):
    global phase,vehicle,rig,load,trip,laststate,lastwrite,lastz
    try:
        assert time.monotonic()-started<720,'Trip test timeout'
        if phase==0:
            if time.monotonic()-started<1:return
            level.editor_request_begin_play();phase=1;return
        world=editor.get_game_world()
        if not world:return
        now=unreal.GameplayStatics.get_time_seconds(world)
        if vehicle is None:
            named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor)}
            vehicle=named['CargoLiftAGV'];rig=named['CargoLiftTest'];load=named['CargoLiftPallet']
            platform=next(c for c in rig.get_components_by_class(unreal.StaticMeshComponent) if c.get_name()=='LiftPlatform')
            assert platform.is_simulating_physics() and abs(platform.get_mass()-500)<.2,('Deck body or aggregate mass',platform.get_mass())
            report['moving_assembly_mass_kg']=platform.get_mass()
            report['actor_names']={a.get_name():a.get_actor_label() for a in named.values()}
        if phase==1:
            if now<3:return
            # Keep the production physics/controller timing at normal speed.
            unreal.GameplayStatics.set_global_time_dilation(world,1)
            report['global_time_dilation']=1
            clocks=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WarehouseSimulationClock)
            assert len(clocks)==1
            assert clocks[0].set_speed(case.get('speed',4))
            report['production_clock_speed']=case.get('speed',4)
            report['world_step_seconds']=1/30
            measured_step=unreal.GameplayStatics.get_world_delta_seconds(world)
            assert abs(measured_step-1/30)<1e-5,('Wrong production timestep',measured_step)
            report['measured_world_step_seconds']=measured_step
            if case.get('verbose_contacts',False):
                unreal.SystemLibrary.execute_console_command(world,'Log LogTemp Verbose')
            vehicle.toggle_power();submit();phase=2;report['status']='running'
        state=vehicle.get_editor_property('ai_state');liftstate=rig.get_editor_property('state')
        if liftstate==unreal.WarehouseElevatorState.TRAVELLING:
            assert rig.are_landing_doors_closed(),'Moving with an unclosed landing door'
        assert state!=unreal.WarehouseAIState.FAULT,(vehicle.get_editor_property('status'),rig.get_editor_property('status'))
        assert load.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
        assert not load.get_attach_parent_actor(),'Pallet attached to actor instead of physical contact'
        key=(str(state),str(liftstate))
        if key!=laststate:
            report['events'].append({'t':round(now,2),'vehicle':str(state),'lift':str(liftstate),'z':vehicle.get_actor_location().z})
            laststate=key
        if capture and state in (unreal.WarehouseAIState.ELEVATOR_RIDE,unreal.WarehouseAIState.ELEVATOR_EXIT) and str(state) not in captured:
            captured.add(str(state))
            cameras=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.CameraActor)
            preview=next((c for c in cameras if c.get_actor_label()=='CargoLiftCamera'),None)
            if preview:
                floor=from_floor if state==unreal.WarehouseAIState.ELEVATOR_RIDE else to_floor
                eye=frame.transform_location(unreal.Vector(-800,-650,340+floor*400))
                target=frame.transform_location(unreal.Vector(-70,0,150+floor*400))
                preview.set_actor_location(eye,False,True)
                preview.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye,target),False)
                # The warehouse player camera otherwise owns the PIE viewport;
                # AutomationLibrary's camera argument alone does not replace it.
                controller=unreal.GameplayStatics.get_player_controller(world,0)
                if controller:controller.set_view_target_with_blend(preview,0)
                stage='Boarded' if state==unreal.WarehouseAIState.ELEVATOR_RIDE else 'Exit'
                prefix='SavedMap_' if saved_map else ''
                unreal.AutomationLibrary.take_high_res_screenshot(1440,1000,str(OUT/f'{prefix}Physical_L{from_floor+1}_L{to_floor+1}_{stage}.png'),camera=preview,delay=.2)
        report['live']={'t':now,'vehicle':str(state),'vehicle_status':vehicle.get_editor_property('status'),'lift':str(liftstate),
            'lift_status':rig.get_editor_property('status'),'vehicle_position':str(vehicle.get_actor_location()),'pallet_position':str(load.get_actor_location()),
            'completed_transfers':rig.get_editor_property('completed_transfers')}
        report['live']['moving_parts']={c.get_name():[c.get_world_location().x,c.get_world_location().y,c.get_world_location().z] for c in rig.get_components_by_class(unreal.StaticMeshComponent) if c.get_name() in ('LiftPlatform','Cargo_CabinShell','Gate_0_-1','Gate_0_1','Gate_1_-1','Gate_1_1','Gate_2_-1','Gate_2_1')}
        deck=next(c for c in rig.get_components_by_class(unreal.StaticMeshComponent) if c.get_name()=='LiftPlatform')
        report['live']['deck_rotation']=str(deck.get_world_rotation())
        if state==unreal.WarehouseAIState.ELEVATOR_BOARD and (not report['boarding_samples'] or now-report['boarding_samples'][-1]['t']>.5):
            p=vehicle.get_actor_location();rotation=vehicle.get_actor_rotation()
            wheels={c.get_name():[c.get_world_location().x,c.get_world_location().y,c.get_world_location().z] for c in vehicle.get_components_by_class(unreal.StaticMeshComponent) if 'Wheel' in c.get_name()}
            dr=deck.get_world_rotation()
            report['boarding_samples'].append({'t':now,'position':[p.x,p.y,p.z],'rotation':[rotation.pitch,rotation.yaw,rotation.roll],'wheels':wheels,'deck_rotation':[dr.pitch,dr.yaw,dr.roll]})
        if state==unreal.WarehouseAIState.ELEVATOR_RIDE:
            platform=next(c for c in rig.get_components_by_class(unreal.StaticMeshComponent) if c.get_name()=='LiftPlatform')
            deck_errors.append(abs(vehicle.get_actor_location().z-platform.get_world_location().z-5))
            if not report['riding_samples'] or now-report['riding_samples'][-1]['t']>.1:
                joint=rig.get_components_by_class(unreal.PhysicsConstraintComponent)[0]
                drive=joint.get_editor_property('constraint_instance').get_editor_property('profile_instance').get_editor_property('linear_drive')
                report['riding_samples'].append({'t':now,'vehicle_z':vehicle.get_actor_location().z,'deck_z':platform.get_world_location().z+5,
                    'deck_vz':platform.get_physics_linear_velocity().z,'vehicle_velocity':str(vehicle.get_velocity()),'door_closed':rig.are_landing_doors_closed(),
                    'drive_target':str(drive.get_editor_property('position_target')),'overlaps':deck_overlaps(platform)})
            assert deck_errors[-1]<2,('Vehicle lost deck support',deck_errors[-1])
            assert not vehicle.get_attach_parent_actor(),'Vehicle attached to elevator'
        if rig.get_editor_property('completed_transfers')==1 and not report.get('departure_braked'):
            vehicle.toggle_power()
            report['departure_braked']=True
        if rig.get_editor_property('completed_transfers')==1 and liftstate==unreal.WarehouseElevatorState.IDLE:
            assert abs(vehicle.get_actor_location().z-to_floor*400)<1
            assert rig.get_editor_property('current_floor')==to_floor
            assert deck_errors,'No physical riding samples'
            assert rig.are_landing_doors_closed(),'Doors did not reclose after exit'
            report['all_landing_doors_reclosed']=True
            report['trips'].append({'destination_cm':to_floor*400,'completed_transfers':1,'t':now,'max_deck_height_error_cm':max(deck_errors),'riding_samples':len(deck_errors)})
            finish();return
        if time.monotonic()-lastwrite>1:write();lastwrite=time.monotonic()
    except Exception:finish(traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
