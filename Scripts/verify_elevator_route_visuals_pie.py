"""Rendered native 1->3 empty / 3->2 loaded transfer and route-arrow regression.

Copies the saved vehicle rig into an unsaved three-floor Entry fixture. A real
work order, finite motors, doors/platform and Chaos contacts perform the trip.
No actor poses, velocities, AI states or routes are assigned after PIE starts.
Only camera/floor visibility and the production 4x clock are controlled.
Use -SensorRegressionSkipCapture for native transport/component checks without
screenshots; this does not certify rendered image quality.
"""
import json
from pathlib import Path
import sys
import time
import traceback
import unreal

SKIP_CAPTURE = '-sensorregressionskipcapture' in unreal.SystemLibrary.get_command_line().lower()
GUIDES_DIAGNOSTIC = globals().get('BUILDING_GUIDES_DIAGNOSTIC', False)
GUIDES_ONLY = GUIDES_DIAGNOSTIC or globals().get('BUILDING_GUIDES_ONLY', False) or '-elevatorguidesonly' in unreal.SystemLibrary.get_command_line().lower()
FOUR_FLOORS = GUIDES_ONLY or globals().get('BUILDING_FOUR_FLOOR_CHECKS', False) or '-fourfloorelevatorchecks' in unreal.SystemLibrary.get_command_line().lower()
TOP_FLOOR, DELIVERY_FLOOR = (3, 0) if FOUR_FLOORS else (2, 1)
JOB_ID = f'ARROWS-L1-L{TOP_FLOOR + 1}-L{DELIVERY_FLOOR + 1}'
if SKIP_CAPTURE:
    print('ELEVATOR_ROUTE_CAPTURE_SKIPPED', 'Native two-trip transport, floor and arrow-component assertions retained; no rendered-image claim')

sys.path.insert(0, str(Path(__file__).parent))
from apply_real_world_scale import fit

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
source = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'WH_AutonomousForklift')
rig = {c.get_name(): (c.get_relative_transform(), c.static_mesh, list(c.get_editor_property('override_materials')))
       for c in source.get_components_by_class(unreal.StaticMeshComponent)}
assert level.load_level('/Engine/Maps/Entry')
for a in list(actors.get_all_level_actors()):
    if not isinstance(a, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(a)
world = editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property(
    'default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))


def spawn(cls, label, point, yaw=0):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point), unreal.Rotator(yaw=yaw))
    actor.set_actor_label(label)
    return actor


for floor in range(TOP_FLOOR + 1):
    for name, x, y, width, depth in (
        ('South', 0, -288, 4000, 3424), ('West', -570.5, 1712, 2859, 576),
        ('East', 1570.5, 1712, 859, 576), ('North', 1000, 1888, 282, 224),
    ):
        deck = spawn(unreal.StaticMeshActor, f'ArrowL{floor + 1}{name}', (x, y, floor * 400 - 10))
        deck.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
        deck.static_mesh_component.set_collision_profile_name('BlockAll')
        deck.set_actor_scale3d(unreal.Vector(width / 100, depth / 100, .2))
        deck.tags = ['WarehouseMapStructure'] + ([f'WarehouseFloor{floor}'] if floor else [])
pit = spawn(unreal.StaticMeshActor, 'ArrowPitBase', (1000, 1600, -30))
pit.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
pit.static_mesh_component.set_collision_profile_name('BlockAll')
pit.set_actor_scale3d(unreal.Vector(2.82, 3.52, .2))
pit.tags = ['WarehouseMapStructure']
lift = spawn(unreal.WarehouseElevator, 'ArrowElevator', (1000, 1600, 0), 90)
assert lift.set_floor_count(TOP_FLOOR + 1)
if FOUR_FLOORS:
    assert len(lift.get_editor_property('gates')) == 8, 'Four-floor lift is missing native gate references'
