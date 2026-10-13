"""Native saved-map dock geometry/idempotence and PIE floor/clearance/cargo checks; never saves."""
from pathlib import Path
import json
import sys
import time
import traceback
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from configure_multi_truck_docks import BAYS, GROUPS, LEVEL, PLATFORM_SIZE, REAR_Y, configure_multi_truck_docks

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level(LEVEL)
named = {a.get_actor_label(): a for a in editor.get_all_level_actors()}


def bounds(actor):
    center, extent = actor.get_actor_bounds(False)
    return center - extent, center + extent


def geometry(named):
    trucks = [named[f'EXT_ParkedTruck_{index:02}'] for index, _, _ in BAYS]
    assert sum(name.startswith('EXT_ParkedTruck_') for name in named) == 3
    low, high = bounds(named['EXT_DockPlatform'])
    assert abs(high.z) < .01
    assert ((high - low) - unreal.Vector(*PLATFORM_SIZE)).length() < .1
    assert abs(low.y + 2950) < .1 and abs(high.y + 1980) < .1
    assert bounds(named['EXT_RampLanding'])[1].x >= low.x - .1
    rows = []
    for index, x, title in BAYS:
        truck = named[f'EXT_ParkedTruck_{index:02}']
        prefix = 'TRK_' if index == 2 else f'TRK_Dock{index:02}_'
        floor = named[prefix + 'Floor']
        roof = named[prefix + 'Roof']
        floor_low, floor_high = bounds(floor)
        roof_low, _ = bounds(roof)
        cab_low, cab_high = bounds(truck)
        assert abs((cab_low.x + cab_high.x) / 2 - x) < .1
        assert abs(cab_high.y - REAR_Y) < .1
        assert abs(cab_low.z + 120) < .1
        assert abs((cab_high.y - cab_low.y) - 732.138) < .2
        assert abs((cab_high.x - cab_low.x) - 306.289) < .2
        assert (truck.get_actor_scale3d() - unreal.Vector(1, 1, 1)).length() < .001
        assert abs(floor_high.y - floor_low.y - 493) < .1
        assert abs(floor_high.x - floor_low.x - 232.46) < .1
        assert abs(roof_low.z - floor_high.z - 216) < .1
        group = [a for a in named.values() if GROUPS[index - 1] in [str(t) for t in a.tags]]
        expected = 70 if index == 2 else 22  # chassis + 21 shell/light/leveller parts, plus 48 original cartons
        assert len(group) == expected, (index, len(group))
        assert all('LayoutType:Other' in [str(t) for t in a.tags] for a in group)
        assert floor.static_mesh_component.get_collision_profile_name() == 'BlockAll'
        assert str(named[f'EXT_DockAreaUI_{index:02}'].get_editor_property('area_name')) == title
        rows.append({'bay': index, 'center_x_cm': x, 'group_actors': len(group),
                     'chassis_cm': [cab_high.x - cab_low.x, cab_high.y - cab_low.y],
                     'cargo_floor_cm': [floor_high.x - floor_low.x, floor_high.y - floor_low.y],
                     'interior_height_cm': roof_low.z - floor_high.z})
    for first, second in zip(trucks, trucks[1:]):
        assert bounds(second)[0].x - bounds(first)[1].x > 140, 'Truck mirrors overlap'
    assert sum(name.startswith('TRK_Cargo_') for name in named) == 48
    return rows


rows = geometry(named)
before = {name: a.get_actor_transform() for name, a in named.items()}
profiles = len(named['WH_DamageSystem'].get_editor_property('objects'))
configure_multi_truck_docks()
after = {a.get_actor_label(): a for a in editor.get_all_level_actors()}
assert set(after) == set(before), 'Repeated application creates extra actors'
assert len(after['WH_DamageSystem'].get_editor_property('objects')) == profiles
for name, pose in before.items():
    actual = after[name].get_actor_transform()
    assert (actual.translation - pose.translation).length() < .1, ('Repeated application drifts', name)
geometry(after)
print('MULTI_TRUCK_DOCK_GEOMETRY_PASSED', json.dumps(rows, ensure_ascii=False))

