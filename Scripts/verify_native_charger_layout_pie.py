"""Move a copied charging bay, then run native charging/exit in Entry PIE.

Never saves. Copies the saved AGV meshes/materials/authoring transforms before
PIE; runtime poses, velocities, mass, physics and AI state are never assigned.
Only the station moves through the real paused layout transaction. Battery
time is accelerated only after actual electrical docking is confirmed.
"""
import json
import math
import os
import time
import traceback
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
source = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'WH_AutonomousForklift')
source_station = source.get_editor_property('charging_station')
assert source_station and source_station.get_editor_property('assigned_vehicle') == source


def authored_parts(actor):
    result = {}
    for part in actor.get_components_by_class(unreal.StaticMeshComponent):
        mesh = part.static_mesh
        if not mesh:
            continue
        result[part.get_name()] = (mesh.get_path_name(),
            [part.get_material(i).get_path_name() if part.get_material(i) else None for i in range(part.get_num_materials())])
    return result


vehicle_parts, station_parts = authored_parts(source), authored_parts(source_station)
def actor_local_poses(actor):
    # Detached primitive relative transforms are world transforms. Normalize
    # every source part against the actor, independently of its attachment.
    return {p.get_name(): unreal.MathLibrary.make_relative_transform(p.get_world_transform(), actor.get_actor_transform())
            for p in actor.get_components_by_class(unreal.SceneComponent) if p != actor.root_component}


vehicle_poses, station_poses = actor_local_poses(source), actor_local_poses(source_station)
source_damage = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.WarehouseDamageSystem))
source_strength = next(s for s in source_damage.get_editor_property('objects') if source in s.members)
strength_values = {name: source_strength.get_editor_property(name) for name in (
    'mass_kg', 'rated_load_kg', 'impact_yield_j', 'impact_failure_j', 'overload_seconds', 'failure')}
properties = {name: source.get_editor_property(name) for name in (
    'empty_travel_speed_cm', 'loaded_travel_speed_cm', 'fork_leading_speed_cm', 'rated_load_kg',
    'max_fork_height_cm', 'vehicle_mass_kg', 'battery_voltage', 'battery_capacity_ah',
    'charge_below_percent', 'resume_at_percent')}
station_properties = {name: source_station.get_editor_property(name) for name in ('power_kw', 'efficiency')}
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
settings = unreal.GameplayStatics.get_all_actors_of_class(editor.get_editor_world(), unreal.WorldSettings)[0]
settings.set_editor_property('default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(300, 0, -10))
floor.set_actor_label('NativeChargerFloor')
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.set_actor_scale3d(unreal.Vector(30, 20, .2))
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-800, -600, 125))
vehicle = actors.spawn_actor_from_class(unreal.WarehouseForklift, unreal.Vector(220, 0, 0))
vehicle.set_actor_label('NativeChargerAGV')
station = actors.spawn_actor_from_class(unreal.WarehouseChargingStation, unreal.Vector())
station.set_actor_label('NativeMovedCharger')


def copy_parts(actor, values):
    parts = {p.get_name(): p for p in actor.get_components_by_class(unreal.StaticMeshComponent)}
    for name, (mesh, materials) in values.items():
        assert name in parts, ('Copied production rig part missing', name)
        part = parts[name]
        part.set_static_mesh(unreal.load_asset(mesh))
        for i, path in enumerate(materials):
            if path:
                part.set_material(i, unreal.load_asset(path))


def copy_poses(actor, values):
    def depth(part):
        value = 0
        while part.get_attach_parent():
            part = part.get_attach_parent()
            value += 1
        return value
    pose = actor.get_actor_transform()
    for part in sorted(actor.get_components_by_class(unreal.SceneComponent), key=depth):
        if part == actor.root_component:
            continue
        assert part.get_name() in values, ('Production part transform missing', part.get_name())
        part.set_world_transform(unreal.MathLibrary.compose_transforms(values[part.get_name()], pose), False, True)
        actual_local = unreal.MathLibrary.make_relative_transform(part.get_world_transform(), pose)
        assert unreal.MathLibrary.nearly_equal_transform_transform(actual_local, values[part.get_name()], .001, .000001, .000001), ('Copied part actor-local pose changed', part.get_name())


