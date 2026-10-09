"""Isolated, asynchronous Chaos contact checks. Never saves assets or a map.

Use a separate editor process. Engine Entry avoids production scene contacts.
Fixtures are placed before PIE; after BeginPlay only commands, constraints and
physical impulses act on them. No assigned pose, velocity or fake physics tick.
The prototype uses explicit tyre shear; the main AGV uses native wheel joints.
"""
import os
import math
import re
import sys
import time
import traceback
import unreal

sys.path.insert(0, os.path.dirname(__file__))
from apply_real_world_scale import fit

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
editor_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = unreal.GameplayStatics.get_all_actors_of_class(editor_world, unreal.WorldSettings)[0]
settings.set_editor_property('default_game_mode', unreal.load_class(None,
    '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
player_class = unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
carton_mesh = unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1')
assert carton_mesh, 'Prepare intact carton collision before running this test'
fixtures = []


def spawn(cls, label, location, yaw=0):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*location), unreal.Rotator(yaw=yaw))
    assert actor, label
    actor.set_actor_label(label)
    actor.set_editor_property('is_spatially_loaded', False)
    return actor


def carton(label, center, size, mass):
    actor = spawn(unreal.WarehouseCargo, label, center)
    actor.set_cargo_mesh(carton_mesh)
    fit(actor, size, center)
    actor.set_gross_mass_kg(mass)
    fixtures.append((actor, mass, unreal.WarehouseFailure.CRUSH))
    return actor


def pallet(label, location):
    actor = spawn(unreal.WarehousePallet, label, location)
    fixtures.append((actor, 25, unreal.WarehouseFailure.CRUSH))
    return actor


def worker(label, location):
    actor = spawn(unreal.WarehouseWorker, label, location)
    assert actor.copy_player_appearance(player_class)
    fixtures.append((actor, 80, unreal.WarehouseFailure.RIGID))
    return actor


def prototype(label, location):
    actor = spawn(unreal.AgvTestVehicle, label, location)
    fixtures.append((actor, 1000, unreal.WarehouseFailure.MACHINE))
    return actor


floor = spawn(unreal.StaticMeshActor, 'ContactFloor', (10000, 10000, -10))
floor.static_mesh_component.set_static_mesh(cube)
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.set_actor_scale3d(unreal.Vector(50, 50, .2))
spawn(unreal.PlayerStart, 'ContactPlayerStart', (11000, 9600, 125))

# Horizontal momentum is measured while both parcels remain above the floor.
carton('MomentumA', (10200, 11000, 600), (20, 20, 20), 5)
carton('MomentumB', (10240, 11000, 600), (20, 20, 20), 7)

# Dynamic support: an unanchored 2,000 kg deck starts 800 cm above the floor.
# Its gravity is disabled in PIE to isolate the tangential motor reaction; it
# still receives the vehicle's real normal contact and opposite tyre force.
deck = spawn(unreal.StaticMeshActor, 'ReactionDeck', (11000, 11300, 800))
deck.static_mesh_component.set_static_mesh(cube)
deck.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
deck.static_mesh_component.set_collision_profile_name('PhysicsActor')
deck.set_actor_scale3d(unreal.Vector(11, 11, .4))
deck.static_mesh_component.set_mass_override_in_kg(unreal.Name('None'), 2000)
deck.static_mesh_component.set_simulate_physics(True)
fixtures.append((deck, 2000, unreal.WarehouseFailure.RIGID))
prototype('ReactionAGV', (11000, 11300, 821))

prototype('LoadedAGV', (9000, 9200, 1))
carton('ForkLoad', (9060, 9200, 30.7), (50, 75, 40), 12)
prototype('ImpactAGV', (9400, 10100, 1))
main = spawn(unreal.WarehouseForklift, 'MainAGV', (9700, 10100, 1))
main.set_editor_property('auto_start', False)
fixtures.append((main, 1000, unreal.WarehouseFailure.MACHINE))
pallet('VehiclePallet', (9700, 9990, .1))

carton('PalletStriker', (10000, 10300, 15.1), (30, 30, 30), 5)
pallet('ContactPallet', (10080, 10300, .1))
worker('ContactWorker', (10100, 10600, 125))
carton('HumanStriker', (10000, 10600, 40.1), (40, 40, 80), 10)
worker('HumanA', (10400, 10600, 125))
worker('HumanB', (10500, 10600, 125))
worker('TipWorker', (11500, 10300, 125))
carton('HeldCarton', (11075, 9600, 15.1), (30, 30, 30), 12)
carton('HeldStriker', (11200, 9450, 20.1), (40, 40, 40), 8)
carton('DropCarton', (11500, 9800, 850), (35, 30, 25), 9)

