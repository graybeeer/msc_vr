"""Native receiving, breakable unit loads, rack delivery and layout rollback.

Only fixture construction/layout transactions set poses. During transport and
tear tests Chaos owns every body; impacts use real finite impulses.
"""
import json
from pathlib import Path
import runpy
import sys
import time
import traceback
import unreal

sys.path.insert(0, str(Path(__file__).parent))
from apply_real_world_scale import fit
from configure_inbound_area import RACK_CENTRES

if not unreal.EditorAssetLibrary.does_asset_exist('/Game/Warehouse/Materials/M_PalletWrap'):
    runpy.run_path(str(Path(__file__).with_name('prepare_pallet_wrap.py')))
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
saved = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
rig = {c.get_name(): (c.get_relative_transform(), c.static_mesh, list(c.get_editor_property('override_materials')))
       for c in saved['WH_AutonomousForklift'].get_components_by_class(unreal.StaticMeshComponent)}
rack_data = [(a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d(), a.static_mesh_component.static_mesh,
              list(a.static_mesh_component.get_editor_property('override_materials')))
             for a in [saved['WH_InboundRack_00_' + p] for p in list('ABCDEFGH') + ['Deck_20', 'Deck_140']]]
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property('default_game_mode',
    unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))


def spawn(cls, label, point):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
    actor.set_actor_label(label)
    return actor


floor = spawn(unreal.StaticMeshActor, 'WrapFloor', (0, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(60, 40, .2))
floor.static_mesh_component.set_collision_profile_name('BlockAll')
spawn(unreal.PlayerStart, 'WrapStart', (-2000, -1500, 125))
vehicle = spawn(unreal.WarehouseForklift, 'WrapAGV', (0, 0, .1))
vehicle.set_editor_property('auto_start', True)
vehicle.set_editor_property('pending_jobs', [])
vehicle.set_editor_property('idle_parking_enabled', False)
for part in vehicle.get_components_by_class(unreal.StaticMeshComponent):
    transform, mesh, materials = rig[part.get_name()]
    part.set_relative_transform(transform, False, True)
    part.set_static_mesh(mesh)
    part.set_editor_property('override_materials', materials)
pallet = spawn(unreal.WarehousePallet, 'WrapPallet', (500, 0, .1))
empty = spawn(unreal.WarehousePallet, 'WrapEmpty', (500, 220, .1))
probe = spawn(unreal.WarehousePallet, 'WrapImpactPallet', (500, 900, .1))
mesh = unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2')
cargo = []
for name, point, size, mass in [('WrapBoxA', (500, -21, 30.25), (40, 38, 30), 12),
                               ('WrapBoxB', (500, 21, 30.25), (40, 38, 30), 18),
                               ('WrapImpactBox', (500, 900, 30.25), (40, 38, 30), 5)]:
    box = spawn(unreal.WarehouseCargo, name, point)
    box.set_cargo_mesh(mesh)
    box.set_gross_mass_kg(mass)
    fit(box, size, point)
    cargo.append(box)
rack = []
for index, (point, rotation, scale, mesh, materials) in enumerate(rack_data):
    actor = spawn(unreal.StaticMeshActor, f'WrapRack{index}', (point.x - RACK_CENTRES[0][0] + 1200,
        point.y - RACK_CENTRES[0][1] + 62.5, point.z))
    actor.set_actor_rotation(rotation, False)
    actor.set_actor_scale3d(scale)
    actor.static_mesh_component.set_static_mesh(mesh)
    actor.static_mesh_component.set_editor_property('override_materials', materials)
    actor.static_mesh_component.set_collision_profile_name('BlockAll')
    actor.tags = ['LayoutType:Rack', 'LayoutGroup:WrapRack']
    rack.append(actor)
zone = spawn(unreal.WarehouseInboundZone, 'WrapZone', (500, 110, 0))
zone.set_editor_property('half_size_cm', unreal.Vector(180, 260, 80))
zone.set_editor_property('rack_slots', [unreal.WarehouseRackSlot(name='WrapRackLower', rack=rack[0],
    pose=unreal.Transform(location=unreal.Vector(1200, 0, 20)), clear_height_cm=110, capacity_kg=10)])
inspect = spawn(unreal.WarehouseInboundZone, 'WrapInspectionOnly', (500, 900, 0))
inspect.set_editor_property('half_size_cm', unreal.Vector(180, 180, 80))
strength = spawn(unreal.WarehouseDamageSystem, 'WrapStrength', (0, -1800, 0))
strength.set_editor_property('objects', [unreal.WarehouseStrength(name=a.get_actor_label(), members=[a], mass_kg=m,
    rated_load_kg=1500, impact_yield_j=200, impact_failure_j=1000)
    for a, m in [(pallet, 25), (empty, 25), (probe, 25)] + list(zip(cargo, [12, 18, 5]))] + [
    unreal.WarehouseStrength(name='WrapRack', members=rack, failure=unreal.WarehouseFailure.RACK, mass_kg=250, rated_load_kg=4000,
        level_capacity_kg=2000, shelf_heights_cm=[20, 140])])
console = spawn(unreal.WarehouseComputer, 'WrapConsole', (-1800, 1000, 0))
sun = spawn(unreal.DirectionalLight, 'WrapSun', (0, 0, 1800))
sun.set_actor_rotation(unreal.Rotator(pitch=-55, yaw=-30), False)
sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(4)
spawn(unreal.SkyAtmosphere, 'WrapSky', (0, 0, 0))
camera = spawn(unreal.CameraActor, 'WrapCamera', (700, -240, 220))
camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(60)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(500, 0, 30)), False)

