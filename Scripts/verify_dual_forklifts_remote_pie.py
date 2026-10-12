"""Real-time PIE integration: two physical L1 jobs and smartphone remote controls.

Remote motion/lift commands use SetRemoteInput with character polling disabled.
They verify native actuators and release guards, not OS mouse-button mapping.

Test objects/operator are transient. Running vehicle poses are never assigned.
This script never saves.
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
# Keep unused probes physically supported outside the production aisles.
# They must not obstruct the independent-navigation prerequisite before use.
probe_floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(8000, 8000, -5))
probe_floor.set_actor_label('RemoteProbeStorageFloor')
probe_floor.set_editor_property('is_spatially_loaded', False)
probe_floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
probe_floor.static_mesh_component.set_collision_profile_name('BlockAll')
probe_floor.set_actor_scale3d(unreal.Vector(10, 10, .1))
fixture = actors.spawn_actor_from_class(unreal.WarehouseCargo, unreal.Vector(7750, 8150, 20))
fixture.set_actor_label('RemoteTestCarton')
fixture.set_editor_property('is_spatially_loaded', False)
fixture.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1'))
fixture.set_gross_mass_kg(5)
fit(fixture, (20, 18, 18), (7750, 8150, 9.1))
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
test_pallet = actors.spawn_actor_from_class(unreal.WarehousePallet, unreal.Vector(7750, 7750, .1))
test_pallet.set_actor_label('RemoteTestPallet')
test_pallet.set_editor_property('is_spatially_loaded', False)
test_load = actors.spawn_actor_from_class(unreal.WarehouseCargo, unreal.Vector(7750, 7750, 25.2))
test_load.set_actor_label('RemoteTestLoad')
test_load.set_editor_property('is_spatially_loaded', False)
test_load.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1'))
test_load.set_gross_mass_kg(12)
fit(test_load, (30, 24, 20), (7750, 7750, 25.2))
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
deadman = {}
load_before = None
initial_orders = []
outbound_requested = False
remote_heading_before = 0
remote_diagnostic_second = -1
insertion_settled_at = None


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


def verify_unselected_vehicle(vehicle, origin):
    displacement = (vehicle.get_actor_location() - origin).length()
    speed = components(vehicle)['Body'].get_physics_linear_velocity().length()
    print('REMOTE_UNSELECTED_VEHICLE_NATIVE_REST', vehicle.get_actor_label(),
          'displacement_cm', displacement, 'speed_cm_s', speed, 'status', vehicle.status)
    assert not vehicle.is_remote_controlled() and not vehicle.powered, 'Remote input activated the unselected vehicle'
    assert displacement < 5 and speed < 5, ('Unselected braked vehicle made substantial physical motion', displacement, speed)


def diagnose_remote_turn(vehicle, elapsed):
    game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    body = components(vehicle)['Body']
    print('REMOTE_TURN_DIAGNOSTIC', elapsed, vehicle.status, 'pose', vehicle.get_actor_transform(),
          'lift_cm', vehicle.get_fork_height_cm(), 'speed', body.get_physics_linear_velocity(),
          'heading_change', abs((vehicle.get_actor_rotation().yaw - remote_heading_before + 180) % 360 - 180))
    # Read the same candidate geometry as the normal reverse/steering command.
    # These overlaps are diagnostics only; no pose or contact is changed.
    step = unreal.GameplayStatics.get_world_delta_seconds(game)
    distance = -min(180., vehicle.empty_travel_speed_cm) * step
    rotation = vehicle.get_actor_rotation()
    rotation.yaw += distance * .35 / max(117.3, vehicle.minimum_turning_radius_cm) * 180 / 3.141592653589793
    pose = unreal.Transform(location=vehicle.get_actor_location() + vehicle.get_actor_forward_vector() * distance, rotation=rotation)
    for part in vehicle.get_components_by_class(unreal.StaticMeshComponent):
        local = unreal.MathLibrary.make_relative_transform(part.get_world_transform(), vehicle.get_actor_transform())
        candidate = unreal.MathLibrary.compose_transforms(local, pose)
        hits = unreal.SystemLibrary.component_overlap_components(part, candidate,
            [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2],
            unreal.PrimitiveComponent, [vehicle]) or []
        if hits:
            print('REMOTE_TURN_NATIVE_OVERLAP', part.get_name(), candidate.translation,
                  [(hit.get_owner().get_actor_label(), hit.get_name()) for hit in hits])


def look_at_vehicle(vehicle):
    # Stand alongside the chassis, outside the forward person-safety volume.
    body = components(vehicle)['Body']
    target = body.get_world_location() + vehicle.get_actor_forward_vector() * -55 + unreal.Vector(0, 0, 75)
    positions = [vehicle.get_actor_location() + vehicle.get_actor_right_vector() * side + vehicle.get_actor_forward_vector() * -40
                 for side in (-250, 250)]
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    movement.stop_movement_immediately()
    # Use the collision-tested Teleport API rather than forcing an actor-only move.
    # The capsule/base diagnostics distinguish overlap resolution from stale movement state.
    for position in positions:
        if pawn.teleport(position + unreal.Vector(0, 0, 125), pawn.get_actor_rotation()):
            break
    else:
        raise AssertionError(('Operator capsule has no room on either vehicle side', positions))
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
    global phase, at, named, fleet, pc, pawn, before, remote, lift_before, remote_cycles, obstacle_start, interrupted_job, interrupted_pending, other_before, deadman, load_before, outbound_requested, remote_heading_before, remote_diagnostic_second, insertion_settled_at
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
            # The saved fleet now parks at charging bays. Use the real console
            # to give the second ready AGV independent work, then wait for both
            # native dock exits and work-order acceptance before comparing loads.
            second = fleet[1]
            if (not outbound_requested and not second.active_job.job_id and not second.pending_jobs
                    and second.ai_state == unreal.WarehouseAIState.READY):
                console = named['WH_ManagementComputer']
                assert console.issue_outbound(named['WH_Drop_Box'], second, 0), console.last_message
                outbound_requested = True
            if not all(v.active_job.job_id and v.target_pallet for v in fleet):
                assert now < 90, 'Both AGVs did not leave their docks and accept independent work'
                return
            assert fleet[0].get_editor_property('target_pallet') != fleet[1].get_editor_property('target_pallet')
            assert fleet[0].get_editor_property('charging_station') != fleet[1].get_editor_property('charging_station')
            for v in fleet:
                assert v.get_editor_property('charging_station').get_editor_property('assigned_vehicle') == v
                assert unreal.WarehouseAIState.SELF_CHECK in seen[fleet.index(v)], 'Self check was skipped'
            duplicate = unreal.WarehouseWorkOrder(job_id='CONFLICT-V01-V02', source_system='FLEET-CONFLICT-TEST',
                pallet=fleet[0].target_pallet, destination=fleet[0].active_job.destination)
            assert not fleet[1].submit_job(duplicate), 'Both AGVs accepted the same pallet'
            initial_orders.extend((v.active_job.job_id, v.target_pallet,
                unreal.Transform(location=v.active_job.destination.translation, rotation=v.active_job.destination.rotation.rotator())) for v in fleet)
            initial[:] = [v.get_actor_location() for v in fleet]
            print('DUAL_AGV_SHARED_PALLET_REJECTED')
            phase = 2
            return
        if phase == 2:
            if not all((v.get_actor_location() - initial[i]).length() > 80 for i, v in enumerate(fleet)):
                # The second native vehicle may wait for the first vehicle's
                # reserved aisle; retain actual movement checks through that wait.
                assert now < 180, 'Both forklifts did not independently navigate'
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
            assert interrupted_job, 'There was no active job to test remote takeover'
            pawn.toggle_remote_control()
            assert pawn.get_remote_forklift() == remote and remote.is_remote_controlled()
            assert not fleet[1].is_remote_controlled()
            other_before = fleet[1].get_actor_location()
            at = now
            phase = 15
            return
        if phase == 15:
            assert fleet[1].get_editor_property('powered') and not fleet[1].is_remote_controlled(), 'Remote takeover stopped the unrelated AGV'
            assert fleet[1].get_editor_property('active_job').job_id == initial_orders[1][0], 'Remote takeover changed the unrelated work order'
            # A live aisle reservation or turn can legitimately delay translation.
            if now - at < .5 or (fleet[1].get_actor_location() - other_before).length() <= 1:
                assert now - at < 20, 'Unrelated AGV made no actual progress after remote takeover'
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
                if not all(order[0] in v.get_editor_property('completed_job_ids') for order, v in zip(initial_orders, fleet)):
                    return
                for vehicle, (_, pallet, destination) in zip(fleet, initial_orders):
                    if abs(destination.translation.z) <= 5:
                        assert vehicle.is_floor_placement_valid(pallet, destination), 'Completed floor load did not settle inside its actual reservation'
                    else:
                        assert (pallet.get_actor_location() - destination.translation).length() <= 2, 'Completed rack load missed its actual destination'
                print('DUAL_AGV_PHYSICAL_JOBS_PASSED', 'Both distinct L1 pallets unloaded; cargo/pallet physics stayed enabled')
            # Keep the actual paused poses; move only the pickup fixture/operator.
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
            verify_unselected_vehicle(fleet[1], before[1])
            # Stop transmitting instead of sending brake. The .25-second
            # watchdog uses the native world real clock; stopping then takes
            # finite tyre/joint braking time, as in the native drive test.
            body = components(remote)['Body']
            assert body.is_simulating_physics()
            assert body.get_collision_enabled() != unreal.CollisionEnabled.NO_COLLISION
            deadman = dict(sim=now, real=unreal.GameplayStatics.get_real_time_seconds(game),
                wall=time.monotonic(), pose=remote.get_actor_transform(), origin=remote.get_actor_location(),
                initial_speed=body.get_physics_linear_velocity().length(), peak_speed=body.get_physics_linear_velocity().length(),
                max_displacement=0., timeout_at=None, settled_at=None, last_log=-1.,
                parts=[(part, part.get_collision_enabled(), part.is_simulating_physics(), part.get_collision_profile_name())
                    for part in remote.get_components_by_class(unreal.PrimitiveComponent)])
            print('REMOTE_DEADMAN_TRANSMISSION_CUTOFF', 'simulation', deadman['sim'], 'native_real', deadman['real'],
                'pose', deadman['pose'], 'speed_cm_s', deadman['initial_speed'], 'status', remote.status)
            phase = 19
            return
        if phase == 19:
            body = components(remote)['Body']
            elapsed = now - deadman['sim']
            real_elapsed = unreal.GameplayStatics.get_real_time_seconds(game) - deadman['real']
            speed = body.get_physics_linear_velocity().length()
            displacement = (remote.get_actor_location() - deadman['origin']).length()
            deadman['peak_speed'] = max(deadman['peak_speed'], speed)
            deadman['max_displacement'] = max(deadman['max_displacement'], displacement)
            for part, collision, simulating, profile in deadman['parts']:
                assert (part.get_collision_enabled(), part.is_simulating_physics(), part.get_collision_profile_name()) == (collision, simulating, profile), ('Deadman changed native collision/dynamics', part.get_name())
            assert remote.is_remote_controlled() and remote.powered, 'Deadman disconnected the remote session instead of braking'
            timed_out = 'REMOTE INPUT TIMEOUT' in remote.status
            frame_allowance = 2 * unreal.GameplayStatics.get_world_delta_seconds(game) + .0001
            if timed_out and deadman['timeout_at'] is None:
                assert .25 <= real_elapsed <= .25 + frame_allowance, ('Deadman timeout did not follow the native .25-second clock', real_elapsed)
                deadman['timeout_at'] = elapsed
                print('REMOTE_DEADMAN_TIMEOUT_OBSERVED', 'simulation_elapsed', elapsed,
                    'native_real_elapsed', real_elapsed, 'wall_elapsed', time.monotonic() - deadman['wall'],
                    'speed_cm_s', speed, 'displacement_cm', displacement)
            if real_elapsed > .25 + frame_allowance:
                assert timed_out, ('Deadman did not detect lost transmission', real_elapsed, remote.status)
            if elapsed - deadman['last_log'] >= .1:
                deadman['last_log'] = elapsed
                print('REMOTE_DEADMAN_BRAKING_SAMPLE', 'simulation_elapsed', elapsed, 'native_real_elapsed', real_elapsed,
                    'wall_elapsed', time.monotonic() - deadman['wall'], 'speed_cm_s', speed,
                    'displacement_cm', displacement, 'status', remote.status)
            # Measured full-speed native braking took .733s / 53cm. Include
            # up to 45cm during the .25s signal timeout and retain a 120cm cap.
            assert displacement < 120, ('Deadman stopping distance exceeded 120cm', deadman['max_displacement'], speed)
            if timed_out and speed < 5:
                if deadman['settled_at'] is None:
                    deadman['settled_at'] = now
            else:
                deadman['settled_at'] = None
            if deadman['settled_at'] is None or now - deadman['settled_at'] < .3:
                assert elapsed < 2, ('Deadman finite brake did not settle within 2 simulation seconds', speed,
                    deadman['peak_speed'], deadman['max_displacement'], remote.status)
                return
            assert elapsed <= 2, ('Deadman settled after its simulation deadline', elapsed)
            print('REMOTE_INPUT_DEADMAN_PASSED', 'initial_speed_cm_s', deadman['initial_speed'],
                'peak_speed_cm_s', deadman['peak_speed'], 'max_displacement_cm', deadman['max_displacement'],
                'timeout_simulation_seconds', deadman['timeout_at'], 'settled_simulation_seconds', elapsed,
                'held_below_5cm_s_seconds', now - deadman['settled_at'])
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
            remote_heading_before = remote.get_actor_rotation().yaw
            remote_diagnostic_second = -1
            remote.set_remote_input(-1, .35, 0)
            at = now
            phase = 10
            return
        if phase == 10:
            remote.set_remote_input(-1, .35, 0)
            heading_change = abs((remote.get_actor_rotation().yaw - remote_heading_before + 180) % 360 - 180)
            if int(now - at) != remote_diagnostic_second:
                remote_diagnostic_second = int(now - at)
                print('REMOTE_SECOND_DRIVE_PROGRESS', now - at, remote.status, 'pose', remote.get_actor_transform(),
                      'lift_cm', remote.get_fork_height_cm(), 'translation_cm', (remote.get_actor_location() - before[1]).length(),
                      'heading_change', heading_change)
                if remote_diagnostic_second == 1:
                    diagnose_remote_turn(remote, now - at)
            if now - at < 1.5 or (remote.get_actor_location() - before[1]).length() <= 5 or heading_change <= 1:
                assert now - at < 15, ('Second AGV remote drive/steering failed after self check', remote.status,
                    remote.get_actor_transform(), remote.get_fork_height_cm(), (remote.get_actor_location() - before[1]).length(), heading_change)
                return
            assert (remote.get_actor_location() - before[1]).length() > 5, 'Second AGV remote drive failed'
            assert heading_change > 1, 'Remote steering did not change heading'
            verify_unselected_vehicle(fleet[0], before[0])
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
            look_at_vehicle(remote)
            wall = named['RemoteTestObstacle']
            wall.set_actor_location(remote.get_actor_location() + remote.get_actor_forward_vector() * 160 + unreal.Vector(0, 0, 40), False, True)
            wall.set_actor_rotation(unreal.Rotator(yaw=remote.get_actor_rotation().yaw), True)
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
            obstacle_start = remote.get_actor_location()
            at = now
            phase = 27
            return
        if phase == 27:
            remote.set_remote_input(-.3, 0, 0)
            distance = (remote.get_actor_location() - obstacle_start).length()
            if distance < 80:
                assert now-at < 15, ('Native backoff did not clear the obstacle-test position', distance, remote.status)
                return
            remote.set_remote_input(0, 0, 0)
            print('REMOTE_NATIVE_BACKOFF_TO_LIFT_AREA_PASSED', distance, remote.get_actor_transform(),
                  'carriage_simulating', components(remote)['Carriage'].is_simulating_physics(),
                  'carriage_parent', components(remote)['Carriage'].get_attach_parent())
            remote.set_remote_input(0, 0, -1)
            at = now
            phase = 22
            return
        if phase == 22:
            remote.set_remote_input(0, 0, -1)
            if now - at < .3 or remote.get_fork_height_cm() > 9.55:
                assert now - at < 15, ('Native remote lowering did not reach insertion height', remote.get_fork_height_cm(), remote.get_editor_property('status'))
                return
            remote.set_remote_input(0, 0, 0)
            insertion_settled_at = None
            at = now
            phase = 26
            return
        if phase == 26:
            # Calibrate the real unloaded carriage before preparing the fixture.
            # After finite joint/contact deflection, actor-relative nominal zero
            # is not necessarily the pallet hole's actual world height.
            carriage = components(remote)['CarriagePivot']
            error = .1 - carriage.get_world_location().z
            remote.set_remote_input(0, 0, .2 if error > .15 else (-.2 if error < -.15 else 0))
            vertical_speed = abs(components(remote)['Carriage'].get_physics_linear_velocity().z - components(remote)['Body'].get_physics_linear_velocity().z)
            if abs(error) <= .15 and vertical_speed < 1:
                if insertion_settled_at is None:
                    insertion_settled_at = now
            else:
                insertion_settled_at = None
            if insertion_settled_at is None or now - insertion_settled_at < .3:
                if int(now-at) != remote_diagnostic_second:
                    remote_diagnostic_second = int(now-at)
                    print('REMOTE_INSERTION_SERVO_DIAGNOSTIC', now-at, 'error', error,
                          'fork_height', remote.get_fork_height_cm(), 'pivot', carriage.get_world_transform(),
                          'max_height', remote.max_fork_height_cm, 'powered', remote.powered, 'status', remote.status)
                    rig = components(remote)
                    physical_carriage = rig['Carriage']
                    joint = rig['CarriageHydraulicJoint']
                    print('REMOTE_CARRIAGE_NATIVE_DIAGNOSTIC',
                          'body_pose', physical_carriage.get_world_transform(),
                          'com', physical_carriage.get_center_of_mass(),
                          'velocity', physical_carriage.get_physics_linear_velocity(),
                          'pivot_parent', carriage.get_attach_parent(),
                          'pivot_relative', carriage.get_relative_transform(),
                          'constrained', joint.get_constrained_components(),
                          'broken', joint.is_broken(), 'force', joint.get_constraint_force())
                assert now - at < 15, ('Real carriage did not settle at the pallet hole height', error, vertical_speed, remote.status)
                return
            remote.set_remote_input(0, 0, 0)
            print('REMOTE_NATIVE_INSERTION_HEIGHT_READY', carriage.get_world_transform(), 'vertical_speed', vertical_speed)
            pallet = named['RemoteTestPallet']
            load = named['RemoteTestLoad']
            position = carriage.get_world_location() + carriage.get_forward_vector() * 65
            for actor, height in ((pallet, .1), (load, 25.2)):
                actor.set_actor_location(unreal.Vector(position.x, position.y, height), False, True)
                actor.set_actor_rotation(unreal.Rotator(yaw=carriage.get_world_rotation().yaw), True)
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
            assert named['RemoteTestPallet'].can_engage(components(remote)['CarriagePivot'].get_world_transform()), (
                'Fixture pallet holes did not contain the actual tines before lift',
                named['RemoteTestPallet'].get_actor_transform(), components(remote)['CarriagePivot'].get_world_transform())
            print('REMOTE_NATIVE_FULL_TINE_INSERTION_PASSED')
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
