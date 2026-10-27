"""Human physical floor buttons, lamps and native rides in an unsaved production rig.

No passenger pose/velocity reset or attachment. Test obstacles are spawned and
removed after their interlock assertion; the original warehouse is never saved.
"""
import json
import time
import traceback
from datetime import datetime, timedelta, timezone
from pathlib import Path
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
out = Path(unreal.Paths.project_dir()).resolve() / 'Saved/Testing/HumanElevator'
out.mkdir(parents=True, exist_ok=True)
case_file = out / 'Case.json'
case = json.loads(case_file.read_text(encoding='utf-8')) if case_file.is_file() else {}
ui_only = '-humanelevatoruionly' in unreal.SystemLibrary.get_command_line().lower()
simulation_speed = int(globals().get('HUMAN_LIFT_SPEED', 4))
assert simulation_speed in (1, 2, 4)
assert not editor.get_game_world()
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
source = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'WH_AutonomousForklift')
rig_parts = {c.get_name(): (c.static_mesh, list(c.get_editor_property('override_materials'))) for c in source.get_components_by_class(unreal.StaticMeshComponent)}
rig_poses = {c.get_name(): unreal.MathLibrary.make_relative_transform(c.get_world_transform(), source.get_actor_transform()) for c in source.get_components_by_class(unreal.SceneComponent) if c != source.root_component}
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
settings.set_editor_property('default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
cube = unreal.load_asset('/Engine/BasicShapes/Cube')


def block(label, position, size):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position))
    actor.set_actor_label(label)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_collision_profile_name('BlockAll')
    actor.set_actor_scale3d(unreal.Vector(*(n / 100 for n in size)))
    actor.tags = [unreal.Name('WarehouseMapStructure')]
    return actor


for floor in range(3):
    lo, hi = (-181, 181) if floor == 0 else (-181, 200)
    z = floor * 400 - 10
    block('HumanLiftWest' + str(floor), ((-2000 + lo) / 2, 0, z), (2000 + lo, 2000, 20))
    block('HumanLiftEast' + str(floor), ((hi + 1000) / 2, 0, z), (1000 - hi, 2000, 20))
    for side in (-1, 1):
        block('HumanLiftSide' + str(floor) + str(side), ((lo + hi) / 2, side * 570.5, z), (hi - lo, 859, 20))
block('HumanLiftPit', (0, 0, -30), (352, 282, 20))
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-340, -130, 110))
lift = actors.spawn_actor_from_class(unreal.WarehouseElevator, unreal.Vector())
lift.set_actor_label('HumanLift')
assert lift.set_floor_count(3)
lift.set_editor_property('use_cargo_model', True)
lift.apply_cargo_model()
crate = block('HumanLiftCrate', (65, 60, 20.5), (40, 40, 40))
crate.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
crate.static_mesh_component.set_simulate_physics(True)
crate.static_mesh_component.set_mass_override_in_kg(unreal.Name('None'), 12, True)
crate.tags = []
door_obstacle = block('HumanDoorObstacle', (-1200, 800, 30), (30, 30, 60))
door_obstacle.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
door_obstacle.static_mesh_component.set_collision_profile_name('NoCollision')
door_obstacle.tags = []
vehicle = actors.spawn_actor_from_class(unreal.WarehouseForklift, unreal.Vector(-500, 0, 0))
vehicle.set_actor_label('HumanLiftAGV')
for c in vehicle.get_components_by_class(unreal.StaticMeshComponent):
    mesh, mats = rig_parts[c.get_name()]
    c.set_static_mesh(mesh)
    c.set_editor_property('override_materials', mats)


def depth(component):
    n = 0
    while component.get_attach_parent():
        component = component.get_attach_parent()
        n += 1
    return n


for c in sorted(vehicle.get_components_by_class(unreal.SceneComponent), key=depth):
    if c != vehicle.root_component:
        c.set_world_transform(unreal.MathLibrary.compose_transforms(rig_poses[c.get_name()], vehicle.get_actor_transform()), False, True)
for name, value in dict(auto_start=False, idle_parking_enabled=False, pending_jobs=[], target_pallet=None, charging_station=None, elevator=lift).items():
    vehicle.set_editor_property(name, value)