strength = spawn(unreal.WarehouseDamageSystem, 'ContactStrength', (0, 0, 0))
specs = []
for actor, mass, failure in fixtures:
    mesh = actor.get_component_by_class(unreal.StaticMeshComponent)
    specs.append(unreal.WarehouseStrength(name=actor.get_actor_label(), members=[actor],
        physics_meshes=[mesh.static_mesh if mesh else None], mass_kg=mass,
        rated_load_kg=1000000, failure=failure, impact_yield_j=100000,
        impact_failure_j=1000000, overload_seconds=100))
strength.set_editor_property('objects', specs)

started = time.monotonic()
phase = 0
at = 0
last_now = 0
named = None
pawn = None
initial = {}
peaks = {}
prototype_controls = {}
held_before = None
tip_min_up = 1


def body(actor):
    root = actor.root_component
    if isinstance(root, unreal.PrimitiveComponent):
        return root
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def position(label):
    return body(named[label]).get_center_of_mass()


def velocity(label):
    return body(named[label]).get_physics_linear_velocity()


def impulse(label, vector):
    body(named[label]).add_impulse(unreal.Vector(*vector), unreal.Name('None'), False)


def observe(*labels):
    for label in labels:
        peaks[label] = max(peaks.get(label, 0), velocity(label).length())


def assert_live(label, mass=None):
    part = body(named[label])
    assert part.is_simulating_physics(), (label, 'Physical body stopped simulating')
    assert part.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS, (label, 'Contact collision disabled')
    assert not named[label].get_attach_parent_actor(), (label, 'Externally attached instead of physical contact')
    if mass is not None:
        assert abs(part.get_mass() - mass) < .1, (label, part.get_mass(), mass)


def above_floor(label):
    actor = named[label]
    bounds = unreal.WarehouseDamageSystem.get_physical_bounds(actor)
    assert bounds.is_valid, (label, 'Missing native collision bounds')
    if isinstance(actor.root_component, unreal.SkeletalMeshComponent):
        # Chaos rotates each body's local AABB, which overestimates a rotated
        # capsule. Test its actual native collision shapes against the region
        # below the unchanged -0.8 cm floor penetration allowance instead.
        mesh = actor.root_component
        # ECollisionChannel retains its native ECC_ prefix in this UE Python build.
        assert mesh.get_collision_object_type() == unreal.CollisionChannel.ECC_PAWN
        pawn_type = getattr(unreal.ObjectTypeQuery, 'PAWN', None)
        if pawn_type is None:
            pawn_type = unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY3

        def overlaps(center, extent):
            result = unreal.SystemLibrary.box_overlap_components(actor, center, extent,
                [pawn_type], unreal.SkeletalMeshComponent, [])
            # Python builds expose bool + one out-array either as an optional
            # array or a tuple. A positive control below verifies the query.
            if isinstance(result, tuple) and len(result) == 2 and isinstance(result[0], bool):
                result = result[1]
            return [part for part in (result or []) if part.get_owner() == actor]

        center = (bounds.min + bounds.max) * .5
        extent = (bounds.max - bounds.min) * .5 + unreal.Vector(1, 1, 1)
        assert mesh in overlaps(center, extent), (label, 'Native skeletal overlap positive control failed')
        forbidden_top = -.8
        forbidden_bottom = min(-100., bounds.min.z - 1)
        forbidden_center = unreal.Vector(center.x, center.y, (forbidden_bottom + forbidden_top) * .5)
        forbidden_extent = unreal.Vector(extent.x, extent.y, (forbidden_top - forbidden_bottom) * .5)
        intruders = overlaps(forbidden_center, forbidden_extent)
        assert not intruders, (label, 'Actual native skeletal shape penetrated below floor allowance', intruders, bounds)
        print('CHAOS_RAGDOLL_NATIVE_SHAPE_FLOOR_PASSED', label,
            'broadphase_min_z', bounds.min.z, 'actual_forbidden_region_top_z', forbidden_top,
            'positive_control', True, 'native_shape_overlaps_below_allowance', len(intruders))
        return
    assert bounds.min.z >= -.8, (label, 'Native collision tunneled through floor', bounds)


