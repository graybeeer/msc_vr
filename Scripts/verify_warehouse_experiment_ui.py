"""Exercise the real experiment Slate controls in a prepared, idle PIE world.

Run after preparing a fresh initial-condition fixture with at least one pallet
and destination. The world must have no experiment orders or running batch.
This test leaves PIE open, does not save the map, and removes only its uniquely
named snapshots, their archived versions, and its workload. If all six slots
already contain saved layouts, it skips without changing those layouts. It does not claim native
transport, a level reload, or operating-system mouse/keyboard verification.
Use -ExperimentUICapture to capture both actual game Slate pages for review.
"""
import hashlib
import json
from pathlib import Path
import struct
import time
import traceback

import unreal


ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/Testing/WarehouseExperiments'
STORE = ROOT / 'Saved/WarehouseExperiments'
OUT.mkdir(parents=True, exist_ok=True)
CAPTURE = '-experimentuicapture' in unreal.SystemLibrary.get_command_line().lower()
NAME = 'UI_CHECK_' + str(time.time_ns())
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
started = time.monotonic()
phase = 'start'
phase_at = started
handle = None
game = pc = None
before_files = {}
before_saved_files = {}
new_layout = new_workload = ''
checks = []
report = dict(passed=False, checks=checks, states=[], screenshots=[],
              osInputTested=False, nativeTransportTested=False,
              levelReloadTested=False, actualSlateCallbacks=True)


def map_files():
    candidates = [ROOT / 'Content/FirstPerson/Lvl_FirstPerson.umap']
    for directory in ('__ExternalActors__', '__ExternalObjects__'):
        candidates.extend((ROOT / 'Content' / directory / 'FirstPerson/Lvl_FirstPerson').rglob('*'))
    return {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in candidates if path.is_file()}


def saved_files():
    return {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest()
            for folder in ('Layouts', 'Workloads')
            for path in (STORE / folder).rglob('*.json') if path.is_file()}


def layout_path(layout_id):
    return STORE / 'Layouts' / (layout_id + '.json')


def read_layout(layout_id):
    return json.loads(layout_path(layout_id).read_text(encoding='utf-8-sig'))


def save_layout(label):
    before_ids = set(ui()['layout_ids'])
    ui('text layout ' + quote(label))
    state = ui('click saveLayout')
    added = set(state['layout_ids']) - before_ids
    assert state['accepted'] and len(added) == 1, state
    new_id = added.pop()
    assert read_layout(new_id).get('name') == label
    return new_id


def enter(next_phase):
    global phase, phase_at
    phase, phase_at = next_phase, time.monotonic()


def ui(command='snapshot'):
    state_path = OUT / 'UIState.json'
    state_path.unlink(missing_ok=True)
    unreal.SystemLibrary.execute_console_command(game, 'Warehouse.Experiment.UI ' + command, pc)
    assert state_path.is_file(), ('No actual Slate response', command)
    state = json.loads(state_path.read_text(encoding='utf-8-sig'))
    report['states'].append(dict(command=command, state=state))
    return state


def quote(value):
    return json.dumps(value, ensure_ascii=False)


def capture(label):
    if not CAPTURE:
        return
    unreal.load_module('AutomationUtils')
    capture_name = label + '_' + str(time.time_ns())
    incoming = ROOT / 'Saved/Automation/Incoming/WarehouseExperiments' / capture_name
    unreal.AutomationUtilsBlueprintLibrary.take_gameplay_automation_screenshot(
        capture_name, map_name_override='WarehouseExperiments')
    options = unreal.AutomationScreenshotOptions()
    options.set_editor_property('disable_noisy_rendering_features', False)
    options.set_editor_property('disable_tonemapping', False)
    unreal.AutomationLibrary.take_automation_screenshot_of_ui(
        game, unreal.LatentActionInfo(), capture_name, options)
    images = list(incoming.rglob('*.png'))
    assert len(images) == 1, ('No unique actual game Slate image', label)
    data = images[0].read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n' and len(data) > 1000
    width, height = struct.unpack('>II', data[16:24])
    destination = OUT / (label + '.png')
    destination.write_bytes(data)
    report['screenshots'].append(dict(path=str(destination), width=width, height=height,
                                      actualSlate=True, visualReviewRequired=True))


