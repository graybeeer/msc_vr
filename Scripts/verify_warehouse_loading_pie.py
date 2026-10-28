"""Native loading presentation, nested tasks, input restoration and map travel.

Uses public production APIs. The screen capture is the actual game viewport's
Slate widget, not an image of the scene behind the loading panel. PIE does not
exercise the standalone MoviePlayer loading thread; report that distinction.
"""
import hashlib
import json
from pathlib import Path
import time
import traceback

import unreal


command = unreal.SystemLibrary.get_command_line().lower()
rendered = '-nullrhi' not in command
keep_open = '-keeptesteditoropen' in command
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
root = Path(unreal.Paths.project_dir()).resolve()
output = root/'Saved/Testing/WarehouseLoading_20261028'
output.mkdir(parents=True, exist_ok=True)
maps = [root/'Content'/path for path in ('FirstPerson/Lvl_FirstPerson.umap',
        'Warehouse/Play/Maps/MainMenu.umap', 'Warehouse/Play/Maps/Stage01.umap')]
map_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in maps if path.exists()}
progress_paths = (root/'Saved/SaveGames/WarehousePlayProgress_v1.sav', root/'Saved/SaveGames/WarehousePlayProgress_Test.sav')
progress_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None for path in progress_paths}
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
report = dict(passed=False, rendered=rendered, checks=[], screenshots=[], osInputTested=False,
              scope='Production loading subsystem in PIE; nested tasks and native MainMenu/Stage1 map travel',
              exclusions=['Standalone MoviePlayer thread', 'Packaged startup', 'OS key/mouse input', 'Performance improvement comparison'])
started = time.monotonic()
phase = 'start'
phase_at = started
busy = finished = False
handle = None
game = pc = loading = None
subsystem_identity = None
outer = inner = 0
before_input = None
map_started = map_completed = 0
before_started = before_completed = 0


def check(name, **values):
    report['checks'].append(dict(name=name, **values))
    print('WAREHOUSE_LOADING_CHECK', name, json.dumps(values, ensure_ascii=False))


def enter(name):
    global phase, phase_at
    phase, phase_at = name, time.monotonic()


def subsystem():
    instance = unreal.GameplayStatics.get_game_instance(game)
    matches = [obj for obj in unreal.ObjectIterator(unreal.WarehouseLoadingSubsystem) if obj.get_outer() == instance]
    assert len(matches) == 1, ('Expected one loading subsystem per active game instance', len(matches))
    return matches[0]


def counters():
    return dict(started=loading.get_editor_property('started_screens'), completed=loading.get_editor_property('completed_screens'),
                active=loading.get_active_task_count(), paint=loading.get_current_paint_count(),
                last_paint=loading.get_editor_property('last_paint_count'),
                presentation=loading.get_editor_property('last_presentation'))


def input_state():
    return dict(move=pc.is_move_input_ignored(), look=pc.is_look_input_ignored(),
                cursor=pc.get_editor_property('show_mouse_cursor'), paused=unreal.GameplayStatics.is_game_paused(game))


