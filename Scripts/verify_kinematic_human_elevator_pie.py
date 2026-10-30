"""Unsaved production-player regression for CMC elevator rides with held cargo.

Run in an editor with PIE stopped. No passenger pose or velocity is written after
PIE starts: normal CharacterMovement follows the physical platform. The heavy
crate uses the actual pickup/drop API and preserves its 500 kg physical mass.
Add -ElevatorPromptCapture in a rendered editor to capture the actual button HUD.
"""
import json
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not editor.get_game_world(), 'Stop PIE before starting the fixture'
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
settings.set_editor_property('default_game_mode', unreal.load_class(
    None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
cube = unreal.load_asset('/Engine/BasicShapes/Cube')


def block(label, position, size):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position))
    actor.set_actor_label(label)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_collision_profile_name('BlockAll')
    actor.set_actor_scale3d(unreal.Vector(*(n / 100 for n in size)))
    actor.tags = [unreal.Name('WarehouseMapStructure')]
    return actor


for floor in range(2):
    lo, hi = (-181, 181) if floor == 0 else (-181, 200)
    z = floor * 400 - 10
    block('PassengerFloorWest' + str(floor), ((-1000 + lo) / 2, 0, z), (1000 + lo, 1600, 20))
    block('PassengerFloorEast' + str(floor), ((hi + 1000) / 2, 0, z), (1000 - hi, 1600, 20))
    for side in (-1, 1):
        block('PassengerFloorSide' + str(floor) + str(side), ((lo + hi) / 2, side * 470.5, z), (hi - lo, 659, 20))
block('PassengerPit', (0, 0, -30), (352, 282, 20))
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 110))
lift = actors.spawn_actor_from_class(unreal.WarehouseElevator, unreal.Vector())
lift.set_actor_label('KinematicPassengerLift')
assert lift.set_floor_count(2)
crate = actors.spawn_actor_from_class(unreal.WarehouseCargo, unreal.Vector(70, 0, 20.5))
crate.set_actor_label('KinematicPassengerCargo')
crate.set_cargo_mesh(cube)
crate.set_actor_scale3d(unreal.Vector(.4, .4, .4))
crate.set_gross_mass_kg(500)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)

out = Path(unreal.Paths.project_dir()).resolve() / 'Saved/Testing/KinematicHumanElevator'
out.mkdir(parents=True, exist_ok=True)
report = dict(date=datetime.now(timezone.utc).isoformat(), status='starting', tests=[], samples=[],
              runtime_passenger_teleports=False, runtime_passenger_attachments=False)
started = time.monotonic()
phase = 'start'
phase_at = 0
pawn = pc = capsule = movement = deck = cargo_body = None
finished = in_tick = False
last_state = None
held_seen = False
capture_prompt = '-elevatorpromptcapture' in unreal.SystemLibrary.get_command_line().lower()
world_frames = capture_frame = 0
last_world_time = -1


def vector(v):
    return [v.x, v.y, v.z]


def enter(next_phase, now):
    global phase, phase_at
    phase, phase_at = next_phase, now


def capture_button_prompt(game):
    # Capture the actual game's Slate viewport, including the E prompt.
    # HighResShot omits Slate; Shot showui can capture the surrounding editor.
    unreal.load_module('AutomationUtils')
    name = 'CompactFloorButton_' + str(time.time_ns())
    incoming = Path(unreal.Paths.project_saved_dir()) / 'Automation/Incoming/ElevatorPrompt' / name
    options = unreal.AutomationScreenshotOptions()
    options.set_editor_property('disable_noisy_rendering_features', False)
    options.set_editor_property('disable_tonemapping', False)
    unreal.AutomationUtilsBlueprintLibrary.take_gameplay_automation_screenshot(name, map_name_override='ElevatorPrompt')
    unreal.AutomationLibrary.take_automation_screenshot_of_ui(game, unreal.LatentActionInfo(), name, options)
    images = list(incoming.rglob('*.png'))
    assert len(images) == 1, 'No unique actual game-viewport UI capture'
    data = images[0].read_bytes()
    assert len(data) > 1000 and data[:8] == b'\x89PNG\r\n\x1a\n' and data[-8:-4] == b'IEND', 'Incomplete UI PNG'
    path = out / (name + '.png')
    path.write_bytes(data)
    report['prompt_capture'] = dict(path=str(path), width=int.from_bytes(data[16:20], 'big'),
        height=int.from_bytes(data[20:24], 'big'), real_world_frames_waited=world_frames - capture_frame,
        api='AutomationLibrary.take_automation_screenshot_of_ui', visual_review_required=True)


