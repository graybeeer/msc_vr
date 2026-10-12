"""Saved-map native physics: four colored 1,000 kg boxes; no map save or cargo rewrite."""
import sys
import time
import traceback
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from configure_heavy_test_cargo import LEVEL, PREFIX, SIZE, COLUMNS, KINDS, COLORS, assert_clear_corner

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level(LEVEL)
named = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
labels = [PREFIX + '%02d' % (index + 1) for index in range(4)]
assert len([a for a in actors.get_all_level_actors() if isinstance(a, unreal.WarehouseCargo) and a.actor_has_tag('ExceptionCargo')]) == 4
profiles = named['WH_DamageSystem'].get_editor_property('objects')
for index, label in enumerate(labels):
    cargo = named[label]
    assert isinstance(cargo, unreal.WarehouseCargo) and cargo.actor_has_tag('ExceptionCargo')
    packing = cargo.get_editor_property('packing')
    assert packing.valid and abs(packing.gross_mass_kg - 1000) < .01
    assert abs(packing.net_mass_kg + packing.packaging_mass_kg - 1000) < .01
    assert abs(packing.net_mass_kg - 980) < .01 and abs(packing.packaging_mass_kg - 20) < .01
    assert str(cargo.get_editor_property('cargo_kind')) == KINDS[index]
    assert str(cargo.get_editor_property('cargo_id')) == 'HEAVY-TEST-%02d' % (index + 1)
    spec = next(s for s in profiles if s.name == label)
    assert abs(spec.mass_kg - 1000) < .01 and spec.rated_load_kg == 3000 and not spec.failed
    assert named[label + '_Label'].get_attach_parent_actor() == cargo
    assert str(named[label + '_Label'].get_component_by_class(unreal.TextRenderComponent).get_editor_property('text')) == '1000 kg'
    _, extents = cargo.get_actor_bounds(False)
    assert max(abs(getattr(extents, axis) * 2 - SIZE[i]) for i, axis in enumerate('xyz')) < .1

assert '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower(), 'Run this visual verification with -RenderOffscreen'
preview = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(2250, 1390, 280))
preview.set_actor_label('HeavyTestCargoPreview')
preview.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(preview.get_actor_location(), unreal.Vector(2525, 1820, 85)), False)
capture = preview.get_component_by_class(unreal.SceneCaptureComponent2D)
capture.texture_target = unreal.RenderingLibrary.create_render_target2d(editor.get_editor_world(), 1280, 720, unreal.TextureRenderTargetFormat.RTF_RGBA8)
capture.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
capture.fov_angle = 55
capture.capture_every_frame = False
capture.capture_on_movement = False
unreal.SystemLibrary.execute_console_command(editor.get_editor_world(), 'Editor.AsyncAssetCompilationFinishAll')

