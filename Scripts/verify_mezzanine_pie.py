"""PIE: actual physics cargo on a three-floor trip, observer slicing and rendered previews."""
import unreal,time,traceback
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
preview=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(350,-1180,190),unreal.MathLibrary.find_look_at_rotation(unreal.Vector(350,-1180,190),unreal.Vector(-700,700,600)))
preview.set_actor_label('MezzaninePreview')
cap=preview.get_component_by_class(unreal.SceneCaptureComponent2D)
cap.texture_target=unreal.RenderingLibrary.create_render_target2d(world,1440,1080,unreal.TextureRenderTargetFormat.RTF_RGBA8)
cap.capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR;cap.fov_angle=90
pp=cap.get_editor_property('post_process_settings')
pp.set_editor_property('override_dynamic_global_illumination_method',True)
pp.set_editor_property('dynamic_global_illumination_method',unreal.DynamicGlobalIlluminationMethod.LUMEN)
cap.set_editor_property('post_process_settings',pp)
started=time.monotonic();phase=0;phase_at=0;v=None;lift=None;box=None;pc=None;system=None;carried=False;initial={};parts=[];states=[]

def finish():
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()
def render_state(a):
    c=a.get_component_by_class(unreal.StaticMeshComponent)
    return a.get_actor_enable_collision(),c.get_collision_enabled(),list(c.get_materials()),a.get_editor_property('hidden')