def ragdoll_diagnostics(label):
    """Compare visual bounds with native per-body collision surface queries."""
    actor = named[label]
    mesh = actor.get_component_by_class(unreal.SkeletalMeshComponent)
    print('CHAOS_RAGDOLL_BOUNDS_DIAGNOSTIC', label, 'actor', actor.get_actor_transform(),
        'colliding_component_bounds', actor.get_actor_bounds(True),
        'mesh_world', mesh.get_world_transform(), 'collision', mesh.get_collision_enabled(),
        'mesh_tick', mesh.is_component_tick_enabled(),
        'visibility_anim_tick', mesh.get_editor_property('visibility_based_anim_tick_option'))
    found = 0
    for index in range(mesh.get_num_bones()):
        bone = mesh.get_bone_name(index)
        if not mesh.is_simulating_physics(bone):
            continue
        found += 1
        center = mesh.get_center_of_mass(bone)
        distance, bottom = mesh.get_closest_point_on_collision(
            center - unreal.Vector(0, 0, 10000), bone)
        print('CHAOS_RAGDOLL_BODY_DIAGNOSTIC', label, 'bone', bone,
            'physics_com', center, 'socket_pose', mesh.get_socket_transform(bone),
            'physics_velocity', mesh.get_physics_linear_velocity(bone),
            'collision_point_from_below', bottom, 'query_distance', distance)
    assert found, (label, 'No simulated ragdoll bones found')


def assert_ragdoll_transfer_momentum(label):
    """Check native immediately-read velocities, before later contact impulses."""
    actor = named[label]
    unreal.SystemLibrary.execute_console_command(actor, 'FLUSHLOG')
    arguments = {}
    for match in re.finditer(r'-(abslog|log|logfilename)=(?:"([^\"]+)"|(\S+))',
            unreal.SystemLibrary.get_command_line(), re.IGNORECASE):
        arguments[match.group(1).lower()] = match.group(2) or match.group(3)
    filename = arguments.get('log') or arguments.get('logfilename')
    if filename:
        filename = os.path.join(unreal.SystemLibrary.get_project_directory(), 'Saved', 'Logs', filename)
    else:
        filename = arguments.get('abslog') or os.path.join(
            unreal.SystemLibrary.get_project_directory(), 'Saved', 'Logs',
            unreal.SystemLibrary.get_game_name() + '.log')
    with open(filename, encoding='utf-8-sig', errors='replace') as stream:
        output = stream.read()
    pattern = (r'WAREHOUSE_RAGDOLL_LINEAR_MOMENTUM Actor=' + re.escape(actor.get_name()) +
        r' ExpectedP=\(([^)]+)\) ActualP=\(([^)]+)\) Error=(\S+) OldMass=(\S+) NewMass=(\S+)')
    records = re.findall(pattern, output)
    assert records, (label, 'Missing native ragdoll momentum readback', filename)
    for expected_text, actual_text, error_text, old_mass_text, new_mass_text in records:
        expected = [float(value) for value in expected_text.split(',')]
        actual = [float(value) for value in actual_text.split(',')]
        error = math.sqrt(sum((a-e)**2 for a, e in zip(actual, expected)))
        tolerance = max(.01, math.sqrt(sum(value**2 for value in expected))*1e-5)
        assert abs(error-float(error_text)) < 1e-5, (label, 'Native transfer log error is inconsistent')
        assert error <= tolerance, (label, 'Ragdoll transfer changed native linear momentum', expected, actual, error, tolerance)
        assert abs(float(new_mass_text)-float(old_mass_text)) < .01, (label, 'Ragdoll transfer changed mass')
        print('CHAOS_RAGDOLL_LINEAR_MOMENTUM_PASSED', label,
            'expected', expected, 'actual', actual, 'error_kg_cm_s', error,
            'tolerance_kg_cm_s', tolerance)


