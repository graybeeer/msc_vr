"""One-pallet freight lift: native contact trips and optional saved installation.

Default: an unsaved Entry fixture, real Chaos carrier/gates and independent
pallet/carton bodies. After PIE starts only public requests, interlock inputs
and an external force/torque-driven test conveyor are used; no pose, velocity, state,
attachment or completion counter is assigned. This does not test WMS routing.
-PalletLiftSavedMap checks installed classes/assets/dimensions without PIE.
-PalletLiftLayout checks saved equipment editing and three-floor openings.
The reusable test host may instead pass mode pallet-lift-saved or
pallet-lift-layout in Saved/Testing/InboundWrapRequest.json.
-KeepTestEditorOpen leaves the host available. Neither mode saves the map.
"""
import json
import math
from pathlib import Path
import time
import traceback
import unreal


command = unreal.SystemLibrary.get_command_line().lower()
request_path = Path(unreal.Paths.project_saved_dir()) / 'Testing/InboundWrapRequest.json'
mode = json.loads(request_path.read_text(encoding='utf-8-sig')).get('mode', '') if request_path.exists() else ''
mode = globals().get('PALLET_LIFT_MODE', mode)
if '-palletliftlayout' in command:
    mode = 'pallet-lift-layout'
elif '-palletliftsavedmap' in command or '-savedmap' in command:
    mode = 'pallet-lift-saved'
saved_map = mode == 'pallet-lift-saved'
layout_mode = mode == 'pallet-lift-layout'
keep_open = '-keeptesteditoropen' in command
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
out = Path(unreal.Paths.project_saved_dir()) / 'Testing/PalletLift'
out.mkdir(parents=True, exist_ok=True)
report = {'passed': False, 'mode': 'saved-layout' if layout_mode else 'saved-installation' if saved_map else 'native-contact',
          'checks': [], 'samples': [], 'renderedUI': False, 'wmsRoutingTested': False,
          'ride_drift': {'reference': 'each accepted pallet pose; horizontal distance at each ride tick',
                         'samples': 0, 'mean_cm': 0, 'max_cm': 0}}
started = time.monotonic()
handle = None
game = live = lift = pallet = cargo = platform = strength = None
phase = 'start'
at = last_sample = last_diagnostic = 0
trip_index = 0
trips = [(0, 1), (1, 2), (2, 0)]
conveyor_integral = unreal.Vector()
conveyor_yaw_integral = 0
settled_at = None
carrier_mass = None
gate_masses = []
layout_editor = layout_original = layout_generic = layout_readded = None
ride_origin = None
ride_drift_sum = 0
late_start = None
empty_late_started = changed_late_started = False
payload_layout_checked = False


def check(name, **data):
    report['checks'].append(dict(name=name, **data))
    print('PALLET_LIFT_CHECK', name, json.dumps(data, ensure_ascii=False))


def finish(passed, detail):
    report.update(passed=passed, detail=detail, wall_seconds=round(time.monotonic()-started, 3))
    (out / ('LayoutReport.json' if layout_mode else 'SavedMapReport.json' if saved_map else 'NativeReport.json')).write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PALLET_LIFT_LAYOUT_PASSED' if passed and layout_mode else 'PALLET_LIFT_SAVED_PASSED' if passed and saved_map else
          'PALLET_LIFT_NATIVE_PASSED' if passed else 'PALLET_LIFT_FAILED', detail)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    if layout_editor and layout_editor.get_editor_property('editing'):
        layout_editor.set_editing(False, 0)
    if editor.get_game_world():
        level.editor_request_end_play()
    if not keep_open:
        unreal.SystemLibrary.quit_editor()


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def bounds(part):
    centre, extent = unreal.SystemLibrary.get_component_bounds(part)[:2]
    return centre-extent, centre+extent


def allof(cls):
    return unreal.GameplayStatics.get_all_actors_of_class(game, cls)


def assert_payload():
    for actor, mass in ((pallet, 25), (cargo, 12)):
        part = body(actor)
        assert part.is_simulating_physics() and part.is_gravity_enabled(), 'Payload physics/gravity disabled'
        assert part.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS, 'Payload lost physical collision'
        assert abs(part.get_mass()-mass) < .1, ('Payload mass changed', actor.get_actor_label(), part.get_mass())
        assert not actor.get_attach_parent_actor(), 'Payload was attached to the carrier'
    assert platform.is_simulating_physics(), 'Carrier stopped being a native body'
    assert platform.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
    assert abs(platform.get_mass()-carrier_mass) < .1
    for gate, mass in zip(lift.get_editor_property('gates')[:lift.get_editor_property('floor_count')*2], gate_masses):
        assert gate.is_simulating_physics() and abs(gate.get_mass()-mass) < .1
        assert gate.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS


def assert_carrier_footprint(actor):
    # Match production PalletLiftLocalBounds: transform every corner of the
    # native physical world AABB, rather than measuring a render-box centre.
    physical = unreal.WarehouseDamageSystem.get_physical_bounds(actor)
    assert all(getattr(physical.min, k) < getattr(physical.max, k) for k in 'xyz'), 'Payload lacks a native physical bounds box'
    pose = lift.get_actor_transform()
    points = [unreal.MathLibrary.inverse_transform_location(pose, unreal.Vector(x, y, z))
              for x in (physical.min.x, physical.max.x)
              for y in (physical.min.y, physical.max.y)
              for z in (physical.min.z, physical.max.z)]
    low = [min(getattr(p, k) for p in points) for k in 'xy']
    high = [max(getattr(p, k) for p in points) for k in 'xy']
    assert all(a >= -62.6 and b <= 62.6 for a, b in zip(low, high)), (
        'Payload physical footprint left the 125cm carrier', actor.get_actor_label(), low, high)


