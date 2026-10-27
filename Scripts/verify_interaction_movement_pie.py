"""Read saved layout before any reconfiguration; native E/G and physical gait. Never saves."""
import unreal,time,traceback
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from warehouse_staging_layout import PALLET_POSES

command=unreal.SystemLibrary.get_command_line().lower()
SAVED='-interactionsavedstartup' in command
REMOTE_CAMERA='-remotetpschecks' in command
CAPTURE='-remotetpscapture' in command
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
zone=named['WH_InboundDispatch']
for label,position in PALLET_POSES.items():
    assert (named[label].get_actor_location()-unreal.Vector(*position)).length()<.1,('Unsaved staging location',label,named[label].get_actor_location())
assert len(zone.get_editor_property('rack_slots'))==16
print('INTERACTION_DISK_LAYOUT_PASSED')
if not SAVED:
    rig_data={c.get_name():(c.get_relative_transform(),c.static_mesh,list(c.get_editor_property('override_materials')))
              for c in named['WH_AutonomousForklift'].get_components_by_class(unreal.StaticMeshComponent)}
    assert level.load_level('/Engine/Maps/Entry')
    for a in list(actors.get_all_level_actors()):
        if not isinstance(a,(unreal.WorldSettings,unreal.LevelScriptActor)):actors.destroy_actor(a)
    world=editor.get_editor_world()
    unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WorldSettings)[0].set_editor_property('default_game_mode',unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
    floor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-10))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.set_actor_scale3d(unreal.Vector(80,80,.2));floor.static_mesh_component.set_collision_profile_name('BlockAll')
    actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(-40,-170,125))
    rig=actors.spawn_actor_from_class(unreal.WarehouseForklift,unreal.Vector())
    rig.set_actor_label('InteractionRig');rig.set_editor_property('auto_start',False)
    for c in rig.get_components_by_class(unreal.StaticMeshComponent):
        transform,mesh,materials=rig_data[c.get_name()]
        c.set_relative_transform(transform,False,True);c.set_static_mesh(mesh);c.set_editor_property('override_materials',materials)
    wall=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(3000,3000,200))
    wall.set_actor_label('InteractionOccluder');wall.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    wall.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    wall.set_actor_scale3d(unreal.Vector(.3,.3,1));wall.static_mesh_component.set_collision_profile_name('BlockAll')
    if CAPTURE:
        actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,600),unreal.Rotator(pitch=-45,yaw=-25))
        actors.spawn_actor_from_class(unreal.SkyAtmosphere,unreal.Vector())
unreal.SystemLibrary.execute_console_command(editor.get_editor_world(),'t.OverrideFPS 60')
began=time.monotonic();phase='settle';at=0;live=None;frames=0;start=None;speeds=[];feet=[];stop_at=None

def finish(ok,detail):
    print('INTERACTION_MOVEMENT_PIE_'+('PASSED' if ok else 'FAILED'),detail)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def enter(name):
    global phase,at,frames,start,speeds,feet,stop_at
    phase=name;at=now;frames=0;start=pawn.get_actor_location();speeds=[];feet=[];stop_at=None

def aim(point):
    camera=pawn.get_components_by_class(unreal.CameraComponent)[0]
    pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_world_location(),point))
    return camera

def check_phone_visibility(using):
    names={'RemotePhone','RemotePhoneScreen','RemoteWorldPhone','RemoteWorldPhoneScreen'}
    parts=[c for c in pawn.get_components_by_class(unreal.StaticMeshComponent) if c.get_name() in names]
    assert len(parts)==4,('Missing phone body/screen',len(parts))
    for c in parts:
        assert c.is_visible()==using and c.get_editor_property('hidden_in_game')!=using,('Phone visibility',c.get_name(),using)
    print('REMOTE_PHONE_VISIBILITY_PASSED','remote' if using else 'unused',phase)

