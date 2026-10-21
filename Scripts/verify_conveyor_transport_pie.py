"""Unsaved native Chaos test of roller zones and a powered 1F -> 4F lift.

After BeginPlay, cargo poses, velocities, masses and actor attachment are never
assigned. Normal transport uses the actual rollers and lift. The separate
withdrawal case uses an explicit TEST-ONLY 800N external physical hoist.
"""
import json
from pathlib import Path
import time
import traceback
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
output = Path(unreal.Paths.project_saved_dir())/'Testing/Construction'
output.mkdir(parents=True, exist_ok=True)
request_path = Path(unreal.Paths.project_saved_dir())/'Testing/InboundWrapRequest.json'
request = json.loads(request_path.read_text(encoding='utf-8-sig')) if request_path.exists() else {}
withdrawal_only = request.get('mode') == 'withdrawal'
report = dict(passed=False, checks=[], samples=[], nativePhysics=True, osInputTested=False,
              natural_lower_load_positive_servo_residuals=[],
              mode='withdrawal' if withdrawal_only else 'full',
              recovery_stimulus='TEST-ONLY external hoist, finite 800N; not ordinary conveyor transport or player hand carrying')
started = time.monotonic()
phase = 'start'
handle = game = None
last_sample = 0
recovery_hoist = None
completed_source_disabled = False
last_servo_observation = -1
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)

def check(name, **details):
    report['checks'].append(dict(name=name, **details))
    print('CONVEYOR_CHECK', name, json.dumps(details, ensure_ascii=False))

