"""Receiving FIFO, finite worker capacity, mismatch quarantine and explicit reinspection.

Creates a disposable PIE fixture. No authored level is saved; no parcel is
teleported or repaired by the receiving workflow. Run after the native build.
"""
import json
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)


def spawn(cls, name, xyz):
    result = actors.spawn_actor_from_class(cls, unreal.Vector(*xyz))
    result.set_actor_label(name)
    return result


floor = spawn(unreal.StaticMeshActor, 'ReceivingFloor', (250, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(40, 20, .2))
floor.static_mesh_component.set_collision_profile_name('BlockAll')
spawn(unreal.PlayerStart, 'ReceivingPlayer', (-1100, -700, 125))
zone = spawn(unreal.WarehouseInboundZone, 'ReceivingZone', (225, 0, 0))
zone.set_editor_property('half_size_cm', unreal.Vector(1100, 400, 80))
zone.set_editor_property('inspection_seconds', 3)
zone.set_editor_property('inspection_workers', 1)
overlap_check = '-receivingoverlapcheck' in unreal.SystemLibrary.get_command_line().lower()
if overlap_check:
    second_zone = spawn(unreal.WarehouseInboundZone, 'ReceivingOverlap', (225, 0, 0))
    second_zone.set_editor_property('half_size_cm', unreal.Vector(1100, 400, 80))
    second_zone.set_editor_property('inspection_seconds', 30)
    second_zone.set_editor_property('inspection_workers', 1)
fixtures = []
for index, x in enumerate((-450, 0, 450, 900)):
    pallet = spawn(unreal.WarehousePallet, f'ReceivingPallet{index}', (x, 0, .1))
    pallet.set_editor_property('expected_carton_count', 2 if index == 1 else 1)
    if index == 2:
        pallet.set_editor_property('expected_cargo_by_kind', {'다른 상품': 1})
    box = spawn(unreal.WarehouseCargo, f'ReceivingCarton{index}', (x, 0, 31))
    box.set_cargo_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    box.set_actor_scale3d(unreal.Vector(.4, .4, .3))
    box.set_gross_mass_kg(12)
    box.set_editor_property('cargo_kind', '검수 테스트 상품')
    box.set_editor_property('cargo_id', f'RECEIVING-{index}')
    fixtures.extend((pallet, box))
strength = spawn(unreal.WarehouseDamageSystem, 'ReceivingStrength', (0, -800, 0))
strength.set_editor_property('objects', [unreal.WarehouseStrength(
    name=a.get_actor_label(), members=[a], mass_kg=25 if isinstance(a, unreal.WarehousePallet) else 12,
    rated_load_kg=2000, impact_yield_j=200, impact_failure_j=1000,
    failure=unreal.WarehouseFailure.RIGID) for a in fixtures])
out = Path(unreal.Paths.project_saved_dir()) / 'Testing/ReceivingInspection'
out.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
phase = 'start'
live = None
checks = []
inspection_order = []
first = {}
initial_poses = {}


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def finish(ok, message):
    report = {'passed': ok, 'checks': checks, 'inspection_order': inspection_order, 'message': message}
    (out / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('RECEIVING_INSPECTION_' + ('PASSED' if ok else 'FAILED'), message)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    global phase, live
    try:
        assert time.monotonic() - started < 300, ('timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'settle'
            return
        world = editor.get_game_world()
        if not world:
            return
        if live is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)}
        z, damage = live['ReceivingZone'], live['ReceivingStrength']
        pallets = [live[f'ReceivingPallet{i}'] for i in range(4)]
        boxes = [live[f'ReceivingCarton{i}'] for i in range(4)]
        now = unreal.GameplayStatics.get_time_seconds(world)
        state = [p.get_editor_property('receiving_state') for p in pallets]
        assert z.get_editor_property('active_inspection_count') <= 1
        for i, s in enumerate(state):
            if s == unreal.WarehouseReceivingState.INSPECTING and i not in first:
                first[i] = now
                inspection_order.append(i)
        for i, box in enumerate(boxes):
            assert str(box.get_editor_property('cargo_id')) == f'RECEIVING-{i}'
            assert abs(body(box).get_mass() - 12) < .1
            assert body(box).is_simulating_physics() and body(box).is_gravity_enabled()
            assert not box.get_attach_parent_actor()
        if phase == 'settle' and now > 1.5:
            for i, p in enumerate(pallets):
                initial_poses[i] = p.get_actor_location()
            damage.apply_impact(boxes[3], 2000)
            assert damage.has_failed(boxes[3])
            assert not z.set_expected_manifest(pallets[0], 99, {})
            unreal.GameplayStatics.set_global_time_dilation(world, 4)
            checks.append('non-quarantined manifest edits rejected; deterministic damaged parcel')
            phase = 'inspect'
        elif phase == 'inspect' and state[0] == unreal.WarehouseReceivingState.ACCEPTED and all(
                s == unreal.WarehouseReceivingState.QUARANTINED for s in state[1:]):
            assert inspection_order == [0, 1, 2, 3], inspection_order
            assert pallets[0].get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED
            assert 'RECEIVING_COUNT' in pallets[1].get_editor_property('receiving_status')
            assert 'RECEIVING_KIND' in pallets[2].get_editor_property('receiving_status')
            assert 'RECEIVING_DAMAGED' in pallets[3].get_editor_property('receiving_status')
            for i, p in enumerate(pallets):
                assert (p.get_actor_location() - initial_poses[i]).length() < 5
            assert z.get_editor_property('dispatched_count') == 0
            checks.append('FIFO capacity, manifest checks and physical quarantine without transport')
            # A transported unit cannot be edited or released from quarantine.
            previous_tags = list(pallets[1].tags)
            pallets[1].tags = previous_tags + ['LogisticsInTransit']
            assert not z.request_reinspection(pallets[1])
            assert not z.set_expected_manifest(pallets[1], 1, {})
            pallets[1].tags = previous_tags
            assert not z.request_reinspection(pallets[3]), 'reinspection must not repair damage'
            assert z.set_expected_manifest(pallets[1], 1, {})
            assert z.set_expected_manifest(pallets[2], 1, {'검수 테스트 상품': 1})
            assert pallets[1].get_editor_property('receiving_state') == unreal.WarehouseReceivingState.QUARANTINED
            assert z.request_reinspection(pallets[1]) and z.request_reinspection(pallets[2])
            assert all(p.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.PENDING for p in pallets[1:3])
            checks.append('explicit correction/reinspection; transport and unrepaired damage gates')
            phase = 'reinspect'
        elif phase == 'reinspect' and all(s == unreal.WarehouseReceivingState.ACCEPTED for s in state[:3]):
            assert state[3] == unreal.WarehouseReceivingState.QUARANTINED
            assert all(p.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED for p in pallets[:3])
            assert z.get_editor_property('confirmed_count') == 3
            if overlap_check:
                if live['ReceivingOverlap'].get_editor_property('active_inspection_count'):
                    return
                checks.append('overlapping receiving zone releases worker slots after another zone accepts/quarantines its stock')
            checks.append('reinspection completes normal one-second physical wrapping at 4x simulation time')
            finish(True, 'Receiving inspection, quarantine and reinspection verified in disposable PIE fixture.')
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
