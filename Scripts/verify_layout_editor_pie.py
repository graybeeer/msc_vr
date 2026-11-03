"""Saved warehouse runtime layout transactions; never save the level.

Calls the native drag transaction API, rather than injecting mouse input.
Commit/cancel/overlap/floor checks retain actual meshes and physical joints.
Metadata must first be authored by configure_warehouse_layout.py.

-LayoutPhysicalOnly excludes screen projection/marquee checks for NullRHI.
It retains all native layout, face-snap and post-resume physics assertions.
"""
import json
from pathlib import Path
import time
import math
import traceback
import unreal

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
PHYSICAL_ONLY = '-layoutphysicalonly' in unreal.SystemLibrary.get_command_line().lower()
report = {'passed': False, 'physicalOnly': PHYSICAL_ONLY, 'renderedUIExcluded': PHYSICAL_ONLY,
          'excludedChecks': ['screen-projected marquee selection and cursor projection'] if PHYSICAL_ONLY else []}
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level(LEVEL)
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.WarehouseForklift):
        actor.set_editor_property('auto_start', False)
        actor.set_editor_property('idle_parking_enabled',False) # Isolate rig restoration/restart from newly added automatic home trips.
    if isinstance(actor, unreal.WarehouseInboundZone):
        actor.set_editor_property('fleet', [])
# A parked-at-base positive fixture is authored before PIE and never saved.
# Production vehicles start at inbound staging, so the second saved pairing
# remains untouched and tests moving a charger while its vehicle is away.
authored = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
for label in ('WH_AutonomousForklift', 'WH_AutonomousForklift_02'):
    saved_vehicle = authored[label]
    saved_station = saved_vehicle.get_editor_property('charging_station')
    assert saved_station and saved_station.get_editor_property('assigned_vehicle') == saved_vehicle
    assert saved_station.is_docked(saved_vehicle), ('Saved initial pose is not at its assigned charging bay', label)
print('CHARGER_SAVED_START_POSES_PASSED', '2 original saved pairs, before any test fixtures')
fixture_vehicle = authored['WH_AutonomousForklift']
fixture_station = fixture_vehicle.get_editor_property('charging_station')
assert fixture_station and fixture_station.get_editor_property('assigned_vehicle') == fixture_vehicle
fixture_vehicle.set_actor_transform(fixture_station.get_actor_transform(), False, True)
dock = fixture_station.get_actor_transform()
exit_pose = unreal.Transform(location=dock.translation + fixture_station.get_actor_forward_vector() * 120,
                             rotation=fixture_station.get_actor_rotation())
fixture_vehicle.set_editor_property('navigation_anchors', list(fixture_vehicle.get_editor_property('navigation_anchors')) + [dock, exit_pose])
# Keep one explicit away-from-base fixture after production starts are unified.
# These are authored before native rig initialization, never runtime warps.
away_fixture = authored['WH_AutonomousForklift_02']
away_fixture.set_actor_location(unreal.Vector(1030, -1790, 0), False, True)
away_fixture.set_actor_rotation(unreal.Rotator(), True)
operator_fixture = actors.spawn_actor_from_class(unreal.CameraActor, dock.translation + unreal.Vector(0, 0, 100))
operator_fixture.set_actor_label('LayoutChargerTestOperator')
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
selection_camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 6500), unreal.Rotator(pitch=-90))
selection_camera.set_actor_label('LayoutSelectionTestCamera')
selection_camera.get_component_by_class(unreal.CameraComponent).set_editor_property('field_of_view', 60.)
selection_camera.get_component_by_class(unreal.CameraComponent).set_editor_property('constrain_aspect_ratio', False)
step_fixture = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(-30000, -30000, -2000))
step_fixture.set_actor_label('LayoutWheelStepFixture')
step_fixture.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
step_fixture.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
step_fixture.static_mesh_component.set_collision_profile_name('BlockAll')
step_fixture.set_actor_scale3d(unreal.Vector(.2, .2, .05))  # A real 20x20x5cm solid step, not a floor-name exception.
# Unsaved clear test space lets original production assemblies rotate without
# clearing warehouse objects or bypassing native placement/support checks.
for floor_index in range(3):
    pad = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(8000, 0, floor_index*400-10))
    pad.set_actor_label('LayoutRotationFloor_L'+str(floor_index))
    pad.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    pad.static_mesh_component.set_collision_profile_name('BlockAll')
    pad.set_actor_scale3d(unreal.Vector(80, 80, .2))
    pad.tags = ['LayoutFixed', 'WarehouseMapStructure', 'ElevatorLayoutFloor']
rotation_blocker = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(-30000, -30000, -2000))
rotation_blocker.set_actor_label('LayoutRotationSolidBlocker')
rotation_blocker.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
rotation_blocker.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
rotation_blocker.static_mesh_component.set_collision_profile_name('BlockAll')
rotation_blocker.set_actor_scale3d(unreal.Vector(3, 3, 4))

started = time.monotonic()
phase = 0
game = layout = live = selected = snapshot = None
resume_at = None
truck_snapshot = None
truck_controller = truck_cargo_identity = None
selection_pair = None
selection_at = None


def tag_values(actor):
    return [str(t) for t in actor.tags]


def group_key(actor):
    return next((t for t in tag_values(actor) if t.startswith('LayoutGroup:')), actor.get_name())


def rack_records(floor=0):
    unique = {}
    for actor in live:
        if 'LayoutType:Rack' in tag_values(actor) and f'WarehouseFloor{floor}' in tag_values(actor):
            unique.setdefault(group_key(actor), actor)
    return [(actor, group_members(actor), box_of(group_members(actor))) for actor in unique.values()]


def group_members(actor):
    group = next((t for t in tag_values(actor) if t.startswith('LayoutGroup:')), None)
    return [a for a in live if group in tag_values(a)] if group else [actor]


def box_of(members):
    low = [float('inf')] * 3
    high = [float('-inf')] * 3
    for actor in members:
        centre, extent = actor.get_actor_bounds(False)
        for i, key in enumerate(('x', 'y', 'z')):
            low[i] = min(low[i], getattr(centre, key) - getattr(extent, key))
            high[i] = max(high[i], getattr(centre, key) + getattr(extent, key))
    return low, high


def goods_inside(low, high):
    return [a for a in live if isinstance(a, (unreal.WarehouseCargo, unreal.WarehousePallet))
            and low[0] - 3 <= a.get_actor_location().x <= high[0] + 3
            and low[1] - 3 <= a.get_actor_location().y <= high[1] + 3
            and -2 <= a.get_actor_location().z < 398]


def with_supported(members):
    result = list(dict.fromkeys(members))
    systems = [a for a in live if isinstance(a, unreal.WarehouseDamageSystem)]
    if systems:
        for index, actor in enumerate(result):
            if not isinstance(actor,(unreal.WarehousePallet,unreal.WarehouseCargo)) and 'LayoutType:Rack' not in tag_values(actor):
                continue
            for cargo in systems[0].get_supported_actors(actor, index == 0):
                if isinstance(cargo, (unreal.WarehouseCargo, unreal.WarehousePallet)) and cargo not in result:
                    result.append(cargo)
    return result


def capture(members):
    result = {}
    for actor in members:
        components = {}
        for component in actor.get_components_by_class(unreal.SceneComponent):
            physical = isinstance(component, unreal.PrimitiveComponent)
            transit_body = physical and 'LogisticsInTransit' in tag_values(actor) and unreal.WarehouseDamageSystem.is_physical_contact(component)
            components[component.get_name()] = {
                'part': component,
                'pose': component.get_world_transform(),
                'parent': component.get_attach_parent(),
                'mobility': component.get_editor_property('mobility'),
                'collision': component.get_collision_enabled() if physical else None,
                'simulated': component.is_simulating_physics() if physical else None,
                'mass': component.get_mass() if physical and component.is_simulating_physics() else None,
                'mass_override': (component.get_editor_property('body_instance').get_editor_property('override_mass'), component.get_editor_property('body_instance').get_editor_property('mass_in_kg_override')) if transit_body else None,
                'gravity': component.is_gravity_enabled() if transit_body else None,
            }
        result[actor] = {'pose': actor.get_actor_transform(), 'components': components,
                         'actor_collision': actor.get_actor_enable_collision()}
    return result


def translated(snapshot, delta):
    for actor, saved in snapshot.items():
        assert (actor.get_actor_location() - saved['pose'].translation - delta).length() < .12, ('Actor translation mismatch', actor.get_actor_label())
        for state in saved['components'].values():
            part = state['part']
            assert (part.get_world_location() - state['pose'].translation - delta).length() < .15, ('Part translation mismatch', actor.get_actor_label(), part.get_name(), part.get_world_location(), state['pose'].translation, delta)


