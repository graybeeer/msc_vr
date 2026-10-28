"""Unsaved Entry PIE regression for 1 m mezzanine editing and generated topology.

Exercises production building/editor APIs and native scene collision queries.
No map or asset is saved. This is not an OS pointer or packaged-game test.
"""
import hashlib
import json
import math
from pathlib import Path
import time
import traceback

import unreal


command = unreal.SystemLibrary.get_command_line().lower()
rendered = '-nullrhi' not in command
keep_open = '-keeptesteditoropen' in command
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
output = (Path(unreal.Paths.project_saved_dir()) / 'Testing/BuildingTiles_20261028').resolve()
output.mkdir(parents=True, exist_ok=True)
saved_map = Path(unreal.Paths.project_content_dir()) / 'FirstPerson/Lvl_FirstPerson.umap'
original_hash = hashlib.sha256(saved_map.read_bytes()).hexdigest()
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
report = dict(passed=False, rendered=rendered, osInputTested=False, packagedGameTested=False,
              scope='Unsaved Entry; public draft strokes and atomic building edits; native collision and cargo preservation',
              visualFixture='Temporary untextured cube architecture, not the default warehouse appearance',
              checks=[], shapes=[], screenshots=[], exclusions=['OS input', 'Packaged game', 'Full warehouse performance comparison',
                                                'Snapshot file roundtrip is covered by native SnapshotValues'])
started = time.monotonic()
phase = 'start'
busy = finished = False
handle = None
game = pc = construction = building = loading = parcel = None
stock_snapshot = None
draft_checkpoint = None
capture_after = 0
capture_frames = 0


def check(name, **data):
    report['checks'].append(dict(name=name, **data))
    print('BUILDING_TILES_CHECK', name, json.dumps(data, ensure_ascii=False))


def allof(cls):
    return list(unreal.GameplayStatics.get_all_actors_of_class(game, cls))


def pose(actor):
    return tuple(round(float(getattr(vector, axis)), 4)
                 for vector, axes in ((actor.get_actor_location(), ('x', 'y', 'z')),
                                      (actor.get_actor_rotation(), ('pitch', 'yaw', 'roll')),
                                      (actor.get_actor_scale3d(), ('x', 'y', 'z')))
                 for axis in axes)


def physical(actor):
    body = actor.get_component_by_class(unreal.StaticMeshComponent)
    return (pose(actor), actor.get_actor_enable_collision(), actor.get_editor_property('hidden'),
            body.get_collision_enabled(), body.is_simulating_physics(), body.is_gravity_enabled(),
            round(body.get_mass(), 3) if body.is_simulating_physics() else None)


def geometry():
    return tuple((actor.get_path_name(), physical(actor)) for actor in building.get_editor_property('generated_structure'))


def counts():
    return dict(active=loading.get_active_task_count(), started=loading.get_editor_property('started_screens'),
                completed=loading.get_editor_property('completed_screens'), paint=loading.get_editor_property('last_paint_count'))


def balanced(before):
    after = counts()
    assert after['active'] == 0 and after['started']-before['started'] == after['completed']-before['completed'], (before, after)
    assert unreal.GameplayStatics.is_game_paused(game), 'Building transaction changed the editor pause state'
    if rendered and after['started'] > before['started']:
        assert after['paint'] > 0, 'Building calculation did not paint its loading curtain'
    return after


def spawn(cls, name, point):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
    assert actor, name
    actor.set_actor_label(name)
    return actor


