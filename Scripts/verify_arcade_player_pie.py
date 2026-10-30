"""Unsaved actual-player-BP movement, carry and compatibility regression.

Uses native input APIs, not OS keys. An AIController drives DoMove/Sprint/Crouch
so the real PlayerController's keyboard polling cannot overwrite test inputs.
The real PlayerController is used for camera, observer and remote interaction.
Only the player changes to CharacterMovement; cargo/pallet/AGV remain physical.
"""
import hashlib
import json
import math
from pathlib import Path
import sys
import time
import traceback

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT/'Saved/Testing/ArcadePlayer'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(Path(__file__).resolve().parent))
from apply_real_world_scale import fit

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
start_wall = time.monotonic()
game = pc = ai = pawn = capsule = movement = mesh = camera = box = pallet = rig = dummy = None
now = 0.0
phase = 'setup'
busy = finished = cleaning = False
handle = None
error = None
report = dict(passed=False, checks=[], recent_samples=[], os_keys_tested=False,
              scope='Actual player BP/native input API; immediate arcade movement and heavy carry; genuine cargo/pallet/AGV Chaos bodies; no transport orders or sensor-algorithm regression')
source_files = list((ROOT/'Source/msc_vr').glob('WarehouseForklift*'))
source_files += [p for p in (ROOT/'Source/msc_vr/Agv').rglob('*') if p.suffix in ('.h', '.cpp')]
source_files += [ROOT/'Content/FirstPerson/Blueprints/BP_FirstPersonCharacter.uasset',
                 ROOT/'Content/FirstPerson/Lvl_FirstPerson.umap']
source_hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files if p.is_file()}


def check(name, **data):
    report['checks'].append(dict(name=name, **data))
    print('ARCADE_PLAYER_CHECK', name, json.dumps(data, ensure_ascii=False))


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def horizontal(vector):
    return math.hypot(vector.x, vector.y)


def difference_angle(a, b):
    return abs((a-b+180) % 360-180)


def enter(name):
    global phase
    phase = name


def frames(count=1):
    for _ in range(count):
        yield


def wait(seconds, action=None):
    began = now
    while now-began < seconds:
        if action:
            action()
        yield


def healthy():
    assert not pawn.is_physical_ragdoll(), ('Player fell/ragdolled', phase)
    assert not capsule.is_simulating_physics(), 'Player capsule still uses rigid-body locomotion'
    rotation = pawn.get_actor_rotation()
    assert abs(rotation.pitch) < .05 and abs(rotation.roll) < .05, (phase, rotation)
    assert pawn.get_actor_location().z-capsule.get_scaled_capsule_half_height() > -2, ('Player under floor', phase)


def relocate(point, yaw=0):
    movement.stop_movement_immediately()
    pawn.set_actor_location(unreal.Vector(*point), False, True)
    pawn.set_actor_rotation(unreal.Rotator(yaw=yaw), True)
    controller = pawn.get_controller()
    if controller:
        controller.set_control_rotation(unreal.Rotator(yaw=yaw))


def drive(name, right, forward, sprint=False, seconds=.65, target=350):
    enter(name)
    speeds, foot, first_velocity = [], [], None
    pawn.set_locomotion_input(sprint, False)
    began = now
    while now-began < seconds:
        pawn.do_move(right, forward)
        yield
        healthy()
        speed = horizontal(pawn.get_velocity())
        if first_velocity is None:
            first_velocity = speed
        speeds.append(speed)
        foot.append(mesh.get_socket_transform('foot_l', unreal.RelativeTransformSpace.RTS_COMPONENT).translation.x)
    # At most one native movement update is allowed; there is no acceleration ramp.
    assert abs(first_velocity-target) < 2, (name, 'First-frame response', first_velocity, target)
    assert all(abs(s-target) < 2 for s in speeds), (name, min(speeds), max(speeds), target)
    check(name, target_cm_s=target, first_frame_cm_s=first_velocity,
          minimum_cm_s=min(speeds), maximum_cm_s=max(speeds), gait_span_cm=max(foot)-min(foot))
    return max(foot)-min(foot)