def disabled(snapshot):
    for actor, saved in snapshot.items():
        assert not actor.get_actor_enable_collision(), ('Actor still blocks while dragging', actor.get_actor_label())
        for state in saved['components'].values():
            if state['collision'] is not None:
                assert not state['part'].is_simulating_physics(), ('Simulates while dragging', state['part'].get_name())
    # Actor filtering must also remove these parts from real query traces.
    for actor in snapshot:
        centre, extent = actor.get_actor_bounds(False)
        hit = unreal.SystemLibrary.line_trace_single(game, centre + unreal.Vector(0, 0, extent.z + 20),
            centre - unreal.Vector(0, 0, extent.z + 20), unreal.TraceTypeQuery.ECC_VISIBILITY,
            False, [], unreal.DrawDebugTrace.NONE, True)
        if hit is None:
            continue
        native = hit if isinstance(hit, unreal.HitResult) else next(value for value in hit if isinstance(value, unreal.HitResult))
        data = native.to_tuple()
        if data[0]:
            assert data[9] not in snapshot, ('Native trace still hit a dragged actor', actor.get_actor_label())


def restored(snapshot):
    for actor, saved in snapshot.items():
        assert actor.get_actor_enable_collision() == saved['actor_collision'], ('Actor collision flag not restored', actor.get_actor_label())
        for state in saved['components'].values():
            part = state['part']
            assert part.get_editor_property('mobility') == state['mobility'], ('Mobility not restored', actor.get_actor_label(), part.get_name())
            assert part.get_attach_parent() == state['parent'], ('Attachment/weld parent changed', actor.get_actor_label(), part.get_name())
            if state['collision'] is not None:
                assert part.get_collision_enabled() == state['collision'], ('Collision not restored', part.get_name())
                assert part.is_simulating_physics() == state['simulated'], ('Physics not restored', part.get_name())
                if state['mass'] is not None:
                    assert abs(part.get_mass() - state['mass']) < .01, ('Physical mass changed', actor.get_actor_label(), part.get_name())
                if state['gravity'] is not None:
                    assert part.is_gravity_enabled() == state['gravity'], ('Transit body gravity changed', actor.get_actor_label(), part.get_name())
                if state['mass_override'] is not None:
                    body = part.get_editor_property('body_instance')
                    assert (body.get_editor_property('override_mass'), body.get_editor_property('mass_in_kg_override')) == state['mass_override'], ('Transit saved mass override changed', actor.get_actor_label(), part.get_name())


def around(pose, pivot, degrees):
    local = unreal.MathLibrary.make_relative_transform(pose, unreal.Transform(location=pivot))
    return unreal.MathLibrary.compose_transforms(local, unreal.Transform(location=pivot, rotation=unreal.Rotator(yaw=degrees)))


def rotated(state, pivot, degrees):
    for actor, saved in state.items():
        assert unreal.MathLibrary.nearly_equal_transform_transform(actor.get_actor_transform(), around(saved['pose'], pivot, degrees), .15, .05, .001), ('Actor failed rigid rotation', actor.get_actor_label())
        for part in saved['components'].values():
            assert unreal.MathLibrary.nearly_equal_transform_transform(part['part'].get_world_transform(), around(part['pose'], pivot, degrees), .15, .05, .001), ('Component failed rigid rotation', actor.get_actor_label(), part['part'].get_name())
    restored(state)


def rotation_references(members, zone_names=()):
    result = []
    for actor in live:
        if isinstance(actor, unreal.WarehouseInboundZone):
            for index, slot in enumerate(actor.get_editor_property('rack_slots')):
                if slot.rack in members:
                    result.append((lambda a=actor, i=index: a.get_editor_property('rack_slots')[i].pose, slot.pose, True))
        if isinstance(actor, unreal.WarehouseElevator) and actor in members:
            for floor in range(3):
                for method in (actor.waiting_pose, actor.boarding_pose):
                    result.append((lambda m=method, f=floor: m(f), method(floor), True))
        if isinstance(actor, unreal.WarehouseChargingStation) and actor in members:
            for vehicle in actor.get_linked_vehicles():
                if vehicle not in members:
                    continue
                anchors = vehicle.get_editor_property('navigation_anchors')
                for index in range(max(0, len(anchors)-2), len(anchors)):
                    result.append((lambda v=vehicle, i=index: v.get_editor_property('navigation_anchors')[i], anchors[index], True))
    for name in zone_names:
        zone = next(z for z in layout.get_editor_property('zones') if z.name == name)
        result.append((lambda n=name: unreal.Transform(location=next(z for z in layout.get_editor_property('zones') if z.name == n).centre), unreal.Transform(location=zone.centre), False))
        for index, pose in enumerate(zone.destinations):
            result.append((lambda n=name, i=index: next(z for z in layout.get_editor_property('zones') if z.name == n).destinations[i], pose, True))
    return result


def check_rotation_references(refs, pivot, degrees):
    for current, old, orientation in refs:
        expected = around(old, pivot, degrees)
        # A zone's centre has no orientation; all stored poses do.
        assert (current().translation-expected.translation).length() < .15, 'Rotated layout reference stayed stale'
        if orientation:
            assert unreal.MathLibrary.nearly_equal_transform_transform(current(), expected, .15, .05, .001), 'Rotated layout pose orientation stayed stale'


def rotation_bounds(members, zone_names=()):
    boxes = [unreal.SystemLibrary.get_component_bounds(p)[:2] for a in members
             for p in a.get_components_by_class(unreal.PrimitiveComponent)
             if unreal.WarehouseDamageSystem.is_physical_contact(p)]
    boxes += [(z.centre, z.half_size) for z in layout.get_editor_property('zones') if z.name in zone_names]
    for bay in (a for a in members if isinstance(a, unreal.WarehouseWaitingBay)):
        half = bay.get_editor_property('size_cm')*.5
        # Native RotationBounds transforms the flat local SizeCm FBox, so use
        # its four transformed corners (including actor yaw and scale).
        corners = [unreal.MathLibrary.transform_location(bay.get_actor_transform(), unreal.Vector(x*half.x, y*half.y, 0))
                   for x in (-1, 1) for y in (-1, 1)]
        low = [min(getattr(p, k) for p in corners) for k in 'xyz']
        high = [max(getattr(p, k) for p in corners) for k in 'xyz']
        boxes.append((unreal.Vector(*[(a+b)*.5 for a, b in zip(low, high)]),
                      unreal.Vector(*[(b-a)*.5 for a, b in zip(low, high)])))
    if not boxes:
        boxes = [(a.get_actor_location(), unreal.Vector()) for a in members]
    low = [min(getattr(c, k)-getattr(e, k) for c, e in boxes) for k in 'xyz']
    high = [max(getattr(c, k)+getattr(e, k) for c, e in boxes) for k in 'xyz']
    return low, high


def rotation_roundtrip(seeds, members, bounds_members=None, zone_names=(), collision_rollback=False):
    members = list(dict.fromkeys(members))
    layout.set_action(unreal.WarehouseLayoutAction.MOVE)
    layout.set_selection(seeds)
    point = (next(z for z in layout.get_editor_property('zones') if z.name == zone_names[0]).centre
             if zone_names else seeds[0].get_actor_location())
    # Discover native group expansion (supported goods, zone furniture, parked
    # rigs) by a cancelled real transaction rather than duplicating its rules.
    enabled = [a for a in live if a.get_actor_enable_collision()]
    assert layout.begin_drag(seeds[0], point), layout.get_editor_property('status')
    members = list(dict.fromkeys(members+[a for a in enabled if not a.get_actor_enable_collision()]))
    assert not layout.end_drag(False)
    original = capture(members)
    original_refs = rotation_references(members, zone_names)
    layout.set_action(unreal.WarehouseLayoutAction.MOVE)
    layout.set_selection(seeds)
    low, high = rotation_bounds(members, zone_names)
    centre = unreal.Vector(*[(low[i]+high[i])*.5 for i in range(3)])
    point = (next(z for z in layout.get_editor_property('zones') if z.name == zone_names[0]).centre
             if zone_names else seeds[0].get_actor_location())
    assert drag(seeds[0], point, unreal.Vector(8000-centre.x, -centre.y, 0), True), ('Cannot move real assembly to clear rotation fixture', layout.get_editor_property('status'))
    delta = seeds[0].get_actor_location()-original[seeds[0]]['pose'].translation
    translated(original, delta)
    staged = capture(members)
    low, high = rotation_bounds(members, zone_names)
    pivot = unreal.Vector(*[(low[i]+high[i])*.5 for i in range(3)])
    refs = rotation_references(members, zone_names)
    zone_sizes = {z.name: z.half_size for z in layout.get_editor_property('zones') if z.name in zone_names}
    layout.set_selection(seeds)
    assert layout.rotate_selection(90), ('Clear production assembly could not rotate', layout.get_editor_property('status'))
    rotated(staged, pivot, 90)
    for seed in seeds[1:]:
        assert abs((seed.get_actor_location()-seeds[0].get_actor_location()).length()-(staged[seed]['pose'].translation-staged[seeds[0]]['pose'].translation).length()) < .15, 'Common-centre rotation changed relative group spacing'
    check_rotation_references(refs, pivot, 90)
    for zone in layout.get_editor_property('zones'):
        if zone.name in zone_sizes:
            old = zone_sizes[zone.name]
            assert abs(zone.half_size.x-old.y)<.1 and abs(zone.half_size.y-old.x)<.1, 'Rotated zone footprint did not swap width/depth'
    assert layout.rotate_selection(-90), ('Inverse clear rotation failed', layout.get_editor_property('status'))
    rotated(staged, pivot, 0)
    check_rotation_references(refs, pivot, 0)
    if collision_rollback:
        blocker = next(a for a in live if a.get_actor_label() == 'LayoutRotationSolidBlocker')
        blocker_pose = blocker.get_actor_transform()
        low, high = box_of(members)
        candidates = []
        for actor in members:
            if not isinstance(actor, unreal.WarehouseCargo):
                continue
            centre, _ = actor.get_actor_bounds(False)
            target = around(unreal.Transform(location=centre), pivot, 90).translation
            outside = max(low[0]-target.x, target.x-high[0], low[1]-target.y, target.y-high[1])
            candidates.append((outside, target))
        outside, target = max(candidates, key=lambda item: item[0])
        assert outside > 15, 'No rotated cargo point clear of the original mixed footprint for a solid rollback fixture'
        blocker.set_actor_scale3d(unreal.Vector(.2, .2, .2))
        blocker.set_actor_location(target, False, True)
        try:
            layout.set_selection(seeds)
            assert not layout.rotate_selection(90), 'Rotation intersecting a real solid was accepted'
            rotated(staged, pivot, 0)
            check_rotation_references(refs, pivot, 0)
        finally:
            blocker.set_actor_transform(blocker_pose, False, True)
        print('LAYOUT_ROTATION_ATOMIC_COLLISION_ROLLBACK_PASSED')
    layout.set_selection(seeds)
    assert drag(seeds[0], point+delta, -delta, True), ('Rotated assembly did not return to its original placement', layout.get_editor_property('status'))
    rotated(original, unreal.Vector(), 0)
    check_rotation_references(original_refs, unreal.Vector(), 0)
    report.setdefault('rotationCases', []).append(dict(category=str(layout.get_editor_property('category')), groups=len(seeds), members=len(members), references=len(refs), degrees=[90,-90], collisionRollback=collision_rollback))
    print('LAYOUT_RIGID_ROTATION_REFERENCE_PHYSICS_PASSED', str(layout.get_editor_property('category')), len(seeds), len(members), len(refs))


