"""Native traffic cases: shared lane yield/resume, unavailable escape, closed-section detour. No saves."""
import unreal,time,traceback,json
from pathlib import Path
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
command=unreal.SystemLibrary.get_command_line().lower()
blocked='-fleetnoescape' in command
closed='-fleetclosedsection' in command
parked='-fleetparkedwarehouse' in command
blocked_start='-fleetblockedescapestart' in command
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
source=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WH_AutonomousForklift')
rig={c.get_name():(c.static_mesh,list(c.get_editor_property('override_materials'))) for c in source.get_components_by_class(unreal.StaticMeshComponent)}
poses={c.get_name():unreal.MathLibrary.make_relative_transform(c.get_world_transform(),source.get_actor_transform()) for c in source.get_components_by_class(unreal.SceneComponent) if c!=source.root_component}
pallet_mesh=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WH_TrainingPallet').get_component_by_class(unreal.StaticMeshComponent).static_mesh
if not parked:
    assert level.load_level('/Engine/Maps/Entry')
    for original in list(actors.get_all_level_actors()):
        if not isinstance(original,(unreal.WorldSettings,unreal.LevelScriptActor)):actors.destroy_actor(original)
world=editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WorldSettings)[0].set_editor_property('default_game_mode',unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)

def spawn(cls,label,point,yaw=0):
    a=actors.spawn_actor_from_class(cls,unreal.Vector(*point),unreal.Rotator(yaw=yaw));a.set_actor_label(label);return a
def cube(label,point,size):
    a=spawn(unreal.StaticMeshActor,label,point);a.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    a.static_mesh_component.set_collision_profile_name('BlockAll');a.set_actor_scale3d(unreal.Vector(*size)/100.);return a
def depth(component):
    result=0
    while component.get_attach_parent():result+=1;component=component.get_attach_parent()
    return result

if parked:
    named={a.get_actor_label():a for a in actors.get_all_level_actors()}
    named['WH_InboundDispatch'].set_editor_property('fleet',[])
    for old,label,point,yaw in [('WH_AutonomousForklift','TrafficA',(1080,-1790,0),0),('WH_AutonomousForklift_02','TrafficB',(-580,-1570,0),180)]:
        if label=='TrafficB' and blocked_start:point,yaw=(-631,-1574,0),-166
        v=named[old];v.set_actor_label(label);v.set_actor_location(unreal.Vector(*point),False,False);v.set_actor_rotation(unreal.Rotator(yaw=yaw),False);v.set_editor_property('auto_start',False);v.set_editor_property('pending_jobs',[])
    named['WH_TrainingPallet'].set_actor_label('TrafficPalletA')
    p=named['WH_Drop_Pallet'];delta=unreal.Vector(-800,-1570,.1)-p.get_actor_location();p.set_actor_location(p.get_actor_location()+delta,False,False)
    box=named['WH_Drop_Box'];box.set_actor_location(box.get_actor_location()+delta,False,False)
else:
    cube('TrafficFloor',(0,0,-10),(8000,8000,20))
    spawn(unreal.PlayerStart,'TrafficPlayer',(0,1000,100))
for label,point,yaw in ([] if parked else [('TrafficA',(-500 if blocked else -120,0,0),0),('TrafficB',(500 if blocked else 400,0,0),180)]):
    v=spawn(unreal.WarehouseForklift,label,point,yaw);v.set_editor_property('vehicle_name',label);v.set_editor_property('auto_start',False)
    v.set_editor_property('idle_parking_enabled',False)
    for c in v.get_components_by_class(unreal.StaticMeshComponent):
        m,materials=rig[c.get_name()];c.set_static_mesh(m);c.set_editor_property('override_materials',materials)
    for c in sorted(v.get_components_by_class(unreal.SceneComponent),key=depth):
        if c!=v.root_component:c.set_world_transform(unreal.MathLibrary.compose_transforms(poses[c.get_name()],v.get_actor_transform()),False,True)
    v.set_editor_property('navigation_anchors',[unreal.Transform(location=unreal.Vector(x,y,0),rotation=unreal.Rotator(yaw=h)) for x,y,h in [(-200,-400,0),(650,-400,0),(1000,-400,0),(-200,400,0),(650,400,0),(1000,400,0),(-200,0,0),(650,0,0)]])
