"""Saved-map PIE wall fade checks through normal controller ticks; never saves the map.

Rendered runs also capture the same real wall before/after fading. -NullRHI runs
verify material parameters and physical state, and explicitly skip screenshots.
"""
import json
import time
import traceback
from pathlib import Path

import unreal


OUTPUT = Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/ObserverWallFade')
OUTPUT.mkdir(parents=True, exist_ok=True)
COMMAND_LINE = unreal.SystemLibrary.get_command_line().lower()
RENDERED = '-nullrhi' not in COMMAND_LINE
KEEP_EDITOR_OPEN = '-keeptesteditoropen' in COMMAND_LINE
SHOTS = ('NearOpaque', 'NearFaded', 'AwayRestored', 'PausedNearFaded')
for name in SHOTS:
    (OUTPUT / (name + '.png')).unlink(missing_ok=True)

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report = dict(version='observer-wall-fade-1-native-controller-ticks', ok=False,
              rendered=RENDERED, checks=[], screenshots=[],
              scope='Actual saved warehouse, native PC ticks and material/physics assertions; no OS input claim')
started = at = time.monotonic()
phase = 'start'
handle = None
pc = game = camera = wall = None
wall_parts = []
baseline = {}
paused_time = None
prepared = False


def finish(ok, detail):
    report.update(ok=ok, detail=detail)
    (OUTPUT / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('OBSERVER_WALL_FADE_PIE_PASSED' if ok else 'OBSERVER_WALL_FADE_PIE_FAILED', detail)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if not KEEP_EDITOR_OPEN:
        unreal.SystemLibrary.quit_editor()


def materials(part):
    return tuple(part.get_materials())


def physical_state(part):
    body = part.get_editor_property('body_instance')
    return (part.get_editor_property('static_mesh'), part.get_collision_enabled(), part.get_collision_object_type(),
            part.is_simulating_physics(), part.get_editor_property('mobility'),
            part.get_editor_property('render_custom_depth'), part.get_editor_property('custom_depth_stencil_value'),
            body.get_editor_property('phys_material_override'), body.get_editor_property('override_mass'),
            body.get_editor_property('mass_in_kg_override'), part.get_owner().get_actor_enable_collision(),
            part.get_owner().get_editor_property('hidden'))


def check_preserved(restored=False):
    for part, (saved_materials, state) in baseline.items():
        assert physical_state(part) == state, 'Wall mesh/collision/physics/custom-depth changed: ' + part.get_path_name()
        if restored:
            assert materials(part) == saved_materials, 'Original material pointers not restored: ' + part.get_path_name()


def faded_opacities():
    result = []
    for part in wall_parts:
        for material in materials(part):
            if isinstance(material, unreal.MaterialInstanceDynamic):
                result.append(float(material.get_scalar_parameter_value('ObserverWallOpacity')))
    return result


def check_faded(label):
    values = faded_opacities()
    assert values and min(values) < .6, 'Front near wall was not faded: ' + repr(values)
    assert all(0 <= value <= 1 for value in values), 'Invalid wall opacity'
    check_preserved()
    report['checks'].append(label)
    report[label + '_opacities'] = values


def aim_near(backwards=False):
    origin = near_position
    aim = origin - inward * 100 if backwards else wall_point + inward * 600
    camera.set_actor_location_and_rotation(origin, unreal.MathLibrary.find_look_at_rotation(origin, aim), False, True)


def aim_away():
    origin = near_position - inward * 10000
    camera.set_actor_location_and_rotation(origin, unreal.MathLibrary.find_look_at_rotation(origin, wall_point), False, True)


def shot(name):
    if RENDERED:
        unreal.SystemLibrary.execute_console_command(
            game, 'Shot showui filename=' + str(OUTPUT / (name + '.png')).replace('\\', '/') + ' -nosuffix', pc)


try:
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson'), 'Saved warehouse map missing'
    saved_actors = actors.get_all_level_actors()
    assert not any(a.get_actor_label().startswith('LayoutRotationFloor_') for a in saved_actors), 'A temporary layout-test floor was saved in the map'
    for actor in saved_actors:
        if actor.get_actor_label() == 'ObserverWallFadeTestCamera':
            actors.destroy_actor(actor)
    assert unreal.load_asset('/Game/Warehouse/Materials/ObserverWalls/DA_ObserverWallPalette'), 'Prepared observer wall palette missing'
    saved_walls = [a for a in saved_actors if a.actor_has_tag('ObserverFadeWall')]
    assert len(saved_walls) >= 5, 'ObserverFadeWall preparation is not saved in the real map'
    # Use the long west wall, not a spawned test cube. Its thin dimension supplies
    # the wall normal, while the warehouse-facing direction is fixed by its name.
    saved_wall = next((a for a in saved_walls if a.get_actor_label().startswith('WH_Wall_W')), None)
    assert saved_wall, 'Saved west warehouse wall is missing'
    wall_label = saved_wall.get_actor_label()
    centre, extent = saved_wall.get_actor_bounds(False)
    assert extent.x < extent.y and extent.x < 100, 'West wall is not the expected thin X plane'
    assert extent.z > 120, 'West wall is too short for a ground-floor camera'
    wall_point = unreal.Vector(centre.x, centre.y, max(centre.z - extent.z + 120, min(200, centre.z + extent.z - 120)))
    inward = unreal.Vector(1, 0, 0)
    near_position = wall_point - inward * (extent.x + 90)
    fixture = actors.spawn_actor_from_class(unreal.CameraActor, near_position,
                                          unreal.MathLibrary.find_look_at_rotation(near_position, wall_point + inward * 600))
    assert fixture, 'Test camera could not be created'
    fixture.set_actor_label('ObserverWallFadeTestCamera')
    fixture.get_component_by_class(unreal.CameraComponent).set_field_of_view(70)
    vehicles = [a for a in saved_actors if isinstance(a, unreal.WarehouseForklift)]
    assert vehicles, 'Saved operating warehouse forklifts missing'
    for vehicle in vehicles:
        vehicle.set_editor_property('auto_start', False)
    report.update(wall=wall_label, saved_wall_count=len(saved_walls), saved_forklift_count=len(vehicles))
    prepared = True
except Exception:
    finish(False, traceback.format_exc())


def tick(dt):
    global phase, at, pc, game, camera, wall, wall_parts, baseline, paused_time
    try:
        assert time.monotonic() - started < 180, 'Wall fade PIE timed out'
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        if phase == 'settle':
            if unreal.GameplayStatics.get_time_seconds(game) < 3:
                return
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            wall = next(a for a in unreal.GameplayStatics.get_all_actors_with_tag(game, 'ObserverFadeWall')
                        if a.get_actor_label() == wall_label)
            camera = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.CameraActor)
                          if a.get_actor_label() == 'ObserverWallFadeTestCamera')
            walls = unreal.GameplayStatics.get_all_actors_with_tag(game, 'ObserverFadeWall')
            parts = [part for actor in walls for part in actor.get_components_by_class(unreal.StaticMeshComponent)]
            assert parts, 'Tagged walls have no saved mesh components'
            baseline = {part: (materials(part), physical_state(part)) for part in parts}
            wall_parts = wall.get_components_by_class(unreal.StaticMeshComponent)
            assert wall_parts and any(material for part in wall_parts for material in materials(part)), 'West wall has no material'
            assert not pc.is_observer_view()
            unreal.SystemLibrary.execute_console_command(game, 'r.SetRes 1280x720w', pc)
            unreal.SystemLibrary.execute_console_command(game, 'DisableAllScreenMessages', pc)
            pc.set_view_target_with_blend(camera, 0)
            aim_near()
            at = time.monotonic()
            phase = 'opaque'
            return
        if time.monotonic() - at < 1:
            return
        if phase == 'opaque':
            check_preserved(True)
            shot('NearOpaque')
            phase = 'enable_observer'
        elif phase == 'enable_observer':
            pc.toggle_observer_view()
            pc.set_layout_editing(False)
            assert pc.is_observer_view()
            # A separate view target keeps normal observer input smoothing from
            # moving this precisely framed camera. The real PC tick still owns Update.
            pc.set_view_target_with_blend(camera, 0)
            aim_near()
            phase = 'front'
        elif phase == 'front':
            check_faded('front_near_fades')
            shot('NearFaded')
            phase = 'face_back'
        elif phase == 'face_back':
            aim_near(True)
            phase = 'behind'
        elif phase == 'behind':
            assert all(materials(part) == baseline[part][0] for part in wall_parts), 'Wall behind the camera still fades'
            check_preserved()
            report['checks'].append('behind_camera_restores')
            aim_away()
            phase = 'away'
        elif phase == 'away':
            check_preserved(True)
            shot('AwayRestored')
            report['checks'].append('outside_range_restores')
            phase = 'enable_pause'
        elif phase == 'enable_pause':
            pc.set_observer_floor(0)
            pc.set_layout_editing(True)
            assert unreal.GameplayStatics.is_game_paused(game), 'Layout mode did not pause simulation'
            paused_time = unreal.GameplayStatics.get_time_seconds(game)
            pc.set_view_target_with_blend(camera, 0)
            aim_near()
            phase = 'paused_front'
        elif phase == 'paused_front':
            assert abs(unreal.GameplayStatics.get_time_seconds(game) - paused_time) < .01
            check_faded('paused_realtime_front_fades')
            shot('PausedNearFaded')
            phase = 'paused_face_away'
        elif phase == 'paused_face_away':
            aim_away()
            phase = 'paused_away'
        elif phase == 'paused_away':
            assert abs(unreal.GameplayStatics.get_time_seconds(game) - paused_time) < .01
            check_preserved(True)
            report['checks'].append('paused_realtime_away_restores')
            pc.toggle_layout_editing()
            assert not unreal.GameplayStatics.is_game_paused(game)
            aim_near()
            phase = 'exit_prepare'
        elif phase == 'exit_prepare':
            check_faded('near_before_observer_exit')
            pc.toggle_observer_view()
            assert not pc.is_observer_view() and pc.get_view_target() == pc.get_controlled_pawn()
            check_preserved(True)
            report['checks'].append('observer_exit_restores_original_pointers')
            phase = 'finish'
        elif phase == 'finish':
            check_preserved(True)
            if RENDERED:
                assert all((OUTPUT / (name + '.png')).is_file() for name in SHOTS), 'Requested rendered capture missing'
                report['screenshots'] = [str(OUTPUT / (name + '.png')) for name in SHOTS]
            report['checks'].append('all_wall_mesh_collision_physics_customdepth_preserved')
            finish(True, 'Front-only near fade, range/observer exit restoration and paused real-time updates verified through native controller ticks')
            return
        at = time.monotonic()
    except Exception:
        finish(False, traceback.format_exc())


if prepared:
    handle = unreal.register_slate_post_tick_callback(tick)
