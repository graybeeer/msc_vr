"""A real Chaos-stepped three-floor trip; no manually accelerated AI or saved test edits."""
import unreal,time,traceback
performance=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
throttle_property='bThrottleCPUWhenNotForeground'
old_throttle=performance.get_editor_property(throttle_property)
performance.set_editor_property(throttle_property,False)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
started=time.monotonic();phase=0;last_state=None;carried=False;named=None
unload_only=globals().get('unload_only',False)
job_id='PHYSICAL-L3-UNLOAD' if unload_only else 'DEMO-L1-L3'

def finish():
    performance.set_editor_property(throttle_property,old_throttle)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def tick(dt):
    global phase,last_state,carried,named
    try:
        assert time.monotonic()-started<600,'Dynamic transfer timeout'
        if phase==0:
            if time.monotonic()-started<3:return
            level.editor_request_begin_play();phase=1;return
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        if named is None:
            named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
        v=named['WH_AutonomousForklift'];pallet=named['WH_TrainingPallet'];box=named['MZ_TransferCargo'];lift=named['MZ_AGVElevator']
        pbody=pallet.get_component_by_class(unreal.StaticMeshComponent);body=box.get_component_by_class(unreal.StaticMeshComponent)
        pc=unreal.GameplayStatics.get_player_controller(game,0)
        if phase==1:
            if unreal.GameplayStatics.get_time_seconds(game)<3:return
            pc.get_controlled_pawn().set_actor_location(unreal.Vector(-1800,-1500,96),False,True)
            if unload_only:
                # Recheck only the changed final stage on the actual third-floor deck.
                pallet.set_actor_location(unreal.Vector(1000,-500,800.1),False,True)
                box.set_actor_location(box.get_actor_location()+unreal.Vector(0,0,800),False,True)
                for component in (pbody,body):
                    component.set_physics_linear_velocity(unreal.Vector())
                    component.set_physics_angular_velocity_in_degrees(unreal.Vector());component.wake_all_rigid_bodies()
                v.set_actor_location(unreal.Vector(1000,-720,800),False,True)
                v.set_editor_property('pending_jobs',[unreal.WarehouseWorkOrder(job_id=job_id,source_system='PHYSICS-UNLOAD-TEST',pallet=pallet,destination=unreal.Transform(location=unreal.Vector(1000,-200,800),rotation=unreal.Rotator(yaw=90)))])
            unreal.SystemLibrary.execute_console_command(game,'t.MaxFPS 60',pc)
            unreal.SystemLibrary.execute_console_command(game,'Slate.bAllowThrottling 0',pc)
            unreal.GameplayStatics.set_global_time_dilation(game,2)
            v.toggle_power();phase=2;return
        assert pbody.is_simulating_physics() and body.is_simulating_physics(),'Load physics disabled'
        assert not pallet.get_attach_parent_actor() and not box.get_attach_parent_actor(),'Load was rigidly attached'
        state=v.get_editor_property('ai_state')
        if state!=last_state:
            print('DYNAMIC_TRANSFER_STATE',state,pallet.get_actor_location(),pallet.get_actor_rotation(),box.get_actor_bounds(False))
            last_state=state
        assert state!=unreal.WarehouseAIState.FAULT,(v.get_editor_property('status'),lift.get_editor_property('status'))
        carried=carried or (state==unreal.WarehouseAIState.DEPART_PICKUP and pallet.get_actor_location().z>(805 if unload_only else 5))
        if phase==2 and job_id in v.get_editor_property('completed_job_ids'):
            phase=3;return
        if phase==3:
            c,e=pallet.get_actor_bounds(False);bc,be=box.get_actor_bounds(False)
            assert carried and abs(c.z-e.z-800)<2,(c,e)
            assert abs(bc.z-be.z-815)<3,(bc,be)
            assert lift.get_editor_property('completed_transfers')==(0 if unload_only else 1)
            print('DYNAMIC_L3_UNLOAD_PASSED' if unload_only else 'DYNAMIC_THREE_FLOOR_TRANSFER_PASSED','continuous load physics, physical support, unload, completion report')
            finish()
    except Exception:
        print('DYNAMIC_THREE_FLOOR_TRANSFER_FAILED',traceback.format_exc());finish()
handle=unreal.register_slate_post_tick_callback(tick)