started = time.monotonic()
phase = 0
live = None
initial = {}
at = 0
corner_checked = False


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    global phase, live, initial, at, corner_checked
    try:
        assert time.monotonic() - started < 240, 'Heavy test cargo PIE timeout'
        if phase == 0:
            if time.monotonic() - started < 1:
                return
            level.editor_request_begin_play()
            phase = 1
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            initial = {label: live[label].get_actor_bounds(False)[0] for label in labels}
        if not corner_checked:
            if now < .25:
                return
            assert_clear_corner(game, live)
            corner_checked = True
        system = live['WH_DamageSystem']
        for index, label in enumerate(labels):
            cargo = live[label]
            part = body(cargo)
            assert part.is_simulating_physics() and part.is_gravity_enabled(), label
            assert part.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
            assert abs(part.get_mass() - 1000) < .1, ('Actual Chaos mass differs from UI', label, part.get_mass())
            assert not cargo.get_attach_parent_actor(), 'Heavy cargo was attached instead of physically stacked'
            assert not system.has_failed(cargo), ('Heavy cargo failed at startup', label)
            center, extents = cargo.get_actor_bounds(False)
            assert center.z - extents.z > -.8, ('Heavy cargo tunneled under the floor', label)
            assert (center - initial[label]).length() < 5, ('Heavy stack drifted', label, center, initial[label])
            assert abs(cargo.get_actor_rotation().roll) < 2 and abs(cargo.get_actor_rotation().pitch) < 2
            assert live[label + '_Label'].get_attach_parent_actor() == cargo
        if phase == 1:
            if now < 10:
                return
            for lower, upper in ((labels[0], labels[1]), (labels[2], labels[3])):
                supported = system.get_supported_actors(live[lower])
                assert live[upper] in supported and abs(system.get_supported_mass(live[lower]) - 1000) < .1, ('Stack support mass wrong', lower)
                assert system.get_supported_mass(live[upper]) < .1
            found_colors = []
            for index, label in enumerate(labels):
                cargo = live[label]
                assert '1000.00 kg' in str(cargo.get_cargo_description()) and KINDS[index] in str(cargo.get_cargo_description())
                material = body(cargo).get_material(0)
                assert isinstance(material, unreal.MaterialInstanceConstant), 'Tint material missing'
                parameter = next(name for name in unreal.MaterialEditingLibrary.get_vector_parameter_names(material)
                                 if str(name).lower() in ('heavytint', 'heavytesttint', 'tint', 'basecolortint', 'colortint'))
                tint = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(material, parameter)
                actual = (tint.r, tint.g, tint.b, tint.a)
                assert max(abs(a - b) for a, b in zip(actual, COLORS[index])) < .001
                found_colors.append(actual)
                assert body(cargo).get_physics_linear_velocity().length() < 10, ('Heavy stack did not settle', label)
            assert len(set(found_colors)) == 4
            print('HEAVY_TEST_CARGO_STACKS_PASSED', '10 simulated seconds; four native 1000 kg bodies; lower boxes support 1000 kg each; intact textures/four tints')
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            pawn = pc.get_controlled_pawn()
            assert hasattr(pawn, 'try_pickup_cargo') and hasattr(pawn, 'get_held_cargo'), 'Saved map did not spawn the native player'
            pawn.do_move(0, 0)
            pawn.set_actor_location(unreal.Vector(2460, 1660, 96.5), False, True)
            assert pawn.get_held_cargo() is None
            camera = pawn.get_component_by_class(unreal.CameraComponent)
            for label in labels:
                assert (live[label].get_actor_bounds(False)[0] - camera.get_world_location()).length() < 300, 'Pickup denial tested out of reach'
                assert not pawn.try_pickup_cargo(live[label]), 'Player lifted 1000 kg'
            assert pawn.get_held_cargo() is None
            print('HEAVY_TEST_CARGO_HAND_PICKUP_DENIED', 'native interaction within reach; existing 30 kg hand limit retained')
            shot = live['HeavyTestCargoPreview'].get_component_by_class(unreal.SceneCaptureComponent2D)
            shot.set_editor_property('hidden_actors',[pawn])
            shot.capture_scene()
            at = now
            phase = 2
            return
        if phase == 2:
            if now - at < .2:
                return
            output = Path(unreal.Paths.project_saved_dir()) / 'Testing'
            output.mkdir(parents=True, exist_ok=True)
            shot = live['HeavyTestCargoPreview'].get_component_by_class(unreal.SceneCaptureComponent2D)
            unreal.RenderingLibrary.export_render_target(game, shot.texture_target, str(output), 'HeavyTestCargo.png')
            assert (output / 'HeavyTestCargo.png').is_file(), 'Heavy cargo visual capture was not exported'
            print('HEAVY_TEST_CARGO_PIE_PASSED four exception cargos; mass/UI/kinds/colors/labels/support/stability/pickup denial; visual Saved/Testing/HeavyTestCargo.png')
            finish()
    except Exception:
        unreal.log_error(traceback.format_exc())
        finish()


handle = unreal.register_slate_post_tick_callback(tick)
