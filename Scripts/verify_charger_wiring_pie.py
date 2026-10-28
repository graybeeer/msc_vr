"""Unsaved PIE: charger supply cabling, real layout transactions and shell rebuild.

Uses the production charger and owned distribution-cabinet meshes. Calls the
same native edit APIs as the UI, not OS input. Never saves a map or asset.
The root runner owns the editor process.
"""
import hashlib
import json
import math
from pathlib import Path
import time
import traceback
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
SAVED_MAP = bool(globals().get('CHARGER_WIRING_SAVED_MAP', False)) or '-chargerwiringsavedmap' in unreal.SystemLibrary.get_command_line().lower()
OUT = ROOT/('Saved/Testing/ChargerWiringSavedMap' if SAVED_MAP else 'Saved/Testing/ChargerWiring')
OUT.mkdir(parents=True, exist_ok=True)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
originals = [ROOT/'Content/FirstPerson/Lvl_FirstPerson.umap',
             ROOT/'Source/msc_vr/WarehouseForkliftPhysics.cpp',
             ROOT/'Source/msc_vr/WarehouseForkliftAI.cpp']
for folder in ('__ExternalActors__', '__ExternalObjects__'):
    originals.extend((ROOT/'Content'/folder/'FirstPerson/Lvl_FirstPerson').rglob('*.uasset'))
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in originals}
report = dict(passed=False, checks=[], screenshots=[], os_input_tested=False,
              scope='Unsaved Entry; production charger/models, physical supply conduit, native layout move/rotate/add/delete/cancel and building resize',
              limits=['No electrical circuit/load simulation', 'No full warehouse throughput benchmark'])
if SAVED_MAP:
    report['scope'] = 'Actual default map PIE; existing chargers/cabinets and production service obstacles; no map save or transport-order test'
start = time.monotonic()
phase = 'setup'
busy = finished = cleaning = False
error = None
game = pc = station = layout = wiring = None
named = {}
original_station_bounds = None
temporary_editor_flags = []
temporary_editor_camera = None


def check(name, **data):
    report['checks'].append(dict(name=name, **data))
    print('CHARGER_WIRING_CHECK', name, json.dumps(data, ensure_ascii=False))


def frames(count=2):
    for _ in range(count):
        yield


def allof(cls):
    return unreal.GameplayStatics.get_all_actors_of_class(game, cls)


def vec(value):
    return [round(float(getattr(value, a)), 4) for a in ('x', 'y', 'z')]


def distance(a, b):
    return math.sqrt(sum((getattr(a, k)-getattr(b, k))**2 for k in ('x', 'y', 'z')))


def bounds(actor):
    box = unreal.WarehouseDamageSystem.get_physical_bounds(actor)
    return vec(box.min), vec(box.max)


def route(actor):
    return list(actor.get_editor_property('supply_cable_route'))


def cable_actors():
    return [a for a in allof(unreal.Actor) if a.actor_has_tag('ChargerSupplyCable')]


def refresh():
    unreal.WarehouseChargerWiring.refresh_connections(station)


def connection(actor, expected=None):
    target = actor.get_editor_property('supply_cabinet')
    points = route(actor)
    status = actor.get_editor_property('supply_cable_status')
    assert target and len(points) >= 2, (actor.get_actor_label(), status, points)
    if expected is not None:
        assert target == expected, ('Wrong nearest panel', target, expected, status)
    assert distance(points[0], actor.get_actor_location()) < 220, ('Cable inlet is not beside charger', points[0])
    assert distance(points[-1], target.get_actor_location()) < 140, ('Cable end misses panel', points[-1])
    horizontal = [(a, b) for a, b in zip(points, points[1:])
                  if math.hypot(b.x-a.x, b.y-a.y) > 20]
    assert horizontal, ('No supply route', points)
    floor_z = math.floor((actor.get_actor_location().z+1)/400)*400
    assert all(min(a.z, b.z) >= floor_z+275 for a, b in horizontal), ('Cable crosses driving height', [vec(p) for p in points])
    return target, points