def finish(success, detail, skipped=False):
    global handle
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        handle = None
    try:
        if pc:
            pc.close_warehouse_experiments()
        # Archiving creates new immutable files. Only this run's new, uniquely
        # named files can be removed; every pre-existing record is hash-checked.
        for folder in ('Layouts', 'Workloads'):
            for path in (STORE / folder).rglob('*.json'):
                path = path.resolve()
                assert path.is_relative_to(STORE.resolve())
                if str(path) in before_saved_files:
                    continue
                contents = json.loads(path.read_text(encoding='utf-8-sig'))
                if str(contents.get('name', '')).startswith(NAME):
                    path.unlink()
        after_saved = saved_files()
        assert all(after_saved.get(path) == digest for path, digest in before_saved_files.items()), 'Pre-existing layouts or workloads changed'
        report['preExistingSavedDataUnchanged'] = True
        report['savedMapUnchanged'] = before_files == map_files()
        assert report['savedMapUnchanged'], 'Source map packages changed during UI test'
    except Exception:
        success = False
        detail += '\n' + traceback.format_exc()
    report.update(passed=success, skipped=skipped, detail=detail, wall_seconds=time.monotonic() - started)
    (OUT / 'UIReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('WAREHOUSE_EXPERIMENT_UI_' + ('SKIPPED' if skipped else ('PASSED' if success else 'FAILED')), detail)


def step():
    global game, pc, before_files, before_saved_files, new_layout, new_workload
    assert time.monotonic() - started < 180, ('UI test timeout', phase)
    age = time.monotonic() - phase_at
    if phase == 'start':
        game = editor.get_game_world()
        assert game, 'Start a clean initial-condition PIE fixture before this script'
        pc = unreal.GameplayStatics.get_player_controller(game, 0)
        assert pc, 'Fixture requires a local player controller'
        before_files = map_files()
        before_saved_files = saved_files()
        pc.open_warehouse_experiments()
        enter('open')
    elif phase == 'open' and age > 1:
        initial = ui()
        assert not initial['busy'] and initial['order_count'] == 0, 'Use an idle, empty experiment draft fixture'
        if initial['layout_count'] >= 6:
            finish(False, 'All six layout slots are already in use; no existing layout was changed', skipped=True)
            return
        assert initial['source_ids'] and initial['destination_ids'], 'Fixture lacks pallet/destination choices'
        assert not initial['actions_enabled']['saveLayout']
        assert not initial['actions_enabled']['start']
        assert not ui('click saveLayout')['accepted'], 'Empty-name save button must reject input'
        # Fill only unused slots through real UI callbacks. Existing layouts
        # are never selected for management, replaced, or deleted.
        own_ids = [save_layout(NAME + '_slot' + str(index))
                   for index in range(6 - initial['layout_count'])]
        state = ui()
        assert state['layout_count'] == 6 and not state['actions_enabled']['saveLayout']
        assert not ui('click saveLayout')['accepted']
        assert ui()['layout_count'] == 6, 'Full storage must not create a seventh layout'
        instance = unreal.GameplayStatics.get_game_instance(game)
        store = next(item for item in unreal.ObjectIterator(unreal.WarehouseExperimentSubsystem)
                     if item.get_outer() == instance)
        assert not store.save_layout(NAME + '_overflow'), 'The native API must also reject a seventh layout'
        assert ui()['layout_count'] == 6
        checks.append('Six saved layouts disable new save; a seventh real UI click and direct native save are rejected')

        original_id = own_ids[0]
        assert ui('click ' + quote('pickLayout:' + original_id))['accepted']
        ui('text layout ' + quote(NAME + '_replaced'))
        original_data = layout_path(original_id).read_bytes()
        original_ids = set(ui()['layout_ids'])
        assert ui('click replaceLayout')['accepted']
        ui('text layout ' + quote(NAME + '_changed_confirmation'))
        assert ui('click replaceLayout')['accepted']
        assert set(ui()['layout_ids']) == original_ids
        assert layout_path(original_id).read_bytes() == original_data, 'Changing the name must cancel a pending overwrite confirmation'
        assert ui('click removeLayout')['accepted']
        assert ui('click replaceLayout')['accepted']
        assert set(ui()['layout_ids']) == original_ids
        assert layout_path(original_id).read_bytes() == original_data, 'Choosing another management action must cancel a pending overwrite confirmation'
        checks.append('Editing the name or choosing a different management action cancels pending overwrite confirmation')
        # Return to the intended name; this must require two fresh clicks.
        ui('text layout ' + quote(NAME + '_replaced'))
        assert ui('click replaceLayout')['accepted']
        assert set(ui()['layout_ids']) == original_ids
        assert layout_path(original_id).read_bytes() == original_data, 'First overwrite click must only request confirmation'
        state = ui('click replaceLayout')
        replaced_ids = set(state['layout_ids']) - original_ids
        assert state['accepted'] and state['layout_count'] == 6 and len(replaced_ids) == 1, state
        replaced_id = replaced_ids.pop()
        assert original_id not in state['layout_ids'] and not layout_path(original_id).exists()
        assert read_layout(replaced_id).get('name') == NAME + '_replaced'
        archived_original = [path for path in (STORE / 'Layouts/Archive').rglob('*.json')
                             if path.read_bytes() == original_data]
        assert len(archived_original) == 1, 'Overwrite must preserve the old input snapshot in the archive'

        # The target follows the new immutable snapshot ID after replacement.
        assert ui('click removeLayout')['accepted']
        assert replaced_id in ui()['layout_ids'], 'First delete click must only request confirmation'
        state = ui('click removeLayout')
        assert state['accepted'] and state['layout_count'] == 5 and replaced_id not in state['layout_ids'], state
        assert not layout_path(replaced_id).exists() and state['actions_enabled']['saveLayout']
        checks.append('Two-click overwrite and delete act on the selected test layout; replacement follows its new ID and preserves the previous snapshot')
        new_layout = save_layout(NAME + '_reuse')
        assert ui()['layout_count'] == 6
        checks.append('A deleted slot is reusable through the normal save button')
        assert ui('click removeLayout')['accepted']
        ui('text layout ' + quote(NAME + '_changed_delete_confirmation'))
        assert ui('click removeLayout')['accepted']
        assert new_layout in ui()['layout_ids'], 'Changing the name must cancel a pending delete confirmation'
        # Only test cross-target confirmation with another layout created by
        # this run. Never select a pre-existing user's layout for deletion.
        if len(own_ids) > 1:
            other_id = own_ids[1]
            before_target_ids = set(ui()['layout_ids'])
            other_data = layout_path(other_id).read_bytes()
            assert ui('click ' + quote('pickLayout:' + other_id))['accepted']
            assert ui('click removeLayout')['accepted']
            assert set(ui()['layout_ids']) == before_target_ids
            assert layout_path(other_id).read_bytes() == other_data, 'Changing the target must cancel pending delete confirmation'
            checks.append('Changing the selected target cancels pending delete confirmation; neither test layout is deleted by the first click')
        else:
            report['targetChangeConfirmationSkipped'] = 'Only one slot was free; no pre-existing layout was selected for a destructive-action test'
        checks.append('Editing the name cancels pending delete confirmation')
        assert ui('click ' + quote('pickLayout:' + new_layout))['accepted']
        ui('toggleLayout ' + quote(new_layout))
        assert new_layout in ui()['selected_layout_ids']
        ui('choose source ' + quote(initial['source_ids'][0]))
        ui('choose destination ' + quote(initial['destination_ids'][0]))
        assert ui('click addOrder')['order_count'] == 1
        assert ui()['actions_enabled']['start']
        before_workloads = set(ui()['workload_ids'])
        ui('text workload ' + quote(NAME))
        state = ui('click saveWorkload')
        new_workload_ids = set(state['workload_ids']) - before_workloads
        assert len(new_workload_ids) == 1, state
        new_workload = next(iter(new_workload_ids))
        assert ui('click clearOrders')['order_count'] == 0
        ui('choose workload ' + quote(new_workload))
        assert ui('click loadWorkload')['order_count'] == 1
        checks.append('Real name, checkbox, pallet/destination combo, add, save, clear and workload-load callbacks preserve a common order')
        assert ui('click configuration')['page'] == 0
        enter('configuration')
    elif phase == 'configuration' and age > 1:
        capture('ExperimentConfiguration')
        assert ui('click results')['page'] == 1
        enter('results')
    elif phase == 'results' and age > 1:
        capture('ExperimentResults')
        state = ui()
        assert not state['actions_enabled']['stop'], 'No active batch must disable stop'
        assert state['actions_enabled']['export'] == (state['result_count'] > 0)
        if state['result_count'] > 0:
            assert ui('click export')['accepted']
            checks.append('Existing results export through the real CSV button')
        ui('click configuration')
        assert ui('click clearOrders')['order_count'] == 0
        ui('toggleLayout ' + quote(new_layout))
        assert new_layout not in ui()['selected_layout_ids']
        assert ui('click back')['accepted']
        assert not pc.is_warehouse_menu_open(), 'Closing must release the experiment modal'
        if not pc.is_observer_view():
            assert not pc.is_move_input_ignored() and not pc.is_look_input_ignored()
        checks.append('Real configuration/results/back controls and normal input return pass; no simulated batch was started by this UI-only test')
        finish(True, 'Experiment Slate controls and optional actual viewport capture passed')


_busy = False


def tick(_dt):
    global _busy
    if _busy:
        return
    _busy = True
    try:
        step()
    except Exception:
        finish(False, traceback.format_exc())
    finally:
        _busy = False


handle = unreal.register_slate_post_tick_callback(tick)
