"""Unsaved PIE: acknowledge current HUD alerts without clearing vehicle errors.

Use -AlertDismissClick for the final real mouse X-button check.
Without that flag the final dismissal uses the production API (not OS input).
"""
import json
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
root = Path(unreal.Paths.project_dir()).resolve()
out = root / 'Saved/Testing/AlertDismiss_20261105'
out.mkdir(parents=True, exist_ok=True)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property(
    'default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(40, 40, .2))
floor.static_mesh_component.set_collision_profile_name('BlockAll')
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-600, 0, 125))
for index in range(2):
    vehicle = actors.spawn_actor_from_class(unreal.WarehouseForklift, unreal.Vector(index * 500, 500, .1))
    vehicle.set_actor_label('AlertProbe' + str(index))
    vehicle.set_editor_property('auto_start', False)
    vehicle.set_editor_property('pending_jobs', [])
    vehicle.set_editor_property('idle_parking_enabled', False)

phase = 'start'
at = started = time.monotonic()
pc = None
fleet = []
count = 0
valid_pallet = actors.spawn_actor_from_class(unreal.WarehousePallet, unreal.Vector(800, -800, .1))
checks = []
click = '-alertdismissclick' in unreal.SystemLibrary.get_command_line().lower()
report = dict(passed=False, os_click_required=click, checks=checks, saved_map_written=False)


def enter(value):
    global phase, at
    phase, at = value, time.monotonic()
    (out / 'Phase.txt').write_text(phase, encoding='utf-8')


def finish(ok, detail):
    global phase
    phase = 'finished'
    report.update(passed=ok, detail=detail, wall_seconds=time.monotonic() - started)
    (out / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('ALERT_DISMISS_RESULT', ok, detail)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    global pc, fleet, count
    try:
        if phase == 'finished':
            return
        assert time.monotonic() - started < 300, phase
        age = time.monotonic() - at
        if phase == 'start':
            level.editor_request_begin_play()
            enter('settle')
            return
        world = editor.get_game_world()
        if not world:
            return
        if phase == 'settle' and age > 3:
            pc = unreal.GameplayStatics.get_player_controller(world, 0)
            fleet = sorted(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WarehouseForklift), key=lambda v: v.get_actor_label())
            # These are explicit UI stimuli, not a vehicle/transport regression.
            for vehicle in fleet:
                vehicle.set_actor_tick_enabled(False)
            assert not fleet[0].submit_job(unreal.WarehouseWorkOrder(job_id='AlertInvalidA'))
            enter('first')
        elif phase == 'first' and age > .6:
            assert pc.is_warehouse_alert_visible()
            assert pc.resource_alert_text
            pc.toggle_warehouse_alert_cursor()
            assert pc.get_editor_property('show_mouse_cursor') and pc.is_move_input_ignored()
            pc.dismiss_warehouse_alert()
            assert not pc.is_warehouse_alert_visible()
            assert not pc.get_editor_property('show_mouse_cursor') and not pc.is_move_input_ignored()
            count = pc.emergency_alarm_count
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
            pawn.set_actor_location(pawn.get_actor_location() + unreal.Vector(400, 0, 0), False, True)
            checks.append('Dismissal is immediate; temporary cursor and movement lock restore')
            enter('acknowledged')
        elif phase == 'acknowledged' and age > 7:
            assert not pc.is_warehouse_alert_visible()
            assert pc.emergency_alarm_count == count and not pc.is_emergency_alarm_playing()
            assert fleet[0].get_editor_property('last_error_message')
            assert pc.resource_alert_text
            checks.append('Current error stays dismissed after player movement and seven seconds; original vehicle and dashboard diagnostics remain intact')
            assert not fleet[1].submit_job(unreal.WarehouseWorkOrder(job_id='AlertInvalidB'))
            enter('new_source')
        elif phase == 'new_source' and age > .6:
            assert pc.is_warehouse_alert_visible()
            pc.dismiss_warehouse_alert()
            pallet = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WarehousePallet))
            assert fleet[0].submit_job(unreal.WarehouseWorkOrder(job_id='AlertValid', pallet=pallet, destination=unreal.Transform()))
            assert not fleet[0].get_editor_property('last_error_message')
            enter('clear_source')
        elif phase == 'clear_source' and age > .6:
            assert not pc.is_warehouse_alert_visible()
            assert not fleet[0].submit_job(unreal.WarehouseWorkOrder(job_id='AlertInvalidAAgain'))
            enter('recurrence')
        elif phase == 'recurrence' and age > .6:
            assert pc.is_warehouse_alert_visible()
            checks.append('A new vehicle error and an error recurring after clearance both reopen the banner')
            pc.toggle_warehouse_alert_cursor()
            enter('before_click')
        elif phase == 'before_click' and age > 1:
            enter('await_click')
            if not click:
                pc.dismiss_warehouse_alert()
        elif phase == 'await_click':
            if not pc.is_warehouse_alert_visible():
                assert not pc.get_editor_property('show_mouse_cursor')
                enter('closed')
            else:
                assert age < 180, 'Waiting for actual X-button mouse click'
        elif phase == 'closed' and age > 1:
            assert not pc.is_warehouse_alert_visible()
            assert fleet[0].get_editor_property('last_error_message')
            checks.append('X-button click dismissed the banner' if click else 'Production dismiss API closed the final banner; OS click not tested')
            finish(True, 'Alert acknowledgement, recurrence and input restoration passed')
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