def physical_cables():
    cables = cable_actors()
    assert cables, 'No generated supply geometry'
    count = 0
    for actor in cables:
        assert actor.actor_has_tag('LayoutFixed') and actor.actor_has_tag('WarehouseMapStructure')
        assert not actor.is_actor_tick_enabled(), 'Cable must not tick'
        parts = actor.get_components_by_class(unreal.PrimitiveComponent)
        for part in parts:
            if part.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
                continue
            assert unreal.WarehouseDamageSystem.is_physical_contact(part), ('Cable does not share physical policy', part)
            assert not part.is_simulating_physics(), 'Supply conduit must be fixed'
            count += 1
    assert count, 'Supply cables have no physical geometry'
    return len(cables), count


def setup():
    global original_station_bounds
    assert not editor.get_game_world(), 'Stop the previous PIE first'
    assert level.load_level('/Engine/Maps/Entry')
    for actor in list(actors.get_all_level_actors()):
        if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
            actors.destroy_actor(actor)
    world = editor.get_editor_world()
    settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
    settings.set_editor_property('default_game_mode', unreal.load_class(None,
        '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    panel = unreal.load_asset('/Game/Warehouse/Services/Meshes/SM_WS_DistributionCabinet')
    door = unreal.load_asset('/Game/Warehouse/Services/Meshes/SM_WS_DistributionDoor')
    wall_material = unreal.load_asset('/Game/Warehouse/AGV/Materials/M_Machined_steel')
    assert cube and panel and door

    def spawn(cls, label, point):
        a = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
        assert a, label
        a.set_actor_label(label)
        return a

    def solid(label, point, size, extra=()):
        a = spawn(unreal.StaticMeshActor, label, point)
        a.static_mesh_component.set_static_mesh(cube)
        a.static_mesh_component.set_collision_profile_name('BlockAll')
        a.set_actor_scale3d(unreal.Vector(*(n/100 for n in size)))
        if wall_material:
            a.static_mesh_component.set_material(0, wall_material)
        a.tags = ['LayoutFixed', 'WarehouseMapStructure']+list(extra)
        return a

    solid('Floor', (0, 0, -20), (4800, 4000, 40), ['ElevatorLayoutFloor', 'WarehouseFloor0'])
    solid('WH_Roof', (0, 0, 1215), (4880, 4080, 30), ['ObserverRoof'])
    solid('WH_Wall_N', (0, 2020, 600), (4880, 40, 1200), ['ObserverFadeWall'])
    solid('WH_Wall_E', (2420, 0, 600), (40, 4000, 1200), ['ObserverFadeWall'])
    solid('WH_Wall_W', (-2420, 0, 600), (40, 4000, 1200), ['ObserverFadeWall'])
    # The open south side keeps the camera/entrance clear. Source templates
    # are intentionally outside the fixture and hidden by building regeneration.
    solid('WH_Column_01', (12000, 0, 600), (40, 40, 1200))
    solid('WH_RoofBeam_01', (12000, 500, 1180), (400, 20, 40))
    solid('MZ_L2_InnerLeft_Bar55', (12000, 1000, 500), (160, 5, 5))
    obstruction = solid('WallHeightObstruction', (0, 1998, 330), (4800, 16, 16))
    obstruction.set_actor_enable_collision(False)
    b = panel.get_bounds()
    panel_y = 2000-(b.origin.y+b.box_extent.y)+.1
    db = door.get_bounds()
    for label, x in (('Left', -1200), ('Right', 1200)):
        a = spawn(unreal.StaticMeshActor, 'WS_DistributionCabinet_'+label, (x, panel_y, 145))
        a.static_mesh_component.set_static_mesh(panel)
        a.static_mesh_component.set_collision_profile_name('BlockAll')
        a.tags = ['WarehouseServices', 'WarehouseMapStructure', 'LayoutFixed', 'WarehouseFloor0']
        front = panel_y+b.origin.y-b.box_extent.y
        d = spawn(unreal.StaticMeshActor, 'WS_DistributionDoor_'+label,
                  (x-28, front-db.origin.y-db.box_extent.y+.1, 145))
        d.static_mesh_component.set_static_mesh(door)
        d.static_mesh_component.set_collision_profile_name('BlockAll')
        d.tags = list(a.tags)
    charger = spawn(unreal.WarehouseChargingStation, 'WiringCharger', (-600, 1500, 0))
    charger.set_actor_rotation(unreal.Rotator(yaw=-90), False)
    original_station_bounds = bounds(charger)
    spawn(unreal.PlayerStart, 'WiringPlayerStart', (0, -1700, 110))
    spawn(unreal.WarehouseSimulationClock, 'WiringClock', (0, 0, 0))
    view = spawn(unreal.CameraActor, 'WiringCamera', (100, 650, 320))
    view.get_component_by_class(unreal.CameraComponent).set_field_of_view(65)
    sun = spawn(unreal.DirectionalLight, 'WiringSun', (0, 0, 1500))
    sun.set_actor_rotation(unreal.Rotator(pitch=-55, yaw=30), False)
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(4)
    fill = spawn(unreal.PointLight, 'WiringFill', (0, 1500, 550))
    fill.get_component_by_class(unreal.PointLightComponent).set_intensity(60000)
    fill.get_component_by_class(unreal.PointLightComponent).set_attenuation_radius(5000)
    spawn(unreal.SkyAtmosphere, 'WiringSky', (0, 0, 0))
    level.editor_request_begin_play()


def scenario_body():
    global phase, game, pc, named, station, layout, wiring
    setup()
    while not editor.get_game_world():
        yield
    game = editor.get_game_world()
    yield from frames(20)
    named = {a.get_actor_label(): a for a in allof(unreal.Actor)}
    station = named['WiringCharger']
    left, right = (named['WS_DistributionCabinet_'+side] for side in ('Left', 'Right'))
    pc = unreal.GameplayStatics.get_player_controller(game, 0)
    matches = [o for o in unreal.ObjectIterator(unreal.WarehouseChargerWiring) if o.get_outer() == game]
    assert len(matches) == 1, matches
    wiring = matches[0]
    phase = 'initial_connection'
    refresh()
    _, points = connection(station, left)
    assert bounds(station) == original_station_bounds, 'Supply route enlarged charger edit/physics bounds'
    wall_runs = [(a, b) for a, b in zip(points, points[1:])
                 if abs(a.y-2000) < 35 and abs(b.y-2000) < 35 and abs(a.x-b.x) > 60]
    assert wall_runs, ('Cable did not use the nearby north wall', [vec(p) for p in points])
    cables, parts = physical_cables()
    check('nearest_panel_high_wall_route_and_physical_policy', panel=left.get_actor_label(),
          route=[vec(p) for p in points], cable_actors=cables, physical_parts=parts)
    revision = wiring.get_editor_property('rebuild_count')
    yield from frames(30)
    assert wiring.get_editor_property('rebuild_count') == revision, 'Idle frames rebuilt cable routes'
    check('idle_frames_do_not_regenerate_routes', frames=30, rebuild_count=revision)

    phase = 'wall_height_fallback'
    named['WallHeightObstruction'].set_actor_enable_collision(True)
    for side in ('E', 'W'):
        named['WH_Wall_'+side].set_actor_enable_collision(False)
    refresh()
    _, fallback = connection(station, left)
    assert any(abs(a.y-2000) < 35 and abs(b.y-2000) < 35 and abs(a.x-b.x) > 60
               and abs(a.z-330) > 15 for a, b in zip(fallback, fallback[1:])), [vec(p) for p in fallback]
    named['WallHeightObstruction'].set_actor_enable_collision(False)
    for side in ('E', 'W'):
        named['WH_Wall_'+side].set_actor_enable_collision(True)
    refresh()
    check('blocked_standard_height_tries_other_wall_heights_before_interior_span', route=[vec(p) for p in fallback])

    phase = 'layout_move_cancel_rotation'
    pc.set_observer_floor(0)
    pc.set_layout_editing(True)
    layout = allof(unreal.WarehouseLayoutEditor)[0]
    layout.set_category(unreal.WarehouseLayoutCategory.CHARGER)
    layout.set_editor_property('edge_snap_enabled', False)
    layout.set_selection([station])
    old = station.get_actor_location()
    assert layout.begin_drag(station, old), layout.get_editor_property('status')
    assert all(not a.get_actor_enable_collision() for a in cable_actors()), 'Old supply collision remains during drag'
    layout.update_drag(unreal.Vector(1000, 1500, 0))
    assert layout.end_drag(True), layout.get_editor_property('status')
    yield from frames(2)
    connection(station, right)
    moved = station.get_actor_location()
    before = [vec(p) for p in route(station)]
    assert layout.begin_drag(station, moved)
    layout.update_drag(unreal.Vector(-900, 1000, 0))
    assert not layout.end_drag(False)
    yield from frames(2)
    assert distance(moved, station.get_actor_location()) < .01
    connection(station, right)
    assert before == [vec(p) for p in route(station)], 'Cancel did not restore original wiring'
    layout.set_selection([station])
    assert layout.rotate_selection(90), layout.get_editor_property('status')
    yield from frames(2)
    connection(station, right)
    assert before[0] != vec(route(station)[0]), 'Rotated charger kept stale cable inlet'
    check('native_layout_move_cancel_and_rotation_refresh_connections')

    phase = 'add_delete_and_selection'
    for cable in cable_actors():
        layout.set_category(unreal.WarehouseLayoutCategory.ALL)
        layout.set_selection([cable])
        assert layout.get_editor_property('selection_count') == 0, 'Derived cable became independent edit object'
    layout.set_category(unreal.WarehouseLayoutCategory.CHARGER)
    layout.set_action(unreal.WarehouseLayoutAction.ADD)
    assert layout.begin_add(unreal.Vector(0, 900, 0)), layout.get_editor_property('status')
    assert layout.end_drag(True), layout.get_editor_property('status')
    yield from frames(2)
    chargers = allof(unreal.WarehouseChargingStation)
    assert len(chargers) == 2
    added = next(c for c in chargers if c != station)
    connection(added)
    count_added = len(cable_actors())
    layout.set_action(unreal.WarehouseLayoutAction.DELETE)
    layout.set_selection([added])
    assert layout.delete_selection(), layout.get_editor_property('status')
    yield from frames(2)
    assert len(allof(unreal.WarehouseChargingStation)) == 1
    connection(station)
    assert len(cable_actors()) < count_added, 'Deleting charger left orphaned supply actors'
    layout.set_action(unreal.WarehouseLayoutAction.MOVE)
    check('added_charger_is_wired_deletion_cleans_up_and_cables_are_not_edit_roots')

    phase = 'hidden_and_missing_panels'
    mains = station.get_editor_property('mains_power')
    station.set_actor_hidden_in_game(True)
    right.set_actor_hidden_in_game(True)
    refresh()
    connection(station, right)
    station.set_actor_hidden_in_game(False)
    right.set_actor_hidden_in_game(False)
    check('observer_cosmetic_hiding_preserves_physical_electrical_connection')
    right.set_actor_hidden_in_game(True)
    right.set_actor_enable_collision(False)
    named['WS_DistributionDoor_Right'].set_actor_hidden_in_game(True)
    named['WS_DistributionDoor_Right'].set_actor_enable_collision(False)
    refresh()
    connection(station, left)
    left.set_actor_hidden_in_game(True)
    left.set_actor_enable_collision(False)
    refresh()
    assert not station.get_editor_property('supply_cabinet') and not route(station)
    assert station.get_editor_property('supply_cable_status')
    assert not cable_actors(), 'Missing panels leave stale physical supply cable'
    assert station.get_editor_property('mains_power') == mains, 'Visual routing altered existing charge availability'
    for side in ('Left', 'Right'):
        for key in ('DistributionCabinet', 'DistributionDoor'):
            a = named['WS_'+key+'_'+side]
            a.set_actor_hidden_in_game(False)
            a.set_actor_enable_collision(True)
    refresh()
    connection(station, right)
    check('hidden_panels_are_ignored_missing_supply_clears_geometry_without_charging_regression')

    if '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower():
        phase = 'capture'
        camera = named['WiringCamera']
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),
                                  unreal.Vector(900, 1940, 190)), False)
        pc.set_view_target_with_blend(camera, 0)
        yield from frames(12)
        image = OUT/('WallSupply_'+str(time.time_ns())+'.png')
        unreal.SystemLibrary.execute_console_command(game, f'HighResShot 1600x900 filename="{image.as_posix()}"')
        for _ in range(120):
            yield
            if image.exists() and image.stat().st_size > 1000:
                content = image.read_bytes()
                if content[:8] == b'\x89PNG\r\n\x1a\n' and content[-8:-4] == b'IEND':
                    report['screenshots'].append(str(image))
                    break
        assert report['screenshots'], 'Rendered screenshot was not written'

    phase = 'building_regeneration'
    pc.set_construction_editing(True)
    building = allof(unreal.WarehouseBuildingLayout)[0]
    proposed = building.get_settings()
    proposed.width_cm, proposed.depth_cm = 5600, 4800
    for field in ('floor2', 'floor3', 'floor4'):
        floor = proposed.get_editor_property(field)
        floor.enabled = False
        proposed.set_editor_property(field, floor)
    assert building.apply_settings(proposed), building.get_editor_property('status')
    yield from frames(2)
    generated = list(building.get_editor_property('generated_structure'))
    panels = [a for a in generated if isinstance(a, unreal.StaticMeshActor) and
              a.static_mesh_component.static_mesh and
              'SM_WS_DistributionCabinet' in a.static_mesh_component.static_mesh.get_name()]
    assert panels, 'Building regeneration removed all active distribution cabinets'
    source_centre = unreal.SystemLibrary.get_component_bounds(next(c for c in station.get_components_by_class(unreal.StaticMeshComponent) if c.get_name() == 'Cabinet'))[0]
    expected = min(panels, key=lambda a: distance(unreal.SystemLibrary.get_component_bounds(a.static_mesh_component)[0], source_centre))
    connection(station, expected)
    physical_cables()
    for source in (left, right):
        assert source.get_editor_property('hidden'), 'Old panel remains active after replacement shell'
    previous_count = len(cable_actors())
    assert building.apply_settings(proposed), building.get_editor_property('status')
    yield from frames(2)
    connection(station)
    assert len(cable_actors()) == previous_count, 'Reconfirmation accumulated old cable actors'
    check('building_resize_keeps_active_wall_cabinets_reconnects_and_does_not_accumulate',
          cabinets=len(panels), cable_actors=previous_count)