def finish(error=None):
    global finished
    if finished:
        return
    finished = True
    report.update(status='failed' if error else 'passed', wall_seconds=time.monotonic() - started)
    if error:
        report['error'] = error
    try:
        report['final'] = dict(phase=phase, lift_status=lift.get_editor_property('status'),
            human_status=lift.get_editor_property('human_status'), state=str(lift.get_editor_property('state')),
            floor=lift.get_editor_property('current_floor'), human_payload_kg=lift.get_editor_property('human_payload_mass_kg'),
            person=vector(pawn.get_actor_location()), platform=vector(deck.get_world_location()),
            movement_mass_kg=movement.get_editor_property('mass'), cargo=vector(crate.get_actor_location()))
    except Exception as diagnostic:
        report['diagnostic_error'] = str(diagnostic)
    (out / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('KINEMATIC_HUMAN_ELEVATOR_' + ('FAILED' if error else 'PASSED'), error or report['tests'])
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    global pawn, pc, lift, crate, capsule, movement, deck, cargo_body, in_tick, last_state, held_seen
    global world_frames, last_world_time, capture_frame
    if finished or in_tick:
        return
    in_tick = True
    try:
        assert time.monotonic() - started < 180, ('Elevator timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            enter('settle', 0)
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if now > last_world_time:
            world_frames += 1
            last_world_time = now
        if pawn is None:
            pawn = unreal.GameplayStatics.get_player_character(game, 0)
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            if not pawn:
                return
            named = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            lift, crate = (named[n] for n in ('KinematicPassengerLift', 'KinematicPassengerCargo'))
            capsule = pawn.get_component_by_class(unreal.CapsuleComponent)
            movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
            cargo_body = crate.get_component_by_class(unreal.StaticMeshComponent)
            deck = next(c for c in lift.get_components_by_class(unreal.StaticMeshComponent) if c.get_name() == 'LiftPlatform')
            assert movement.get_class().get_name() == 'WarehousePlayerMovement'
        assert not capsule.is_simulating_physics() and not pawn.is_physical_ragdoll(), 'Player reverted to physical capsule/ragdoll'
        assert not pawn.get_attach_parent_actor(), 'Passenger was attached to the elevator'
        capsule_body = capsule.get_editor_property('body_instance')
        assert capsule_body.get_editor_property('override_mass') and abs(capsule_body.get_editor_property('mass_in_kg_override') - 80) < .1, 'Passenger capsule mass must remain 80 kg'
        state = lift.get_editor_property('state')
        if str(state) != last_state:
            report.setdefault('events', []).append(dict(t=now, phase=phase, state=str(state), status=lift.get_editor_property('status')))
            last_state = str(state)
        if phase in ('ride_up', 'ride_down') and state == unreal.WarehouseElevatorState.TRAVELLING:
            feet = capsule.get_world_location().z - capsule.get_scaled_capsule_half_height()
            top = deck.get_world_location().z + 5
            assert abs(feet - top) < 5, ('Character lost moving-base support', feet, top)
            assert lift.are_landing_doors_closed(), 'Platform moved with an open landing'
            assert pawn.get_held_cargo() == crate and not cargo_body.is_simulating_physics()
            # The authored cube has a centred pivot and 20 cm half-height.
            # Production carry places its centre at player Z + 10 + half-height.
            held_height_gap = abs(crate.get_actor_location().z - pawn.get_actor_location().z - 30)
            assert held_height_gap < 1, ('Held cargo lagged behind post-physics moving-base correction', held_height_gap)
            assert abs(lift.get_editor_property('human_payload_mass_kg') - 580) < .5, 'Held cargo was omitted or counted twice'
            assert abs(movement.get_editor_property('mass') - 580) < .5, 'Held weight is not transmitted by CharacterMovement'
            if not report['samples'] or now - report['samples'][-1]['t'] > .2:
                report['samples'].append(dict(t=now, phase=phase, feet_cm=feet, deck_cm=top,
                    payload_kg=lift.get_editor_property('human_payload_mass_kg'), held_height_gap_cm=held_height_gap))
        if phase == 'settle':
            if now < 2:
                return
            assert lift.is_human_inside(pawn), 'Spawned production player did not settle in the cabin'
            assert abs(lift.get_editor_property('human_payload_mass_kg') - 580) < .5
            assert lift.request_human_destination(pawn, 0), lift.get_editor_property('human_status')
            report['tests'].append('kinematic 80 kg passenger and loose 500 kg cargo recognized')
            enter('open', now)
        elif phase == 'open':
            if state != unreal.WarehouseElevatorState.AWAIT_EXIT:
                return
            pc.set_control_rotation(unreal.Rotator(yaw=180))
            enter('prompt_away', now)
        elif phase == 'prompt_away':
            if now - phase_at < .2:
                return
            assert lift.get_human_interaction_text(pawn) == '', 'Cabin guidance remained visible away from a button'
            camera = pawn.get_component_by_class(unreal.CameraComponent)
            button = next(c for c in lift.get_components_by_class(unreal.StaticMeshComponent)
                          if c.get_name() == 'HumanFloorButton_0')
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_world_location(), button.get_world_location()))
            enter('prompt_button', now)
        elif phase == 'prompt_button':
            if now - phase_at < .2:
                return
            assert pawn.get_interaction_target() == lift and lift.human_button_at_sight(pawn) == 0
            assert lift.get_human_interaction_text(pawn) == 'E : 1층', 'Button prompt contains extra cabin guidance'
            pawn.toggle_carry()
            assert not pc.is_move_input_ignored() and not unreal.GameplayStatics.is_game_paused(game)
            report['tests'].append('only aimed floor button is prompted; actual E interaction keeps movement enabled')
            if capture_prompt:
                capture_frame = world_frames
                enter('prompt_capture', now)
                return
            assert pawn.try_pickup_cargo(crate), 'Actual pickup rejected the heavy cargo'
            enter('hold', now)
        elif phase == 'prompt_capture':
            if world_frames - capture_frame < 2:
                return
            assert lift.human_button_at_sight(pawn) == 0 and lift.get_human_interaction_text(pawn) == 'E : 1층'
            capture_button_prompt(game)
            assert pawn.try_pickup_cargo(crate), 'Actual pickup rejected the heavy cargo'
            enter('hold', now)
        elif phase == 'hold':
            assert pawn.get_held_cargo() == crate, 'Heavy cargo pickup did not remain held'
            if now - phase_at < 1:
                return
            assert crate.actor_has_tag(unreal.Name('PlayerHeldCargo')) and not cargo_body.is_simulating_physics()
            held_body = cargo_body.get_editor_property('body_instance')
            assert held_body.get_editor_property('override_mass') and abs(held_body.get_editor_property('mass_in_kg_override') - 500) < .1
            assert abs(movement.get_editor_property('mass') - 580) < .5
            assert lift.is_human_inside(pawn)
            lift.set_editor_property('rated_mass_kg', 550)
            assert not lift.request_human_destination(pawn, 1), 'Held cargo escaped rated-load validation'
            assert 'OVERLOAD' in lift.get_editor_property('human_status')
            assert abs(lift.get_editor_property('human_payload_mass_kg') - 580) < .5
            assert not lift.is_destination_button_lit(1), 'Rejected destination lit its button'
            lift.set_editor_property('rated_mass_kg', 2000)
            assert lift.request_human_destination(pawn, 1), lift.get_editor_property('human_status')
            assert lift.is_destination_button_lit(1)
            held_seen = True
            report['tests'].append('actual held cargo retains real mass and overload interlock')
            enter('ride_up', now)
        elif phase == 'ride_up':
            if state != unreal.WarehouseElevatorState.AWAIT_EXIT or lift.get_editor_property('current_floor') != 1:
                return
            assert lift.is_human_inside(pawn) and not lift.is_destination_button_lit(1)
            assert abs(deck.get_world_location().z + 5 - 400) < 1
            assert lift.request_human_destination(pawn, 0), lift.get_editor_property('human_status')
            report['tests'].append('upward CMC moving-base ride with 500 kg held cargo')
            enter('ride_down', now)
        elif phase == 'ride_down':
            if state != unreal.WarehouseElevatorState.AWAIT_EXIT or lift.get_editor_property('current_floor') != 0:
                return
            assert lift.is_human_inside(pawn) and not lift.is_destination_button_lit(0)
            assert abs(deck.get_world_location().z + 5) < 1
            assert held_seen and len(report['samples']) >= 10
            assert lift.get_editor_property('completed_human_transfers') == 2
            pawn.drop_cargo()
            report['tests'].append('downward CMC moving-base ride; both destinations completed')
            enter('drop', now)
        elif phase == 'drop':
            if now - phase_at < .5:
                return
            assert not pawn.get_held_cargo() and cargo_body.is_simulating_physics()
            assert not crate.actor_has_tag(unreal.Name('PlayerHeldCargo'))
            assert abs(cargo_body.get_mass() - 500) < .1 and abs(movement.get_editor_property('mass') - 80) < .1
            assert abs(lift.get_editor_property('human_payload_mass_kg') - 580) < .5
            report['tests'].append('drop restores physical cargo and 80 kg movement mass without changing total cabin load')
            finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        in_tick = False


handle = unreal.register_slate_post_tick_callback(tick)