if not parked:
    for label,point,yaw in [('TrafficPalletA',(700 if blocked else 150,0,.1),0),('TrafficPalletB',(-1400,0,.1),180)]:
        spawn(unreal.WarehousePallet,label,point,yaw).get_component_by_class(unreal.StaticMeshComponent).set_static_mesh(pallet_mesh)
if blocked:
    cube('TrafficWallN',(0,120,125),(1800,20,250));cube('TrafficWallS',(0,-120,125),(1800,20,250));cube('TrafficWallBack',(800,0,125),(30,260,250))
if not parked:
    # Destination/support validation uses the real strength system, including
    # in empty-pallet traffic fixtures. Without it the job faults before routing.
    strength=spawn(unreal.WarehouseDamageSystem,'TrafficStrength',(0,1500,0))
    specs=[]
    for a in actors.get_all_level_actors():
        if isinstance(a,unreal.WarehousePallet) or isinstance(a,unreal.StaticMeshActor):
            specs.append(unreal.WarehouseStrength(name=a.get_actor_label(),members=[a],mass_kg=25 if isinstance(a,unreal.WarehousePallet) else (50000 if a.get_actor_label()=='TrafficFloor' else 500),
                rated_load_kg=1000000,impact_yield_j=100000,impact_failure_j=1000000,
                failure=unreal.WarehouseFailure.CRUSH if isinstance(a,unreal.WarehousePallet) else unreal.WarehouseFailure.STRUCTURE))
    strength.set_editor_property('objects',specs)
started=time.monotonic();live=None;game=None;issued=False;second=False;last=-10;baseline=None;initial_b=None;fault_at=None;yield_recorded=False

