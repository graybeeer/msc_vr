"""Read-only saved-map vegetation, logistics links, ground contacts and colour capture.

Runs briefly in PIE, then pauses for a 1920x1080 exterior screenshot. No level,
asset, lighting or material is saved or replaced. Temporary editor changes are
restored; a persistent test host remains open with -KeepTestEditorOpen.
"""
import hashlib
import json
from pathlib import Path
import time
import traceback

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT/'Saved/Testing/ExteriorSimplification'
OUT.mkdir(parents=True, exist_ok=True)
LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
EYE, TARGET, FOV = (6000, -9000, 4200), (0, -2500, 250), 65
PLANT_PATHS = ('/Game/Warehouse/Collision/SM_EuropeanBeech_',
               '/Game/Warehouse/Collision/GV_Vol7_Shrub_',
               '/Game/EuropeanBeech/', '/Game/GV_FreeShrubsPack/')
MINIMUM_COUNTS = {'WarehouseForklift': 2, 'WarehouseComputer': 1,
                  'WarehouseCargo': 20, 'WarehousePallet': 10,
                  'WarehouseElevator': 1, 'WarehousePalletLift': 1,
                  'WarehouseChargingStation': 2, 'WarehouseShippingSystem': 1}
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
started = phase_at = time.monotonic()
phase = 'start'
busy = finished = False
handle = None
game = pc = camera = editor_camera = None
original_flags = []
source_hashes = {}
editor_counts = {}
capture_frames = 0
capture_path = OUT/('SimpleExterior_'+str(time.time_ns())+'.png')
finish_error = None
report = dict(passed=False, checks=[], screenshots=[], level=LEVEL,
    scope='Actual saved main map; vegetation removal, logistics object/link smoke checks and native exterior floor contacts; no transport-order regression or OS input',
    camera=dict(eye=EYE, target=TARGET, field_of_view=FOV),
    colour_source='Original production materials, lighting and exposure; no colour filter or material override')


def enter(name):
    global phase, phase_at
    phase, phase_at = name, time.monotonic()


def check(name, **values):
    report['checks'].append(dict(name=name, **values))
    print('SIMPLE_EXTERIOR_CHECK', name, json.dumps(values, ensure_ascii=False))


def hashes():
    files = [ROOT/'Content/FirstPerson/Lvl_FirstPerson.umap']
    for folder in ('__ExternalActors__', '__ExternalObjects__'):
        files.extend((ROOT/'Content'/folder/'FirstPerson/Lvl_FirstPerson').rglob('*.uasset'))
    return {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in files if path.is_file()}


def plant_names(objects):
    found = []
    for actor in objects:
        if actor.get_actor_label().startswith(('EXT_Beech_', 'EXT_Shrub_')) or any(
                part.static_mesh and part.static_mesh.get_path_name().startswith(PLANT_PATHS)
                for part in actor.get_components_by_class(unreal.StaticMeshComponent)):
            found.append(actor.get_actor_label())
    return found


def counts(objects):
    return {name: sum(isinstance(actor, getattr(unreal, name)) for actor in objects)
            for name in MINIMUM_COUNTS}


def png_ready(path):
    if not path.is_file():
        return None
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if len(data) < 1000 or data[:8] != b'\x89PNG\r\n\x1a\n' or data[-8:-4] != b'IEND':
        return None
    return dict(path=str(path), width=int.from_bytes(data[16:20], 'big'),
                height=int.from_bytes(data[20:24], 'big'), bytes=len(data))


def check_ground():
    rows = []
    samples = [('EXT_Yard', 1800, -4700, -120),
               ('EXT_AccessRoad', -1500, -7000, -120)]
    samples += [('EXT_DockPlatform', x, -2600, 0) for x in (-100, 350, 800)]
    for label, x, y, expected_z in samples:
        result = unreal.SystemLibrary.line_trace_single(game, unreal.Vector(x, y, 100),
            unreal.Vector(x, y, -250), unreal.TraceTypeQuery.ECC_VISIBILITY,
            True, [], unreal.DrawDebugTrace.NONE, True)
        hit = result if isinstance(result, unreal.HitResult) else next(
            (value for value in (result or []) if isinstance(value, unreal.HitResult)), None)
        values = hit.to_tuple() if hit is not None else (False,)
        assert values[0], ('Missing ground contact', label, x, y)
        location, owner = values[5], values[9]
        assert owner and owner.get_actor_label() == label, (label, owner.get_actor_label() if owner else None)
        assert abs(location.z-expected_z) < .2, (label, location)
        component = owner.get_component_by_class(unreal.StaticMeshComponent)
        assert component and unreal.WarehouseDamageSystem.is_physical_contact(component), label
        rows.append(dict(actor=label, x=x, y=y, surface_z_cm=round(location.z, 3)))
    check('yard_road_and_all_three_dock_positions_have_real_collision_support', samples=rows)


