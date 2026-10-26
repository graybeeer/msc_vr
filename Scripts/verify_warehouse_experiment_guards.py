"""PIE regression checks for experiment reservations, order sequence and history.

Requires the ignored /Game/__ExperimentTests/Transport fixture created by
verify_warehouse_experiment_transport.py. Uses the production runner and actual
vehicle ticks; never saves a map, moves a vehicle, or substitutes AI/physics.
Run with no PIE session active. -KeepTestEditorOpen leaves the editor open.
Unique snapshot/run files remain under Saved as reproducible test evidence.
"""
import csv
import hashlib
import json
from pathlib import Path
import time
import traceback

import unreal


ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/Testing/WarehouseExperiments'
STORE = ROOT / 'Saved/WarehouseExperiments'
FIXTURE = '/Game/__ExperimentTests/Transport'
TAG = 'GuardCheck_' + str(time.time_ns())
KEEP = '-keeptesteditoropen' in unreal.SystemLibrary.get_command_line().lower()
OUT.mkdir(parents=True, exist_ok=True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
started = time.monotonic()
phase = 'start'
phase_at = started
next_poll = 0.0
busy = False
handle = None
game = subsystem = None
layout_id = ''
original_order_ids = []
baseline_count = 0
session_paths = []
stored_runs = []
frozen_metrics = None
report = dict(passed=False, checks=[], samples=[], sessions=session_paths,
              scope='Native PIE destination reservation, per-pallet order sequence, stopped metrics and new GameInstance history',
              osInputTested=False, nativeTransportCompletionTested=False)


def package_hashes():
    paths = [ROOT / 'Content/FirstPerson/Lvl_FirstPerson.umap',
             ROOT / 'Content/__ExperimentTests/Transport.umap']
    for directory in ('__ExternalActors__', '__ExternalObjects__'):
        paths.extend((ROOT / 'Content' / directory / 'FirstPerson/Lvl_FirstPerson').rglob('*'))
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in paths if p.is_file()}


original_hashes = package_hashes()


def enter(value):
    global phase, phase_at
    phase, phase_at = value, time.monotonic()


def check(name, **data):
    report['checks'].append(dict(name=name, **data))
    print('EXPERIMENT_GUARD_CHECK', name, json.dumps(data, ensure_ascii=False))


def state():
    return json.loads(subsystem.get_status_json())


def refresh():
    global game, subsystem
    game = editor.get_game_world()
    if not game:
        return False
    instance = unreal.GameplayStatics.get_game_instance(game)
    subsystem = next((s for s in unreal.ObjectIterator(unreal.WarehouseExperimentSubsystem)
                      if s.get_outer() == instance), None)
    return subsystem is not None


def ensure_paused():
    pc = unreal.GameplayStatics.get_player_controller(game, 0)
    assert pc, 'Fixture requires the normal player controller'
    pc.set_layout_editing(True)
    assert unreal.GameplayStatics.is_game_paused(game), 'Initial conditions must be paused'


def validate_stopped(expected_submitted):
    global frozen_metrics
    subsystem.stop_batch()
    stopped = state()
    assert not stopped['running'] and not stopped['loading'], stopped
    metrics = stopped['metrics']
    summary = metrics['summary']
    assert metrics['status'] == 'stopped', metrics
    assert summary['planned'] == 2 and summary['released'] == 2, summary
    assert summary['submitted'] == expected_submitted, summary
    assert summary['completed'] == 0 and summary['failed'] == 0 and summary['cancelled'] == 0, summary
    assert summary['unfinished'] == 2 and summary['not_released'] == 0, summary
    assert summary['throughput_per_hour'] == 0 and summary['mean_transport_seconds'] == 0, summary
    for job in metrics['jobs']:
        assert job['result'] == 'unfinished', job
        assert abs(sum(job['phase_seconds'].values()) - job['elapsed_seconds']) < .05, job
    folder = STORE / 'Runs' / metrics['run_id']
    saved = json.loads((folder / 'Result.json').read_text(encoding='utf-8-sig'))
    assert saved == metrics, 'Durable result differs from the stopped live record'
    for name in ('Conditions.json', 'Jobs.csv', 'Vehicles.csv', 'Summary.csv'):
        assert (folder / name).is_file(), ('Missing durable run artifact', name)
    frozen_metrics = metrics
    stored_runs.append(metrics)
    check('stopped_run_saved_without_false_completion', run_id=metrics['run_id'], summary=summary)


