"""Native building expansion, snapshot restoration, and a second edit after travel.

Uses an unsaved production level and never saves its packages. A snapshot must
preserve both generated structures and the hidden source restoration records.
"""
import hashlib
import json
from pathlib import Path
import time
import traceback

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/Testing/WarehouseExperiments'
OUT.mkdir(parents=True, exist_ok=True)
LAYOUTS = ROOT / 'Saved/WarehouseExperiments/Layouts'
LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
KEEP = '-keeptesteditoropen' in unreal.SystemLibrary.get_command_line().lower()
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report = dict(passed=False, osInputTested=False, checks=[])
phase = 'start'
started = time.monotonic()
handle = None
next_poll = 0.0
game = subsystem = building = None
snapshot = None
saved_id = None


def files():
    paths = [ROOT / 'Content/FirstPerson/Lvl_FirstPerson.umap']
    for folder in ('__ExternalActors__', '__ExternalObjects__'):
        paths.extend((ROOT / 'Content' / folder / 'FirstPerson/Lvl_FirstPerson').rglob('*'))
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in paths if p.is_file()}


original = files()
assert level.load_level(LEVEL)
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.WarehouseForklift):
        actor.set_editor_property('auto_start', False)
        actor.set_editor_property('idle_parking_enabled', False)
    elif isinstance(actor, unreal.WarehouseInboundZone):
        actor.set_editor_property('fleet', [])


def allof(cls):
    return list(unreal.GameplayStatics.get_all_actors_of_class(game, cls))


def identity(actor):
    return next((str(t)[13:] for t in actor.tags if str(t).startswith('ExperimentId:')),
                'actor:' + actor.get_name())


def check(name, **values):
    report['checks'].append(dict(name=name, **values))
    print('EXPERIMENT_BUILDING_CHECK', name, json.dumps(values, ensure_ascii=False))


def edit():
    global building
    pc = unreal.GameplayStatics.get_player_controller(game, 0)
    pc.close_warehouse_experiments()
    pc.set_layout_editing(True)
    pc.set_construction_editing(True)
    building = allof(unreal.WarehouseBuildingLayout)[0]
    assert unreal.GameplayStatics.is_game_paused(game)


def expand():
    attempts = []
    for width, depth, roof in [(200, 200, 100), (200, 0, 100), (0, 0, 100)]:
        settings = building.get_settings()
        settings.width_cm += width
        settings.depth_cm += depth
        settings.roof_height_cm += roof
        result = building.apply_settings(settings)
        attempts.append(dict(width_delta=width, depth_delta=depth, roof_delta=roof,
                             accepted=result, reason=building.get_editor_property('status')))
        if result:
            check('native_building_expansion', attempts=attempts)
            return
    raise AssertionError(('No building expansion accepted', attempts))


def verify():
    assert unreal.GameplayStatics.is_game_paused(game), 'Restored building was not paused for editing'
    for vehicle in allof(unreal.WarehouseForklift):
        velocity = vehicle.get_velocity()
        assert velocity.length() < .01, ('Initial-condition restore retained motion', vehicle.get_actor_label(), str(velocity))
        assert not vehicle.is_remote_controlled() and not vehicle.get_supported_pallet()
        assert not vehicle.get_editor_property('powered')
    live = {identity(a): a for a in allof(unreal.Actor)}
    row = next(r for r in snapshot['actors'] if r['class'].endswith('.WarehouseBuildingLayout'))
    restored = live[row['id']]
    settings = row['properties']['Settings']
    for key in ('WidthCm', 'DepthCm', 'RoofHeightCm'):
        snake = {'WidthCm': 'width_cm', 'DepthCm': 'depth_cm', 'RoofHeightCm': 'roof_height_cm'}[key]
        assert abs(getattr(restored.get_settings(), snake) - float(settings[key])) < .01
    generated = restored.get_editor_property('generated_structure')
    expected = [r['actor'] for r in row['properties']['GeneratedStructure']]
    assert {identity(a) for a in generated} == set(expected)
    assert restored.get_editor_property('has_applied_changes') and generated
    for source in restored.get_editor_property('replaced_structure'):
        assert source.get_editor_property('hidden') and not source.get_actor_enable_collision(), source.get_name()
    for geometry in snapshot['actors']:
        if geometry['id'] not in expected:
            continue
        actor = live[geometry['id']]
        for part in geometry['parts']:
            component = next(c for c in actor.get_components_by_class(unreal.SceneComponent)
                             if c.get_name() == part['name'])
            vector = [float(v.split('=')[1]) for v in part['pose']['translation'].split()]
            assert (component.get_world_location() - unreal.Vector(*vector)).length() < .2
    assert len(allof(unreal.WarehouseCargo)) == snapshot['cargoCount']
    assert len(allof(unreal.WarehousePallet)) == sum(r['class'].endswith('.WarehousePallet') for r in snapshot['actors'])
    check('restored_building_dimensions_geometry_sources_stock', generated=len(generated),
          sources=len(restored.get_editor_property('replaced_structure')), vehicles_at_rest=True)


