"""Unsaved Entry PIE: tile gestures, atomic building edits and preserved stock.

Uses the actual player controller/construction editor. After PIE starts the
fixture's cargo, rack and player are never warped, attached or force-settled.
Rendered runs also project real resize handles through the public pointer API;
NullRHI explicitly skips screen projection. This is not an OS-input test.
"""
import hashlib
import json
from pathlib import Path
import time
import traceback

import unreal


command = unreal.SystemLibrary.get_command_line().lower()
keep_open = '-keeptesteditoropen' in command
rendered = '-nullrhi' not in command
with_lifts = globals().get('BUILDING_SHAFT_LAYOUT_CHECKS', False) or '-buildingshaftlayoutchecks' in command
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
output = Path(unreal.Paths.project_saved_dir()) / 'Testing/Construction'
output.mkdir(parents=True, exist_ok=True)
saved_map = Path(unreal.Paths.project_content_dir()) / 'FirstPerson/Lvl_FirstPerson.umap'
map_digest = hashlib.sha256(saved_map.read_bytes()).hexdigest() if saved_map.exists() else None
report = dict(passed=False, rendered=rendered, osInputTested=False, checks=[], screenshots=[],
              excluded=[] if rendered else ['Camera-projected pointer gestures and rendered Slate'],
              scope='Unsaved Entry fixture, native production controller, geometry transactions and independent Chaos stock')
started = time.monotonic()
phase = 'start'
phase_at = started
handle = None
game = pc = layout = construction = building = None
live = {}
source_snapshot = {}
stock_snapshot = {}
completed = False
capture_paths = []


def check(name, **data):
    report['checks'].append(dict(name=name, **data))
    print('CONSTRUCTION_EDITOR_CHECK', name, json.dumps(data, ensure_ascii=False))


def screenshot_ready(path):
    if not path.is_file():
        return None
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if len(data) < 1000 or data[:8] != b'\x89PNG\r\n\x1a\n' or data[-8:-4] != b'IEND':
        return None
    return dict(path=str(path), bytes=len(data), width=int.from_bytes(data[16:20], 'big'),
                height=int.from_bytes(data[20:24], 'big'))


def capture_game_viewport_ui(path):
    # `Shot showui` captures the enclosing editor window in offscreen PIE.
    # Epic's UI automation API instead takes the actual world's SViewport.
    # Its synchronous broadcast can use AutomationUtils' existing PNG writer;
    # never accept a later queued scene screenshot as evidence of Slate UI.
    unreal.load_module('AutomationUtils')
    name = path.stem
    incoming = Path(unreal.Paths.project_saved_dir()) / 'Automation/Incoming/ConstructionUI' / name
    latent = unreal.LatentActionInfo()
    unreal.AutomationUtilsBlueprintLibrary.take_gameplay_automation_screenshot(
        name, map_name_override='ConstructionUI')
    assert not list(incoming.rglob('*.png')), 'PNG writer ran before the explicit game-viewport UI capture'
    unreal.AutomationLibrary.take_automation_screenshot_of_ui(
        game, latent, name, unreal.AutomationScreenshotOptions())
    images = [candidate for candidate in incoming.rglob('*.png') if screenshot_ready(candidate)]
    assert len(images) == 1, 'The synchronous game SViewport UI capture produced no unique complete PNG'
    path.write_bytes(images[0].read_bytes())
    report['uiCapture'] = dict(source='World.GetGameViewport.GetGameViewportWidget / Slate TakeScreenshot',
                               api='AutomationLibrary.take_automation_screenshot_of_ui',
                               writer='AutomationUtilsBlueprintLibrary.take_gameplay_automation_screenshot',
                               original=str(images[0]), visualReviewRequired=True, osInputTested=False)


def allof(cls):
    return unreal.GameplayStatics.get_all_actors_of_class(game, cls)


def numeric_pose(actor):
    return tuple(round(getattr(vector, axis), 5)
                 for vector, axes in ((actor.get_actor_location(), ('x', 'y', 'z')),
                                      (actor.get_actor_rotation(), ('pitch', 'yaw', 'roll')),
                                      (actor.get_actor_scale3d(), ('x', 'y', 'z')))
                 for axis in axes)


def physical_snapshot(actor):
    parts = []
    for component in actor.get_components_by_class(unreal.PrimitiveComponent):
        parts.append((component.get_name(), component.get_collision_enabled(), component.get_collision_object_type(),
                      component.get_editor_property('mobility'), component.is_simulating_physics(),
                      component.is_gravity_enabled(), round(component.get_mass(), 4) if component.is_simulating_physics() else None,
                      component.get_attach_parent()))
    return numeric_pose(actor), actor.get_actor_enable_collision(), actor.get_editor_property('hidden'), tuple(parts)


