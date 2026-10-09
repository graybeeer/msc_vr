"""Real-time PIE integration: two physical L1 jobs and smartphone remote controls.

Test fixtures and runtime repositioning are transient. This script never saves.
Pass -RemoteChecksOnly to stop both autonomous jobs before unloading and exercise
the remote phases independently. That mode never reports completed L1 jobs.
"""
import os
import sys
import time
import traceback
import unreal

sys.path.insert(0, os.path.dirname(__file__))
from apply_real_world_scale import fit
from configure_dual_forklifts import FLEET, LEVEL

REMOTE_CHECKS_ONLY = '-remotechecksonly' in unreal.SystemLibrary.get_command_line().lower()

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level(LEVEL)
fixture = actors.spawn_actor_from_class(unreal.WarehouseCargo, unreal.Vector(600, -1700, 20))
fixture.set_actor_label('RemoteTestCarton')
fixture.set_editor_property('is_spatially_loaded', False)
fixture.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1'))
fixture.set_gross_mass_kg(5)
fit(fixture, (20, 18, 18), (600, -1700, 9.1))
obstacle = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(8000, 8000, 40))
obstacle.set_actor_label('RemoteTestObstacle')
obstacle.set_editor_property('is_spatially_loaded', False)
obstacle.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
obstacle.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
obstacle.static_mesh_component.set_collision_profile_name('BlockAll')
obstacle.set_actor_scale3d(unreal.Vector(.6, 1.6, .8))
human = actors.spawn_actor_from_class(unreal.WarehouseWorker, unreal.Vector(8000, 7800, 96))
human.set_actor_label('RemoteTestWorker')
human.set_editor_property('is_spatially_loaded', False)
assert human.copy_player_appearance(unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'))
test_pallet = actors.spawn_actor_from_class(unreal.WarehousePallet, unreal.Vector(1700, -1650, .1))
test_pallet.set_actor_label('RemoteTestPallet')
test_pallet.set_editor_property('is_spatially_loaded', False)
test_load = actors.spawn_actor_from_class(unreal.WarehouseCargo, unreal.Vector(1700, -1650, 25.2))
test_load.set_actor_label('RemoteTestLoad')
test_load.set_editor_property('is_spatially_loaded', False)
test_load.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1'))
test_load.set_gross_mass_kg(12)
fit(test_load, (30, 24, 20), (1700, -1650, 25.2))
strength = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'WH_DamageSystem')
specs = list(strength.get_editor_property('objects'))
for actor, mass, capacity in ((test_pallet, 25, 1500), (test_load, 12, 120)):
    specs.append(unreal.WarehouseStrength(name=actor.get_actor_label(), members=[actor],
        physics_meshes=[actor.get_component_by_class(unreal.StaticMeshComponent).static_mesh],
        failure=unreal.WarehouseFailure.CRUSH, mass_kg=mass, rated_load_kg=capacity,
        impact_yield_j=1000, impact_failure_j=5000, overload_seconds=10))
strength.set_editor_property('objects', specs)

started = time.monotonic()
phase = 0
at = 0
named = None
fleet = []
pc = None
pawn = None
seen = [set(), set()]
last_state = [None, None]
initial = []
before = []
remote = None
lift_before = 0
remote_cycles = 0
obstacle_start = None
interrupted_job = None
interrupted_pending = None
other_before = None
deadman_at = 0
load_before = None


def finish():
    if pawn:
        pawn.set_actor_tick_enabled(True)
        pawn.end_remote_control()
    performance.set_editor_property('bThrottleCPUWhenNotForeground', throttle)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def components(actor):
    return {c.get_name(): c for c in actor.get_components_by_class(unreal.SceneComponent)}