def all_category_checks():
    frame = next(a for a in live if a.get_actor_label() == 'WH_InboundRack_00_A')
    waiting = next(a for a in live if isinstance(a, unreal.WarehouseWaitingBay))
    upper = next(a for a in live if 'LayoutType:Rack' in tag_values(a) and 'WarehouseFloor1' in tag_values(a))
    fixed = next(a for a in live if a.get_actor_label() == 'LayoutRotationFloor_L0')
    pawn = unreal.GameplayStatics.get_player_pawn(game, 0)
    transit = next(a for a in live if a.actor_has_tag('LogisticsInTransit'))
    layout.set_category(unreal.WarehouseLayoutCategory.ALL)
    layout.set_selection([frame, waiting, upper, fixed, pawn, transit])
    selection = list(layout.get_selection())
    assert len(selection) == 2 and {group_key(a) for a in selection} == {group_key(frame), group_key(waiting)}, ('All category failed mixed selection/current-floor/fixed/pawn/transit filtering', selection)
    members = group_members(frame)+[waiting]
    rotation_roundtrip([frame, waiting], members)
    print('LAYOUT_ALL_MIXED_SELECTION_MOVE_ROTATION_FILTERS_PASSED')


def truck_cargo_contract(controller, cartons, identity=None):
    assert controller.is_manifest_ready(), controller.get_editor_property('status')
    manifest = list(controller.get_editor_property('manifest_pallets'))
    assert set(manifest) == set(controller.get_editor_property('initial_pallets')), 'Original truck pallet manifest changed'
    unit_cargo = [cargo for pallet in manifest for cargo in controller.get_unit_cargo(pallet)]
    assert len(unit_cargo) == len(set(unit_cargo)), 'Carton belongs to multiple truck units'
    transit = {cargo for cargo in cartons if 'LogisticsInTransit' in tag_values(cargo)}
    assert set(unit_cargo) == transit, 'Transit cartons do not match the exact truck manifest'
    assert all(controller.is_unit_on_truck(pallet) for pallet in manifest), 'A manifested unit left its recorded truck slot'
    expected_mass = sum(p.get_editor_property('pallet_mass_kg') + p.get_editor_property('payload_mass_kg') for p in manifest)
    expected_mass += sum(c.get_editor_property('gross_mass_kg') for c in unit_cargo)
    assert abs(controller.payload_mass_kg() - expected_mass) < .1, 'Truck unit mass metadata changed'
    current_identity = {}
    for carton in cartons:
        part = carton.get_component_by_class(unreal.StaticMeshComponent)
        mass = carton.get_editor_property('gross_mass_kg')
        assert part.is_simulating_physics() == (carton not in transit), ('Truck cinematic/dynamic body contract changed', carton.get_actor_label())
        if carton in transit:
            body = part.get_editor_property('body_instance')
            assert body.get_editor_property('override_mass') and abs(body.get_editor_property('mass_in_kg_override') - mass) < .1, ('Truck carton saved mass override changed', carton.get_actor_label())
        else:
            assert abs(part.get_mass() - mass) < .1, ('Truck carton native mass changed', carton.get_actor_label())
        current_identity[carton] = (str(carton.get_editor_property('cargo_id')), mass)
    if identity is not None:
        assert current_identity == identity, 'Truck carton identity or gross mass changed during layout editing'
    return current_identity


def truck_overlap_diagnostic(stage, controller, carton, bridge, restore_pose=None):
    pallet = next((p for p in controller.get_editor_property('manifest_pallets') if carton in controller.get_unit_cargo(p)), None)
    part = carton.get_component_by_class(unreal.StaticMeshComponent)
    for actor in (bridge, carton, pallet):
        if actor:
            print('LAYOUT_TRUCK_PROBE_BOUNDS', stage, actor.get_actor_label(), actor.get_actor_transform(),
                  [(p.get_name(), unreal.SystemLibrary.get_component_bounds(p)) for p in actor.get_components_by_class(unreal.PrimitiveComponent) if unreal.WarehouseDamageSystem.is_physical_contact(p)])
    print('LAYOUT_TRUCK_PROBE_FRAME', stage, controller.get_actor_transform(), 'manifest_valid', bool(pallet and controller.is_unit_on_truck(pallet)),
          'cargo_in_pallet', unreal.MathLibrary.make_relative_transform(carton.get_actor_transform(), pallet.get_actor_transform()) if pallet else None,
          'pallet_in_truck', unreal.MathLibrary.make_relative_transform(pallet.get_actor_transform(), controller.get_actor_transform()) if pallet else None)
    for name, pose in [('current', part.get_world_transform())] + ([('restore_preview', restore_pose)] if restore_pose else []):
        overlaps = unreal.SystemLibrary.component_overlap_components(part, pose,
            [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2], unreal.PrimitiveComponent, [carton]) or []
        print('LAYOUT_TRUCK_PROBE_NATIVE', stage, name, pose, 'bridge_overlap', any(p.get_owner() == bridge for p in overlaps),
              'hits', [(p.get_owner().get_actor_label(), p.get_name()) for p in overlaps])


def drag(actor, point, delta, commit):
    assert layout.begin_drag(actor, point), ('BeginDrag rejected', actor.get_actor_label() if actor else 'zone', str(layout.get_editor_property('status')))
    layout.update_drag(point + delta)
    return layout.end_drag(commit)


def find_accepted(actor, point, snapshot, candidates):
    for x, y in candidates:
        delta = unreal.Vector(x, y, 0)
        accepted = drag(actor, point, delta, True)
        status = str(layout.get_editor_property('status'))
        report.setdefault('candidateAttempts', []).append(dict(
            actor=actor.get_actor_label() if actor else 'zone',
            category=str(layout.get_editor_property('category')),
            origin_cm=[point.x, point.y, point.z],
            offset_cm=[x, y, 0], accepted=bool(accepted), status=status))
        if accepted:
            translated(snapshot, delta)
            restored(snapshot)
            return delta
        print('LAYOUT_REJECTED_CANDIDATE',actor.get_actor_label() if actor else 'zone',delta,status)
        translated(snapshot, unreal.Vector())
        restored(snapshot)
    raise AssertionError(('No safe layout commit in candidate positions', actor.get_actor_label() if actor else 'zone', str(layout.get_editor_property('status'))))


def rollback_committed(actor, point, delta):
    assert drag(actor, point + delta, -delta, True), ('Cannot return committed object to the original clear layout', str(layout.get_editor_property('status')))


