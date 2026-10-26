"""Unsaved production-map building smoke test; no level or external actor saves.

Opening the tile editor must preserve every captured structural component.
A real building change must succeed while retaining stock/rack components.
Try both dimensions, then width alone, then roof height alone. Every rejected
attempt must retain all static geometry and stock, and is reported with its
exact native reason. Rejecting every candidate is a failure. Fourth-floor
activation is attempted separately and reported explicitly.
"""
import hashlib
import json
from pathlib import Path
import time
import traceback

import unreal


level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
content = Path(unreal.Paths.project_content_dir())
output = Path(unreal.Paths.project_saved_dir()) / 'Testing/Construction'
output.mkdir(parents=True, exist_ok=True)
keep_open = '-keeptesteditoropen' in unreal.SystemLibrary.get_command_line().lower()
report = dict(passed=False, checks=[], savedMapUnchanged=False, osInputTested=False,
              scope='Actual unsaved FirstPerson map, production PC/clock, component-level structure and stock preservation, native building expansion')
started = time.monotonic()
phase = 'start'
handle = None
game = pc = layout = construction = building = None
original_vehicle_flags = []
editor_baseline = {}
saved_hashes = {}
finish_result = None
finished = False


def check(name, **data):
    report['checks'].append(dict(name=name, **data))
    print('BUILDING_SAVED_MAP_CHECK', name, json.dumps(data, ensure_ascii=False))


def hashes():
    paths = [content / 'FirstPerson/Lvl_FirstPerson.umap']
    for directory in ('__ExternalActors__/FirstPerson/Lvl_FirstPerson', '__ExternalObjects__/FirstPerson/Lvl_FirstPerson'):
        root = content / directory
        if root.exists():
            paths.extend(root.rglob('*.uasset'))
    return {str(path.relative_to(content)): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths if path.exists()}


def values(vector, fields):
    return tuple(round(getattr(vector, key), 5) for key in fields)


def transform_values(transform):
    return values(transform.translation, 'xyz') + values(transform.rotation, 'xyzw') + values(transform.scale3d, 'xyz')


def snapshot(actor, physics=True):
    parts = []
    for part in actor.get_components_by_class(unreal.SceneComponent):
        row = [part.get_name(), transform_values(part.get_world_transform()), part.get_editor_property('mobility'), part.get_attach_parent()]
        if isinstance(part, unreal.PrimitiveComponent):
            row.extend([part.get_collision_enabled(), part.get_collision_object_type(), part.is_visible(), part.is_simulating_physics()])
            if physics and part.is_simulating_physics():
                row.extend([round(part.get_mass(), 4), part.is_gravity_enabled(), values(part.get_physics_linear_velocity(), 'xyz'),
                            values(part.get_physics_angular_velocity_in_degrees(), 'xyz')])
        parts.append(tuple(row))
    return transform_values(actor.get_actor_transform()), actor.get_editor_property('hidden'), actor.get_actor_enable_collision(), tuple(parts)


def allof(cls):
    return unreal.GameplayStatics.get_all_actors_of_class(game, cls)


def assert_preserved(actor, before, context):
    after = snapshot(actor)
    if after != before:
        keys = ['actor_transform', 'hidden', 'actor_collision', 'components']
        report['preservationFailure'] = dict(actor=actor.get_actor_label(), context=context,
            changed_fields=[key for key, old, new in zip(keys, before, after) if old != new],
            before=repr(before), after=repr(after))
        raise AssertionError((context, actor.get_actor_label(), report['preservationFailure']['changed_fields']))


