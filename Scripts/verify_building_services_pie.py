"""Unsaved Entry PIE: service density follows confirmed building geometry.

Uses real local service meshes, the production building transaction, and an
independent physical parcel. Does not save any map or drive operating forklifts.
The root test runner owns the editor; this script never launches another one.
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
with_loading = bool(globals().get('BUILDING_LOADING_CHECKS', False)) or '-buildingloadingchecks' in command
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
output = Path(unreal.Paths.project_saved_dir()) / 'Testing/BuildingServices_20261028'
output.mkdir(parents=True, exist_ok=True)
saved_map = Path(unreal.Paths.project_content_dir()) / 'FirstPerson/Lvl_FirstPerson.umap'
original_hash = hashlib.sha256(saved_map.read_bytes()).hexdigest()
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
report = dict(passed=False, checks=[], stages=[], screenshots=[], rendered=rendered,
              loadingChecks=with_loading,
              screenshotsRequireVisualReview=rendered, osInputTested=False,
              scope='Unsaved Entry; local service assets; production confirmed building edits; native freight shaft; preserved Chaos parcel',
              exclusions=['No full warehouse performance comparison', 'No OS pointer test', 'No running forklift transport in this fixture'])
started = time.monotonic()
phase = 'start'
busy = finished = False
game = pc = building = layout = parcel = camera = None
stock_snapshot = None
small_settings = None
small_counts = None
large_counts = None
capture_path = None
capture_next = None
capture_frames = 0
capture_at = 0


def check(name, **data):
    report['checks'].append(dict(name=name, **data))
    print('BUILDING_SERVICES_CHECK', name, json.dumps(data, ensure_ascii=False))


def allof(cls):
    return unreal.GameplayStatics.get_all_actors_of_class(game, cls)


def loading_snapshot():
    if not with_loading:
        return None
    instance = unreal.GameplayStatics.get_game_instance(game)
    matches = [obj for obj in unreal.ObjectIterator(unreal.WarehouseLoadingSubsystem) if obj.get_outer() == instance]
    assert len(matches) == 1
    loading = matches[0]
    return dict(active=loading.is_loading(), tasks=loading.get_active_task_count(),
                started=loading.get_editor_property('started_screens'), completed=loading.get_editor_property('completed_screens'),
                paint=loading.get_editor_property('last_paint_count'))


def pose(actor):
    return [round(float(getattr(v, a)), 5) for v, axes in
            ((actor.get_actor_location(), ('x', 'y', 'z')), (actor.get_actor_rotation(), ('pitch', 'yaw', 'roll')),
             (actor.get_actor_scale3d(), ('x', 'y', 'z'))) for a in axes]


def stock_state():
    body = parcel.get_component_by_class(unreal.StaticMeshComponent)
    return dict(pose=pose(parcel), mass=round(body.get_mass(), 4), simulated=body.is_simulating_physics(),
                gravity=body.is_gravity_enabled(), collision=str(body.get_collision_enabled()),
                cargo_id=str(parcel.get_editor_property('cargo_id')), body_mesh=body.static_mesh.get_path_name())


def spawn(cls, name, point):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
    assert actor, name
    actor.set_actor_label(name)
    return actor


def fixture():
    assert not editor.get_game_world(), 'Stop previous PIE before this test'
    assert level.load_level('/Engine/Maps/Entry')
    for actor in list(actors.get_all_level_actors()):
        if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
            actors.destroy_actor(actor)
    world = editor.get_editor_world()
    settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
    mode = unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C')
    assert mode, 'Production player GameMode is required'
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

    # Source decks have the real freight-lift opening. No deck or stock is
    # force-moved after play; the building transaction replaces these sources.
    hole = (-1140, 1360, -960, 1540)
    for floor, center, size in ((0, (400, 0), (4800, 4000)), (1, (0, 0), (3600, 3600)), (2, (0, 0), (3600, 3600))):
        x0, y0, x1, y1 = center[0]-size[0]/2, center[1]-size[1]/2, center[0]+size[0]/2, center[1]+size[1]/2
        a, b, c, d = hole
        rectangles = [(x0, y0, a, y1), (c, y0, x1, y1), (a, y0, c, b), (a, d, c, y1)]
        for index, (left, bottom, right, top) in enumerate(rectangles):
            name = ('Floor' if floor == 0 else f'MZ_L{floor+1}_Deck_Test') + ('' if index == 0 else '_Cut_'+str(index))
            block(name, ((left+right)/2, (bottom+top)/2, floor*400-10), (right-left, top-bottom, 20),
                  structural+['ElevatorLayoutFloor', f'WarehouseFloor{floor}'])
    block('WH_Roof', (400, 0, 1215), (4800, 4000, 30), structural+['ObserverRoof'])
    for index, (name, size) in enumerate((('WH_Wall_N', (4800, 35, 1200)), ('WH_Column_01', (40, 40, 1200)),
                                        ('WH_RoofBeam_01', (1000, 20, 30)), ('MZ_L2_InnerLeft_Bar55', (160, 5, 5)))):
        block(name, (20000, index*5000, 2000), size, structural)
    source_meshes = ('FirePipeStraight_2m', 'TrayStraight_2m', 'CableBundle_2m', 'ConduitStraight_1m',
                     'TrayHanger', 'PipeHanger', 'SprinklerHead', 'LEDLinear_1200')
    for index, name in enumerate(source_meshes):
        asset = unreal.load_asset('/Game/Warehouse/Services/Meshes/SM_WS_'+name)
        assert asset, 'Missing project service asset: '+name
        actor = spawn(unreal.StaticMeshActor, 'WS_'+name+'_Fixture', (25000, index*2000, 2000))
        actor.static_mesh_component.set_static_mesh(asset)
        actor.static_mesh_component.set_collision_profile_name('BlockAll')
        if name in ('TrayStraight_2m', 'CableBundle_2m'):
            actor.set_actor_rotation(unreal.Rotator(yaw=90), False)
        actor.tags = structural+['WarehouseServices']
    freight = spawn(unreal.WarehousePalletLift, 'ServicesFreightLift', (-1050, 1450, 0))
    freight.set_actor_rotation(unreal.Rotator(yaw=90), False)
    freight.tags = list(freight.tags)+['LayoutType:Elevator']
    item = spawn(unreal.WarehouseCargo, 'ServicesPreservedCargo', (1000, -1400, 20.1))
    item.set_cargo_mesh(cube)
    item.set_actor_scale3d(unreal.Vector(.4, .3, .4))
    item.set_gross_mass_kg(12)
    item.set_editor_property('cargo_id', 'SERVICES-PRESERVED-12KG')
    strength = spawn(unreal.WarehouseDamageSystem, 'ServicesStrength', (0, 0, 0))
    strength.set_editor_property('objects', [unreal.WarehouseStrength(name='PreservedCargo', members=[item],
        mass_kg=12, rated_load_kg=200, impact_yield_j=1000, impact_failure_j=5000)])
    spawn(unreal.PlayerStart, 'ServicesPlayerStart', (1700, 1500, 100))
    spawn(unreal.WarehouseSimulationClock, 'ServicesClock', (0, 0, 0))
    view = spawn(unreal.CameraActor, 'ServicesCamera', (2200, -1700, 230))
    view.get_component_by_class(unreal.CameraComponent).set_field_of_view(78)
    sun = spawn(unreal.DirectionalLight, 'ServicesSun', (0, 0, 1600))
    sun.set_actor_rotation(unreal.Rotator(pitch=-55, yaw=25), False)
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(3)
    sky = spawn(unreal.SkyLight, 'ServicesSky', (0, 0, 1800))
    sky.get_component_by_class(unreal.SkyLightComponent).set_intensity(1)


def settings_for(width, depth, maximum=False):
    proposed = building.get_settings()
    proposed.width_cm, proposed.depth_cm = width, depth
    proposed.roof_height_cm = 1800 if maximum else 1200
    for index, field in enumerate(('floor2', 'floor3', 'floor4'), 1):
        deck = proposed.get_editor_property(field)
        deck.enabled = index < 3 or maximum
        deck.offset_cm = unreal.Vector2D(0, 0)
        deck.size_cm = unreal.Vector2D(width-1000, depth-800)
        deck.atrium_size_cm = unreal.Vector2D(1000, 2600)
        deck.stair_offset_cm = unreal.Vector2D(-(width-1000)/2-80, -800)
        deck.stair_yaw_degrees = 90
        proposed.set_editor_property(field, deck)
    return proposed


def xy_overlap(a, b, margin=.5):
    return a[0]+margin < b[2] and b[0]+margin < a[2] and a[1]+margin < b[3] and b[1]+margin < a[3]


def service_audit(label, apply_seconds):
    generated = list(building.get_editor_property('generated_structure'))
    services = [a for a in generated if a.actor_has_tag('WarehouseServices')]
    assert services, 'No real service modules generated'
    assert len(generated) == len([a for a in allof(unreal.StaticMeshActor) if a.actor_has_tag('WarehouseBuildingGenerated')]), 'Old generated geometry accumulated'
    settings = building.get_settings()
    origin = building.get_actor_location()
    freight = next(a for a in allof(unreal.WarehousePalletLift) if a.get_actor_label() == 'ServicesFreightLift')
    assert freight.get_editor_property('floor_count') == (4 if settings.floor4.enabled else 3)
    shaft = unreal.WarehouseDamageSystem.get_physical_bounds(freight)
    shaft_xy = (shaft.min.x-origin.x-9, shaft.min.y-origin.y-9, shaft.max.x-origin.x+9, shaft.max.y-origin.y+9)
    counts, lengths, overlaps_tested, risers = {}, {}, 0, []
    for actor in services:
        tags = [str(tag) for tag in actor.tags]
        role = next((tag.split(':', 1)[1] for tag in tags if tag.startswith('WarehouseService:')), None)
        assert role, ('Missing service-role tag', actor.get_actor_label(), tags)
        counts[role] = counts.get(role, 0)+1
        mesh = actor.static_mesh_component
        assert '/Warehouse/Services/' in mesh.static_mesh.get_path_name(), mesh.static_mesh.get_path_name()
        assert not actor.is_actor_tick_enabled() and not building.is_actor_tick_enabled()
        assert mesh.get_editor_property('mobility') == unreal.ComponentMobility.STATIC
        assert unreal.WarehouseDamageSystem.is_physical_contact(mesh) and not mesh.is_simulating_physics()
        center, half = actor.get_actor_bounds(False)
        local = center-origin
        bounds = (local.x-half.x, local.y-half.y, local.x+half.x, local.y+half.y)
        assert abs(local.x)+half.x <= settings.width_cm/2+2 and abs(local.y)+half.y <= settings.depth_cm/2+2, ('Service outside shell', role, bounds)
        lengths.setdefault(role, []).append(round(max(half.x, half.y, half.z)*2, 2))
        if role in ('Tray', 'Cable'):
            scale = actor.get_actor_scale3d()
            rotation = actor.get_actor_rotation()
            assert abs(scale.y-1) < .001 and abs(scale.z-1) < .001 and .39 <= scale.x <= 2.01, ('Rotated source distorted native section', role, scale)
            assert abs(rotation.pitch) < .01 and abs(rotation.roll) < .01 and abs(rotation.yaw/90-round(rotation.yaw/90)) < .001, ('Service run is not aligned with the layout', role, rotation)
        if actor.actor_has_tag('WarehouseServiceRiser'):
            assert abs(abs(local.x)-(settings.width_cm/2-80)) < 4 and settings.depth_cm/2-90 < local.y < settings.depth_cm/2+2, ('Wall riser did not follow new wall', local, settings.width_cm, settings.depth_cm)
            risers.append([round(local.x, 2), round(local.y, 2), round(local.z, 2)])
        if role not in ('Pipe', 'Tray', 'Cable', 'Light', 'Sprinkler'):
            continue
        assert not xy_overlap(bounds, shaft_xy), ('Service crosses native freight shaft', actor.get_actor_label(), bounds, shaft_xy)
        for floor, field in enumerate(('floor2', 'floor3', 'floor4'), 1):
            deck = settings.get_editor_property(field)
            if not deck.enabled or not floor*400-170 < local.z < floor*400:
                continue
            outer = (deck.offset_cm.x-deck.size_cm.x/2, deck.offset_cm.y-deck.size_cm.y/2,
                     deck.offset_cm.x+deck.size_cm.x/2, deck.offset_cm.y+deck.size_cm.y/2)
            assert bounds[0] >= outer[0]-.5 and bounds[1] >= outer[1]-.5 and bounds[2] <= outer[2]+.5 and bounds[3] <= outer[3]+.5, ('Service has no deck overhead', role, bounds, outer)
            atrium = (deck.offset_cm.x-deck.atrium_size_cm.x/2, deck.offset_cm.y-deck.atrium_size_cm.y/2,
                      deck.offset_cm.x+deck.atrium_size_cm.x/2, deck.offset_cm.y+deck.atrium_size_cm.y/2)
            assert not xy_overlap(bounds, atrium), ('Service crosses atrium', role, bounds, atrium)
            yaw = math.radians(deck.stair_yaw_degrees)
            hx, hy = abs(math.cos(yaw))*336+abs(math.sin(yaw))*75, abs(math.sin(yaw))*336+abs(math.cos(yaw))*75
            opening = (deck.stair_offset_cm.x-hx, deck.stair_offset_cm.y-hy, deck.stair_offset_cm.x+hx, deck.stair_offset_cm.y+hy)
            assert not xy_overlap(bounds, opening), ('Service crosses staircase opening', role, bounds, opening)
            overlaps_tested += 1
    for required in ('Pipe', 'Tray', 'Cable', 'Conduit', 'Support', 'Sprinkler', 'Light'):
        assert counts.get(required, 0) > 0, ('Missing service type', required, counts)
    assert overlaps_tested > 0, 'No upper-deck service envelope tested'
    assert stock_state() == stock_snapshot, ('Building edit changed preserved cargo', stock_state(), stock_snapshot)
    row = dict(label=label, width_cm=settings.width_cm, depth_cm=settings.depth_cm, roof_cm=settings.roof_height_cm,
               floors=4 if settings.floor4.enabled else 3, generated=len(generated), services=len(services), counts=counts,
               module_max_lengths_cm={role: max(values) for role, values in lengths.items()},
               wall_riser_positions_cm=risers,
               deck_envelopes_checked=overlaps_tested, apply_wall_seconds=round(apply_seconds, 4),
               revision=building.get_editor_property('revision'), source_templates_hidden=True)
    report['stages'].append(row)
    check(label, generated=len(generated), counts=counts, apply_wall_seconds=row['apply_wall_seconds'])
    return counts


def begin_capture(name, next_phase):
    global phase, capture_path, capture_next, capture_frames, capture_at
    if not rendered:
        phase = next_phase
        return
    # Show the overhead decks too: a single-floor cutaway makes real hangers
    # appear unattached when viewing this fixture from eye level.
    pc.set_observer_floor(-1)
    pc.set_view_target_with_blend(camera, 0)
    settings = building.get_settings()
    camera.set_actor_location(unreal.Vector(400+settings.width_cm*.43, -settings.depth_cm*.41, 235), False, True)
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(-1000, 1000, 310)), False)
    capture_path = output/(name+'_'+str(time.time_ns())+'.png')
    capture_next, capture_frames, capture_at, phase = next_phase, 0, time.monotonic()+.5, 'capture_wait'


def finish(error=None):
    global finished
    if finished:
        return
    finished = True
    unreal.unregister_slate_post_tick_callback(handle)
    report['savedMapUnchanged'] = hashlib.sha256(saved_map.read_bytes()).hexdigest() == original_hash
    if not report['savedMapUnchanged']:
        error = (error or '')+' Saved default map changed.'
    report.update(passed=not error, wall_seconds=round(time.monotonic()-started, 3))
    if error:
        report['error'] = error
    (output/'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('BUILDING_SERVICES_PIE_'+('FAILED' if error else 'PASSED'), error or report['checks'])
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    if editor.get_game_world():
        level.editor_request_end_play()
    if not keep_open:
        unreal.SystemLibrary.quit_editor()


def tick_impl(delta_seconds):
    global game, pc, building, layout, parcel, camera, stock_snapshot, small_settings, small_counts, large_counts
    global phase, capture_frames
    assert time.monotonic()-started < 600, ('Service test timed out', phase)
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
        freight = allof(unreal.WarehousePalletLift)[0]
        if not freight.is_idle_for_layout():
            return
        named = {a.get_actor_label(): a for a in allof(unreal.Actor)}
        pc = unreal.GameplayStatics.get_player_controller(game, 0)
        parcel, camera = named['ServicesPreservedCargo'], named['ServicesCamera']
        pc.set_observer_floor(0)
        pc.set_layout_editing(True)
        layout = allof(unreal.WarehouseLayoutEditor)[0]
        assert unreal.GameplayStatics.is_game_paused(game)
        stock_snapshot = stock_state()
        assert stock_snapshot['simulated'] and stock_snapshot['gravity'] and abs(stock_snapshot['mass']-12) < .1
        pc.set_construction_editing(True)
        building = allof(unreal.WarehouseBuildingLayout)[0]
        assert not building.get_editor_property('generated_structure') and building.get_editor_property('revision') == 0
        check('opening_building_editor_does_not_generate_or_change_saved_shell')
        small_settings = building.get_settings()
        if with_loading:
            before = loading_snapshot()
            assert not before['active'] and before['tasks'] == 0
            invalid = building.get_settings()
            invalid.width_cm = 100
            previous_revision = building.get_editor_property('revision')
            assert not building.apply_settings(invalid), 'Unsafe building width accepted'
            after = loading_snapshot()
            assert not after['active'] and after['tasks'] == 0
            assert after['started']-before['started'] == after['completed']-before['completed']
            assert building.get_editor_property('revision') == previous_revision and stock_state() == stock_snapshot
            check('failed_building_transaction_does_not_leave_loading_active', before=before, after=after)
        phase = 'small'
        return
    if phase == 'capture_wait':
        capture_frames += 1
        if capture_frames < 10 or time.monotonic() < capture_at:
            return
        unreal.SystemLibrary.execute_console_command(game, 'HighResShot 1600x900 filename='+capture_path.as_posix(), pc)
        phase = 'capture'
        return
    if phase == 'capture':
        if not capture_path.exists():
            return
        try:
            data = capture_path.read_bytes()
        except OSError:
            return
        if len(data) < 1000 or data[:8] != b'\x89PNG\r\n\x1a\n' or data[-8:-4] != b'IEND':
            return
        assert int.from_bytes(data[16:20], 'big') == 1600 and int.from_bytes(data[20:24], 'big') == 900
        report['screenshots'].append(str(capture_path))
        phase = capture_next
        return
    if phase in ('small', 'large', 'small_again', 'maximum', 'maximum_again'):
        label = phase
        proposed = small_settings if phase in ('small', 'small_again') else settings_for(9600, 8000, True) if phase.startswith('maximum') else settings_for(8000, 6400)
        before_loading = loading_snapshot()
        before = time.monotonic()
        assert building.apply_settings(proposed), (phase, building.get_editor_property('status'))
        elapsed = time.monotonic()-before
        if with_loading:
            after_loading = loading_snapshot()
            assert not after_loading['active'] and after_loading['tasks'] == 0
            assert after_loading['started'] == before_loading['started']+1 and after_loading['completed'] == before_loading['completed']+1
            if rendered:
                assert after_loading['paint'] > 0, 'Long building operation completed without painting its loading screen'
            check('building_loading_'+label, before=before_loading, after=after_loading)
        counts = service_audit(label, elapsed)
        if phase == 'small':
            small_counts = counts
            begin_capture('Small_48x40m', 'large')
        elif phase == 'large':
            large_counts = counts
            assert sum(counts.values()) > sum(small_counts.values())
            assert counts['Pipe'] > small_counts['Pipe'] and counts['Tray'] > small_counts['Tray'] and counts['Light'] > small_counts['Light']
            check('larger_building_increases_service_density_and_moves_wall_risers', before=small_counts, after=counts)
            begin_capture('Large_80x64m', 'small_again')
        elif phase == 'small_again':
            assert counts == small_counts, ('Shrink did not restore original density', counts, small_counts)
            check('shrink_regenerates_original_counts_without_accumulation')
            phase = 'maximum'
        elif phase == 'maximum':
            large_counts = counts
            begin_capture('Maximum_96x80m_4Floors', 'maximum_again')
        else:
            assert counts == large_counts, ('Repeated confirmation accumulated service objects', counts, large_counts)
            check('maximum_four_floor_reconfirmation_has_no_accumulation_and_no_runtime_tick')
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