def wheel_floor_checks():
    # A rolling round tyre keeps its contact geometry, but its render bounds
    # grow below the floor at intermediate axle angles. Exercise both saved rigs.
    layout.set_category(unreal.WarehouseLayoutCategory.FORKLIFT)
    step = next(a for a in live if a.get_actor_label() == 'LayoutWheelStepFixture')
    for vehicle in [a for a in live if isinstance(a, unreal.WarehouseForklift)]:
        original = capture([vehicle])
        wheels = [p for p in vehicle.get_components_by_class(unreal.StaticMeshComponent)
                  if p.get_name() in ('DriveWheel', 'LoadWheelL', 'LoadWheelR')]
        assert len(wheels) == 3
        point = vehicle.get_actor_location()
        for angle in (0, 30, 45, 60, 135):
            spin = unreal.MathLibrary.rotator_from_axis_and_angle(vehicle.get_actor_right_vector(), angle)
            for wheel in wheels:
                saved = original[vehicle]['components'][wheel.get_name()]['pose']
                wheel.set_world_rotation(unreal.MathLibrary.compose_rotators(saved.rotation.rotator(), spin), False, True)
            state = capture([vehicle])
            print('LAYOUT_WHEEL_CONTACT_FIXTURE', vehicle.get_actor_label(), angle,
                  [(w.get_name(), unreal.SystemLibrary.get_component_bounds(w)[0].z -
                    unreal.SystemLibrary.get_component_bounds(w)[1].z) for w in wheels])
            assert drag(vehicle, point, unreal.Vector(), True), ('Valid rolling wheel rejected at its current floor pose',
                vehicle.get_actor_label(), angle, str(layout.get_editor_property('status')))
            translated(state, unreal.Vector())
            restored(state)
            delta = find_accepted(vehicle, point, state, [(0, 30), (0, -30), (-50, 0), (50, 0)])
            rollback_committed(vehicle, point, delta)
            restored(state)

        # Supporting contact must not turn into a blanket floor/obstacle ignore.
        wheel = next(w for w in wheels if w.get_name() == 'LoadWheelR')
        wheel_centre = unreal.SystemLibrary.get_component_bounds(wheel)[0]
        step.set_actor_location(unreal.Vector(wheel_centre.x, wheel_centre.y, point.z + 2.5), False, True)
        assert not drag(vehicle, point, unreal.Vector(), True), 'Wheel was allowed through a raised solid step'
        assert '겹침' in str(layout.get_editor_property('status'))
        translated(state, unreal.Vector())
        restored(state)
        step.set_actor_location(unreal.Vector(-30000, -30000, -2000), False, True)

        # Lower only the free axle bodies by 1cm as a penetration fixture. Moving
        # welded children separately would also move their parent a second time.
        down = unreal.Vector(0, 0, -1)
        for wheel in wheels:
            pose = state[vehicle]['components'][wheel.get_name()]['pose'].copy()
            pose.translation += down
            wheel.set_world_transform(pose, False, True)
        penetrating = capture([vehicle])
        assert not drag(vehicle, vehicle.get_actor_location(), unreal.Vector(), True), 'Wheel penetration into the actual floor was ignored'
        assert '겹침' in str(layout.get_editor_property('status'))
        translated(penetrating, unreal.Vector())
        restored(penetrating)
        for wheel in wheels:
            wheel.set_world_transform(state[vehicle]['components'][wheel.get_name()]['pose'], False, True)

        assert not drag(vehicle, point, unreal.Vector(30000, 30000, 0), True), 'Vehicle accepted unsupported floor'
        translated(state, unreal.Vector())
        restored(state)
        wall = next(a for a in live if a.get_actor_label().startswith('WH_Wall_') and a.get_actor_enable_collision()
                    and a.get_actor_bounds(False)[1].z > 100)
        chassis = vehicle.root_component
        shift = wall.get_actor_bounds(False)[0] - unreal.SystemLibrary.get_component_bounds(chassis)[0]
        shift.z = 0
        assert not drag(vehicle, point, shift, True), 'Vehicle was allowed through a real warehouse wall'
        assert '겹침' in str(layout.get_editor_property('status'))
        translated(state, unreal.Vector())
        restored(state)
        station = vehicle.get_editor_property('charging_station')
        if station and station.is_docked(vehicle):
            layout.set_category(unreal.WarehouseLayoutCategory.CHARGER)
            paired = capture([station, vehicle])
            dock_point = station.get_actor_location()
            delta = find_accepted(station, dock_point, paired, [(30, 0), (-30, 0), (0, 30), (0, -30)])
            rollback_committed(station, dock_point, delta)
            translated(paired, unreal.Vector())
            restored(paired)
            layout.set_category(unreal.WarehouseLayoutCategory.FORKLIFT)
        for wheel in wheels:
            wheel.set_world_transform(original[vehicle]['components'][wheel.get_name()]['pose'], False, True)
        translated(original, unreal.Vector())
        restored(original)
    layout.clear_selection()
    print('LAYOUT_ROLLING_WHEEL_FLOOR_CONTACT_PASSED', '2 saved rigs; 5 axle angles; move/return; step/wall/1cm penetration/unsupported floor rejected; collision/mass/joints retained')


def charger_checks():
    vehicle = next(a for a in live if a.get_actor_label() == 'WH_AutonomousForklift')
    station = vehicle.get_editor_property('charging_station')
    saved_tags = list(station.tags)
    try:
        # Older saved stations also carried Other metadata; the native category
        # guard must still prevent the generic vehicle-refresh path.
        station.tags = list(dict.fromkeys([str(tag) for tag in saved_tags] + ['LayoutType:Other']))
        layout.set_category(unreal.WarehouseLayoutCategory.OTHER)
        layout.set_selection([station])
        assert not layout.get_selection(), 'Charger was selectable in Other category'
        assert not layout.begin_drag(station, station.get_actor_location()), 'Charger drag bypassed Charger category'
    finally:
        station.tags = saved_tags
    layout.set_category(unreal.WarehouseLayoutCategory.CHARGER)
    assert vehicle in station.get_linked_vehicles(), 'Saved vehicle/charger association was lost'
    assert (vehicle.get_actor_location() - station.get_actor_location()).length() < 5, 'Parked fixture did not settle at its dock'
    assert vehicle.get_actor_up_vector().z > .999, 'Parked fixture lost its actual upright orientation'
    assert (vehicle.get_actor_forward_vector() - station.get_actor_forward_vector()).length() < .035, 'Parked fixture lost its actual docking orientation'
    layout.set_selection([station])
    assert layout.get_selection() == [station], 'Saved charger is not selectable in Charger category'
    state = capture([station, vehicle])
    point = station.get_actor_location()
    anchors = [p.translation for p in vehicle.get_editor_property('navigation_anchors')]
    assert layout.begin_drag(station, point), str(layout.get_editor_property('status'))
    layout.update_drag(point + unreal.Vector(37, -23, 800))
    translated(state, unreal.Vector(40, -20, 0))
    disabled(state)
    assert not layout.end_drag(False)
    translated(state, unreal.Vector())
    restored(state)
    delta = find_accepted(station, point, state, [(30, 0), (-30, 0), (0, 30), (0, -30), (100, 0)])
    assert vehicle.get_editor_property('charging_station') == station
    assert station.get_editor_property('assigned_vehicle') == vehicle
    assert vehicle in station.get_linked_vehicles()
    for old, new in zip(anchors[-2:], list(vehicle.get_editor_property('navigation_anchors'))[-2:]):
        assert (new.translation - old - delta).length() < .1, 'Dock/start or charging-exit navigation reference stayed stale'
    rollback_committed(station, point, delta)
    translated(state, unreal.Vector())
    restored(state)
    print('LAYOUT_CHARGER_PARKED_RIG_CANCEL_COMMIT_REFERENCES_PASSED', delta)
    rotation_roundtrip([station], [station, vehicle])
    assert vehicle.get_editor_property('charging_station') == station and vehicle in station.get_linked_vehicles()

    # A real cabinet/wall intersection must be rejected, with both native rigs
    # restored. The fixture does not replace collision geometry or responses.
    cabinet = next(p for p in station.get_components_by_class(unreal.StaticMeshComponent) if p.get_name() == 'Cabinet')
    wall = next(a for a in live if a.get_actor_label() == 'WH_Wall_E') if any(a.get_actor_label() == 'WH_Wall_E' for a in live) else next(
        a for a in live if a.get_actor_label().startswith('WH_Wall_') and a.get_actor_enable_collision()
        and a.get_actor_bounds(False)[1].z > 100)
    wall_centre, _ = wall.get_actor_bounds(False)
    shift = wall_centre - cabinet.get_world_location()
    shift.z = 0
    assert layout.begin_drag(station, point)
    layout.update_drag(point + shift)
    assert not layout.end_drag(True), 'Charger cabinet was allowed through a physical wall'
    assert '겹침' in str(layout.get_editor_property('status')), layout.get_editor_property('status')
    translated(state, unreal.Vector())
    restored(state)
    assert not drag(station, point, unreal.Vector(30000, 30000, 0), True), 'Charger accepted a position without a real same-floor support'
    translated(state, unreal.Vector())
    restored(state)
    print('LAYOUT_CHARGER_COLLISION_FLOOR_ROLLBACK_PASSED')

    operator = next(a for a in live if a.get_actor_label() == 'LayoutChargerTestOperator')
    assert vehicle.begin_remote_control(operator), 'Could not exercise the actual remote exclusion guard'
    try:
        assert layout.begin_drag(station, point)
        layout.update_drag(point + unreal.Vector(100, 0, 0))
        translated({station: state[station]}, unreal.Vector(100, 0, 0))
        translated({vehicle: state[vehicle]}, unreal.Vector())
        assert vehicle.get_actor_enable_collision(), 'Remote vehicle entered the disabled charger drag group'
        assert not layout.end_drag(False)
        translated(state, unreal.Vector())
        restored(state)
    finally:
        vehicle.end_remote_control(operator)
    print('LAYOUT_CHARGER_REMOTE_VEHICLE_NOT_DRAGGED_PASSED')

    away = next(a for a in live if a.get_actor_label() == 'WH_AutonomousForklift_02')
    away_station = away.get_editor_property('charging_station')
    assert (away.get_actor_location() - away_station.get_actor_location()).length() > 5
    untouched = capture([away])
    station_state = capture([away_station])
    away_point = away_station.get_actor_location()
    delta = find_accepted(away_station, away_point, station_state, [(30, 0), (-30, 0), (0, 30), (0, -30), (100, 0)])
    translated(untouched, unreal.Vector())
    restored(untouched)
    assert away.get_editor_property('charging_station') == away_station
    rollback_committed(away_station, away_point, delta)
    translated(untouched, unreal.Vector())
    restored(untouched)
    print('LAYOUT_CHARGER_AWAY_VEHICLE_POSITION_PRESERVED_PASSED', delta)
    layout.clear_selection()