vehicle = spawn(unreal.WarehouseForklift, 'ArrowAGV', (300, 400, .1), 90)
vehicle.set_editor_property('auto_start', False)
vehicle.set_editor_property('pending_jobs', [])
vehicle.set_editor_property('idle_parking_enabled', False)
vehicle.set_editor_property('elevator', lift)
for part in vehicle.get_components_by_class(unreal.StaticMeshComponent):
    transform, mesh, materials = rig[part.get_name()]
    part.set_relative_transform(transform, False, True)
    part.set_static_mesh(mesh)
    part.set_editor_property('override_materials', materials)
pallet = spawn(unreal.WarehousePallet, 'ArrowPallet', (600, -600, TOP_FLOOR * 400 + .1), 90)
cargo = spawn(unreal.WarehouseCargo, 'ArrowCargo', (600, -600, TOP_FLOOR * 400 + 25.25))
cargo.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1'))
fit(cargo, (30, 24, 20), (600, -600, TOP_FLOOR * 400 + 25.25))
cargo.set_gross_mass_kg(12)
strength = spawn(unreal.WarehouseDamageSystem, 'ArrowStrength', (-1500, -1500, 0))
strength.set_editor_property('objects', [unreal.WarehouseStrength(name=a.get_actor_label(), members=[a], mass_kg=m,
    rated_load_kg=1500, impact_yield_j=10000, impact_failure_j=100000) for a, m in ((pallet, 25), (cargo, 12))])
spawn(unreal.PlayerStart, 'ArrowPlayerStart', (-1500, -1500, 100))
sun = spawn(unreal.DirectionalLight, 'ArrowSun', (0, 0, 1800))
sun.set_actor_rotation(unreal.Rotator(pitch=-60, yaw=-35), False)
sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(4)
sun.get_component_by_class(unreal.DirectionalLightComponent).set_cast_shadows(False)
spawn(unreal.SkyAtmosphere, 'ArrowSky', (0, 0, 0))
camera = spawn(unreal.CameraActor, 'ArrowCamera', (0, 300, 1800))
camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(65)

out = Path(unreal.Paths.project_saved_dir()) / ('Testing/BuildingFourFloorGuideDiagnostic' if GUIDES_DIAGNOSTIC else 'Testing/BuildingFourFloorGuides' if GUIDES_ONLY else
    'Testing/BuildingFourFloorLift' if FOUR_FLOORS else 'Testing/ElevatorRouteVisuals')
out.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
phase = 'start'
live = None
pc = vehicle = lift = pallet = cargo = camera = None
state_at = started
last_state = None
seen = set()
report = []
captures = set()
layer = None
layer_at = 0
layer_sim = 0
done_at = None
guide_at = None
guide_positions = None
guide_stable_at = None


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def arrows():
    for actor in unreal.GameplayStatics.get_all_actors_of_class(pc, unreal.Actor):
        if actor.actor_has_tag('TransportDisplay'):
            for part in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
                if part.component_has_tag('TransportRouteVisual'):
                    return part
    return None


def view(floor):
    pc.set_observer_floor(floor)
    pc.set_layout_editing(False)
    center = unreal.Vector(1000, 1370, floor * 400 + 40)
    eye = center + unreal.Vector(1100, -950, 1300)
    camera.set_actor_location(eye, False, True)
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, center), False)
    pc.set_view_target_with_blend(camera, 0)