def stop(name):
    enter(name)
    origin = pawn.get_actor_location()
    yield
    speed = horizontal(pawn.get_velocity())
    assert speed < .1, (name, 'Input release did not stop in one update', speed)
    yield from wait(.15)
    drift = horizontal(pawn.get_actor_location()-origin)
    assert drift < 1, (name, 'Residual slide', drift)
    check(name, speed_after_one_update_cm_s=speed, drift_cm=drift)


def set_object(actor, centre):
    part = body(actor)
    part.set_simulate_physics(True)
    part.set_physics_linear_velocity(unreal.Vector())
    part.set_physics_angular_velocity_in_degrees(unreal.Vector())
    actor.set_actor_rotation(unreal.Rotator(), True)
    current, _ = actor.get_actor_bounds(False)
    actor.set_actor_location(actor.get_actor_location()+unreal.Vector(*centre)-current, False, True)
    part.wake_all_rigid_bodies()


def ground_rest(actor, expected_mass):
    part = body(actor)
    centre, extent = actor.get_actor_bounds(False)
    assert part.is_simulating_physics() and part.is_gravity_enabled(), actor.get_name()
    assert abs(part.get_mass()-expected_mass) < .2, (actor.get_name(), part.get_mass())
    assert -.8 < centre.z-extent.z < 3, (actor.get_name(), 'Not resting on floor', centre, extent)
    assert part.get_physics_linear_velocity().length() < 15, (actor.get_name(), 'Never settled')


def setup():
    assert not editor.get_game_world(), 'Finish existing PIE first'
    assert level.load_level('/Engine/Maps/Entry')
    for actor in list(actors.get_all_level_actors()):
        if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
            actors.destroy_actor(actor)
    world = editor.get_editor_world()
    settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
    settings.set_editor_property('default_game_mode', unreal.load_class(None,
        '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')

    def solid(label, position, size, rotation=unreal.Rotator()):
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position), rotation)
        actor.set_actor_label(label)
        actor.static_mesh_component.set_static_mesh(cube)
        actor.static_mesh_component.set_collision_profile_name('BlockAll')
        actor.set_actor_scale3d(unreal.Vector(*(v/100 for v in size)))
        return actor

    solid('ArcadeFloor', (0, 0, -20), (12000, 8000, 40))
    solid('ArcadeWall', (700, 0, 250), (40, 1000, 500))
    solid('ArcadeRamp', (0, 1800, 100), (800, 500, 20), unreal.Rotator(pitch=15))
    solid('ArcadeAGVWall', (4000, -1800, 300), (40, 1200, 600))
    ceiling = solid('ArcadeCeiling', (4500, 3000, 150), (250, 250, 20))
    ceiling.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-4500, 1000, 110))
    human_class = unreal.load_class(None,
        '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
    human = actors.spawn_actor_from_class(human_class, unreal.Vector(-3000, -2000, 110))
    human.set_actor_label('ArcadeHuman')
    human.set_editor_property('auto_possess_player', unreal.AutoReceiveInput.DISABLED)
    human.set_editor_property('auto_possess_ai', unreal.AutoPossessAI.DISABLED)
    actors.spawn_actor_from_class(unreal.AIController, unreal.Vector()).set_actor_label('ArcadeInputController')
    cargo = actors.spawn_actor_from_class(unreal.WarehouseCargo, unreal.Vector(-2850, -3100, 20))
    cargo.set_actor_label('ArcadeHeavyCargo')
    cargo.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1'))
    fit(cargo, (36, 30, 30), (-2850, -3100, 15.5))
    cargo.set_gross_mass_kg(1000)
    empty = actors.spawn_actor_from_class(unreal.WarehousePallet, unreal.Vector(-3000, -1200, .5))
    empty.set_actor_label('ArcadeEmptyPallet')
    empty.set_editor_property('pallet_mass_kg', 25)
    empty.set_editor_property('payload_mass_kg', 0)
    strength = actors.spawn_actor_from_class(unreal.WarehouseDamageSystem, unreal.Vector())
    strength.set_actor_label('ArcadeStrength')
    strength.set_editor_property('objects', [
        unreal.WarehouseStrength(name=a.get_actor_label(), members=[a],
            physics_meshes=[body(a).static_mesh], mass_kg=mass, rated_load_kg=capacity,
            impact_yield_j=100000, impact_failure_j=200000)
        for a, mass, capacity in ((cargo, 1000, 1200), (empty, 25, 1500))])
    vehicle = actors.spawn_actor_from_class(unreal.WarehouseForklift, unreal.Vector(3000, -1800, 0))
    vehicle.set_actor_label('ArcadeAGV')
    vehicle.set_editor_property('auto_start', False)
    vehicle.set_editor_property('autonomous_mode', False)
    operator = actors.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(3000, -2100, 70))
    operator.set_actor_label('ArcadeRemoteTarget')
    for component in operator.get_components_by_class(unreal.PrimitiveComponent):
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    level.editor_request_begin_play()


