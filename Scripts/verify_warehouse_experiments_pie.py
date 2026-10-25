"""Saved-warehouse snapshot round trips through real fresh-world level travel.

Uses production save/load and native layout transaction APIs, not OS input.
The original map and external actor packages are hashed and never saved.
An unsaved distant support pad isolates add/rotate/group-move cases without
clearing production stock. Runtime JSON files are retained as test evidence.
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
report = dict(passed=False, osInputTested=False, nativeTransportTested=False,
              scope='Saved warehouse, native layout edits, persisted JSON, fresh-world A/B/A restore', checks=[])
started = time.monotonic()
phase = 'start'
phase_at = started
handle = None
next_poll = 0.0
game = subsystem = layout = pc = None
snapshots = {}
ids = {}
live_before = {}
created_names = {}
TAG = 'SnapshotTest_' + str(int(time.time()))


def files():
    paths = [ROOT / 'Content/FirstPerson/Lvl_FirstPerson.umap']
    for folder in ('__ExternalActors__', '__ExternalObjects__'):
        paths.extend((ROOT / 'Content' / folder / 'FirstPerson/Lvl_FirstPerson').rglob('*'))
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in paths if p.is_file()}


original_files = files()
assert level.load_level(LEVEL)
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.WarehouseForklift):
        actor.set_editor_property('auto_start', False)
        actor.set_editor_property('idle_parking_enabled', False)
    elif isinstance(actor, unreal.WarehouseInboundZone):
        actor.set_editor_property('fleet', [])
pad = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(8000, 0, -10))
pad.set_actor_label(TAG + '_Pad')
pad.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
pad.static_mesh_component.set_collision_profile_name('BlockAll')
pad.set_actor_scale3d(unreal.Vector(40, 40, .2))
pad.tags = ['LayoutFixed', 'WarehouseMapStructure']


def check(name, **data):
    report['checks'].append(dict(name=name, **data))
    print('EXPERIMENT_STORAGE_CHECK', name, json.dumps(data, ensure_ascii=False))


def enter(value):
    global phase, phase_at
    phase, phase_at = value, time.monotonic()


def allof(cls):
    return list(unreal.GameplayStatics.get_all_actors_of_class(game, cls))


def stable_id(actor):
    return next((str(t)[13:] for t in actor.tags if str(t).startswith('ExperimentId:')),
                'actor:' + actor.get_name())


def find_id(identity):
    return next((a for a in allof(unreal.Actor) if stable_id(a) == identity), None)


def ensure_edit():
    global pc, layout
    pc = unreal.GameplayStatics.get_player_controller(game, 0)
    pc.close_warehouse_experiments()
    pc.set_observer_floor(0)
    pc.set_layout_editing(True)
    layout = allof(unreal.WarehouseLayoutEditor)[0]
    assert layout.get_editor_property('editing') and unreal.GameplayStatics.is_game_paused(game)


def save(key):
    name = TAG + '_' + key
    assert subsystem.save_layout(name), subsystem.get_editor_property('last_message')
    state = json.loads(subsystem.get_status_json())
    row = next(r for r in state['layouts'] if r['name'] == name)
    ids[key] = row['id']
    snapshots[key] = json.loads((LAYOUTS / (row['id'] + '.json')).read_text(encoding='utf-8-sig'))
    check('saved_' + key, id=row['id'], cargo_hash=row['cargo_hash'],
          actors=len(snapshots[key]['actors']), cargo=snapshots[key]['cargoCount'], held=snapshots[key]['heldCargoCount'])


def add(category, point):
    layout.set_category(category)
    layout.set_action(unreal.WarehouseLayoutAction.ADD)
    before = {a.get_name() for a in allof(unreal.Actor)}
    assert layout.begin_add(point), layout.get_editor_property('status')
    layout.update_drag(point)
    assert layout.end_drag(True), layout.get_editor_property('status')
    return [a for a in allof(unreal.Actor) if a.get_name() not in before]


def verify(key):
    expected = snapshots[key]
    assert unreal.GameplayStatics.is_game_paused(game), 'Ordinary restore resumed the simulation'
    live = {stable_id(a): a for a in allof(unreal.Actor)}
    missing = [r['id'] for r in expected['actors'] if r['id'] not in live]
    assert not missing, ('Missing restored identities', missing[:20])
    cargo = allof(unreal.WarehouseCargo)
    assert len(cargo) == expected['cargoCount'], ('Cargo count changed', len(cargo), expected['cargoCount'])
    store = allof(unreal.WarehouseLayoutInventory)[0]
    assert len(store.get_editor_property('held_cargo')) == expected['heldCargoCount']
    restored_held = [str(r.cargo_id) for r in store.get_editor_property('held_cargo')]
    assert restored_held == report['held_ids'], ('Held FIFO order changed', restored_held)
    static_count = 0
    for row in expected['actors']:
        actor = live[row['id']]
        if row['class'].endswith('.WarehouseCargo'):
            props = row['properties']
            assert abs(actor.get_editor_property('gross_mass_kg') - float(props['GrossMassKg'])) < .001
            assert actor.root_component.is_simulating_physics(), actor.get_name()
        if row['class'].endswith('.WarehouseForklift'):
            assert abs(actor.get_physical_mass_kg() - 1000) < .5
            assert actor.get_editor_property('empty_travel_speed_cm') == 180
            assert not actor.get_editor_property('powered'), ('Restored vehicle auto-started', row['id'])
            assert not actor.get_editor_property('pending_jobs'), ('Restored vehicle received unrequested work', row['id'])
            assert not actor.get_editor_property('active_job').job_id, ('Restored vehicle started unrequested work', row['id'])
            expected_auto_start = row['properties']['bAutoStart'].lower() == 'true'
            assert actor.get_editor_property('auto_start') == expected_auto_start, ('Auto-start preference changed', row['id'])
            for prop in ('charging_station', 'waiting_bay', 'elevator'):
                reference = actor.get_editor_property(prop)
                if reference:
                    assert stable_id(reference) in live, ('Broken equipment reference', actor.get_name(), prop)
        if row['class'] == '/Script/Engine.StaticMeshActor':
            # Collision and source-floor bookkeeping must survive independently of
            # runtime labels generated by native damage/observer presentation.
            for part in row['parts']:
                component = next((c for c in actor.get_components_by_class(unreal.SceneComponent)
                                  if c.get_name() == part['name']), None)
                assert component
                text = part['pose']['translation']
                xyz = [float(field.split('=')[1]) for field in text.split()]
                actual = component.get_world_location()
                delta = (actual - unreal.Vector(*xyz)).length()
                if delta >= .2:
                    detail = dict(label=actor.get_actor_label(), actor=row['id'], component=part['name'],
                                  expected_cm=xyz, actual_cm=[actual.x, actual.y, actual.z], delta_cm=delta,
                                  saved_simulates=part.get('simulate', False), mobility=part['mobility'])
                    if isinstance(component, unreal.PrimitiveComponent):
                        velocity = component.get_physics_linear_velocity()
                        detail.update(simulates=component.is_simulating_physics(),
                                      velocity_cm_s=[velocity.x, velocity.y, velocity.z])
                    nearby = []
                    for other in live.values():
                        if other == actor or (other.get_actor_location() - actual).length() > 250:
                            continue
                        center, extent = other.get_actor_bounds(True)
                        nearby.append(dict(label=other.get_actor_label(), id=stable_id(other),
                                           center=[center.x, center.y, center.z],
                                           extent=[extent.x, extent.y, extent.z]))
                    detail['nearby_bounds'] = nearby[:30]
                    check('restore_transform_mismatch', **detail)
                    raise AssertionError(detail)
                if part.get('mesh'):
                    assert component.get_editor_property('static_mesh').get_path_name() == part['mesh']
            static_count += 1
    for zone in allof(unreal.WarehouseInboundZone):
        assert zone.get_editor_property('dispatched_count') == 0, 'Inbound NPC dispatched during initial restore'
        for slot in zone.get_editor_property('rack_slots'):
            assert slot.rack and stable_id(slot.rack) in live, ('Broken rack slot', slot.name)
    for conveyor in allof(unreal.WarehouseConveyor):
        for prop in ('next_conveyor', 'lift'):
            target = conveyor.get_editor_property(prop)
            if target:
                assert stable_id(target) in live
    ensure_edit()
    check('fresh_world_' + key + '_identities_assets_physics_refs', static_actors=static_count,
          cargo=len(cargo), held=len(restored_held), vehicles=len(allof(unreal.WarehouseForklift)),
          paused=True, no_unscheduled_work=True, auto_start_preserved=True)


def request_finish(ok, message):
    report.update(candidate_passed=ok, message=message)
    level.editor_request_end_play()
    enter('finish')


def finish():
    global handle
    try:
        unchanged = files() == original_files
        report['savedMapUnchanged'] = unchanged
        report['passed'] = report.get('candidate_passed', False) and unchanged
        report['wall_seconds'] = round(time.monotonic() - started, 2)
        report['layouts'] = ids
        (OUT / 'StorageReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        print('EXPERIMENT_STORAGE_' + ('PASSED' if report['passed'] else 'FAILED'), report['message'])
    finally:
        if handle is not None:
            unreal.unregister_slate_post_tick_callback(handle)
            handle = None
        if not KEEP:
            unreal.SystemLibrary.quit_editor()


def tick(_dt):
    global game, subsystem, created_names, next_poll
    now = time.monotonic()
    if now < next_poll:
        return
    next_poll = now + .5
    try:
        assert time.monotonic() - started < 1200, ('Timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            enter('settle')
            return
        if phase == 'finish':
            if editor.get_game_world():
                return
            finish()
            return
        game = editor.get_game_world()
        if not game:
            return
        instance = unreal.GameplayStatics.get_game_instance(game)
        subsystem = next(s for s in unreal.ObjectIterator(unreal.WarehouseExperimentSubsystem)
                         if s.get_outer() == instance)
        assert subsystem
        state = json.loads(subsystem.get_status_json())
        if phase == 'settle':
            if unreal.GameplayStatics.get_time_seconds(game) < 3:
                return
            assert not subsystem.save_layout(TAG + '_Unpaused'), 'Running world was accepted as an initial condition'
            ensure_edit()
            # Save the authored preference without starting a vehicle. A fresh
            # ordinary load must suppress BeginPlay's automatic startup timer.
            allof(unreal.WarehouseForklift)[0].set_editor_property('auto_start', True)
            # Preserve physical box data through the existing deletion/FIFO path.
            candidate = next(c for c in allof(unreal.WarehouseCargo) if c.get_actor_label() == 'WH_Drop_Box')
            layout.set_category(unreal.WarehouseLayoutCategory.CARGO)
            layout.set_action(unreal.WarehouseLayoutAction.DELETE)
            layout.set_selection([candidate])
            assert layout.delete_selection(), layout.get_editor_property('status')
            store = allof(unreal.WarehouseLayoutInventory)[0]
            assert store.create_held_cargo(TAG + '_HELD', unreal.Text('실험 보류 상품'), 14, unreal.Vector(30, 25, 20))
            report['held_ids'] = [str(r.cargo_id) for r in store.get_editor_property('held_cargo')]
            save('A')
            enter('edit_B')
            return
        if phase == 'edit_B':
            members = add(unreal.WarehouseLayoutCategory.RACK, unreal.Vector(8000, -900, 0))
            rack = next(a for a in members if a.actor_has_tag('LayoutType:Rack'))
            layout.set_action(unreal.WarehouseLayoutAction.MOVE)
            layout.set_selection([rack])
            assert layout.rotate_selection(90), layout.get_editor_property('status')
            point = unreal.Vector(8000, -900, 0)
            assert layout.begin_drag(rack, point), layout.get_editor_property('status')
            layout.update_drag(point + unreal.Vector(400, 0, 0))
            assert layout.end_drag(True), layout.get_editor_property('status')
            created_names['rack'] = stable_id(rack)
            bays = add(unreal.WarehouseLayoutCategory.WAITING_BAY, unreal.Vector(7500, -900, 0))
            created_names['bay'] = stable_id(next(a for a in bays if isinstance(a, unreal.WarehouseWaitingBay)))
            first = add(unreal.WarehouseLayoutCategory.CONVEYOR, unreal.Vector(7600, 700, 0))
            second = add(unreal.WarehouseLayoutCategory.CONVEYOR, unreal.Vector(8100, 700, 0))
            first = next(a for a in first if isinstance(a, unreal.WarehouseConveyor))
            second = next(a for a in second if isinstance(a, unreal.WarehouseConveyor))
            layout.set_action(unreal.WarehouseLayoutAction.MOVE)
            layout.set_selection([first, second])
            assert layout.connect_selected_conveyors(), layout.get_editor_property('status')
            created_names['conveyor'] = stable_id(first)
            created_names['next_conveyor'] = stable_id(second)
            save('B')
            assert snapshots['A']['cargoFingerprint'] == snapshots['B']['cargoFingerprint'], 'Layout-only edits changed common cargo conditions'
            check('B_native_add_rotate_group_move_and_conveyor_connection', ids=created_names)
            before = {stable_id(a) for a in allof(unreal.Actor)}
            bad = LAYOUTS / ('f' * 32 + '.json')
            assert not bad.exists(), 'Reserved tamper-test ID already exists'
            tampered = dict(snapshots['A'])
            tampered['id'] = 'f' * 32
            tampered['cargoCount'] += 1
            bad.write_text(json.dumps(tampered, ensure_ascii=False), encoding='utf-8')
            try:
                assert not subsystem.load_layout('f' * 32), 'Tampered layout accepted'
                assert not subsystem.load_layout('../outside'), 'Path traversal accepted'
                assert before == {stable_id(a) for a in allof(unreal.Actor)}, 'Rejected load mutated the current world'
            finally:
                bad.unlink()
            check('tampered_and_invalid_id_rejected_without_mutation')
            assert subsystem.load_layout(ids['A']), subsystem.get_editor_property('last_message')
            enter('load_A')
            return
        if phase.startswith('load_'):
            if state['loading']:
                return
            assert time.monotonic() - phase_at > 1
            key = phase[5:]
            assert '초기 배치 복원 완료' in state['message'], state
            verify('A' if key == 'A_again' else key)
            if key == 'A':
                assert not find_id(created_names['rack']) and not find_id(created_names['bay']), 'B-only additions leaked into A'
                assert subsystem.load_layout(ids['B']), subsystem.get_editor_property('last_message')
                enter('load_B')
            elif key == 'B':
                first = find_id(created_names['conveyor'])
                assert first and stable_id(first.get_editor_property('next_conveyor')) == created_names['next_conveyor']
                # Restored prototype catalogue must still support ordinary additions.
                layout.set_category(unreal.WarehouseLayoutCategory.RACK)
                assert layout.get_add_choice_label() == '팔레트 보관 랙'
                check('restored_native_reference_and_add_catalogue')
                assert subsystem.load_layout(ids['A']), subsystem.get_editor_property('last_message')
                enter('load_A_again')
            else:
                check('A_B_A_fresh_world_round_trip_complete')
                request_finish(True, 'Save/load A/B/A, held FIFO, added/removed objects, rack transforms, conveyor references and native physics passed')
            return
    except Exception:
        message = traceback.format_exc()
        print('EXPERIMENT_STORAGE_EXCEPTION', message)
        request_finish(False, message)


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