def payload_diagnostic(reason):
    def vector(value):
        return [round(getattr(value, axis), 4) for axis in 'xyz']
    data = {'reason': reason, 'phase': phase, 'status': lift.get_editor_property('status'),
            'supported': [a.get_actor_label() for a in strength.get_supported_actors(pallet)], 'payload': [],
            'conveyor_command': report.get('conveyor_last_command')}
    for actor in (pallet, cargo):
        physical = unreal.WarehouseDamageSystem.get_physical_bounds(actor)
        points = [unreal.MathLibrary.inverse_transform_location(lift.get_actor_transform(), unreal.Vector(x, y, z))
                  for x in (physical.min.x, physical.max.x) for y in (physical.min.y, physical.max.y) for z in (physical.min.z, physical.max.z)]
        rotation = actor.get_actor_rotation()
        data['payload'].append({'actor': actor.get_actor_label(), 'position': vector(actor.get_actor_location()),
            'rotation': {axis: round(getattr(rotation, axis), 4) for axis in ('pitch', 'yaw', 'roll')},
            'physical_world_min': vector(physical.min), 'physical_world_max': vector(physical.max),
            'physical_lift_min': [round(min(getattr(p, axis) for p in points), 4) for axis in 'xyz'],
            'physical_lift_max': [round(max(getattr(p, axis) for p in points), 4) for axis in 'xyz'],
            'linear_cm_s': vector(body(actor).get_physics_linear_velocity()),
            'angular_deg_s': vector(body(actor).get_physics_angular_velocity_in_degrees())})
    report.setdefault('payload_diagnostics', []).append(data)
    print('PALLET_LIFT_PAYLOAD_DIAGNOSTIC', json.dumps(data, ensure_ascii=False))


def conveyor(goal_x, dt):
    """External test conveyor: finite force and yaw guide on the bottom pallet."""
    global conveyor_integral, conveyor_yaw_integral
    position = pallet.get_actor_location()
    velocity = body(pallet).get_physics_linear_velocity()
    error = unreal.Vector(goal_x-position.x, -position.y, 0)
    desired = unreal.Vector(max(-20, min(20, error.x*.6)), max(-8, min(8, error.y*.6)), 0)
    speed_error = desired-unreal.Vector(velocity.x, velocity.y, 0)
    step = min(dt, .05)
    candidate = conveyor_integral+speed_error*step*100
    # Bound accumulated force, but keep integrating both axes so a saturated
    # guide can change direction and overcome stiction near the target.
    if candidate.length() > 2000:
        candidate *= 2000/candidate.length()
    conveyor_integral = candidate
    if error.length() < .5:
        conveyor_integral *= max(0, 1-step*4)
    acceleration = speed_error*10+conveyor_integral
    if acceleration.length() > 2500:
        acceleration *= 2500/acceleration.length()
    # Finite powered handler: at 25kg at most 625N, or 125W at 0.2m/s,
    # applied only to the bottom pallet. Payload contacts remain physical.
    force = acceleration*body(pallet).get_mass()
    body(pallet).wake_all_rigid_bodies()
    body(pallet).add_force(force)
    # Finite conveyor side guides keep the wooden pallet at its authored yaw.
    # 80Nm is at most a 145.5N guide reaction at a 0.55m edge; no pose/velocity
    # is assigned. UE torque uses kg*cm^2/s^2, so one Nm is 10000 UE units.
    yaw_error = (-pallet.get_actor_rotation().yaw+180) % 360-180
    angular = body(pallet).get_physics_angular_velocity_in_degrees()
    desired_yaw_speed = math.radians(max(-10, min(10, yaw_error*1.5)))
    yaw_speed_error = desired_yaw_speed-math.radians(angular.z)
    yaw_candidate = max(-75, min(75, conveyor_yaw_integral+yaw_speed_error*step*120))
    proposed_torque = yaw_speed_error*35+yaw_candidate
    if abs(proposed_torque) <= 80 or proposed_torque*yaw_speed_error <= 0:
        conveyor_yaw_integral = yaw_candidate
    if abs(yaw_error) < .25:
        conveyor_yaw_integral *= max(0, 1-step*4)
    torque_nm = max(-80, min(80, yaw_speed_error*35+conveyor_yaw_integral))
    body(pallet).wake_all_rigid_bodies()
    body(pallet).add_torque_in_radians(unreal.Vector(0, 0, torque_nm*10000))
    report['conveyor_last_command'] = {'goal_x_cm': goal_x, 'force_n': [round(getattr(force, k)/100, 4) for k in 'xyz'],
        'integral_cm_s2': [round(getattr(conveyor_integral, k), 4) for k in 'xyz'], 'torque_nm': round(torque_nm, 4),
        'yaw_integral_nm': round(conveyor_yaw_integral, 4), 'body_awake': body(pallet).is_any_rigid_body_awake()}
    report['conveyor_limits'] = {'force_n': 625, 'yaw_target_degrees': 0, 'torque_nm': 80, 'yaw_speed_degrees_s': 10}
    return error.length() < 2 and velocity.length() < 2 and abs(yaw_error) < .75 and angular.length() < 1


def saved_installation():
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    named = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    installed = named['MZ_PalletCargoLift']
    generic = named['MZ_AGVElevator']
    assert isinstance(installed, unreal.WarehousePalletLift) and not isinstance(installed, unreal.WarehouseElevator)
    assert isinstance(generic, unreal.WarehouseElevator) and not isinstance(generic, unreal.WarehousePalletLift)
    assert str(installed.get_type_label()) and str(installed.get_interaction_text(0))
    assert installed.actor_has_tag('LayoutType:Elevator')
    assert [round(installed.floor_height(f)-installed.get_actor_location().z) for f in range(3)] == [40, 440, 840]
    assert [round(generic.floor_height(f)-generic.get_actor_location().z) for f in range(3)] == [0, 400, 800]
    for vehicle in actors.get_all_level_actors():
        if isinstance(vehicle, unreal.WarehouseForklift):
            assert vehicle.get_editor_property('elevator') == generic, 'Existing AGV was assigned to a pallet-only lift'
    check('saved_separate_freight_and_agv_equipment', freight_label=installed.get_actor_label(), generic_label=generic.get_actor_label())
    expected = {'frame': 'SM_PalletLift_Frame', 'platform': 'SM_PalletLift_Carrier'}
    for field, name in expected.items():
        part = installed.get_editor_property(field)
        assert part.static_mesh and part.static_mesh.get_name() == name, ('Installed model missing', field)
        assert part.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
    assert len(installed.get_editor_property('gates')) >= 6 and installed.get_editor_property('floor_count') == 3
    assert all(g.static_mesh and g.static_mesh.get_name() == 'SM_PalletLift_Gate' for g in installed.get_editor_property('gates'))
    mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    carrier_hulls = mesh_editor.get_convex_collision_count(installed.get_editor_property('platform').static_mesh)
    gate_hulls = mesh_editor.get_convex_collision_count(installed.get_editor_property('gates')[0].static_mesh)
    assert carrier_hulls >= 17 and gate_hulls >= 13, ('Authored moving-part UCX collision was lost', carrier_hulls, gate_hulls)
    check('saved_separate_roller_and_gate_convex_shapes', carrier_hulls=carrier_hulls, gate_hulls=gate_hulls)
    low, high = bounds(installed.get_editor_property('frame'))
    size = high-low
    assert abs(size.x-180) < 1 and abs(size.y-180) < 1 and 1100 <= size.z <= 1121, ('Freight frame dimensions changed', size)
    assert not installed.get_editor_property('frame').is_simulating_physics()
    check('saved_real_model_dimensions_and_collision', frame_size_cm=[size.x, size.y, size.z], gates=6)
    finish(True, 'Saved separate equipment, generic AGV references, installed assets, frame dimensions and colliders; no PIE or rendered/UI claim')