def shell_diagnostic(live):
    """Read current physical meshes and query candidate poses without moving them."""
    named = {actor.get_actor_label(): actor for actor in live}
    labels = ['Floor', 'WH_InboundFloor', 'WH_Wall_N', 'WH_Wall_E', 'WH_Wall_W',
              'WH_Wall_S_Left', 'WH_Wall_S_Right', 'WH_LoadingDoor_Header',
              'WH_ManagementComputer', 'EXT_SidePath_-1', 'WH_Utility_0', 'WH_Utility_1', 'WH_Utility_2', 'WH_Utility_3']
    rows = []
    for label in labels:
        actor = named.get(label)
        if not actor:
            continue
        parts = []
        for part in actor.get_components_by_class(unreal.PrimitiveComponent):
            if not unreal.WarehouseDamageSystem.is_physical_contact(part):
                continue
            centre, extent, _ = unreal.SystemLibrary.get_component_bounds(part)
            parts.append(dict(name=part.get_name(), bounds_min=values(centre-extent, 'xyz'),
                              bounds_max=values(centre+extent, 'xyz'),
                              pose=transform_values(part.get_world_transform())))
        rows.append(dict(name=label, parts=parts))
    report['sourceShellPhysicalGeometry'] = rows
    barrel = named.get('WH_Utility_0')
    template = named.get('WH_Column_-1850_-1500')
    if barrel and template:
        native = []
        part = template.get_component_by_class(unreal.StaticMeshComponent)
        centre, extent, _ = unreal.SystemLibrary.get_component_bounds(part)
        for delta_y in (0, -25, -50, 50, -100, 100):
            pose = part.get_world_transform()
            pose.scale3d *= unreal.Vector(22/(extent.x*2), 22/(extent.y*2), 400/(extent.z*2))
            pose.translation += unreal.Vector(-1775, 1775+delta_y, 200)-centre
            hits = unreal.SystemLibrary.component_overlap_components(part, pose,
                [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2],
                unreal.PrimitiveComponent, [template]) or []
            native.append(dict(candidate_y_cm=1775+delta_y,
                               barrel_parts=[hit.get_name() for hit in hits if hit.get_owner() == barrel]))
        report['mezzanineColumnBarrelNativeQueries'] = native
    computer = named.get('WH_ManagementComputer')
    if computer:
        native = []
        for label in ('WH_Wall_S_Left', 'WH_Wall_S_Right', 'WH_LoadingDoor_Header'):
            wall = named.get(label)
            if not wall:
                continue
            for part in wall.get_components_by_class(unreal.StaticMeshComponent):
                if not unreal.WarehouseDamageSystem.is_physical_contact(part):
                    continue
                for stage, delta_y in [('authored_source', 0), ('previous_outward_wall_offset', -37.5)]:
                    pose = part.get_world_transform()
                    pose.translation += unreal.Vector(0, delta_y, 0)
                    hits = unreal.SystemLibrary.component_overlap_components(part, pose,
                        [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2],
                        unreal.PrimitiveComponent, [wall]) or []
                    native.append(dict(wall=label, stage=stage, delta_y_cm=delta_y,
                                       computer_parts=[hit.get_name() for hit in hits if hit.get_owner() == computer]))
        report['sourceWallComputerNativeQueries'] = native


def begin_finish(passed, detail):
    global phase, finish_result
    if finish_result is not None:
        return
    finish_result = passed, detail
    try:
        if construction:
            construction.end_gesture(False)
            construction.set_active(False)
        if layout and layout.get_editor_property('editing'):
            layout.set_editing(False, 0)
        if editor.get_game_world():
            level.editor_request_end_play()
        for vehicle, auto_start in original_vehicle_flags:
            vehicle.set_editor_property('auto_start', auto_start)
        phase = 'cleanup'
    except Exception:
        finish_result = False, detail + '\nCleanup start failed:\n' + traceback.format_exc()
        finalize()


