"""Native sensor boundaries/cadence in an unsaved Entry fixture. Never saves.

The production rig, actuators, clock and perception/query entry points are used.
Obstacle moves below are explicit visibility stimuli, not transport successes.
No AGV body pose/velocity, AI state or completion record is changed during play.
Transport, contact, layout and elevator regressions run separately.
"""
import json
import math
from pathlib import Path
import statistics
import time
import traceback
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
saved_vehicles = {a.get_actor_label(): a for a in actors.get_all_level_actors() if isinstance(a, unreal.WarehouseForklift)}
assert len(saved_vehicles) == 2, ('Unexpected operating fleet size', list(saved_vehicles))
assert not any(isinstance(a, unreal.AgvTestVehicle) for a in actors.get_all_level_actors()), 'Standalone prototype still runs in the operating map'
for label in ('WH_AutonomousForklift', 'WH_AutonomousForklift_02'):
    saved_vehicle = saved_vehicles[label]
    assert saved_vehicle.get_editor_property('sensor_mode') == unreal.WarehouseSensorMode.AUTONOMOUS, ('Saved production vehicle did not enable the integrated sensors', label)
    assert abs(saved_vehicle.get_editor_property('empty_travel_speed_cm') - 180) < .01, ('Saved production maximum speed changed', label)
print('SENSOR_SAVED_VEHICLE_DEFAULTS_PASSED', 'Both saved warehouse rigs use Autonomous sensors and 180cm/s; independent from fixture overrides')
source = saved_vehicles['WH_AutonomousForklift']
poses = {p.get_name(): unreal.MathLibrary.make_relative_transform(p.get_world_transform(), source.get_actor_transform())
         for p in source.get_components_by_class(unreal.SceneComponent) if p != source.root_component}
assets = {p.get_name(): (p.static_mesh, list(p.get_editor_property('override_materials')))
          for p in source.get_components_by_class(unreal.StaticMeshComponent) if p.static_mesh}
drive = {name: source.get_editor_property(name) for name in ('empty_travel_speed_cm', 'loaded_travel_speed_cm',
    'vehicle_mass_kg', 'drive_acceleration_cm', 'drive_torque_nm', 'brake_torque_nm', 'drive_power_watts', 'max_fork_height_cm')}
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
unreal.GameplayStatics.get_all_actors_of_class(editor.get_editor_world(), unreal.WorldSettings)[0].set_editor_property(
    'default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
cube = unreal.load_asset('/Engine/BasicShapes/Cube')


def spawn(cls, label, point):
    a = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
    a.set_actor_label(label)
    a.set_editor_property('is_spatially_loaded', False)
    return a


def block(label, point, size, tags=(), movable=False, physical=True):
    a = spawn(unreal.StaticMeshActor, label, point)
    a.static_mesh_component.set_static_mesh(cube)
    a.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE if movable else unreal.ComponentMobility.STATIC)
    a.static_mesh_component.set_collision_profile_name('BlockAll')
    if not physical:
        a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    a.set_actor_scale3d(unreal.Vector(*(n / 100 for n in size)))
    a.tags = list(tags)
    return a


def rig(label, point):
    a = spawn(unreal.WarehouseForklift, label, point)
    for p in a.get_components_by_class(unreal.StaticMeshComponent):
        if p.get_name() in assets:
            mesh, materials = assets[p.get_name()]
            p.set_static_mesh(mesh)
            p.set_editor_property('override_materials', materials)
    def depth(p):
        n = 0
        while p.get_attach_parent():
            n += 1
            p = p.get_attach_parent()
        return n
    for p in sorted(a.get_components_by_class(unreal.SceneComponent), key=depth):
        if p.get_name() in poses:
            p.set_world_transform(unreal.MathLibrary.compose_transforms(poses[p.get_name()], a.get_actor_transform()), False, True)
    for name, value in drive.items():
        a.set_editor_property(name, value)
    a.set_editor_property('auto_start', False)
    a.set_editor_property('idle_parking_enabled', False)
    a.set_editor_property('sensor_mode', unreal.WarehouseSensorMode.AUTONOMOUS)
    return a