def reject_wall_overlap(actor, point, state):
    # LayoutFixed also includes nonblocking trees/signs and meshes whose pivot
    # is outside their visible bounds. Use a real enabled BlockAll cube wall and
    # put a solid moved carton inside its measured bounds, rather than centring
    # the empty opening of a rack frame at an arbitrary actor pivot.
    parcel = next(a for a in state if isinstance(a, unreal.WarehouseCargo))
    parcel_centre, _ = parcel.get_actor_bounds(False)
    walls = []
    for candidate in live:
        if not candidate.get_actor_label().startswith('WH_Wall_') or not candidate.get_actor_enable_collision():
            continue
        part = candidate.get_component_by_class(unreal.StaticMeshComponent)
        if not part or part.get_collision_profile_name() != 'BlockAll' or part.get_collision_enabled() != unreal.CollisionEnabled.QUERY_AND_PHYSICS:
            continue
        centre, extent = candidate.get_actor_bounds(False)
        if min(extent.x, extent.y) <= 25 and max(extent.x, extent.y) >= 200 and extent.z > 100 and centre.z - extent.z < 10:
            walls.append(candidate)
    assert walls, 'Missing enabled physical warehouse wall for collision rejection'
    wall = min(walls, key=lambda a: (a.get_actor_bounds(False)[0] - parcel_centre).length())
    wall_centre, wall_extent = wall.get_actor_bounds(False)
    delta = wall_centre - parcel_centre
    delta.z = 0
    previous_snap = layout.get_editor_property('edge_snap_enabled')
    before = layout.get_editor_property('moved_count')
    try:
        layout.set_editor_property('edge_snap_enabled', False)
        assert layout.begin_drag(actor, point)
        layout.update_drag(point + delta)
        moved_centre, moved_extent = parcel.get_actor_bounds(False)
        depths = [min(getattr(moved_centre, axis) + getattr(moved_extent, axis), getattr(wall_centre, axis) + getattr(wall_extent, axis))
                  - max(getattr(moved_centre, axis) - getattr(moved_extent, axis), getattr(wall_centre, axis) - getattr(wall_extent, axis))
                  for axis in ('x', 'y', 'z')]
        assert min(depths) > 2, ('Fixture failed to produce a real volumetric wall overlap', depths)
        print('LAYOUT_BLOCKING_WALL_FIXTURE', wall.get_actor_label(), wall_centre, 'moved carton', parcel.get_actor_label(), moved_centre, 'penetration_cm', depths)
        assert not layout.end_drag(True), 'Native overlapping wall drop was accepted'
        assert '겹침' in str(layout.get_editor_property('status')), ('Drop rejected for another reason', layout.get_editor_property('status'))
        assert layout.get_editor_property('moved_count') == before, 'Rejected overlap partially committed the transaction'
    finally:
        layout.set_editor_property('edge_snap_enabled', previous_snap)


def multi_rack_checks():
    records = [r for r in rack_records() if len(goods_inside(*r[2])) >= 2]
    assert len(records) >= 2, 'Need two different real loaded rack groups'
    first = records[0]
    def centre(bounds):
        low, high = bounds
        return unreal.Vector(*[(low[i] + high[i]) * .5 for i in range(3)])
    second = min(records[1:], key=lambda r: (centre(r[2]) - centre(first[2])).length())
    first_actor, second_actor = first[0], second[0]
    upper = rack_records(1)
    assert upper, 'Need an upper-floor rack to verify floor selection exclusion'
    expected_groups = {group_key(first_actor), group_key(second_actor)}
    layout.clear_selection()
    layout.set_selection([first_actor, second_actor, first[1][-1], upper[0][0]])
    assert layout.get_editor_property('selection_count') == 2
    assert {group_key(a) for a in layout.get_selection()} == expected_groups, 'Selection did not deduplicate groups/exclude upper floor'
    moving = []
    for actor, frame, bounds in (first, second):
        moving += with_supported(frame + goods_inside(*bounds))
    moving = list(dict.fromkeys(moving))
    state = capture(moving)
    foreign = capture([a for a in live if isinstance(a, (unreal.WarehouseCargo, unreal.WarehousePallet)) and a not in moving])
    other_floors = capture([a for a in live if 'WarehouseFloor1' in tag_values(a) or 'WarehouseFloor2' in tag_values(a)])
    point = first_actor.get_actor_location()
    assert layout.begin_drag(first_actor, point)
    layout.update_drag(point + unreal.Vector(37, -23, 800))
    translated(state, unreal.Vector(40, -20, 0))
    disabled(state)
    translated(foreign, unreal.Vector())
    translated(other_floors, unreal.Vector())
    assert not layout.end_drag(False)
    translated(state, unreal.Vector())
    restored(state)
    assert {group_key(a) for a in layout.get_selection()} == expected_groups
    before = layout.get_editor_property('moved_count')
    delta = find_accepted(first_actor, point, state, [(-100, 0), (100, 0), (0, -100), (0, 100), (-200, 0), (200, 0)])
    assert layout.get_editor_property('moved_count') == before + 1, 'Multiselect did not commit as one transaction'
    translated(foreign, unreal.Vector())
    translated(other_floors, unreal.Vector())
    rollback_committed(first_actor, point, delta)
    translated(state, unreal.Vector())
    restored(state)
    reject_wall_overlap(first_actor, point, state)
    translated(state, unreal.Vector())
    restored(state)
    translated(foreign, unreal.Vector())
    translated(other_floors, unreal.Vector())
    print('LAYOUT_MULTI_RACK_ATOMIC_CONTENTS_PASSED', expected_groups, len(moving), 'members; unchanged foreign/gap cargo and upper floors')
    layout.clear_selection()
    return first, second


def project(point):
    pc = unreal.GameplayStatics.get_player_controller(game, 0)
    result = unreal.GameplayStatics.project_world_to_screen(pc, point, False)
    if isinstance(result, unreal.Vector2D):
        return result
    if isinstance(result, tuple):
        value = next((a for a in result if isinstance(a, unreal.Vector2D)), None)
        if value is not None and not any(a is False for a in result):
            return value
    if result is None or result is False or isinstance(result, tuple) and any(a is False for a in result):
        return None  # Native ProjectWorldLocationToScreen can reject points behind the camera.
    raise AssertionError(('Projection API failed', result))


def projected_bounds(bounds):
    low, high = bounds
    points = [p for i in range(8) if (p := project(unreal.Vector(high[0] if i & 1 else low[0],
        high[1] if i & 2 else low[1], high[2] if i & 4 else low[2]))) is not None]
    if not points:
        return None
    return (min(p.x for p in points), min(p.y for p in points), max(p.x for p in points), max(p.y for p in points))


