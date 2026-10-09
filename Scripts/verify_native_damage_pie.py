"""Isolated native-contact damage checks in Engine Entry. Never saves a map.

Run in a separate editor with -ExecCmds="py .../verify_native_damage_pie.py".
Checks a pinned body's angular-only impact, failed cargo weight, released mass,
runtime contact registration and refusal to resize an active physics parcel.
"""
import math
import time
import traceback
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
editor_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = unreal.GameplayStatics.get_all_actors_of_class(editor_world, unreal.WorldSettings)[0]
settings.set_editor_property('default_game_mode', unreal.GameModeBase)
cube = unreal.load_asset('/Engine/BasicShapes/Cube')


def block(label, location, size, collision='BlockAll'):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location))
    actor.set_actor_label(label)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_collision_profile_name(collision)
    actor.set_actor_scale3d(unreal.Vector(*(v / 100 for v in size)))
    return actor


def carton(label, location, size, mass):
    actor = actors.spawn_actor_from_class(unreal.WarehouseCargo, unreal.Vector(*location))
    actor.set_actor_label(label)
    actor.set_cargo_mesh(cube)
    actor.set_gross_mass_kg(mass)
    actor.set_actor_scale3d(unreal.Vector(*(v / 100 for v in size)))
    return actor


floor = block('NativeDamageFloor', (0, 0, -15), (5000, 5000, 30))
pin = block('NativeAngularPin', (-600, 0, 80), (2, 2, 160))
beam = carton('NativeAngularBeam', (-600, 0, 140), (200, 12, 12), 20)
stop = block('NativeAngularStop', (-525, 50, 140), (25, 25, 100))
joint_actor = actors.spawn_actor_from_class(unreal.PhysicsConstraintActor, unreal.Vector(-600, 0, 140))
joint_actor.set_actor_label('NativeAngularBearing')
joint = joint_actor.get_component_by_class(unreal.PhysicsConstraintComponent)
joint.set_disable_collision(True)
joint.set_projection_enabled(False)
locked = unreal.LinearConstraintMotion.LCM_LOCKED
joint.set_linear_x_limit(locked, 0)
joint.set_linear_y_limit(locked, 0)
joint.set_linear_z_limit(locked, 0)
joint.set_angular_twist_limit(unreal.AngularConstraintMotion.ACM_LOCKED, 0)
joint.set_angular_swing1_limit(unreal.AngularConstraintMotion.ACM_FREE, 0)
joint.set_angular_swing2_limit(unreal.AngularConstraintMotion.ACM_LOCKED, 0)
joint.set_constrained_components(pin.static_mesh_component, unreal.Name('None'),
    beam.get_component_by_class(unreal.StaticMeshComponent), unreal.Name('None'))

pieces = [
    block('NativeRackPostA', (270, -600, 90), (8, 8, 180)),
    block('NativeRackPostB', (330, -600, 90), (8, 8, 180)),
    block('NativeRackBeam', (300, -600, 184), (72, 8, 8)),
]
pallet = actors.spawn_actor_from_class(unreal.WarehousePallet, unreal.Vector(600, 0, .1))
pallet.set_actor_label('NativeFailedCargoPallet')
pallet.set_editor_property('payload_mass_kg', 20)
load = carton('NativeFailedCargo', (600, 0, 25.2), (30, 30, 20), 10)
late = block('NativeLateBody', (1500, 0, 60), (50, 50, 50), 'NoCollision')

system = actors.spawn_actor_from_class(unreal.WarehouseDamageSystem, unreal.Vector())
system.set_actor_label('NativeDamageSystem')


def spec(name, members, mass, failure, yield_j=1, failure_j=1000):
    return unreal.WarehouseStrength(name=name, members=members,
        physics_meshes=[cube] * len(members), mass_kg=mass, rated_load_kg=1000000,
        failure=failure, impact_yield_j=yield_j, impact_failure_j=failure_j)


system.set_editor_property('objects', [
    spec('Floor', [floor], 50000, unreal.WarehouseFailure.STRUCTURE, 10000, 100000),
    spec('AngularBeam', [beam], 20, unreal.WarehouseFailure.CRUSH),
    spec('AngularStop', [stop], 10000, unreal.WarehouseFailure.STRUCTURE),
    spec('ReleasedRack', pieces, 90, unreal.WarehouseFailure.RACK),
    spec('FailedCargo', [load], 10, unreal.WarehouseFailure.CRUSH),
    spec('Pallet', [pallet], 25, unreal.WarehouseFailure.CRUSH, 10000, 100000),
    spec('LateBody', [late], 10, unreal.WarehouseFailure.CRUSH),
])

started = time.monotonic()
phase = 0
at = 0
named = None
game = None
damage = None
initial_scale = None
initial_piece_scales = None


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def profile(name):
    return next(s for s in damage.get_editor_property('objects') if s.name == name)


