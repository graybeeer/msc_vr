"""Actual cargo settling, unsupported-stack fall, accessible truck and UI screenshots; never saves."""
import unreal,time,traceback
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
editor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
preview=editor.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(470,-2390,155),unreal.MathLibrary.find_look_at_rotation(unreal.Vector(470,-2390,155),unreal.Vector(350,-2810,65)))
preview.set_actor_label('TruckPreview')
cap=preview.get_component_by_class(unreal.SceneCaptureComponent2D)
cap.texture_target=unreal.RenderingLibrary.create_render_target2d(world,1280,960,unreal.TextureRenderTargetFormat.RTF_RGBA8)
cap.capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR;cap.fov_angle=65
started=time.monotonic();phase=0;initial={};upper_z=0;at=0
def finish():
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()
def tick(dt):
    global phase,initial,upper_z,at
    try:
        assert time.monotonic()-started<180,'PIE timeout'
        if phase==0:
            if time.monotonic()-started<3:return
            level.editor_request_begin_play();phase=1;return
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
        boxes={n:a for n,a in named.items() if n.startswith('TRK_Cargo_')}
        if phase==1:
            assert len(boxes)==48,len(boxes)
            if now<.3:return
            initial={n:a.get_actor_location() for n,a in boxes.items()};phase=2;return
        if phase==2:
            if now<6:return
            unstable=[n for n,a in boxes.items() if (a.get_actor_location()-initial[n]).length()>8]
            assert not unstable,unstable
            for n,a in boxes.items():
                c=a.get_component_by_class(unreal.StaticMeshComponent)
                assert c.is_simulating_physics(),n
                assert abs(c.get_mass()-a.get_editor_property('gross_mass_kg'))<.05,n
            capture=named['TruckPreview'].get_component_by_class(unreal.SceneCaptureComponent2D)
            capture.capture_scene();unreal.RenderingLibrary.export_render_target(game,capture.texture_target,unreal.Paths.project_saved_dir(),'TruckInterior.png')
            # Walk through the rear opening onto the actual floor.
            pc=unreal.GameplayStatics.get_player_controller(game,0);pawn=pc.get_controlled_pawn()
            pawn.set_actor_location(unreal.Vector(350,-2620,95),False,True)
            at=now;phase=3;return
        if phase==3:
            if now-at<2:return
            pc=unreal.GameplayStatics.get_player_controller(game,0);pawn=pc.get_controlled_pawn()
            pos=pawn.get_actor_location()
            assert abs(pos.x-350)<12 and -2700<pos.y<-2570 and 55<pos.z<130,('Truck entry blocked',pos)
            upper_z=boxes['TRK_Cargo_001'].get_actor_location().z
            base=boxes['TRK_Cargo_000'];base.get_component_by_class(unreal.StaticMeshComponent).set_simulate_physics(False)
            base.set_actor_location(unreal.Vector(-9000,-9000,-9000),False,True)
            at=now;phase=4;return
        if phase==4:
            if now-at<2:return
            assert boxes['TRK_Cargo_001'].get_actor_location().z<upper_z-8,'Unsupported carton did not fall'
            pc=unreal.GameplayStatics.get_player_controller(game,0);pawn=pc.get_controlled_pawn()
            pawn.set_actor_location(unreal.Vector(1000,-1180,96),False,True)
            pc.set_control_rotation(unreal.Rotator(pitch=-12,yaw=90))
            at=now;phase=5;return
        if phase==5:
            if now-at<2:return
            pc=unreal.GameplayStatics.get_player_controller(game,0)
            unreal.SystemLibrary.execute_console_command(game,'Shot SHOWUI -nosuffix filename=C:/msc_UnrealProject/msc_vr/Saved/ForkliftInteractionUI.png',pc)
            at=now;phase=6;return
        if phase==6:
            if now-at<1:return
            print('TRUCK_INTERIOR_PIE_PASSED 48 stable physics cartons, correct masses, player entry, unsupported fall, UI capture')
            finish()
    except Exception:
        print('TRUCK_INTERIOR_PIE_FAILED',traceback.format_exc());finish()
handle=unreal.register_slate_post_tick_callback(tick)
