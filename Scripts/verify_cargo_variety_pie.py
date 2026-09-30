"""Verify saved cargo variety and real Chaos mass/load transfer in PIE. Never saves test changes."""
import json,time,traceback,math,unreal
from pathlib import Path
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
loads=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseCargo)]
assert len(loads)==262
assert len({str(a.get_editor_property('cargo_id')) for a in loads})==262
assert len({round(a.get_editor_property('gross_mass_kg'),2) for a in loads})==262
assert len({str(a.get_editor_property('cargo_kind')) for a in loads})==16
assert len({a.get_component_by_class(unreal.StaticMeshComponent).static_mesh.get_path_name() for a in loads})==4
assert all("/BoxesPalletsPack/" in a.get_component_by_class(unreal.StaticMeshComponent).static_mesh.get_path_name() for a in loads)
assert len({tuple(round(v,1) for v in (a.get_actor_bounds(False)[1].x,a.get_actor_bounds(False)[1].y,a.get_actor_bounds(False)[1].z)) for a in loads})>100
original={a.get_actor_label():a.get_actor_bounds(False)[0].z for a in loads}
# Render the real rack, not a synthetic example map.
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
origin=unreal.Vector(-650,-1700,195);aim=unreal.Vector(-215,-1050,100)
view=actors.spawn_actor_from_class(unreal.SceneCapture2D,origin,unreal.MathLibrary.find_look_at_rotation(origin,aim))
view.set_actor_label('CargoVarietyPreview')
capture=view.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.texture_target=unreal.RenderingLibrary.create_render_target2d(world,1440,900,unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
capture.fov_angle=72;capture.capture_every_frame=False;capture.always_persist_rendering_state=True
started=time.monotonic();phase=0;phase_time=0;game=None;system=None;named={};pc=None;sample=None;before=0;mass=0;gamecap=None;unstable=[]

def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def tick(dt):
    global phase,phase_time,game,system,named,pc,sample,before,mass,gamecap,unstable
    try:
        assert time.monotonic()-started<240,'Cargo PIE timeout'
        if phase==0:
            if time.monotonic()-started<8:return
            level.editor_request_begin_play();phase=1;return
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase==1:
            if now<4:return
            named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            system=named['WH_DamageSystem'];pc=unreal.GameplayStatics.get_player_controller(game,0)
            loads=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseCargo)
            specs={s.name:s for s in system.get_editor_property('objects')}
            for a in loads:
                body=a.get_component_by_class(unreal.StaticMeshComponent)
                expected=a.get_editor_property('gross_mass_kg')
                assert body.is_simulating_physics(),a.get_actor_label()
                assert abs(body.get_mass()-expected)<.02,(a.get_actor_label(),body.get_mass(),expected)
                assert abs(specs[a.get_actor_label()].mass_kg-expected)<.02
                assert not system.has_failed(a),'Cargo broke at normal startup: '+a.get_actor_label()
                delta=a.get_actor_bounds(False)[0].z-original[a.get_actor_label()]
                if abs(delta)>=12:unstable.append((a.get_actor_label(),round(delta,2),body.static_mesh.get_name()))
            print('CARGO_UNSTABLE_DIAGNOSTIC',unstable)
            sample=named['WH_Drop_Box'];mass=sample.get_editor_property('gross_mass_kg')
            before=system.get_supported_mass(named['WH_Drop_Pallet'])
            assert abs(before-mass)<.2,('Initial pallet load',before,mass)
            sample.set_gross_mass_kg(mass+3.21)
            system.advance_strength(.1)
            assert abs(sample.get_component_by_class(unreal.StaticMeshComponent).get_mass()-mass-3.21)<.02
            assert abs(system.get_supported_mass(named['WH_Drop_Pallet'])-before-3.21)<.05,'Changed cargo mass did not propagate to pallet'
            sample.set_gross_mass_kg(mass)
            print('CARGO_MASS_VERIFIED',len(loads),'physics bodies, live pallet load update')
            gamecap=named['CargoVarietyPreview'].get_component_by_class(unreal.SceneCaptureComponent2D)
            gamecap.capture_scene()
            center=sample.get_actor_bounds(False)[0]
            # Aim the possessed player's own camera at a nearby real cargo for the readout.
            pawn=pc.get_controlled_pawn()
            pawn.set_actor_location(unreal.Vector(center.x+140,center.y,96.5),False,True)
            eye=pawn.get_component_by_class(unreal.CameraComponent).get_world_location()
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye,center))
            phase_time=now;phase=2;return
        if phase==2:
            gamecap.capture_scene()
            if now-phase_time<2:return
            unreal.RenderingLibrary.export_render_target(game,gamecap.texture_target,str(Path(unreal.Paths.project_saved_dir()).resolve()),'CargoVariety.png')
            unreal.SystemLibrary.execute_console_command(game,'Shot showui filename=C:/msc_UnrealProject/msc_vr/Saved/CargoReadout.png',pc)
            phase_time=now;phase=3;return
        if phase==3:
            if now-phase_time<2:return
            assert not unstable,('Unstable initial stacks',unstable)
            print('CARGO_VARIETY_PIE_VERIFIED 262 individual masses, 16 kinds, four Fab closed carton meshes, stable stacks, readout capture')
            finish()
    except Exception:
        print('CARGO_VARIETY_PIE_FAILED',traceback.format_exc());finish()
handle=unreal.register_slate_post_tick_callback(tick)