def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    global phase, at, named, game, damage, initial_scale, initial_piece_scales
    try:
        assert time.monotonic() - started < 180, 'Native damage PIE timeout'
        if phase == 0:
            if time.monotonic() - started < .5:
                return
            level.editor_request_begin_play()
            phase = 1
            return
        game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if named is None:
            named = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            damage = named['NativeDamageSystem']
            # Reinitialize against the now-dynamic beam, rather than an editor kinematic body.
            bearing = named['NativeAngularBearing'].get_component_by_class(unreal.PhysicsConstraintComponent)
            bearing.set_constrained_components(body(named['NativeAngularPin']), unreal.Name('None'),
                body(named['NativeAngularBeam']), unreal.Name('None'))
            # The first PIE callback may arrive after a gravity step. Define the
            # intended bearing explicitly, rather than capturing the fallen COM.
            # Constraint frames use unscaled local body space: pin Z80 + 60 = Z140.
            bearing.set_constraint_reference_frame(unreal.ConstraintFrame.FRAME1,
                unreal.Transform(location=unreal.Vector(0, 0, 60)))
            bearing.set_constraint_reference_frame(unreal.ConstraintFrame.FRAME2, unreal.Transform())
            print('NATIVE_ANGULAR_BEARING_DIAGNOSTIC', 'initialized', 'anchor',
                named['NativeAngularBearing'].get_actor_location(), 'beam COM',
                body(named['NativeAngularBeam']).get_center_of_mass(), 'body position',
                body(named['NativeAngularBeam']).get_world_location())
        if phase == 1:
            if now < 2:
                return
            angular = body(named['NativeAngularBeam'])
            print('NATIVE_ANGULAR_BEARING_DIAGNOSTIC', 'settled', 'anchor',
                named['NativeAngularBearing'].get_actor_location(), 'beam COM',
                angular.get_center_of_mass(), 'body position', angular.get_world_location(),
                'linear velocity', angular.get_physics_linear_velocity())
            assert angular.is_simulating_physics()
            assert abs(angular.get_world_location().z - 140) < .5, 'Angular bearing fixture moved from its anchor'
            assert angular.get_physics_linear_velocity().length() < 1, 'Bearing fixture did not hold its COM'
            assert profile('AngularBeam').damage == 0, 'Bearing fixture was damaged before the angular impulse'
            inertia = angular.get_inertia_tensor()
            assert math.isfinite(inertia.z) and inertia.z > 0
            angular.add_angular_impulse_in_radians(unreal.Vector(0, 0, inertia.z * 4))
            at = now
            phase = 2
            return
        if phase == 2:
            if profile('AngularBeam').damage <= 0:
                assert now - at < 8, 'Angular-only native contact produced no damage'
                return
            print('NATIVE_ANGULAR_CONTACT_PASSED', 'pinned COM, native angular impulse', profile('AngularBeam').damage)
            parcel = named['NativeFailedCargo']
            initial_scale = parcel.get_actor_scale3d()
            assert abs(body(parcel).get_mass() - 10) < .05
            assert abs(body(named['NativeFailedCargoPallet']).get_mass() - 45) < .05
            assert damage.get_supported_mass(named['NativeFailedCargoPallet']) >= 29.9
            recipe = unreal.WarehouseCargo.generate_cargo_recipe(20261010, 8, 0, unreal.Vector(50, 40, 35), 1)
            assert recipe.valid and not parcel.apply_cargo_recipe(recipe), 'Active parcel was resized/teleported'
            damage.apply_impact(parcel, 5000)
            assert damage.has_failed(parcel)
            at = now
            phase = 3
            return
        if phase == 3:
            if now - at < .5:
                return
            parcel = named['NativeFailedCargo']
            assert (parcel.get_actor_scale3d() - initial_scale).length() < .0001, 'Failure changed cargo collision scale'
            assert body(parcel).is_simulating_physics() and abs(body(parcel).get_mass() - 10) < .05
            assert damage.get_supported_mass(named['NativeFailedCargoPallet']) >= 29.9, 'Failed cargo mass vanished from support graph'
            print('NATIVE_FAILED_CARGO_WEIGHT_PASSED', '10kg cargo + 20kg virtual contents; collision geometry retained')
            rack = [named[label] for label in ('NativeRackPostA', 'NativeRackPostB', 'NativeRackBeam')]
            initial_piece_scales = [a.get_actor_scale3d() for a in rack]
            damage.apply_impact(rack[2], 5000)
            assert all(body(a).is_simulating_physics() for a in rack)
            actual_mass = sum(body(a).get_mass() for a in rack)
            assert abs(actual_mass - 90) < .05, ('Released mass changed', actual_mass)
            assert body(rack[0]).get_mass() > body(rack[2]).get_mass(), 'Different-volume pieces received equal mass'
            assert all((a.get_actor_scale3d() - scale).length() < .0001 for a, scale in zip(rack, initial_piece_scales)), 'Rack failure fabricated geometric buckling'
            print('NATIVE_RELEASED_MASS_PASSED', actual_mass, [body(a).get_mass() for a in rack])
            new_body = body(named['NativeLateBody'])
            new_body.set_mobility(unreal.ComponentMobility.MOVABLE)
            new_body.set_collision_profile_name('PhysicsActor')
            new_body.set_enable_gravity(True)
            new_body.set_mass_override_in_kg(unreal.Name('None'), 10)
            new_body.set_simulate_physics(True)
            before_registration = new_body.get_mass()
            damage.register_physical_contacts(named['NativeLateBody'])
            after_registration = new_body.get_mass()
            print('NATIVE_RUNTIME_CONTACT_DIAGNOSTIC', 'before mass', before_registration,
                'after mass', after_registration, 'simulating', new_body.is_simulating_physics())
            assert new_body.is_simulating_physics(), 'Late activation fixture remained static'
            assert abs(before_registration - 10) < .05, ('Late fixture mass was not configured', before_registration)
            assert abs(after_registration - before_registration) < .05, 'Contact registration changed owner mass'
            at = now
            phase = 4
            return
        if phase == 4:
            if profile('LateBody').damage <= 0:
                assert now - at < 8, 'Runtime physical activation did not register native hit damage'
                return
            print('NATIVE_RUNTIME_CONTACT_REGISTRATION_PASSED', profile('LateBody').damage)
            print('NATIVE_DAMAGE_PIE_PASSED', 'angular contact, failed cargo weight, released mass, runtime registration, immutable active parcel')
            finish()
    except Exception:
        print('NATIVE_DAMAGE_PIE_FAILED', traceback.format_exc())
        finish()


handle = unreal.register_slate_post_tick_callback(tick)
