"""Native immediate carry placement/wrap/layout boundary checks; never saves.

Actual player BP, project pallet/carton meshes and Chaos support contacts.
Calls the E/F1 handlers' native APIs, not OS keyboard input. Test obstacles
are ordinary collidable cubes. Does not start a second editor or modify assets.
"""
import hashlib
import json
from pathlib import Path
import sys
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT/'Saved/Testing/ArcadeCarryPlacement'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(Path(__file__).resolve().parent))
from apply_real_world_scale import fit

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
originals = [ROOT/'Content/FirstPerson/Lvl_FirstPerson.umap',
             ROOT/'Content/FirstPerson/Blueprints/BP_FirstPersonCharacter.uasset']
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in originals}
started = time.monotonic()
now = 0.0
phase = 'setup'
game = pc = ai = pawn = movement = strength = None
named = {}
busy = finished = cleaning = False
handle = None
error = None
report = dict(passed=False, checks=[], screenshots=[], recent_samples=[], os_keys_tested=False,
              scope='Actual BP/project-mesh immediate carry placement, final destination clearance, wrap binding and held-unit layout guards; unsaved transient PIE fixture')


def check(name, **data):
    report['checks'].append(dict(name=name, **data))
    print('ARCADE_CARRY_BOUNDARY_CHECK', name, json.dumps(data, ensure_ascii=False))


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def enter(name):
    global phase
    phase = name


def assert_yaw(expected):
    control = pawn.get_controller().get_control_rotation().yaw
    actor = pawn.get_actor_rotation().yaw
    difference = lambda value: abs((value-expected+180) % 360-180)
    assert difference(control) < .2, ('Test controller lost requested yaw', expected, control)
    assert difference(actor) < .2, ('Player did not follow requested yaw', expected, actor)


def frames(n=1):
    for _ in range(n):
        yield


def wait(seconds, action=None):
    began = now
    while now-began < seconds:
        if action:
            action()
        yield


def relocate(point, yaw=0):
    movement.stop_movement_immediately()
    pawn.set_actor_location(unreal.Vector(*point), False, True)
    pawn.get_controller().set_control_rotation(unreal.Rotator(yaw=yaw))


def dynamic(actor, mass):
    part = body(actor)
    assert part.is_simulating_physics() and part.is_gravity_enabled(), actor.get_actor_label()
    assert abs(part.get_mass()-mass) < .2, (actor.get_actor_label(), part.get_mass(), mass)
    assert not actor.actor_has_tag('PlayerHeldCargo')


def set_pose(actor, location):
    part = body(actor)
    part.set_physics_linear_velocity(unreal.Vector())
    part.set_physics_angular_velocity_in_degrees(unreal.Vector())
    actor.set_actor_rotation(unreal.Rotator(), True)
    actor.set_actor_location(unreal.Vector(*location), False, True)


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