def fixture():
    assert not editor.get_game_world(), 'Stop previous PIE before running the tile fixture'
    assert level.load_level('/Engine/Maps/Entry')
    for actor in list(actors.get_all_level_actors()):
        if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
            actors.destroy_actor(actor)
    world = editor.get_editor_world()
    settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
    mode = unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C')
    assert mode
    settings.set_editor_property('default_game_mode', mode)
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    assert cube
    structural = ['LayoutFixed', 'WarehouseMapStructure']

    def block(name, point, size, tags=()):
        actor = spawn(unreal.StaticMeshActor, name, point)
        actor.static_mesh_component.set_static_mesh(cube)
        actor.static_mesh_component.set_collision_profile_name('BlockAll')
        actor.set_actor_scale3d(unreal.Vector(*[v/100 for v in size]))
        actor.tags = list(tags)
        return actor

    block('Floor', (400, 0, -10), (4800, 4000, 20), structural+['ElevatorLayoutFloor', 'WarehouseFloor0'])
    for floor in (1, 2):
        block(f'MZ_L{floor+1}_Deck_TileFixture', (0, 0, floor*400-10), (3600, 3600, 20),
              structural+['ElevatorLayoutFloor', f'WarehouseFloor{floor}'])
    block('WH_Roof', (400, 0, 1215), (4800, 4000, 30), structural+['ObserverRoof'])
    for index, (name, size) in enumerate((('WH_Wall_N', (4800, 35, 1200)), ('WH_Column_01', (40, 40, 1200)),
                                        ('WH_RoofBeam_01', (1000, 20, 30)), ('MZ_L2_InnerLeft_Bar55', (160, 5, 5)))):
        block(name, (20000, index*5000, 2000), size, structural)
    for name, point, mass in (('TilesGroundCargo', (1200, -1400, 20.1), 12),
                              ('TilesUnsupportedCargo', (1000, 1000, 420.1), 23)):
        cargo = spawn(unreal.WarehouseCargo, name, point)
        cargo.set_cargo_mesh(cube)
        cargo.set_actor_scale3d(unreal.Vector(.4, .3, .4))
        cargo.set_gross_mass_kg(mass)
        cargo.set_editor_property('cargo_id', name)
    spawn(unreal.WarehouseDamageSystem, 'TilesStrength', (0, 0, 0))
    spawn(unreal.PlayerStart, 'TilesSafePlayerStart', (1700, 1500, 100))
    spawn(unreal.WarehouseSimulationClock, 'TilesClock', (0, 0, 0))
    sun = spawn(unreal.DirectionalLight, 'TilesSun', (0, 0, 1800))
    sun.set_actor_rotation(unreal.Rotator(pitch=-55, yaw=25), False)
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(3)
    sky = spawn(unreal.SkyLight, 'TilesSky', (0, 0, 1800))
    sky.get_component_by_class(unreal.SkyLightComponent).set_intensity(1)


BASE = {(x, y) for x in range(-22, 14) for y in range(-18, 18)}
SHAPES = {
    'L': {(x, y) for x, y in BASE if x < -14 or y < -10},
    'U': {(x, y) for x, y in BASE if x < -14 or x >= 6 or y < -10},
    'central_hole': {(x, y) for x, y in BASE if not (-8 <= x < 0 and -4 <= y < 4)},
}


def set_mask(deck, cells):
    deck.set_editor_property('use_tile_mask', True)
    deck.tiles = [unreal.IntPoint(x=x, y=y) for x, y in sorted(cells)]
    if cells:
        left, right = min(x for x, _ in cells)*100, (max(x for x, _ in cells)+1)*100
        bottom, top = min(y for _, y in cells)*100, (max(y for _, y in cells)+1)*100
        deck.offset_cm = unreal.Vector2D((left+right)/2, (bottom+top)/2)
        deck.size_cm = unreal.Vector2D(right-left, top-bottom)
    deck.atrium_size_cm = unreal.Vector2D(0, 0)


def cells_of(deck):
    assert deck.get_editor_property('use_tile_mask')
    return {(p.x, p.y) for p in deck.tiles}


def settings_for(cells):
    proposed = building.get_settings()
    deck = proposed.floor2
    deck.enabled = True
    set_mask(deck, cells)
    proposed.floor2 = deck
    for field in ('floor3', 'floor4'):
        other = proposed.get_editor_property(field)
        other.enabled = False
        proposed.set_editor_property(field, other)
    return proposed


def trace(a, b):
    origin = building.get_actor_location()
    hit = unreal.SystemLibrary.line_trace_single(game, origin+unreal.Vector(*a), origin+unreal.Vector(*b),
        unreal.TraceTypeQuery.ECC_VISIBILITY, False, [parcel], unreal.DrawDebugTrace.NONE, True)
    # Unreal Python represents a non-blocking trace as None rather than a
    # HitResult. A deleted floor tile should reach this normal empty-space path.
    return hit.to_tuple() if hit is not None else (False,)