def saved_map_scenario():
    global phase, game, pc, station, wiring, temporary_editor_camera
    assert not editor.get_game_world(), 'Stop previous PIE first'
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    source = list(actors.get_all_level_actors())
    source_chargers = [a for a in source if isinstance(a, unreal.WarehouseChargingStation)]
    assert len(source_chargers) >= 2, 'Expected original charging stations'
    source_bounds = {a.get_actor_label(): bounds(a) for a in source_chargers}
    for actor in source:
        if isinstance(actor, unreal.WarehouseForklift):
            temporary_editor_flags.append((actor, actor.get_editor_property('auto_start')))
            actor.set_editor_property('auto_start', False)
    temporary_editor_camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 300))
    temporary_editor_camera.set_actor_label('WiringSavedMapCamera')
    level.editor_request_begin_play()
    while not editor.get_game_world():
        yield
    game = editor.get_game_world()
    yield from frames(25)
    phase = 'saved_map_initial_connections'
    pc = unreal.GameplayStatics.get_player_controller(game, 0)
    stations = sorted(allof(unreal.WarehouseChargingStation), key=lambda a: a.get_actor_label())
    station = stations[0]
    candidates = []
    for actor in allof(unreal.StaticMeshActor):
        part = actor.static_mesh_component
        if part.static_mesh and part.static_mesh.get_name() == 'SM_WS_DistributionCabinet' and actor.get_actor_enable_collision() and not actor.get_editor_property('hidden'):
            candidates.append(actor)
    assert candidates, 'No active original distribution panels'
    rows = []
    for charger in stations:
        source_centre = unreal.SystemLibrary.get_component_bounds(next(c for c in charger.get_components_by_class(unreal.StaticMeshComponent) if c.get_name() == 'Cabinet'))[0]
        nearest = min(candidates, key=lambda a: distance(source_centre, unreal.SystemLibrary.get_component_bounds(a.static_mesh_component)[0]))
        _, points = connection(charger, nearest)
        assert bounds(charger) == source_bounds[charger.get_actor_label()], 'Derived wires altered real charger dimensions'
        rows.append(dict(charger=charger.get_actor_label(), panel=nearest.get_actor_label(), route=[vec(p) for p in points]))
    cable_count, part_count = physical_cables()
    check('actual_map_begin_play_connects_existing_chargers_to_nearest_panels',
          chargers=len(stations), panels=len(candidates), cables=cable_count, physical_parts=part_count, connections=rows)
    wiring = next(o for o in unreal.ObjectIterator(unreal.WarehouseChargerWiring) if o.get_outer() == game)
    previous = wiring.get_editor_property('rebuild_count')
    yield from frames(30)
    assert wiring.get_editor_property('rebuild_count') == previous
    check('actual_map_idle_does_not_repeat_cable_search', frames=30, rebuild_count=previous)
    unreal.GameplayStatics.set_game_paused(game, True)
    # Reproduce first addition after the vehicle's fixed map was already cached.
    vehicle = allof(unreal.WarehouseForklift)[0]
    old_power = vehicle.get_editor_property('powered')
    if not old_power:
        vehicle.toggle_power()
    assert vehicle.get_editor_property('powered')
    # The production sensor kit is created lazily on the first powered tick.
    unreal.GameplayStatics.set_game_paused(game, False)
    yield from frames(2)
    unreal.GameplayStatics.set_game_paused(game, True)
    sensor = vehicle.get_component_by_class(unreal.AgvShadowSensorsComponent)
    assert sensor, 'Powered forklift did not initialize its production sensor kit'
    unreal.WarehouseChargerWiring.set_suspended(station, True)
    sensor.invalidate_snapshot('WIRING TEST: CACHE EMPTY INSTALLATION')
    unreal.GameplayStatics.set_game_paused(game, False)
    deadline = unreal.GameplayStatics.get_time_seconds(game)+3
    while not sensor.get_editor_property('snapshot_valid'):
        assert unreal.GameplayStatics.get_time_seconds(game) < deadline, 'Ordinary sensor tick did not initialize the empty installation map'
        yield
    unreal.GameplayStatics.set_game_paused(game, True)
    before_scan = sensor.get_editor_property('snapshot_count')
    before_map = sensor.get_editor_property('map_revision')
    last_scan = sensor.get_editor_property('last_snapshot_seconds')
    unreal.WarehouseChargerWiring.set_suspended(station, False)
    assert sensor.get_editor_property('snapshot_count') == before_scan, 'Wiring triggered an extra sensor scan'
    unreal.GameplayStatics.set_game_paused(game, False)
    deadline = unreal.GameplayStatics.get_time_seconds(game)+3
    while sensor.get_editor_property('snapshot_count') == before_scan:
        assert unreal.GameplayStatics.get_time_seconds(game) < deadline, 'Next ordinary one-second scan did not arrive'
        yield
    unreal.GameplayStatics.set_game_paused(game, True)
    interval = sensor.get_editor_property('last_snapshot_seconds')-last_scan
    assert interval >= .99, ('One-second sampling period was bypassed', interval)
    assert sensor.get_editor_property('map_revision') > before_map
    for cable in cable_actors():
        for part in cable.get_components_by_class(unreal.PrimitiveComponent):
            if unreal.WarehouseDamageSystem.is_physical_contact(part):
                assert sensor.is_known_component(part), 'Added fixed supply conduit missing from known map'
    if vehicle.get_editor_property('powered') != old_power:
        vehicle.toggle_power()
    check('first_cable_addition_marks_fixed_map_dirty_without_extra_sensor_scans')
    if '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower():
        phase = 'saved_map_capture'
        camera = next(a for a in allof(unreal.CameraActor) if a.get_actor_label() == 'WiringSavedMapCamera')
        points = route(station)
        lo = unreal.Vector(*(min(getattr(p, axis) for p in points) for axis in ('x', 'y', 'z')))
        hi = unreal.Vector(*(max(getattr(p, axis) for p in points) for axis in ('x', 'y', 'z')))
        centre = (lo+hi)*.5
        span = max(700, hi.x-lo.x, hi.y-lo.y)
        eye = centre+station.get_actor_forward_vector()*(span*.85)+station.get_actor_right_vector()*(span*.35)+unreal.Vector(0, 0, 190)
        camera.set_actor_location(eye, False, True)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, centre), False)
        camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(75)
        pc.set_view_target_with_blend(camera, 0)
        yield from frames(15)
        image = OUT/('ActualWarehouseSupply_'+str(time.time_ns())+'.png')
        unreal.SystemLibrary.execute_console_command(game, f'HighResShot 1920x1080 filename="{image.as_posix()}"')
        for _ in range(120):
            yield
            if image.exists() and image.stat().st_size > 1000:
                content = image.read_bytes()
                if content[:8] == b'\x89PNG\r\n\x1a\n' and content[-8:-4] == b'IEND':
                    report['screenshots'].append(str(image))
                    break
        assert report['screenshots'], 'Actual-map rendered screenshot was not written'