target_pallet = actors.spawn_actor_from_class(unreal.WarehousePallet, unreal.Vector(-900, -500, 800.1))
target_pallet.set_actor_label('SharedLiftTarget')
actors.spawn_actor_from_class(unreal.WarehouseSimulationClock, unreal.Vector(-1700, -800, 0))
actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1500), unreal.Rotator(pitch=-45, yaw=30))
actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector())
sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 1500))
sky.get_component_by_class(unreal.SkyLightComponent).set_intensity(1.5)
fill = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(-20, -40, 210))
fill.get_component_by_class(unreal.PointLightComponent).set_intensity(2000)
fill.get_component_by_class(unreal.PointLightComponent).set_editor_property('attenuation_radius', 500)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
capture = '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower()
report = dict(date=datetime.now(timezone(timedelta(hours=9))).date().isoformat(), status='starting', ui_only=ui_only, simulation_speed=simulation_speed,
              runtime_passenger_teleports=False, tests=[], samples=[], screenshots=[])
began = time.monotonic()
phase = 'start'
phase_at = 0
pawn = pc = deck = body = target_pallet = None
shared_stop_position = None
shared_seen_stop = False
obstacle = None
trip = 0
last_state = None
frame_count = 0
in_tick = False
finished = False
capture_requested = {}


def enter(name, now):
    global phase, phase_at, frame_count
    phase, phase_at, frame_count = name, now, 0