def finish(ok,detail):
    print('FLEET_TRAFFIC_PASSED' if ok else 'FLEET_TRAFFIC_FAILED',detail)
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def tick(dt):
    global live,game,issued,second,last,baseline,initial_b,fault_at,yield_recorded
    try:
        assert time.monotonic()-started<500,'Traffic fixture wall timeout'
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            pawn=unreal.GameplayStatics.get_player_pawn(game,0)
            print('TRAFFIC_FIXTURE_PLAYER',pawn.get_actor_location())
            if not parked:assert pawn.get_actor_location().y>500,'Player spawned inside the test lane'
            if not parked:
                clocks=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseSimulationClock)
                assert clocks and clocks[0].set_speed(4)
        a,b=live['TrafficA'],live['TrafficB']
        if b.get_editor_property('traffic_yielding') and not yield_recorded:
            yield_recorded=True
            data=[{'p':[r.pose.translation.x,r.pose.translation.y,r.pose.translation.z],'yaw':r.pose.rotation.rotator().yaw,'reverse':r.get_editor_property('bReverse'),'pivot':r.get_editor_property('bPivot')} for r in b.get_editor_property('planned_route')]
            Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/FleetYieldRoute.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
            print('YIELD_ROUTE_RECORDED',len(data))
        managers=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseFleetCoordinator)
        assert len(managers)==1,'Central controller is not unique'
        manager=managers[0]
        if now-last>2:
            last=now
            print('TRAFFIC_SAMPLE',round(now,2),a.get_actor_location(),a.get_editor_property('status'),b.get_actor_location(),b.get_editor_property('status'),'wait',manager.get_editor_property('wait_count'),'yield',manager.get_editor_property('yield_count'))
            if b.get_editor_property('traffic_yielding'):
                route=b.get_editor_property('planned_route')
                print('TRAFFIC_YIELD_POSE',b.get_actor_transform(),'first',[(str(r.pose),r.get_editor_property('bReverse'),r.get_editor_property('bPivot')) for r in list(route)[:2]])
        for v in (a,b):
            assert v.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics(),'Vehicle physics disabled'
        if not issued:
            if now<2:return
            assert unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseDamageSystem),'Missing traffic fixture support system'
            assert a.is_floor_footprint_valid(live['TrafficPalletA'],live['TrafficPalletA'].get_actor_transform()),'Traffic fixture pallet exceeds its reserved footprint'
            a.toggle_power();b.toggle_power();baseline=a.get_actor_location();initial_b=b.get_actor_location()
            if closed:manager.close_section('Lane-01',unreal.Vector(250,-120,1),unreal.Vector(520,120,240),'test obstruction')
            job=unreal.WarehouseWorkOrder(job_id='TRAFFIC-A',source_system='FMS-TRAFFIC-CHECK',pallet=live['TrafficPalletA'],destination=unreal.Transform(location=unreal.Vector(*((-800,-1395,0) if parked else (1100,0,0))),rotation=unreal.Rotator(yaw=180 if parked else 0)))
            assert a.submit_job(job);issued=True;return
        if blocked:
            if a.get_editor_property('emergency_blocked'):
                if fault_at is None:fault_at=now
                if now-fault_at<.6:return
                assert manager.get_editor_property('yield_count')==0,'Unsafe retreat was assigned'
                assert (a.get_actor_location()-baseline).length()<15 and (b.get_actor_location()-initial_b).length()<15,'Vehicle forced through blocked corridor'
                assert a.get_editor_property('ai_state')==unreal.WarehouseAIState.FAULT
                pc=unreal.GameplayStatics.get_player_controller(game,0)
                assert pc.get_editor_property('emergency_alarm_count')>=1,'Operator alarm was not generated'
                print('FLEET_NO_SAFE_ESCAPE_PASSED',a.get_editor_property('emergency_reason'));finish(True,'Blocked escape keeps both vehicles stopped and requests operator');return
            assert now<90,'No recovery warning for an unavailable escape';return
        assert a.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,a.get_editor_property('status')
        assert b.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,b.get_editor_property('status')
        if parked:
            if 'TRAFFIC-A' in a.get_editor_property('completed_job_ids'):
                print('FLEET_PARKED_TRANSPORT_PASSED');finish(True,'Saved warehouse: real load delivered around a parked AGV and unloaded cargo');return
            assert now<300,'Parked warehouse progress timeout';return
        if manager.get_editor_property('yield_count')>0 and not second and not closed:
            job=unreal.WarehouseWorkOrder(job_id='TRAFFIC-B',source_system='FMS-TRAFFIC-CHECK',pallet=live['TrafficPalletB'],destination=unreal.Transform(location=unreal.Vector(-2300,0,0),rotation=unreal.Rotator(yaw=180)))
            assert b.submit_job(job);second=True
        if closed:
            p=a.get_actor_location()
            assert not (250<p.x<520 and abs(p.y)<120),'Vehicle entered a closed section'
            if p.x>700:
                assert manager.get_editor_property('closed_section_count')==1
                manager.reopen_section('Lane-01');assert manager.get_editor_property('closed_section_count')==0
                print('FLEET_CLOSED_SECTION_DETOUR_PASSED');finish(True,'Native route avoided closed volume and reopened it explicitly');return
        elif second and a.get_actor_location().x>800 and b.get_actor_location().x<-650:
            assert not b.get_editor_property('traffic_yielding'),'Yielding vehicle did not resume its original job'
            print('FLEET_YIELD_PASS_RESUME_PASSED');finish(True,'A loaded vehicle passed; B physically retreated and resumed its queued job');return
        assert now<240,'Central traffic progress timeout'
    except Exception:finish(False,traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