out = Path(unreal.Paths.project_saved_dir()) / 'Testing/InboundWrap'
out.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
live = None
phase = 'start'
at = 0
last_log = -10
checks = []
peak_force = 0


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def check(name):
    checks.append(name)
    print('INBOUND_WRAP_CHECK_PASSED', name)


def finish(ok, message):
    (out / 'Report.json').write_text(json.dumps({'passed': ok, 'checks': checks, 'message': message}, ensure_ascii=False, indent=2), encoding='utf-8')
    print('INBOUND_WRAP_PIE_' + ('PASSED' if ok else 'FAILED'), message)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    global live, phase, at, last_log, peak_force
    try:
        assert time.monotonic() - started < 900, ('Timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'pending'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
        v, p, empty, probe, zone, inspect, strength, console = [live[n] for n in
            ('WrapAGV', 'WrapPallet', 'WrapEmpty', 'WrapImpactPallet', 'WrapZone', 'WrapInspectionOnly', 'WrapStrength', 'WrapConsole')]
        boxes = [live[n] for n in ('WrapBoxA', 'WrapBoxB', 'WrapImpactBox')]
        pc = unreal.GameplayStatics.get_player_controller(game, 0)
        if phase == 'gentle' and now - at < .5:
            body(boxes[2]).add_force(unreal.Vector(0, 0, 10000))  # 100 N exceeds this carton's weight, below film strength.
        if phase == 'tear':
            if now - at < .5:
                body(boxes[2]).add_force(unreal.Vector(0, 0, 300000))
            for joint in probe.get_components_by_class(unreal.PhysicsConstraintComponent):
                force, torque = joint.get_constraint_force()
                peak_force = max(peak_force, force.length() / 100)
        for actor, mass in [(p, 25), (probe, 25)] + list(zip(boxes, [12, 18, 5])):
            assert body(actor).is_simulating_physics() and not actor.get_attach_parent_actor()
            assert body(actor).is_gravity_enabled() and abs(body(actor).get_mass() - mass) < .1
        assert v.get_editor_property('ai_state') != unreal.WarehouseAIState.FAULT, v.get_editor_property('status')
        if now - last_log > 10:
            last_log = now
            print('INBOUND_WRAP_PROGRESS', phase, now, zone.get_editor_property('status'), v.get_editor_property('status'))
        if phase == 'pending' and now > .8:
            assert p.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.PENDING
            assert not v.submit_job(unreal.WarehouseWorkOrder(job_id='UNCONFIRMED', pallet=p,
                destination=unreal.Transform(location=unreal.Vector(1200, 0, 20))))
            assert 'INBOUND_PENDING' in v.validate_job_resources(unreal.WarehouseWorkOrder(pallet=p))
            assert pc.set_simulation_speed(4)
            pc.set_view_target_with_blend(live['WrapCamera'], 0)
            check('unconfirmed order rejected')
            phase = 'confirmation'
        elif phase == 'confirmation' and now > 6:
            assert p.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED
            assert probe.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED
            assert empty.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.PENDING
            assert set(p.get_wrapped_cargo()) == set(boxes[:2])
            assert all(b.get_wrapped_pallet() == p for b in boxes[:2])
            assert zone.get_editor_property('confirmed_count') == 1 and zone.get_editor_property('dispatched_count') == 0
            assert inspect.get_editor_property('confirmed_count') == 1 and not inspect.get_editor_property('rack_slots')
            film = p.get_components_by_class(unreal.SplineMeshComponent)
            assert len(film) >= 4 and all(c.is_visible() and c.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION for c in film)
            assert all(c.get_attach_parent() == body(p) for c in film), 'Film not attached to moving pallet'
            joints = p.get_components_by_class(unreal.PhysicsConstraintComponent)
            assert len(joints) == 2
            for joint in joints:
                part_a, bone_a, part_b, bone_b = joint.get_constrained_components()
                assert part_a == body(p) and part_b in [body(b) for b in boxes[:2]], 'Wrap joint missing real bodies'
            for unit in (p, probe):
                for joint in unit.get_components_by_class(unreal.PhysicsConstraintComponent):
                    profile = joint.get_editor_property('constraint_instance').get_editor_property('profile_instance')
                    limit = profile.get_editor_property('linear_limit')
                    assert limit.get_editor_property('limit') == 1.5 and not limit.get_editor_property('soft_constraint')
                    assert profile.get_editor_property('linear_breakable') and profile.get_editor_property('linear_break_threshold') == 150000
            assert all(not unreal.WarehouseDamageSystem.is_physical_contact(c) for c in film)
            assert all(c.get_material(0).get_path_name().startswith('/Game/Warehouse/Materials/M_PalletWrap') for c in film)
            assert abs(strength.get_supported_mass(p) - 30) < .1
            console.refresh_state()
            assert all('입고 확정' in row.receipt_status for row in console.get_editor_property('inventory') if row.pallet == p)
            unreal.SystemLibrary.execute_console_command(game, 'HighResShot 1600x900 filename=' + (out / 'Confirmed.png').as_posix(), pc)
            body(boxes[2]).add_impulse(unreal.Vector(200, 0, 0))
            at, phase = now, 'gentle'
            check('NPC confirms once; empty/over-capacity guards; rendering, inventory and masses')
        elif phase == 'gentle' and now - at > 1.5:
            assert probe.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED
            assert abs(boxes[2].get_actor_location().z - probe.get_actor_location().z - 30) < 6
            check('small impulse and sub-threshold separating force keep film intact')
            body(boxes[2]).add_impulse(unreal.Vector(0, 0, 15000))
            at, phase = now, 'tear'
        elif phase == 'tear' and now - at > 2:
            assert probe.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.WRAP_TORN, (
                'No tear', peak_force, probe.is_actor_tick_enabled(), boxes[2].get_actor_location(),
                probe.get_actor_location(), [j.is_broken() for j in probe.get_components_by_class(unreal.PhysicsConstraintComponent)])
            assert not probe.get_wrapped_cargo() and not boxes[2].get_wrapped_pallet()
            assert not probe.get_components_by_class(unreal.PhysicsConstraintComponent)
            assert (boxes[2].get_actor_location() - probe.get_actor_location()).length() > 60
            camera = live['WrapCamera']
            camera.set_actor_location(unreal.Vector(800, 400, 400), False, True)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), probe.get_actor_location() + unreal.Vector(0, 0, 25)), False)
            unreal.SystemLibrary.execute_console_command(game, 'HighResShot 1600x900 filename=' + (out / 'Torn.png').as_posix(), pc)
            check('strong separating force breaks film and releases independently simulated cargo')
            slots = list(zone.get_editor_property('rack_slots'))
            slots[0].capacity_kg = 2000
            zone.set_editor_property('rack_slots', slots)
            at, phase = now, 'transport'
        elif phase == 'transport':
            assert probe.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.WRAP_TORN, 'NPC silently resealed torn film'
            assert zone.get_editor_property('dispatched_count') <= 1, 'Duplicate dispatch'
            assert p.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED
            if zone.get_editor_property('completed_count') == 1:
                assert strength.get_supported_actors(live['WrapRack0']).count(p) == 1
                assert set(boxes[:2]) <= set(strength.get_supported_actors(p))
                assert abs(strength.get_supported_mass(p) - 30) < .1
                assert abs(strength.get_supported_mass(live['WrapRack0']) - 55) < .5
                assert zone.get_editor_property('confirmed_count') == 1
                camera = live['WrapCamera']
                camera.set_actor_location(unreal.Vector(1370, -240, 220), False, True)
                camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), p.get_actor_location() + unreal.Vector(0, 0, 25)), False)
                unreal.SystemLibrary.execute_console_command(game, 'HighResShot 1600x900 filename=' + (out / 'Stored.png').as_posix(), pc)
                check('one wrapped pallet / two cartons physically delivered to rack without changing mass')
                layout = next(iter(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseLayoutEditor)))
                layout.set_editing(True, 0)
                layout.set_category(unreal.WarehouseLayoutCategory.CARGO)
                layout.set_editor_property('edge_snap_enabled', False)
                before = [a.get_actor_transform() for a in [p] + boxes[:2]]
                point = boxes[0].get_actor_location()
                assert layout.begin_drag(boxes[0], point)
                layout.update_drag(point + unreal.Vector(0, 300, 0))
                moved = p.get_actor_location() - before[0].translation
                assert moved.length() > 100
                for actor, pose in zip(boxes[:2], before[1:]):
                    assert ((actor.get_actor_location() - pose.translation) - moved).length() < .1
                layout.end_drag(False)
                layout.set_editing(False, 0)
                assert all((a.get_actor_location() - pose.translation).length() < .1 for a, pose in zip([p] + boxes[:2], before))
                at, phase = now, 'restored'
                check('layout moves whole wrapped unit and rolls back its physical joints')
            assert now - at < 500, 'Rack delivery timeout'
        elif phase == 'restored' and now - at > 2:
            assert p.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED
            assert set(p.get_wrapped_cargo()) == set(boxes[:2])
            check('restored package remains intact under native physics')
            finish(True, 'NPC receiving, film rendering/retention/tearing, rack transport, inventory and layout passed')
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