def finish(error=None):
    global finished
    if finished:
        return
    finished = True
    report.update(status='failed' if error else 'passed', wall_seconds=time.monotonic() - began)
    try:
        def vector(value):
            return [value.x, value.y, value.z]
        report['final_state'] = dict(phase=phase, lift_state=str(lift.get_editor_property('state')),
            lift_status=lift.get_editor_property('status'), human_status=lift.get_editor_property('human_status'),
            current_floor=lift.get_editor_property('current_floor'), next_stop=lift.get_editor_property('destination_floor'),
            vehicle_destination=lift.get_editor_property('vehicle_destination_floor'), pending_stops=list(lift.get_editor_property('pending_stops')),
            human_payload_kg=lift.get_editor_property('human_payload_mass_kg'),
            vehicle_ai=str(vehicle.get_editor_property('ai_state')), vehicle_status=vehicle.get_editor_property('status'),
            vehicle_position=vector(vehicle.get_actor_location()), vehicle_speed=vehicle.get_editor_property('current_speed_cm'),
            person_position=vector(pawn.get_actor_location()), person_velocity=vector(body.get_physics_linear_velocity()),
            platform_position=vector(deck.get_world_location()), platform_velocity=vector(deck.get_physics_linear_velocity()),
            person_inside=lift.is_human_inside(pawn), can_enter=lift.can_enter(vehicle), can_exit=lift.can_exit(vehicle))
    except Exception as diagnostic_error:
        report['diagnostic_error'] = str(diagnostic_error)
    if error:
        report['error'] = error
    (out / ('UIReport.json' if ui_only else 'Report.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('HUMAN_ELEVATOR_PIE_' + ('FAILED' if error else 'PASSED'), error or report['tests'])
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def screenshot(name):
    if capture:
        capture_requested[name + '.png'] = time.time()
        unreal.SystemLibrary.execute_console_command(editor.get_game_world(),
            'HighResShot 1600x900 filename="' + str(out / (name + '.png')).replace('\\', '/') + '"', pc)
        report['screenshots'].append(name + '.png')


def screenshots_ready():
    return all((out / name).is_file() and (out / name).stat().st_size > 0 and
               (out / name).stat().st_mtime >= requested - 1
               for name, requested in capture_requested.items())


def tick(dt):
    global pawn, pc, lift, crate, vehicle, deck, body, obstacle, trip, last_state, frame_count, in_tick, target_pallet, shared_stop_position, shared_seen_stop
    # High-resolution screenshots may pump Slate and invoke this callback again.
    if in_tick or finished:
        return
    in_tick = True
    try:
        assert time.monotonic() - began < 1200, ('Human elevator timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            enter('settle', 0)
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        frame_count += 1
        if pawn is None:
            named = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            pawn = unreal.GameplayStatics.get_player_character(game, 0)
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            assert pc.set_simulation_speed(simulation_speed), 'Production simulation clock rejected supported speed'
            unreal.SystemLibrary.execute_console_command(game, 'r.SetRes 1440x1000w', pc)
            lift, crate, vehicle, obstacle, target_pallet = (named[n] for n in ('HumanLift', 'HumanLiftCrate', 'HumanLiftAGV', 'HumanDoorObstacle', 'SharedLiftTarget'))
            deck = next(c for c in lift.get_components_by_class(unreal.StaticMeshComponent) if c.get_name() == 'LiftPlatform')
            body = pawn.get_component_by_class(unreal.CapsuleComponent)
        state = lift.get_editor_property('state')
        if str(state) != last_state:
            report.setdefault('events', []).append(dict(t=now, phase=phase, state=str(state), status=lift.get_editor_property('status'), human_status=lift.get_editor_property('human_status')))
            last_state = str(state)
            parts = {c.get_name(): c for c in lift.get_components_by_class(unreal.StaticMeshComponent)}
            labels = {c.get_name(): c for c in lift.get_components_by_class(unreal.TextRenderComponent)}
            position = lift.get_position_indicator_text()
            for floor in range(3):
                label = str(labels['HumanCallLabel_' + str(floor)].get_editor_property('text'))
                current = str(lift.get_editor_property('current_floor') + 1)
                assert 'CALL L' + str(floor + 1) in label and any(kind + ' L' + current in label for kind in ('CAR', 'LAST')), ('Exterior current-floor indicator stale', floor, label, position)
                for prefix, lit in (('HumanFloorButton_', lift.is_destination_button_lit(floor)), ('HumanCallLamp_', lift.is_call_button_lit(floor))):
                    material = parts[prefix + str(floor)].get_material(0)
                    assert ('M_Sensor_status_green' in material.get_path_name()) == lit, ('Lamp material disagrees with accepted request', prefix, floor)
        guidance = None
        if state in (unreal.WarehouseElevatorState.CLOSING_FOR_CALL, unreal.WarehouseElevatorState.CLOSING_LOADED, unreal.WarehouseElevatorState.CLOSING_EMPTY):
            guidance = '문을 닫고 있습니다'
        elif state in (unreal.WarehouseElevatorState.CALLING, unreal.WarehouseElevatorState.TRAVELLING):
            guidance = str(lift.get_editor_property('destination_floor') + 1) + '층으로 이동 중입니다'
        if guidance and not lift.check_interlocks():
            human_status = lift.get_editor_property('human_status')
            assert guidance in human_status and '문이 열렸습니다' not in human_status, ('Human guidance contradicts moving/closing state', state, human_status)
            prompt = lift.get_human_interaction_text(pawn)
            assert not prompt or guidance in prompt, ('Visible interaction hint is stale', state, prompt)
            checked = report.setdefault('human_guidance_states', [])
            if str(state) not in checked:
                checked.append(str(state))
        assert body.is_simulating_physics() and not pawn.is_physical_ragdoll(), ('Passenger lost native upright physics', phase)
        assert not pawn.get_attach_parent_actor(), 'Passenger was attached to elevator'
        if state == unreal.WarehouseElevatorState.TRAVELLING and phase not in ('shared_continue', 'shared_finish'):
            assert lift.are_landing_doors_closed(), 'Travelling with an open landing'
            feet = body.get_world_location().z - body.get_scaled_capsule_half_height()
            deck_top = deck.get_world_location().z + 5
            assert abs(feet - deck_top) < 5, ('Person lost physical deck support', feet, deck_top)
            if not report['samples'] or now - report['samples'][-1]['t'] > .2:
                report['samples'].append(dict(t=now, trip=trip, feet_cm=feet, deck_cm=deck_top, crate_cm=crate.get_actor_location().z if crate else None, input_ignored=pc.is_move_input_ignored()))
            if crate:
                assert crate.static_mesh_component.is_simulating_physics() and not crate.get_attach_parent_actor()
                assert abs(crate.get_actor_location().z - deck_top - 20) < 3, 'Loose crate did not physically ride'
        if phase == 'settle':
            if now < 2:
                return
            assert abs(deck.get_mass() - 500) < .2, ('Deck mass changed', deck.get_mass())
            assert lift.human_landing_floor(pawn) == 0
            lift.set_editor_property('power_available', False)
            assert not lift.request_human_call(pawn, 0)
            assert 'POWER LOST' in lift.get_editor_property('human_status'), 'Power refusal reason was overwritten'
            lift.set_editor_property('power_available', True)
            assert not lift.request_human_call(pawn, 3)
            assert '외부 호출대' in lift.get_editor_property('human_status'), 'Invalid call refusal reason was overwritten'
            assert lift.request_human_call(pawn, 0), lift.get_editor_property('human_status')
            report['tests'].extend(['exterior call range/floor', 'power interlock'])
            enter('call_open', now)
        elif phase in ('call_open', 'return_call_open'):
            if state not in (unreal.WarehouseElevatorState.AWAIT_BOARDING, unreal.WarehouseElevatorState.AWAIT_EXIT):
                return
            enter('board', now)
        elif phase == 'board':
            target = unreal.Vector(0, 0, trip * 400 + 100)
            direction = target - pawn.get_actor_location()
            direction.z = 0
            if direction.length() > 15:
                pawn.add_movement_input(direction / direction.length(), 1, True)
                return
            enter('panel_aim', now)
        elif phase == 'panel_aim':
            camera = pawn.get_components_by_class(unreal.CameraComponent)[0]
            panel = next(c for c in lift.get_components_by_class(unreal.StaticMeshComponent) if c.get_name() == 'HumanFloorButton_' + str(1 - trip))
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_world_location(), panel.get_world_location()))
            if now - phase_at < .6:
                return
            assert lift.can_use_human_panel(pawn) and pawn.get_interaction_target() == lift
            assert lift.human_button_at_sight(pawn) == 1 - trip, 'Actual world floor button was not selected'
            assert not unreal.GameplayStatics.is_game_paused(game) and not pc.is_move_input_ignored()
            if ui_only:
                enter('ui_capture_wait', now)
                return
            if trip == 0:
                lift.set_editor_property('rated_mass_kg', 85)
                pawn.toggle_carry()
                assert not lift.is_destination_button_lit(1), 'Rejected overload lit destination'
                assert 'OVERLOAD' in lift.get_editor_property('human_status'), 'Overload refusal reason was overwritten'
                assert abs(lift.get_editor_property('human_payload_mass_kg') - 92) < .5
                lift.set_editor_property('rated_mass_kg', 2000)
                report['tests'].append('people+cargo overload')
                # Activate an authored test obstacle; never reposition a rider.
                obstacle.set_actor_location(unreal.Vector(-205, 40, 30), False, True)
                obstacle.static_mesh_component.set_collision_profile_name('BlockAllDynamic')
                obstacle.static_mesh_component.set_simulate_physics(True)
            pawn.toggle_carry()
            assert lift.is_destination_button_lit(1 - trip), 'Actual E handler did not register floor button'
            assert not lift.is_destination_button_lit(trip)
            assert not lift.set_floor_count(4), 'Human service allowed building floor change'
            enter('blocked_door' if trip == 0 else 'ride', now)
            screenshot('HumanPanel' + str(trip))
        elif phase == 'ui_capture_wait':
            if now - phase_at < .6:
                return
            assert capture, 'UI evidence requires a rendered editor'
            assert not pc.is_move_input_ignored()
            enter('ui_finish_wait', now)
            screenshot('HumanPanelUI')
            report['tests'].append('actual E targets physical floor button without locking player')
        elif phase == 'ui_finish_wait':
            if frame_count < 2 or not screenshots_ready():
                assert now - phase_at < 5, 'Actual UI screenshot did not complete'
                return
            assert not pc.is_move_input_ignored()
            assert not unreal.GameplayStatics.is_game_paused(game)
            report['tests'].append('physical floor buttons retain player input')
            report['screenshots_verified_before_pie_end'] = True
            finish()
        elif phase == 'blocked_door':
            if now - phase_at < 4.5:
                return
            assert state in (unreal.WarehouseElevatorState.AWAIT_EXIT, unreal.WarehouseElevatorState.CLOSING_LOADED), ('Door obstruction did not stop departure', state)
            assert abs(deck.get_world_location().z + 5) < 1
            obstacle.destroy_actor()
            obstacle = None
            report['tests'].append('physical door light curtain')
            enter('ride', now)
        elif phase == 'ride':
            if state != unreal.WarehouseElevatorState.AWAIT_EXIT or lift.get_editor_property('current_floor') != 1 - trip:
                return
            assert lift.get_editor_property('current_floor') == 1 - trip
            assert lift.get_editor_property('completed_human_transfers') == trip + 1
            assert not pc.is_move_input_ignored()
            assert not unreal.GameplayStatics.is_game_paused(game)
            assert not lift.is_destination_button_lit(1 - trip), 'Arrived destination lamp remained on'
            assert ('L' + str(2 - trip)) in lift.get_position_indicator_text()
            report['tests'].append('native physical ride + floor button lamps trip ' + str(trip + 1))
            if crate:
                crate.destroy_actor()
                crate = None
            enter('exit', now)
        elif phase == 'exit':
            target = unreal.Vector(-360, -110, (1 - trip) * 400 + 100)
            direction = target - pawn.get_actor_location()
            direction.z = 0
            if direction.length() > 12:
                pawn.add_movement_input(direction / direction.length(), 1, True)
                return
            assert lift.human_landing_floor(pawn) == 1 - trip
            report['tests'].append('physical exit trip ' + str(trip + 1))
            enter('release', now)
        elif phase == 'release':
            if state != unreal.WarehouseElevatorState.IDLE or lift.get_editor_property('human_service'):
                return
            if trip == 0:
                trip = 1
                assert lift.request_human_call(pawn, 1), lift.get_editor_property('human_status')
                enter('return_call_open', now)
            else:
                assert lift.request_transfer(vehicle, 0, 2)
                assert lift.is_call_button_lit(0) and lift.is_destination_button_lit(2)
                assert lift.request_human_call(pawn, 0)
                assert lift.cancel_transfer(vehicle)
                assert lift.get_editor_property('reserved_vehicle') is None
                assert lift.is_call_button_lit(0) and not lift.is_destination_button_lit(2), 'Cancelling AGV discarded a shared human request'
                report['tests'].append('cancel unboarded FMS request preserves human same-floor call')
                enter('cancel_reopen', now)
        elif phase == 'cancel_reopen':
            if state != unreal.WarehouseElevatorState.AWAIT_EXIT:
                return
            assert not lift.is_call_button_lit(0)
            job = unreal.WarehouseWorkOrder(job_id='SHARED-LIFT-L1-L3', source_system='FMS-SHARED-TEST', pallet=target_pallet,
                destination=unreal.Transform(location=unreal.Vector(-900, -900, 800), rotation=unreal.Rotator(yaw=0)))
            assert vehicle.submit_job(job)
            vehicle.toggle_power()
            enter('shared_vehicle_board', now)
        elif phase == 'shared_vehicle_board':
            ai = vehicle.get_editor_property('ai_state')
            assert ai != unreal.WarehouseAIState.FAULT, (vehicle.get_editor_property('status'), lift.get_editor_property('status'))
            if lift.get_editor_property('reserved_vehicle') != vehicle:
                return
            assert lift.is_destination_button_lit(2), 'FMS destination did not light cabin button'
            if state == unreal.WarehouseElevatorState.AWAIT_BOARDING:
                # A second call at an already open landing must preserve entry
                # permission while the vehicle is physically driving aboard.
                before = lift.can_enter(vehicle)
                assert lift.request_human_call(pawn, 0)
                assert lift.get_editor_property('state') == state and lift.can_enter(vehicle) == before
            if ai != unreal.WarehouseAIState.ELEVATOR_RIDE:
                return
            assert lift.request_human_call(pawn, 0), 'Current landing should stay open for shared boarding'
            enter('shared_human_board', now)
        elif phase == 'shared_human_board':
            # Board after the vehicle is stopped; its original human safety
            # envelope is not disabled merely to make a shared cabin possible.
            pos = pawn.get_actor_location()
            goal = unreal.Vector(-260, 82, 100) if pos.x < -230 and abs(pos.y-82) > 4 else unreal.Vector(125, 82, 100)
            direction = goal - pos
            direction.z = 0
            if direction.length() > .75:
                # The side strip has centimetres of clearance. Use ordinary
                # analog walk input to settle instead of stopping a 350cm/s
                # full input anywhere inside a 4cm acceptance circle.
                pawn.add_movement_input(direction / direction.length(), min(1, max(.025, direction.length() / 70)), True)
                return
            if body.get_physics_linear_velocity().length() > 2:
                return
            assert lift.is_human_inside(pawn), ('Shared passenger is not wholly inside', pawn.get_actor_location(), body.get_world_rotation(), body.get_physics_linear_velocity())
            # This is the original failure geometry: a yawed circular capsule
            # beside the cabin wall, not a rotated square render bounding box.
            yaw = body.get_world_rotation().yaw
            assert abs((yaw + 45) % 90 - 45) > 20, ('Near-wall rotation regression needs diagonal yaw', yaw)
            report['rotated_passenger'] = dict(yaw_degrees=yaw, radius_cm=body.get_scaled_capsule_radius(),
                position=[pos.x, pos.y, pos.z], inside=True)
            report['tests'].append('diagonally yawed real capsule fits near cabin wall without enlarging clearance')
            assert lift.request_human_destination(pawn, 1), lift.get_editor_property('human_status')
            assert lift.is_destination_button_lit(1) and lift.is_destination_button_lit(2)
            assert lift.get_editor_property('human_payload_mass_kg') >= 79.5
            lift.set_editor_property('rated_mass_kg', 1000)
            assert 'OVERLOAD' in lift.check_interlocks(), 'Mixed vehicle+person mass was not summed'
            lift.set_editor_property('rated_mass_kg', 2000)
            assert not lift.can_exit(vehicle)
            report['tests'].append('AGV then human board same physical cabin; both destinations lit; combined overload')
            enter('shared_ride', now)
        elif phase == 'shared_ride':
            ai = vehicle.get_editor_property('ai_state')
            assert ai != unreal.WarehouseAIState.FAULT, (vehicle.get_editor_property('status'), lift.get_editor_property('status'))
            if state != unreal.WarehouseElevatorState.AWAIT_EXIT or lift.get_editor_property('current_floor') != 1:
                return
            assert ai == unreal.WarehouseAIState.ELEVATOR_RIDE and not lift.can_exit(vehicle)
            assert not lift.is_destination_button_lit(1) and lift.is_destination_button_lit(2)
            shared_stop_position = vehicle.get_actor_location()
            shared_seen_stop = True
            screenshot('SharedIntermediateStop')
            enter('shared_human_exit', now)
        elif phase == 'shared_human_exit':
            assert shared_seen_stop and not lift.can_exit(vehicle), 'AGV got exit permission at human-only stop'
            assert vehicle.get_editor_property('ai_state') == unreal.WarehouseAIState.ELEVATOR_RIDE
            assert (vehicle.get_actor_location() - shared_stop_position).length() < 3, 'AGV moved while human used intermediate landing'
            goal = unreal.Vector(-360, 82, 500)
            direction = goal - pawn.get_actor_location()
            direction.z = 0
            if direction.length() > 12:
                pawn.add_movement_input(direction / direction.length(), 1, True)
                return
            assert not lift.is_human_inside(pawn)
            report['tests'].append('human exits L2 while AGV remains braked for L3')
            enter('shared_continue', now)
        elif phase == 'shared_continue':
            ai = vehicle.get_editor_property('ai_state')
            assert ai != unreal.WarehouseAIState.FAULT, (vehicle.get_editor_property('status'), lift.get_editor_property('status'))
            if lift.can_exit(vehicle):
                assert lift.get_editor_property('current_floor') == 2, 'AGV exit permission at wrong floor'
                assert not lift.is_destination_button_lit(2)
                report['tests'].append('AGV exit permission only at its original L3 destination')
                enter('shared_finish', now)
        elif phase == 'shared_finish':
            assert vehicle.get_editor_property('ai_state') != unreal.WarehouseAIState.FAULT, vehicle.get_editor_property('status')
            if lift.get_editor_property('completed_transfers') < 1:
                return
            assert lift.get_editor_property('reserved_vehicle') is None
            assert abs(vehicle.get_actor_location().z - 800) < 2
            assert abs(pawn.get_actor_location().z - 496) < 10
            report['tests'].append('AGV physically exits L3 while human remains on L2')
            assert len(report['samples']) > 10
            enter('finish_wait', now)
        elif phase == 'finish_wait':
            # Keep PIE and its widgets alive until the requested images exist.
            if frame_count < 2 or not screenshots_ready():
                assert now - phase_at < 5, 'UI screenshots did not finish before PIE shutdown'
                return
            report['screenshots_verified_before_pie_end'] = True
            checked = report.get('human_guidance_states', [])
            assert all(str(s) in checked for s in (unreal.WarehouseElevatorState.CLOSING_LOADED, unreal.WarehouseElevatorState.TRAVELLING, unreal.WarehouseElevatorState.CLOSING_EMPTY)), ('Missing live human guidance coverage', checked)
            report['tests'].append('live closing/travelling interaction guidance; fault and request refusal reasons preserved')
            finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        in_tick = False


handle = unreal.register_slate_post_tick_callback(tick)