def look_at_vehicle(vehicle):
    # Stand alongside the chassis, outside the forward person-safety volume.
    body = components(vehicle)['Body']
    target = body.get_world_location() + vehicle.get_actor_forward_vector() * -55 + unreal.Vector(0, 0, 75)
    position = vehicle.get_actor_location() - vehicle.get_actor_right_vector() * 170 + vehicle.get_actor_forward_vector() * -40
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    movement.stop_movement_immediately()
    # Use the collision-tested Teleport API rather than forcing an actor-only move.
    # The capsule/base diagnostics distinguish overlap resolution from stale movement state.
    assert pawn.teleport(position + unreal.Vector(0, 0, 125), pawn.get_actor_rotation()), ('Operator capsule has no room', position)
    movement.stop_movement_immediately()
    camera = pawn.get_component_by_class(unreal.CameraComponent)
    rotation = unreal.MathLibrary.find_look_at_rotation(camera.get_world_location(), target)
    pc.set_control_rotation(rotation)
    camera.set_world_rotation(rotation, False, True)
    diagnose_operator('REMOTE_OPERATOR_POSITIONED')


def diagnose_operator(marker):
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    capsule = pawn.get_component_by_class(unreal.CapsuleComponent)
    center, extent, _ = unreal.SystemLibrary.get_component_bounds(capsule)
    print(marker, 'pawn', pawn.get_actor_transform(), 'capsule_center', center, 'capsule_extent', extent,
        'velocity', movement.get_editor_property('velocity'), 'movement_mode', movement.get_editor_property('movement_mode'),
        'attach_parent', pawn.get_attach_parent_actor())
    for actor in named.values():
        if actor == pawn:
            continue
        for part in actor.get_components_by_class(unreal.PrimitiveComponent):
            if part.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
                continue
            other_center, other_extent, _ = unreal.SystemLibrary.get_component_bounds(part)
            delta = other_center - center
            if all(abs(getattr(delta, axis)) < getattr(extent, axis) + getattr(other_extent, axis) - .1 for axis in 'xyz'):
                print('REMOTE_OPERATOR_AABB_CONTACT_CANDIDATE', actor.get_actor_label(), part.get_name(),
                    'center', other_center, 'extent', other_extent, 'profile', part.get_collision_profile_name())


def diagnose_pickup(box, marker):
    diagnose_operator(marker + '_OPERATOR')
    body = box.get_component_by_class(unreal.StaticMeshComponent)
    camera = pawn.get_component_by_class(unreal.CameraComponent)
    center, extent = box.get_actor_bounds(False)
    eye = camera.get_world_location()
    print(marker, 'box_pose', box.get_actor_transform(), 'bounds_center', center, 'bounds_extent', extent,
        'camera', eye, 'distance_cm', (center - eye).length(),
        'pawn_pose', pawn.get_actor_transform(), 'attach_parent', box.get_attach_parent_actor(),
        'held', pawn.has_held_cargo(), 'remote', pawn.get_remote_forklift(),
        'failed', named['WH_DamageSystem'].has_failed(box), 'mesh', body.static_mesh,
        'simulating', body.is_simulating_physics(), 'collision_profile', body.get_collision_profile_name(),
        'linear_velocity', body.get_physics_linear_velocity(),
        'angular_velocity', body.get_physics_angular_velocity_in_degrees())
    for actor in named.values():
        if actor == box:
            continue
        for part in actor.get_components_by_class(unreal.PrimitiveComponent):
            if part.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
                continue
            other_center, other_extent, _ = unreal.SystemLibrary.get_component_bounds(part)
            delta = other_center - center
            if all(abs(getattr(delta, axis)) < getattr(extent, axis) + getattr(other_extent, axis) - .1 for axis in 'xyz'):
                print('REMOTE_PICKUP_AABB_CONTACT_CANDIDATE', actor.get_actor_label(), part.get_name(),
                    'center', other_center, 'extent', other_extent, 'profile', part.get_collision_profile_name())