def geometry_snapshot():
    return tuple((actor.get_path_name(), physical_snapshot(actor))
                 for actor in building.get_editor_property('generated_structure'))


def assert_floor_at(point, floor, ignored, message):
    hit = unreal.SystemLibrary.line_trace_single(game, unreal.Vector(point.x, point.y, floor*400+5),
        unreal.Vector(point.x, point.y, floor*400-5), unreal.TraceTypeQuery.ECC_VISIBILITY, False,
        ignored, unreal.DrawDebugTrace.NONE, True)
    values = hit.to_tuple()
    assert values[0] and abs(values[5].z-floor*400) < .1, (message, floor, values)


def assert_stock_unchanged():
    for name, previous in stock_snapshot.items():
        actor = live[name]
        assert physical_snapshot(actor) == previous, ('Building edit changed a preserved object', name)


def current_settings():
    return building.get_settings()


def reject(proposed, label, reason_fragment=None):
    revision = building.get_editor_property('revision')
    geometry = geometry_snapshot()
    assert not building.apply_settings(proposed), label
    assert building.get_editor_property('revision') == revision and geometry_snapshot() == geometry, ('Rejected edit mutated geometry', label)
    status = building.get_editor_property('status')
    assert status and any('\uac00' <= char <= '\ud7a3' for char in status), ('Missing Korean rejection reason', label, status)
    if reason_fragment:
        assert reason_fragment in status, (label, status)
    assert_stock_unchanged()
    check(label, reason=status)