copy_parts(vehicle, vehicle_parts)
copy_parts(station, station_parts)
copy_poses(vehicle, vehicle_poses)
copy_poses(station, station_poses)
for name, value in properties.items():
    vehicle.set_editor_property(name, value)
for name, value in station_properties.items():
    station.set_editor_property(name, value)
vehicle.set_editor_property('auto_start', False)
vehicle.set_editor_property('autonomous_mode', True)
vehicle.set_editor_property('pending_jobs', [])
vehicle.set_editor_property('target_pallet', None)
vehicle.set_editor_property('battery_percent', 29)
vehicle.set_editor_property('battery_time_scale', 1)
vehicle.set_editor_property('charging_station', station)
station.set_editor_property('assigned_vehicle', vehicle)
station.tags = list(station.tags) + [unreal.Name('LayoutFixed')]  # Old saved metadata remains selectable.
layout_actor = actors.spawn_actor_from_class(unreal.WarehouseLayoutEditor, unreal.Vector())
layout_actor.set_actor_label('NativeChargerLayout')
damage = actors.spawn_actor_from_class(unreal.WarehouseDamageSystem, unreal.Vector())
damage.set_actor_label('NativeChargerDamage')
damage.set_editor_property('objects', [unreal.WarehouseStrength(name='NativeChargerFloor', members=[floor],
    mass_kg=50000, rated_load_kg=100000, impact_yield_j=10000, impact_failure_j=100000,
    failure=unreal.WarehouseFailure.STRUCTURE),
    unreal.WarehouseStrength(name='NativeChargerAGV', members=[vehicle], **strength_values)])

started = time.monotonic()
phase = 0
game = rig = bay = layout = strength = None
requested_at = charge_at = initial_position = dock_position = None
initial_battery = charge_battery = None
last_log = -10
seen = set()
samples = []
placement_diagnostics = []
report_path = os.path.join(unreal.Paths.project_saved_dir(), 'Testing', 'NativeChargerLayout.json')


def sample(now):
    pose = rig.get_actor_transform()
    point = pose.translation
    forward, dock_forward = rig.get_actor_forward_vector(), bay.get_actor_forward_vector()
    alignment = sum(getattr(forward, key) * getattr(dock_forward, key) for key in ('x', 'y', 'z'))
    return {'simulation_seconds': now, 'phase': phase, 'ai_state': str(rig.get_editor_property('ai_state')),
        'status': str(rig.get_editor_property('status')), 'position_cm': [point.x, point.y, point.z],
        'yaw_deg': rig.get_actor_rotation().yaw,
        'pitch_deg': rig.get_actor_rotation().pitch, 'roll_deg': rig.get_actor_rotation().roll,
        'up_z': rig.get_actor_up_vector().z, 'forward_alignment_error_deg': math.degrees(math.acos(max(-1, min(1, alignment)))),
        'speed_cm_s': rig.root_component.get_physics_linear_velocity().length(),
        'angular_speed_deg_s': rig.root_component.get_physics_angular_velocity_in_degrees().length(),
        'physical_mass_kg': rig.get_physical_mass_kg(), 'battery_percent': rig.get_editor_property('battery_percent'),
        'dock_distance_cm': (point - bay.get_actor_location()).length(), 'electrically_docked': bay.is_docked(rig)}