def fixture():
    assert level.load_level('/Engine/Maps/Entry')
    for actor in list(actors.get_all_level_actors()):
        if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
            actors.destroy_actor(actor)
    world = editor.get_editor_world()
    unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property('default_game_mode', unreal.GameModeBase)
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')

    def spawn(cls, name, location):
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(*location))
        actor.set_actor_label(name)
        return actor

    def block(name, location, size):
        actor = spawn(unreal.StaticMeshActor, name, location)
        actor.static_mesh_component.set_static_mesh(cube)
        actor.static_mesh_component.set_collision_profile_name('BlockAll')
        actor.set_actor_scale3d(unreal.Vector(*[v/100 for v in size]))
        actor.tags = ['WarehouseMapStructure', 'LayoutFixed']
        return actor

    def carton(name, location, size, mass):
        actor = spawn(unreal.WarehouseCargo, name, location)
        actor.set_cargo_mesh(cube)
        actor.set_actor_scale3d(unreal.Vector(*[v/100 for v in size]))
        actor.set_gross_mass_kg(mass)
        return actor

    block('PalletLiftFixtureFloor', (5500, 0, -10), (14000, 3000, 20))
    # Use the same fixed world/Chaos steps as the actual warehouse, even though
    # this isolated fixture deliberately uses GameModeBase instead of a player.
    spawn(unreal.load_class(None, '/Script/msc_vr.WarehouseSimulationClock'), 'PalletLiftClock', (-1000,-1200,0))
    spawn(unreal.PlayerStart, 'PalletLiftPlayerStart', (-1000, -1200, 100))
    # Fixed receiving rollers stop 3 cm short of the moving carrier, leaving
    # clearance for native convex sweep margins. Nothing
    # underneath the payload bridges the carrier while it travels vertically.
    for floor in range(3):
        # Use real roller beds at the same nominal top, with a rounded leading
        # contact instead of forcing worn wooden feet across a square table edge.
        for index in range(3):
            table = spawn(unreal.StaticMeshActor, f'PalletLiftLanding{floor}_{index}', (-128-index*125, 0, floor*400+40))
            table.static_mesh_component.set_static_mesh(unreal.load_asset('/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_Carrier'))
            table.static_mesh_component.set_collision_profile_name('BlockAll')
            table.tags = ['WarehouseMapStructure', 'LayoutFixed']
    parcels = []
    for prefix, x in (('Main', 0), ('Two', 1000), ('Overload', 2000), ('Oversize', 3000)):
        spawn(unreal.WarehousePalletLift, prefix+'Lift', (x, 0, 0))
        item = spawn(unreal.WarehousePallet, prefix+'Pallet', (x, 0, 40.15))
        parcels.append((item, 25))
        if prefix == 'Two':
            parcels.append((spawn(unreal.WarehousePallet, 'SecondPallet', (x, 0, 55.4)), 25))
        else:
            size = (140, 80, 20) if prefix == 'Oversize' else (30, 24, 20)
            mass = 2100 if prefix == 'Overload' else 12
            parcels.append((carton(prefix+'Cargo', (x, 0, 65.4), size, mass), mass))
    spawn(unreal.WarehousePalletLift, 'PersonLift', (4000, 0, 0))
    human = spawn(unreal.Character, 'CarrierPerson', (4000, 0, 136))
    human.get_component_by_class(unreal.CapsuleComponent).set_collision_profile_name('Pawn')
    spawn(unreal.WarehousePalletLift, 'VehicleLift', (5000, 0, 0))
    vehicle = spawn(unreal.WarehouseForklift, 'CarrierVehicle', (5000, 0, 40.15))
    vehicle.set_editor_property('auto_start', False)
    vehicle.set_editor_property('pending_jobs', [])
    for index, flag in enumerate(('power_available', 'e_stop_released', 'door_sensors_healthy', 'level_sensor_healthy')):
        bad = spawn(unreal.WarehousePalletLift, 'InterlockLift'+str(index), (6000+index*1000, 0, 0))
        bad.set_editor_property(flag, False)
    # The overhead supports and boxes are outside the cabin's 259.8cm top.
    # Removing only these fixed stands after acceptance creates real late loads.
    for prefix, x in (('EmptyLate', 10000), ('ChangedLate', 11000)):
        spawn(unreal.WarehousePalletLift, prefix+'Lift', (x, 0, 0))
        block(prefix+'Stand', (x, 0, 263), (30, 24, 4))
        parcels.append((carton(prefix+'Box', (x, 0, 275.15), (30, 24, 20), 12), 12))
        if prefix == 'ChangedLate':
            parcels.append((spawn(unreal.WarehousePallet, prefix+'Pallet', (x, 0, 40.15)), 25))
            parcels.append((carton(prefix+'Cargo', (x, 0, 65.4), (30, 24, 20), 12), 12))
    generic = spawn(unreal.WarehouseElevator, 'UnchangedGenericLift', (-1000, 800, 0))
    vehicle.set_editor_property('elevator', generic)
    spawn(unreal.WarehouseLayoutEditor, 'AcceptedCargoLayoutGuard', (-1000, -1400, 0))
    damage = spawn(unreal.WarehouseDamageSystem, 'PalletLiftStrength', (-1000, -1000, 0))
    damage.set_editor_property('objects', [unreal.WarehouseStrength(name=a.get_actor_label(), members=[a],
        mass_kg=mass, rated_load_kg=100000, impact_yield_j=100000, impact_failure_j=1000000) for a, mass in parcels])