def topology(label, cells, elapsed):
    generated = list(building.get_editor_property('generated_structure'))
    assert len(generated) == len([a for a in allof(unreal.StaticMeshActor) if a.actor_has_tag('WarehouseBuildingGenerated')])
    assert cells_of(building.get_settings().floor2) == cells
    origin = building.get_actor_location()
    tested = 0
    for x, y in BASE:
        cx, cy = x*100+50, y*100+50
        hit = trace((cx, cy, 405), (cx, cy, 395))
        assert bool(hit[0]) == ((x, y) in cells), ('Incorrect physical floor at tile', label, (x, y), hit)
        if hit[0]:
            assert abs(hit[5].z-origin.z-400) < .1, (label, (x, y), hit)
        tested += 1
    edge_count = seam_count = 0
    stair = building.get_settings().floor2
    angle = math.radians(stair.stair_yaw_degrees)
    exit_x = stair.stair_offset_cm.x+406*math.cos(angle)+60*math.sin(angle)
    exit_y = stair.stair_offset_cm.y+406*math.sin(angle)-60*math.cos(angle)
    flight_half_x = abs(math.cos(angle))*336+abs(math.sin(angle))*75
    flight_half_y = abs(math.sin(angle))*336+abs(math.cos(angle))*75
    for x, y in sorted(cells):
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            px, py = (x+.5+dx*.5)*100, (y+.5+dy*.5)*100
            if math.hypot(px-exit_x, py-exit_y) < 300:
                continue  # The real stair landing must remain an unguarded passage.
            if (abs(px-stair.stair_offset_cm.x) < flight_half_x+100 and
                    abs(py-stair.stair_offset_cm.y) < flight_half_y+100):
                continue  # A rail module touching the stair flight is omitted whole.
            if (x+dx, y+dy) not in cells:
                hit = trace((px-dx*12, py-dy*12, 510), (px+dx*12, py+dy*12, 510))
                assert hit[0], ('Missing protective rail on a floor/void boundary', label, (x, y), (dx, dy))
                edge_count += 1
            elif dx == 1 and x % 4 == 0 and y % 4 == 0:
                hit = trace((px-12, py, 510), (px+12, py, 510))
                assert not hit[0], ('Spurious rail through adjacent occupied tiles', label, (x, y))
                seam_count += 1
    assert edge_count and seam_count
    service_count = 0
    for actor in generated:
        if not actor.actor_has_tag('WarehouseServiceCeiling:1'):
            continue
        center, extent = actor.get_actor_bounds(False)
        center -= origin
        left, right = center.x-extent.x, center.x+extent.x
        bottom, top = center.y-extent.y, center.y+extent.y
        for x in range(math.floor((left+.1)/100), math.floor((right-.1)/100)+1):
            for y in range(math.floor((bottom+.1)/100), math.floor((top-.1)/100)+1):
                assert (x, y) in cells, ('Service attached over deleted tile', label, actor.get_name(), (x, y))
        assert unreal.WarehouseDamageSystem.is_physical_contact(actor.static_mesh_component)
        assert not actor.is_actor_tick_enabled()
        service_count += 1
    assert service_count, ('No actual deck-mounted services verified', label)
    assert physical(parcel) == stock_snapshot, 'Building edit changed retained ground cargo physics'
    row = dict(shape=label, tiles=len(cells), generated=len(generated), deck_services=service_count,
               collision_samples=tested, guarded_edges=edge_count, open_seams=seam_count,
               revision=building.get_editor_property('revision'), apply_wall_seconds=round(elapsed, 4))
    report['shapes'].append(row)
    check('physical_topology_'+label, **row)


def apply_shape(label):
    cells = SHAPES[label]
    before = counts()
    revision = building.get_editor_property('revision')
    began = time.monotonic()
    assert building.apply_settings(settings_for(cells)), (label, building.get_editor_property('status'))
    elapsed = time.monotonic()-began
    assert building.get_editor_property('revision') == revision+1
    balanced(before)
    topology(label, cells, elapsed)


def point(x, y):
    return building.get_actor_location()+unreal.Vector(x*100+50, y*100+50, 403)


def stroke(mode, first, last=None):
    construction.set_brush_mode(mode)
    assert construction.begin_gesture_at(point(*first), unreal.Vector2D(0, 0)), construction.get_editor_property('status')
    if last is not None:
        construction.move_gesture_at(point(*last))
    assert construction.end_gesture(True), construction.get_editor_property('status')


def begin_draft_checks():
    global draft_checkpoint
    pc.set_observer_floor(1)
    construction.set_target(unreal.WarehouseConstructionTarget.FLOOR2)
    before_geometry, before_count = geometry(), counts()
    revision = building.get_editor_property('revision')
    before_cells = cells_of(building.get_settings().floor2)
    stroke(unreal.WarehouseConstructionBrush.REMOVE_TILES, (-19, 5), (-19, 8))
    assert construction.get_editor_property('has_pending_tiles')
    assert building.get_editor_property('revision') == revision and geometry() == before_geometry
    stroke(unreal.WarehouseConstructionBrush.ADD_TILES, (-14, -10))
    previous_preview_count = construction.get_preview_tile_count()
    assert construction.begin_gesture_at(point(-14, -9), unreal.Vector2D(0, 0))
    construction.end_gesture(False)
    assert construction.get_preview_tile_count() == previous_preview_count
    assert construction.get_editor_property('has_pending_tiles'), 'Cancelling one stroke discarded earlier strokes'
    assert building.get_editor_property('revision') == revision and geometry() == before_geometry
    assert counts() == before_count, 'Painting a preview began a real building calculation'
    assert cells_of(building.get_settings().floor2) == before_cells
    assert construction.get_preview_tile_count() == len(before_cells)-3
    check('multiple_strokes_only_change_draft_not_scene_or_loading', revision=revision,
          preview_tiles=construction.get_preview_tile_count(), aborted_stroke_preserved_prior_draft=True)
    draft_checkpoint = (before_geometry, before_count, revision, before_cells)
    pc.frame_warehouse()