def setup():
    assert not editor.get_game_world(), 'Stop existing PIE first'
    assert level.load_level('/Engine/Maps/Entry')
    for actor in list(actors.get_all_level_actors()):
        if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
            actors.destroy_actor(actor)
    world = editor.get_editor_world()
    settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
    settings.set_editor_property('default_game_mode', unreal.load_class(None,
        '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    carton = unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1')
    assert cube and carton

    def spawn(cls, label, point):
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
        actor.set_actor_label(label)
        return actor

    def solid(label, point, size):
        actor = spawn(unreal.StaticMeshActor, label, point)
        actor.static_mesh_component.set_static_mesh(cube)
        actor.static_mesh_component.set_collision_profile_name('BlockAll')
        actor.set_actor_scale3d(unreal.Vector(*(v/100 for v in size)))
        return actor

    solid('CarryBoundaryFloor', (0, 1500, -20), (5000, 6500, 40))
    sun = spawn(unreal.DirectionalLight, 'CarryBoundarySun', (0, 0, 1500))
    sun.set_actor_rotation(unreal.Rotator(pitch=-45, yaw=-25), False)
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(4)
    light = spawn(unreal.PointLight, 'CarryBoundaryFill', (-350, -150, 320))
    light.get_component_by_class(unreal.PointLightComponent).set_intensity(6000)
    light.get_component_by_class(unreal.PointLightComponent).set_attenuation_radius(1200)
    spawn(unreal.SkyAtmosphere, 'CarryBoundarySky', (0, 0, 0))
    spawn(unreal.PlayerStart, 'CarryBoundaryStart', (-1600, -1200, 110))
    human = spawn(unreal.load_class(None,
        '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'),
        'CarryBoundaryHuman', (-160, 0, 110))
    human.set_editor_property('auto_possess_player', unreal.AutoReceiveInput.DISABLED)
    human.set_editor_property('auto_possess_ai', unreal.AutoPossessAI.DISABLED)
    spawn(unreal.AIController, 'CarryBoundaryController', (0, 0, 0))
    stock = []
    for label, x, y in [('BoxSupport', 0, 0), ('PalletSupport', 0, 600),
                        ('PalletHeld', -100, 440), ('RotationPallet', -80, 2100),
                        ('WrappedPallet', 0, 2900)]:
        pallet = spawn(unreal.WarehousePallet, label, (x, y, .2))
        stock.append((pallet, 25))
    for label, point, mass in [('HeavyBox', (-85, 0, 15.5), 1000),
                               ('TrappedBox', (0, 1400, 15.5), 1000),
                               ('WrappedBox', (0, 2900, 30.5), 12)]:
        box = spawn(unreal.WarehouseCargo, label, point)
        box.set_cargo_mesh(carton)
        fit(box, (36, 30, 30), point)
        box.set_gross_mass_kg(mass)
        stock.append((box, mass))
    # A box has room to settle normally, but no physically clear extraction path.
    for x in (-35, 35):
        solid('TrapSideX'+str(x), (x, 1400, 40), (10, 80, 80))
    for y in (-35, 35):
        solid('TrapSideY'+str(y), (0, 1400+y, 40), (80, 10, 80))
    solid('TrapRoof', (0, 1400, 40), (80, 80, 10))
    blocker = solid('RotationBlocker', (1500, 2100, 130), (160, 30, 220))
    blocker.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    zone = spawn(unreal.WarehouseInboundZone, 'WrapReceivingZone', (0, 2900, 0))
    zone.set_editor_property('half_size_cm', unreal.Vector(160, 160, 80))
    damage = spawn(unreal.WarehouseDamageSystem, 'CarryBoundaryStrength', (-1500, 0, 0))
    damage.set_editor_property('objects', [unreal.WarehouseStrength(
        name=a.get_actor_label(), members=[a], physics_meshes=[body(a).static_mesh],
        mass_kg=mass, rated_load_kg=1500, impact_yield_j=100000, impact_failure_j=200000)
        for a, mass in stock])
    level.editor_request_begin_play()


def scenario_body():
    global game, pc, ai, pawn, movement, strength, named
    setup()
    while not editor.get_game_world():
        yield
    game = editor.get_game_world()
    named = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
    pawn, ai = named['CarryBoundaryHuman'], named['CarryBoundaryController']
    strength = named['CarryBoundaryStrength']
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    pc = unreal.GameplayStatics.get_player_controller(game, 0)
    ai.possess(pawn)
    # No focus target is used: keep the explicit input heading instead of the
    # AIController default copying the pawn's previous yaw back every tick.
    ai.set_editor_property('set_control_rotation_from_pawn_orientation', False)
    ai.set_control_rotation(unreal.Rotator())
    yield from wait(1.2)

    enter('1000kg_box_placement')
    box, support = named['HeavyBox'], named['BoxSupport']
    assert pawn.try_pickup_cargo(box)
    assert pawn.get_carry_blend() == 1, 'Pickup still blends over time'
    check('pickup_hand_pose_is_immediate', blend=pawn.get_carry_blend())
    yield from wait(.3)
    assert pawn.get_held_cargo() == box
    if '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower():
        enter('first_person_heavy_carry_capture')
        ai.un_possess()
        pc.possess(pawn)
        pc.set_control_rotation(unreal.Rotator(pitch=-15, yaw=0))
        pc.set_view_target_with_blend(pawn, 0)
        yield from wait(.8)
        native_camera = pawn.get_component_by_class(unreal.CameraComponent)
        manager = unreal.GameplayStatics.get_player_camera_manager(game, 0)
        view_target = pc.get_view_target()
        distance = (manager.get_camera_location()-native_camera.get_world_location()).length()
        meshes = {part.get_name(): part for part in pawn.get_components_by_class(unreal.SkeletalMeshComponent)}
        carry_mesh = meshes['TwoHandCarryMesh']
        diagnostic = dict(view_target=view_target.get_actor_label() if view_target else None,
            pawn=pawn.get_actor_label(), held=pawn.get_held_cargo().get_actor_label() if pawn.get_held_cargo() else None,
            manager_location=str(manager.get_camera_location()), manager_rotation=str(manager.get_camera_rotation()),
            component_location=str(native_camera.get_world_location()), component_rotation=str(native_camera.get_world_rotation()),
            camera_position_error_cm=distance, control_rotation=str(pc.get_control_rotation()),
            carry_mesh_visible=carry_mesh.is_visible(), carry_mesh_hidden=carry_mesh.get_editor_property('hidden_in_game'),
            held_bounds=str(box.get_actor_bounds(False)),
            hands=[dict(target=str(pawn.get_carry_hand_location(index)), bone=str(carry_mesh.get_socket_location(bone)))
                   for index, bone in enumerate(('hand_l', 'hand_r'))])
        report['first_person_capture_diagnostic'] = diagnostic
        check('first_person_capture_uses_actual_player_camera_and_held_object', **diagnostic)
        assert view_target == pawn and pawn.get_held_cargo() == box, diagnostic
        assert distance < 2, diagnostic
        path = OUT/('FirstPerson_1000kgCarry_'+str(time.time_ns())+'.png')
        unreal.SystemLibrary.execute_console_command(game, 'HighResShot 1920x1080 filename='+path.as_posix(), pc)
        began_capture = time.monotonic()
        while not png_ready(path):
            assert time.monotonic()-began_capture < 45, 'First-person carry PNG did not finish'
            yield
        info = png_ready(path)
        assert info['width'] >= 1600 and info['height'] >= 900, info
        info.update(view='Actual player first-person camera', cargo_mass_kg=1000,
                    temporary_fixture_lighting=True, requires_visual_review=True)
        report['screenshots'].append(info)
        pc.un_possess()
        ai.possess(pawn)
        ai.set_editor_property('set_control_rotation_from_pawn_orientation', False)
        ai.set_control_rotation(unreal.Rotator())
        yield from wait(.1)
        enter('1000kg_box_placement')
    began = now
    assert pawn.try_place_on_pallet(support, support.get_actor_location()+unreal.Vector(0, 0, 15))
    while pawn.get_held_cargo() and now-began < 3.3:
        yield
    assert not pawn.get_held_cargo() and not pawn.is_placing_cargo()
    elapsed = now-began
    assert elapsed == 0, ('Normal placement still waits for animation', elapsed)
    assert pawn.get_carry_blend() == 0, 'Drop hand pose did not reset immediately'
    yield from wait(.8)
    dynamic(box, 1000)
    assert box in strength.get_supported_actors(support, True), '1000 kg box not supported by pallet'
    assert abs(box.get_actor_bounds(False)[0].x-support.get_actor_location().x) < 4
    check('1000kg_box_places_on_actual_pallet_and_returns_to_chaos', seconds=elapsed,
          mass_kg=body(box).get_mass(), supported_mass_kg=strength.get_supported_mass(support))

    enter('empty_pallet_stack')
    held, target = named['PalletHeld'], named['PalletSupport']
    relocate((-160, 600, 100))
    yield from wait(.2)
    assert pawn.try_pickup_pallet(held)
    yield from wait(.3)
    assert pawn.get_held_cargo() == held
    began = now
    assert pawn.try_place_on_pallet(target, target.get_actor_location()+unreal.Vector(0, 0, 15))
    while pawn.get_held_cargo() and now-began < 3.3:
        yield
    elapsed = now-began
    assert not pawn.get_held_cargo() and elapsed == 0
    yield from wait(1)
    dynamic(held, 25)
    dynamic(target, 25)
    low, high = target.get_actor_bounds(False), held.get_actor_bounds(False)
    assert abs(high[0].z-high[1].z-(low[0].z+low[1].z)) < 2.5, (low, high)
    assert (unreal.Vector(high[0].x, high[0].y, 0)-unreal.Vector(low[0].x, low[0].y, 0)).length() < 2
    check('empty_pallet_snaps_to_real_pallet_then_dynamic_stack_remains', seconds=elapsed,
          upper_bottom_cm=high[0].z-high[1].z, lower_top_cm=low[0].z+low[1].z)

    enter('instant_extraction')
    trapped = named['TrappedBox']
    relocate((-160, 1400, 100))
    yield from wait(.2)
    began = now
    assert pawn.try_pickup_cargo(trapped), 'A clear final hand pose must not be blocked by the old extraction animation path'
    assert pawn.get_held_cargo() == trapped and now-began == 0
    assert trapped.get_actor_bounds(False)[0].x < -45, 'Instant extraction stayed in its old cage'
    assert pawn.try_place_on_surface(unreal.Vector(-90,1400,0))
    assert not pawn.get_held_cargo() and not pawn.is_placing_cargo()
    elapsed = now-began
    assert elapsed == 0
    dynamic(trapped, 1000)
    assert abs(movement.get_editor_property('mass')-80) < .2
    assert abs(trapped.get_actor_bounds(False)[0].x+90) < 3
    origin = pawn.get_actor_location()
    yield from wait(.3, lambda: pawn.do_move(0, -1))
    assert (pawn.get_actor_location()-origin).length() > 50 and not pawn.is_physical_ragdoll()
    check('old_animation_path_does_not_block_instant_pickup_and_ground_snap', seconds=elapsed)

    enter('blocked_rotation')
    rotating = named['RotationPallet']
    relocate((-180, 2100, 100))
    yield from wait(.2)
    assert pawn.try_pickup_pallet(rotating)
    yield from wait(.3)
    assert pawn.get_held_cargo() == rotating
    named['RotationBlocker'].set_actor_location(unreal.Vector(-180, 2193, 130), False, True)
    ai.set_control_rotation(unreal.Rotator(yaw=90))
    began = now
    yield from wait(.3)
    assert_yaw(90)
    elapsed = now-began
    assert pawn.get_held_cargo() == rotating, 'Closer hand pose should fit in front of the wall'
    center, extent = rotating.get_actor_bounds(False)
    pose = body(rotating).get_world_transform()
    # The pallet's axis-aligned bounds include empty corners when partly turned.
    # Query the real convex bodies, allowing their ordinary contact skin only.
    pose.scale3d *= unreal.Vector(.998,.998,.998)
    overlaps = unreal.SystemLibrary.component_overlap_components(body(rotating), pose,
        [getattr(unreal.ObjectTypeQuery,'OBJECT_TYPE_QUERY'+str(i)) for i in range(1,7)],
        unreal.PrimitiveComponent, [pawn, rotating]) or []
    assert not any(part.get_owner()==named['RotationBlocker'] for part in overlaps), 'Held pallet penetrates the actual rotation blocker'
    assert pawn.try_place_on_surface(center)
    assert not pawn.get_held_cargo()
    dynamic(rotating, 25)
    assert abs(movement.get_editor_property('mass')-80) < .2
    check('pallet_turn_uses_nearer_clear_hand_pose_without_knockdown', seconds=elapsed,
          requested_yaw=90, controller_yaw=ai.get_control_rotation().yaw, player_yaw=pawn.get_actor_rotation().yaw)

    enter('wrapped_group')
    wrapped, load = named['WrappedPallet'], named['WrappedBox']
    deadline = now+6
    while wrapped.get_editor_property('receipt_state') != unreal.WarehouseReceiptState.CONFIRMED and now < deadline:
        yield
    assert wrapped.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED
    assert load.get_wrapped_pallet() == wrapped and load in wrapped.get_wrapped_cargo()
    joints = list(wrapped.get_components_by_class(unreal.PhysicsConstraintComponent))
    assert joints and all(not joint.is_broken() for joint in joints)
    relocate((-160, 2900, 100))
    yield from wait(.2)
    assert pawn.try_pickup_cargo(load), 'Clicking wrapped carton should lift the whole unit'
    yield from wait(.3)
    assert pawn.get_held_cargo() == wrapped and set(pawn.get_held_cargo_group()) == {wrapped, load}
    for yaw in (90, 180, 0):
        ai.set_control_rotation(unreal.Rotator(yaw=yaw))
        yield from wait(.2)
        assert_yaw(yaw)
        assert pawn.get_held_cargo() == wrapped and load.get_wrapped_pallet() == wrapped

    # Editing is a real layout transaction. Reject direct cargo and expanded zone
    # content before any collision or pose can change, including a mixed unit.
    enter('held_layout_guard')
    ai.un_possess()
    pc.possess(pawn)
    pc.set_control_rotation(unreal.Rotator())
    pc.toggle_observer_view()
    yield from frames(2)
    assert pc.is_observer_view()
    layout = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseLayoutEditor)[0]
    poses = {a: a.get_actor_transform() for a in (wrapped, load)}
    layout.set_category(unreal.WarehouseLayoutCategory.CARGO)
    layout.set_selection([load])
    assert layout.get_editor_property('selection_count') == 0
    assert not layout.begin_drag(load, load.get_actor_location())
    # Held cargo is ineligible even for selection; direct actions can be refused
    # before a drag starts, without the expanded-group warning used below.
    assert not layout.rotate_selection(90)
    assert not layout.delete_selection()
    zone = unreal.WarehouseLayoutZone(name='HeldUnitGuard', centre=wrapped.get_actor_location(),
        half_size=unreal.Vector(160, 160, 150), members=[wrapped])
    layout.set_editor_property('zones', [zone])
    layout.set_category(unreal.WarehouseLayoutCategory.ZONE)
    assert not layout.begin_drag(None, wrapped.get_actor_location())
    assert '내려놓' in layout.get_editor_property('status'), layout.get_editor_property('status')
    for actor, expected in poses.items():
        assert (actor.get_actor_location()-expected.translation).length() < .01
        assert not body(actor).is_simulating_physics() and body(actor).get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
    pc.toggle_observer_view()
    yield from frames(2)
    assert not pc.is_observer_view()
    check('held_wrapped_unit_rejects_direct_move_rotate_delete_and_zone_group_move')

    enter('wrapped_drop')
    pawn.drop_cargo()
    dynamic(wrapped, 25)
    dynamic(load, 12)
    assert all(not joint.is_broken() for joint in joints)
    yield from wait(.1)  # Before floor impact: validate restored bindings, not film strength.
    assert load.get_wrapped_pallet() == wrapped
    assert wrapped.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED
    assert all(not joint.is_broken() for joint in joints)
    check('wrapped_group_pickup_rotate_drop_preserves_binding_before_floor_impact',
          pallet_mass_kg=body(wrapped).get_mass(), cargo_mass_kg=body(load).get_mass(),
          later_impact_tearing_not_asserted=True)


