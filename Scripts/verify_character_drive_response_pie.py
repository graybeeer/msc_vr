"""Transient Chaos PIE checks for human gait/braking and native AGV response.

Uses the real player Blueprint and its existing animation assets. Measures bone
poses, not rendered pixels. A collider-free TargetPoint sends remote commands;
this checks the vehicle actuator path, not OS key injection or smartphone UI.
Never saves. -DriveChecksOnly skips the separate human fixture.
"""
import os
import sys
import time
import traceback
import unreal

sys.path.insert(0, os.path.dirname(__file__))
from apply_real_world_scale import fit

DRIVE_ONLY = '-drivechecksonly' in unreal.SystemLibrary.get_command_line().lower()
RENDER_CAPTURE = '-responsecapture' in unreal.SystemLibrary.get_command_line().lower()
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
perf = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
player_class = unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
settings.set_editor_property('default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.set_actor_scale3d(unreal.Vector(80, 80, .2))
if RENDER_CAPTURE:
    actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(pitch=-45, yaw=30))
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, -250, 125))
vehicle = actors.spawn_actor_from_class(unreal.WarehouseForklift, unreal.Vector())
vehicle.set_actor_label('ResponseAGV')
vehicle.set_editor_property('auto_start', False)
vehicle.set_editor_property('autonomous_mode', False)
operator = actors.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, -220, 75))
operator.set_actor_label('ResponseOperator')
for component in operator.get_components_by_class(unreal.PrimitiveComponent):
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
if not DRIVE_ONLY:
    human = actors.spawn_actor_from_class(player_class, unreal.Vector(1800, 1200, 125))
    human.set_actor_label('ResponseHuman')
    human.set_editor_property('auto_possess_player', unreal.AutoReceiveInput.DISABLED)
    human.set_editor_property('auto_possess_ai', unreal.AutoPossessAI.DISABLED)
    human.set_editor_property('ai_controller_class', None)
    parcel = actors.spawn_actor_from_class(unreal.WarehouseCargo, unreal.Vector(1850, 1440, 10.25))
    parcel.set_actor_label('ResponseParcel')
    parcel.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1'))
    parcel.set_gross_mass_kg(12)
    fit(parcel, (30, 24, 20), (1850, 1440, 10.25))

began = time.monotonic()
phase = 'start'
at = now = 0
game = person = rig = dummy = box = None
source = first_person = anim = capsule = chassis = None
origin = None
samples = []
feet = []
stop_time = reach_time = None
pivot_start = yaw_start = None

def finish():
    if rig and dummy:
        rig.end_remote_control(dummy)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def enter(next_phase):
    global phase, at, origin, samples, feet, stop_time, reach_time
    phase, at = next_phase, now
    vehicle_phase = next_phase.startswith(('drive_', 'forward_', 'reverse_', 'remote_', 'pivot_', 'turn_'))
    origin = rig.get_actor_location() if vehicle_phase or not person else person.get_actor_location()
    samples, feet = [], []
    stop_time = reach_time = None