block('WH_SensorBuildingFloor', (0, 1200, -10), (6000, 6400, 20), ('WarehouseMapStructure', 'LayoutFixed'))
block('WH_SensorUpperFloor', (0, 3200, 390), (2400, 1200, 20), ('WarehouseMapStructure', 'LayoutFixed'))
block('WH_SensorBuildingWall', (600, -735, 150), (240, 10, 300), ('WarehouseMapStructure', 'LayoutFixed'))
# Full-height opaque occluder hides even the upper LiDAR from this unknown rack.
block('WH_SensorOccluderWall', (310, 0, 250), (30, 280, 500), ('WarehouseMapStructure', 'LayoutFixed'), movable=True)
block('SensorUnmappedStaticRack', (650, 0, 100), (110, 90, 200), ('LayoutType:Rack', 'LayoutGroup:SensorRack'))
block('SensorPhysicalDecoration', (-650, 500, 100), (30, 30, 200), ('LayoutType:Other',), movable=True)
block('SensorPaint', (550, -650, 2), (120, 120, 4), ('TransportRouteVisual',), physical=False)
trigger = block('SensorTrigger', (750, -650, 100), (80, 80, 200))
trigger.static_mesh_component.set_collision_profile_name('Trigger')
person1 = block('SensorPersonBesideWall', (600, -700, 85), (30, 30, 170), ('WarehousePhysicalHuman',), movable=True)
person2 = block('SensorPersonBesideTarget', (700, 930, 85), (30, 30, 170), ('WarehousePhysicalHuman',), movable=True)
for person in (person1, person2):
    person.static_mesh_component.set_collision_object_type(unreal.CollisionChannel.ECC_PAWN)
pallet = spawn(unreal.WarehousePallet, 'SensorTargetPallet', (700, 850, .1))
spawn(unreal.WarehouseWaitingBay, 'SensorSelectionOnlyBay', (550, -900, 0))
vehicles = [rig('SensorVehicleA', (0, 0, .1)), rig('SensorVehicleB', (0, 1600, .1)),
            rig('SensorVehicleUpper', (0, 3200, 400.1)), rig('SensorVehicleD', (-2200, -1200, .1))]
vehicles[0].set_editor_property('target_pallet', pallet)
spawn(unreal.WarehousePallet, 'SensorDrivePallet', (1400, -1200, .1))
native_obstacle = block('SensorNativeDriveObstacle', (-400, -1200, 100), (50, 180, 200), movable=True)
native_obstacle.set_actor_enable_collision(False)
spawn(unreal.PlayerStart, 'SensorPlayerStart', (-2000, -1600, 125))
spawn(unreal.TargetPoint, 'SensorRemoteOperator', (0, -220, 75))
spawn(unreal.WarehouseLayoutEditor, 'SensorLayoutEditor', (0, -1800, 0))

out = Path(unreal.Paths.project_saved_dir()) / 'Testing/ForkliftSensors'
out.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
phase = 'start'
at = 0.
case_wall = 0.
live = None
pc = layout = None
index = 0
cases = [(count, speed) for count in (1, 2, 4) for speed in (1, 2, 4)]
checks, measurements = [], []
last_counts, scan_stamps, scan_costs, beams, tick_durations = {}, {}, {}, {}, []
lidar_costs, non_lidar_costs = {}, {}
reference_counts = {}
wait_count = 0
saved_revision = 0
remote_count = remote_beams = remote_obstacle_count = 0
remote_scans = None
added_vehicle = None
drive_samples = []
drive_started = drive_triggered = None
stop_since = None


def prop(obj, name):
    return obj.get_editor_property(name)


def kit(v):
    result = v.get_component_by_class(unreal.AgvShadowSensorsComponent)
    assert result, ('Missing production sensor kit', v.get_actor_label())
    return result


def records(v):
    return kit(v).get_observed_obstacles()


def labels(v):
    return {o.actor.get_actor_label() for o in records(v) if o.actor}


def vec(v):
    return [v.x, v.y, v.z]


def physical_front(v):
    bounds = [unreal.SystemLibrary.get_component_bounds(p)
              for p in v.get_components_by_class(unreal.StaticMeshComponent)
              if unreal.WarehouseDamageSystem.is_physical_contact(p)]
    return max(centre.x + extent.x for centre, extent, radius in bounds)