def layout_fixture():
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.WarehouseForklift):
            actor.set_editor_property('auto_start', False)
    # A native drag stages the existing assembly here for a positive quarter
    # turn even if the production wall/landing does not permit that turn.
    for floor in range(3):
        pad = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(8000, 0, floor*400-10))
        pad.set_actor_label('PalletLiftLayoutFloor'+str(floor))
        pad.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
        pad.static_mesh_component.set_collision_profile_name('BlockAll')
        pad.set_actor_scale3d(unreal.Vector(80, 80, .2))
        pad.tags = ['WarehouseMapStructure', 'LayoutFixed', 'ElevatorLayoutFloor']


def layout_snapshot(actor):
    parts = []
    for part in actor.get_components_by_class(unreal.SceneComponent):
        physical = isinstance(part, unreal.PrimitiveComponent)
        simulated = physical and part.is_simulating_physics()
        parts.append(dict(part=part, pose=part.get_world_transform(), parent=part.get_attach_parent(),
            mobility=part.get_editor_property('mobility'), collision=part.get_collision_enabled() if physical else None,
            simulated=simulated, mass=part.get_mass() if simulated else None,
            linear=part.get_physics_linear_velocity() if simulated else None,
            angular=part.get_physics_angular_velocity_in_degrees() if simulated else None))
    return dict(actor=actor, pose=actor.get_actor_transform(), collision=actor.get_actor_enable_collision(), parts=parts)


def assert_layout_state(saved, delta=None, pivot=None, yaw=0, velocities=False):
    delta = delta or unreal.Vector()

    def expected(pose):
        local = unreal.MathLibrary.make_relative_transform(pose, unreal.Transform(location=pivot)) if pivot is not None else pose
        return unreal.MathLibrary.compose_transforms(local, unreal.Transform(location=(pivot or unreal.Vector())+delta, rotation=unreal.Rotator(yaw=yaw)))

    actor = saved['actor']
    assert unreal.MathLibrary.nearly_equal_transform_transform(actor.get_actor_transform(), expected(saved['pose']), .15, .05, .001), 'Layout actor transform changed unexpectedly'
    assert actor.get_actor_enable_collision() == saved['collision']
    for entry in saved['parts']:
        part = entry['part']
        assert unreal.MathLibrary.nearly_equal_transform_transform(part.get_world_transform(), expected(entry['pose']), .15, .05, .001), ('Layout component transform changed', part.get_name())
        assert part.get_attach_parent() == entry['parent'] and part.get_editor_property('mobility') == entry['mobility']
        if entry['collision'] is not None:
            assert part.get_collision_enabled() == entry['collision'] and part.is_simulating_physics() == entry['simulated']
        if entry['mass'] is not None:
            assert abs(part.get_mass()-entry['mass']) < .01
            if velocities:
                assert (part.get_physics_linear_velocity()-entry['linear']).length() < .02
                assert (part.get_physics_angular_velocity_in_degrees()-entry['angular']).length() < .02


def trace_layout_floors(pose, opened):
    ignore = list(allof(unreal.WarehouseElevator))+list(allof(unreal.WarehousePalletLift))
    for floor in range(3):
        for x, y in ((0, 0), (-40, -40), (40, 40)):
            point = unreal.MathLibrary.transform_location(pose, unreal.Vector(x, y, floor*400))
            hit = unreal.SystemLibrary.line_trace_single(game, point+unreal.Vector(0, 0, 1), point-unreal.Vector(0, 0, 1),
                unreal.TraceTypeQuery.ECC_VISIBILITY, False, ignore, unreal.DrawDebugTrace.NONE, True)
            native = hit if isinstance(hit, unreal.HitResult) else next((v for v in hit if isinstance(v, unreal.HitResult)), None) if hit else None
            data = native.to_tuple() if native else None
            blocked = bool(data and data[0])
            assert blocked != opened, ('Wrong three-floor opening/fill collision', floor, x, y, opened, data)
            if blocked:
                assert unreal.WarehouseDamageSystem.is_physical_contact(data[10]), 'Fill has query-only collision'


def layout_drag(actor, delta, commit=True, floor=0):
    location = actor.get_actor_location()
    point = unreal.Vector(location.x, location.y, floor*400)
    positions = {p: p.get_world_location() for p in actor.get_components_by_class(unreal.SceneComponent)}
    assert layout_editor.begin_drag(actor, point), layout_editor.get_editor_property('status')
    layout_editor.update_drag(point+delta)
    actual = actor.get_actor_location()-location
    assert abs(actual.z) < .1, 'Same-floor drag changed shaft elevation'
    for part, position in positions.items():
        assert (part.get_world_location()-position-actual).length() < .15, ('Not all floor components followed drag', part.get_name())
    assert not actor.get_actor_enable_collision(), 'Dragged lift still collides'
    assert not actor.get_editor_property('platform').is_simulating_physics(), 'Dragged carrier still simulates'
    return layout_editor.end_drag(commit)