def marquee_checks(pair):
    boxes = [projected_bounds(r[2]) for r in pair]
    assert all(box is not None for box in boxes), ('Selected rack pair has no visible projected corners',
        [r[0].get_actor_label() for r in pair],
        unreal.GameplayStatics.get_player_camera_manager(game, 0).get_camera_location(),
        unreal.GameplayStatics.get_player_camera_manager(game, 0).get_camera_rotation())
    rectangle = (min(b[0] for b in boxes) - 2, min(b[1] for b in boxes) - 2,
                 max(b[2] for b in boxes) + 2, max(b[3] for b in boxes) + 2)
    def intersects(a, b):
        return a[0] <= b[2] and a[2] >= b[0] and a[1] <= b[3] and a[3] >= b[1]
    expected = {group_key(r[0]) for r in rack_records()
                if (box := projected_bounds(r[2])) is not None and intersects(box, rectangle)}
    assert {group_key(r[0]) for r in pair}.issubset(expected)
    layout.clear_selection()
    assert layout.select_screen_rectangle(unreal.Vector2D(*rectangle[:2]), unreal.Vector2D(*rectangle[2:]), False)
    actual = {group_key(a) for a in layout.get_selection()}
    assert actual == expected, ('Marquee projection/group selection mismatch', actual, expected)
    assert layout.get_editor_property('selection_count') == len(actual), 'Marquee duplicated rack parts as groups'
    assert all('WarehouseFloor0' in tag_values(a) for a in layout.get_selection()), 'Marquee selected another floor'
    assert layout.select_screen_rectangle(unreal.Vector2D(*rectangle[:2]), unreal.Vector2D(*rectangle[2:]), True)
    assert {group_key(a) for a in layout.get_selection()} == expected
    assert layout.get_editor_property('selection_count') == len(expected), 'Additive repeat duplicated selections'
    outside = next((r[0] for r in rack_records() if group_key(r[0]) not in expected), None)
    if outside:
        layout.set_selection([outside])
        assert layout.select_screen_rectangle(unreal.Vector2D(*rectangle[:2]), unreal.Vector2D(*rectangle[2:]), True)
        assert {group_key(a) for a in layout.get_selection()} == expected | {group_key(outside)}, 'Additive marquee lost the existing outside selection'
    layout.set_category(unreal.WarehouseLayoutCategory.OTHER)
    assert not layout.get_selection(), 'Category switch retained invalid old selection'
    layout.set_category(unreal.WarehouseLayoutCategory.RACK)
    print('LAYOUT_MARQUEE_PROJECTED_GROUP_FLOOR_PASSED', len(expected), 'unique floor-0 groups; native rectangle API, not OS input')


def face_snap_checks():
    source = next(a for a in live if a.get_actor_label() == 'WH_InboundRack_00_A')
    members = group_members(source)
    assert not goods_inside(*box_of(members)), 'Snap fixture rack is expected to be empty'
    layout.clear_selection()
    layout.set_editor_property('edge_snap_enabled', True)
    # Chargers now start against this wall. A previously empty snap gap may
    # contain a real parked forklift; try both faces and require a native-clear
    # commit rather than weakening collision or moving the vehicle out of it.
    committed = False
    own_low, own_high = box_of(members)
    for peer, _, peer_bounds in rack_records():
        if group_key(peer)==group_key(source):
            continue
        peer_low, peer_high=peer_bounds
        if min(own_high[0],peer_high[0])-max(own_low[0],peer_low[0])<50:
            continue
        for side in (-1,1):
            state = capture(members)
            point = source.get_actor_location()
            offset=peer_low[1]-own_high[1]-10 if side<0 else peer_high[1]-own_low[1]+10
            assert layout.begin_drag(source, point)
            layout.update_drag(point + unreal.Vector(0,offset,500))
            actual_delta = source.get_actor_location() - point
            disabled(state)
            low,high = box_of(members)
            gap=peer_low[1]-high[1] if side<0 else low[1]-peer_high[1]
            hinted='랙 면에 맞춤' in str(layout.get_editor_property('status'))
            if not hinted or abs(gap-.1)>=.2:
                layout.end_drag(False)
                translated(state,unreal.Vector())
                restored(state)
                continue
            if not layout.end_drag(True):
                translated(state,unreal.Vector())
                restored(state)
                continue
            restored(state)
            rollback_committed(source, point, actual_delta)
            print('LAYOUT_PEER_RACK_FACE_SNAP_PASSED', peer.get_actor_label(), actual_delta, '0.1cm gap, native clear commit')
            committed=True
            break
        if committed:
            break
    assert committed,'No native-clear peer rack snap could be committed'

    # Try real cardinal walls while retaining the existing perpendicular
    # coordinate. No geometry or collision rules are changed for the test.
    walls = []
    for actor in live:
        if 'LayoutFixed' not in tag_values(actor) or not actor.get_actor_enable_collision():
            continue
        centre, extent = actor.get_actor_bounds(False)
        if min(extent.x, extent.y) <= 25 and max(extent.x, extent.y) >= 200 and extent.z >= 75 and centre.z - extent.z <= 40:
            walls.append(actor)
    assert walls, 'No physical warehouse wall for face snap test'
    committed = False
    for wall in walls:
        state = capture(members)
        point = source.get_actor_location()
        low, high = box_of(members)
        wall_centre, wall_extent = wall.get_actor_bounds(False)
        wall_low = [wall_centre.x - wall_extent.x, wall_centre.y - wall_extent.y]
        wall_high = [wall_centre.x + wall_extent.x, wall_centre.y + wall_extent.y]
        axis = 0 if wall_extent.x < wall_extent.y else 1
        across = 1 - axis
        if min(high[across], wall_high[across]) - max(low[across], wall_low[across]) <= 10:
            continue
        before_wall = (low[axis] + high[axis]) * .5 < (wall_low[axis] + wall_high[axis]) * .5
        raw_value = wall_low[axis] - high[axis] - 10 if before_wall else wall_high[axis] - low[axis] + 10
        raw = unreal.Vector(raw_value if axis == 0 else 0, raw_value if axis == 1 else 0, 0)
        assert layout.begin_drag(source, point)
        layout.update_drag(point + raw)
        final_low, final_high = box_of(members)
        gap = wall_low[axis] - final_high[axis] if before_wall else final_low[axis] - wall_high[axis]
        hinted = '벽 면에 맞춤' in str(layout.get_editor_property('status'))
        actual_delta = source.get_actor_location() - point
        if not hinted or abs(gap - .1) >= .2:
            layout.end_drag(False)
            translated(state, unreal.Vector())
            restored(state)
            continue
        if not layout.end_drag(True):
            translated(state, unreal.Vector())
            restored(state)
            continue
        restored(state)
        rollback_committed(source, point, actual_delta)
        print('LAYOUT_WALL_FACE_SNAP_PASSED', wall.get_actor_label(), actual_delta, '0.1cm gap, native clear commit')
        committed = True
        break
    assert committed, 'No native-clear wall snap could be committed'
    layout.clear_selection()
    layout.set_editor_property('edge_snap_enabled', False)