def finish(ok, detail):
    if rig:
        rig.set_editor_property('battery_time_scale', 1)
    if layout and layout.get_editor_property('editing'):
        layout.set_editing(False, 0)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, 'w', encoding='utf-8') as output:
        json.dump({'passed': ok, 'detail': detail, 'seen_states': sorted(seen), 'samples': samples,
            'placement_diagnostics': placement_diagnostics,
            'scope': 'Copied production rig/bay in Entry; actual layout movement, native charging approach/dock/battery/exit. No OS UI or warehouse-wide traffic test.'}, output, ensure_ascii=False, indent=2)
    print('NATIVE_CHARGER_LAYOUT_PASSED' if ok else 'NATIVE_CHARGER_LAYOUT_FAILED', detail)
    unreal.SystemLibrary.execute_console_command(game, 'FLUSHLOG')
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def diagnose_floor_placement(native_before):
    """Read the same all-component bounds/corner rays as native PlacementClear."""
    centre, extent = bay.get_actor_bounds(False)
    low, high = centre - extent, centre + extent
    support = min(0., math.floor((low.z + 5.) / 10.) * 10.)
    ground = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.StaticMeshActor)
                  if a.get_actor_label() == 'NativeChargerFloor')
    native_floor = unreal.WarehouseDamageSystem.get_physical_bounds(ground)
    print('NATIVE_CHARGER_LAYOUT_BOUNDS', 'bay origin', bay.get_actor_location(), 'all-component min/max', low, high,
          'probe support height', support, 'floor pose', ground.get_actor_transform(),
          'floor native min/max', native_floor.min, native_floor.max,
          'floor collision', ground.static_mesh_component.get_collision_enabled(),
          'floor visibility response', ground.static_mesh_component.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY))
    print('NATIVE_CHARGER_PHYSICAL_BOUNDS', 'before drag valid', native_before.is_valid,
          'native original min/max', native_before.min, native_before.max,
          'native proposed min/max', native_before.min + unreal.Vector(100, 0, 0), native_before.max + unreal.Vector(100, 0, 0))
    for part in bay.get_components_by_class(unreal.SceneComponent):
        print('NATIVE_CHARGER_PART_BOUNDS', part.get_name(), 'world', part.get_world_transform(),
              'local mesh bounds', part.get_local_bounds() if isinstance(part, unreal.StaticMeshComponent) else None)
    for x in (low.x + 2, high.x - 2):
        for y in (low.y + 2, high.y - 2):
            start = unreal.Vector(x, y, support + 3)
            end = start - unreal.Vector(0, 0, 12)
            result = unreal.SystemLibrary.line_trace_single(game, start, end, unreal.TraceTypeQuery.ECC_VISIBILITY,
                False, [bay], unreal.DrawDebugTrace.NONE, True)
            native = result if isinstance(result, unreal.HitResult) else next(
                (v for v in (result or ()) if isinstance(v, unreal.HitResult)), None)
            hit = native.to_tuple() if native else None
            record = {'start_cm': [start.x, start.y, start.z], 'end_cm': [end.x, end.y, end.z],
                'blocking_hit': bool(hit and hit[0]),
                'actor': hit[9].get_actor_label() if hit and hit[9] else None,
                'normal': [hit[7].x, hit[7].y, hit[7].z] if hit else None,
                'impact_cm': [hit[5].x, hit[5].y, hit[5].z] if hit else None}
            placement_diagnostics.append(record)
            print('NATIVE_CHARGER_LAYOUT_FLOOR_PROBE', record)
    unreal.SystemLibrary.execute_console_command(game, 'FLUSHLOG')


