"""Native main-vehicle wheel/lift and optional nonpossessed human checks in Entry.

Never saves. All fixture poses are set before PIE; gameplay moves only through
native actuators/forces. -NativeVehicleOnly skips the separate human checks.
A collider-free TargetPoint owns remote commands: this does not test G/phone UI.
"""
import os
import sys
import time
import traceback
import unreal

sys.path.insert(0, os.path.dirname(__file__))
from apply_real_world_scale import fit

VEHICLE_ONLY = '-nativevehicleonly' in unreal.SystemLibrary.get_command_line().lower()
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
settings.set_editor_property('default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
intact = unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1')
assert intact, 'Prepare intact cargo first'
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -15))
floor.set_actor_label('NativeMotionFloor')
floor.static_mesh_component.set_static_mesh(cube)
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.set_actor_scale3d(unreal.Vector(80, 50, .3))
start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-1500, -1500, 125))
start.set_actor_label('NativeMotionPlayerStart')
vehicle = actors.spawn_actor_from_class(unreal.WarehouseForklift, unreal.Vector())
vehicle.set_actor_label('NativeMainAGV')
vehicle.set_editor_property('auto_start', False)
vehicle.set_editor_property('autonomous_mode', False)
operator = actors.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, -220, 75))
operator.set_actor_label('NativeDummyOperator')
for component in operator.get_components_by_class(unreal.PrimitiveComponent):
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
# Keep 5 cm at the back edge instead of inserting to a 0.25 cm end stop.
# The actual vehicle may settle slightly on its tyres during self-check.
pallet = actors.spawn_actor_from_class(unreal.WarehousePallet, unreal.Vector(65, 0, .15))
pallet.set_actor_label('NativeMotionPallet')
vehicle.set_editor_property('target_pallet', pallet)


def carton(label, center, mass=12):
    actor = actors.spawn_actor_from_class(unreal.WarehouseCargo, unreal.Vector(*center))
    actor.set_actor_label(label)
    actor.set_cargo_mesh(intact)
    actor.set_gross_mass_kg(mass)
    fit(actor, (30, 24, 20), center)
    return actor


