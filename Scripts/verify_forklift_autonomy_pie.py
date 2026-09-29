"""PIE smoke test with actual Chaos cargo; run via -ExecCmds=py, never saves."""
import unreal, time, traceback, sys, os
sys.path.insert(0,os.path.dirname(__file__))
from configure_forklift_autonomy import configure_autonomy
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
v=configure_autonomy(True)
orders=list(v.get_editor_property('pending_jobs'))
orders[0].set_editor_property('destination',unreal.Transform(location=unreal.Vector(1500,200,0),rotation=unreal.Rotator(yaw=0)))
v.set_editor_property('pending_jobs',orders)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
mesh=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WH_Cargo_Left_02_1_Top').get_component_by_class(unreal.StaticMeshComponent).static_mesh
assert unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_convex_collision_count(mesh)==1,'Rebuild the sealed-carton collision asset'
assert unreal.EditorAssetLibrary.get_metadata_tag(mesh,'WarehouseCollisionVersion')=='sealed-carton-v1','Rebuild imported carton collision'
start=time.monotonic();phase=0;vehicle=None;box=None;system=None;released_at=0;carried=False;game=None

def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(dt):
    global phase,vehicle,box,system,released_at,carried,game
    try:
        assert time.monotonic()-start<180,'PIE timeout'
        if phase==0:
            if time.monotonic()-start<5:return
            level.editor_play_simulate();phase=1;return
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase==1:
            if now<2:return
            named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            vehicle=named['WH_AutonomousForklift'];box=named['WH_Cargo_Left_02_1_Top'];system=named['WH_DamageSystem']
            assert not system.has_failed(vehicle) and not system.has_failed(box),'Startup damage'
            # Configure streamed actors inside the game world, after World Partition duplication.
            body=box.get_component_by_class(unreal.StaticMeshComponent)
            body.set_simulate_physics(False)
            center,extent=box.get_actor_bounds(False)
            pickup=named['WH_TrainingPallet'].get_actor_location()
            jobs=list(vehicle.get_editor_property('pending_jobs'))
            assert jobs and jobs[0].get_editor_property('pallet')==named['WH_TrainingPallet'],'Saved job reference did not remap into PIE'
            jobs[0].set_editor_property('destination',unreal.Transform(location=unreal.Vector(1500,200,0),rotation=unreal.Rotator(yaw=0)))
            vehicle.set_editor_property('pending_jobs',jobs)
            body.set_world_location(body.get_world_location()+pickup+unreal.Vector(0,0,15+extent.z+10)-center,False,True)
            body.set_simulate_physics(True)
            body.set_physics_linear_velocity(unreal.Vector())
            body.set_physics_angular_velocity_in_degrees(unreal.Vector())
            released_at=now;phase=4;return
        if phase==4:
            if now-released_at<2:return
            vehicle.toggle_power();phase=2
        if phase==2:
            # Accelerate only the deterministic controller; Chaos still runs each real PIE frame.
            for _ in range(12):
                vehicle.advance_simulation(.05)
                carried=carried or bool(box.get_attach_parent_actor())
                assert vehicle.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,vehicle.get_editor_property('status')
                if 'DEMO-001' in vehicle.get_editor_property('completed_job_ids'):
                    released_at=now;phase=3;break
        elif phase==3:
            if now-released_at<2:return
            assert carried,'Cargo was never attached'
            assert not box.get_attach_parent_actor(),'Cargo still attached after unloading'
            assert abs(box.get_actor_location().x-1500)<70 and abs(box.get_actor_location().y-200)<70,box.get_actor_location()
            assert not system.has_failed(vehicle) and not system.has_failed(box),'Unexpected transport damage'
            assert box.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
            print('AUTONOMY_PIE_VERIFIED actual cargo pickup, curved travel, release, no unexpected damage')
            finish()
    except Exception:
        print('AUTONOMY_PIE_FAILED',traceback.format_exc());finish()
handle=unreal.register_slate_post_tick_callback(tick)