for actor in after.values():
    if isinstance(actor, unreal.WarehouseForklift):
        actor.set_editor_property('auto_start', False)

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
capture = editor.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(2300, -5300, 1700),
    unreal.MathLibrary.find_look_at_rotation(unreal.Vector(2300, -5300, 1700), unreal.Vector(350, -2700, 50)))
capture.set_actor_label('MultiTruckDockPreview')
component = capture.get_component_by_class(unreal.SceneCaptureComponent2D)
component.texture_target = unreal.RenderingLibrary.create_render_target2d(world, 1600, 1000, unreal.TextureRenderTargetFormat.RTF_RGBA8)
component.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
component.fov_angle = 65
started = time.monotonic()
phase = 0
initial = {}
report = {'bays': rows, 'ok': False}


def finish(ok):
    report['ok'] = ok
    report['wall_seconds'] = round(time.monotonic() - started, 2)
    path = Path(unreal.Paths.project_saved_dir()) / 'Testing' / 'MultiTruckDocksReport.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def hit(world, start, end):
    result = unreal.SystemLibrary.line_trace_single(world, start, end,
        unreal.TraceTypeQuery.ECC_VISIBILITY, True, [], unreal.DrawDebugTrace.NONE, True)
    native = result if isinstance(result, unreal.HitResult) else next(value for value in result if isinstance(value, unreal.HitResult))
    data = native.to_tuple()
    assert data[0], ('Missing collision support', start, end)
    return data[5], data[9]


def tick(dt):
    global phase, initial
    try:
        assert time.monotonic() - started < 180, 'Dock PIE timeout'
        if phase == 0:
            if time.monotonic() - started < 3:
                return
            level.editor_request_begin_play()
            phase = 1
            return
        game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
        boxes = {name: a for name, a in live.items() if name.startswith('TRK_Cargo_')}
        if phase == 1:
            if now < .3:
                return
            assert len(boxes) == 48
            initial = {name: actor.get_actor_location() for name, actor in boxes.items()}
            phase = 2
            return
        if now < 8:
            return
        for name, actor in boxes.items():
            assert (actor.get_actor_location() - initial[name]).length() < 8, ('Original truck cargo unstable', name)
            part = actor.get_component_by_class(unreal.StaticMeshComponent)
            assert part.is_simulating_physics(), name
            assert abs(part.get_mass() - actor.get_editor_property('gross_mass_kg')) < .1
        supports = []
        for index, x, _ in BAYS:
            prefix = 'TRK_' if index == 2 else f'TRK_Dock{index:02}_'
            floor_low, floor_high = bounds(live[prefix + 'Floor'])
            # Empty rear 150cm/centre aisle is accessible in all three trucks.
            for y in (floor_high.y - 40, floor_high.y - 120, (floor_low.y + floor_high.y) / 2):
                point, actor = hit(game, unreal.Vector(x, y, 160), unreal.Vector(x, y, -70))
                assert actor.get_actor_label() in (prefix + 'Floor', prefix + 'DockLeveller'), actor.get_actor_label()
                assert -25 < point.z < 10, point
                supports.append([index, round(y, 2), round(point.z, 2)])
            # Warehouse-to-leveller walkway stays at warehouse height.
            point, actor = hit(game, unreal.Vector(x, -2600, 180), unreal.Vector(x, -2600, -70))
            assert actor.get_actor_label() == 'EXT_DockPlatform'
            assert abs(point.z) < .1
        report['native_floor_samples'] = supports
        report['stable_original_truck_cartons'] = len(boxes)
        if '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower():
            native = live['MultiTruckDockPreview'].get_component_by_class(unreal.SceneCaptureComponent2D)
            native.capture_scene()
            unreal.RenderingLibrary.export_render_target(game, native.texture_target, unreal.Paths.project_saved_dir(), 'MultiTruckDocks.png')
        print('MULTI_TRUCK_DOCKS_PIE_PASSED', '3 occupied open interiors, native floor/bridge contacts, 48 stable dynamic cartons')
        finish(True)
    except Exception:
        report['failure'] = traceback.format_exc()
        print('MULTI_TRUCK_DOCKS_PIE_FAILED', report['failure'])
        finish(False)


handle = unreal.register_slate_post_tick_callback(tick)