def end(ok, message):
    global phase
    report.update(candidate_passed=ok, message=message)
    level.editor_request_end_play()
    phase = 'finish'


def tick(_dt):
    global game, subsystem, phase, handle, snapshot, saved_id, next_poll
    now = time.monotonic()
    if now < next_poll:
        return
    next_poll = now + .5
    try:
        assert time.monotonic() - started < 1200, ('Timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if phase == 'finish':
            if game:
                return
            report['savedMapUnchanged'] = files() == original
            report['passed'] = report['candidate_passed'] and report['savedMapUnchanged']
            report['wall_seconds'] = round(time.monotonic() - started, 2)
            (OUT / 'BuildingStorageReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
            print('EXPERIMENT_BUILDING_' + ('PASSED' if report['passed'] else 'FAILED'))
            unreal.unregister_slate_post_tick_callback(handle)
            handle = None
            if not KEEP:
                unreal.SystemLibrary.quit_editor()
            return
        if not game:
            return
        instance = unreal.GameplayStatics.get_game_instance(game)
        subsystem = next(s for s in unreal.ObjectIterator(unreal.WarehouseExperimentSubsystem)
                         if s.get_outer() == instance)
        state = json.loads(subsystem.get_status_json())
        if phase == 'settle':
            if unreal.GameplayStatics.get_time_seconds(game) < 3:
                return
            edit()
            expand()
            name = 'BuildingSnapshot_' + str(int(time.time()))
            assert subsystem.save_layout(name), subsystem.get_editor_property('last_message')
            row = next(r for r in json.loads(subsystem.get_status_json())['layouts'] if r['name'] == name)
            saved_id = row['id']
            snapshot = json.loads((LAYOUTS / (saved_id + '.json')).read_text(encoding='utf-8-sig'))
            report['layout'] = saved_id
            assert snapshot['floorSources'], 'Building rebuild did not retain elevator source-floor records'
            generated = [r for r in snapshot['actors']
                         if 'WarehouseBuildingGenerated' in r['properties'].get('Tags', [])]
            assert generated and all(r['parts'] for r in generated), 'Transient building geometry/light parts were omitted'
            replacements = {r['actor'] for r in snapshot['floorReplacements']}
            assert all(r['parts'] for r in snapshot['actors'] if r['id'] in replacements), 'Replacement floor mesh parts were omitted'
            check('generated_parts_saved', generated=len(generated), floor_replacements=len(replacements))
            assert subsystem.load_layout(saved_id), subsystem.get_editor_property('last_message')
            phase = 'load'
        elif phase in ('load', 'reload'):
            if state['loading']:
                return
            assert '초기 배치 복원 완료' in state['message'], state
            verify()
            edit()
            if phase == 'load':
                expand()
                check('second_native_building_edit_after_restore')
                assert subsystem.load_layout(saved_id), subsystem.get_editor_property('last_message')
                phase = 'reload'
            else:
                end(True, 'Generated building and source-floor state survive fresh-world loading and a second native edit; repeated restoration succeeds')
    except Exception:
        error = traceback.format_exc()
        print('EXPERIMENT_BUILDING_EXCEPTION', error)
        end(False, error)


callback_busy = False


def guarded_tick(dt):
    global callback_busy
    if callback_busy:
        return
    callback_busy = True
    try:
        tick(dt)
    finally:
        callback_busy = False


handle = unreal.register_slate_post_tick_callback(guarded_tick)