def capture_draft_ui():
    unreal.load_module('AutomationUtils')
    name = 'MezzanineTilesDraft_'+str(time.time_ns())
    incoming = Path(unreal.Paths.project_saved_dir())/'Automation/Incoming/BuildingTiles'/name
    unreal.AutomationUtilsBlueprintLibrary.take_gameplay_automation_screenshot(name, map_name_override='BuildingTiles')
    unreal.AutomationLibrary.take_automation_screenshot_of_ui(game, unreal.LatentActionInfo(), name,
                                                             unreal.AutomationScreenshotOptions())
    candidates = []
    for path in incoming.rglob('*.png'):
        data = path.read_bytes()
        if len(data) > 1000 and data[:8] == b'\x89PNG\r\n\x1a\n' and data[-8:-4] == b'IEND':
            candidates.append((path, data))
    assert len(candidates) == 1, ('No unique complete game-viewport UI capture', str(incoming))
    path, data = candidates[0]
    destination = output/(name+'.png')
    destination.write_bytes(data)
    report['screenshots'].append(dict(path=str(destination), width=int.from_bytes(data[16:20], 'big'),
                                     height=int.from_bytes(data[20:24], 'big'), source='Actual world game viewport Slate widget',
                                     pending_preview=True, requiresVisualReview=True,
                                     fixture='Temporary untextured geometry; not the default warehouse map'))


def complete_draft_checks():
    before_geometry, before_count, revision, before_cells = draft_checkpoint
    assert geometry() == before_geometry and building.get_editor_property('revision') == revision
    assert not construction.toggle_selected_floor(), 'Immediate floor toggle bypassed unapplied tile changes'
    began = time.monotonic()
    assert construction.apply_pending_tiles(), construction.get_editor_property('status')
    assert building.get_editor_property('revision') == revision+1
    assert not construction.get_editor_property('has_pending_tiles')
    balanced(before_count)
    expected = (before_cells-{(-19, y) for y in range(5, 9)}) | {(-14, -10)}
    topology('multi_stroke_apply', expected, time.monotonic()-began)
    before_geometry, revision, before_count = geometry(), building.get_editor_property('revision'), counts()
    stroke(unreal.WarehouseConstructionBrush.ADD_TILES, (-14, -9))
    construction.cancel_pending_tiles()
    assert not construction.get_editor_property('has_pending_tiles')
    assert building.get_editor_property('revision') == revision and geometry() == before_geometry and counts() == before_count
    stroke(unreal.WarehouseConstructionBrush.ADD_TILES, (-14, -9))
    pc.set_observer_floor(2)
    assert construction.get_editor_property('target') == unreal.WarehouseConstructionTarget.FLOOR3
    assert not construction.get_editor_property('has_pending_tiles')
    assert building.get_editor_property('revision') == revision and geometry() == before_geometry
    check('cancel_and_floor_view_switch_preserve_original_geometry_and_sync_target')


def reject(proposed, label):
    previous, revision, before = geometry(), building.get_editor_property('revision'), counts()
    assert not building.apply_settings(proposed), label
    assert geometry() == previous and building.get_editor_property('revision') == revision, label
    balanced(before)
    assert physical(parcel) == stock_snapshot
    check(label, reason=building.get_editor_property('status'))