def finalize():
    global finished, error
    if finished:
        return
    finished = True
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    unchanged = all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == value for path, value in hashes.items())
    if not unchanged:
        error = (error or '')+'\nOriginal map/player BP changed'
    report.update(passed=not error, phase=phase, error=error, source_files_unchanged=unchanged,
                  wall_seconds=round(time.monotonic()-started, 3))
    (OUT/'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('ARCADE_CARRY_BOUNDARY_PIE_'+('FAILED' if error else 'PASSED'), error or str(OUT/'Report.json'))
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    global busy, cleaning, error, now
    if busy or finished:
        return
    busy = True
    try:
        if cleaning:
            if not editor.get_game_world():
                finalize()
            return
        assert time.monotonic()-started < 180, ('Carry boundary timeout', phase)
        active = editor.get_game_world()
        if active:
            now = unreal.GameplayStatics.get_time_seconds(active)
        if pawn:
            held = pawn.get_held_cargo()
            report['recent_samples'].append(dict(phase=phase, sim_seconds=now,
                player=str(pawn.get_actor_location()), held=held.get_actor_label() if held else None,
                held_pose=str(held.get_actor_transform()) if held else None))
            report['recent_samples'] = report['recent_samples'][-12:]
        next(scenario)
    except StopIteration:
        cleaning = True
        level.editor_request_end_play()
    except Exception:
        error = traceback.format_exc()
        cleaning = True
        level.editor_request_end_play()
    finally:
        busy = False


scenario = scenario_body()
handle = unreal.register_slate_post_tick_callback(tick)