def diagnose_phone_pose():
    camera = pawn.get_component_by_class(unreal.CameraComponent)
    print('REMOTE_PHONE_POSE', 'pawn', pawn.get_actor_transform(), 'camera', camera.get_world_transform(),
        'phone', components(pawn)['RemotePhone'].get_world_transform(),
        'hand_targets', [pawn.get_carry_hand_location(index) for index in range(2)])
    # Public bone/socket and scene-transform getters only; no private movement state.
    for mesh in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
        print('REMOTE_PHONE_SKELETAL_TRANSFORM', mesh.get_name(), mesh.get_world_transform())
        for index, suffix in enumerate(('_l', '_r')):
            shoulder = mesh.get_socket_location(unreal.Name('upperarm' + suffix))
            elbow = mesh.get_socket_location(unreal.Name('lowerarm' + suffix))
            wrist = mesh.get_socket_location(unreal.Name('hand' + suffix))
            target = pawn.get_carry_hand_location(index)
            arm_length = (elbow - shoulder).length() + (wrist - elbow).length()
            target_distance = (target - shoulder).length()
            print('REMOTE_PHONE_ARM_REACH', mesh.get_name(), suffix,
                'shoulder', shoulder, 'elbow', elbow, 'wrist', wrist, 'target', target,
                'upper_length_cm', (elbow - shoulder).length(), 'forearm_length_cm', (wrist - elbow).length(),
                'reach_cm', arm_length, 'target_distance_cm', target_distance,
                'reach_margin_cm', arm_length - target_distance, 'wrist_error_cm', (target - wrist).length())