def finish():
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(_ui_dt):
    global phase, at, last_now, named, pawn, initial, peaks, held_before, tip_min_up
    try:
        assert time.monotonic() - started < 240, ('Chaos contact test timeout', phase)
        if phase == 0:
            if time.monotonic() - started < 1:
                return
            level.editor_request_begin_play()
            phase = 1
            return
        game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        world_dt = now - last_now
        last_now = now
        if named is None:
            named = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            pawn = unreal.GameplayStatics.get_player_controller(game, 0).get_controlled_pawn()
            assert pawn and callable(getattr(pawn, 'get_held_cargo', None)), 'Project first-person GameMode did not create the physical player'
            for label in ('ReactionAGV', 'LoadedAGV', 'ImpactAGV'):
                named[label].set_actor_tick_enabled(False)
                # This fixture isolates contact mechanics, with its navigation
                # and scanner loop stopped. Explicitly grant the drive circuit
                # permission instead of retaining a first-frame sensor stop.
                named[label].get_editor_property('drive').set_safety_speed_limit(200)
            body(named['ReactionDeck']).set_enable_gravity(False)
        # Exactly one control update per elapsed game tick. Chaos alone integrates the bodies.
        if world_dt > 0:
            for label, command in list(prototype_controls.items()):
                drive = named[label].get_editor_property('drive')
                drive.set_command(*command)
                drive.step(world_dt)
        for label in ('HeldCarton', 'DropCarton', 'ForkLoad'):
            above_floor(label)
        if phase == 1:
            if now < .15:
                return
            for label, mass in (('MomentumA', 5), ('MomentumB', 7), ('LoadedAGV', 1000),
                    ('ReactionAGV', 1000), ('ImpactAGV', 1000), ('ContactPallet', 25), ('HeldCarton', 12)):
                assert_live(label, mass)
            assert abs(named['MainAGV'].get_physical_mass_kg() - 1000) < .1, named['MainAGV'].get_physical_mass_kg()
            assert_live('MainAGV')
            drive = named['LoadedAGV'].get_editor_property('drive')
            assert abs(drive.get_editor_property('rated_payload_kg') - 250) < .1
            drive.set_payload(12, unreal.Vector(60, 0, 30), unreal.Vector2D(50, 75))
            assert abs(body(named['LoadedAGV']).get_mass() - 1000) < .1, 'Payload mass counted twice'
            initial = {label: position(label) for label in ('LoadedAGV', 'ForkLoad', 'ContactPallet',
                'ContactWorker', 'HumanB', 'MainAGV')}
            impulse('MomentumA', (1000, 0, 0))
            prototype_controls['ReactionAGV'] = (60, 0)
            prototype_controls['LoadedAGV'] = (-60, 0)
            at = now
            phase = 2
            return
        if phase == 2:
            observe('MomentumB')
            if now - at < .45:
                return
            horizontal_p = body(named['MomentumA']).get_mass()*velocity('MomentumA').x + body(named['MomentumB']).get_mass()*velocity('MomentumB').x
            assert peaks['MomentumB'] > 10, ('Parcel contact transferred no momentum', peaks)
            assert abs(horizontal_p - 1000) < 100, ('Airborne pair momentum error', horizontal_p)
            deck_v = velocity('ReactionDeck').x
            vehicle_v = velocity('ReactionAGV').x
            total_p = body(named['ReactionDeck']).get_mass()*deck_v + body(named['ReactionAGV']).get_mass()*vehicle_v
            reaction_drive = named['ReactionAGV'].get_editor_property('drive')
            print('CHAOS_REACTION_DIAGNOSTIC', 'time', now, 'vehicle', named['ReactionAGV'].get_actor_transform(),
                'deck', named['ReactionDeck'].get_actor_transform(), 'relative velocities', velocity('ReactionAGV'), velocity('ReactionDeck'),
                'drive', {key: reaction_drive.get_editor_property(key) for key in
                    ('speed_cm_s', 'wheel_speed_cm_s', 'steer_angle_deg', 'motor_torque_nm', 'drive_wheel_load_n',
                        'drive_wheel_slip_cm_s', 'stability_margin', 'safety_speed_limit_cm_s')})
            assert vehicle_v > 1 and deck_v < -.2, ('Dynamic support received no opposite motor reaction', vehicle_v, deck_v)
            assert abs(total_p) < max(100, abs(1000*vehicle_v)*.08), ('Drive generated net support momentum', total_p)
            prototype_controls.pop('ReactionAGV')
            print('CHAOS_MASS_CONTACT_MOMENTUM_PASSED', 'parcel momentum', horizontal_p,
                'deck/car velocities', deck_v, vehicle_v, 'weld/main mass', 1000)
            impulse('PalletStriker', (1500, 0, 0))
            impulse('HumanStriker', (5000, 0, 0))
            impulse('HumanA', (32000, 0, 0))
            impulse('VehiclePallet', (0, 18750, 0))
            at = now
            phase = 3
            return
        if phase == 3:
            observe('ContactPallet', 'ContactWorker', 'HumanB', 'MainAGV')
            if now - at < 1.5:
                return
            for label in ('ContactPallet', 'ContactWorker', 'HumanB', 'MainAGV'):
                assert peaks.get(label, 0) > .1, (label, 'Contact caused no physical reaction', peaks)
                assert_live(label)
            assert (position('LoadedAGV')-initial['LoadedAGV']).length() > 20, 'Motor command failed to move actual AGV body'
            assert (position('ForkLoad')-initial['ForkLoad']).length() > 10, 'Loose fork load did not travel on native contact'
            assert_live('ForkLoad', 12)
            assert_live('LoadedAGV', 1000)
            prototype_controls['LoadedAGV'] = (0, 0)
            # The impulse supplies momentum; Halt must leave it intact until the finite brake acts.
            impulse('LoadedAGV', (-100000, 0, 0))
            before = velocity('LoadedAGV').x
            named['LoadedAGV'].get_editor_property('drive').halt()
            assert abs(velocity('LoadedAGV').x-before) < .01, 'Halt erased native body velocity'
            print('CHAOS_CARGO_PALLET_HUMAN_VEHICLE_CONTACT_PASSED', peaks,
                'loose fork load travel', (position('ForkLoad')-initial['ForkLoad']).length())
            peaks = {}
            initial['MainAGV'] = position('MainAGV')
            impulse('ImpactAGV', (300000, 0, 0))
            at = now
            phase = 4
            return
        if phase == 4:
            observe('MainAGV')
            if now - at < 1:
                return
            assert peaks.get('MainAGV', 0) > .1, ('Vehicle pair transferred no momentum', peaks)
            assert (position('MainAGV')-initial['MainAGV']).length() > .01, 'Main vehicle ignored prototype body contact'
            assert_live('ImpactAGV', 1000)
            assert_live('MainAGV')
            print('CHAOS_VEHICLE_PAIR_PASSED', peaks['MainAGV'])
            assert pawn.try_pickup_cargo(named['HeldCarton']), 'Physical player failed to pick up nearby 12 kg carton'
            at = now
            phase = 5
            return
        if phase == 5:
            assert_live('HeldCarton', 12)
            assert pawn.get_held_cargo() == named['HeldCarton'], 'Grip slipped before external impact'
            if now - at < 1:
                return
            held_before = position('HeldCarton')
            start = position('HeldStriker')
            # Shoot at the box from its outer front/side, clear of the player's capsule.
            target = held_before + unreal.Vector(10, -5, 0)
            flight = .4
            delta = target-start
            impulse('HeldStriker', (8*delta.x/flight, 8*delta.y/flight,
                8*(delta.z/flight+490*flight)))
            peaks = {}
            at = now
            phase = 6
            return
        if phase == 6:
            assert_live('HeldCarton', 12)
            observe('HeldCarton')
            if now - at < .8:
                return
            assert peaks.get('HeldCarton', 0) > 10, ('Held box ignored environment impact', peaks)
            print('CHAOS_HELD_CARGO_CONTACT_PASSED', peaks['HeldCarton'],
                'motion', (position('HeldCarton')-held_before).length(), 'still held', bool(pawn.get_held_cargo()))
            if pawn.get_held_cargo():
                pawn.drop_cargo()
            assert_live('HeldCarton', 12)
            angular = body(named['LoadedAGV'])
            inertia = angular.get_inertia_tensor()
            angular.add_angular_impulse_in_radians(unreal.Vector(inertia.x*5, 0, 0))
            person = body(named['TipWorker'])
            inertia = person.get_inertia_tensor()
            person.add_angular_impulse_in_radians(unreal.Vector(inertia.x*8, 0, 0))
            at = now
            phase = 7
            return
        if phase == 7:
            tip_min_up = min(tip_min_up, named['LoadedAGV'].get_actor_up_vector().z)
            assert_live('LoadedAGV', 1000)
            assert_live('ForkLoad', 12)
            assert_live('HeldCarton', 12)
            if now - at < 3:
                return
            assert tip_min_up < .8, ('AGV roll/pitch secretly constrained', tip_min_up)
            assert named['TipWorker'].is_ragdoll(), 'Large human tipping impulse never entered physical ragdoll'
            assert_live('TipWorker')
            ragdoll_diagnostics('TipWorker')
            assert_ragdoll_transfer_momentum('TipWorker')
            above_floor('TipWorker')
            center, extent = named['DropCarton'].get_actor_bounds(True)
            assert abs(center.z-extent.z) < 3, ('Free fall failed to settle on the floor', center, extent)
            center, extent = named['HeldCarton'].get_actor_bounds(True)
            assert abs(center.z-extent.z) < 3, ('Released grip body failed to settle', center, extent)
            print('CHAOS_TIP_RAGDOLL_GRIP_RELEASE_DROP_PASSED', 'min AGV up', tip_min_up)
            print('WAREHOUSE_CONTACT_PHYSICS_PIE_PASSED native contacts, mass conservation, dynamic support reaction, '
                'parcel/pallet/person/vehicle pairs, loose fork load, held impact, finite brake, free tip, ragdoll and floor drops')
            finish()
    except Exception:
        print('WAREHOUSE_CONTACT_PHYSICS_PIE_FAILED', traceback.format_exc())
        finish()


handle = unreal.register_slate_post_tick_callback(tick)