def finish(passed, detail):
    global completed
    if completed:
        return
    completed = True
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    try:
        if construction:
            construction.end_gesture(False)
            construction.set_active(False)
        if layout and layout.get_editor_property('editing'):
            layout.set_editing(False, 0)
        if editor.get_game_world():
            level.editor_request_end_play()
        actual_digest = hashlib.sha256(saved_map.read_bytes()).hexdigest() if saved_map.exists() else None
        report['savedMapUnchanged'] = actual_digest == map_digest
        if actual_digest != map_digest:
            passed = False
            detail += '\nSaved default map file changed during an unsaved test.'
    except Exception:
        passed = False
        detail += '\nCleanup failed:\n' + traceback.format_exc()
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    report.update(passed=passed, detail=detail, wall_seconds=round(time.monotonic()-started, 3))
    (output / ('ShaftLayoutReport.json' if with_lifts else 'EditorReport.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    marker = 'BUILDING_SHAFT_LAYOUT' if with_lifts else 'CONSTRUCTION_EDITOR'
    print(marker + ('_PIE_PASSED' if passed else '_PIE_FAILED'), detail)
    if not keep_open:
        unreal.SystemLibrary.quit_editor()


def fixture():
    assert level.load_level('/Engine/Maps/Entry')
    for actor in list(actors.get_all_level_actors()):
        if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
            actors.destroy_actor(actor)
    world = editor.get_editor_world()
    settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
    mode = unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C')
    assert mode, 'The production FirstPerson GameMode is unavailable'
    settings.set_editor_property('default_game_mode', mode)
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    assert cube

    def spawn(cls, name, point):
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
        actor.set_actor_label(name)
        return actor

    def block(name, point, size, tags=()):
        actor = spawn(unreal.StaticMeshActor, name, point)
        actor.static_mesh_component.set_static_mesh(cube)
        actor.static_mesh_component.set_collision_profile_name('BlockAll')
        actor.set_actor_scale3d(unreal.Vector(*[value/100 for value in size]))
        actor.tags = list(tags)
        return actor

    structural = ['LayoutFixed', 'WarehouseMapStructure']
    def floor_parts(name, center, size, floor):
        pieces = [(center[0]-size[0]/2, center[1]-size[1]/2, center[0]+size[0]/2, center[1]+size[1]/2)]
        holes = [(859, 1424, 1141, 1776 if floor == 0 else 1800),
                 (-1140, 1360, -960, 1540)] if with_lifts else []
        if with_lifts and floor > 0:
            holes += [(x-9.05, y-9.05, x+9.05, y+9.05) for x in (868, 1132) for y in (1413, 1787)]
        for hole in holes:
            remaining = []
            for x0, y0, x1, y1 in pieces:
                a, b, c, d = max(x0, hole[0]), max(y0, hole[1]), min(x1, hole[2]), min(y1, hole[3])
                if a >= c or b >= d:
                    remaining.append((x0, y0, x1, y1))
                    continue
                remaining += [r for r in [(x0, y0, a, y1), (c, y0, x1, y1), (a, y0, c, b), (a, d, c, y1)]
                              if r[2]-r[0] > .01 and r[3]-r[1] > .01]
            pieces = remaining
        for index, (x0, y0, x1, y1) in enumerate(pieces):
            tags = structural + [f'WarehouseFloor{floor}', 'ElevatorLayoutFloor']
            if not floor:
                tags += ['ObserverArea']
            block(name if index == 0 else name + '_Cut_' + str(index), ((x0+x1)/2, (y0+y1)/2, floor*400-10), (x1-x0, y1-y0, 20), tags)
    floor_parts('Floor', (400, 0), (4800, 4000), 0)
    floor_parts('MZ_L2_Deck_Test', (0, 0), (3600, 3600), 1)
    floor_parts('MZ_L3_Deck_Test', (0, 0), (3600, 3600), 2)
    if with_lifts:
        general = spawn(unreal.WarehouseElevator, 'ConstructionGeneralLift', (1000, 1600, 0))
        general.set_actor_rotation(unreal.Rotator(yaw=90), False)
        general.tags = list(general.tags) + ['LayoutType:Elevator']
        freight = spawn(unreal.WarehousePalletLift, 'ConstructionFreightLift', (-1050, 1450, 0))
        freight.set_actor_rotation(unreal.Rotator(yaw=90), False)
        freight.tags = list(freight.tags) + ['LayoutType:Elevator']
        block('ConstructionLiftPit', (1000, 1600, -30), (282, 352, 20), ['LayoutFixed'])
    # Authored cube modules exercise every automatic decoration path. These
    # source templates are outside the fixture and are captured as architecture.
    templates = [('WH_Wall_N', (4800, 35, 1200)), ('WH_Roof', (4800, 4000, 30)),
                 ('WH_Column_01', (40, 40, 1200)), ('WH_RoofBeam_01', (1000, 20, 30)),
                 ('MZ_L2_InnerLeft_Bar55', (160, 5, 5)), ('WS_FirePipeStraight_2m_01', (200, 6, 6)),
                 ('WS_TrayStraight_2m_01', (200, 35, 12)), ('WS_LEDLinear_1200_01', (120, 16, 12))]
    for index, (name, size) in enumerate(templates):
        block(name, (400, 0, 1215) if name == 'WH_Roof' else (20000, index*5000, 2000), size,
              structural + (['ObserverRoof'] if name == 'WH_Roof' else []))
    for name, point, size in [
            ('WH_Wall_S_Left', (-1350, -1980, 600), (1300, 35, 1200)),
            ('WH_Wall_S_Right', (1750, -1980, 600), (2100, 35, 1200)),
            ('WH_LoadingDoor_Header', (0, -1980, 845), (1400, 35, 710))]:
        block(name, point, size, structural)
    # The outside desk clears the authored wall by 32.5cm; moving that wall
    # outward by the former fixed offset would penetrate its tabletop by 5cm.
    computer = spawn(unreal.WarehouseComputer, 'ConstructionWallMountedComputer', (1050, -2065, 0))
    computer.set_actor_rotation(unreal.Rotator(yaw=90), False)
    block('EXT_Canopy', (0, -2200, 445), (200, 500, 28), ['LayoutFixed', 'ObserverRoof'])
    block('EXT_DockPlatform', (0, -2200, -65), (400, 440, 130), ['LayoutFixed'])
    block('ConstructionExteriorOverhang', (220, -2200, 445), (100, 500, 28), ['LayoutFixed', 'WarehouseExterior'])
    block('ConstructionExteriorPier', (2920, 1400, 100), (40, 40, 200), ['LayoutFixed', 'WarehouseExterior'])
    block('ConstructionMountedGuard', (190, -1985, -70), (40, 20, 80), ['LayoutFixed', 'WarehouseExterior'])
    block('ConstructionDockMoveBlocker', (0, -2680, -70), (40, 40, 80), ['LayoutFixed', 'WarehouseExterior'])
    sloped = block('ConstructionSlopedExteriorPlate', (250, -2250, 10), (200, 50, 4), ['LayoutFixed', 'WarehouseExterior'])
    sloped.set_actor_rotation(unreal.Rotator(pitch=-10), False)
    block('ConstructionFixtureRack', (1450, -1300, 100), (200, 80, 200), ['LayoutType:Rack', 'WarehouseFloor0'])
    block('WH_LightRack_1_H', (1900, 0, 50), (120, 80, 100), ['LayoutType:Rack', 'WarehouseFloor0'])
    for name, point, size, mass in [('ConstructionEdgeCargo', (2740, 1500, 20), (40, 30, 40), 12),
                                    ('ConstructionUpperCargo', (-1200, 1200, 420), (40, 30, 40), 23),
                                    ('WH_LightStock_1_0', (1900, 0, 120), (40, 30, 40), 12)]:
        parcel = spawn(unreal.WarehouseCargo, name, point)
        parcel.set_cargo_mesh(cube)
        parcel.set_actor_scale3d(unreal.Vector(*[value/100 for value in size]))
        parcel.set_gross_mass_kg(mass)
    barrel_mesh = unreal.load_asset('/Game/Warehouse/Physics/SM_Ind_War_Storage_Barrel_Plastic_Blue_01')
    assert barrel_mesh, 'The production barrel physics mesh is required for the resting-contact regression'
    barrel = spawn(unreal.StaticMeshActor, 'ConstructionRestingBarrel', (-1700, 1770, 0))
    barrel.static_mesh_component.set_static_mesh(barrel_mesh)
    barrel.static_mesh_component.set_collision_profile_name('BlockAll')
    centre, extent = barrel.get_actor_bounds(False)
    barrel.set_actor_location(barrel.get_actor_location()+unreal.Vector(0, 0, extent.z-centre.z), False, False)
    barrel.tags = ['WarehousePortable']
    strength = spawn(unreal.WarehouseDamageSystem, 'ConstructionStrength', (0, 0, 0))
    strength.set_editor_property('objects', [unreal.WarehouseStrength(name='ConstructionRestingBarrel',
        members=[barrel], physics_meshes=[barrel_mesh], mass_kg=55, rated_load_kg=150,
        impact_yield_j=1000, impact_failure_j=5000)])
    buried = block('ConstructionBuriedBody', (1900, 900, 6), (40, 40, 20), ['LayoutFixed'])
    buried.set_actor_enable_collision(False)
    spawn(unreal.PlayerStart, 'ConstructionSafePlayerStart', (1700, 1500, 100))
    spawn(unreal.load_class(None, '/Script/msc_vr.WarehouseSimulationClock'), 'ConstructionClock', (0, 0, 0))


def projected(world):
    value = pc.project_world_location_to_screen(world, False)
    if isinstance(value, unreal.Vector2D):
        return value
    assert value and not any(isinstance(item, bool) and not item for item in value), ('Projection failed', world, value)
    return next(item for item in value if isinstance(item, unreal.Vector2D))


def _tick_impl(delta_seconds):
    global game, pc, layout, construction, building, live, source_snapshot, stock_snapshot, phase, phase_at, capture_paths
    try:
        assert time.monotonic()-started < 240, ('Construction test timed out', phase)
        if phase == 'start':
            fixture()
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if phase == 'settle':
            if now < 1.5:
                return
            if with_lifts:
                freight = allof(unreal.WarehousePalletLift)[0]
                if not freight.is_idle_for_layout():
                    assert now < 20, ('Cargo lift did not settle before editing', freight.get_editor_property('status'))
                    return
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            assert pc and hasattr(pc, 'get_construction_editor'), 'Production controller was not instantiated'
            live = {actor.get_actor_label(): actor for actor in allof(unreal.Actor)}
            assert not allof(unreal.WarehouseBuildingLayout), 'Opening PIE eagerly created a building editor/controller'
            pc.set_observer_floor(0)
            pc.set_layout_editing(True)
            layout = allof(unreal.WarehouseLayoutEditor)[0]
            assert unreal.GameplayStatics.is_game_paused(game) and layout.get_editor_property('editing')
            source_names = ['Floor', 'MZ_L2_Deck_Test', 'MZ_L3_Deck_Test', 'WH_Wall_N', 'WH_Roof', 'WH_Column_01',
                            'WH_Wall_S_Left', 'WH_Wall_S_Right', 'WH_LoadingDoor_Header', 'EXT_Canopy', 'EXT_DockPlatform']
            source_snapshot = {name: physical_snapshot(live[name]) for name in source_names}
            stock_snapshot = {name: physical_snapshot(live[name]) for name in
                              ['ConstructionEdgeCargo', 'ConstructionUpperCargo', 'ConstructionFixtureRack',
                               'ConstructionExteriorOverhang', 'ConstructionExteriorPier',
                               'ConstructionMountedGuard', 'ConstructionDockMoveBlocker', 'ConstructionSlopedExteriorPlate',
                               'ConstructionWallMountedComputer', 'ConstructionRestingBarrel',
                               'WH_LightRack_1_H', 'WH_LightStock_1_0']}
            for name in ('ConstructionEdgeCargo', 'ConstructionUpperCargo'):
                assert live[name].get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
            pc.set_construction_editing(True)
            construction = pc.get_construction_editor()
            building = allof(unreal.WarehouseBuildingLayout)[0]
            assert construction and construction.get_editor_property('active')
            assert building.get_editor_property('revision') == 0 and not building.get_editor_property('generated_structure')
            assert current_settings().width_cm == 4800 and current_settings().depth_cm == 4000
            assert current_settings().floor2.enabled and current_settings().floor3.enabled and not current_settings().floor4.enabled
            sources = list(building.get_editor_property('replaced_structure'))
            assert not any(live[name] in sources for name in ('WH_LightRack_1_H', 'WH_LightStock_1_0'))
            check('light_rack_and_its_stock_are_preserved_inventory_not_lighting_architecture')
            for name, previous in source_snapshot.items():
                assert physical_snapshot(live[name]) == previous, ('Opening editor changed the default shell', name)
            check('opening_editor_captures_default_three_floors_without_geometry_changes')
            barrel = live['ConstructionRestingBarrel']
            barrel_body = barrel.get_component_by_class(unreal.StaticMeshComponent)
            barrel_bounds = unreal.WarehouseDamageSystem.get_physical_bounds(barrel)
            assert barrel_body.is_simulating_physics() and barrel_body.is_gravity_enabled() and abs(barrel_body.get_mass()-55) < .1
            assert -2 <= barrel_bounds.min.z < 1, ('Production barrel did not settle on the native floor', barrel_bounds)
            check('production_barrel_settles_naturally_on_existing_floor_before_editing',
                  native_bottom_cm=barrel_bounds.min.z, mass_kg=barrel_body.get_mass(), warped_after_pie=False)
            construction.set_target(unreal.WarehouseConstructionTarget.BUILDING)
            assert construction.begin_gesture_at(unreal.Vector(2800, 2000, 3), unreal.Vector2D(1, 1))
            construction.move_gesture_at(unreal.Vector(3000, 2200, 3))
            assert construction.get_editor_property('draft').width_cm == 5200 and current_settings().width_cm == 4800
            assert not building.get_editor_property('generated_structure')
            assert not construction.end_gesture(False)
            for name, previous in source_snapshot.items():
                assert physical_snapshot(live[name]) == previous
            check('drag_preview_cancel_preserves_real_geometry_collision_and_stock')
            phase = 'pointer'
            phase_at = time.monotonic()
            return
        if phase == 'pointer':
            if time.monotonic()-phase_at < .4:
                return
            if rendered:
                before = building.get_editor_property('revision')
                assert construction.pointer_down(projected(unreal.Vector(2800, 2000, 3))), 'Resize handle was not hit through real viewport projection'
                construction.pointer_move(projected(unreal.Vector(2900, 2100, 3)))
                assert construction.get_editor_property('dragging') and construction.get_editor_property('draft').width_cm == 5000
                assert not construction.end_gesture(False)
                assert building.get_editor_property('revision') == before
                check('camera_projected_real_pointer_handle_drag_and_cancel')
            phase = 'transactions'
            return
        if phase == 'transactions':
            for property_name, value in [('width_cm', 9601), ('depth_cm', 8001), ('roof_height_cm', 2001),
                                         ('width_cm', 1599), ('depth_cm', 1599), ('roof_height_cm', 299)]:
                proposed = current_settings()
                proposed.set_editor_property(property_name, value)
                reject(proposed, 'fixed_limit_reject_' + property_name + '_' + str(value), '범위')
            buried = live['ConstructionBuriedBody']
            buried.set_actor_enable_collision(True)
            assert unreal.WarehouseDamageSystem.get_physical_bounds(buried).min.z < -3
            proposed = current_settings()
            proposed.roof_height_cm += 100
            reject(proposed, 'object_buried_more_than_three_cm_is_not_resting_contact', 'ConstructionBuriedBody')
            buried.set_actor_enable_collision(False)
            proposed = current_settings()
            proposed.width_cm = 4600
            reject(proposed, 'shrink_rejects_outside_stair_landings', '계단')
            # Keep the stairs legal for the separate stock-boundary case; the
            # outer stair landing is the first guard for this narrower shell.
            for floor_name in ('floor2', 'floor3'):
                floor = proposed.get_editor_property(floor_name)
                old_stair = floor.stair_offset_cm
                floor.stair_offset_cm = unreal.Vector2D(old_stair.x+100, old_stair.y)
                proposed.set_editor_property(floor_name, floor)
            reject(proposed, 'shrink_rejects_existing_edge_stock_with_exact_reason', 'ConstructionEdgeCargo')
            # UI clamps both large and small previews, without touching geometry.
            for point, expected in [(unreal.Vector(20000, 20000, 3), (9600, 8000)),
                                    (unreal.Vector(401, 1, 3), (1600, 1600))]:
                assert construction.begin_gesture_at(unreal.Vector(2800, 2000, 3), unreal.Vector2D(1, 1))
                construction.move_gesture_at(point)
                draft = construction.get_editor_property('draft')
                assert (draft.width_cm, draft.depth_cm) == expected
                assert not construction.end_gesture(False)
            check('tile_preview_clamps_to_fixed_16_to_96_by_16_to_80_metre_limits')
            proposed = current_settings()
            proposed.width_cm = 5000
            proposed.roof_height_cm += 100
            reject(proposed, 'new_wall_collision_with_independent_exterior_object_remains_blocked', 'ConstructionExteriorPier')
            proposed = current_settings()
            proposed.depth_cm = 4600
            proposed.roof_height_cm += 100
            reject(proposed, 'moving_entrance_architecture_into_an_external_body_rejected_without_mutation', 'ConstructionDockMoveBlocker')
            proposed = current_settings()
            proposed.roof_height_cm += 100
            assert building.apply_settings(proposed), building.get_editor_property('status')
            assert current_settings().width_cm == 4800 and current_settings().depth_cm == 4000
            assert_stock_unchanged()
            entrance = [actor for actor in building.get_editor_property('generated_structure') if actor.actor_has_tag('WarehouseEntranceExterior')]
            assert len(entrance) == 2 and all(live[name].get_editor_property('hidden') for name in ['EXT_Canopy', 'EXT_DockPlatform'])
            assert all(actor.get_actor_enable_collision() and actor.static_mesh_component.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS for actor in entrance)
            south_walls = [actor for actor in building.get_editor_property('generated_structure')
                           if actor.actor_has_tag('ObserverFadeWall') and
                           abs(actor.get_actor_bounds(False)[0].y+1980) < .1 and
                           actor.get_actor_bounds(False)[0].z > 0]
            assert len(south_walls) == 3, 'Authored south wall faces/loading opening drifted during a roof-only change'
            assert all(abs(actor.get_actor_bounds(False)[1].y-17.5) < .1 for actor in south_walls)
            header = next(actor for actor in south_walls if abs(actor.get_actor_bounds(False)[0].x) < .1)
            centre, extent = header.get_actor_bounds(False)
            assert abs(extent.x-700) < .1 and abs(centre.z-extent.z-490) < .1
            check('roof_only_preserves_authored_wall_faces_and_loading_opening_without_moving_external_computer')
            entry_before = [actor.get_actor_bounds(False)[0].y for actor in entrance]
            check('roof_only_change_preserves_straddling_exterior_and_rebuilds_real_entrance_architecture', entrance_parts=len(entrance))
            check('sloped_exterior_plate_with_overlapping_bounds_and_clear_native_geometry_is_preserved')
            for change in (100, -100):
                proposed = current_settings()
                proposed.roof_height_cm += change
                assert building.apply_settings(proposed), building.get_editor_property('status')
                assert_stock_unchanged()
            check('unchanged_entrance_native_contacts_survive_second_and_third_apply_with_hidden_originals')
            assert construction.begin_gesture_at(unreal.Vector(2800, 2000, 3), unreal.Vector2D(1, 1))
            construction.move_gesture_at(unreal.Vector(3000, 2200, 3))
            assert construction.end_gesture(), construction.get_editor_property('status')
            assert current_settings().width_cm == 5200 and current_settings().depth_cm == 4400
            assert building.get_editor_property('generated_structure')
            entrance = [actor for actor in building.get_editor_property('generated_structure') if actor.actor_has_tag('WarehouseEntranceExterior')]
            assert len(entrance) == 2
            assert all(abs(new-old+200) < .1 for new, old in zip(sorted(actor.get_actor_bounds(False)[0].y for actor in entrance), sorted(entry_before)))
            assert_stock_unchanged()
            check('entrance_canopy_and_dock_follow_front_wall_without_moving_independent_exterior_objects')
            assert all('WarehouseMapStructure' in [str(tag) for tag in actor.tags] for actor in building.get_editor_property('generated_structure'))
            assert all(live[name].get_editor_property('hidden') for name in source_snapshot)
            assert_stock_unchanged()
            services = [actor for actor in building.get_editor_property('generated_structure') if actor.actor_has_tag('WarehouseServices')]
            generated_lights = [actor for actor in allof(unreal.SpotLight)
                                if actor.actor_has_tag('WarehouseBuildingGenerated') and actor.get_owner() == building]
            assert services and generated_lights, 'Automatic building services/lights were not generated'
            check('successful_native_tile_resize_preserves_stock_and_generates_architecture_services',
                  generated=len(building.get_editor_property('generated_structure')), services=len(services))
            construction.set_target(unreal.WarehouseConstructionTarget.FLOOR3)
            assert construction.toggle_selected_floor(), construction.get_editor_property('status')
            construction.set_target(unreal.WarehouseConstructionTarget.FLOOR2)
            before = geometry_snapshot()
            revision = building.get_editor_property('revision')
            assert not construction.toggle_selected_floor(), 'Removed the occupied second floor'
            assert '지지' in construction.get_editor_property('status') and geometry_snapshot() == before
            assert building.get_editor_property('revision') == revision and current_settings().floor2.enabled
            check('occupied_mezzanine_removal_is_atomic_and_reports_lost_support', reason=construction.get_editor_property('status'))
            construction.set_target(unreal.WarehouseConstructionTarget.FLOOR3)
            assert construction.toggle_selected_floor(), construction.get_editor_property('status')
            construction.set_target(unreal.WarehouseConstructionTarget.FLOOR4)
            assert construction.toggle_selected_floor(), construction.get_editor_property('status')
            assert current_settings().floor4.enabled and current_settings().roof_height_cm >= 1500
            proposed = current_settings()
            floor = proposed.floor3
            floor.enabled = False
            proposed.floor3 = floor
            reject(proposed, 'fourth_floor_requires_contiguous_lower_storeys', '순서')
            pc.set_observer_floor(3)
            assert pc.get_editor_property('observer_floor') == 3
            check('fourth_floor_native_geometry_enabled_and_observer_selectable')
            proposed = current_settings()
            floor = proposed.floor2
            floor.stair_offset_cm = floor.offset_cm
            floor.stair_yaw_degrees = 90
            proposed.floor2 = floor
            reject(proposed, 'isolated_atrium_stair_landing_rejected_without_geometry_or_stock_mutation', '계단참')
            construction.set_target(unreal.WarehouseConstructionTarget.STAIRS)
            construction.set_stair_floor(1)
            old = current_settings().floor2.stair_offset_cm
            start = building.get_actor_location() + unreal.Vector(old.x, old.y, 403)
            assert construction.begin_gesture_at(start, unreal.Vector2D())
            construction.move_gesture_at(start + unreal.Vector(700, 0, 0))
            assert construction.end_gesture(), construction.get_editor_property('status')
            assert abs(current_settings().floor2.stair_offset_cm.x-old.x-700) < .01
            assert construction.rotate_stairs(90), construction.get_editor_property('status')
            assert abs(abs(current_settings().floor2.stair_yaw_degrees)-180) < .01
            assert_stock_unchanged()
            check('selected_second_floor_stairs_move_on_tiles_and_rotate_ninety_degrees')
            if rendered:
                pc.set_observer_floor(0)
                construction.set_target(unreal.WarehouseConstructionTarget.BUILDING)
                suffix = str(time.time_ns())
                capture_paths = [output / ('ConstructionHighRes_' + suffix + '.png'),
                                 output / ('ConstructionUI_' + suffix + '.png')]
                phase = 'capture_ready'
                phase_at = time.monotonic()
            else:
                phase = 'post_transactions'
            return
        if phase == 'capture_ready':
            if time.monotonic()-phase_at < 1:
                return
            assert unreal.GameplayStatics.is_game_paused(game) and construction.get_editor_property('active')
            unreal.SystemLibrary.execute_console_command(game,
                'HighResShot 1600x900 filename=' + capture_paths[0].as_posix(), pc)
            phase = 'capture_highres'
            phase_at = time.monotonic()
            return
        if phase == 'capture_highres':
            assert time.monotonic()-phase_at < 45, 'HighResShot did not finish writing the construction screenshot'
            highres = screenshot_ready(capture_paths[0])
            if highres is None:
                return
            assert (highres['width'], highres['height']) == (1600, 900), highres
            # A failed optional UI capture does not invalidate the independent
            # native geometry checks, and must not be relabelled as UI success.
            try:
                capture_game_viewport_ui(capture_paths[1])
            except Exception:
                report['uiCapture'] = dict(verified=False, detail=traceback.format_exc(), osInputTested=False)
                report['excluded'].append('Actual construction Slate capture unavailable; visual UI remains unverified')
                capture_paths.pop()
            phase = 'capture_ui'
            phase_at = time.monotonic()
            return
        if phase == 'capture_ui':
            assert time.monotonic()-phase_at < 45, 'Construction UI screenshot did not finish writing'
            images = [screenshot_ready(path) for path in capture_paths]
            if any(image is None for image in images):
                return
            assert unreal.GameplayStatics.is_game_paused(game) and construction.get_editor_property('active')
            assert_stock_unchanged()
            report['screenshots'] = images
            check('rendered_paused_generated_building_saved_with_independent_stock_preserved',
                  highres=images[0], os_input_tested=False)
            if len(images) == 2:
                check('actual_game_viewport_slate_png_saved_for_visual_review',
                      slate_ui=images[1], visual_review_required=True, os_input_tested=False)
            phase = 'post_transactions'
            return
        if phase == 'post_transactions':
            pc.set_construction_editing(False)
            if with_lifts:
                pc.set_observer_floor(0)
                layout.set_category(unreal.WarehouseLayoutCategory.ELEVATOR)
                general, freight = live['ConstructionGeneralLift'], live['ConstructionFreightLift']
                assert general.get_editor_property('floor_count') == 4 and freight.get_editor_property('floor_count') == 4
                # Preserve full-floor sources while live cut pieces follow the current shafts.
                old = general.get_actor_location()
                layout.set_selection([general])
                rejected_moves, accepted_offset = [], None
                for dy in [-300, -500, -600, -700]:
                    assert layout.begin_drag(general, old), layout.get_editor_property('status')
                    layout.update_drag(old+unreal.Vector(0, dy, 0))
                    if layout.end_drag():
                        accepted_offset = dy
                        break
                    rejected_moves.append(dict(offset_y_cm=dy, reason=layout.get_editor_property('status')))
                    actual = general.get_actor_location()
                    assert all(abs(getattr(actual, axis)-getattr(old, axis)) < .1 for axis in ['x', 'y', 'z']), 'Rejected lift move did not restore its original pose'
                    assert_stock_unchanged()
                assert accepted_offset is not None, ('No physically clear supported lift position', rejected_moves)
                assert abs(general.get_actor_location().y-old.y-accepted_offset) < 2
                for floor in range(4):
                    assert_floor_at(old, floor, [general, freight], 'Old general shaft hole remained open after building transaction')
                check('building_generated_sources_allow_all_floor_lift_relocation_and_old_hole_fill',
                      accepted_offset_y_cm=accepted_offset, rejected_moves=rejected_moves)
                at = freight.get_actor_location()
                layout.set_selection([freight])
                assert layout.delete_selection(), layout.get_editor_property('status')
                for floor in range(4):
                    assert_floor_at(at, floor, [general], 'Deleted freight shaft hole remained open after building transaction')
                check('building_generated_sources_restore_all_four_floors_after_freight_lift_delete')
                assert_stock_unchanged()
            pc.toggle_layout_editing()
            assert not unreal.GameplayStatics.is_game_paused(game)
            phase_at = now
            phase = 'resume'
            return
        if phase == 'resume':
            if now-phase_at < 2:
                return
            for name, expected_height in [('ConstructionEdgeCargo', 20), ('ConstructionUpperCargo', 420)]:
                actor = live[name]
                body = actor.get_component_by_class(unreal.StaticMeshComponent)
                assert body.is_simulating_physics() and body.is_gravity_enabled() and not actor.get_attach_parent_actor()
                assert abs(actor.get_actor_location().z-expected_height) < 2, ('Cargo lost its real floor after editing', name, actor.get_actor_location())
            check('independent_native_stock_remains_supported_after_simulation_resume')
            finish(True, 'Tile preview/cancel and native transactions; fixed limits, unsafe shrink/lost-floor rejection, stock/rack preservation, automatic architecture/services, four floors and stairs; no saved map changes')
    except Exception:
        finish(False, traceback.format_exc())


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