def layout_checks():
    global layout_editor, layout_original, layout_generic, layout_readded, at, phase
    lift = next(a for a in allof(unreal.WarehousePalletLift) if a.get_actor_label() == 'MZ_PalletCargoLift')
    generic = next(a for a in allof(unreal.WarehouseElevator) if a.get_actor_label() == 'MZ_AGVElevator')
    layout_editor = allof(unreal.WarehouseLayoutEditor)[0]
    layout_editor.set_editing(True, 0)
    layout_editor.set_category(unreal.WarehouseLayoutCategory.ELEVATOR)
    layout_editor.set_action(unreal.WarehouseLayoutAction.MOVE)
    layout_editor.set_editor_property('edge_snap_enabled', False)  # Exact translations; snapping has separate generic regressions.
    layout_original, layout_generic = layout_snapshot(lift), layout_snapshot(generic)
    for floor in range(3):
        layout_editor.set_editing(True, floor)
        layout_editor.set_selection([lift])
        assert layout_editor.get_editor_property('selection_count') == 1
        assert layout_drag(lift, unreal.Vector(20, 10, 800), False, floor=floor) is False
        assert_layout_state(layout_original, velocities=True)
        assert [round(lift.floor_height(f)) for f in range(3)] == [40, 440, 840]
    check('layout_selection_and_cancel_on_all_three_floors')
    layout_editor.set_editing(True, 0)
    layout_editor.set_selection([lift])
    for delta in (unreal.Vector(0, 20, 0), unreal.Vector(0, -20, 0), unreal.Vector(20, 0, 0), unreal.Vector(-20, 0, 0), unreal.Vector(0, 50, 0), unreal.Vector(0, -50, 0)):
        if layout_drag(lift, delta):
            assert_layout_state(layout_original, delta)
            trace_layout_floors(lift.get_actor_transform(), True)
            assert layout_drag(lift, -delta), layout_editor.get_editor_property('status')
            assert_layout_state(layout_original)
            break
        assert_layout_state(layout_original, velocities=True)
    else:
        raise AssertionError(('No clear small relocation', layout_editor.get_editor_property('status')))
    check('layout_small_native_commit_and_return')
    source_pose = lift.get_actor_transform()
    delta = unreal.Vector(8000-source_pose.translation.x, -source_pose.translation.y, 0)
    assert layout_drag(lift, delta), layout_editor.get_editor_property('status')
    delta = lift.get_actor_location()-source_pose.translation
    assert_layout_state(layout_original, delta)
    trace_layout_floors(source_pose, False)
    trace_layout_floors(lift.get_actor_transform(), True)
    staged = layout_snapshot(lift)
    boxes = [unreal.SystemLibrary.get_component_bounds(p)[:2] for p in lift.get_components_by_class(unreal.PrimitiveComponent)
             if unreal.WarehouseDamageSystem.is_physical_contact(p)]
    low = [min(getattr(c, k)-getattr(e, k) for c, e in boxes) for k in 'xyz']
    high = [max(getattr(c, k)+getattr(e, k) for c, e in boxes) for k in 'xyz']
    pivot = unreal.Vector(*[(a+b)*.5 for a, b in zip(low, high)])
    assert layout_editor.rotate_selection(90), layout_editor.get_editor_property('status')
    assert_layout_state(staged, pivot=pivot, yaw=90)
    trace_layout_floors(lift.get_actor_transform(), True)
    rotated = layout_snapshot(lift)
    assert not layout_drag(lift, generic.get_actor_location()-lift.get_actor_location()), 'Overlapping generic shaft was accepted'
    assert_layout_state(rotated, velocities=True)
    assert_layout_state(layout_generic)
    trace_layout_floors(lift.get_actor_transform(), True)
    trace_layout_floors(generic.get_actor_transform(), True)
    check('layout_native_rotation_all_parts_physics_and_atomic_reject', parts=len(rotated['parts']))
    removed_pose = lift.get_actor_transform()
    original_frame_mesh = lift.get_editor_property('frame').static_mesh
    layout_editor.set_editing(True, 2)
    layout_editor.set_selection([lift])
    assert layout_editor.delete_selection(), layout_editor.get_editor_property('status')
    assert lift not in allof(unreal.WarehousePalletLift)
    trace_layout_floors(removed_pose, False)
    assert_layout_state(layout_generic)
    check('layout_delete_fills_all_three_floors_and_retains_generic')
    layout_editor.set_action(unreal.WarehouseLayoutAction.ADD)
    layout_editor.set_category(unreal.WarehouseLayoutCategory.ELEVATOR)
    for _ in range(100):
        if layout_editor.get_add_choice_label() == '화물용 엘리베이터':
            break
        layout_editor.cycle_add_choice()
    else:
        raise AssertionError('Pallet-only prototype missing from the elevator catalogue')
    before = set(allof(unreal.WarehousePalletLift))
    point = unreal.Vector(8000, 0, 800)
    assert layout_editor.begin_add(point), layout_editor.get_editor_property('status')
    layout_editor.update_drag(point)
    assert layout_editor.end_drag(), layout_editor.get_editor_property('status')
    layout_readded = next(a for a in allof(unreal.WarehousePalletLift) if a not in before)
    assert [round(layout_readded.floor_height(f)) for f in range(3)] == [40, 440, 840]
    trace_layout_floors(layout_readded.get_actor_transform(), True)
    assert_layout_state(layout_generic)
    assert layout_readded.get_editor_property('frame').static_mesh == original_frame_mesh
    assert len(layout_readded.get_editor_property('gates')) == 8 and layout_readded.get_editor_property('floor_count') == 3
    check('layout_pallet_only_prototype_readded_from_third_floor')
    layout_editor.set_editing(False, 2)
    at, phase = unreal.GameplayStatics.get_time_seconds(game), 'layout_resume'