def finalize():
    global finished
    if finished:
        return
    finished = True
    unreal.unregister_slate_post_tick_callback(handle)
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    for actor, value in temporary_editor_flags:
        if unreal.SystemLibrary.is_valid(actor):
            actor.set_editor_property('auto_start', value)
    if temporary_editor_camera and unreal.SystemLibrary.is_valid(temporary_editor_camera):
        actors.destroy_actor(temporary_editor_camera)
    unchanged = all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == digest for p, digest in hashes.items())
    failure = error if unchanged else (error or '')+'\nOriginal map or forklift algorithm changed'
    report.update(passed=not failure, phase=phase, error=failure,
                  saved_map_and_forklift_sources_unchanged=unchanged,
                  wall_seconds=round(time.monotonic()-start, 3))
    (OUT/'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('CHARGER_WIRING_PIE_'+('FAILED' if failure else 'PASSED'), failure or str(OUT/'Report.json'))
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    global busy, cleaning, error
    if busy or finished:
        return
    busy = True
    try:
        if cleaning:
            if not editor.get_game_world():
                finalize()
            return
        assert time.monotonic()-start < 240, ('Wiring test timed out', phase)
        next(scenario)
    except StopIteration:
        cleaning = True
        level.editor_request_end_play()
    except Exception:
        error = traceback.format_exc()
        cleaning = True
        level.editor_request_end_play()
    finally:
        busy = False


scenario = saved_map_scenario() if SAVED_MAP else scenario_body()
handle = unreal.register_slate_post_tick_callback(tick)