def sample_gait():
    feet.append(source.get_socket_transform('foot_l', unreal.RelativeTransformSpace.RTS_COMPONENT).translation.x)
    # The first-person copy must also refresh while its world source is hidden.
    a = source.get_socket_transform('foot_l', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    b = first_person.get_socket_transform('foot_l', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    assert (a - b).length() < 2, ('First-person copy stopped following the gait', a, b)

def check_gait(marker, minimum_speed):
    assert max(samples) > minimum_speed, ('Movement speed', samples[-10:])
    assert anim.get_editor_property('ShouldMove'), 'Walking animation transition is false while moving'
    assert max(feet) - min(feet) > 10, ('Foot pose did not cycle', min(feet), max(feet))
    assert capsule.is_simulating_physics() and abs(capsule.get_mass() - 80) < .05
    print(marker, 'speed_cm_s', max(samples), 'foot_stride_cm', max(feet) - min(feet))

def check_stop(marker, limit_time, limit_distance):
    distance = (person.get_actor_location() - origin).length()
    assert stop_time is not None and stop_time < limit_time, ('Human stop time', stop_time)
    assert distance < limit_distance, ('Human stopping distance', distance)
    assert person.get_velocity().length() < 3
    assert not anim.get_editor_property('ShouldMove'), 'Stopped character still walks'
    print(marker, 'stop_seconds', stop_time, 'distance_cm', distance)

def check_remote_fade():
    operator_character = unreal.GameplayStatics.get_player_character(game, 0)
    parts = {component.get_name(): component for component in operator_character.get_components_by_class(unreal.MeshComponent)}
    names = ('TwoHandCarryMesh', 'RemoteOperatorWorldMesh', 'RemotePhone', 'RemotePhoneScreen', 'RemoteWorldPhone', 'RemoteWorldPhoneScreen')
    expected = {name: [parts[name].get_material(i) for i in range(parts[name].get_num_materials())] for name in names if 'Phone' in name}
    expected['TwoHandCarryMesh'] = [parts['First Person Mesh'].get_material(i) for i in range(parts['First Person Mesh'].get_num_materials())]
    expected['RemoteOperatorWorldMesh'] = [parts['CharacterMesh0'].get_material(i) for i in range(parts['CharacterMesh0'].get_num_materials())]
    for name in ('RemotePhone', 'RemoteWorldPhone'):
        assert not parts[name].is_visible() and not parts[name].is_active(), ('Phone active outside remote control', name)
    assert operator_character.begin_remote_control(rig), ('Real character remote connection failed', operator_character.get_actor_transform())
    for name in names:
        component = parts[name]
        assert component.is_visible(), ('Remote render component hidden', name)
        for i in range(component.get_num_materials()):
            material = component.get_material(i)
            assert isinstance(material, unreal.MaterialInstanceDynamic), ('Fade not installed', name, material)
            assert abs(material.get_scalar_parameter_value('RemoteCharacterOpacity') - .3) < .001, name
    assert parts['RemotePhone'].is_active() and parts['RemoteWorldPhone'].is_active()
    operator_character.end_remote_control()
    for name in names:
        assert [parts[name].get_material(i) for i in range(parts[name].get_num_materials())] == expected[name], ('Original slots not restored', name)
    for name in ('RemotePhone', 'RemotePhoneScreen', 'RemoteWorldPhone', 'RemoteWorldPhoneScreen'):
        assert not parts[name].is_visible(), ('Phone remained visible after disconnect', name)
    assert not parts['RemotePhone'].is_active() and not parts['RemoteWorldPhone'].is_active()
    print('REMOTE_CHARACTER_PHONE_FADE_PASSED', '0.3 opacity on both body/phone views; exact material restoration and inactive hidden phone after disconnect')

def begin_response_checks():
    if DRIVE_ONLY:
        assert rig.begin_remote_control(dummy)
        enter('remote_check')
    else:
        enter('walk')

def tick(dt):
    global now, phase, game, person, rig, dummy, box, source, first_person, anim, capsule, chassis
    global stop_time, reach_time, pivot_start, yaw_start
    try:
        assert time.monotonic() - began < 210, ('Response PIE timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'wait_world'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if phase == 'wait_world':
            named = {actor.get_actor_label(): actor for actor in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            rig, dummy = named['ResponseAGV'], named['ResponseOperator']
            chassis = rig.root_component
            unreal.GameplayStatics.get_player_controller(game, 0).set_control_rotation(unreal.Rotator(yaw=90))
            if not DRIVE_ONLY:
                person, box = named['ResponseHuman'], named['ResponseParcel']
                parts = {mesh.get_name(): mesh for mesh in person.get_components_by_class(unreal.SkeletalMeshComponent)}
                source, first_person = parts['CharacterMesh0'], parts['First Person Mesh']
                anim = source.get_anim_instance()
                capsule = person.get_component_by_class(unreal.CapsuleComponent)
            enter('settle')
            return
        age = now - at
        if person:
            assert not person.is_physical_ragdoll(), ('Normal movement ragdolled', phase)
        if phase == 'settle':
            if age < 1.5:
                return
            assert rig.empty_travel_speed_cm == 180
            assert abs(rig.get_physical_mass_kg() - 1000) < .1
            operator_character = unreal.GameplayStatics.get_player_character(game, 0)
            eye = operator_character.get_component_by_class(unreal.CameraComponent).get_world_location()
            target = rig.get_actor_location() - rig.get_actor_forward_vector() * 45 + unreal.Vector(0, 0, 70)
            unreal.GameplayStatics.get_player_controller(game, 0).set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye, target))
            enter('phone_aim')
            return
        if phase == 'phone_aim':
            if age < .4:
                return
            operator_character = unreal.GameplayStatics.get_player_character(game, 0)
            camera = operator_character.get_component_by_class(unreal.CameraComponent)
            print('REMOTE_FADE_SIGHT_DIAGNOSTIC', camera.get_world_location(), camera.get_world_rotation(), rig.get_actor_transform())
            check_remote_fade()
            if RENDER_CAPTURE:
                assert operator_character.begin_remote_control(rig)
                enter('phone_render')
            else:
                begin_response_checks()
            return
        if phase == 'phone_render':
            if age < 2:
                return
            path = os.path.join(unreal.Paths.project_dir(), 'Saved/Testing/RemotePhoneFade.png').replace('\\', '/')
            os.makedirs(os.path.dirname(path), exist_ok=True)
            unreal.SystemLibrary.execute_console_command(game, 'HighResShot 960x540 filename=' + path)
            enter('phone_capture_wait')
            return
        if phase == 'phone_capture_wait':
            if age < 1:
                return
            path = os.path.join(unreal.Paths.project_dir(), 'Saved/Testing/RemotePhoneFade.png')
            assert os.path.isfile(path), ('Remote fade screenshot missing', path)
            print('REMOTE_FADE_RENDER_CAPTURED', path)
            unreal.GameplayStatics.get_player_character(game, 0).end_remote_control()
            begin_response_checks()
            return
        if phase == 'walk':
            person.add_movement_input(unreal.Vector(0, 1, 0), 1, True)
            samples.append(person.get_velocity().length())
            sample_gait()
            if age < 1.4:
                return
            check_gait('CHARACTER_WALK_ANIMATION_PASSED', 185)
            enter('walk_stop')
            return
        if phase in ('walk_stop', 'sprint_stop'):
            if person.get_velocity().length() < 10 and stop_time is None:
                stop_time = age
            if age < 1:
                return
            check_stop('CHARACTER_' + phase.upper() + '_PASSED', .6 if phase == 'walk_stop' else .95, 50 if phase == 'walk_stop' else 125)
            if phase == 'walk_stop':
                assert person.try_pickup_cargo(box), 'Pickup fixture out of reach'
                enter('carry_settle')
            else:
                person.do_jump_start()
                enter('jump')
            return
        if phase == 'carry_settle':
            if age < .8:
                return
            enter('carry_walk')
            return
        if phase == 'carry_walk':
            person.add_movement_input(unreal.Vector(0, 1, 0), 1, True)
            samples.append(person.get_velocity().length())
            sample_gait()
            assert person.get_held_cargo() == box, 'Carry slipped during normal walk'
            if age < 1:
                return
            check_gait('CHARACTER_CARRY_GAIT_PASSED', 100)
            carry = next(mesh for mesh in person.get_components_by_class(unreal.SkeletalMeshComponent) if mesh.get_name() == 'TwoHandCarryMesh')
            for i, bone in enumerate(('hand_l', 'hand_r')):
                assert (carry.get_socket_location(bone) - person.get_carry_hand_location(i)).length() < 6, ('Carry hand contact', bone)
            body = box.get_component_by_class(unreal.StaticMeshComponent)
            before = body.get_physics_linear_velocity()
            person.drop_cargo()
            assert body.is_simulating_physics() and (body.get_physics_linear_velocity() - before).length() < .001
            person.set_locomotion_input(True, False)
            enter('sprint')
            return
        if phase == 'sprint':
            person.add_movement_input(unreal.Vector(0, 1, 0), 1, True)
            samples.append(person.get_velocity().length())
            sample_gait()
            if age < 2:
                return
            check_gait('CHARACTER_SPRINT_GAIT_PASSED', 330)
            before = capsule.get_physics_linear_velocity()
            person.set_locomotion_input(False, False)
            assert (capsule.get_physics_linear_velocity() - before).length() < .001, 'Input reset momentum'
            enter('sprint_stop')
            return
        if phase == 'jump':
            samples.append(person.get_actor_location().z - origin.z)
            if age < 2:
                return
            assert max(samples) > 35, ('Physical jump', max(samples))
            print('CHARACTER_JUMP_PASSED', max(samples))
            person.set_locomotion_input(False, True)
            enter('crouch')
            return
        if phase == 'crouch':
            if age < 1:
                return
            assert capsule.get_unscaled_capsule_half_height() < 59 and person.get_actor_location().z < origin.z - 25
            person.set_locomotion_input(False, False)
            enter('stand')
            return
        if phase == 'stand':
            if capsule.get_unscaled_capsule_half_height() < 95:
                assert age < 4, 'Cannot stand back up'
                return
            print('CHARACTER_PHYSICAL_LOCOMOTION_PASSED', 'walk/carry/run gait, stopping, jump, crouch; actual 80kg body')
            assert rig.begin_remote_control(dummy)
            enter('remote_check')
            return
        if phase == 'remote_check':
            rig.set_remote_input(0, 0, 0)
            if age < 1.3:
                return
            enter('drive_forward')
            return
        if phase in ('drive_forward', 'drive_reverse'):
            sign = 1 if phase == 'drive_forward' else -1
            rig.set_remote_input(sign, 0, 0)
            speed = sign * rig.current_speed_cm
            samples.append((age, speed))
            if speed > 171 and reach_time is None:
                reach_time = age
            if age < 4:
                return
            steady = [speed for age, speed in samples if age > 3]
            wheel = next(c for c in rig.get_components_by_class(unreal.StaticMeshComponent) if c.get_name() == 'DriveWheel')
            print('AGV_DRIVE_DIAGNOSTIC', phase, 'peak', max(speed for _, speed in samples), 'samples', samples[::max(1, len(samples)//12)], 'linear', chassis.get_physics_linear_velocity(), 'wheel_spin', wheel.get_physics_angular_velocity_in_radians(), 'wheel_position', wheel.get_world_location(), 'chassis_rotation', rig.get_actor_rotation())
            assert reach_time is not None and reach_time < 2.5, ('Slow AGV acceleration', phase, reach_time, rig.status)
            assert 175 < sum(steady) / len(steady) < 185, ('1.8m/s cruise target', phase, steady[-10:])
            assert max(speed for _, speed in samples) < 195, ('Speed control overshoot', phase, samples[-10:])
            print('AGV_' + phase.upper() + '_PASSED', 'seconds_to_95pct', reach_time, 'cruise_cm_s', sum(steady) / len(steady))
            before = chassis.get_physics_linear_velocity()
            rig.set_remote_input(0, 0, 0)
            assert (chassis.get_physics_linear_velocity() - before).length() < .001, 'Brake command reset velocity'
            enter('forward_brake' if sign > 0 else 'reverse_brake')
            return
        if phase in ('forward_brake', 'reverse_brake'):
            rig.set_remote_input(0, 0, 0)
            if chassis.get_physics_linear_velocity().length() < 5 and stop_time is None:
                stop_time = age
            if age < 1.8:
                return
            distance = (rig.get_actor_location() - origin).length()
            assert stop_time is not None and stop_time < 1.3, ('AGV brake time', phase, stop_time)
            assert distance < 135, ('AGV stopping distance', distance)
            print('AGV_' + phase.upper() + '_PASSED', 'stop_seconds', stop_time, 'distance_cm', distance)
            if phase == 'forward_brake':
                enter('drive_reverse')
            else:
                wheels = {c.get_name(): c for c in rig.get_components_by_class(unreal.StaticMeshComponent)}
                pivot_start = (wheels['LoadWheelL'].get_world_location() + wheels['LoadWheelR'].get_world_location()) * .5
                yaw_start = rig.get_actor_rotation().yaw
                enter('pivot_turn')
            return
        if phase in ('pivot_turn', 'pivot_left'):
            sign = 1 if phase == 'pivot_turn' else -1
            rig.set_remote_input(0, sign, 0)
            if age < 5:
                return
            wheels = {c.get_name(): c for c in rig.get_components_by_class(unreal.StaticMeshComponent)}
            pivot = (wheels['LoadWheelL'].get_world_location() + wheels['LoadWheelR'].get_world_location()) * .5
            yaw = (rig.get_actor_rotation().yaw - yaw_start + 180) % 360 - 180
            print('AGV_PIVOT_DIAGNOSTIC', 'yaw_degrees', yaw, 'pivot_drift_cm', (pivot - pivot_start).length(), 'status', rig.status, 'wheel_axle', wheels['DriveWheel'].get_right_vector(), 'body_yaw', rig.get_actor_rotation().yaw)
            assert sign * yaw > 45, ('Stationary turn direction/response', phase, yaw, rig.status)
            assert (pivot - pivot_start).length() < 45, 'Vehicle travelled instead of turning about its load wheels'
            rig.set_remote_input(0, 0, 0)
            enter('turn_brake' if sign > 0 else 'left_brake')
            return
        if phase in ('turn_brake', 'left_brake'):
            rig.set_remote_input(0, 0, 0)
            if age < 1.5:
                return
            assert abs(chassis.get_physics_angular_velocity_in_degrees().z) < 3, 'Turn did not brake'
            if phase == 'turn_brake':
                wheels = {c.get_name(): c for c in rig.get_components_by_class(unreal.StaticMeshComponent)}
                pivot_start = (wheels['LoadWheelL'].get_world_location() + wheels['LoadWheelR'].get_world_location()) * .5
                yaw_start = rig.get_actor_rotation().yaw
                enter('pivot_left')
                return
            print('CHARACTER_DRIVE_RESPONSE_PIE_PASSED', 'drive/fade only' if DRIVE_ONLY else 'human bone poses and drive/fade',
                  'native finite forces/torques; 1.8m/s both directions; stationary left/right motor turn; rendered phone capture' if RENDER_CAPTURE else 'native finite forces/torques; 1.8m/s both directions; stationary left/right motor turn; no rendered image claim')
            finish()
    except Exception:
        print('CHARACTER_DRIVE_RESPONSE_PIE_FAILED', 'phase', phase, traceback.format_exc())
        finish()

handle = unreal.register_slate_post_tick_callback(tick)
