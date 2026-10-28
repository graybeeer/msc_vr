"""Passive saved-map CSV capture: first person then running observer view.

Run via the serial editor host with -HitchProfileLabel=<unique name>.
Uses the saved fleet, physics, orders and 1x clock without saving the map.
Warmup is recorded explicitly; this is a performance sample, not a job test.
"""
import csv
import json
import re
import time
import traceback
from pathlib import Path
import unreal

command = unreal.SystemLibrary.get_command_line()
match = re.search(r'-HitchProfileLabel=([A-Za-z0-9_-]+)', command, re.I)
assert match, 'A unique -HitchProfileLabel is required'
label = match.group(1)
saved = Path(unreal.Paths.project_saved_dir()).resolve()
out = saved / 'Testing' / 'PeriodicHitches'
out.mkdir(parents=True, exist_ok=True)
capture = saved / 'Profiling' / 'CSV' / ('Warehouse_' + label + '.csv')
assert not capture.exists(), 'Use a new label; previous captures are preserved'
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.get_game_world(), 'Stop PIE before profiling'
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
report = dict(label=label, command=command, phases=[], samples=[], sensor_updates=[],
              callback_gaps=[], map_saved=False, physics_modified=False, requested_speed=1)
started = time.monotonic()
game = pc = fleet = None
phase = 'warmup'
phase_start = 0.
last_sim = -1.
last_wall = None
last_sample = -10.
last_sensor_counts = {}
pending = None
capture_size = None
capture_stable = 0.
busy = False


def enter(name, now):
    global phase, phase_start
    phase, phase_start = name, now
    report['phases'].append(dict(name=name, simulation_seconds=now, wall_seconds=time.monotonic()-started))
    print('PERIODIC_PROFILE_PHASE', name, now)


def finish(error=None):
    global pending
    if pending is not None:
        return
    pending = time.monotonic()
    report['error'] = error
    if game:
        unreal.SystemLibrary.execute_console_command(game, 'csvprofile stop', pc)


def close():
    report.update(passed=report['error'] is None, csv=str(capture), wall_seconds=time.monotonic()-started)
    (out / (label + '.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PERIODIC_PROFILE_PASSED' if report['passed'] else 'PERIODIC_PROFILE_FAILED', label, report['error'])
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(delta):
    global game, pc, fleet, last_sim, last_wall, last_sample, capture_size, capture_stable, busy
    if busy:
        return
    busy = True
    try:
        wall = time.monotonic()
        if pending is not None:
            if wall-pending > 30:
                report['error'] = report['error'] or 'CSV writer timeout'
                close()
            elif capture.is_file():
                try:
                    with capture.open('rb') as stream:
                        stream.seek(max(0, capture.stat().st_size-32768))
                        footer = next(csv.reader([stream.read().decode('utf-8', errors='replace').splitlines()[-1]]))
                    size = capture.stat().st_size
                    complete = '[HasHeaderRowAtEnd]' in footer and footer[footer.index('[HasHeaderRowAtEnd]')+1] == '1'
                    if complete and size == capture_size and wall-capture_stable > .3:
                        close()
                    elif complete and size != capture_size:
                        capture_size, capture_stable = size, wall
                except (PermissionError, FileNotFoundError):
                    pass
            return
        assert wall-started < 600, 'Profile wall-clock timeout'
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if now == last_sim:
            return
        if fleet is None:
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            fleet = sorted(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseForklift), key=lambda v: v.get_name())
            assert fleet, 'Saved warehouse has no vehicles'
            assert pc.set_simulation_speed(1)
            report['actors'] = len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor))
            report['cargo'] = len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseCargo))
            report['pallets'] = len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehousePallet))
            report['vehicles'] = [v.get_actor_label() for v in fleet]
            enter('warmup', now)
        if last_wall is not None and wall-last_wall > .08:
            report['callback_gaps'].append(dict(phase=phase, simulation_seconds=now, wall_gap_ms=(wall-last_wall)*1000))
        last_sim, last_wall = now, wall
        if phase == 'warmup' and now-phase_start >= 12:
            unreal.SystemLibrary.execute_console_command(game, 'csv.CompressionMode 0', pc)
            unreal.SystemLibrary.execute_console_command(game, 'csvprofile startfile=Warehouse_' + label, pc)
            unreal.SystemLibrary.execute_console_command(game, 'csvprofile start', pc)
            enter('first_person', now)
        elif phase == 'first_person' and now-phase_start >= 40:
            pc.toggle_observer_view()
            pc.set_layout_editing(False)
            pc.set_observer_floor(0)
            enter('observer_transition', now)
        elif phase == 'observer_transition' and now-phase_start >= 5:
            enter('observer_floor0', now)
        elif phase == 'observer_floor0' and now-phase_start >= 40:
            enter('complete', now)
            finish()
        for vehicle in fleet:
            sensor = vehicle.get_component_by_class(unreal.AgvShadowSensorsComponent)
            if not sensor:
                continue
            count = sensor.get_editor_property('snapshot_count')
            key = vehicle.get_name()
            if last_sensor_counts.get(key) != count:
                last_sensor_counts[key] = count
                report['sensor_updates'].append(dict(phase=phase, simulation_seconds=now, vehicle=vehicle.get_actor_label(), count=count,
                    milliseconds=sensor.get_editor_property('last_sensor_milliseconds'), beams=sensor.get_editor_property('last_beam_count')))
        if now-last_sample >= 5:
            last_sample = now
            sample = dict(phase=phase, simulation_seconds=now, wall_seconds=wall-started,
                fleet=[dict(vehicle=v.get_actor_label(), powered=v.get_editor_property('powered'), state=str(v.get_editor_property('ai_state')),
                            status=v.get_editor_property('status'), speed=v.get_editor_property('current_speed_cm')) for v in fleet])
            report['samples'].append(sample)
            print('PERIODIC_PROFILE_SAMPLE', json.dumps(sample, ensure_ascii=False))
    except Exception:
        finish(traceback.format_exc())
    finally:
        busy = False


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle = unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