def verify_reloaded_history():
    current = state()
    assert 'metrics' not in current, 'A new GameInstance must not inherit a live collector'
    assert current['result_count'] >= baseline_count + len(stored_runs), current
    assert subsystem.export_csv(), current['message']
    with (STORE / 'Comparison.csv').open(encoding='utf-8-sig', newline='') as stream:
        rows = {row['run_id']: row for row in csv.DictReader(stream)}
    for saved in stored_runs:
        row = rows.get(saved['run_id'])
        assert row, ('Previously saved run missing from newly loaded history', saved['run_id'])
        assert row['status'] == 'stopped' and int(row['unfinished']) == 2 and int(row['completed']) == 0, row
        assert abs(float(row['simulation_seconds']) - saved['summary']['elapsed_seconds']) < .002, row
    check('new_GameInstance_loaded_prior_results', session=session_paths[-1],
          result_count=current['result_count'], run_ids=[r['run_id'] for r in stored_runs])


def request_finish(ok, detail):
    report.update(candidate_passed=ok, detail=detail)
    try:
        if refresh() and (state()['running'] or state()['loading']):
            subsystem.stop_batch()
    except Exception:
        report['cleanup_error'] = traceback.format_exc()
    level.editor_request_end_play()
    enter('finish')


def finish():
    global handle
    report['savedMapUnchanged'] = package_hashes() == original_hashes
    report['passed'] = report.get('candidate_passed', False) and report['savedMapUnchanged']
    report['layout_id'] = layout_id
    report['run_ids'] = [r['run_id'] for r in stored_runs]
    report['wall_seconds'] = time.monotonic() - started
    (OUT / 'GuardsReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('WAREHOUSE_EXPERIMENT_GUARDS_' + ('PASSED' if report['passed'] else 'FAILED'), report.get('detail', ''))
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        handle = None
    if not KEEP:
        unreal.SystemLibrary.quit_editor()


def step():
    global layout_id, original_order_ids, baseline_count, game, subsystem
    if phase == 'finish':
        if not editor.get_game_world():
            finish()
        return
    assert time.monotonic() - started < 360, ('Guard test wall timeout', phase)
    if phase == 'start':
        assert not editor.get_game_world(), 'End the current PIE session before this test'
        assert (ROOT / 'Content/__ExperimentTests/Transport.umap').is_file(), 'Prepare the Transport fixture first'
        assert level.load_level(FIXTURE), 'Unable to load the saved Transport fixture'
        level.editor_request_begin_play()
        enter('reservation_setup')
        return
    if phase in ('restart_for_sequence', 'restart_for_history'):
        if editor.get_game_world():
            return
        game = subsystem = None
        assert level.load_level(FIXTURE)
        level.editor_request_begin_play()
        enter('sequence_setup' if phase == 'restart_for_sequence' else 'history_check')
        return
    if not refresh():
        return
    current = state()
    if phase in ('reservation_setup', 'sequence_setup', 'history_check'):
        if unreal.GameplayStatics.get_time_seconds(game) < 4:
            return
        session_paths.append(unreal.GameplayStatics.get_game_instance(game).get_path_name())
        assert len(session_paths) == len(set(session_paths)), 'Expected a newly created GameInstance after ending PIE'
        if phase != 'reservation_setup':
            verify_reloaded_history()
        if phase == 'history_check':
            request_finish(True, 'Destination reservation, same-pallet sequence, stopped accounting and durable new-session history passed')
            return
        ensure_paused()
        assert len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseForklift)) == 2
        assert len(current['sources']) == 2 and len(current['destinations']) == 2, current
        subsystem.clear_orders()
        sources = current['sources']
        destinations = current['destinations']
        if phase == 'reservation_setup':
            baseline_count = current['result_count']
            assert subsystem.save_layout(TAG), subsystem.get_editor_property('last_message')
            layout_id = next(row['id'] for row in state()['layouts'] if row['name'] == TAG)
            for source in sources:
                assert subsystem.add_order(source['id'], destinations[0]['id'], 0, ''), subsystem.get_editor_property('last_message')
            original_order_ids = [o.get_editor_property('order_id') for o in subsystem.get_editor_property('orders')]
            assert subsystem.start_batch([layout_id], 1, 60, 42, 1), subsystem.get_editor_property('last_message')
            enter('reservation_observe')
        else:
            for destination in destinations:
                assert subsystem.add_order(sources[0]['id'], destination['id'], 0, ''), subsystem.get_editor_property('last_message')
            original_order_ids = [o.get_editor_property('order_id') for o in subsystem.get_editor_property('orders')]
            # UE FRandomStream seed 0 swaps the two equal-release orders. We also
            # assert the actual collector array is reversed, so this tests the guard.
            assert subsystem.start_batch([layout_id], 1, 60, 0, 1), subsystem.get_editor_property('last_message')
            enter('sequence_observe')
        return
    if phase in ('reservation_observe', 'sequence_observe'):
        if current['loading'] or 'metrics' not in current:
            return
        assert current['running'], current
        metrics = current['metrics']
        summary = metrics['summary']
        submitted = [j for j in metrics['jobs'] if j['submitted_seconds'] >= 0]
        report['samples'].append(dict(phase=phase, elapsed=summary['elapsed_seconds'],
                                      submitted=[j['order_id'] for j in submitted],
                                      completed=summary['completed'], failed=summary['failed']))
        assert len(submitted) <= 1, ('Conflicting order was dispatched concurrently', metrics)
        assert summary['failed'] == 0 and summary['completed'] == 0, ('Fixture completed or failed before the reservation observation window', metrics)
        if phase == 'reservation_observe':
            if summary['elapsed_seconds'] < 10:
                return
            assert len(submitted) == 1, ('No native submission observed', metrics)
            waiting = next(j for j in metrics['jobs'] if j['submitted_seconds'] < 0)
            assert waiting['phase_seconds']['queue'] >= 9.9, waiting
            check('shared_destination_keeps_second_order_queued', elapsed=summary['elapsed_seconds'],
                  submitted_order=submitted[0]['order_id'], waiting_order=waiting['order_id'])
            validate_stopped(1)
            enter('reservation_frozen')
        else:
            assert [j['order_id'] for j in metrics['jobs']] == list(reversed(original_order_ids)), 'Seed did not actually reverse the equal-release array'
            if not submitted:
                return
            assert submitted[0]['order_id'] == original_order_ids[0], ('Later same-pallet instruction overtook its predecessor', metrics)
            check('same_pallet_preserves_registration_sequence_after_shuffle', original_ids=original_order_ids,
                  shuffled_ids=[j['order_id'] for j in metrics['jobs']], first_submitted=submitted[0]['order_id'])
            validate_stopped(1)
            enter('sequence_frozen')
        return
    if phase in ('reservation_frozen', 'sequence_frozen') and time.monotonic() - phase_at > 1:
        assert current['metrics'] == frozen_metrics, 'Stopped metrics changed while the world continued ticking'
        check('stopped_metrics_remain_frozen', run_id=frozen_metrics['run_id'])
        level.editor_request_end_play()
        enter('restart_for_sequence' if phase == 'reservation_frozen' else 'restart_for_history')


def tick(_dt):
    global next_poll, busy
    if busy or time.monotonic() < next_poll:
        return
    next_poll = time.monotonic() + .5
    busy = True
    try:
        step()
    except Exception:
        request_finish(False, traceback.format_exc())
    finally:
        busy = False


handle = unreal.register_slate_post_tick_callback(tick)