def tick(dt):
    global phase, at, named, fleet, pc, pawn, before, remote, lift_before, remote_cycles, obstacle_start, interrupted_job, interrupted_pending, other_before, deadman_at, load_before
    try:
        assert time.monotonic() - started < 900, 'Fleet/remote integration timeout'
        if phase == 0:
            if time.monotonic() - started < 3:
                return
            level.editor_request_begin_play()
            phase = 1
            return
        game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if named is None:
            if now < .15:
                return
            named = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            pawn = pc.get_controlled_pawn()
            fleet = [named[row[0]] for row in FLEET]
            print('DUAL_MAP_OBJECT_COUNTS', 'forklifts', len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseForklift)),
                'cartons including 2 fixtures', len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseCargo)),
                'pallets including 1 fixture', len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehousePallet)))
            initial.extend(v.get_actor_location() for v in fleet)
            pawn.set_actor_location(unreal.Vector(600, -1700, 125), False, True)
            diagnose_operator('REMOTE_OPERATOR_FIRST_PHYSICS_STATE')
            unreal.SystemLibrary.execute_console_command(game, 't.MaxFPS 60', pc)
            unreal.SystemLibrary.execute_console_command(game, 'Slate.bAllowThrottling 0', pc)
        for index, vehicle in enumerate(fleet):
            state = vehicle.get_editor_property('ai_state')
            seen[index].add(state)
            pallet = named[FLEET[index][1]]
            cargo = named[FLEET[index][2]]
            if state != last_state[index]:
                last_state[index] = state
                print('DUAL_AGV_STATE', index + 1, state, vehicle.get_editor_property('status'),
                    'pallet_pose', pallet.get_actor_transform(),
                    'pallet_angular_velocity_deg_s', pallet.get_component_by_class(unreal.StaticMeshComponent).get_physics_angular_velocity_in_degrees())
            if state == unreal.WarehouseAIState.FAULT:
                print('DUAL_AGV_FAULT_POSE', index + 1, 'vehicle', vehicle.get_actor_transform(),
                    'pallet', named[FLEET[index][1]].get_actor_transform(),
                    'cargo', named[FLEET[index][2]].get_actor_transform(),
                    'destination', vehicle.get_editor_property('active_job').destination)
            assert state != unreal.WarehouseAIState.FAULT, (index + 1, vehicle.get_editor_property('status'))
            assert abs(vehicle.get_actor_location().z) < 1, 'L1 job unexpectedly used another floor'
            assert pallet.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics(), 'Pallet became kinematic'
            assert cargo.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics(), 'Cargo became kinematic'
            assert not pallet.get_attach_parent_actor() and not cargo.get_attach_parent_actor(), 'Fork load became rigidly attached'
        if phase == 1:
            if now < 3:
                return
            assert all(v.get_editor_property('auto_start') and v.get_editor_property('powered') for v in fleet)
            assert fleet[0].get_editor_property('target_pallet') != fleet[1].get_editor_property('target_pallet')
            assert fleet[0].get_editor_property('charging_station') != fleet[1].get_editor_property('charging_station')
            for v in fleet:
                assert v.get_editor_property('charging_station').get_editor_property('assigned_vehicle') == v
                assert unreal.WarehouseAIState.SELF_CHECK in seen[fleet.index(v)], 'Self check was skipped'
            duplicate = unreal.WarehouseWorkOrder(job_id='CONFLICT-V01-V02', source_system='FLEET-CONFLICT-TEST',
                pallet=named[FLEET[0][1]], destination=unreal.Transform(location=unreal.Vector(1000, 250, 0), rotation=unreal.Rotator(yaw=90)))
            assert not fleet[1].submit_job(duplicate), 'Both AGVs accepted the same pallet'
            print('DUAL_AGV_SHARED_PALLET_REJECTED')
            phase = 2
            return
        if phase == 2:
            if not all((v.get_actor_location() - initial[i]).length() > 80 for i, v in enumerate(fleet)):
                assert now < 90, 'Both forklifts did not independently navigate'
                return
            print('DUAL_AGV_SIMULTANEOUS_NAVIGATION_PASSED', [v.get_actor_location() for v in fleet])
            remote = fleet[0]
            look_at_vehicle(remote)
            at = now
            phase = 14
            return
        if phase == 14:
            if now - at < .3:
                return
            look_at_vehicle(remote)
            interrupted_job = remote.get_editor_property('active_job').job_id
            interrupted_pending = [job.job_id for job in remote.get_editor_property('pending_jobs')]
            assert interrupted_job == FLEET[0][-1], 'The first job was not active before takeover'
            pawn.toggle_remote_control()
            assert pawn.get_remote_forklift() == remote and remote.is_remote_controlled()
            assert not fleet[1].is_remote_controlled()
            other_before = fleet[1].get_actor_location()
            at = now
            phase = 15
            return
        if phase == 15:
            if now - at < .5:
                return
            assert (fleet[1].get_actor_location() - other_before).length() > 1, 'Remote takeover paused the unrelated AGV'
            pawn.toggle_remote_control()
            assert not remote.is_remote_controlled() and not remote.get_editor_property('powered'), 'G release silently resumed autonomy'
            assert remote.get_editor_property('active_job').job_id == interrupted_job, 'Remote takeover discarded active job'
            assert [job.job_id for job in remote.get_editor_property('pending_jobs')] == interrupted_pending
            pawn.set_actor_location(unreal.Vector(600, -1700, 125), False, True)
            remote.toggle_power()
            assert remote.get_editor_property('ai_state') == unreal.WarehouseAIState.SELF_CHECK, 'E resume skipped self check'
            print('REMOTE_ACTIVE_JOB_PAUSE_RESUME_PASSED', interrupted_job, 'other AGV kept navigating; G stopped; E rechecked and replans')
            phase = 3
            return
        if phase == 3:
            if REMOTE_CHECKS_ONLY:
                for vehicle in fleet:
                    if vehicle.get_editor_property('powered'):
                        vehicle.toggle_power()
                print('REMOTE_ONLY_AUTONOMY_STOPPED', 'Skipping physical job completion checks; active/pending work orders retained',
                    [(v.get_editor_property('active_job').job_id, [job.job_id for job in v.get_editor_property('pending_jobs')]) for v in fleet])
            else:
                if not all(row[-1] in v.get_editor_property('completed_job_ids') for row, v in zip(FLEET, fleet)):
                    return
                for row in FLEET:
                    c, e = named[row[1]].get_actor_bounds(False)
                    assert abs(c.z - e.z) < 2 and abs(c.x - row[4]) < 3 and abs(c.y - row[7]) < 3, ('Wrong ground-floor unloading pose', c, e)
                print('DUAL_AGV_PHYSICAL_JOBS_PASSED', 'Both distinct L1 pallets unloaded; cargo/pallet physics stayed enabled')
            # Work orders are retained. Move only the test instance to a clear bay.
            for row, v in zip(FLEET, fleet):
                v.set_actor_location(unreal.Vector(row[4], row[5], 0), False, True)
                v.set_actor_rotation(unreal.Rotator(yaw=90), True)
            remote = fleet[0]
            look_at_vehicle(remote)
            box = named['RemoteTestCarton']
            box.set_actor_location(pawn.get_actor_location() + unreal.Vector(0, 45, -85), False, True)
            box.get_component_by_class(unreal.StaticMeshComponent).set_physics_linear_velocity(unreal.Vector())
            diagnose_pickup(box, 'REMOTE_PICKUP_FIXTURE_PLACED')
            at = now
            phase = 4
            return
        if phase == 4:
            if now - at < .3:
                return
            box = named['RemoteTestCarton']
            diagnose_pickup(box, 'REMOTE_PICKUP_FIXTURE_SETTLED')
            picked_up = pawn.try_pickup_cargo(box)
            print('REMOTE_PICKUP_TRY_RESULT', picked_up, 'held', pawn.has_held_cargo())
            assert picked_up and pawn.has_held_cargo(), 'Settled pickup fixture was not carryable; see pickup diagnostics'
            assert not pawn.begin_remote_control(remote), 'Hands carrying cargo allowed remote control'
            pawn.toggle_remote_control()
            assert not pawn.get_remote_forklift() and not remote.is_remote_controlled()
            pawn.drop_cargo()
            assert not pawn.has_held_cargo()
            print('REMOTE_HELD_CARGO_GUARD_PASSED')
            at = now
            phase = 5
            return
        if phase == 5:
            if now - at < .7:
                return
            look_at_vehicle(remote)
            pawn.toggle_remote_control()
            assert pawn.get_remote_forklift() == remote and remote.get_remote_operator() == pawn, 'G did not select looked-at forklift'
            parts = components(pawn)
            assert parts['RemotePhone'].is_visible() and parts['RemoteWorldPhone'].is_visible()
            assert parts['RemoteOperatorWorldMesh'].is_visible()
            assert parts['RemotePhone'].get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
            assert not pawn.try_pickup_cargo(named['RemoteTestCarton']), 'Remote control allowed cargo pickup'
            assert not fleet[1].is_remote_controlled(), 'Both forklifts entered remote mode'
            phone = parts['RemotePhone'].get_world_location()
            eye = pawn.get_component_by_class(unreal.CameraComponent).get_world_location()
            phone_center, phone_extent, _ = unreal.SystemLibrary.get_component_bounds(parts['RemotePhone'])
            assert phone_center.z + phone_extent.z < eye.z - 10, ('Phone top blocks eye-level view', phone_center, phone_extent, eye)
            remote.set_remote_input(0, 0, 0)
            at = now
            phase = 16
            return
        if phase == 16:
            remote.set_remote_input(0, 0, 0)
            if now - at < 1.25:
                return
            assert remote.is_remote_controlled() and remote.get_editor_property('powered'), 'Remote self check did not complete'
            assert pawn.get_carry_blend() > .99, 'Phone hand pose did not finish blending'
            diagnose_phone_pose()
            for mesh_name in ('TwoHandCarryMesh', 'RemoteOperatorWorldMesh'):
                mesh = components(pawn)[mesh_name]
                errors = [(mesh.get_socket_location(unreal.Name(bone)) - pawn.get_carry_hand_location(index)).length()
                    for index, bone in enumerate(('hand_l', 'hand_r'))]
                print('REMOTE_PHONE_HAND_IK_ERROR_CM', mesh_name, errors)
                assert max(errors) < 3, ('Phone hand target exceeds reach', mesh_name, errors)
            if '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower():
                unreal.SystemLibrary.execute_console_command(game, 'Shot showui filename=C:/msc_UnrealProject/msc_vr/Saved/RemotePhoneView.png', pc)
            # Isolate the vehicle API from the character's hardware keyboard poll.
            pawn.set_actor_tick_enabled(False)
            before = [v.get_actor_location() for v in fleet]
            remote.set_remote_input(-1, 0, 0)
            at = now
            phase = 6
            return
        if phase == 6:
            remote.set_remote_input(-1, 0, 0)
            if now - at < 2 or (remote.get_actor_location() - before[0]).length() <= 8:
                assert now - at < 15, 'Remote reverse input did not move selected AGV after self check'
                return
            assert (remote.get_actor_location() - before[0]).length() > 8, 'Remote reverse input did not move selected AGV'
            assert (fleet[1].get_actor_location() - before[1]).length() < .1, 'Remote input moved unrelated AGV'
            # Stop transmitting instead of sending brake, checking the .25s deadman.
            deadman_at = time.monotonic()
            phase = 19
            return
        if phase == 19:
            if time.monotonic() - deadman_at < .5:
                return
            assert abs(remote.get_editor_property('current_speed_cm')) < .1, 'Lost remote input did not brake the AGV'
            print('REMOTE_INPUT_DEADMAN_PASSED')
            lift_before = remote.get_fork_height_cm()
            remote.set_remote_input(0, 0, 1)
            at = now
            phase = 7
            return
        if phase == 7:
            remote.set_remote_input(0, 0, 1)
            if now - at < 1.5 or remote.get_fork_height_cm() <= lift_before + 5:
                assert now - at < 15, 'Remote lift did not raise forks'
                return
            assert remote.get_fork_height_cm() > lift_before + 5, 'Remote lift did not raise forks'
            remote.set_remote_input(0, 0, 0)
            pawn.toggle_remote_control()
            pawn.set_actor_tick_enabled(True)
            assert not pawn.get_remote_forklift() and not remote.is_remote_controlled()
            print('REMOTE_FIRST_AGV_DRIVE_LIFT_PASSED')
            at = now
            phase = 8
            return
        if phase == 8:
            if now - at < .25:
                return
            look_at_vehicle(remote)
            pawn.toggle_remote_control()
            assert pawn.get_remote_forklift() == remote
            pawn.toggle_remote_control()
            assert not pawn.get_remote_forklift() and not components(pawn)['RemotePhone'].is_visible()
            remote_cycles += 1
            if remote_cycles < 3:
                at = now
                return
            print('REMOTE_REPEATED_G_TOGGLE_PASSED', remote_cycles)
            remote = fleet[1]
            look_at_vehicle(remote)
            at = now
            phase = 9
            return
        if phase == 9:
            if now - at < .3:
                return
            pawn.toggle_remote_control()
            assert pawn.get_remote_forklift() == remote and not fleet[0].is_remote_controlled()
            pawn.set_actor_tick_enabled(False)
            remote.set_remote_input(0, 0, 0)
            at = now
            phase = 17
            return
        if phase == 17:
            remote.set_remote_input(0, 0, 0)
            if now - at < 1.25:
                return
            assert remote.is_remote_controlled() and remote.get_editor_property('powered')
            before = [v.get_actor_location() for v in fleet]
            remote.set_remote_input(-1, .35, 0)
            at = now
            phase = 10
            return
        if phase == 10:
            remote.set_remote_input(-1, .35, 0)
            if now - at < 1.5 or (remote.get_actor_location() - before[1]).length() <= 5 or abs(remote.get_actor_rotation().yaw - 90) <= 1:
                assert now - at < 15, 'Second AGV remote drive/steering failed after self check'
                return
            assert (remote.get_actor_location() - before[1]).length() > 5, 'Second AGV remote drive failed'
            assert abs(remote.get_actor_rotation().yaw - 90) > 1, 'Remote steering did not change heading'
            assert (fleet[0].get_actor_location() - before[0]).length() < .1
            pawn.set_actor_tick_enabled(True)
            pc.toggle_warehouse_menu()
            at = now
            phase = 11
            return
        if phase == 11:
            if now - at < .3:
                return
            assert pc.is_warehouse_menu_open()
            assert not pawn.get_remote_forklift() and not remote.is_remote_controlled(), 'Menu did not release/brake remote AGV'
            pc.toggle_warehouse_menu()
            remote.set_actor_location(unreal.Vector(FLEET[1][4], FLEET[1][5], 0), False, True)
            remote.set_actor_rotation(unreal.Rotator(yaw=90), True)
            look_at_vehicle(remote)
            wall = named['RemoteTestObstacle']
            wall.set_actor_location(remote.get_actor_location() + remote.get_actor_forward_vector() * 160 + unreal.Vector(0, 0, 40), False, True)
            wall.set_actor_rotation(unreal.Rotator(yaw=90), True)
            at = now
            phase = 12
            return
        if phase == 12:
            if now - at < .3:
                return
            pawn.toggle_remote_control()
            assert pawn.get_remote_forklift() == remote
            pawn.set_actor_tick_enabled(False)
            remote.set_remote_input(0, 0, 0)
            at = now
            phase = 18
            return
        if phase == 18:
            remote.set_remote_input(0, 0, 0)
            if now - at < 1.25:
                return
            assert remote.is_remote_controlled() and remote.get_editor_property('powered')
            obstacle_start = remote.get_actor_location()
            remote.set_remote_input(1, 0, 0)
            at = now
            phase = 13
            return
        if phase == 13:
            remote.set_remote_input(1, 0, 0)
            if now - at < 3 or 'OBSTACLE' not in remote.get_editor_property('status'):
                assert now - at < 15, ('Remote obstacle stop did not occur', remote.get_editor_property('status'))
                return
            assert (remote.get_actor_location() - obstacle_start).length() < 40, 'Remote AGV crossed an obstacle'
            assert 'OBSTACLE' in remote.get_editor_property('status'), remote.get_editor_property('status')
            remote.set_remote_input(0, 0, 0)
            print('REMOTE_SECOND_AGV_STEERING_MENU_OBSTACLE_PASSED')
            named['RemoteTestObstacle'].set_actor_location(unreal.Vector(8000, 8000, 40), False, True)
            named['RemoteTestWorker'].set_actor_location(remote.get_actor_location() + remote.get_actor_forward_vector() * 230 + unreal.Vector(0, 0, 96), False, True)
            obstacle_start = remote.get_actor_location()
            at = now
            phase = 20
            return
        if phase == 20:
            remote.set_remote_input(0, 0, 0)
            if now - at < .3:
                return
            remote.set_remote_input(1, 0, 0)
            at = now
            phase = 21
            return
        if phase == 21:
            remote.set_remote_input(1, 0, 0)
            if now - at < 1 or 'PERSON' not in remote.get_editor_property('status'):
                assert now - at < 15, ('Remote person scanner did not stop', remote.get_editor_property('status'))
                return
            assert (remote.get_actor_location() - obstacle_start).length() < 15, 'Remote AGV moved through the worker safety zone'
            print('REMOTE_PERSON_SAFETY_SCANNER_PASSED')
            remote.set_remote_input(0, 0, 0)
            named['RemoteTestWorker'].set_actor_location(unreal.Vector(8000, 7800, 96), False, True)
            remote.set_actor_location(unreal.Vector(FLEET[1][4], FLEET[1][5], 0), False, True)
            remote.set_actor_rotation(unreal.Rotator(yaw=90), True)
            remote.set_remote_input(0, 0, -1)
            at = now
            phase = 22
            return
        if phase == 22:
            remote.set_remote_input(0, 0, -1)
            if now - at < .3 or remote.get_fork_height_cm() > 9.55:
                assert now - at < 15, ('Remote F lowering did not reach insertion height', remote.get_fork_height_cm(), remote.get_editor_property('status'))
                return
            remote.set_remote_input(0, 0, 0)
            pallet = named['RemoteTestPallet']
            load = named['RemoteTestLoad']
            position = remote.get_actor_location() + remote.get_actor_forward_vector() * 60
            for actor, height in ((pallet, .1), (load, 25.2)):
                actor.set_actor_location(position + unreal.Vector(0, 0, height), False, True)
                actor.set_actor_rotation(unreal.Rotator(yaw=90), True)
                body = actor.get_component_by_class(unreal.StaticMeshComponent)
                body.set_physics_linear_velocity(unreal.Vector())
                body.set_physics_angular_velocity_in_degrees(unreal.Vector())
                body.wake_all_rigid_bodies()
            at = now
            phase = 25
            return
        if phase == 25:
            remote.set_remote_input(0, 0, 0)
            if now - at < .5:
                return
            remote.set_remote_input(0, 0, 1)
            at = now
            phase = 23
            return
        if phase == 23:
            remote.set_remote_input(0, 0, 1)
            for label in ('RemoteTestPallet', 'RemoteTestLoad'):
                actor = named[label]
                assert actor.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
                assert not actor.get_attach_parent_actor(), 'Remote lifting rigidly attached the load'
            if remote.get_fork_height_cm() < 20:
                assert now - at < 15, ('Remote pallet lift failed', remote.get_editor_property('status'), remote.get_fork_height_cm())
                return
            remote.set_remote_input(0, 0, 0)
            assert remote.get_editor_property('target_pallet') == named['RemoteTestPallet'], 'Remote lift did not detect the inserted pallet'
            assert remote.get_load_mass_kg() > 35, 'Remote lift omitted the supported carton mass'
            assert named['RemoteTestPallet'].get_actor_location().z > 4, 'Pallet did not rise on physical fork contact'
            load_before = [named[label].get_actor_location() for label in ('RemoteTestPallet', 'RemoteTestLoad')]
            remote.set_remote_input(-.2, 0, 0)
            at = now
            phase = 24
            return
        if phase == 24:
            remote.set_remote_input(-.2, 0, 0)
            for index, label in enumerate(('RemoteTestPallet', 'RemoteTestLoad')):
                actor = named[label]
                assert actor.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics() and not actor.get_attach_parent_actor()
                assert actor.get_actor_location().z > 4, 'Remote load support was lost during reverse travel'
            if now - at < 2 or min((named[label].get_actor_location() - load_before[i]).length()
                    for i, label in enumerate(('RemoteTestPallet', 'RemoteTestLoad'))) <= 15:
                assert now - at < 15, ('Physical load did not travel with remote forks', remote.get_editor_property('status'))
                return
            remote.set_remote_input(0, 0, 0)
            pawn.set_actor_tick_enabled(True)
            pawn.end_remote_control()
            assert not pawn.get_remote_forklift() and not remote.is_remote_controlled()
            print('REMOTE_PHYSICAL_PALLET_LIFT_REVERSE_PASSED', 'pallet and carton continuously dynamic, contact support, real payload mass')
            if REMOTE_CHECKS_ONLY:
                print('REMOTE_ONLY_PIE_PASSED', 'active-job G interruption and E resume; G phone and hand IK; held-cargo guard; independent remote drive/lift/steering; repeated toggle; deadman; menu brake; object and person stop; physical remote load lift/reverse; autonomous L1 completion not tested')
            else:
                print('DUAL_FORKLIFTS_REMOTE_PIE_PASSED', '2 simultaneous physical L1 jobs; active-job G interruption and E resume; G phone and hand IK; held-cargo guard; independent remote drive/lift/steering; repeated toggle; deadman; menu brake; object and person stop; physical remote load lift/reverse')
            finish()
    except Exception:
        print('REMOTE_ONLY_PIE_FAILED' if REMOTE_CHECKS_ONLY else 'DUAL_FORKLIFTS_REMOTE_PIE_FAILED', traceback.format_exc())
        finish()


handle = unreal.register_slate_post_tick_callback(tick)
