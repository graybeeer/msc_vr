import unreal,time,traceback
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
preview_actor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector())
preview_actor.set_actor_label('RefinedAGVPreview')
editor_cap=preview_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
editor_cap.texture_target=unreal.RenderingLibrary.create_render_target2d(world,1280,960,unreal.TextureRenderTargetFormat.RTF_RGBA8)
editor_cap.capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR;editor_cap.fov_angle=60
editor_cap.set_editor_property('capture_every_frame',False)
pp=editor_cap.get_editor_property('post_process_settings');pp.override_dynamic_global_illumination_method=True;pp.dynamic_global_illumination_method=unreal.DynamicGlobalIlluminationMethod.LUMEN;editor_cap.set_editor_property('post_process_settings',pp)
started=time.monotonic();phase=0;v=None;lift=None;cap=None;preview=None;carried=False;lifted=False;captured=False;done_at=0

def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(dt):
    global phase,v,lift,cap,preview,carried,lifted,captured,done_at
    try:
        assert time.monotonic()-started<240,'PIE timeout'
        if phase==0:
            if time.monotonic()-started<3:return
            level.editor_request_begin_play();phase=1;return
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase==1:
            if now<4:return
            named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            v=named['WH_AutonomousForklift'];lift=named['MZ_AGVElevator']
            parts={c.get_name():c for c in v.get_components_by_class(unreal.StaticMeshComponent)}
            assert len(parts)==12,list(parts)
            preview=named['RefinedAGVPreview']
            cap=preview.get_component_by_class(unreal.SceneCaptureComponent2D)
            cap.set_editor_property('capture_every_frame',True)
            phase=2;return
        loc=v.get_actor_transform().transform_location(unreal.Vector(-240,-310,190))
        target=v.get_actor_transform().transform_location(unreal.Vector(10,0,105))
        preview.set_actor_location_and_rotation(loc,unreal.MathLibrary.find_look_at_rotation(loc,target),False,True)
        if phase==2:
            if now<7:return
            cap.capture_scene()
            unreal.RenderingLibrary.export_render_target(game,cap.texture_target,unreal.Paths.project_saved_dir(),'RefinedAGV_Game.png')
            captured=True;v.toggle_power();phase=3;return
        if phase==3:
            named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            box=named['MZ_TransferCargo']
            parts={c.get_name():c for c in v.get_components_by_class(unreal.StaticMeshComponent)}
            for _ in range(12):
                v.advance_simulation(.05);lift.advance_elevator(.05)
                carried=carried or box.get_attach_parent_actor() is not None
                assert v.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,v.get_editor_property('status')
                h=parts['Carriage'].get_attach_parent().get_editor_property('relative_location').z
                if h>1:
                    lifted=True
                    assert abs(parts['LiftStage'].get_editor_property('relative_location').z-h*.5)<.01
                    rest=parts['LiftChains'].static_mesh.get_bounds().box_extent.z*2
                    assert abs(parts['LiftChains'].get_editor_property('relative_scale3d').z-(rest-.5*h)/rest)<.001
                if 'DEMO-L1-L3' in v.get_editor_property('completed_job_ids'):
                    phase=4;done_at=now;break
        if phase==4:
            if now-done_at<3:return
            named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            box=named['MZ_TransferCargo'];system=named['WH_DamageSystem']
            assert carried and lifted and captured
            assert not box.get_attach_parent_actor() and box.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
            assert not system.has_failed(v) and not system.has_failed(box)
            assert lift.get_editor_property('completed_transfers')==1
            print('REFINED_AGV_PIE_PASSED rendered model, chain rig, actual cargo, 1-to-3 transfer, unload physics')
            finish()
    except Exception:
        print('REFINED_AGV_PIE_FAILED',traceback.format_exc());finish()
handle=unreal.register_slate_post_tick_callback(tick)