def layout_tick(dt):
    global game, phase
    try:
        assert time.monotonic()-started < 180, ('Pallet lift layout timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'layout_settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        if phase == 'layout_settle':
            if unreal.GameplayStatics.get_time_seconds(game) < 4:
                return
            selected = next(a for a in allof(unreal.WarehousePalletLift) if a.get_actor_label() == 'MZ_PalletCargoLift')
            assert selected.is_idle_for_layout(), selected.get_editor_property('status')
            layout_checks()
        elif phase == 'layout_resume':
            if unreal.GameplayStatics.get_time_seconds(game)-at < 3:
                return
            assert layout_readded.is_idle_for_layout(), layout_readded.get_editor_property('status')
            deck = layout_readded.get_editor_property('platform')
            assert deck.is_simulating_physics() and abs(deck.get_mass()-120) < .1
            assert abs(bounds(deck)[1].z-40) < 1 and deck.get_physics_linear_velocity().length() < 2
            assert all(g.is_simulating_physics() and abs(g.get_mass()-50) < .1 for g in layout_readded.get_editor_property('gates')[:6])
            assert_layout_state(layout_generic)
            trace_layout_floors(layout_readded.get_actor_transform(), True)
            check('layout_readded_native_lock_and_gates_after_real_ticks')
            finish(True, 'Saved freight lift all-floor selection, small move, rigid quarter turn, physical/collision/velocity rollback, three-floor holes/fill, native delete/re-add and simulation resume; generic equipment unchanged; no OS/UI interaction claim')
    except Exception:
        finish(False, traceback.format_exc())


def rejected_cases():
    reasons = {'Two': 'ONLY ONE PALLET ALLOWED', 'Overload': 'PALLET LIFT OVERLOAD',
               'Oversize': 'LOAD OUTSIDE 125 CM CARRIER / 220 CM HEIGHT'}
    for prefix, reason in reasons.items():
        candidate = live[prefix+'Lift']
        candidate_pallet = live[prefix+'Pallet']
        assert candidate.get_editor_property('state') == unreal.WarehousePalletLiftState.IDLE, ('Negative fixture not ready', prefix, candidate.get_editor_property('status'))
        assert abs(bounds(body(candidate_pallet))[0].z-bounds(candidate.get_editor_property('platform'))[1].z) < 1
        assert not candidate.request_transfer(live[prefix+'Pallet'], 0, 1), ('Invalid load accepted', prefix)
        assert candidate.get_editor_property('status') == reason, ('Rejected for an unrelated fixture error', prefix, candidate.get_editor_property('status'))
        assert candidate.get_editor_property('completed_transfers') == 0
        check('reject_'+prefix.lower(), status=candidate.get_editor_property('status'))
    for prefix in ('Person', 'Vehicle'):
        candidate = live[prefix+'Lift']
        occupant = live['Carrier'+prefix]
        occupied = unreal.WarehouseDamageSystem.get_physical_bounds(occupant)
        x = candidate.get_actor_location().x
        assert occupied.min.x < x+62.3 and occupied.max.x > x-62.3 and occupied.min.y < 62.3 and occupied.max.y > -62.3
        assert occupied.max.z > 40.2 and occupied.min.z < 259.8, ('Occupant left the physical cabin', prefix, occupied)
        assert not candidate.call_to_floor(1), ('Occupied cabin was called as empty', prefix)
        assert candidate.get_editor_property('completed_transfers') == 0
        check('reject_cabin_'+prefix.lower(), status=candidate.get_editor_property('status'))
    flags = (('power', 'POWER LOST - BRAKE HOLD'), ('estop', 'E-STOP - BRAKE HOLD'),
             ('door_sensor', 'DOOR SENSOR FAULT'), ('level_sensor', 'LEVEL SENSOR FAULT'))
    for index, (flag, reason) in enumerate(flags):
        candidate = live['InterlockLift'+str(index)]
        assert not candidate.call_to_floor(1), ('Failed interlock accepted a call', flag)
        assert candidate.get_editor_property('status') == reason, ('Wrong failed interlock reason', flag, candidate.get_editor_property('status'))
        assert candidate.get_editor_property('completed_transfers') == 0
        check('reject_'+flag, status=candidate.get_editor_property('status'))
    assert not lift.request_transfer(None, 0, 1)
    assert not lift.request_transfer(pallet, -1, 1)
    assert not lift.request_transfer(pallet, 0, 3)
    assert not lift.request_transfer(pallet, 0, 0)
    generic = live['UnchangedGenericLift']
    assert not isinstance(lift, unreal.WarehouseElevator)
    assert live['CarrierVehicle'].get_editor_property('elevator') == generic
    assert abs(generic.floor_height(2)-800) < .1
    check('separate_generic_and_invalid_requests')


def late_cases(now):
    global empty_late_started, changed_late_started
    if late_start is None:
        return
    if not empty_late_started:
        candidate = live['EmptyLateLift']
        assert bounds(body(live['EmptyLateBox']))[0].z > 259.8, 'Late empty-call box already entered cabin'
        assert candidate.call_to_floor(1), candidate.get_editor_property('status')
        live['EmptyLateStand'].destroy_actor()
        empty_late_started = True
        check('empty_call_accepted_before_physical_late_drop', time=round(now, 3))
    if not changed_late_started:
        candidate = live['ChangedLateLift']
        assert bounds(body(live['ChangedLateBox']))[0].z > 259.8, 'Late accepted-load box already entered cabin'
        accepted = candidate.request_transfer(live['ChangedLatePallet'], 0, 1)
        if not accepted:
            assert candidate.get_editor_property('status') == 'WAIT FOR LOAD TO SETTLE' and now-late_start < 20, candidate.get_editor_property('status')
        else:
            assert abs(candidate.get_editor_property('payload_mass_kg')-37) < .2
            live['ChangedLateStand'].destroy_actor()
            changed_late_started = True
            check('load_accepted_before_physical_extra_drop', time=round(now, 3))
    for name, mass in (('EmptyLateBox', 12), ('ChangedLateBox', 12), ('ChangedLatePallet', 25), ('ChangedLateCargo', 12)):
        actor = live[name]
        part = body(actor)
        assert part.is_simulating_physics() and part.is_gravity_enabled() and not actor.get_attach_parent_actor()
        assert part.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS and abs(part.get_mass()-mass) < .1


def assert_late_cases():
    assert empty_late_started and changed_late_started, 'Late-load fixtures were not accepted before dropping cargo'
    for prefix, reason in (('EmptyLate', 'UNREQUESTED LOAD IN EMPTY CARRIER - REMOVE LOAD'),
                           ('ChangedLate', 'ACCEPTED LOAD CHANGED / RECONFIRM PALLET')):
        candidate = live[prefix+'Lift']
        actual = body(live[prefix+'Box'])
        assert bounds(actual)[0].z < 259.8, 'Late box never physically fell into cabin'
        assert candidate.get_editor_property('status') == reason, (prefix, candidate.get_editor_property('status'))
        assert candidate.get_editor_property('completed_transfers') == 0 and candidate.get_editor_property('current_floor') == 0
        deck = candidate.get_editor_property('platform')
        assert abs(bounds(deck)[1].z-40) < 1 and deck.get_physics_linear_velocity().length() < 2
        check('late_load_held_'+prefix.lower(), status=reason, box_bottom_cm=round(bounds(actual)[0].z, 3), deck_top_cm=round(bounds(deck)[1].z, 3))


def accepted_payload_layout_guard():
    global layout_editor, payload_layout_checked
    guard = live['AcceptedCargoLayoutGuard']
    layout_editor = guard  # Also allows finish() to unpause after an assertion.
    assert not unreal.GameplayStatics.is_game_paused(game)
    assert lift.get_editor_property('accepted_pallet') == pallet
    assert lift.is_handling_cargo(pallet) and lift.is_handling_cargo(cargo)
    expected_status = '화물용 엘리베이터가 운반 중인 물류는 하역을 완료한 뒤 편집하세요.'
    snapshots = [layout_snapshot(actor) for actor in (lift, pallet, cargo)]
    completed = lift.get_editor_property('completed_transfers')
    guard.set_editing(True, 0)
    try:
        assert unreal.GameplayStatics.is_game_paused(game)
        store = allof(unreal.WarehouseLayoutInventory)[0]
        archived = [(str(row.cargo_id), row.mass_kg) for row in store.get_editor_property('held_cargo')]
        for category, selection in ((unreal.WarehouseLayoutCategory.CARGO, [cargo]),
                                    (unreal.WarehouseLayoutCategory.PALLET, [pallet]),
                                    (unreal.WarehouseLayoutCategory.ALL, [cargo, pallet])):
            guard.set_category(category)
            guard.set_action(unreal.WarehouseLayoutAction.MOVE)
            guard.set_selection(selection)
            assert guard.get_editor_property('category') == category
            assert guard.get_editor_property('selection_count') == len(selection), 'Accepted cargo was not genuinely selected'
            assert set(guard.get_selection()) == set(selection)
            assert not guard.begin_drag(selection[0], selection[0].get_actor_location()), 'Accepted lift cargo could begin a drag'
            assert guard.get_editor_property('status') == expected_status
            assert not guard.get_editor_property('dragging')
            for saved in snapshots:
                assert_layout_state(saved, velocities=True)
            guard.set_action(unreal.WarehouseLayoutAction.DELETE)
            assert not guard.delete_selection(), 'Accepted lift cargo was deleted'
            assert guard.get_editor_property('status') == expected_status
            for saved in snapshots:
                assert_layout_state(saved, velocities=True)
            assert pallet in allof(unreal.WarehousePallet) and cargo in allof(unreal.WarehouseCargo)
            assert [(str(row.cargo_id), row.mass_kg) for row in store.get_editor_property('held_cargo')] == archived
            assert lift.get_editor_property('accepted_pallet') == pallet and lift.get_editor_property('completed_transfers') == completed
            check('accepted_payload_layout_move_delete_rejected', category=str(category), selection=len(selection))
    finally:
        guard.set_editing(False, 0)
    assert not unreal.GameplayStatics.is_game_paused(game)
    payload_layout_checked = True
    check('accepted_payload_layout_guard_resumed_native_simulation')


def tick(dt):
    global game, live, lift, pallet, cargo, platform, strength, phase, at, last_sample, last_diagnostic
    global trip_index, conveyor_integral, settled_at, carrier_mass, gate_masses
    global conveyor_yaw_integral
    global ride_origin, ride_drift_sum
    global late_start
    try:
        assert time.monotonic()-started < 600, ('Native freight lift timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live = {a.get_actor_label(): a for a in allof(unreal.Actor)}
            lift, pallet, cargo = [live[name] for name in ('MainLift', 'MainPallet', 'MainCargo')]
            platform = lift.get_editor_property('platform')
            strength = live['PalletLiftStrength']
            carrier_mass = platform.get_mass()
            gate_masses = [g.get_mass() for g in lift.get_editor_property('gates')[:lift.get_editor_property('floor_count')*2]]
            steel = platform.get_editor_property('body_instance').get_editor_property('phys_material_override')
            assert steel, 'Production carrier contact material is missing'
            friction = steel.get_editor_property('friction')
            static_friction = steel.get_editor_property('static_friction')
            assert abs(friction-.35) < .00001 and abs(static_friction-.4025) < .00001
            landings = [body(a) for name, a in live.items() if name.startswith('PalletLiftLanding')]
            assert len(landings) == 9
            for landing in landings:
                landing.set_phys_material_override(steel)
                actual = landing.get_editor_property('body_instance').get_editor_property('phys_material_override')
                assert actual == steel and actual.get_editor_property('friction') == friction and actual.get_editor_property('static_friction') == static_friction
            check('receiving_rollers_share_production_steel_contact', rollers=len(landings), friction=friction, static_friction=static_friction)
        if phase == 'settle':
            if now < 4:
                return
            assert abs(bounds(platform)[1].z-40) < 1
            assert cargo in strength.get_supported_actors(pallet), 'Fixture carton lacks actual pallet support'
            assert abs(bounds(body(pallet))[0].z-bounds(platform)[1].z) < 1
            rejected_cases()
            check('native_initial_support_and_lock', carrier_mass_kg=carrier_mass)
            phase, at = 'request', now
        assert_payload()
        late_cases(now)
        state = lift.get_editor_property('state')
        assert state != unreal.WarehousePalletLiftState.FAULT, lift.get_editor_property('status')
        if now-last_sample >= .5:
            last_sample = now
            report['samples'].append(dict(time=round(now, 3), phase=phase, state=str(state),
                floor=lift.get_editor_property('current_floor'), deck_top=round(bounds(platform)[1].z, 3),
                pallet=[round(getattr(pallet.get_actor_location(), k), 3) for k in 'xyz'],
                cargo=[round(getattr(cargo.get_actor_location(), k), 3) for k in 'xyz'],
                cargo_speed_cm_s=round(body(cargo).get_physics_linear_velocity().length(), 3),
                conveyor_command=report.get('conveyor_last_command'),
                pallet_centre_offset_cm=round((unreal.Vector(pallet.get_actor_location().x, pallet.get_actor_location().y, 0)-
                    unreal.Vector(lift.get_actor_location().x, lift.get_actor_location().y, 0)).length(), 3),
                deck_speed=round(platform.get_physics_linear_velocity().length(), 3),
                completed=lift.get_editor_property('completed_transfers')))
        if now-last_diagnostic >= 2:
            last_diagnostic = now
            print('PALLET_LIFT_NATIVE_SAMPLE', json.dumps(report['samples'][-1], ensure_ascii=False),
                  'status', lift.get_editor_property('status'), 'pallet_speed', body(pallet).get_physics_linear_velocity())
        if phase == 'request':
            source, destination = trips[trip_index]
            assert lift.get_editor_property('current_floor') == source
            accepted = lift.request_transfer(pallet, source, destination) if trip_index == 0 else lift.try_interact(source)
            if not accepted and lift.get_editor_property('status') == 'WAIT FOR LOAD TO SETTLE' and now-at < 15:
                return
            if not accepted:
                payload_diagnostic('transfer request rejected')
            assert accepted, lift.get_editor_property('status')
            assert lift.get_editor_property('accepted_pallet') == pallet
            assert abs(lift.get_editor_property('payload_mass_kg')-37) < .2, 'Payload mass must include actual pallet and carton'
            assert not lift.request_transfer(live['TwoPallet'], source, destination), 'Busy carrier accepted a second pallet'
            if not payload_layout_checked:
                accepted_payload_layout_guard()
            ride_origin = pallet.get_actor_location()
            if late_start is None:
                late_start = now
                late_cases(now)
            at, phase = now, 'ride'
            return
        if phase == 'ride':
            assert now-at < 100, ('Lift did not arrive', state, lift.get_editor_property('status'))
            assert lift.get_editor_property('completed_transfers') == trip_index, 'Transfer completed before physical unloading'
            for actor in (pallet, cargo):
                assert_carrier_footprint(actor)
            offset = pallet.get_actor_location()-ride_origin
            drift = unreal.Vector(offset.x, offset.y, 0).length()
            ride_drift_sum += drift
            stats = report['ride_drift']
            stats['samples'] += 1
            stats['mean_cm'] = round(ride_drift_sum/stats['samples'], 4)
            stats['max_cm'] = max(stats['max_cm'], round(drift, 4))
            assert cargo in strength.get_supported_actors(pallet), 'Carton lost pallet support during lift motion'
            assert abs(bounds(body(pallet))[0].z-bounds(platform)[1].z) < 2, 'Pallet lost physical carrier support'
            if state != unreal.WarehousePalletLiftState.AWAIT_UNLOAD:
                return
            destination = trips[trip_index][1]
            assert lift.get_editor_property('current_floor') == destination
            assert abs(bounds(platform)[1].z-lift.floor_height(destination)) < 1
            assert platform.get_physics_linear_velocity().length() < 2
            check('loaded_arrival_waits_for_actual_unload', source=trips[trip_index][0], destination=destination)
            at, phase, settled_at = now, 'unload', None
            conveyor_integral = unreal.Vector()
            conveyor_yaw_integral = 0
            return
        if phase == 'unload':
            assert now-at < 55, ('Force conveyor could not remove pallet', pallet.get_actor_location(), lift.get_editor_property('status'))
            ready = conveyor(-180, dt)
            if not ready:
                settled_at = None
                return
            if settled_at is None:
                settled_at = now
            if now-settled_at < .5:
                return
            assert bounds(body(pallet))[1].x < bounds(platform)[0].x-1, 'Pallet still overlaps carrier'
            assert abs(bounds(body(pallet))[0].z-lift.floor_height(trips[trip_index][1])) < 2
            assert cargo in strength.get_supported_actors(pallet)
            assert lift.get_editor_property('completed_transfers') == trip_index+1
            assert not lift.get_editor_property('accepted_pallet')
            check('native_force_unload_completion', floor=trips[trip_index][1], completed=trip_index+1)
            trip_index += 1
            if trip_index == len(trips):
                assert payload_layout_checked
                assert_late_cases()
                at, phase = now, 'empty_call'
                assert lift.try_interact(1), lift.get_editor_property('status')
                return
            at, phase, settled_at = now, 'reload', None
            conveyor_integral = unreal.Vector()
            conveyor_yaw_integral = 0
            return
        if phase == 'reload':
            assert now-at < 55, ('Force conveyor could not reload pallet', pallet.get_actor_location())
            ready = conveyor(0, dt)
            if not ready:
                settled_at = None
                return
            if settled_at is None:
                settled_at = now
            if now-settled_at >= 1:
                assert abs(bounds(body(pallet))[0].z-bounds(platform)[1].z) < 1
                assert cargo in strength.get_supported_actors(pallet)
                phase = 'request'
            return
        if phase == 'empty_call':
            assert now-at < 60, 'Empty carrier call timed out'
            assert lift.get_editor_property('completed_transfers') == 3, 'Empty call was counted as a loaded transfer'
            if lift.get_editor_property('current_floor') != 1 or state != unreal.WarehousePalletLiftState.IDLE:
                return
            assert abs(bounds(platform)[1].z-440) < 1
            assert abs(bounds(body(pallet))[0].z-40) < 2, 'Unloaded pallet followed empty carrier'
            for candidate in allof(unreal.WarehousePalletLift):
                if candidate != lift:
                    assert candidate.get_editor_property('completed_transfers') == 0
                    assert abs(bounds(candidate.get_editor_property('platform'))[1].z-40) < 1, 'A rejected fixture still moved'
            check('empty_call_leaves_unloaded_pallet_and_counter_unchanged')
            finish(True, 'Native 0->1->2->0 pallet/carton contact trips, public loaded/empty interaction, finite force/torque unload/reload, single-pallet/size/mass/person/vehicle/interlock rejection, physical late cargo holds on empty and accepted-load calls, separate generic equipment; no WMS, OS input or rendered UI claim')
    except Exception:
        if lift and pallet and cargo and strength:
            try:
                payload_diagnostic('native test failed')
            except Exception as diagnostic_error:
                report['diagnostic_error'] = str(diagnostic_error)
        finish(False, traceback.format_exc())


try:
    if saved_map:
        saved_installation()
    elif layout_mode:
        layout_fixture()
        handle = unreal.register_slate_post_tick_callback(layout_tick)
    else:
        fixture()
        handle = unreal.register_slate_post_tick_callback(tick)
except Exception:
    finish(False, traceback.format_exc())