load = carton('NativeMotionLoad', (65, 0, 25.25))
human = hand_box = None
if not VEHICLE_ONLY:
    human = actors.spawn_actor_from_class(unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'), unreal.Vector(1200, 900, 125))
    human.set_actor_label('NativeUnpossessedHuman')
    human.set_editor_property('auto_possess_player', unreal.AutoReceiveInput.DISABLED)
    human.set_editor_property('auto_possess_ai', unreal.AutoPossessAI.DISABLED)
    human.set_editor_property('ai_controller_class', None)
    hand_box = carton('NativeHandBox', (1250, 1140, 10.25))
damage_actor = actors.spawn_actor_from_class(unreal.WarehouseDamageSystem, unreal.Vector())
damage_actor.set_actor_label('NativeMotionDamage')
specs = []
for actor, mass in ((floor, 50000), (pallet, 25), (load, 12), (hand_box, 12)):
    if actor:
        specs.append(unreal.WarehouseStrength(name=actor.get_actor_label(), members=[actor], mass_kg=mass,
            rated_load_kg=100000, impact_yield_j=10000, impact_failure_j=100000,
            failure=unreal.WarehouseFailure.STRUCTURE if actor == floor else unreal.WarehouseFailure.CRUSH))
damage_actor.set_editor_property('objects', specs)

began = time.monotonic()
phase = 0
at = 0
named = None
game = None
rig = dummy = deck = cargo = person = parcel = None
initial_height = initial_z = 0
speed_samples = []
yaw_before = 0
deadman_at = 0
peak_jump = 0
human_start = None
walk_peak = sprint_peak = 0


def body(actor):
    if isinstance(actor.root_component, unreal.PrimitiveComponent):
        return actor.root_component
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def dump(marker):
    print(marker, 'vehicle_pose', rig.get_actor_transform(), 'speed', rig.get_editor_property('current_speed_cm'),
        'physical_mass_kg', rig.get_physical_mass_kg(), 'fork_height', rig.get_fork_height_cm(),
        'status', rig.get_editor_property('status'), 'pallet_pose', deck.get_actor_transform(),
        'cargo_pose', cargo.get_actor_transform())


def load_is_dynamic():
    for actor in (deck, cargo):
        assert body(actor).is_simulating_physics(), (actor, 'kinematic load')
        assert not actor.get_attach_parent_actor(), (actor, 'rigidly attached load')


def finish():
    if rig and dummy:
        rig.end_remote_control(dummy)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    global phase, at, named, game, rig, dummy, deck, cargo, person, parcel, initial_height, initial_z
    global yaw_before, deadman_at, human_start, walk_peak, sprint_peak, peak_jump
    try:
        assert time.monotonic() - began < 300, 'Native motion PIE timeout'
        if phase == 0:
            if time.monotonic() - began < .5:
                return
            level.editor_request_begin_play()
            phase = 1
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if named is None:
            named = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            rig, dummy, deck, cargo = [named[key] for key in ('NativeMainAGV', 'NativeDummyOperator', 'NativeMotionPallet', 'NativeMotionLoad')]
            person, parcel = named.get('NativeUnpossessedHuman'), named.get('NativeHandBox')
            unreal.SystemLibrary.execute_console_command(game, 't.MaxFPS 60')
        load_is_dynamic()
        assert not rig.get_editor_property('mechanical_failure'), ('Physical rig failed', rig.get_editor_property('status'))
        if phase == 1:
            if now < 2:
                return
            assert abs(rig.get_physical_mass_kg() - 1000) < .5
            assert abs(body(deck).get_mass() - 25) < .05 and abs(body(cargo).get_mass() - 12) < .05
            assert rig.get_actor_up_vector().z > .95
            assert rig.begin_remote_control(dummy), rig.get_editor_property('status')
            initial_height = rig.get_fork_height_cm()
            initial_z = deck.get_actor_location().z
            dump('NATIVE_MAIN_RIG_READY')
            at, phase = now, 2
            return
        if phase == 2:
            rig.set_remote_input(0, 0, 0)
            if now - at < 1.25:
                return
            carriage = next(c for c in rig.get_components_by_class(unreal.SceneComponent) if c.get_name() == 'CarriagePivot')
            print('NATIVE_INSERTION_DIAGNOSTIC', 'frame', carriage.get_world_transform(),
                'pallet', deck.get_actor_transform(), 'can_engage', deck.can_engage(carriage.get_world_transform()))
            assert deck.can_engage(carriage.get_world_transform()), 'Fixture did not pass full-tine insertion checks'
            at, phase = now, 3
            return
        if phase == 3:
            rig.set_remote_input(0, 0, 1)
            if now - at < 2:
                return
            dump('NATIVE_PHYSICAL_LIFT')
            assert rig.get_fork_height_cm() > initial_height + 10
            assert deck.get_actor_location().z > initial_z + 5
            assert cargo.get_actor_bounds(False)[0].z - cargo.get_actor_bounds(False)[1].z > 18
            assert rig.get_editor_property('target_pallet') == deck and rig.get_load_mass_kg() >= 36.9
            at, phase = now, 4
            return
        if phase == 4:
            rig.set_remote_input(0, 0, -1)
            if rig.get_fork_height_cm() > initial_height + .8:
                assert now - at < 8, 'Native F lowering did not settle'
                return
            c, e = deck.get_actor_bounds(False)
            assert c.z - e.z < 2, ('Pallet did not return to ground', c, e)
            print('NATIVE_LIFT_LOWER_CONTACT_PASSED', '25kg pallet + 12kg carton, always dynamic')
            at, phase = now, 5
            return
        if phase == 5:
            rig.set_remote_input(-1, 0, 0)
            speed = rig.get_editor_property('current_speed_cm')
            speed_samples.append((now - at, speed))
            if now - at < 5:
                return
            steady = [value for age, value in speed_samples if age > 4]
            assert steady and -133 <= sum(steady) / len(steady) < -80, ('Native -130cm/s target not reached', steady)
            assert min(value for _, value in speed_samples) >= -140, ('Excess native speed', speed_samples[-10:])
            before = body(rig).get_physics_linear_velocity()
            rig.set_remote_input(0, 0, 0)
            after = body(rig).get_physics_linear_velocity()
            assert (before - after).length() < .001, 'Brake command reset body velocity'
            print('NATIVE_WHEEL_REVERSE_PASSED', 'command -130cm/s', 'steady', sum(steady) / len(steady), 'instantaneous brake velocity', after)
            at, deadman_at, phase = now, time.monotonic(), 6
            return
        if phase == 6:
            if time.monotonic() - deadman_at < .5 or abs(rig.get_editor_property('current_speed_cm')) > 3:
                assert now - at < 8, ('Finite native braking did not stop', rig.get_editor_property('current_speed_cm'))
                return
            assert 'TIMEOUT' in rig.get_editor_property('status'), rig.get_editor_property('status')
            print('NATIVE_DEADMAN_FINITE_BRAKE_PASSED', rig.get_editor_property('current_speed_cm'))
            yaw_before = rig.get_actor_rotation().yaw
            at, phase = now, 7
            return
        if phase == 7:
            rig.set_remote_input(-.5, .5, 0)
            # A finite steering actuator needs time to build force against
            # tyre contact; measure the response after five physical seconds.
            if now - at < 5:
                return
            axle = next(c for c in rig.get_components_by_class(unreal.PhysicsConstraintComponent) if c.get_name() == 'PhysicalAxle_0')
            print('NATIVE_STEERING_AXLE_DIAGNOSTIC', 'swing1', axle.get_current_swing1(),
                'swing2', axle.get_current_swing2(), 'twist', axle.get_current_twist())
            yaw_change = (rig.get_actor_rotation().yaw - yaw_before + 180) % 360 - 180
            assert yaw_change < -5, ('Reverse with positive curvature must decrease heading', yaw_change)
            print('NATIVE_WHEEL_STEERING_PASSED', yaw_change)
            dump('NATIVE_STEERING_FINAL_POSE')
            rig.set_remote_input(0, 0, 0)
            rig.end_remote_control(dummy)
            print('NATIVE_FORKLIFT_MOTION_PIE_PASSED', 'main 1000kg chassis; dynamic lift/lower; native wheel reverse/brake/steering; DummyOperator only, G UI not tested')
            if VEHICLE_ONLY:
                finish()
                return
            assert person.get_controller() is None, 'Human fixture was possessed'
            human_start = person.get_actor_location()
            assert abs(person.get_component_by_class(unreal.CapsuleComponent).get_mass() - 80) < .05
            person.set_locomotion_input(False, False)
            at, phase = now, 8
            return
        assert not person.is_physical_ragdoll(), 'Normal human movement unexpectedly ragdolled'
        capsule = person.get_component_by_class(unreal.CapsuleComponent)
        if phase == 8:
            person.add_movement_input(unreal.Vector(0, 1, 0), 1, True)
            walk_peak = max(walk_peak, person.get_velocity().length())
            if now - at < 1.2:
                return
            assert walk_peak > 120 and (person.get_actor_location() - human_start).length() > 60
            at, phase = now, 9
            return
        if phase == 9:
            if now - at < 1.1:
                return
            assert person.try_pickup_cargo(parcel), ('Native carry pickup failed', person.get_actor_location(), parcel.get_actor_location())
            assert body(parcel).is_simulating_physics() and not parcel.get_attach_parent_actor()
            at, phase = now, 10
            return
        if phase == 10:
            if now - at < 3:
                return
            assert person.get_held_cargo() == parcel, 'Finite grip slipped in unobstructed carry'
            assert capsule.get_unscaled_capsule_half_height() >= 95, 'Carried weight caused false crouch'
            before = body(parcel).get_physics_linear_velocity()
            person.drop_cargo()
            assert not person.get_held_cargo() and body(parcel).is_simulating_physics()
            assert (body(parcel).get_physics_linear_velocity() - before).length() < .001, 'Drop reset momentum'
            print('NATIVE_HUMAN_DYNAMIC_GRIP_PASSED', '80kg human + 12kg body, no attachment, standing load feedforward, drop momentum retained')
            person.set_locomotion_input(True, False)
            at, phase = now, 11
            return
        if phase == 11:
            person.add_movement_input(unreal.Vector(0, 1, 0), 1, True)
            sprint_peak = max(sprint_peak, person.get_velocity().length())
            if now - at < 2:
                return
            assert sprint_peak > 250 and sprint_peak > walk_peak + 40
            person.set_locomotion_input(False, False)
            at, phase = now, 12
            return
        if phase == 12:
            if now - at < 1.8:
                return
            assert person.get_velocity().length() < 15, 'Human finite brake did not settle before jump'
            human_start = person.get_actor_location()
            person.do_jump_start()
            at, phase = now, 13
            return
        if phase == 13:
            peak_jump = max(peak_jump, person.get_actor_location().z - human_start.z)
            if now - at < 2.5:
                return
            assert peak_jump > 35, ('Native jump did not rise', peak_jump)
            person.set_locomotion_input(False, True)
            at, phase = now, 14
            return
        if phase == 14:
            if now - at < 1:
                return
            assert capsule.get_unscaled_capsule_half_height() < 59
            assert person.get_actor_location().z < human_start.z - 25, 'Crouch collider shrank without physical COM lowering'
            person.set_locomotion_input(False, False)
            at, phase = now, 15
            return
        if phase == 15:
            if capsule.get_unscaled_capsule_half_height() < 95:
                assert now - at < 4, 'Finite leg support could not stand up'
                return
            assert abs(capsule.get_mass() - 80) < .05
            print('NATIVE_HUMAN_MOVE_JUMP_CROUCH_PASSED', 'walk_peak', walk_peak, 'sprint_peak', sprint_peak, 'jump_height', peak_jump)
            print('NATIVE_MOTION_AND_HUMAN_PIE_PASSED', 'native actuator contacts and unpossessed human forces; full G smartphone UI and fleet job completion not tested')
            finish()
    except Exception:
        print('NATIVE_MOTION_PIE_FAILED', 'phase', phase, traceback.format_exc())
        if rig:
            dump('NATIVE_MOTION_FAILURE_POSE')
        finish()


handle = unreal.register_slate_post_tick_callback(tick)