def finish():
    (out / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def assert_guide_positions(expected_count, previous):
    """Count changes must move neither real resting doors nor the locked deck."""
    assert lift.get_editor_property('floor_count') == expected_count
    parts = lift.get_components_by_class(unreal.StaticMeshComponent)
    platform = next(p for p in parts if p.get_name() == 'LiftPlatform')
    assert platform.is_simulating_physics() and abs(platform.get_mass() - 500) < .1
    assert abs(platform.get_world_location().z + 5) < .5, ('Ground deck guide moved', platform.get_world_location())
    assert platform.get_physics_linear_velocity().length() < 1
    gates = [p for p in parts if p.get_name().startswith('Gate_')]
    assert len(gates) == 8, ('Fourth-floor native doors missing', [p.get_name() for p in gates])
    assert len(lift.get_editor_property('gates')) == 8
    readouts = []
    for gate in gates:
        floor = int(gate.get_name().split('_')[1])
        if floor >= expected_count:
            assert not gate.is_visible() and not gate.is_simulating_physics()
            assert gate.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
            continue
        assert gate.is_visible() and gate.is_simulating_physics()
        assert gate.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
        assert abs(gate.get_mass() - 80) < .1, (gate.get_name(), gate.get_mass())
        assert abs(gate.get_world_location().z - (floor * 400 + 135)) < .5, ('Door guide height moved', gate.get_name(), gate.get_world_location())
        assert gate.get_physics_linear_velocity().length() < 2
        local = lift.get_actor_transform().inverse_transform_location(gate.get_world_location())
        side = int(gate.get_name().split('_')[2])
        free_error = local.y-side*60
        assert abs(local.x+205) < .5, ('Door locked-axis guide moved', gate.get_name(), local)
        assert abs(free_error) < .5, ('Door has not physically settled closed on its signed sliding axis', gate.get_name(), free_error, local)
        if gate.get_name() in previous:
            assert (gate.get_world_location() - previous[gate.get_name()]).length() < .5, ('Door moved with guide centre', gate.get_name(), gate.get_world_location(), previous[gate.get_name()])
        readouts.append(dict(gate=gate.get_name(), locked_axis_error_cm=local.x+205, signed_closed_error_cm=free_error,
            height_error_cm=local.z-(floor*400+135), speed_cm_s=gate.get_physics_linear_velocity().length(), mass_kg=gate.get_mass()))
    return readouts


def resting_gate_positions():
    return {part.get_name(): part.get_world_location() for part in lift.get_components_by_class(unreal.StaticMeshComponent)
            if part.get_name().startswith('Gate_')}


def assert_no_instant_gate_move(previous):
    for name, point in resting_gate_positions().items():
        assert (point-previous[name]).length() < .01, ('Floor-count edit instantly moved a real door', name, point, previous[name])


def joint_diagnostics():
    """Read reflected native reference frames without changing any body."""
    rows = []
    if not lift:
        return rows
    for joint in lift.get_components_by_class(unreal.PhysicsConstraintComponent):
        row = dict(joint=joint.get_name())
        try:
            anchor, _, leaf, _ = joint.get_constrained_components()
            row.update(anchor=anchor.get_name() if anchor else None, leaf=leaf.get_name() if leaf else None)
            instance = joint.get_editor_property('constraint_instance')
            for key in ('pos1', 'pos2', 'pri_axis1', 'sec_axis1', 'pri_axis2', 'sec_axis2'):
                value = instance.get_editor_property(key)
                row[key] = [value.x, value.y, value.z]
            drive = instance.get_editor_property('profile_instance').get_editor_property('linear_drive')
            target = drive.get_editor_property('position_target')
            row['drive_target_cm'] = [target.x, target.y, target.z]
            limit = instance.get_editor_property('profile_instance').get_editor_property('linear_limit')
            row['linear_limit_cm'] = limit.get_editor_property('limit')
            row['linear_contact_distance_cm'] = limit.get_editor_property('contact_distance')
            force, torque = joint.get_constraint_force()
            row['constraint_force'] = [force.x, force.y, force.z]
            row['constraint_torque'] = [torque.x, torque.y, torque.z]
            if anchor:
                origin = anchor.get_world_location()
                rotation = anchor.get_world_rotation()
                frame = unreal.Transform(location=origin, rotation=rotation)
                point = frame.transform_location(instance.get_editor_property('pos1'))
                local = lift.get_actor_transform().inverse_transform_location(point)
                row['anchor_frame_in_shaft_cm'] = [local.x, local.y, local.z]
        except Exception as error:
            row['diagnostic_unavailable'] = str(error)
        rows.append(row)
    return rows


def gate_diagnostics():
    rows = []
    if not lift:
        return rows
    for part in lift.get_components_by_class(unreal.StaticMeshComponent):
        if not part.get_name().startswith('Gate_'):
            continue
        local = lift.get_actor_transform().inverse_transform_location(part.get_world_location())
        velocity = part.get_physics_linear_velocity()
        rotation = part.get_world_rotation()
        row = dict(gate=part.get_name(), local_position_cm=[local.x, local.y, local.z],
            world_rotation_degrees=[rotation.pitch, rotation.yaw, rotation.roll], velocity_cm_s=[velocity.x, velocity.y, velocity.z],
            simulated=part.is_simulating_physics(), collision=str(part.get_collision_enabled()))
        try:
            types = [getattr(unreal.ObjectTypeQuery, 'OBJECT_TYPE_QUERY'+str(index)) for index in range(1, 7)]
            overlaps = unreal.SystemLibrary.component_overlap_components(part, part.get_world_transform(), types, unreal.PrimitiveComponent, [])
            row['overlap_query'] = str(overlaps)[:4000]
        except Exception as error:
            row['overlap_diagnostic_unavailable'] = str(error)
        rows.append(row)
    return rows


def _tick_impl(dt):
    global phase, live, pc, vehicle, lift, pallet, cargo, camera, last_state, state_at, layer, layer_at, layer_sim, done_at, guide_at, guide_positions, guide_stable_at
    try:
        # Allow the complete native trip and final delivery under slower test hosts.
        assert time.monotonic() - started < 900, ('Transfer visualization timeout', last_state, report[-3:])
        if phase == 'start':
            if time.monotonic() - started < 2:
                return
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        wall = time.monotonic()
        if live is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            vehicle, lift, pallet, cargo, camera = [live[n] for n in ('ArrowAGV', 'ArrowElevator', 'ArrowPallet', 'ArrowCargo', 'ArrowCamera')]
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
        if phase == 'settle':
            if now < 3:
                return
            assert cargo in live['ArrowStrength'].get_supported_actors(pallet), 'Fixture cargo is not supported'
            view(0)
            assert pc.set_simulation_speed(4)
            if GUIDES_ONLY:
                guide_at = now
                phase = 'settle_guides'
                return
            job = unreal.WarehouseWorkOrder(job_id=JOB_ID, source_system='FMS-VISUAL-TEST', pallet=pallet,
                destination=unreal.Transform(location=unreal.Vector(600, -300, DELIVERY_FLOOR * 400), rotation=unreal.Rotator(yaw=90)))
            assert vehicle.submit_job(job)
            vehicle.toggle_power()
            phase = 'run'
            return
        assert body(pallet).is_simulating_physics() and body(cargo).is_simulating_physics()
        assert not pallet.get_attach_parent_actor() and not cargo.get_attach_parent_actor()
        assert abs(vehicle.get_physical_mass_kg() - 1000) < .1 and abs(body(cargo).get_mass() - 12) < .1
        state = vehicle.get_editor_property('ai_state')
        assert state != unreal.WarehouseAIState.FAULT, (vehicle.get_editor_property('status'), lift.get_editor_property('status'),
            vehicle.get_actor_location(), vehicle.get_actor_rotation(), vehicle.get_editor_property('current_speed_cm'))
        if phase == 'settle_guides':
            # Idle alone does not certify resting physical leaves. Establish a
            # strict signed closed-position/low-speed baseline before resizing.
            try:
                readouts = assert_guide_positions(4, {})
            except AssertionError:
                guide_stable_at = None
                if now-guide_at < 10:
                    return
                raise
            if guide_stable_at is None:
                guide_stable_at = now
            if now-guide_stable_at < 1:
                return
            guide_positions = resting_gate_positions()
            report.append(dict(guide_floor_count=4, mode='idle_guides_only' if GUIDES_ONLY else 'post_transport_guides',
                               baseline_stable_seconds=now-guide_stable_at, doors=readouts, joints=joint_diagnostics()))
            if GUIDES_DIAGNOSTIC:
                # Wake only the fixture's eight empty mechanical leaves. This
                # distinguishes pre-existing contact/limit equilibrium from a
                # floor-count rebuild; no position, speed or mass is assigned.
                for part in lift.get_components_by_class(unreal.StaticMeshComponent):
                    if part.get_name().startswith('Gate_') and part.is_simulating_physics():
                        part.wake_all_rigid_bodies()
                guide_at = now
                phase = 'wake_guides'
                return
            assert lift.set_floor_count(3), lift.get_editor_property('status')
            assert_no_instant_gate_move(guide_positions)
            guide_at = now
            phase = 'shrink_guides'
            return
        if phase == 'wake_guides':
            if now-guide_at < 2:
                return
            report.append(dict(stage='native_empty_gate_wake_without_floor_count_change', floor_count=lift.get_editor_property('floor_count'),
                elapsed_sim_seconds=now-guide_at, doors=gate_diagnostics(), joints=joint_diagnostics(),
                diagnostic_only=True, transport_tested=False))
            print('FOUR_FLOOR_ELEVATOR_GUIDE_DIAGNOSTIC_COMPLETE', json.dumps(report[-1], ensure_ascii=False))
            finish()
            return
        if phase in ('shrink_guides', 'expand_guides'):
            assert_rig_count = 3 if phase == 'shrink_guides' else 4
            if now - guide_at < 2:
                return
            readouts = assert_guide_positions(assert_rig_count, guide_positions)
            report.append({'guide_floor_count': assert_rig_count, 'stable_seconds': now-guide_at,
                           'platform_mass_kg': 500, 'active_gate_mass_kg': 80, 'doors': readouts, 'joints': joint_diagnostics()})
            if phase == 'shrink_guides':
                guide_positions = resting_gate_positions()
                assert lift.set_floor_count(4), lift.get_editor_property('status')
                assert_no_instant_gate_move(guide_positions)
                guide_at = now
                phase = 'expand_guides'
                return
            scope = ('Idle native 4->3->4 physical guide/door activation, signed closed positions and mass stability; no transport claim' if GUIDES_ONLY else
                     'Native empty vehicle 1->4, 37kg loaded vehicle 4->1; real unload; settled idle 4->3->4 native guide and gate mass stability')
            report.append(dict(passed=True, scope=scope))
            print('FOUR_FLOOR_ELEVATOR_GUIDES_PIE_PASSED' if GUIDES_ONLY else 'FOUR_FLOOR_ELEVATOR_PIE_PASSED', scope)
            finish()
            return
        stage = {
            unreal.WarehouseAIState.ELEVATOR_APPROACH: 'approach', unreal.WarehouseAIState.ELEVATOR_BOARD: 'board',
            unreal.WarehouseAIState.ELEVATOR_RIDE: 'ride', unreal.WarehouseAIState.ELEVATOR_EXIT: 'exit',
            unreal.WarehouseAIState.NAVIGATE_PICKUP: 'resume_pickup', unreal.WarehouseAIState.NAVIGATE_DELIVERY: 'resume_delivery',
        }.get(state)
        trip = lift.get_editor_property('completed_transfers')
        if state != last_state:
            state_at, last_state = wall, state
            floor = lift.floor_at_height(vehicle.get_actor_location().z)
            if stage != 'ride' and floor >= 0:
                view(floor)
            print('ELEVATOR_ARROW_STATE', now, state, 'trip', trip, 'pose', vehicle.get_actor_location())
        part = arrows()
        route = vehicle.get_display_route()
        key = (trip, stage)
        if stage and wall - state_at > .25 and key not in seen:
            if stage == 'ride':
                assert not route and (not part or part.get_instance_count() == 0), 'Stale floor arrows during vertical ride'
            else:
                floor = lift.floor_at_height(vehicle.get_actor_location().z)
                assert floor >= 0 and len(route) >= 2, ('Missing displayed route', stage, route)
                assert all(abs(p.z - (floor * 400 + .8)) < .1 for p in route), ('Wrong floor route', stage, route)
                if stage in ('approach', 'board', 'exit'):
                    end = (lift.boarding_pose(floor) if stage == 'board' else lift.waiting_pose(floor)).translation
                    assert (unreal.Vector(route[-1].x, route[-1].y, end.z) - end).length() < .5, ('Wrong elevator endpoint', stage, route[-1], end)
                assert part and part.is_visible() and part.get_instance_count() >= 2, ('Missing rendered chevrons', stage)
                assert part.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
                assert not unreal.WarehouseDamageSystem.is_physical_contact(part)
                assert part.get_editor_property('custom_depth_stencil_value') == 128
                assert body(pallet).get_editor_property('custom_depth_stencil_value') == 192
                assert abs(part.get_instance_transform(0, True).translation.z - (floor * 400 + .8)) < .1
                filename = f'{trip}_{stage}_L{floor + 1}.png'
                if not SKIP_CAPTURE:
                    unreal.SystemLibrary.execute_console_command(game, 'HighResShot 1600x900 filename=' + (out / filename).as_posix(), pc)
                    captures.add(filename)
            seen.add(key)
            report.append({'trip': trip, 'stage': stage, 'time': now, 'floor': lift.floor_at_height(vehicle.get_actor_location().z),
                           'route': [[p.x, p.y, p.z] for p in route], 'bars': part.get_instance_count() if part else 0})
            print('ELEVATOR_ARROW_STAGE_PASSED', trip, stage)
        # Exercise the actual observer layer filter on the upper-floor exit.
        if state == unreal.WarehouseAIState.ELEVATOR_EXIT and trip == 0 and key in seen:
            if layer is None:
                # HighResShot renders on a subsequent frame. Let it finish on
                # L3 before changing layers, and require real game ticks as
                # well as wall time so a screenshot stall cannot fake a wait.
                layer, layer_at, layer_sim = 'capture', wall, now
            elif wall - layer_at > .4 and now - layer_sim > .3 and layer == 'capture':
                view(0)
                layer, layer_at, layer_sim = 'hidden', wall, now
            elif wall - layer_at > .4 and now - layer_sim > .3 and layer == 'hidden':
                assert part and not part.is_visible(), 'Upper-floor route visible in L1 observation'
                view(TOP_FLOOR)
                layer, layer_at, layer_sim = 'restored', wall, now
            elif wall - layer_at > .4 and now - layer_sim > .3 and layer == 'restored':
                assert part.is_visible() and part.get_instance_count() >= 2, 'Upper-floor route did not restore'
                layer = 'done'
                print('ELEVATOR_ARROW_OBSERVER_FILTER_PASSED')
        if JOB_ID in vehicle.get_editor_property('completed_job_ids'):
            if done_at is None:
                done_at = wall
            if wall - done_at < .5:
                return
            required = {(trip, stage) for trip in (0, 1) for stage in ('approach', 'board', 'ride', 'exit')}
            required |= {(1, 'resume_pickup'), (2, 'resume_delivery')}
            assert required <= seen, ('Unobserved transfer stages', required - seen)
            assert layer == 'done' and lift.get_editor_property('completed_transfers') == 2
            assert not route and part.get_instance_count() == 0, 'Completed route arrows remained'
            c, e = pallet.get_actor_bounds(False)
            assert abs(c.z - e.z - DELIVERY_FLOOR * 400) < 2, ('Pallet did not unload on requested floor', c, e)
            if not SKIP_CAPTURE:
                assert all((out / name).is_file() for name in captures), 'Screenshot missing'
            if FOUR_FLOORS:
                if lift.get_editor_property('state') != unreal.WarehouseElevatorState.IDLE:
                    assert wall - done_at < 30, ('Elevator did not close idle after unloading', lift.get_editor_property('status'))
                    return
                guide_at = now
                phase = 'settle_guides'
                return
            print('ELEVATOR_ROUTE_VISUALS_PIE_PASSED', 'native empty 1->3, loaded 3->2, floor arrows, observer filter, completion cleanup')
            finish()
    except Exception:
        report.append(dict(passed=False, mode='idle_guides_only' if GUIDES_ONLY else 'native_transport', error=traceback.format_exc(), joints=joint_diagnostics(), doors=gate_diagnostics()))
        print('FOUR_FLOOR_ELEVATOR_PIE_FAILED' if FOUR_FLOORS else 'ELEVATOR_ROUTE_VISUALS_PIE_FAILED', traceback.format_exc())
        finish()


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


handle = unreal.register_slate_post_tick_callback(tick)