def tests():
    global game, pc, ai, pawn, capsule, movement, mesh, camera, box, pallet, rig, dummy
    setup()
    while not editor.get_game_world():
        yield
    game = editor.get_game_world()
    named = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
    pawn, ai = named['ArcadeHuman'], named['ArcadeInputController']
    box, pallet, rig, dummy = (named[key] for key in ('ArcadeHeavyCargo', 'ArcadeEmptyPallet', 'ArcadeAGV', 'ArcadeRemoteTarget'))
    pc = unreal.GameplayStatics.get_player_controller(game, 0)
    capsule = pawn.get_component_by_class(unreal.CapsuleComponent)
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    mesh = next(p for p in pawn.get_components_by_class(unreal.SkeletalMeshComponent) if p.get_name() == 'CharacterMesh0')
    camera = pawn.get_component_by_class(unreal.CameraComponent)
    ai.possess(pawn)
    ai.set_control_rotation(unreal.Rotator())
    yield from wait(1.2)
    healthy()
    assert movement.get_class().get_name() == 'WarehousePlayerMovement', movement.get_class().get_name()
    check('native_character_movement_uses_swept_upright_capsule', component=movement.get_class().get_name(),
          capsule_simulating=False, mesh_simulating=mesh.is_simulating_physics())

    # Empty-hand walking, reversal, diagonal input and sprint cannot accumulate inertia.
    span = yield from drive('walk_immediate_350', 0, 1)
    assert span > .5, ('Walking animation does not move', span)
    yield from stop('walk_release_no_slide')
    yield from drive('reverse_immediate_350', 0, -1, seconds=.35)
    assert pawn.get_velocity().x < -348, pawn.get_velocity()
    yield from drive('reverse_again_immediate_350', 0, 1, seconds=.35)
    assert pawn.get_velocity().x > 348, pawn.get_velocity()
    yield from drive('diagonal_normalized_350', 1, 1, seconds=.35)
    yield from stop('diagonal_release_no_slide')
    yield from drive('sprint_immediate_500', 0, 1, sprint=True, target=500)
    yield from stop('sprint_release_no_slide')

    # The product's simulation clock uses fixed steps and bypasses t.MaxFPS.
    # Change only this unsaved world's clamps to test genuine 10/30/60 Hz
    # movement integration, rather than pretending render throttling changes dt.
    settings = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WorldSettings)[0]
    old_limits = [settings.get_editor_property(key) for key in
                  ('min_undilated_frame_time', 'max_undilated_frame_time')]
    try:
        for hz in (10, 30, 60):
            for key in ('min_undilated_frame_time', 'max_undilated_frame_time'):
                settings.set_editor_property(key, 1.0/hz)
            relocate((-3500, -2800, 100))
            yield from wait(.25)
            before = pawn.get_actor_location()
            began = now
            yield from drive(f'walk_at_{hz}hz_simulation_step', 0, 1, seconds=.35)
            elapsed = now-began
            travelled = horizontal(pawn.get_actor_location()-before)
            assert abs(travelled-350*elapsed) < 2, (hz, elapsed, travelled)
            check('movement_displacement_uses_delta_time', simulation_hz=hz,
                  elapsed_sim_seconds=elapsed, distance_cm=travelled, expected_cm=350*elapsed)
            yield from stop(f'no_slide_at_{hz}hz')
            yield from drive(f'sprint_at_{hz}hz_simulation_step', 0, 1, sprint=True, seconds=.35, target=500)
            yield from stop(f'sprint_no_slide_at_{hz}hz')
    finally:
        for key, value in zip(('min_undilated_frame_time', 'max_undilated_frame_time'), old_limits):
            settings.set_editor_property(key, value)

    enter('external_impulses')
    origin = pawn.get_actor_location()
    movement.add_impulse(unreal.Vector(200000, 100000, 600000), True)
    movement.add_force(unreal.Vector(-2000000, 0, 1500000))
    yield from wait(.3)
    healthy()
    assert (pawn.get_actor_location()-origin).length() < 1
    check('external_impulse_and_force_do_not_knock_down_or_slide_player')

    # Heavy freight uses its real mass again when released; hand carrying is weight independent.
    enter('heavy_pickup')
    relocate((-3000, -2000, 100))
    pawn.set_locomotion_input(False, False)
    set_object(box, (-2860, -2000, 16))
    yield from wait(.3)
    assert pawn.try_pickup_cargo(box), '1000 kg hand pickup rejected'
    yield from wait(.8)
    assert pawn.get_held_cargo() == box and pawn.get_carry_blend() > .8
    check('heavy_pickup_accepts_true_1000kg_cargo', configured_mass_kg=box.get_editor_property('gross_mass_kg'))
    yield from drive('heavy_carry_walk_same_350', 0, 1, seconds=.65)
    assert pawn.get_held_cargo() == box
    yield from drive('heavy_carry_sprint_same_500', 0, 1, sprint=True, seconds=.55, target=500)
    assert pawn.get_held_cargo() == box
    yield from stop('heavy_carry_release_input_no_slide')
    pawn.drop_cargo()
    assert pawn.get_held_cargo() is None
    enter('heavy_drop')
    peak = 0.0
    began = now
    while now-began < 2:
        healthy()
        centre, extent = box.get_actor_bounds(False)
        assert centre.z-extent.z > -.8, 'Released heavy box passed through floor'
        peak = max(peak, body(box).get_physics_linear_velocity().length())
        yield
    ground_rest(box, 1000)
    assert peak < 1200, ('Heavy drop exploded', peak)
    check('heavy_release_preserves_mass_gravity_support_without_explosion', mass_kg=body(box).get_mass(), peak_speed_cm_s=peak)

    enter('pallet_pickup')
    relocate((-3000, -1200, 100))
    set_object(pallet, (-2850, -1200, 8))
    yield from wait(.3)
    assert pawn.try_pickup_pallet(pallet), 'Empty pallet pickup rejected'
    yield from wait(.8)
    assert pawn.get_held_cargo() == pallet
    yield from drive('pallet_carry_same_350', 0, 1, seconds=.4)
    yield from stop('pallet_no_slide')
    pawn.drop_cargo()
    yield from wait(2)
    ground_rest(pallet, 25)
    check('empty_pallet_carry_and_release_restore_25kg_physics')

    enter('loaded_pallet_group')
    relocate((-3000, -1200, 100))
    set_object(pallet, (-2850, -1200, 8))
    set_object(box, (-2850, -1200, 31))
    yield from wait(.6)
    strength = named['ArcadeStrength']
    assert box in strength.get_supported_actors(pallet, True), 'Heavy box is not supported by the pallet'
    relative = box.get_actor_location()-pallet.get_actor_location()
    assert pawn.try_pickup_pallet(pallet), 'Loaded pallet pickup rejected'
    yield from wait(.7)
    held_group = pawn.get_held_cargo_group()
    assert box in held_group, ('Supported cargo excluded from carry group', held_group)
    assert not body(box).is_simulating_physics() and not body(pallet).is_simulating_physics()
    yield from drive('loaded_1025kg_pallet_group_same_350', 0, 1, seconds=.4)
    assert pawn.get_held_cargo() == pallet and box in pawn.get_held_cargo_group()
    actual_relative = box.get_actor_location()-pallet.get_actor_location()
    assert abs(actual_relative.length()-relative.length()) < 2, ('Loaded pallet group separated', relative, actual_relative)
    yield from stop('loaded_pallet_group_no_slide')
    pawn.drop_cargo()
    yield from wait(2)
    ground_rest(pallet, 25)
    assert body(box).is_simulating_physics() and abs(body(box).get_mass()-1000) < .2
    assert body(box).is_gravity_enabled() and box in strength.get_supported_actors(pallet, True)
    check('loaded_pallet_group_preserves_support_and_25_plus_1000kg_on_release',
          pallet_mass_kg=body(pallet).get_mass(), cargo_mass_kg=body(box).get_mass())

    enter('jump')
    relocate((-2400, 500, 100))
    pawn.set_locomotion_input(False, False)
    yield from wait(.3)
    origin = pawn.get_actor_location()
    pawn.do_jump_start()
    heights = []
    began = now
    while now-began < 1.6:
        heights.append(pawn.get_actor_location().z-origin.z)
        healthy()
        yield
    pawn.do_jump_end()
    assert max(heights) > 30 and max(heights) < 120, max(heights)
    assert abs(pawn.get_actor_location().z-origin.z) < 2
    check('jump_returns_to_floor_without_capsule_or_pose_collapse', apex_cm=max(heights))

    enter('airborne_input_lock')
    pawn.do_jump_start()
    yield from frames(2)
    assert pawn.get_actor_location().z > origin.z+5
    ai.set_ignore_move_input(True)
    yield from wait(1.1)
    ai.set_ignore_move_input(False)
    pawn.do_jump_end()
    assert abs(pawn.get_actor_location().z-origin.z) < 2
    assert movement.is_moving_on_ground()
    check('input_lock_does_not_suspend_midair_gravity')

    enter('crouch')
    standing_height = capsule.get_unscaled_capsule_half_height()
    standing_camera = camera.get_world_location().z
    pawn.set_locomotion_input(False, True)
    yield from wait(.5)
    assert capsule.get_unscaled_capsule_half_height() < standing_height-20
    assert camera.get_world_location().z < standing_camera-20
    ceiling = named['ArcadeCeiling']
    ceiling.set_actor_location(pawn.get_actor_location()+unreal.Vector(0, 0, 85), False, True)
    pawn.set_locomotion_input(False, False)
    yield from wait(.4)
    assert capsule.get_unscaled_capsule_half_height() < standing_height-20, 'Stood through ceiling'
    ceiling.set_actor_location(unreal.Vector(4500, 3000, 150), False, True)
    pawn.set_locomotion_input(False, False)
    yield from wait(.5)
    assert abs(capsule.get_unscaled_capsule_half_height()-standing_height) < .1
    check('crouch_lowers_camera_and_respects_low_ceiling_then_restores')

    enter('wall')
    relocate((300, 0, 100))
    yield from wait(.2)
    yield from wait(1.7, lambda: pawn.do_move(0, 1))
    healthy()
    assert pawn.get_actor_location().x+capsule.get_scaled_capsule_radius() <= 680.5
    assert pawn.get_actor_location().x > 600, pawn.get_actor_location()
    check('upright_player_capsule_sweeps_against_real_static_wall')
    enter('ramp')
    relocate((-650, 1800, 100))
    yield from wait(.2)
    yield from wait(2, lambda: pawn.do_move(0, 1))
    assert pawn.get_actor_location().z > 170, pawn.get_actor_location()
    healthy()
    check('engine_floor_detection_walks_up_real_ramp', position=str(pawn.get_actor_location()))

    # The normal controller retains the camera and interaction entry points.
    enter('camera')
    ai.un_possess()
    pc.possess(pawn)
    pc.set_editor_property('mouse_sensitivity', 1.0)
    pc.set_ignore_move_input(False)
    pc.set_ignore_look_input(False)
    relocate((-2400, -2000, 100))
    yield from wait(.2)
    camera_results = []
    # The saved project enables a legacy yaw scalar in addition to sensitivity.
    # Exercise a true 180-degree turn without changing the user's sensitivity.
    yaw_scale = pc.get_deprecated_input_yaw_scale() if unreal.InputSettings.get_input_settings().get_editor_property('bEnableLegacyInputScales') else 1.0
    for yaw_delta in (180, -180, 180, 180):
        previous = pc.get_control_rotation().yaw
        pawn.do_aim(yaw_delta/yaw_scale, 0)
        yield
        control_yaw = pc.get_control_rotation().yaw
        view_yaw = camera.get_world_rotation().yaw
        assert difference_angle(control_yaw, previous+yaw_delta) < .2, (yaw_delta, previous, control_yaw)
        assert difference_angle(view_yaw, control_yaw) < .2, ('Camera yaw lag', view_yaw, control_yaw)
        assert difference_angle(pawn.get_actor_rotation().yaw, control_yaw) < .2, 'Body yaw lag'
        healthy()
        camera_results.append(dict(delta=yaw_delta, control=control_yaw, camera=view_yaw))
    check('mouse_aim_180_degree_reversals_have_no_camera_lag', samples=camera_results)
    pc.toggle_observer_view()
    yield from frames(2)
    assert pc.is_observer_view()
    pc.set_layout_editing(False)
    pc.toggle_observer_view()
    yield from frames(2)
    assert not pc.is_observer_view() and not pc.is_move_input_ignored()
    check('observer_f1_native_api_roundtrip_returns_control')

    enter('remote_interaction')
    relocate((3000, -2020, 100), 90)
    yield from wait(.2)
    eye = camera.get_world_location()
    target = rig.get_actor_location()-rig.get_actor_forward_vector()*45+unreal.Vector(0, 0, 70)
    pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye, target))
    yield from wait(.3)
    assert pawn.get_interaction_target() == rig, ('Remote target not visible', pawn.get_interaction_target())
    assert pawn.begin_remote_control(rig)
    assert pawn.get_remote_forklift() == rig
    yield from frames(2)
    pawn.end_remote_control()
    assert not pawn.get_remote_forklift()
    check('remote_g_native_api_connects_and_disconnects_with_same_player')

    enter('agv_real_collision')
    assert rig.empty_travel_speed_cm == 180
    assert abs(rig.get_physical_mass_kg()-1000) < .2
    assert rig.root_component.is_simulating_physics()
    assert rig.begin_remote_control(dummy)
    first = rig.get_actor_location()
    speeds = []
    began = now
    while now-began < 7:
        rig.set_remote_input(1, 0, 0)
        speeds.append(abs(rig.current_speed_cm))
        yield
    rig.set_remote_input(0, 0, 0)
    rig.end_remote_control(dummy)
    centre, extent, _ = unreal.SystemLibrary.get_component_bounds(rig.root_component)
    assert (rig.get_actor_location()-first).length() > 150
    assert max(speeds) > 100 and max(speeds) < 200
    assert centre.x+extent.x < 3985, ('Physical AGV passed through wall', centre, extent)
    assert abs(rig.get_physical_mass_kg()-1000) < .2 and rig.root_component.is_simulating_physics()
    check('agv_keeps_180cm_s_target_1000kg_body_and_real_static_collision',
          maximum_speed_cm_s=max(speeds), moved_cm=(rig.get_actor_location()-first).length(),
          body_front_x_cm=centre.x+extent.x, wall_front_x_cm=3980)


