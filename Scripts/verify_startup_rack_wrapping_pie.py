"""Saved-map initial rack stock, real film/joints and untouched loose stock.

No map saves, artificial wrap calls or physics overrides. Vehicle dispatch is
disabled only in the unsaved editor copy so initial inventory can be inspected.
"""
import hashlib
import json
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)

root = Path(unreal.Paths.project_dir())
out = root/'Saved/Testing/StartupRackWrapping'
out.mkdir(parents=True, exist_ok=True)
source = root/'Content/FirstPerson/Lvl_FirstPerson.umap'
original = hashlib.sha256(source.read_bytes()).hexdigest()
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not editor.get_game_world()
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.WarehouseForklift):
        actor.set_editor_property('auto_start', False)
        actor.set_editor_property('idle_parking_enabled', False)
    elif isinstance(actor, unreal.WarehouseInboundZone):
        actor.set_editor_property('fleet', [])

began = time.monotonic()
phase = 'start'
busy = False
handle = None
stock = []
report = dict(passed=False, checks=[], scope='Saved warehouse startup; native first-frame wrapping, contacts and finite joints; no transport or OS keys')


def finish(error=None):
    global phase
    phase = 'done'
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unchanged = hashlib.sha256(source.read_bytes()).hexdigest() == original
    report.update(passed=error is None and unchanged, error=error, map_unchanged=unchanged,
                  wall_seconds=time.monotonic()-began)
    (out/'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('STARTUP_RACK_WRAPPING_'+('PASSED' if report['passed'] else 'FAILED'), json.dumps(report, ensure_ascii=False))
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def tick(_dt):
    global busy, phase, stock
    if busy or phase == 'done':
        return
    busy = True
    try:
        assert time.monotonic()-began < 180, 'Startup wrap test timed out'
        world = editor.get_game_world()
        if not world:
            return
        now = unreal.GameplayStatics.get_time_seconds(world)
        if phase == 'start':
            strength = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WarehouseDamageSystem)[0]
            strength.get_supported_actors(None, True)
            racks = set()
            for spec in strength.get_editor_property('objects'):
                if spec.failure == unreal.WarehouseFailure.RACK and not spec.failed:
                    members = [a for a in spec.members if a]
                    if members:
                        racks.update(a for a in strength.get_supported_actors(members[0], False)
                                     if isinstance(a, unreal.WarehousePallet))
            assert racks, 'No real rack pallet support relationships'
            empty = loose = cargo_count = 0
            for pallet in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WarehousePallet):
                loads = strength.get_supported_actors(pallet, False)
                cartons = [a for a in loads if isinstance(a, unreal.WarehouseCargo)]
                state = pallet.get_editor_property('receipt_state')
                if pallet in racks and cartons and not any(isinstance(a, unreal.WarehousePallet) for a in loads):
                    assert state == unreal.WarehouseReceiptState.CONFIRMED, ('Rack stock not wrapped', pallet.get_actor_label(), state)
                    assert not pallet.get_editor_property('wrapping'), 'Initial stock plays the inbound animation'
                    assert set(pallet.get_wrapped_cargo()) == set(cartons), ('Wrong carton membership', pallet.get_actor_label())
                    film = pallet.get_components_by_class(unreal.SplineMeshComponent)
                    joints = pallet.get_components_by_class(unreal.PhysicsConstraintComponent)
                    assert film and len(joints) == len(cartons), ('Film/joints missing', pallet.get_actor_label())
                    assert all(p.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION for p in film)
                    assert all(a.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics() for a in [pallet]+cartons)
                    stock.append((pallet, cartons))
                    cargo_count += len(cartons)
                elif not cartons:
                    assert state == unreal.WarehouseReceiptState.PENDING, ('Empty stock wrapped', pallet.get_actor_label())
                    empty += 1
                elif pallet not in racks:
                    assert state == unreal.WarehouseReceiptState.PENDING, ('Loose/inbound stock pre-confirmed', pallet.get_actor_label())
                    loose += 1
            assert stock and empty and loose, ('Missing saved-map test cases', len(stock), empty, loose)
            report['checks'].append(dict(name='initial_rack_units_only', pallets=len(stock), cartons=cargo_count,
                                         empty_pending=empty, loose_pending=loose, simulation_seconds=now))
            phase = 'settle'
        elif phase == 'settle' and now >= 1.5:
            for pallet, cartons in stock:
                assert pallet.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED, ('Initial film tore without imposed impact', pallet.get_actor_label())
                assert set(pallet.get_wrapped_cargo()) == set(cartons)
                assert all(not joint.is_broken() for joint in pallet.get_components_by_class(unreal.PhysicsConstraintComponent))
            report['checks'].append(dict(name='native_settling_keeps_initial_units', pallets=len(stock), simulation_seconds=now))
            finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        busy = False


handle = unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