def finalize():
    global finished
    if finished:
        return
    finished = True
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    passed, detail = finish_result
    try:
        actual = hashes()
        report['savedMapUnchanged'] = actual == saved_hashes
        if actual != saved_hashes:
            passed = False
            detail += '\nSaved map/external actor data changed during an unsaved test.'
        for actor, before in editor_baseline.items():
            if snapshot(actor, False) != before:
                raise AssertionError('Editor-world source did not restore: ' + actor.get_actor_label())
        report['editorSourceComponentsRestored'] = True
        for vehicle, auto_start in original_vehicle_flags:
            assert vehicle.get_editor_property('auto_start') == auto_start
    except Exception:
        passed = False
        detail += '\nFinal preservation check failed:\n' + traceback.format_exc()
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    report.update(passed=passed, detail=detail, wall_seconds=round(time.monotonic()-started, 3))
    (output / 'SavedMapReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('BUILDING_SAVED_MAP_PIE_PASSED' if passed else 'BUILDING_SAVED_MAP_PIE_FAILED', detail)
    if not keep_open:
        unreal.SystemLibrary.quit_editor()


def _tick_impl(delta_seconds):
    global game, pc, layout, construction, building, phase, editor_baseline, saved_hashes, finish_result
    try:
        assert time.monotonic()-started < 300, ('Saved-map building test timed out', phase)
        if phase == 'cleanup':
            if not editor.get_game_world():
                finalize()
            return
        if phase == 'start':
            saved_hashes = hashes()
            assert saved_hashes and level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
            saved_actors = actors.get_all_level_actors()
            report['editorGeneralLifts'] = [dict(name=actor.get_actor_label(),
                gate_references=len(actor.get_editor_property('gates')),
                native_gate_components=sum(part.get_name().startswith('Gate_')
                    for part in actor.get_components_by_class(unreal.StaticMeshComponent)))
                for actor in saved_actors if isinstance(actor, unreal.WarehouseElevator)]
            for actor in saved_actors:
                if isinstance(actor, unreal.WarehouseForklift):
                    original_vehicle_flags.append((actor, actor.get_editor_property('auto_start')))
                    actor.set_editor_property('auto_start', False)
            editor_baseline = {actor: snapshot(actor, False) for actor in saved_actors if isinstance(actor, unreal.StaticMeshActor)}
            check('loaded_actual_map_without_saving', static_actors=len(editor_baseline),
                  vehicles_temporarily_not_auto_started=len(original_vehicle_flags), files_hashed=len(saved_hashes))
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if phase == 'settle':
            if now < .8:
                return
            # A freshly opened freight gate is allowed to finish its native
            # startup stroke before taking a structural transaction snapshot.
            busy_lifts = [lift for lift in allof(unreal.WarehousePalletLift) if not lift.is_idle_for_layout()]
            if busy_lifts and now < 8:
                return
            assert not busy_lifts, [(lift.get_actor_label(), lift.get_editor_property('status')) for lift in busy_lifts]
            native_lifts = allof(unreal.WarehouseElevator)
            assert native_lifts, 'Actual saved general elevator is missing'
            gate_rows = []
            for lift in native_lifts:
                refs = list(lift.get_editor_property('gates'))
                parts = [part for part in lift.get_components_by_class(unreal.StaticMeshComponent)
                         if part.get_name().startswith('Gate_')]
                assert len(refs) == len(parts) == 8 and set(refs) == set(parts), ('Saved lift gate references were not repaired', lift.get_actor_label())
                gate_rows.append(dict(name=lift.get_actor_label(), gate_references=len(refs), native_gate_components=len(parts)))
            check('actual_saved_general_lifts_expose_all_eight_native_gate_references_in_pie', lifts=gate_rows)
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            assert pc and hasattr(pc, 'get_construction_editor')
            assert len(allof(unreal.WarehouseSimulationClock)) == 1, 'Production simulation clock is missing or duplicated'
            pc.set_observer_floor(0)
            pc.set_layout_editing(True)
            layout = allof(unreal.WarehouseLayoutEditor)[0]
            assert layout.get_editor_property('editing') and unreal.GameplayStatics.is_game_paused(game)
            before_all = {actor: snapshot(actor) for actor in allof(unreal.Actor)}
            pc.set_construction_editing(True)
            construction = pc.get_construction_editor()
            building = allof(unreal.WarehouseBuildingLayout)[0]
            originals = list(building.get_editor_property('replaced_structure'))
            assert originals, 'Actual map architecture was not captured'
            assert not any(actor.actor_has_tag('LayoutType:Rack') for actor in originals), 'A rack was classified as architecture'
            assert building.get_editor_property('revision') == 0 and not building.get_editor_property('generated_structure')
            for actor in originals:
                assert snapshot(actor) == before_all[actor], ('Opening editor changed a source component', actor.get_actor_label())
            stock = [actor for actor in before_all if isinstance(actor, (unreal.WarehouseCargo, unreal.WarehousePallet)) or actor.actor_has_tag('LayoutType:Rack')]
            assert stock, 'Actual production rack/stock set is empty'
            stock_before = {actor: snapshot(actor) for actor in stock}
            original = building.get_settings()
            shell_diagnostic(before_all)
            report['capturedSettings'] = dict(width_cm=original.width_cm, depth_cm=original.depth_cm, roof_height_cm=original.roof_height_cm)
            check('opening_actual_tile_editor_changes_no_structural_component', structure_actors=len(originals), stock_and_rack_actors=len(stock), settle_sim_seconds=now)
            geometry_before = {actor: previous for actor, previous in before_all.items()
                               if isinstance(actor, unreal.StaticMeshActor)}
            attempts, accepted_change = [], None
            for name, width_delta, depth_delta, roof_delta in [
                    ('both_dimensions', 200, 200, 100), ('width_only', 200, 0, 100), ('roof_only', 0, 0, 100)]:
                proposed = building.get_settings()
                proposed.width_cm += width_delta
                proposed.depth_cm += depth_delta
                proposed.roof_height_cm += roof_delta
                accepted = building.apply_settings(proposed)
                attempt = dict(name=name, width_delta_cm=width_delta, depth_delta_cm=depth_delta,
                               roof_delta_cm=roof_delta, accepted=accepted, status=building.get_editor_property('status'))
                attempts.append(attempt)
                report['expansionAttempts'] = attempts
                if accepted:
                    accepted_change = attempt
                    break
                assert building.get_editor_property('revision') == 0 and not building.get_editor_property('generated_structure')
                for actor, previous in geometry_before.items():
                    assert snapshot(actor) == previous, ('Rejected change modified existing static geometry', name, actor.get_actor_label())
                for actor, previous in stock_before.items():
                    assert snapshot(actor) == previous, ('Rejected change modified real cargo/rack physics', name, actor.get_actor_label())
                check('actual_map_change_rejected_atomically_preserving_all_static_geometry_and_stock', attempt=attempt)
            report['expansionAccepted'] = accepted_change is not None
            report['expansionStatus'] = building.get_editor_property('status')
            report['successfulExpansion'] = accepted_change
            report['footprintExpansionAccepted'] = bool(accepted_change and
                (accepted_change['width_delta_cm'] or accepted_change['depth_delta_cm']))
            assert accepted_change is not None, ('Every actual-map building change was rejected', attempts)
            assert building.get_editor_property('generated_structure') and building.get_editor_property('revision') == 1
            for actor, previous in stock_before.items():
                assert_preserved(actor, previous, 'Expansion changed real cargo/rack physics or pose')
            assert all(actor.get_editor_property('hidden') and not actor.get_actor_enable_collision() for actor in originals)
            check('actual_map_successful_native_building_change_preserves_real_stock_and_racks',
                  generated_actors=len(building.get_editor_property('generated_structure')), change=accepted_change)
            construction.set_target(unreal.WarehouseConstructionTarget.FLOOR4)
            floor4 = construction.toggle_selected_floor()
            report['optionalFourthFloor'] = dict(accepted=floor4, status=construction.get_editor_property('status'))
            if floor4:
                assert building.get_settings().floor4.enabled
                for actor, previous in stock_before.items():
                    assert_preserved(actor, previous, 'Fourth floor changed real cargo/rack physics')
                check('optional_actual_fourth_floor_added_without_stock_or_rack_changes')
            else:
                check('optional_actual_fourth_floor_safely_rejected', reason=construction.get_editor_property('status'))
            begin_finish(True, 'Actual-map tile-editor open preservation and successful native building change: ' +
                         accepted_change['name'] + '; rejected candidates and exact native reasons recorded; all real stock/rack component poses, collision and physics retained; fourth-floor attempt separately reported; PIE and source flags restored without saving')
    except Exception:
        if finish_result is None:
            begin_finish(False, traceback.format_exc())
        else:
            finish_result = False, finish_result[1] + '\nCleanup did not complete:\n' + traceback.format_exc()
            finalize()


_callback_busy = False


def tick(delta_seconds):
    # LoadLevel, spawning and geometry rebuilds may synchronously pump Slate.
    # Reentrant callbacks must not repeat a transaction or load another map.
    global _callback_busy
    if _callback_busy:
        return
    _callback_busy = True
    try:
        return _tick_impl(delta_seconds)
    finally:
        _callback_busy = False


handle = unreal.register_slate_post_tick_callback(tick)