def finish(ok, detail):
    report.update(passed=ok, detail=detail, wall_seconds=round(time.monotonic()-started, 2))
    (output/('ConveyorWithdrawalReport.json' if withdrawal_only else 'ConveyorReport.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('CONVEYOR_TRANSPORT_PASSED' if ok else 'CONVEYOR_TRANSPORT_FAILED', detail)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    if editor.get_game_world():
        level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()

def fixture():
    assert level.load_level('/Engine/Maps/Entry')
    for obj in list(actors.get_all_level_actors()):
        if not isinstance(obj, (unreal.WorldSettings, unreal.LevelScriptActor)):
            actors.destroy_actor(obj)
    world = editor.get_editor_world()
    unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property('default_game_mode', unreal.GameModeBase)
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    def spawn(cls, name, point):
        obj = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
        obj.set_actor_label(name)
        return obj
    def line(name, point):
        obj = spawn(unreal.WarehouseConveyor, name, point)
        assert obj.rebuild_conveyor()
        return obj
    def carton(name, point, size):
        obj = spawn(unreal.WarehouseCargo, name, point)
        obj.set_cargo_mesh(cube)
        obj.set_actor_scale3d(unreal.Vector(*[value/100 for value in size]))
        obj.set_gross_mass_kg(12)
        return obj
    floor = spawn(unreal.StaticMeshActor, 'ConveyorFixtureFloor', (1000, 0, -10))
    floor.static_mesh_component.set_static_mesh(cube)
    floor.static_mesh_component.set_collision_profile_name('BlockAll')
    floor.set_actor_scale3d(unreal.Vector(100, 65, .2))
    floor.tags = ['WarehouseMapStructure', 'LayoutFixed']
    spawn(unreal.PlayerStart, 'ConveyorSafePlayerStart', (-1800, -2400, 100))
    spawn(unreal.load_class(None, '/Script/msc_vr.WarehouseSimulationClock'), 'ConveyorClock', (-1800, -2400, 0))
    lift = spawn(unreal.WarehousePalletLift, 'ConveyorLift', (0, 0, 0))
    lift.set_editor_property('powered_conveyor', True)
    lift.set_editor_property('floor_count', 4)
    lift.set_editor_property('travel_speed_cm', 50)
    for n in (0, 3):
        port = line('ConveyorPort'+str(n), (-215.5, 0, n*400))
        port.set_editor_property('lift', lift)
        port.set_editor_property('lift_floor', n)
        port.set_editor_property('destination_floor', 3 if n == 0 else 0)
        port.set_editor_property('lift_infeed', n == 0)
    parcels = []
    for name, point in [('LiftPallet', (-300, 0, 40.2)), ('PickupPallet', (910, 0, 40.2)),
                        ('UpstreamPallet', (1910, 0, 40.2)), ('DownstreamPallet', (2390, 0, 40.2))]:
        parcels.append((spawn(unreal.WarehousePallet, name, point), 25))
    parcels.append((carton('LiftCargo', (-300, 0, 65.3), (30, 24, 20)), 12))
    next(obj for obj, _ in parcels if obj.get_actor_label() == 'PickupPallet').set_actor_rotation(unreal.Rotator(yaw=180), True)
    vehicle = spawn(unreal.WarehouseForklift, 'ConveyorPickupVehicle', (1500, 0, 0))
    vehicle.set_actor_rotation(unreal.Rotator(yaw=180), True)
    vehicle.set_editor_property('auto_start', False)
    vehicle.set_editor_property('pending_jobs', [])
    line('PickupLine', (1000, 0, 0))
    upstream = line('UpstreamLine', (2000, 0, 0))
    downstream = line('DownstreamLine', (2300, 0, 0))
    upstream.set_editor_property('next_conveyor', downstream)
    upstream.set_editor_property('pickup_end', False)
    carton_line = line('CartonLine', (1000, -700, 0))
    carton_line.set_editor_property('kind', unreal.WarehouseConveyorKind.CARTON_BELT)
    carton_line.set_editor_property('width_cm', 70)
    assert carton_line.rebuild_conveyor()
    parcels.append((carton('LooseCarton', (910, -700, 60.2), (40, 35, 40)), 12))
    # A separate infeed exercises physical removal before lift acceptance.
    cancel_lift = spawn(unreal.WarehousePalletLift, 'CancelLift', (-2000, -1800, 0))
    cancel_lift.set_editor_property('powered_conveyor', True)
    for floor_index in (0, 1):
        cancel_port = line('CancelPort'+str(floor_index), (-2215.5, -1800, floor_index*400))
        cancel_port.set_editor_property('lift', cancel_lift)
        cancel_port.set_editor_property('lift_floor', floor_index)
        cancel_port.set_editor_property('destination_floor', 1 if floor_index == 0 else 0)
        cancel_port.set_editor_property('lift_infeed', floor_index == 0)
        if floor_index == 0:
            cancel_port.set_editor_property('enabled', False)
            cancel_port.set_editor_property('speed_cm', 1)
    parcels.append((spawn(unreal.WarehousePallet, 'CancelPallet', (-2300, -1800, 40.2)), 25))
    anchor = spawn(unreal.StaticMeshActor, 'TestOnlyRecoveryAnchor', (-2300, -2400, 300))
    anchor.static_mesh_component.set_static_mesh(cube)
    anchor.static_mesh_component.set_collision_profile_name('BlockAll')
    anchor.set_actor_scale3d(unreal.Vector(.05, .05, .05))
    # No bodies are bound here. Bind only the duplicated live PIE bodies after
    # the infeed reservation is acquired; the tool exerts no earlier force.
    spawn(unreal.PhysicsConstraintActor, 'CancelRecoveryHoist', (-2300, -1800, 48))
    # High failure thresholds isolate machine transport from the separate
    # damage-system tests while preserving every real mass/contact.
    strength = spawn(unreal.WarehouseDamageSystem, 'ConveyorStrength', (-1800, -2000, 0))
    strength.set_editor_property('objects', [unreal.WarehouseStrength(name=obj.get_actor_label(), members=[obj],
        mass_kg=mass, rated_load_kg=100000, impact_yield_j=100000, impact_failure_j=1000000) for obj, mass in parcels])

def local_x(obj, reference):
    return unreal.MathLibrary.inverse_transform_location(reference.get_actor_transform(), obj.get_actor_location()).x

def _tick_impl(dt):
    global phase, game, live, lift, source, receiver, payload, cargo, last_sample, done_at, recovery_hoist, completed_source_disabled, last_servo_observation
    try:
        assert time.monotonic()-started < 330, ('Wall timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        sim = unreal.GameplayStatics.get_time_seconds(game)
        assert sim < 330, ('Simulation timeout', phase)
        if phase == 'settle':
            if sim < .3:
                return
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            lift = live['ConveyorLift']
            source, receiver = live['ConveyorPort0'], live['ConveyorPort3']
            payload, cargo = live['LiftPallet'], live['LiftCargo']
            assert lift.get_editor_property('floor_count') == 4
            clock = next(a for a in live.values() if isinstance(a, unreal.WarehouseSimulationClock))
            clock.set_speed(4)
            for name in ('PickupLine', 'CartonLine', 'ConveyorPort0'):
                line = live[name]
                assert 'WarehouseMapStructure' in [str(t) for t in line.tags]
                assert all(r.is_simulating_physics() for r in line.get_editor_property('rollers'))
            check('four_floor_real_rollers_and_fixed_map_tags')
            if withdrawal_only:
                live['CancelPort0'].set_editor_property('enabled', True)
                done_at, phase = sim, 'cancel_reservation'
            else:
                phase = 'transport'
        for name, mass in [('LiftPallet', 25), ('LiftCargo', 12), ('LooseCarton', 12), ('PickupPallet', 25)]:
            obj = live[name]
            body = obj.get_component_by_class(unreal.StaticMeshComponent)
            assert body.is_simulating_physics() and body.is_gravity_enabled(), (name, 'physics disabled')
            assert body.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
            assert abs(body.get_mass()-mass) < .1, (name, 'mass changed', body.get_mass())
            assert not obj.get_attach_parent_actor(), (name, 'cargo attached')
            assert 'WarehouseMapStructure' not in [str(t) for t in obj.tags], 'Cargo leaked into fixed map'
            assert obj.get_actor_location().z > (20 if name != 'PickupPallet' or phase not in ('pickup', 'cancel_reservation', 'cancel_hoist_raise', 'cancel_recovery') else -1), (name, 'cargo fell through roller bed')
        if sim-last_sample > 2:
            last_sample = sim
            sample = dict(t=round(sim, 2), lift=str(lift.get_editor_property('state')),
                lift_status=lift.get_editor_property('status'),
                gates=[dict(name=g.get_name(), sim=g.is_simulating_physics(), mass=round(g.get_mass(),2),
                    position=[round(getattr(g.get_world_location(), a),2) for a in 'xyz'],
                    velocity=[round(getattr(g.get_physics_linear_velocity(), a),2) for a in 'xyz']) for g in lift.get_editor_property('gates')],
                source=source.get_editor_property('status'), receiver=receiver.get_editor_property('status'),
                pallet=[round(getattr(payload.get_actor_location(), axis), 2) for axis in 'xyz'],
                velocity=[round(getattr(payload.get_component_by_class(unreal.StaticMeshComponent).get_physics_linear_velocity(), axis), 2) for axis in 'xyz'],
                surface=round(source.get_editor_property('actual_surface_speed_cm'), 2))
            sample['other_zones']={name: dict(status=live[name].get_editor_property('status'),
                speed=live[name].get_editor_property('actual_surface_speed_cm'),
                loads=[a.get_actor_label() for a in live[name].get_physical_loads()]) for name in ('PickupLine','CartonLine','UpstreamLine')}
            cancel_lift, cancel_line = live['CancelLift'], live['CancelPort0']
            sample['withdrawal'] = dict(phase=phase,
                pallet_cm=[round(getattr(live['CancelPallet'].get_actor_location(), a), 3) for a in 'xyz'],
                owner=str(cancel_lift.get_editor_property('reserved_conveyor')),
                accepted=str(cancel_lift.get_editor_property('accepted_pallet')),
                lift_state=str(cancel_lift.get_editor_property('state')), lift_status=cancel_lift.get_editor_property('status'),
                source_state=str(cancel_line.get_editor_property('state')), source_status=cancel_line.get_editor_property('status'),
                source_speed=round(cancel_line.get_editor_property('actual_surface_speed_cm'), 3),
                source_loads=[a.get_actor_label() for a in cancel_line.get_physical_loads()])
            if recovery_hoist:
                joint = recovery_hoist.get_component_by_class(unreal.PhysicsConstraintComponent)
                linear_force, _ = joint.get_constraint_force()
                sample['withdrawal']['external_hoist'] = dict(test_only=True, limit_n=800, broken=joint.is_broken(),
                    force_n=[round(getattr(linear_force, a)/100, 3) for a in 'xyz'])
            report['samples'].append(sample)
            print('CONVEYOR_SAMPLE', json.dumps(sample, ensure_ascii=False))
        if phase == 'transport' and sim > 12 and (payload.get_actor_location()-unreal.Vector(-300,0,40)).length() < 2:
            raise AssertionError(('Initial machine handshake made no progress', lift.get_editor_property('status'), source.get_editor_property('status')))
        if phase == 'transport' and not completed_source_disabled and lift.get_editor_property('state') == unreal.WarehousePalletLiftState.AWAIT_UNLOAD:
            centre, extent = payload.get_actor_bounds(False)
            if centre.x+extent.x < -64 and payload in receiver.get_physical_loads():
                # The receiving rollers alone now bear the entire pallet. The
                # doorway is still occupied, so completion cannot be reported
                # yet; pause only the remote source controller, not the receiver.
                assert lift.get_editor_property('completed_transfers') == 0
                assert lift.get_editor_property('reserved_conveyor') == source
                assert source.get_editor_property('completed_lift_transfers') == 0
                source.set_editor_property('enabled', False)
                completed_source_disabled = True
                check('source_controller_disabled_only_after_actual_carrier_clearance_before_doorway_completion',
                    seconds=round(sim, 3), pallet_max_x_cm=round(centre.x+extent.x, 3))
        if phase == 'transport' and completed_source_disabled and lift.get_editor_property('completed_transfers') == 0:
            assert lift.get_editor_property('reserved_conveyor') == source, 'An in-flight transfer released its controller before actual completion'
        if phase == 'transport' and lift.get_editor_property('completed_transfers') >= 1 and receiver.get_editor_property('end_pallet') == payload:
            assert completed_source_disabled, 'The disabled-source completion regression did not exercise its interruption point'
            assert not source.get_editor_property('enabled')
            assert source.get_editor_property('completed_lift_transfers') == 1
            assert not lift.get_editor_property('reserved_conveyor'), 'Completed lift retained a disabled source owner'
            assert abs(source.get_editor_property('actual_surface_speed_cm')) < 1
            assert lift.reserve_conveyor(receiver), 'Another floor controller could not reserve the completed empty lift'
            lift.release_conveyor(receiver)
            assert not lift.get_editor_property('reserved_conveyor')
            check('disabled_source_reports_completed_transfer_once_releases_owner_and_allows_another_floor_controller',
                completed=source.get_editor_property('completed_lift_transfers'), source_speed_cm_s=source.get_editor_property('actual_surface_speed_cm'))
            assert abs(payload.get_actor_location().z-1240) < 2
            assert abs(cargo.get_actor_location().z-1265) < 3
            check('native_1F_to_4F_loading_lift_unloading', seconds=sim, pallet_z=payload.get_actor_location().z)
            for name, objname in [('PickupLine', 'PickupPallet'), ('UpstreamLine', 'UpstreamPallet'), ('CartonLine', 'LooseCarton')]:
                line, obj = live[name], live[objname]
                expected = unreal.WarehouseConveyorState.DOWNSTREAM_BLOCKED if name == 'UpstreamLine' else unreal.WarehouseConveyorState.PICKUP_READY
                assert line.get_editor_property('state') == expected, (name, 'End position was not confirmed', line.get_editor_property('status'))
                assert local_x(obj, line) > 70, (name, 'did not traverse')
                assert abs(obj.get_component_by_class(unreal.StaticMeshComponent).get_physics_linear_velocity().x) < 3, (name, 'did not stop')
                check('native_zone_end_stop_'+name, state=str(line.get_editor_property('state')), local_x=local_x(obj, line))
            assert live['UpstreamPallet'].get_actor_location().x < 2100
            check('zero_pressure_downstream_occupied')
            done_at, phase = sim, 'hold'
        elif phase == 'hold' and sim-done_at > 3:
            assert receiver.get_editor_property('end_pallet') == payload
            assert abs(payload.get_component_by_class(unreal.StaticMeshComponent).get_physics_linear_velocity().length()) < 3
            assert source.get_editor_property('completed_lift_transfers') == 1 and lift.get_editor_property('completed_transfers') == 1
            assert not lift.get_editor_property('reserved_conveyor') and not source.get_editor_property('enabled')
            vehicle = live['ConveyorPickupVehicle']
            vehicle.toggle_power()
            assert vehicle.submit_job(unreal.WarehouseWorkOrder(job_id='CONVEYOR-END-PICKUP',
                pallet=live['PickupPallet'], destination=unreal.Transform(location=unreal.Vector(1500, 700, 0), rotation=unreal.Rotator(yaw=180))))
            phase = 'pickup'
        elif phase == 'pickup':
            vehicle = live['ConveyorPickupVehicle']
            if vehicle.get_editor_property('ai_state') == unreal.WarehouseAIState.LOWER_LOAD:
                offset = vehicle.get_fork_height_cm()-9.5
                if offset < 0 and sim-last_servo_observation >= .1:
                    last_servo_observation = sim
                    observation = dict(seconds=round(sim, 5), measured_lift_offset_cm=offset,
                        upward_residual_to_floor_zero_cm=-offset, within_existing_2mm_contact_tolerance=-offset <= .2)
                    report['natural_lower_load_positive_servo_residuals'].append(observation)
                    print('CONVEYOR_OBSERVED_NATURAL_LOWER_LOAD_SERVO_RESIDUAL', json.dumps(observation))
            results = vehicle.get_editor_property('job_reports')
            assert not any(r.result in ('FAULT', 'FAILED') for r in results), [(r.result, r.detail) for r in results[-3:]]
            if any(r.result == 'COMPLETED' for r in results):
                check('existing_agv_physical_endpoint_pickup_and_delivery', seconds=sim,
                    pallet=[getattr(live['PickupPallet'].get_actor_location(), a) for a in 'xyz'])
                live['CancelPort0'].set_editor_property('enabled', True)
                done_at, phase = sim, 'cancel_reservation'
        elif phase == 'cancel_reservation':
            cancel_lift = live['CancelLift']
            assert sim-done_at < 10, 'Physical infeed never acquired its idle lift reservation'
            if cancel_lift.get_editor_property('reserved_conveyor') == live['CancelPort0']:
                assert not cancel_lift.get_editor_property('accepted_pallet')
                assert live['CancelPallet'] in live['CancelPort0'].get_physical_loads()
                check('preaccept_physical_infeed_reservation_acquired')
                body = live['CancelPallet'].get_component_by_class(unreal.StaticMeshComponent)
                recovery_hoist = live['CancelRecoveryHoist']
                recovery_hoist.set_actor_location(live['CancelPallet'].get_actor_bounds(False)[0], False, False)
                joint = recovery_hoist.get_component_by_class(unreal.PhysicsConstraintComponent)
                joint.set_disable_collision(True)  # Only the remote fixture anchor/load pair.
                joint.set_projection_enabled(False)
                accessor = joint.get_constraint()
                unreal.ConstraintInstanceBlueprintLibrary.set_mass_conditioning_enabled(accessor, False)
                unreal.ConstraintInstanceBlueprintLibrary.set_parent_dominates(accessor, False)
                for setter in (joint.set_linear_x_limit, joint.set_linear_y_limit, joint.set_linear_z_limit):
                    setter(unreal.LinearConstraintMotion.LCM_LIMITED, 250)
                for setter in (joint.set_angular_twist_limit, joint.set_angular_swing1_limit, joint.set_angular_swing2_limit):
                    setter(unreal.AngularConstraintMotion.ACM_LOCKED, 0)
                joint.set_constrained_components(live['TestOnlyRecoveryAnchor'].static_mesh_component, unreal.Name('None'), body, unreal.Name('None'))
                joint.set_linear_drive_acceleration_mode(False)
                joint.set_linear_position_drive(True, True, True)
                joint.set_linear_velocity_drive(True, True, True)
                joint.set_linear_drive_params(10000, 4000, 80000)
                joint.set_linear_position_target(unreal.Vector(0, 0, -100))
                body.wake_all_rigid_bodies()
                check('test_only_external_800n_physical_hoist_attached_after_reservation_without_actor_attachment',
                    force_limit_n=800, projection=False, mass_conditioning=False)
                done_at, phase = sim, 'cancel_hoist_raise'
        elif phase == 'cancel_hoist_raise':
            pallet = live['CancelPallet']
            body = pallet.get_component_by_class(unreal.StaticMeshComponent)
            body.wake_all_rigid_bodies()
            assert body.is_simulating_physics() and body.is_gravity_enabled() and not pallet.get_attach_parent_actor()
            assert sim-done_at < 10, 'External recovery hoist did not physically clear the roller bed'
            centre, extent = pallet.get_actor_bounds(False)
            if centre.z-extent.z > 100 and body.get_physics_linear_velocity().length() < 5:
                assert live['CancelLift'].get_editor_property('reserved_conveyor') == live['CancelPort0'], 'A pallet still inside the handoff volume lost its reservation'
                recovery_hoist.get_component_by_class(unreal.PhysicsConstraintComponent).set_linear_position_target(unreal.Vector(0, -200, -100))
                check('external_recovery_hoist_lifts_actual_pallet_above_bed_while_infeed_reservation_is_retained', bottom_cm=round(centre.z-extent.z, 3))
                done_at, phase = sim, 'cancel_recovery'
        elif phase == 'cancel_recovery':
            cancel_lift, line, pallet = live['CancelLift'], live['CancelPort0'], live['CancelPallet']
            body = pallet.get_component_by_class(unreal.StaticMeshComponent)
            body.wake_all_rigid_bodies()
            assert body.is_simulating_physics() and body.is_gravity_enabled() and not pallet.get_attach_parent_actor()
            assert abs(body.get_mass()-25) < .1
            assert not cancel_lift.get_editor_property('accepted_pallet') and cancel_lift.get_editor_property('completed_transfers') == 0
            assert sim-done_at < 14, ('Manually withdrawn infeed retained a stale reservation', line.get_editor_property('status'), pallet.get_actor_location())
            centre, extent = pallet.get_actor_bounds(False)
            outside_source = abs(centre.y+1800)-extent.y > 62.5 or centre.z+extent.z < 39
            if (outside_source and abs(pallet.get_actor_location().y+1800) > 90 and
                    not cancel_lift.get_editor_property('reserved_conveyor') and line.is_idle_for_layout() and
                    abs(line.get_editor_property('actual_surface_speed_cm')) < 1):
                assert pallet not in line.get_physical_loads()
                assert abs(line.get_editor_property('actual_surface_speed_cm')) < 1
                check('physical_preaccept_withdrawal_cancels_stale_reservation_without_cargo_repair',
                      seconds=round(sim-done_at, 2), pallet_cm=[getattr(pallet.get_actor_location(), a) for a in 'xyz'],
                      status=line.get_editor_property('status'), stimulus='TEST-ONLY finite 800N external hoist')
                recovery_hoist.get_component_by_class(unreal.PhysicsConstraintComponent).break_constraint()
                recovery_hoist.destroy_actor()
                recovery_hoist = None
                assert body.is_simulating_physics() and body.is_gravity_enabled() and not pallet.get_attach_parent_actor()
                assert cancel_lift.reserve_conveyor(live['CancelPort1'])
                cancel_lift.release_conveyor(live['CancelPort1'])
                check('another_controller_can_reserve_lift_after_physical_infeed_cancellation')
                finish(True, 'Physical preaccept infeed withdrawal releases its stale lift reservation' if withdrawal_only else
                    'Native roller motion, ZPA, endpoints, 1F->4F lift and AGV delivery; physical preaccept infeed withdrawal releases its stale lift reservation')
    except Exception:
        finish(False, traceback.format_exc())

callback_busy = False


def tick(dt):
    global callback_busy
    if callback_busy:
        return
    callback_busy = True
    try:
        return _tick_impl(dt)
    finally:
        callback_busy = False


try:
    fixture()
    handle = unreal.register_slate_post_tick_callback(tick)
except Exception:
    finish(False, traceback.format_exc())