def guards():
    proposed = building.get_settings()
    proposed.width_cm = 9700
    reject(proposed, 'maximum_width_guard')
    proposed = building.get_settings()
    proposed.depth_cm = 8100
    reject(proposed, 'maximum_depth_guard')
    proposed = building.get_settings()
    floor = proposed.floor2
    floor.tiles = list(floor.tiles)+[unreal.IntPoint(x=48, y=0)]
    proposed.floor2 = floor
    reject(proposed, 'out_of_bounds_mask_guard')
    proposed = building.get_settings()
    floor = proposed.floor2
    floor.tiles = []
    proposed.floor2 = floor
    reject(proposed, 'enabled_empty_mask_guard')
    proposed = building.get_settings()
    floor = proposed.floor4
    floor.enabled = True
    proposed.floor4 = floor
    proposed.roof_height_cm = 1600
    reject(proposed, 'floor4_requires_floor3')
    assert building.get_maximum_floor_count() == 4
    proposed = building.get_settings()
    for field in ('floor2', 'floor3', 'floor4'):
        floor = proposed.get_editor_property(field)
        floor.enabled = True
        set_mask(floor, SHAPES['central_hole'])
        proposed.set_editor_property(field, floor)
    proposed.roof_height_cm = 1600
    before = counts()
    assert building.apply_settings(proposed), building.get_editor_property('status')
    balanced(before)
    for floor in (1, 2, 3):
        hit = trace((-1650, 1050, floor*400+5), (-1650, 1050, floor*400-5))
        gap = trace((-350, 50, floor*400+5), (-350, 50, floor*400-5))
        assert hit[0] and not gap[0], ('Four-floor mask not reflected by collision', floor)
    check('all_four_floors_have_independent_real_slabs_and_voids', maximum_floors=4,
          generated=len(building.get_editor_property('generated_structure')))


def finish(error=None):
    global finished
    if finished:
        return
    finished = True
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    unchanged = hashlib.sha256(saved_map.read_bytes()).hexdigest() == original_hash
    if not unchanged:
        error = (error or '')+' Saved default map changed.'
    report.update(passed=not error, savedMapUnchanged=unchanged, wall_seconds=round(time.monotonic()-started, 3))
    if error:
        report['error'] = error
    (output/'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('BUILDING_TILES_PIE_'+('FAILED' if error else 'PASSED'), error or report['checks'])
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    if editor.get_game_world():
        level.editor_request_end_play()
    if not keep_open:
        unreal.SystemLibrary.quit_editor()


def tick_impl(delta_seconds):
    global phase, game, pc, building, construction, loading, parcel, stock_snapshot
    global capture_after, capture_frames
    assert time.monotonic()-started < 600, ('Tile test timeout', phase)
    if phase == 'start':
        fixture()
        level.editor_request_begin_play()
        phase = 'settle'
        return
    game = editor.get_game_world()
    if not game:
        return
    if phase == 'settle':
        if unreal.GameplayStatics.get_time_seconds(game) < 2:
            return
        pc = unreal.GameplayStatics.get_player_controller(game, 0)
        if not pc:
            return
        parcel = next(a for a in allof(unreal.WarehouseCargo) if a.get_actor_label() == 'TilesGroundCargo')
        pc.set_observer_floor(0)
        pc.set_layout_editing(True)
        pc.set_construction_editing(True)
        construction = pc.get_construction_editor()
        building = allof(unreal.WarehouseBuildingLayout)[0]
        instance = unreal.GameplayStatics.get_game_instance(game)
        loading = next(obj for obj in unreal.ObjectIterator(unreal.WarehouseLoadingSubsystem) if obj.get_outer() == instance)
        stock_snapshot = physical(parcel)
        assert parcel.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
        assert building.get_editor_property('revision') == 0 and not building.get_editor_property('generated_structure')
        check('opening_editor_keeps_source_geometry_unchanged')
        phase = 'L'
        return
    if phase in SHAPES:
        apply_shape(phase)
        if phase == 'L':
            assert not any(a.get_editor_property('cargo_id') == 'TilesUnsupportedCargo' for a in allof(unreal.WarehouseCargo))
            store = allof(unreal.WarehouseLayoutInventory)[0]
            record = next(row for row in store.get_editor_property('held_cargo') if row.cargo_id == 'TilesUnsupportedCargo')
            assert abs(record.mass_kg-23) < .01
            check('removed_floor_support_archives_cargo_without_losing_identity_or_mass')
            phase = 'U'
        elif phase == 'U':
            begin_draft_checks()
            capture_after, capture_frames, phase = time.monotonic()+.4, 0, 'draft_capture'
        else:
            phase = 'guards'
        return
    if phase == 'draft_capture':
        capture_frames += 1
        if rendered and (capture_frames < 6 or time.monotonic() < capture_after):
            return
        if rendered:
            capture_draft_ui()
        complete_draft_checks()
        phase = 'central_hole'
        return
    if phase == 'guards':
        guards()
        finish()


def tick(delta_seconds):
    global busy
    if finished or busy:
        return
    busy = True
    try:
        tick_impl(delta_seconds)
    except Exception:
        finish(traceback.format_exc())
    finally:
        busy = False


handle = unreal.register_slate_post_tick_callback(tick)
