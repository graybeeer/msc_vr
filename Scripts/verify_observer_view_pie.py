"""Real PIE check: overhead framing, per-view hiding, return to first person. No map save."""
import unreal, time, traceback

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
started=time.monotonic();phase=0;phase_time=0;pc=None;roofs=[];states=[];pawn=None

def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def roof_state(actor):
    mesh=actor.get_component_by_class(unreal.StaticMeshComponent)
    return (actor.get_editor_property('hidden'),actor.get_actor_enable_collision(),mesh.get_collision_enabled(),list(mesh.get_materials()))

def tick(dt):
    global phase,phase_time,pc,roofs,states,pawn
    try:
        assert time.monotonic()-started<180,'Observer PIE timeout'
        if phase==0:
            if time.monotonic()-started<5:return
            level.editor_request_begin_play();phase=1;return
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase==1:
            if now<3:return
            pc=unreal.GameplayStatics.get_player_controller(game,0)
            pawn=pc.get_controlled_pawn()
            roofs=unreal.GameplayStatics.get_all_actors_with_tag(game,'ObserverRoof')
            assert len(roofs)>=2,'Roof tags not saved'
            assert len(unreal.GameplayStatics.get_all_actors_with_tag(game,'ObserverArea'))==1
            states=[roof_state(a) for a in roofs]
            pc.toggle_observer_view()
            assert pc.is_observer_view() and pc.is_move_input_ignored()
            assert pc.get_controlled_pawn()==pawn and pc.get_view_target()!=pawn
            assert states==[roof_state(a) for a in roofs],'Materials/collision/global visibility changed'
            camera=pc.get_view_target()
            assert camera.get_actor_location().z>2000 and abs(camera.get_actor_rotation().pitch+70)<.1
            pc.frame_warehouse()
            print('OBSERVER_OVERHEAD_VERIFIED',len(roofs),'roofs',camera.get_actor_location(),camera.get_actor_rotation())
            phase_time=now;phase=2;return
        if phase==2:
            if now-phase_time<3:return
            workers=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseWorker)
            visible=[a for a in workers if a.get_component_by_class(unreal.WidgetComponent).is_visible()]
            assert visible,'All worker labels obscured by hidden roof'
            print('OBSERVER_LABELS_VISIBLE',len(visible))
            unreal.SystemLibrary.execute_console_command(game,'Shot showui filename=C:/msc_UnrealProject/msc_vr/Saved/ObserverOverview.png',pc)
            phase_time=now;phase=3;return
        if phase==3:
            if now-phase_time<2:return
            camera=pc.get_view_target()
            pc.toggle_observer_view()
            assert not pc.is_observer_view() and pc.get_view_target()==pawn and not pc.is_move_input_ignored()
            assert states==[roof_state(a) for a in roofs]
            pc.toggle_observer_view();pc.toggle_observer_view()
            assert not pc.is_observer_view() and pc.get_view_target()==pawn
            pc.set_view_target_with_blend(camera,0)
            phase_time=now;phase=4;return
        if phase==4:
            if now-phase_time<1:return
            unreal.SystemLibrary.execute_console_command(game,'Shot showui filename=C:/msc_UnrealProject/msc_vr/Saved/ObserverRoofRestored.png',pc)
            phase_time=now;phase=5;return
        if phase==5:
            if now-phase_time<2:return
            pc.set_view_target_with_blend(pawn,0)
            print('OBSERVER_VIEW_PIE_VERIFIED overhead, collision/material preservation, labels, repeated return')
            finish()
    except Exception:
        print('OBSERVER_VIEW_PIE_FAILED',traceback.format_exc());finish()

handle=unreal.register_slate_post_tick_callback(tick)