def check_logistics(objects):
    named = {actor.get_actor_label(): actor for actor in objects}
    trucks = [named.get(f'EXT_ParkedTruck_{i:02}') for i in (1, 2, 3)]
    assert all(trucks) and sum(label.startswith('EXT_ParkedTruck_') for label in named) == 3
    live_counts = counts(objects)
    assert all(live_counts[name] >= editor_counts[name] >= minimum
               for name, minimum in MINIMUM_COUNTS.items()), (live_counts, editor_counts)
    racks = sum(actor.actor_has_tag('LayoutType:Rack') for actor in objects)
    assert racks > 0
    computers = [actor for actor in objects if isinstance(actor, unreal.WarehouseComputer)]
    for computer in computers:
        computer.refresh_state()
        assert len(computer.get_editor_property('fleet')) >= 2
        assert computer.get_editor_property('inventory'), 'Computer inventory did not populate'
    shipping = next(actor for actor in objects if isinstance(actor, unreal.WarehouseShippingSystem))
    shipping_trucks = list(shipping.get_editor_property('trucks'))
    zones = list(shipping.get_editor_property('zones'))
    assert len(shipping_trucks) == 3 and all(unreal.SystemLibrary.is_valid(t) for t in shipping_trucks)
    assert len(zones) >= 2 and {z.get_editor_property('inbound') for z in zones} == {False, True}
    assert all(z.get_editor_property('anchor') and z.get_editor_property('slots') for z in zones)
    assert all(t.get_editor_property('dock_anchor') and t.get_editor_property('load_slots')
               and t.get_editor_property('truck_parts') for t in shipping_trucks)
    check('three_docks_and_existing_interior_equipment_and_shipping_links_remain',
          runtime_counts=live_counts, racks=racks, docked_trucks=3, shipping_zones=len(zones),
          computer_inventory=[len(c.get_editor_property('inventory')) for c in computers])


def begin_finish(error=None):
    global finish_error
    finish_error = error
    if editor.get_game_world():
        level.editor_request_end_play()
    enter('cleanup')


def finalize():
    global finished, finish_error
    if finished:
        return
    finished = True
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    try:
        for actor, original in original_flags:
            assert unreal.SystemLibrary.is_valid(actor)
            actor.set_editor_property('auto_start', original)
        if editor_camera and unreal.SystemLibrary.is_valid(editor_camera):
            assert actors.destroy_actor(editor_camera)
        assert all(a.get_editor_property('auto_start') == value for a, value in original_flags)
        report['editor_temporary_changes_restored'] = True
        report['saved_map_and_external_actors_unchanged'] = hashes() == source_hashes
        assert report['saved_map_and_external_actors_unchanged'], 'Saved map data changed during test'
    except Exception:
        finish_error = (finish_error or '')+'\nCleanup failed:\n'+traceback.format_exc()
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    report.update(passed=not finish_error, wall_seconds=round(time.monotonic()-started, 3))
    if finish_error:
        report['error'] = finish_error
    (OUT/'PIEReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('SIMPLE_EXTERIOR_PIE_'+('FAILED' if finish_error else 'PASSED'), finish_error or report['screenshots'])
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def tick_impl(_dt):
    global game, pc, camera, editor_camera, source_hashes, editor_counts, capture_frames
    assert time.monotonic()-started < 180, ('Exterior test timed out', phase)
    if phase == 'cleanup':
        if not editor.get_game_world():
            finalize()
        return
    if phase == 'start':
        assert not editor.get_game_world(), 'Stop previous PIE before running this test'
        assert '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower(), 'Rendered host required for PNG'
        source_hashes = hashes()
        assert source_hashes and level.load_level(LEVEL)
        saved = list(actors.get_all_level_actors())
        assert not plant_names(saved), ('Decorative vegetation remains in saved map', plant_names(saved))
        editor_counts = counts(saved)
        check('saved_map_has_no_decorative_trees_or_shrubs', vegetation=0, editor_counts=editor_counts)
        for actor in saved:
            if isinstance(actor, unreal.WarehouseForklift):
                original_flags.append((actor, actor.get_editor_property('auto_start')))
                actor.set_editor_property('auto_start', False)
        eye = unreal.Vector(*EYE)
        editor_camera = actors.spawn_actor_from_class(unreal.CameraActor, eye,
            unreal.MathLibrary.find_look_at_rotation(eye, unreal.Vector(*TARGET)))
        assert editor_camera
        editor_camera.set_actor_label('TEMP_SimpleExteriorPreview')
        editor_camera.camera_component.set_field_of_view(FOV)
        level.editor_request_begin_play()
        enter('settle')
        return
    game = editor.get_game_world()
    if not game:
        return
    if phase == 'settle':
        if unreal.GameplayStatics.get_time_seconds(game) < 1.5:
            return
        objects = list(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor))
        assert not plant_names(objects)
        check_logistics(objects)
        check_ground()
        pc = unreal.GameplayStatics.get_player_controller(game, 0)
        camera = next(a for a in objects if a.get_actor_label() == 'TEMP_SimpleExteriorPreview')
        assert pc
        pc.set_observer_floor(-1)
        unreal.GameplayStatics.set_game_paused(game, True)
        assert unreal.GameplayStatics.is_game_paused(game)
        pc.set_view_target_with_blend(camera, 0)
        check('runtime_vegetation_absent_and_simulation_paused_for_original_colour_capture',
              vegetation=0, settle_sim_seconds=unreal.GameplayStatics.get_time_seconds(game))
        enter('capture_settle')
    elif phase == 'capture_settle':
        capture_frames += 1
        if time.monotonic()-phase_at < 2 or capture_frames < 8:
            return
        unreal.SystemLibrary.execute_console_command(game,
            'HighResShot 1920x1080 filename='+capture_path.as_posix(), pc)
        enter('capture_write')
    elif phase == 'capture_write':
        assert time.monotonic()-phase_at < 45, 'Exterior screenshot not written'
        info = png_ready(capture_path)
        if not info:
            return
        assert info['width'] >= 1600 and info['height'] >= 900, info
        report['screenshots'].append(info)
        check('high_resolution_exterior_png_written', **info)
        begin_finish()


def tick(dt):
    global busy, finish_error
    if busy or finished:
        return
    busy = True
    try:
        tick_impl(dt)
    except Exception:
        error = traceback.format_exc()
        if phase == 'cleanup':
            finish_error = (finish_error or '')+'\n'+error
            finalize()
        else:
            begin_finish(error)
    finally:
        busy = False


handle = unreal.register_slate_post_tick_callback(tick)
