"""Render the native options/sensor screen; no OS input or transport claim.

Uses unsaved PIE of the saved warehouse. Restores local menu preferences after
closing PIE. Inspect the three PNGs for the top button and actual sensor panels.
"""
import json
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
root = Path(unreal.Paths.project_dir()).resolve()
out = root / 'Saved/Testing/SensorViewMerge_20261103'
out.mkdir(parents=True, exist_ok=True)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
preferences = {p: p.read_bytes() if p.exists() else None for p in (
    root / 'Saved/Config/WindowsEditor/Game.ini', root / 'Saved/Config/Windows/Game.ini')}
fields = ['bShowForklift1AIView', 'bShowForklift2AIView', 'bAIViewFirstPerson',
          'bAIViewMapLines', 'bAIViewForkSide', 'bAIViewAccumulate']
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
started = at = time.monotonic()
phase = 'start'
pc = None
original = {}
report = dict(passed=False, screenshots=[], os_input_tested=False)
original_screenshot_delegate = unreal.SystemLibrary.get_console_variable_int_value('r.ScreenshotDelegate')


def capture(name):
    unreal.load_module('AutomationUtils')
    key = name + '_' + str(time.time_ns())
    incoming = root / 'Saved/Automation/Incoming/SensorViewMerge' / key
    unreal.AutomationUtilsBlueprintLibrary.take_gameplay_automation_screenshot(key, map_name_override='SensorViewMerge')
    options = unreal.AutomationScreenshotOptions()
    options.set_editor_property('disable_noisy_rendering_features', False)
    options.set_editor_property('disable_tonemapping', False)
    unreal.AutomationLibrary.take_automation_screenshot_of_ui(editor.get_game_world(), unreal.LatentActionInfo(), key, options)
    images = list(incoming.rglob('*.png'))
    assert len(images) == 1, ('Missing Slate screenshot', name)
    target = out / (name + '.png')
    target.write_bytes(images[0].read_bytes())
    report['screenshots'].append(str(target))


def end(ok, detail):
    global phase, at
    report.update(passed=ok, detail=detail)
    unreal.SystemLibrary.execute_console_command(editor.get_game_world() or editor.get_editor_world(),
        'r.ScreenshotDelegate ' + str(original_screenshot_delegate), pc)
    if pc and unreal.SystemLibrary.is_valid(pc):
        for field, value in original.items():
            pc.set_editor_property(field, value)
        pc.apply_ai_view_settings()
        if not pc.is_warehouse_menu_open():
            pc.toggle_warehouse_menu()
        pc.toggle_warehouse_menu()  # Restore the config cache before restoring the original file bytes.
    level.editor_request_end_play()
    phase, at = 'end', time.monotonic()


def _tick(dt):
    global phase, at, pc, original
    try:
        age = time.monotonic() - at
        if phase == 'end':
            if editor.get_game_world() or age < 1:
                return
            for p, data in preferences.items():
                if data is None:
                    p.unlink(missing_ok=True)
                else:
                    p.write_bytes(data)
            report['preferences_restored'] = all((p.read_bytes() if p.exists() else None) == data for p, data in preferences.items())
            (out / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.SystemLibrary.quit_editor()
            return
        assert time.monotonic() - started < 240, phase
        if phase == 'start':
            level.editor_request_begin_play()
            phase, at = 'settle', time.monotonic()
            return
        game = editor.get_game_world()
        if not game or age < 3:
            return
        if phase == 'settle':
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            original = {field: pc.get_editor_property(field) for field in fields}
            for vehicle in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseForklift):
                if not vehicle.get_editor_property('powered'):
                    vehicle.toggle_power()
            pc.toggle_warehouse_menu()
            assert pc.is_warehouse_menu_open()
            phase, at = 'options', time.monotonic()
        elif phase == 'options':
            capture('OptionsTopButton')
            pc.open_ai_view_settings()
            assert pc.is_warehouse_menu_open()
            phase, at = 'settings', time.monotonic()
        elif phase == 'settings':
            for field in fields[:2]:
                pc.set_editor_property(field, True)
            pc.apply_ai_view_settings()
            capture('SensorSettings')
            pc.toggle_warehouse_menu()
            assert not pc.is_warehouse_menu_open()
            phase, at = 'panels', time.monotonic()
        elif phase == 'panels':
            capture('TwoSensorPanels')
            # Debug canvas can sit outside the viewport-widget screenshot.
            # Also capture the containing native window before judging visibility.
            target = (out / 'SensorPanelsWindow.png').as_posix()
            unreal.SystemLibrary.execute_console_command(game, 'r.ScreenshotDelegate 0', pc)
            unreal.SystemLibrary.execute_console_command(game, 'Shot SHOWUI filename="' + target + '" -nosuffix', pc)
            phase, at = 'window_capture', time.monotonic()
        elif phase == 'window_capture':
            target = out / 'SensorPanelsWindow.png'
            if not target.is_file() and age < 30:
                return
            assert target.is_file(), 'Native window screenshot did not arrive'
            report['screenshots'].append(str(target))
            end(True, 'Native options, sensor settings, per-vehicle panel application and close paths completed; PNG review required')
    except Exception:
        end(False, traceback.format_exc())


busy = False


def tick(dt):
    global busy
    if busy:
        return
    busy = True
    try:
        _tick(dt)
    finally:
        busy = False


handle = unreal.register_slate_post_tick_callback(tick)