def tick(dt):
    global phase, game, rig, bay, layout, strength, requested_at, charge_at, initial_position, dock_position
    global initial_battery, charge_battery, last_log
    try:
        assert time.monotonic() - started < 180, 'Native charger wall timeout'
        if phase == 0:
            if time.monotonic() - started < .5:
                return
            level.editor_request_begin_play()
            phase = 1
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if rig is None:
            named = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            rig, bay, layout, strength = [named[name] for name in ('NativeChargerAGV', 'NativeMovedCharger', 'NativeChargerLayout', 'NativeChargerDamage')]
            unreal.SystemLibrary.execute_console_command(game, 't.MaxFPS 60')
        assert rig.root_component.is_simulating_physics(), 'Native vehicle body simulation was disabled'
        assert rig.root_component.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
        assert abs(rig.get_physical_mass_kg() - 1000) < .5, 'Actual vehicle mass changed'
        assert rig.get_actor_up_vector().z > .999, 'Vehicle lost its actual upright pose'
        assert not strength.has_failed(rig), 'Native vehicle damage during charging'
        state = rig.get_editor_property('ai_state')
        seen.add(str(state))
        if now - last_log >= 1:
            record = sample(now)
            samples.append(record)
            print('NATIVE_CHARGER_DIAGNOSTIC', record, 'bay', bay.get_actor_transform(),
                  'occupant', bay.get_editor_property('occupant'), 'emergency', rig.get_editor_property('emergency_reason'))
            unreal.SystemLibrary.execute_console_command(game, 'FLUSHLOG')
            last_log = now
        assert state != unreal.WarehouseAIState.FAULT, ('Native charging fault', sample(now))
        assert not rig.get_editor_property('emergency_blocked'), ('Native charging safety stop', sample(now))
        if phase == 1:
            if now < 2:
                return
            assert rig in bay.get_linked_vehicles()
            initial_position = rig.get_actor_location()
            old_bay = bay.get_actor_location()
            original_native_bounds = unreal.WarehouseDamageSystem.get_physical_bounds(bay)
            assert (initial_position - old_bay).length() > 5
            layout.set_editing(True, 0)
            assert unreal.GameplayStatics.is_game_paused(game)
            layout.set_category(unreal.WarehouseLayoutCategory.CHARGER)
            assert layout.begin_drag(bay, old_bay), layout.get_editor_property('status')
            layout.update_drag(old_bay + unreal.Vector(100, 0, 800))
            assert (bay.get_actor_location() - old_bay - unreal.Vector(100, 0, 0)).length() < .1
            assert (rig.get_actor_location() - initial_position).length() < .1, 'Away vehicle moved with the charger'
            assert rig.root_component.is_simulating_physics(), 'Away vehicle entered the layout physics-disabled group'
            diagnose_floor_placement(original_native_bounds)
            assert layout.end_drag(True), layout.get_editor_property('status')
            layout.set_editing(False, 0)
            assert not unreal.GameplayStatics.is_game_paused(game)
            dock_position = bay.get_actor_location()
            initial_battery = rig.get_editor_property('battery_percent')
            rig.request_charging(False)
            requested_at, phase = now, 2
            print('NATIVE_CHARGER_MOVED_NATIVE_REQUEST', 'old_bay', old_bay, 'new_bay', dock_position,
                  'unchanged_vehicle', initial_position, 'battery', initial_battery)
            return
        assert now - requested_at < 90, ('Native charging flow simulation timeout', sample(now))
        if phase == 2:
            if state != unreal.WarehouseAIState.CHARGING:
                return
            assert bay.is_docked(rig), ('Charging began without electrical contact', sample(now))
            assert bay.get_editor_property('occupant') == rig, 'Vehicle did not own the charging reservation'
            assert (rig.get_actor_location() - initial_position).length() > 100, 'Vehicle did not drive to the moved charging bay'
            charge_battery = rig.get_editor_property('battery_percent')
            assert charge_battery <= initial_battery + .1, 'Battery increased before charging contact'
            charge_at, phase = now, 3
            rig.set_editor_property('battery_time_scale', 3600)
            print('NATIVE_CHARGER_DOCKING_PASSED', sample(now), 'BatteryTimeScale=3600 only while CHARGING; movement/physics time unchanged')
            return
        if phase == 3:
            if state == unreal.WarehouseAIState.CHARGING:
                assert bay.is_docked(rig), ('Charging contact lost', sample(now))
                return
            assert state == unreal.WarehouseAIState.LEAVE_CHARGER, ('Unexpected charging transition', sample(now))
            rig.set_editor_property('battery_time_scale', 1)
            assert rig.get_editor_property('battery_percent') >= 79.9
            assert rig.get_editor_property('battery_percent') > charge_battery + 1, 'Battery did not actually charge'
            phase = 4
            print('NATIVE_CHARGER_BATTERY_PASSED', 'percent', rig.get_editor_property('battery_percent'), 'seconds', now - charge_at)
            return
        if phase == 4 and state == unreal.WarehouseAIState.READY:
            exit_goal = dock_position + bay.get_actor_forward_vector() * 120
            assert (rig.get_actor_location() - exit_goal).length() < 2, ('Actual departure did not reach the charger exit', sample(now))
            assert bay.get_editor_property('occupant') is None, 'Charger reservation was not released after actual exit'
            assert not bay.is_docked(rig), 'Vehicle remained docked after exit'
            samples.append(sample(now))
            finish(True, '100cm station-only layout move; finite native travel to strict dock, battery increase, real 120cm exit and READY; preserved 1000kg physics')
    except Exception:
        if rig and game:
            samples.append(sample(unreal.GameplayStatics.get_time_seconds(game)))
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