def tick(dt):
    global live,pawn,pc,rig,wall,now,frames,stop_at,operator_start,drive_start,control_start,camera_before
    try:
        if time.monotonic()-began>300:raise AssertionError('Wall timeout')
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if not live:
            live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            pawn=unreal.GameplayStatics.get_player_character(game,0);pc=unreal.GameplayStatics.get_player_controller(game,0)
            if not SAVED:rig=live['InteractionRig'];wall=live['InteractionOccluder']
        if SAVED:
            if now<10:return
            dispatch=live['WH_InboundDispatch']
            assert dispatch.get_editor_property('dispatched_count')==1,('Saved map did not dispatch',dispatch.get_editor_property('status'))
            vehicles=[live['WH_AutonomousForklift'],live['WH_AutonomousForklift_02']]
            assert any(v.get_editor_property('active_job').job_id for v in vehicles)
            print('SAVED_MAP_AUTODISPATCH_PASSED',dispatch.get_editor_property('status'))
            finish(True,'Unmodified saved map, all background cargo physics retained; startup dispatch only');return
        age=now-at;frames+=1
        assert not pawn.is_physical_ragdoll(),'Human lost balance'
        if phase=='settle':
            if now<2:return
            check_phone_visibility(False)
            enter('gap');return
        if phase=='gap':
            aim(rig.get_actor_location()+unreal.Vector(20,0,160))
            if frames<5:return
            assert pawn.get_interaction_target()==rig,('Mast gap was not selectable',pawn.get_interaction_target())
            pawn.toggle_carry();assert rig.get_editor_property('powered'),'E handler did not power on'
            pawn.toggle_carry();assert not rig.get_editor_property('powered'),'E handler did not pause'
            pawn.toggle_remote_control();assert pawn.get_remote_forklift()==rig,'G handler did not connect'
            check_phone_visibility(True)
            pawn.toggle_remote_control();assert not pawn.get_remote_forklift(),'G handler did not disconnect'
            check_phone_visibility(False)
            print('FORKLIFT_GAP_E_G_PASSED')
            if REMOTE_CAMERA:
                pawn.toggle_remote_control();assert pawn.get_remote_forklift()==rig
                operator_start=pawn.get_actor_location();control_start=pc.get_control_rotation()
                enter('tps_settle');return
            camera=pawn.get_components_by_class(unreal.CameraComponent)[0]
            wall.set_actor_location((camera.get_world_location()+rig.get_actor_location()+unreal.Vector(20,0,160))*.5,False,True)
            enter('occluded');return
        if phase.startswith('tps_'):
            manager=unreal.GameplayStatics.get_player_camera_manager(game,0)
            eye=manager.get_camera_location()
            delta=eye-rig.get_actor_location()
            forward=rig.get_actor_forward_vector();side=rig.get_actor_right_vector()
            rear=delta.x*forward.x+delta.y*forward.y
            rig.set_remote_input(.6 if phase=='tps_drive' else 0,0,0)
            assert pc.get_controlled_pawn()==pawn,'TPS changed possession'
            if phase=='tps_settle':
                if age<1.2:return
                check_phone_visibility(True)
                assert pc.get_view_target()==rig,'View target is still the operator'
                assert rear<-350 and delta.z>200,('Camera not behind/above forklift',delta)
                # Native input fixture: polling unpressed OS keys would overwrite
                # the remote commands sent by this callback. Preserve all physics.
                pawn.set_actor_tick_enabled(False)
                operator_start=pawn.get_actor_location()
                drive_start=rig.get_actor_location();enter('tps_drive');return
            if phase=='tps_drive':
                if age<1.8:return
                assert (rig.get_actor_location()-drive_start).length()>60,'Native remote drive did not advance'
                assert rear<-350 and delta.z>200,('Camera failed to follow moving forklift',delta)
                assert (pawn.get_actor_location()-operator_start).length()<5,'Operator moved with the camera'
                print('REMOTE_TPS_DRIVE_FOLLOW_PASSED',delta)
                pawn.do_aim(12,5);enter('tps_orbit');return
            if phase=='tps_orbit':
                if age<.3:return
                assert abs(delta.x*side.x+delta.y*side.y)>20,'Mouse did not orbit vehicle camera'
                assert abs(pc.get_control_rotation().yaw-control_start.yaw)<.1,'Remote orbit rotated the operator view'
                pawn.do_aim(-12,-5)
                wall.set_actor_location(rig.get_actor_location()-forward*260+unreal.Vector(0,0,160),False,True)
                wall.set_actor_scale3d(unreal.Vector(.3,2,3))
                enter('tps_wall');return
            if phase=='tps_wall':
                if age<.3:return
                assert -320<rear<-120,('Camera did not shorten before wall',delta)
                print('REMOTE_TPS_WALL_ORBIT_PASSED',delta)
                wall.set_actor_location(unreal.Vector(3000,3000,200),False,True)
                if CAPTURE:
                    enter('tps_capture_wait');return
                pawn.toggle_remote_control();pawn.set_actor_tick_enabled(True);enter('tps_return');return
            if phase=='tps_capture_wait':
                if age<.4:return
                unreal.SystemLibrary.execute_console_command(game,'HighResShot 960x540 filename=C:/msc_UnrealProject/msc_vr/Saved/Testing/RemoteTPS.png')
                enter('tps_capture_finish');return
            if phase=='tps_capture_finish':
                if age<.4:return
                pawn.toggle_remote_control();pawn.set_actor_tick_enabled(True);enter('tps_return');return
            if phase=='tps_return':
                if age<.4:return
                check_phone_visibility(False)
                assert not pawn.get_remote_forklift() and pc.get_view_target()==pawn,'G did not restore first-person view'
                camera=pawn.get_components_by_class(unreal.CameraComponent)[0]
                assert (eye-camera.get_world_location()).length()<5,'First-person camera location not restored'
                assert abs(pc.get_control_rotation().yaw-control_start.yaw)<.1,'Original look direction not restored'
                aim(rig.get_actor_location()+unreal.Vector(20,0,160))
                enter('tps_reconnect');return
            if phase=='tps_reconnect':
                if age<.2:return
                pawn.toggle_remote_control();assert pawn.get_remote_forklift()==rig
                pc.toggle_warehouse_menu();assert not pawn.get_remote_forklift(),'Menu retained remote camera'
                pc.toggle_warehouse_menu();enter('tps_menu_return');return
            if phase=='tps_menu_return':
                if age<.3:return
                check_phone_visibility(False)
                pc.toggle_observer_view();assert pc.is_observer_view();check_phone_visibility(False)
                pc.toggle_observer_view();assert not pc.is_observer_view();check_phone_visibility(False)
                assert pc.get_view_target()==pawn,'Menu did not restore player view'
                print('REMOTE_TPS_PIE_PASSED','Native vehicle follow, mouse orbit, wall avoidance, G/menu first-person return; capture' if CAPTURE else 'Native vehicle follow, mouse orbit, wall avoidance, G/menu first-person return; no rendered claim')
                finish(True,'Remote TPS camera checks');return
        if phase=='occluded':
            if age<.1:return
            assert pawn.get_interaction_target()!=rig,'Interaction passed through a wall'
            pawn.toggle_remote_control();assert not pawn.get_remote_forklift(),'G connected through wall'
            wall.set_actor_location(unreal.Vector(3000,3000,200),False,True)
            assert pawn.teleport(unreal.Vector(360,0,110),unreal.Rotator(yaw=180))
            enter('range_edge');return
        if phase=='range_edge':
            aim(rig.get_actor_location()+unreal.Vector(-30,0,70))
            if age<.5:return
            assert pawn.get_interaction_target()==rig,('Vehicle outline range edge not selectable',pawn.get_actor_location(),rig.get_actor_bounds(True))
            pawn.toggle_remote_control();assert pawn.get_remote_forklift()==rig,'G guard disagreed with visible outline range'
            pawn.toggle_remote_control()
            assert pawn.teleport(unreal.Vector(650,0,110),unreal.Rotator(yaw=180))
            enter('out_of_range');return
        if phase=='out_of_range':
            aim(rig.get_actor_location()+unreal.Vector(-30,0,70))
            if age<.5:return
            assert pawn.get_interaction_target()!=rig,'Vehicle outside 300cm outline range selected'
            print('FORKLIFT_RANGE_GUARD_PASSED')
            assert pawn.teleport(unreal.Vector(800,-1000,110),unreal.Rotator()),'Gait fixture relocation blocked'
            pc.set_control_rotation(unreal.Rotator())
            print('FORKLIFT_OCCLUSION_GUARD_PASSED');enter('gait_settle');return
        if phase=='gait_settle':
            if age<1:return
            enter('walk60');return
        if phase in ('walk60','walk15','reverse','strafe'):
            right,forward=(1,0) if phase=='strafe' else (0,-1 if phase=='reverse' else 1)
            mesh=next(c for c in pawn.get_components_by_class(unreal.SkeletalMeshComponent) if c.get_name()=='CharacterMesh0')
            feet.append(mesh.get_socket_transform('foot_l',unreal.RelativeTransformSpace.RTS_COMPONENT).translation.x)
            # Input is injected after the actor tick. Do not queue an extra held
            # key for the following frame once the release time has arrived.
            if age<(1.5 if phase.startswith('walk') else 1):
                pawn.do_move(right,forward);speeds.append(pawn.get_velocity());return
            velocity=pawn.get_velocity()
            assert abs(velocity.y if phase=='strafe' else velocity.x)>230,('Speed/direction',phase,velocity)
            assert mesh.get_anim_instance().get_editor_property('ShouldMove'),'Walking animation not active'
            assert max(feet)-min(feet)>10,('Foot pose did not move',phase,feet)
            if phase=='reverse':
                assert velocity.x<-230 and abs(velocity.y)<15,('Reverse retained sideways drift',velocity)
                print('HUMAN_DIRECTION_CHANGE_PASSED',velocity);enter('direction_stop');return
            if phase=='strafe':
                enter('reverse');return
            print('HUMAN_SPEED_PASSED',phase,velocity);enter('stop60' if phase=='walk60' else 'stop15');return
        if phase in ('stop60','stop15','direction_stop'):
            speed=pawn.get_velocity().length()
            if speed<10 and stop_at is None:stop_at=age
            if age<1:return
            distance=(pawn.get_actor_location()-start).length()
            assert stop_at is not None and stop_at<(.5 if phase=='stop15' else .45),('Stop delay',phase,stop_at)
            assert distance<50,('Coasting distance',phase,distance)
            assert speed<3,('Continued idle drift',phase,speed)
            print('HUMAN_BRAKING_PASSED',phase,'seconds',stop_at,'distance_cm',distance)
            if phase=='stop60':enter('strafe');return
            if phase=='direction_stop':
                unreal.SystemLibrary.execute_console_command(game,'t.OverrideFPS 15');enter('walk15');return
            finish(True,'E/G mast-gap targeting, wall occlusion, human direction changes and finite braking at 60/15Hz')
    except Exception:
        traceback.print_exc();finish(False,'phase '+phase)
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
