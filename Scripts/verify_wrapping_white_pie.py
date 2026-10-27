"""Unsaved native PIE: one simulation-second local white wrapping overlay.

Captures Before/White/After at1600x900 and pauses simulation only while each
PNG is being written. Original materials, Chaos contacts, NPC confirmation and
finite native wrap-break joints remain the production implementation.
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
out = Path(unreal.Paths.project_saved_dir()) / 'Testing/WrappingWhite'
out.mkdir(parents=True, exist_ok=True)
saved_map = Path(unreal.Paths.project_content_dir()) / 'FirstPerson/Lvl_FirstPerson.umap'
original_hash = hashlib.sha256(saved_map.read_bytes()).hexdigest()
for material_name in ('M_PalletWrappingWhite', 'M_PalletWrap'):
    material = unreal.load_asset('/Game/Warehouse/Materials/'+material_name)
    assert material and unreal.MaterialEditingLibrary.get_material_property_input_node(
        material, unreal.MaterialProperty.MP_FRONT_MATERIAL), (material_name, 'Run Scripts/prepare_pallet_wrap.py: missing render surface')
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
settings.set_editor_property('default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
carton = unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2')
assert carton, 'Install the existing intact carton assets first'


def spawn(cls, name, point):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
    actor.set_actor_label(name)
    assert (actor.get_actor_location()-unreal.Vector(*point)).length() < .01
    return actor


def fit_box(actor, point):
    actor.set_cargo_mesh(carton)
    size = carton.get_bounds().box_extent*2
    actor.set_actor_scale3d(unreal.Vector(40/size.x, 40/size.y, 30/size.z))
    center, _ = actor.get_actor_bounds(False)
    actor.set_actor_location(actor.get_actor_location()+unreal.Vector(*point)-center, False, False)


floor = spawn(unreal.StaticMeshActor, 'WhiteFloor', (0, 350, -10))
floor.static_mesh_component.set_static_mesh(cube)
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.set_actor_scale3d(unreal.Vector(30, 35, .2))
spawn(unreal.PlayerStart, 'WhiteStart', (-900, -900, 110))
fixtures = []
carton_offset = tuple(float(value) for value in globals().get('WRAP_WHITE_OFFSET', (0, 0)))
assert len(carton_offset) == 2 and all(abs(value) <= 30 for value in carton_offset), 'Test carton offset must leave physical pallet support'
for index, y in enumerate((0, 400, 800)):
    pallet = spawn(unreal.WarehousePallet, 'WhitePallet'+str(index), (0, y, .1))
    point = (carton_offset[0], y+carton_offset[1], 30.2) if index == 0 else (0, y, 30.2)
    box = spawn(unreal.WarehouseCargo, 'WhiteBox'+str(index), point)
    fit_box(box, point)
    box.set_gross_mass_kg(12)
    zone = spawn(unreal.WarehouseInboundZone, 'WhiteZone'+str(index), (0, y, 0))
    zone.set_editor_property('half_size_cm', unreal.Vector(180, 180, 80))
    fixtures.extend(((pallet, 25), (box, 12)))
other = spawn(unreal.WarehouseCargo, 'WhiteUnrelated', (150, 0, 15.1))
fit_box(other, (150, 0, 15.1))
other.set_gross_mass_kg(7)
fixtures.append((other, 7))
strength = spawn(unreal.WarehouseDamageSystem, 'WhiteStrength', (0, -1000, 0))
strength.set_editor_property('objects', [unreal.WarehouseStrength(name=actor.get_actor_label(), members=[actor],
    mass_kg=mass, rated_load_kg=1500, impact_yield_j=200, impact_failure_j=1000) for actor, mass in fixtures])
camera = spawn(unreal.CameraActor, 'WhiteCamera', (240, -260, 190))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(55, 0, 24)), False)
camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(55)
sun = spawn(unreal.DirectionalLight, 'WhiteSun', (0, 0, 1500))
sun.set_actor_rotation(unreal.Rotator(pitch=-50, yaw=-35), False)
sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(5)
spawn(unreal.SkyAtmosphere, 'WhiteSky', (0, 0, 0))

started = time.monotonic()
phase = 'start'
capture_next = None
capture_path = None
capture_frames = 0
capture_ready_at = 0
live = None
game = pc = None
originals = {}
wrap_started = tear_started = 0
handle = None
busy = finished = False
report = dict(passed=False, checks=[], screenshots=[], osInputTested=False, screenshotsRequireVisualReview=True,
              carton_offset_xy_cm=list(carton_offset),
              scope='Native NPC receiving, local white overlay, simulation timing, cancellation/deletion and Chaos wrap tear')


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def base_state(part):
    return (tuple(part.get_material(i) for i in range(part.get_num_materials())),
            part.get_collision_enabled(), part.get_collision_object_type(),
            part.is_simulating_physics(), part.is_gravity_enabled(), round(part.get_mass(), 3))


def check(name, **values):
    report['checks'].append(dict(name=name, **values))
    print('WRAPPING_WHITE_CHECK', name, json.dumps(values, ensure_ascii=False))


def check_film(pallet, torn=False):
    parts = pallet.get_components_by_class(unreal.SplineMeshComponent)
    sides = [part for part in parts if part.component_has_tag('PalletWrapSide')]
    tops = [part for part in parts if part.component_has_tag('PalletWrapTop')]
    # This fixture has one carton height. It must be enclosed by four faces and
    # a lid, then expose the broken side and top after a real joint tear.
    assert len(sides) == (3 if torn else 4) and len(tops) == (0 if torn else 1), ('Wrap enclosure is incomplete', torn, len(sides), len(tops))
    assert len(parts) == len(sides) + len(tops), 'Unclassified wrapping geometry'
    pallet_bounds = body(pallet).static_mesh.get_bounds()
    base_height = max(pallet_bounds.origin.z-pallet_bounds.box_extent.z, pallet_bounds.origin.z+pallet_bounds.box_extent.z-3)
    assert all(abs(side.get_start_position().z-base_height) < .001 for side in sides), 'Film bottom does not bind top boards while clearing fork openings'
    for part in parts:
        assert part.component_has_tag('PalletWrapVisual')
        assert part.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION, 'Visual film changed physical contact'
        assert not part.get_editor_property('generate_overlap_events'), 'Visual film contributes gameplay overlaps'
        assert part.static_mesh.get_path_name() == '/Engine/BasicShapes/Plane.Plane', 'Film is not a single sheet'
    if tops:
        cap = tops[0]
        assert abs(cap.get_start_position().z - cap.get_end_position().z) < .001, 'Film lid is not flat'
        assert all(abs(side.get_end_position().z-cap.get_start_position().z) < .001 for side in sides), 'Film lid leaves an open top seam'
    return dict(sides=len(sides), tops=len(tops), base_height_cm=base_height, physical_collision=False)


def capture(name, next_phase):
    global phase, capture_next, capture_path, capture_frames, capture_ready_at
    unreal.GameplayStatics.set_game_paused(game, True)
    capture_path = out / (name+'.png')
    if capture_path.exists():
        capture_path.unlink()
    # Keep the physical phase frozen, but allow queued render-proxy/PSO updates
    # to reach the renderer before requesting the first screenshot of a change.
    capture_frames, capture_ready_at = 0, time.monotonic()+.3
    capture_next, phase = next_phase, 'capture_wait'


def finish(ok, detail):
    global finished
    if finished:
        return
    finished = True
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    report['savedMapUnchanged'] = hashlib.sha256(saved_map.read_bytes()).hexdigest() == original_hash
    report['passed'] = ok and report['savedMapUnchanged']
    report['detail'] = detail
    (out/'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('WRAPPING_WHITE_PIE_'+('PASSED' if report['passed'] else 'FAILED'), detail)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def run_tick(dt):
    global game, pc, live, phase, wrap_started, tear_started, capture_frames
    assert time.monotonic()-started < 90, ('Wrapping check timeout', phase)
    if phase == 'start':
        level.editor_request_begin_play()
        phase = 'settle'
        return
    game = editor.get_game_world()
    if not game:
        return
    pc = unreal.GameplayStatics.get_player_controller(game, 0)
    if live is None:
        live = {actor.get_actor_label(): actor for actor in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
        pc.set_view_target_with_blend(live['WhiteCamera'], 0)
        speed = int(globals().get('WRAP_WHITE_SPEED', 1))
        assert speed in (1, 2, 4) and pc.set_simulation_speed(speed)
        report['speed'] = speed
    now = unreal.GameplayStatics.get_time_seconds(game)
    p, box = live['WhitePallet0'], live['WhiteBox0']
    if phase == 'capture_wait':
        capture_frames += 1
        if capture_frames >= 8 and time.monotonic() >= capture_ready_at:
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
        if len(data) < 1000 or data[-8:-4] != b'IEND':
            return
        assert (int.from_bytes(data[16:20], 'big'), int.from_bytes(data[20:24], 'big')) == (1600, 900)
        report['screenshots'].append(str(capture_path))
        unreal.GameplayStatics.set_game_paused(game, False)
        phase = capture_next
        return
    if phase == 'settle' and now >= 1.5:
        for name in ('WhitePallet0', 'WhiteBox0', 'WhitePallet1', 'WhiteBox1', 'WhitePallet2', 'WhiteBox2', 'WhiteUnrelated'):
            part = body(live[name])
            originals[name] = (base_state(part), part.get_overlay_material())
        assert not p.get_editor_property('wrapping')
        assert p.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.PENDING
        capture('Before', 'await_white')
    elif phase == 'await_white' and p.get_editor_property('wrapping') and p.get_wrapping_remaining_seconds() < .999:
        wrap_started = now-(1-p.get_wrapping_remaining_seconds())
        for index in range(3):
            unit = live['WhitePallet'+str(index)]
            assert unit.get_editor_property('wrapping')
            assert unit.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.PENDING
            assert not unit.get_components_by_class(unreal.PhysicsConstraintComponent)
            assert not live['WhiteBox'+str(index)].get_wrapped_pallet(), 'Preparation was exposed as a completed wrap'
            for name in ('WhitePallet'+str(index), 'WhiteBox'+str(index)):
                part = body(live[name])
                assert base_state(part) == originals[name][0]
                assert part.get_overlay_material().get_path_name().startswith('/Game/Warehouse/Materials/M_PalletWrappingWhite')
        assert body(live['WhiteUnrelated']).get_overlay_material() == originals['WhiteUnrelated'][1]
        assert base_state(body(live['WhiteUnrelated'])) == originals['WhiteUnrelated'][0]
        check('white_is_local_and_original_materials_physics_remain_unchanged')
        capture('White', 'cancel_and_delete')
    elif phase == 'cancel_and_delete':
        live['WhitePallet1'].tear_wrap()
        assert not live['WhitePallet1'].get_editor_property('wrapping')
        for name in ('WhitePallet1', 'WhiteBox1'):
            assert body(live[name]).get_overlay_material() == originals[name][1]
        live['WhiteZone1'].destroy_actor()
        live['WhitePallet2'].destroy_actor()
        assert body(live['WhiteBox2']).get_overlay_material() == originals['WhiteBox2'][1]
        check('cancel_and_pallet_endplay_restore_only_their_own_overlays')
        phase = 'await_confirmed'
    elif phase == 'await_confirmed' and p.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED:
        elapsed = now-wrap_started
        assert 1-.001 <= elapsed <= 1.15, ('White duration in simulation seconds', elapsed)
        assert not p.get_editor_property('wrapping')
        assert live['WhiteZone0'].get_editor_property('confirmed_count') == 1
        for name in ('WhitePallet0', 'WhiteBox0'):
            part = body(live[name])
            assert part.get_overlay_material() == originals[name][1]
            assert base_state(part) == originals[name][0]
        assert p.get_wrapped_cargo() == [box] and box.get_wrapped_pallet() == p
        assert len(p.get_components_by_class(unreal.PhysicsConstraintComponent)) == 1
        check('white_film_encloses_four_sides_and_top_without_collision', **check_film(p))
        check('one_simulation_second_then_real_wrap_confirmation', elapsed_simulation_seconds=elapsed)
        capture('After', 'tear')
    elif phase == 'tear':
        body(box).add_impulse(unreal.Vector(0, 0, 15000))
        tear_started, phase = now, 'await_torn'
    elif phase == 'await_torn':
        if now-tear_started < .5:
            body(box).add_force(unreal.Vector(0, 0, 300000))
        if now-tear_started > 1.5:
            assert p.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.WRAP_TORN
            assert not p.get_wrapped_cargo() and not box.get_wrapped_pallet()
            assert body(box).is_simulating_physics() and body(box).is_gravity_enabled()
            assert body(box).get_overlay_material() == originals['WhiteBox0'][1]
            check('native_tear_opens_side_and_removes_top_without_collision', **check_film(p, torn=True))
            check('strong_native_force_still_tears_film_and_releases_cargo')
            finish(True, 'Local white preparation, exact simulation timer, restored overlays, native wrap and tear')


def tick(dt):
    global busy
    if busy or finished:
        return
    busy = True
    try:
        run_tick(dt)
    except Exception:
        finish(False, traceback.format_exc())
    finally:
        busy = False


handle = unreal.register_slate_post_tick_callback(tick)