def tick(dt):
    global phase,phase_at,v,lift,box,pc,system,carried,initial,parts,states
    try:
        assert time.monotonic()-started<480,'PIE timeout'
        if phase==0:
            if time.monotonic()-started<5:return
            level.editor_request_begin_play();phase=1;return
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase==1:
            if now<3:return
            named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            v=named['WH_AutonomousForklift'];lift=named['MZ_AGVElevator'];box=named['MZ_TransferCargo'];system=named['WH_DamageSystem']
            assert v.get_editor_property('elevator')==lift,'Elevator reference did not remap to PIE'
            assert v.get_editor_property('pending_jobs')[0].pallet==named['WH_TrainingPallet']
            assert not system.has_failed(v) and not system.has_failed(box),'Startup damage'
            failed=[s.name for s in system.get_editor_property('objects') if s.get_editor_property('failed')]
            assert not failed,('Initial structural failures',failed)
            loads=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseCargo)
            initial={a:a.get_actor_bounds(False)[0] for a in loads}
            assert len(initial)==311
            parts=[named['MZ_L2_Deck_Right'],named['MZ_L3_Deck_Right'],named['WH_Roof']]
            states=[render_state(a) for a in parts]
            pc=unreal.GameplayStatics.get_player_controller(game,0);pc.toggle_observer_view()
            shot=named['MezzaninePreview'].get_component_by_class(unreal.SceneCaptureComponent2D)
            unreal.RenderingLibrary.export_render_target(game,shot.texture_target,'C:/msc_UnrealProject/msc_vr/Saved','MezzanineInterior.png')
            shot.set_editor_property('capture_every_frame',False)
            phase_at=now;phase=2;return
        if phase==2:
            if now-phase_at<2:return
            unreal.SystemLibrary.execute_console_command(game,'HighResShot 1600x1000 filename=C:/msc_UnrealProject/msc_vr/Saved/MezzanineOverview.png',pc)
            phase_at=now;phase=20;return
        if phase==20:
            if now-phase_at<1:return
            pc.set_observer_floor(0);assert pc.get_editor_property('observer_floor')==0
            assert states==[render_state(a) for a in parts]
            phase_at=now;phase=3;return
        if phase==3:
            if now-phase_at<2:return
            unreal.SystemLibrary.execute_console_command(game,'HighResShot 1600x1000 filename=C:/msc_UnrealProject/msc_vr/Saved/MezzanineFloor1.png',pc)
            phase_at=now;phase=30;return
        if phase==30:
            if now-phase_at<1:return
            pc.set_observer_floor(1);assert pc.get_editor_property('observer_floor')==1
            phase_at=now;phase=4;return
        if phase==4:
            if now-phase_at<2:return
            unreal.SystemLibrary.execute_console_command(game,'HighResShot 1600x1000 filename=C:/msc_UnrealProject/msc_vr/Saved/MezzanineFloor2.png',pc)
            phase_at=now;phase=40;return
        if phase==40:
            if now-phase_at<1:return
            pc.set_observer_floor(2);assert states==[render_state(a) for a in parts]
            phase_at=now;phase=41;return
        if phase==41:
            if now-phase_at<2:return
            unreal.SystemLibrary.execute_console_command(game,'HighResShot 1600x1000 filename=C:/msc_UnrealProject/msc_vr/Saved/MezzanineFloor3.png',pc)
            phase_at=now;phase=42;return
        if phase==42:
            if now-phase_at<1:return
            pc.toggle_observer_view();assert not pc.is_observer_view()
            assert states==[render_state(a) for a in parts]
            pc.set_observer_floor(-1)
            unstable=[a.get_actor_label() for a,c in initial.items() if (a.get_actor_bounds(False)[0]-c).length()>12]
            assert not unstable,('Unstable initial cargo',[(a.get_actor_label(),initial[a],a.get_actor_bounds(False)[0]) for a in initial if a.get_actor_label() in unstable])
            failed=[s.name for s in system.get_editor_property('objects') if s.get_editor_property('failed')]
            assert not failed,('Startup load failures',failed)
            print('MEZZANINE_OBSERVER_STABILITY_PASSED',len(initial))
            v.toggle_power();phase_at=now;phase=5;return
        if phase==5:
            for _ in range(12):
                v.advance_simulation(.05);lift.advance_elevator(.05)
                carried=carried or box.get_attach_parent_actor() is not None
                assert v.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,(v.get_editor_property('status'),lift.get_editor_property('status'))
                if 'DEMO-L1-L3' in v.get_editor_property('completed_job_ids'):
                    phase_at=now;phase=6;break
        elif phase==6:
            if now-phase_at<3:return
            assert carried and not box.get_attach_parent_actor()
            c,e=box.get_actor_bounds(False)
            assert abs(c.x-1000)<60 and abs(c.y+500)<60 and abs(c.z-e.z-815)<2,(c,e)
            assert box.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
            assert not system.has_failed(v) and not system.has_failed(box),'Unexpected damage'
            assert lift.get_editor_property('completed_transfers')==1
            print('MEZZANINE_PIE_TRANSPORT_PASSED actual cargo pickup, elevator transfer, floor route, unload, stable physics, floor views')
            pc.toggle_observer_view()
            pawn=pc.get_controlled_pawn();pawn.set_actor_location(unreal.Vector(-1880,-1230,98),False,True)
            pc.set_control_rotation(unreal.Rotator(yaw=90))
            phase_at=now;phase=7
        elif phase in (7,8):
            pawn=pc.get_controlled_pawn();pos=pawn.get_actor_location()
            assert now-phase_at<20,('Stair climb timeout',phase,pos)
            if pos.y < -415:
                pawn.add_movement_input(unreal.Vector(0,1,0),1.0,True)
            else:
                pawn.get_component_by_class(unreal.CharacterMovementComponent).stop_movement_immediately()
                half=pawn.get_component_by_class(unreal.CapsuleComponent).get_scaled_capsule_half_height()
                expected=400 if phase==7 else 800
                assert abs(pos.z-half-expected)<5,('Incorrect stair elevation',phase,pos,half)
                if phase==7:
                    pawn.set_actor_location(unreal.Vector(-1880,-1230,498),False,True)
                    phase_at=now;phase=8
                else:
                    print('MEZZANINE_STAIRS_PASSED both physical flights')
                    print('MEZZANINE_PIE_VERIFIED')
                    finish()
    except Exception:
        print('MEZZANINE_PIE_FAILED',traceback.format_exc());finish()
handle=unreal.register_slate_post_tick_callback(tick)