def png_ready(path):
    if not path.is_file():
        return None
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if len(data) < 1000 or data[:8] != b'\x89PNG\r\n\x1a\n' or data[-8:-4] != b'IEND':
        return None
    return dict(path=str(path), width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'), bytes=len(data))


def capture_ui():
    unreal.load_module('AutomationUtils')
    name = 'LoadingOverlay_'+str(time.time_ns())
    incoming = root/'Saved/Automation/Incoming/WarehouseLoading'/name
    assert not incoming.exists()
    # Register the existing engine PNG writer, then synchronously capture the
    # actual world SViewport. No scene-only HighResShot is accepted as UI proof.
    unreal.AutomationUtilsBlueprintLibrary.take_gameplay_automation_screenshot(name, map_name_override='WarehouseLoading')
    unreal.AutomationLibrary.take_automation_screenshot_of_ui(game, unreal.LatentActionInfo(), name, unreal.AutomationScreenshotOptions())
    candidates = [path for path in incoming.rglob('*.png') if png_ready(path)]
    assert len(candidates) == 1, ('No unique finished game-viewport loading capture', str(incoming), len(candidates))
    destination = output/(name+'.png')
    destination.write_bytes(candidates[0].read_bytes())
    info = png_ready(destination)
    info.update(source='World game viewport Slate widget', visualReviewRequired=True,
                loadingActive=loading.is_loading(), nativePaintCount=loading.get_current_paint_count())
    report['screenshots'].append(info)


def finish(error=None):
    global finished
    if finished:
        return
    finished = True
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    try:
        if loading and unreal.SystemLibrary.is_valid(loading):
            if inner:
                loading.end_task(inner)
            if outer:
                loading.end_task(outer)
        unchanged = all(Path(path).exists() and hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest for path, digest in map_hashes.items())
        unchanged_progress = all((hashlib.sha256(Path(path).read_bytes()).hexdigest() if Path(path).exists() else None) == digest for path, digest in progress_hashes.items())
        report.update(savedMapsUnchanged=unchanged, campaignProgressUnchanged=unchanged_progress)
        if not unchanged or not unchanged_progress:
            error = (error or '')+' Saved map or campaign progress changed.'
        if editor.get_game_world():
            level.editor_request_end_play()
    except Exception:
        error = (error or '')+'\nCleanup: '+traceback.format_exc()
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    report.update(passed=not error, wall_seconds=round(time.monotonic()-started, 3))
    if error:
        report['error'] = error
    (output/'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('WAREHOUSE_LOADING_PIE_'+('FAILED' if error else 'PASSED'), error or report['checks'])
    if not keep_open:
        unreal.SystemLibrary.quit_editor()


def tick_impl(delta_seconds):
    global game, pc, loading, subsystem_identity, outer, inner, before_input
    global map_started, map_completed, before_started, before_completed
    assert time.monotonic()-started < 360, ('Loading test timeout', phase)
    if phase == 'start':
        assert not editor.get_game_world(), 'Previous PIE must stop before loading test'
        assert level.load_level('/Game/Warehouse/Play/Maps/MainMenu')
        level.editor_request_begin_play()
        enter('menu')
        return
    game = editor.get_game_world()
    if not game:
        return
    pc = unreal.GameplayStatics.get_player_controller(game, 0)
    if not pc:
        return
    loading = subsystem()
    if phase == 'menu':
        if not pc.is_main_menu() or loading.is_loading() or time.monotonic()-phase_at < .5:
            return
        subsystem_identity = loading.get_path_name()
        before_input = input_state()
        before_started, before_completed = counters()['started'], counters()['completed']
        outer = loading.begin_task('장면과 물류 정보를 불러오고 있습니다.')
        assert outer > 0 and loading.is_loading() and loading.get_active_task_count() == 1
        assert pc.is_move_input_ignored() and pc.is_look_input_ignored()
        if rendered:
            assert loading.get_current_paint_count() > 0, 'BeginTask returned before a loading frame painted'
            assert loading.get_editor_property('last_presentation') == 'Viewport', 'PIE must use its viewport loading fallback'
        check('first_scope_paints_loading_screen_before_work', **counters())
        inner = loading.begin_task('건물과 설비 배치를 준비하고 있습니다')
        assert inner > 0 and inner != outer and loading.get_active_task_count() == 2
        assert counters()['started'] == before_started+1, 'Nested scope created a second loading screen'
        enter('nested_capture')
        return
    if phase == 'nested_capture':
        if time.monotonic()-phase_at < .5:
            return
        assert loading.is_loading() and loading.get_active_task_count() == 2
        if rendered:
            capture_ui()
        loading.end_task(inner)
        loading.end_task(inner)  # A duplicate completion must not remove its parent.
        inner = 0
        assert loading.is_loading() and loading.get_active_task_count() == 1
        check('nested_and_duplicate_end_keep_outer_screen_active', **counters())
        loading.end_task(outer)
        outer = 0
        enter('outer_finished')
        return
    if phase == 'outer_finished':
        if loading.is_loading():
            return
        assert loading.get_active_task_count() == 0
        assert counters()['completed'] == before_completed+1
        assert input_state() == before_input, ('Loading changed previous menu input/pause state', before_input, input_state())
        if rendered:
            assert counters()['last_paint'] > 0
        check('last_scope_restores_existing_menu_input_and_pause', input=input_state(), **counters())
        # Exercise pre-existing ignored input: loading must restore the caller's
        # state, not indiscriminately clear every controller input lock.
        pc.set_ignore_move_input(True)
        pc.set_ignore_look_input(True)
        before_input = input_state()
        outer = loading.begin_task('입력 상태 복원 확인')
        loading.end_task(outer)
        outer = 0
        enter('preexisting_input')
        return
    if phase == 'preexisting_input':
        if loading.is_loading():
            return
        assert input_state() == before_input, ('Pre-existing input locks were cleared', before_input, input_state())
        pc.set_ignore_move_input(False)
        pc.set_ignore_look_input(False)
        check('preexisting_move_and_look_locks_preserved')
        loading.end_task(-999)
        assert not loading.is_loading() and loading.get_active_task_count() == 0
        before = counters()
        assert not pc.open_play_mode(99), 'Invalid stage index accepted'
        assert not loading.is_loading() and loading.get_active_task_count() == 0
        assert counters()['started'] == before['started'], 'Rejected navigation started a loading screen'
        check('rejected_navigation_does_not_leave_loading_active')
        map_started, map_completed = counters()['started'], counters()['completed']
        assert pc.open_play_mode(1), 'Stage 1 must be available without changing campaign progress'
        enter('stage')
        return
    if phase == 'stage':
        stages = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehousePlayStage)
        if not stages or stages[0].get_editor_property('stage_index') != 1 or loading.is_loading():
            return
        assert loading.get_path_name() == subsystem_identity, 'Map transition replaced the game-instance loading service'
        assert counters()['started'] > map_started and counters()['completed'] > map_completed
        assert loading.get_active_task_count() == 0 and pc.is_layout_editing()
        if rendered:
            assert counters()['last_paint'] > 0
        check('menu_to_stage_native_map_load_restores_ready_layout', **counters())
        pc.set_layout_editing(False)
        assert not pc.is_layout_editing() and not unreal.GameplayStatics.is_game_paused(game)
        pc.set_layout_editing(True)
        assert pc.is_layout_editing() and unreal.GameplayStatics.is_game_paused(game)
        check('controller_layout_commands_work_after_map_loading')
        map_started, map_completed = counters()['started'], counters()['completed']
        assert pc.open_play_mode(0)
        enter('returned_menu')
        return
    if phase == 'returned_menu':
        if not pc.is_main_menu() or loading.is_loading():
            return
        assert loading.get_active_task_count() == 0 and counters()['started'] > map_started and counters()['completed'] > map_completed
        assert unreal.GameplayStatics.is_game_paused(game)
        if rendered:
            assert counters()['last_paint'] > 0
        check('return_to_main_menu_removes_loading_and_retains_menu_pause', **counters())
        finish()


def tick(delta_seconds):
    global busy
    if busy or finished:
        return
    busy = True
    try:
        tick_impl(delta_seconds)
    except Exception:
        finish(traceback.format_exc())
    finally:
        busy = False


handle = unreal.register_slate_post_tick_callback(tick)
