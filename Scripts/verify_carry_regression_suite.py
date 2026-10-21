"""Run the three carry regressions in one editor; never save a fixture.

Launch RenderOffscreen with -PlacementOnly -SampleAnimationChecksOnly
-SampleAnimationCapture. Child scripts keep their own assertions/callbacks;
only their shutdown is deferred until all three have reported success.
"""
import builtins
from pathlib import Path
import time
import traceback
import unreal

CASES = (
    ('verify_carry_response_pie.py', 'CARRY_RESPONSE_PIE_PASSED'),
    ('verify_empty_storage_pie.py', 'EMPTY_STORAGE_PIE_PASSED'),
    ('verify_character_drive_response_pie.py', 'GAME_ANIMATION_SAMPLE_PIE_PASSED'),
)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
index = 0
ready_at = time.monotonic()
waiting = True


def stop(failure):
    builtins.print('CARRY_REGRESSION_SUITE_FAILED', failure)
    unreal.unregister_slate_post_tick_callback(suite_handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    global index, waiting
    if not waiting or time.monotonic() < ready_at or level.is_in_play_in_editor() or editor.get_game_world():
        return
    if index == len(CASES):
        builtins.print('CARRY_REGRESSION_SUITE_PASSED')
        unreal.unregister_slate_post_tick_callback(suite_handle)
        unreal.SystemLibrary.quit_editor()
        return
    filename, marker = CASES[index]
    path = Path(__file__).parent / filename
    outcome = {'passed': False, 'failed': False}

    def output(*values, **kwargs):
        line = ' '.join(str(v) for v in values)
        outcome['passed'] |= marker in line
        outcome['failed'] |= '_FAILED' in line
        builtins.print(*values, **kwargs)

    def completed():
        global index, waiting, ready_at
        unreal.unregister_slate_post_tick_callback(context['handle'])
        level.editor_request_end_play()
        if outcome['failed'] or not outcome['passed']:
            stop(filename + ' did not report success')
            return
        index += 1
        waiting = True
        ready_at = time.monotonic() + 1

    context = {'__file__': str(path), '__name__': '__main__', 'print': output}
    waiting = False
    try:
        exec(compile(path.read_text(encoding='utf-8-sig'), str(path), 'exec'), context)
        context['finish'] = completed
    except Exception:
        stop(traceback.format_exc())


suite_handle = unreal.register_slate_post_tick_callback(tick)