def finalize():
    global finished, error
    if finished:
        return
    finished = True
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    try:
        unchanged = all(Path(path).is_file() and hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest
                        for path, digest in source_hashes.items())
        assert unchanged, 'A tested vehicle/sensor source or original BP/map changed during the test'
        report['vehicle_sensor_sources_and_original_bp_map_unchanged_during_test'] = True
    except Exception:
        error = (error or '')+'\n'+traceback.format_exc()
    report.update(passed=not error, error=error, phase=phase, wall_seconds=round(time.monotonic()-start_wall, 3))
    (OUT/'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('ARCADE_PLAYER_PIE_'+('FAILED' if error else 'PASSED'), error or str(OUT/'Report.json'))
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    global busy, game, now, cleaning, error
    if busy or finished:
        return
    busy = True
    try:
        if cleaning:
            if not editor.get_game_world():
                finalize()
            return
        assert time.monotonic()-start_wall < 240, ('Arcade player test timed out', phase)
        active = editor.get_game_world()
        if active:
            now = unreal.GameplayStatics.get_time_seconds(active)
        if pawn:
            report['recent_samples'].append(dict(phase=phase, time=now,
                position=str(pawn.get_actor_location()), velocity=str(pawn.get_velocity())))
            report['recent_samples'] = report['recent_samples'][-20:]
        next(scenario)
    except StopIteration:
        cleaning = True
        level.editor_request_end_play()
    except Exception:
        error = traceback.format_exc()
        scenario.close()  # Restore temporary dt clamps if a generator assertion failed.
        cleaning = True
        level.editor_request_end_play()
    finally:
        busy = False


scenario = tests()
handle = unreal.register_slate_post_tick_callback(tick)