def finish(ok, detail):
    report.update(passed=ok, message=detail, wall_seconds=round(time.monotonic()-started, 2))
    output = Path(unreal.Paths.project_saved_dir()) / 'Testing/LayoutEditor'
    output.mkdir(parents=True, exist_ok=True)
    (output / ('PhysicalReport.json' if PHYSICAL_ONLY else 'Report.json')).write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    marker = 'LAYOUT_PHYSICAL_PIE_' if PHYSICAL_ONLY else 'LAYOUT_EDITOR_PIE_'
    print(marker + ('PASSED' if ok else 'FAILED'), detail)
    if layout and layout.get_editor_property('editing'):
        layout.set_editing(False, 0)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    global game, layout, live, phase, selected, snapshot, resume_at, truck_snapshot, selection_pair, selection_at, selection_camera
    global truck_controller, truck_cargo_identity
    try:
        assert time.monotonic() - started < 180, 'Layout transaction wall timeout'
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if phase == 0:
            if now < 3:
                return
            live = list(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor))
            for actor in live:
                if isinstance(actor, unreal.AgvTestVehicle):
                    nav = actor.get_editor_property('navigator')
                    drive = actor.get_editor_property('drive')
                    localizer = actor.get_editor_property('localizer')
                    print('LAYOUT_EXISTING_AGV_PROTOTYPE', actor.get_actor_label(), actor.get_class().get_path_name(),
                          'tick_enabled', actor.is_actor_tick_enabled(), 'editor_only', actor.get_editor_property('is_editor_only_actor'),
                          'navigating', nav.is_navigating(), 'nav_state', nav.get_editor_property('state'),
                          'sensors_sleep_when_parked', actor.get_editor_property('sleep_sensors_when_parked'),
                          'sensors_asleep', actor.get_editor_property('sensors_asleep'), 'speed_cm', drive.get_editor_property('speed_cm_s'),
                          'map_cells', localizer.get_editor_property('map_occupied_cells'),
                          'sensor_scans', [s.get_revolution_count() for s in (actor.get_editor_property(n) for n in ('top_lidar', 'scanner_l', 'scanner_r', 'fork_sensor_l', 'fork_sensor_r'))])
            # Fleet membership is now discovered dynamically. This geometry/
            # restart test needs idle vehicles; full dispatch is tested separately.
            for actor in live:
                if isinstance(actor,unreal.WarehouseInboundZone):actor.set_actor_tick_enabled(False)
            layouts = [a for a in live if isinstance(a, unreal.WarehouseLayoutEditor)]
            assert len(layouts) == 1, 'Missing/duplicate saved layout metadata actor'
            layout = layouts[0]
            layout.set_editor_property('edge_snap_enabled', False)  # Baseline exact 10cm tests; snap has separate checks below.
            assert len(layout.get_editor_property('zones')) == 2, 'Expected inbound and outbound layout zones'
            racks = [a for a in live if 'LayoutType:Rack' in tag_values(a) and 'WarehouseFloor0' in tag_values(a)]
            assert racks, 'No movable floor-0 rack groups'
            inbound = next(a for a in live if isinstance(a, unreal.WarehouseInboundZone))
            original_slots = list(inbound.get_editor_property('rack_slots'))
            referenced = {s.rack for s in original_slots}
            loaded = [(a, group_members(a), box_of(group_members(a))) for a in racks]
            loaded.sort(key=lambda r: not any(a in referenced for a in r[1]))
            selected, members, bounds = next((r for r in loaded if len(goods_inside(*r[2])) >= 2), (None, None, None))
            assert selected, 'No loaded rack for native group-movement check'
            members = with_supported(members + goods_inside(*bounds))
            snapshot = capture(members)
            others = capture([a for a in live if 'WarehouseFloor1' in tag_values(a) or 'WarehouseFloor2' in tag_values(a)])
            layout.set_editing(True, 0)
            assert unreal.GameplayStatics.is_game_paused(game)
            wheel_floor_checks()
            if '-LayoutWheelContactOnly' in unreal.SystemLibrary.get_command_line():
                finish(True, 'Saved rolling-wheel floor contact transactions')
                return
            layout.set_category(unreal.WarehouseLayoutCategory.RACK)
            point = selected.get_actor_location()
            assert layout.begin_drag(selected, point)
            layout.update_drag(point + unreal.Vector(37, -23, 800))
            translated(snapshot, unreal.Vector(40, -20, 0))
            translated(others, unreal.Vector())
            disabled(snapshot)
            assert not layout.end_drag(False)
            translated(snapshot, unreal.Vector())
            restored(snapshot)
            print('LAYOUT_RACK_CONTENTS_CANCEL_SNAP_PASSED', len(snapshot))

            slots = [(i, s.pose.translation) for i, s in enumerate(inbound.get_editor_property('rack_slots')) if s.rack in members]
            temporary_slot = not slots
            if temporary_slot:
                # Existing loaded rack rows are real geometry but may have no
                # inbound slot registration. Add only unsaved fixture metadata,
                # using a supported pallet's real pose; do not alter any cargo.
                supported_pallet = next(a for a in members if isinstance(a, unreal.WarehousePallet))
                fixture = unreal.WarehouseRackSlot(name='LAYOUT-TEST-TEMPORARY-SLOT',
                    rack=selected, pose=supported_pallet.get_actor_transform(),
                    clear_height_cm=110, capacity_kg=2000)
                inbound.set_editor_property('rack_slots', original_slots + [fixture])
                slots = [(len(original_slots), fixture.pose.translation)]
                print('LAYOUT_UNSAVED_RACK_SLOT_FIXTURE', selected.get_actor_label(), fixture.pose)
            delta = find_accepted(selected, point, snapshot, [(0, -150), (0, 150), (-150, 0), (150, 0), (0, -300), (0, 300), (-300, -300), (300, 300)])
            current = inbound.get_editor_property('rack_slots')
            for i, old in slots:
                assert (current[i].pose.translation - old - delta).length() < .1, 'Rack slot reference did not move'
            rollback_committed(selected, point, delta)
            translated(snapshot, unreal.Vector())
            rotation_roundtrip([selected], members, group_members(selected))
            if temporary_slot:
                inbound.set_editor_property('rack_slots', original_slots)
            print('LAYOUT_RACK_COMMIT_REFERENCES_PASSED', delta)

            reject_wall_overlap(selected, point, snapshot)
            translated(snapshot, unreal.Vector())
            restored(snapshot)
            print('LAYOUT_OVERLAP_ROLLBACK_PASSED')
            phase = 1
            return

        if phase == 1:
            layout.set_category(unreal.WarehouseLayoutCategory.ZONE)
            computer = next(a for a in live if isinstance(a, unreal.WarehouseComputer))
            shipping = next(a for a in live if isinstance(a, unreal.WarehouseShippingSystem))
            for index, zone in enumerate(list(layout.get_editor_property('zones'))):
                low = [zone.centre.x - zone.half_size.x, zone.centre.y - zone.half_size.y, zone.centre.z - zone.half_size.z]
                high = [zone.centre.x + zone.half_size.x, zone.centre.y + zone.half_size.y, zone.centre.z + zone.half_size.z]
                vehicles = [a for a in live if isinstance(a, unreal.WarehouseForklift)
                            and low[0] <= a.get_actor_location().x <= high[0]
                            and low[1] <= a.get_actor_location().y <= high[1] and a.get_actor_location().z < 398]
                members = with_supported(list(zone.members) + goods_inside(low, high) + vehicles)
                assert any(isinstance(a, unreal.WarehousePallet) for a in members), ('No zone pallet contents', zone.name)
                anchors = [a for a in zone.members if a.get_actor_label() == 'WH_ShippingZone_' + zone.name]
                assert len(anchors) == 1 and anchors[0] in members, ('Shipping anchor missing from its moving zone group', zone.name)
                anchor = anchors[0]
                assert all(not unreal.WarehouseDamageSystem.is_physical_contact(p) for p in anchor.get_components_by_class(unreal.PrimitiveComponent)), ('Logical shipping anchor became a physical object', zone.name)
                assert any(z.anchor == anchor for z in shipping.get_editor_property('zones')), ('Shipping lost the zone anchor reference', zone.name)
                anchor_start = anchor.get_actor_location()
                snapshot = capture(members)
                value = computer.get_editor_property('outbound_centre')
                centre = unreal.Vector(value.x, value.y, value.z)
                goals = [unreal.Vector(p.translation.x,p.translation.y,p.translation.z) for p in computer.get_editor_property('outbound_destinations')]
                point = zone.centre
                delta = find_accepted(None, point, snapshot, [(-80, 0), (80, 0), (0, -80), (0, 80), (-150, 0), (150, 0)])
                assert (layout.get_editor_property('zones')[index].centre - point - delta).length() < .1
                assert anchor in layout.get_editor_property('zones')[index].members
                assert (anchor.get_actor_location() - anchor_start - delta).length() < .1, ('Logical shipping anchor stayed behind during a valid zone move', zone.name)
                if zone.name == 'Outbound':
                    assert (computer.get_editor_property('outbound_centre') - centre - delta).length() < .1, ('Computer outbound centre stale',computer.get_editor_property('outbound_centre'),centre,delta)
                    for old, new in zip(goals, computer.get_editor_property('outbound_destinations')):
                        assert (new.translation - old - delta).length() < .1, 'Computer outbound destination stale'
                rollback_committed(None, point, delta)
                assert (anchor.get_actor_location() - anchor_start).length() < .1, ('Shipping anchor did not return with its zone', zone.name)
                assert all(not unreal.WarehouseDamageSystem.is_physical_contact(p) for p in anchor.get_components_by_class(unreal.PrimitiveComponent))
                print('LAYOUT_NONPHYSICAL_SHIPPING_ANCHOR_PASSED', zone.name, anchor.get_actor_label())
                print('LAYOUT_ZONE_CONTENTS_CENTRE_GOALS_PASSED', zone.name, len(members))
                rotation_roundtrip([zone.members[0]], members, zone_names=[zone.name])
            phase = 2
            return

        if phase == 2:
            charger_checks()
            layout.set_category(unreal.WarehouseLayoutCategory.FORKLIFT)
            selected = next(a for a in live if isinstance(a, unreal.WarehouseForklift) and -2 < a.get_actor_location().z < 398)
            snapshot = capture([selected])
            point = selected.get_actor_location()
            assert layout.begin_drag(selected, point)
            layout.update_drag(point + unreal.Vector(0, 20, 400))
            translated(snapshot, unreal.Vector(0, 20, 0))
            disabled(snapshot)
            assert not layout.end_drag(False)
            restored(snapshot)
            translated(snapshot, unreal.Vector())
            anchors = [p.translation for p in selected.get_editor_property('navigation_anchors')]
            delta = find_accepted(selected, point, snapshot, [(0, 30), (0, -30), (-50, 0), (50, 0), (0, 100)])
            assert len(selected.get_editor_property('navigation_anchors')) >= len(anchors), 'Layout lost navigation anchor data'
            rollback_committed(selected, point, delta)
            restored(snapshot)
            print('LAYOUT_AGV_PARTS_JOINT_RESTORE_PASSED', len(selected.get_components_by_class(unreal.PhysicsConstraintComponent)))
            rotation_roundtrip([selected], [selected])
            phase = 3
            return

        if phase == 3:
            layout.set_category(unreal.WarehouseLayoutCategory.ELEVATOR)
            elevator = next(a for a in live if isinstance(a, unreal.WarehouseElevator))
            assert elevator.get_editor_property('state') == unreal.WarehouseElevatorState.IDLE
            members = group_members(elevator)
            snapshot = capture(members)
            unrelated_goods = capture([a for a in live if isinstance(a,(unreal.WarehousePallet,unreal.WarehouseCargo)) and a not in members])
            point = elevator.get_actor_location()
            portals = [(elevator.waiting_pose(f).translation, elevator.boarding_pose(f).translation) for f in range(3)]
            assert layout.begin_drag(elevator, point)
            layout.update_drag(point + unreal.Vector(30, 20, 800))
            translated(snapshot, unreal.Vector(30, 20, 0))
            translated(unrelated_goods, unreal.Vector())
            disabled(snapshot)
            accepted = layout.end_drag(True)
            if accepted:
                restored(snapshot)
                rollback_committed(elevator, point, unreal.Vector(30, 20, 0))
            else:
                translated(snapshot, unreal.Vector())
                restored(snapshot)
            print('LAYOUT_ELEVATOR_ALL_LEVELS_PASSED', 'clear commit' if accepted else 'unsafe geometry rejected', len(members))
            # Moving the shaft must also move its supported opening on all
            # floors. A preview-only cancellation cannot satisfy this feature.
            # The saved art has real wider door rails and separate call stands.
            # Probe nearby grid positions as well as the former 2.5/5m offsets;
            # every candidate must pass the unchanged native slab/support and
            # full physical overlap transaction. Never clear surrounding art.
            delta = find_accepted(elevator, point, snapshot, [
                (0, -50), (0, 50), (-50, 0), (50, 0),
                (0, -100), (0, 100), (-100, 0), (100, 0),
                (0, -150), (0, 150), (-150, 0), (150, 0),
                (0, -250), (0, 250), (-250, 0), (250, 0),
                (0, -300), (0, -350), (0, -400), (0, -500)])
            translated(unrelated_goods, unreal.Vector())
            for floor, (waiting, boarding) in enumerate(portals):
                assert (elevator.waiting_pose(floor).translation - waiting - delta).length() < .1, ('Common elevator waiting position did not move on floor', floor)
                assert (elevator.boarding_pose(floor).translation - boarding - delta).length() < .1, ('Common elevator boarding position did not move on floor', floor)
                assert abs(elevator.floor_height(floor) - floor * 400) < .1, ('Elevator changed floor height', floor)
            print('LAYOUT_ELEVATOR_SAFE_RELOCATION_PASSED', delta)
            rollback_committed(elevator, point, delta)
            translated(snapshot, unreal.Vector())
            restored(snapshot)
            rotation_roundtrip([elevator], members)
            phase = 4
            return

        if phase == 4:
            layout.set_category(unreal.WarehouseLayoutCategory.OTHER)
            pallet = next(a for a in live if isinstance(a, unreal.WarehousePallet) and a.get_actor_label() == 'WH_InboundPallet_Empty')
            state = capture(with_supported([pallet]))
            point = pallet.get_actor_location()
            delta = find_accepted(pallet, point, state, [(0, 50), (0, -50), (-50, 0), (50, 0), (0, 100)])
            rollback_committed(pallet, point, delta)
            print('LAYOUT_OTHER_PALLET_PASSED')

            truck = next(a for a in live if a.get_actor_label() == 'EXT_ParkedTruck_02')
            members = group_members(truck)
            cartons = [a for a in members if isinstance(a, unreal.WarehouseCargo)]
            assert len(cartons) == 48, ('Truck group omitted its original cartons', len(cartons))
            assert all('LayoutGroup:TruckDock02' in tag_values(a) for a in members)
            truck_snapshot = capture(members)
            other_trucks = capture([a for a in live if 'LayoutGroup:TruckDock01' in tag_values(a)
                                    or 'LayoutGroup:TruckDock03' in tag_values(a)])
            truck_controller = next(a for a in live if isinstance(a, unreal.WarehouseLogisticsTruck) and truck in a.get_editor_property('truck_parts'))
            truck_cargo_identity = truck_cargo_contract(truck_controller, cartons)
            bridge = next(a for a in live if a.get_actor_label() == 'TRK_DockLeveller')
            probe_carton = next(a for a in cartons if a.get_actor_label() == 'TRK_Cargo_000')
            truck_overlap_diagnostic('before_drag', truck_controller, probe_carton, bridge)
            point = truck.get_actor_location()
            assert layout.begin_drag(truck, point), str(layout.get_editor_property('status'))
            layout.update_drag(point + unreal.Vector(0, -10, 400))
            translated(truck_snapshot, unreal.Vector(0, -10, 0))
            disabled(truck_snapshot)
            assert not layout.end_drag(False)
            translated(truck_snapshot, unreal.Vector())
            restored(truck_snapshot)
            bridge = next(a for a in live if a.get_actor_label() == 'TRK_DockLeveller')
            assert 'LogisticsFixedDock' in tag_values(bridge) and bridge not in members
            bridge_before = capture([bridge])
            bridge_centre, bridge_extent = bridge.get_actor_bounds(False)
            solid_bounds = [unreal.SystemLibrary.get_component_bounds(p) for a in members
                            for p in a.get_components_by_class(unreal.PrimitiveComponent)
                            if unreal.WarehouseDamageSystem.is_physical_contact(p)]
            # The bridge now belongs to the fixed dock. The previous small
            # offsets can intersect it, so also try separating their real
            # physical bounds longitudinally, without bypassing collision.
            south = math.floor((bridge_centre.y - bridge_extent.y - max(c.y + e.y for c, e, r in solid_bounds) - 20) / 10) * 10
            north = math.ceil((bridge_centre.y + bridge_extent.y - min(c.y - e.y for c, e, r in solid_bounds) + 20) / 10) * 10
            print('LAYOUT_FIXED_DOCK_CLEARANCE_CANDIDATES', 'south', south, 'north', north, 'fixed bridge', bridge_centre, bridge_extent)
            delta = find_accepted(truck, point, truck_snapshot, [(0, -100), (0, -50), (-20, -50), (20, -50), (0, south), (0, north)])
            translated(bridge_before, unreal.Vector())
            restored(bridge_before)
            translated(other_trucks, unreal.Vector())
            restored(other_trucks)
            probe_part = probe_carton.get_component_by_class(unreal.StaticMeshComponent)
            truck_overlap_diagnostic('before_return', truck_controller, probe_carton, bridge,
                truck_snapshot[probe_carton]['components'][probe_part.get_name()]['pose'])
            rollback_committed(truck, point, delta)
            translated(truck_snapshot, unreal.Vector())
            restored(truck_snapshot)
            truck_cargo_contract(truck_controller, cartons, truck_cargo_identity)
            print('LAYOUT_TRUCK_INTERIOR_CARGO_COMMIT_RETURN_PASSED', len(members), 'group members, 48 original cartons; manifested cinematic bodies frozen, nontransit bodies dynamic; mass/identity retained; delta', delta)
            layout.set_category(unreal.WarehouseLayoutCategory.RACK)
            selection_pair = multi_rack_checks()
            multi_members = []
            multi_frames = []
            for seed, frame, bounds in selection_pair:
                multi_frames += frame
                multi_members += with_supported(frame+goods_inside(*bounds))
            rotation_roundtrip([r[0] for r in selection_pair], multi_members, multi_frames, collision_rollback=True)
            all_category_checks()
            layout.set_category(unreal.WarehouseLayoutCategory.RACK)
            if not PHYSICAL_ONLY:
                pc = unreal.GameplayStatics.get_player_controller(game, 0)
                selection_camera = next(a for a in live if a.get_actor_label() == 'LayoutSelectionTestCamera')
                pc.set_view_target_with_blend(selection_camera, 0.)
                selection_at = time.monotonic()
            phase = 41
            return

        if phase == 41:
            if not PHYSICAL_ONLY and time.monotonic() - selection_at < .2:
                return  # Project through a real refreshed camera frame, including while paused.
            if PHYSICAL_ONLY:
                print('LAYOUT_RENDERED_UI_EXCLUDED', report['excludedChecks'])
            else:
                marquee_checks(selection_pair)
            face_snap_checks()
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            pc.set_view_target_with_blend(unreal.GameplayStatics.get_player_pawn(game, 0), 0.)
            layout.set_editing(False, 0)
            assert not unreal.GameplayStatics.is_game_paused(game), 'Editing exit left the simulation paused'
            selected.toggle_power()
            resume_at = now
            phase = 5
            return

        if phase == 5:
            if now - resume_at < 2:
                return
            assert selected.get_editor_property('ai_state') == unreal.WarehouseAIState.READY, selected.get_editor_property('status')
            assert selected.root_component.is_simulating_physics(), 'AGV body simulation was lost'
            restored(snapshot)  # Idle elevator attachment/physics survives real world ticks.
            restored(truck_snapshot)  # Restore the exact dynamic or cinematic body state after real resume ticks.
            truck_cargo_contract(truck_controller, list(truck_cargo_identity), truck_cargo_identity)
            for part in selected.get_components_by_class(unreal.StaticMeshComponent):
                assert part.get_world_location().z > -15, ('AGV part fell through floor', part.get_name())
            print('LAYOUT_UNPAUSE_AGV_RESTART_PASSED')
            scope = ('screen-projected marquee and cursor projection excluded; ' if PHYSICAL_ONLY
                     else 'projected marquee/floor scope, ')
            finish(True, 'Saved single/multiple racks and contents, native rigid 90-degree rotations and inverse/reference/physics/atomic rollback, All mixed selection/move/filter, ' + scope + 'rack/wall face snap, zones, charger/parked rig references and remote/away exclusion, collision/floor rollback, physical AGV rig, common 3-floor elevator, truck cargo and simulation resume')
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