def check(name, **details):
    checks.append(dict(name=name, **details))
    print('FORKLIFT_SENSOR_CHECK', name, json.dumps(details, ensure_ascii=False))


def finish(ok, detail):
    result = dict(passed=ok, detail=detail, phase=phase, checks=checks, measurements=measurements,
                  actual_drive_samples=drive_samples,
                  script_revision='sensor-boundaries-4-operating-fleet-isolation',
                  wall_seconds=round(time.monotonic() - started, 2),
                  scope='Unsaved native production rigs and clock, sensor snapshots and normal spatial queries; separate regression logs cover actual transport/contact/layout/elevator. No OS input claim.')
    (out / 'Report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print('FORKLIFT_SENSORS_PIE_PASSED' if ok else 'FORKLIFT_SENSORS_PIE_FAILED', detail)
    unreal.unregister_slate_post_tick_callback(handle)
    if layout and prop(layout, 'editing'):
        layout.set_editing(False, 0)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def powered(v, enabled):
    if bool(prop(v, 'powered')) != enabled:
        v.toggle_power()


def query(v, x, y, z=0):
    return v.navigation_clear_for_diagnostics(unreal.Transform(location=unreal.Vector(x, y, z)))


def begin_case(now):
    global at, case_wall, reference_counts, last_counts, scan_stamps, scan_costs, beams, tick_durations, phase
    global lidar_costs, non_lidar_costs
    count, speed = cases[index]
    assert pc.set_simulation_speed(speed)
    for i, v in enumerate(vehicles):
        powered(v, i < count)
    reference_counts = {v: prop(kit(v), 'snapshot_count') for v in vehicles}
    last_counts = dict(reference_counts)
    scan_stamps = {v: [] for v in vehicles}
    scan_costs = {v: [] for v in vehicles}
    lidar_costs = {v: [] for v in vehicles}
    non_lidar_costs = {v: [] for v in vehicles}
    beams = {v: [] for v in vehicles}
    tick_durations = []
    at, phase = now, 'cadence'
    case_wall = time.monotonic()
    print('FORKLIFT_SENSOR_CASE', count, speed, now)


def tick(dt):
    global phase, at, live, vehicles, pc, layout, index, wait_count, saved_revision
    global remote_count, remote_beams, remote_obstacle_count, remote_scans, added_vehicle
    global drive_started, drive_triggered, stop_since
    try:
        assert time.monotonic() - started < 300, ('Sensor test timeout', phase, index)
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if phase == 'settle':
            if now < 2:
                return
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            vehicles = [live[n] for n in ('SensorVehicleA', 'SensorVehicleB', 'SensorVehicleUpper', 'SensorVehicleD')]
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            layout = live['SensorLayoutEditor']
            for v in vehicles:
                assert v.root_component.is_simulating_physics()
                assert abs(v.get_physical_mass_kg() - 1000) < .1
                assert abs(prop(v, 'empty_travel_speed_cm') - 180) < .01
                sensor_kit = v.get_component_by_class(unreal.AgvShadowSensorsComponent)
                assert not sensor_kit or prop(sensor_kit, 'snapshot_count') == 0, 'Powered-off rig scanned before activation'
                powered(v, True)
            phase = 'initialize'
            return
        if phase == 'initialize':
            kits = [v.get_component_by_class(unreal.AgvShadowSensorsComponent) for v in vehicles]
            if not all(k and prop(k, 'snapshot_count') >= 1 for k in kits):
                assert now < 8, 'First powered observation never became ready'
                return
            for k in kits:
                lidars = list(prop(k, 'lidars'))
                assert len(lidars) == 5 and len(set(lidars)) == 5
                assert not k.is_component_tick_enabled(), 'Independent sensor tick duplicates the one-second refresh'
            check('four_native_rigs_have_five_independent_sensors_and_no_offline_scan')
            begin_case(now)
            return
        for v in vehicles:
            assert v.root_component.is_simulating_physics(), v.get_actor_label()
            assert abs(v.get_physical_mass_kg() - 1000) < .1
        a = vehicles[0]
        if phase == 'cadence':
            count, speed = cases[index]
            tick_durations.append(float(dt))
            for i, v in enumerate(vehicles):
                k = kit(v)
                current = prop(k, 'snapshot_count')
                if current != last_counts[v]:
                    assert i < count, ('Powered-off rig scanned', v.get_actor_label())
                    assert current == last_counts[v] + 1, ('More than one scan per refresh', current, last_counts[v])
                    stamp = prop(k, 'last_snapshot_seconds')
                    if scan_stamps[v]:
                        assert stamp - scan_stamps[v][-1] >= .999, ('Faster than one simulated second', scan_stamps[v], stamp)
                    scan_stamps[v].append(stamp)
                    scan_costs[v].append(prop(k, 'last_sensor_milliseconds'))
                    lidar_ms = {sensor.get_name(): prop(sensor, 'last_step_milliseconds') for sensor in prop(k, 'lidars')}
                    lidar_costs[v].append(lidar_ms)
                    non_lidar_costs[v].append(prop(k, 'last_sensor_milliseconds') - sum(lidar_ms.values()))
                    beams[v].append(prop(k, 'last_beam_count'))
                    last_counts[v] = current
                    assert prop(k, 'snapshot_valid'), prop(k, 'sensor_status')
                    assert all(s.get_revolution_count() == current for s in prop(k, 'lidars')), 'A scanner ran outside the common refresh'
            if now - at < 5:
                return
            active = vehicles[:count]
            assert all(len(scan_stamps[v]) >= 4 for v in active), ('Missing active scans', {v.get_actor_label(): t for v, t in scan_stamps.items()})
            assert all(prop(kit(v), 'snapshot_count') == reference_counts[v] for v in vehicles[count:])
            costs = [n for v in active for n in scan_costs[v]]
            lidar_samples = [sample for v in active for sample in lidar_costs[v]]
            lidar_sums = [sum(sample.values()) for sample in lidar_samples]
            processing = [n for v in active for n in non_lidar_costs[v]]
            row = dict(vehicle_count=count, speed=speed, simulated_seconds=now-at,
                       wall_seconds=time.monotonic()-case_wall,
                       achieved_speed=(now-at)/(time.monotonic()-case_wall),
                       sensor_ms_median=statistics.median(costs), sensor_ms_p95=sorted(costs)[math.ceil(len(costs)*.95)-1],
                       lidar_ms_samples=lidar_samples,
                       lidar_ms_median={name: statistics.median(sample[name] for sample in lidar_samples) for name in lidar_samples[0]},
                       lidar_sum_ms_samples=lidar_sums, lidar_sum_ms_median=statistics.median(lidar_sums),
                       lidar_sum_ms_p95=sorted(lidar_sums)[math.ceil(len(lidar_sums)*.95)-1],
                       non_lidar_processing_ms_samples=processing, non_lidar_processing_ms_median=statistics.median(processing),
                       non_lidar_processing_ms_p95=sorted(processing)[math.ceil(len(processing)*.95)-1],
                       processing_scope='kit total minus five LiDAR steps; includes map rebuild and measured-point snapshot processing',
                       beams_per_refresh=statistics.median([n for v in active for n in beams[v]]),
                       scan_times={v.get_actor_label(): scan_stamps[v] for v in active},
                       slate_callback_ms_median=statistics.median(tick_durations)*1000)
            measurements.append(row)
            check('one_second_cadence', vehicle_count=count, speed=speed, samples=len(costs))
            index += 1
            if index < len(cases):
                begin_case(now)
                return
            for i, v in enumerate(vehicles):
                powered(v, i == 0)
            assert pc.set_simulation_speed(1)
            at, phase = now, 'occluded'
            wait_count = prop(kit(a), 'snapshot_count')
            return
        if phase == 'occluded' and prop(kit(a), 'snapshot_count') > wait_count:
            seen = labels(a)
            assert 'SensorUnmappedStaticRack' not in seen, ('Occluded rack was observed', seen)
            assert query(a, 650, 0), 'Static rack leaked into the known building map before observation'
            assert not query(a, 600, -735), 'Known building wall missing from spatial queries'
            assert {'SensorPersonBesideWall', 'SensorPersonBesideTarget', 'SensorPhysicalDecoration'} <= seen, ('Physical person/decoration missing', seen)
            assert not {'SensorPaint', 'SensorTrigger', 'SensorSelectionOnlyBay'} & seen, ('Nonphysical display entered perception', seen)
            for o in records(a):
                points = list(o.world_points)
                assert points, ('Observation has no measured returns', o.actor)
                for key in ('x', 'y', 'z'):
                    low, high = min(getattr(p, key) for p in points), max(getattr(p, key) for p in points)
                    assert abs(getattr(o.world_bounds.min, key) - low + 2) < .05
                    assert abs(getattr(o.world_bounds.max, key) - high - 2) < .05
            assert abs(prop(kit(vehicles[2]), 'snapshot_floor_z_cm') - 400) < 1, 'Upper rig used a ground-floor snapshot'
            check('building_only_map_hidden_static_rack_people_displays_and_measured_point_bounds', observed=sorted(seen))
            live['WH_SensorOccluderWall'].set_actor_enable_collision(False)
            wait_count = prop(kit(a), 'snapshot_count')
            phase = 'visible'
            return
        if phase == 'visible' and prop(kit(a), 'snapshot_count') > wait_count:
            assert 'SensorUnmappedStaticRack' in labels(a), 'Uncovered rack was not detected by physical rays'
            assert not query(a, 650, 0), 'Measured rack points did not affect normal autonomous spatial queries'
            check('observed_rack_updates_autonomous_spatial_query')
            live['WH_SensorOccluderWall'].set_actor_enable_collision(True)
            # Move only the obstacle behind its opaque screen: no vehicle or AI state is edited.
            rack = live['SensorUnmappedStaticRack']
            rack.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
            rack.set_actor_location(unreal.Vector(650, 180, 100), False, True)
            wait_count = prop(kit(a), 'snapshot_count')
            at, phase = now, 'reoccluded'
            return
        if phase == 'reoccluded' and prop(kit(a), 'snapshot_count') >= wait_count + 2:
            assert 'SensorUnmappedStaticRack' not in labels(a), 'Hidden rack was tracked using its live transform'
            assert query(a, 650, 180), 'AI predicted a hidden rack at its new true location'
            check('reocclusion_does_not_follow_live_actor_geometry')
            remote_count = prop(kit(a), 'snapshot_count')
            remote_beams = prop(kit(a), 'last_beam_count')
            remote_scans = [s.get_revolution_count() for s in prop(kit(a), 'lidars')]
            remote_obstacle_count = prop(a, 'obstacle_check_count')
            assert a.begin_remote_control(live['SensorRemoteOperator']), prop(a, 'status')
            assert not prop(kit(a), 'snapshot_valid'), 'Remote entry retained a usable autonomous snapshot'
            a.set_editor_property('safety_scanner_healthy', False)
            a.set_remote_input(0, 0, 0)
            at, phase = now, 'remote'
            return
        if phase == 'remote':
            a.set_remote_input(0, 0, 0)
            assert prop(kit(a), 'snapshot_count') == remote_count
            assert [s.get_revolution_count() for s in prop(kit(a), 'lidars')] == remote_scans
            assert prop(a, 'obstacle_check_count') == remote_obstacle_count
            assert not a.check_systems(False), 'Sensor fault leaked into manual systems check'
            assert query(a, 310, 0), 'Remote diagnostic still applies autonomous geometry'
            if now - at < 3:
                return
            a.set_editor_property('safety_scanner_healthy', True)
            a.end_remote_control(live['SensorRemoteOperator'])
            assert not prop(kit(a), 'snapshot_valid'), 'Remote exit reused an old clear snapshot'
            powered(a, True)
            wait_count = prop(kit(a), 'snapshot_count')
            at, phase = now, 'reacquire'
            check('remote_has_zero_sensor_and_ai_queries_even_with_sensor_fault')
            return
        if phase == 'reacquire' and prop(kit(a), 'snapshot_count') > wait_count:
            assert prop(kit(a), 'snapshot_valid') and kit(a).is_snapshot_fresh(now)
            saved_revision = prop(kit(a), 'map_revision')
            layout.set_editing(True, 0)
            layout.set_category(unreal.WarehouseLayoutCategory.RACK)
            layout.set_editor_property('edge_snap_enabled', False)
            rack = live['SensorUnmappedStaticRack']
            point = rack.get_actor_location()
            assert layout.begin_drag(rack, unreal.Vector(point.x, point.y, 0)), prop(layout, 'status')
            layout.update_drag(unreal.Vector(point.x + 400, point.y, 0))
            assert layout.end_drag(), prop(layout, 'status')
            assert not prop(kit(a), 'snapshot_valid'), 'Layout completion retained old observed geometry'
            layout.set_editing(False, 0)
            wait_count = prop(kit(a), 'snapshot_count')
            at, phase = now, 'layout_reacquire'
            check('native_rack_layout_transaction_invalidates_snapshot_and_map_generation')
            return
        if phase == 'layout_reacquire' and prop(kit(a), 'snapshot_count') > wait_count:
            assert prop(kit(a), 'snapshot_valid') and kit(a).is_snapshot_fresh(now)
            assert prop(kit(a), 'map_revision') > saved_revision, 'Layout did not rebuild the invalidated map generation'
            a.set_editor_property('sensor_mode', unreal.WarehouseSensorMode.DISABLED)
            wait_count = prop(kit(a), 'snapshot_count')
            at, phase = now, 'disabled'
            return
        if phase == 'disabled':
            assert prop(kit(a), 'snapshot_count') == wait_count
            assert not prop(kit(a), 'snapshot_valid')
            if now - at < 2:
                return
            a.set_editor_property('sensor_mode', unreal.WarehouseSensorMode.OBSERVE_ONLY)
            at, phase = now, 'observe'
            return
        if phase == 'observe' and prop(kit(a), 'snapshot_count') > wait_count:
            assert prop(kit(a), 'snapshot_valid')
            check('disabled_mode_stops_all_scanning_observation_mode_reacquires')
            a.set_editor_property('sensor_mode', unreal.WarehouseSensorMode.AUTONOMOUS)
            a.set_editor_property('safety_scanner_healthy', False)
            wait_count = prop(kit(a), 'snapshot_count')
            at, phase = now, 'sensor_fault'
            return
        if phase == 'sensor_fault':
            if now - at < 2:
                return
            assert prop(kit(a), 'snapshot_count') == wait_count
            assert not prop(kit(a), 'snapshot_valid') and a.check_systems()
            a.set_editor_property('safety_scanner_healthy', True)
            powered(a, True)
            at, phase = now, 'fault_reacquire'
            return
        if phase == 'fault_reacquire' and prop(kit(a), 'snapshot_count') > wait_count:
            assert prop(kit(a), 'snapshot_valid') and not a.check_systems()
            check('sensor_fault_blocks_autonomy_until_a_fresh_recovery_scan')
            layout.set_editing(True, 0)
            layout.set_category(unreal.WarehouseLayoutCategory.FORKLIFT)
            layout.set_action(unreal.WarehouseLayoutAction.ADD)
            before = set(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseForklift))
            point = unreal.Vector(-1200, -800, 0)
            assert layout.begin_add(point), prop(layout, 'status')
            layout.update_drag(point)
            assert layout.end_drag(), prop(layout, 'status')
            added_vehicle = next(v for v in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseForklift) if v not in before)
            added_vehicle.set_editor_property('auto_start', False)
            added_vehicle.set_editor_property('idle_parking_enabled', False)
            added_vehicle.set_editor_property('sensor_mode', unreal.WarehouseSensorMode.AUTONOMOUS)
            layout.set_editing(False, 0)
            powered(added_vehicle, True)
            at, phase = now, 'added_sensor_rig'
            return
        if phase == 'added_sensor_rig':
            k = added_vehicle.get_component_by_class(unreal.AgvShadowSensorsComponent)
            if not k or not prop(k, 'snapshot_valid'):
                assert now - at < 8, ('Added vehicle never acquired sensors', prop(added_vehicle, 'status'))
                return
            assert len(prop(k, 'lidars')) == 5 and k != kit(a)
            assert abs(added_vehicle.get_physical_mass_kg() - 1000) < .1
            assert added_vehicle.root_component.is_simulating_physics()
            check('native_layout_add_creates_an_independent_ready_sensor_rig')
            powered(added_vehicle, False)
            layout.set_editing(True, 0)
            layout.set_category(unreal.WarehouseLayoutCategory.FORKLIFT)
            layout.set_action(unreal.WarehouseLayoutAction.DELETE)
            layout.set_selection([added_vehicle])
            assert layout.delete_selection(), prop(layout, 'status')
            assert len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseForklift)) == 4
            layout.set_editing(False, 0)
            wait_count = prop(kit(a), 'snapshot_count')
            at, phase = now, 'deleted_sensor_rig'
            return
        if phase == 'deleted_sensor_rig' and prop(kit(a), 'snapshot_count') > wait_count:
            assert prop(kit(a), 'snapshot_valid') and kit(a).is_snapshot_fresh(now)
            check('deleting_a_vehicle_keeps_the_remaining_independent_sensor_rigs_live')
            driver = vehicles[3]
            job = unreal.WarehouseWorkOrder(job_id='SENSOR-REAL-BRAKE-RESUME', source_system='FMS-SENSOR-TEST',
                pallet=live['SensorDrivePallet'], destination=unreal.Transform(location=unreal.Vector(2100, -1200, 0)))
            assert driver.submit_job(job), prop(driver, 'status')
            powered(driver, True)
            drive_started, at, phase = now, now, 'native_drive'
            return
        if phase in ('native_drive', 'native_brake', 'native_resume'):
            driver = vehicles[3]
            assert driver.get_editor_property('ai_state') != unreal.WarehouseAIState.FAULT, prop(driver, 'status')
            assert now - drive_started < 90, ('Native sensor stop/resume timed out', phase, prop(driver, 'status'), driver.get_actor_location())
            position = driver.get_actor_location()
            speed = prop(driver, 'current_speed_cm')
            obstacle = live['SensorNativeDriveObstacle']
            front = physical_front(driver)
            obstacle_front = obstacle.get_actor_location().x - 25
            if not drive_samples or now - drive_samples[-1]['time'] >= .1:
                drive_samples.append(dict(time=now, phase=phase, position=vec(position), speed_cm=speed,
                                          gap_cm=obstacle_front-front, status=prop(driver, 'status'),
                                          ai_state=str(prop(driver, 'ai_state')),
                                          snapshot_count=prop(kit(driver), 'snapshot_count'),
                                          snapshot_time=prop(kit(driver), 'last_snapshot_seconds'),
                                          snapshot_fresh=kit(driver).is_snapshot_fresh(now),
                                          sensor_status=prop(kit(driver), 'sensor_status')))
            if phase == 'native_drive':
                if position.x < -800:
                    return
                assert speed > 145, ('Native cruise did not accelerate before the obstacle', speed, position, prop(driver, 'status'))
                obstacle.set_actor_enable_collision(True)
                drive_triggered = dict(time=now, speed_cm=speed, actor_distance_cm=obstacle.get_actor_location().x-position.x,
                                       gap_cm=obstacle_front-front, snapshot_count=prop(kit(driver), 'snapshot_count'))
                at, phase = now, 'native_brake'
                return
            if phase == 'native_brake':
                assert obstacle_front - front > 2, ('Sensor brake reached physical obstacle contact', position, front, prop(driver, 'status'))
                if abs(speed) < 5 and 'SensorNativeDriveObstacle' in labels(driver):
                    if stop_since is None:
                        stop_since = now
                    if now - stop_since >= .2:
                        check('native_observed_obstacle_brakes_before_contact', trigger=drive_triggered,
                              stopping_actor_position=vec(position), remaining_gap_cm=obstacle_front-front,
                              response_sim_seconds=now-drive_triggered['time'])
                        obstacle.set_actor_enable_collision(False)
                        at, phase = now, 'native_resume'
                else:
                    stop_since = None
                return
            if position.x > obstacle.get_actor_location().x + 250 and speed > 20:
                check('native_autonomy_replans_or_resumes_after_obstacle_removal', position=vec(position), speed_cm=speed)
                finish(True, 'Five sensors; 1/2/4 vehicles x 1/2/4 speed cadence; building-only known map; occluded rack and live-truth isolation; people/decoration/display roles; remote zero; native layout/fault/add/delete readiness; actual sensor braking before contact and native resumption passed')
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
