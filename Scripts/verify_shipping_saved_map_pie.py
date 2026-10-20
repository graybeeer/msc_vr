"""Saved warehouse stock/manifest/UI validation; save only after a successful PIE.

-ApplyShippingMap applies configure_shipping_system.configure() in the editor
world first. Failure never saves those edits. No AGV transport is simulated by
this UI test; only the operator is moved in order to use the real computer.
For the 1600x900 UI capture, create Saved/Testing/ShippingExternalPIE.flag and
start EditorAppToolset.StartPIE with PlayMode_InEditorFloating after the
SHIPPING_EXTERNAL_PIE_READY marker. Remove the flag after testing. Then this
test receives all six existing truck units and waits for the empty truck exit.
"""
import json
from pathlib import Path
import struct
import sys
import time
import traceback
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from configure_shipping_system import LEVEL, configure

command = unreal.SystemLibrary.get_command_line().lower()
APPLY = '-applyshippingmap' in command
KEEP = '-keeptesteditoropen' in command
SKIP_CAPTURE = '-shippingskipuicapture' in command
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
out = Path(unreal.Paths.project_saved_dir()) / 'Testing/ShippingSavedMap'
out.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
external_start = (Path(unreal.Paths.project_saved_dir()) / 'Testing/ShippingExternalPIE.flag').exists()
play_settings = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.LevelEditorPlaySettings'))
window_size = (play_settings.get_editor_property('NewWindowWidth'), play_settings.get_editor_property('NewWindowHeight'))
phase = 'start'
at = 0
handle = None
live = None
pc = None
computer = None
checks = []
report = {'passed': False, 'applied': False, 'checks': checks, 'rendered_ui_capture': not SKIP_CAPTURE}
report_path = out / ('NullRHIReport.json' if SKIP_CAPTURE else 'Report.json')
original = {}
driver_states = []
incoming = None
inbound_id = None
last_unloaded = 0
last_unload_at = -1
received_units = []
snapshot_path = out / 'OriginalCargo.json'
image_path = out / 'UIScreen.png'


def vec(v):
    return tuple(round(getattr(v, axis), 5) for axis in 'xyz')


def snapshot(actor):
    packing = actor.get_editor_property('packing')
    body = actor.get_component_by_class(unreal.StaticMeshComponent)
    # Every current WarehouseCargoRecipe field is included, not just the seed.
    recipe = {name: packing.get_editor_property(name) for name in (
        'valid', 'rule_version', 'delivery_seed', 'item_index', 'product_index', 'amount_scale',
        'packed_density_kg_m3', 'net_mass_kg', 'packaging_mass_kg', 'gross_mass_kg', 'fragile')}
    recipe.update(product_kind=str(packing.product_kind), size_cm=vec(packing.size_cm), size_limit_cm=vec(packing.size_limit_cm))
    return {'id': str(actor.get_editor_property('cargo_id')), 'kind': str(actor.get_editor_property('cargo_kind')),
        'mass_kg': actor.get_editor_property('gross_mass_kg'), 'mesh': body.static_mesh.get_path_name(),
        'mesh_world_scale': vec(body.get_world_transform().scale3d), 'packing': recipe}


def cargo_snapshots(named):
    return {name: snapshot(a) for name, a in named.items()
            if name.startswith('TRK_Cargo_') and isinstance(a, unreal.WarehouseCargo)}


def check(name):
    checks.append(name)
    print('SHIPPING_SAVED_MAP_CHECK_PASSED', name)


def validate_dock_clearance(named, trucks, stage):
    bridges = {a for a in named.values() if a.actor_has_tag('LogisticsFixedDock')
               and any(unreal.WarehouseDamageSystem.is_physical_contact(p)
                       for p in a.get_components_by_class(unreal.PrimitiveComponent))}
    assert len(bridges) == 3, ('Missing physical fixed dock bridges', stage, len(bridges))
    cartons = [named[label] for label in original]
    pallets = [p for truck in trucks for p in truck.get_editor_property('initial_pallets')]
    assert len(cartons) == 48 and len(pallets) == 6 and len(set(pallets)) == 6
    parts_checked = 0
    for actor in cartons+pallets:
        parts = [p for p in actor.get_components_by_class(unreal.PrimitiveComponent)
                 if unreal.WarehouseDamageSystem.is_physical_contact(p)]
        assert parts, ('Truck stock has no physical body', stage, actor.get_actor_label())
        for part in parts:
            overlaps = unreal.SystemLibrary.component_overlap_components(part, part.get_world_transform(),
                [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2],
                unreal.PrimitiveComponent, [actor]) or []
            hits = [(p.get_owner().get_actor_label(), p.get_name()) for p in overlaps if p.get_owner() in bridges]
            assert not hits, ('Truck stock overlaps a fixed dock bridge', stage, actor.get_actor_label(), part.get_name(), hits)
            parts_checked += 1
    report.setdefault('dock_clearance', {})[stage] = {'cartons': len(cartons), 'pallets': len(pallets),
                                                    'physical_parts': parts_checked, 'fixed_bridges': len(bridges)}
    check(stage+' native clearance: all 48 cartons and six pallet bodies avoid all three fixed dock bridges')


def complete(ok, message):
    report.update(passed=ok, message=message, wall_seconds=round(time.monotonic() - started, 2))
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('SHIPPING_SAVED_MAP_PIE_' + ('PASSED' if ok else 'FAILED'), message)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    if not KEEP:
        unreal.SystemLibrary.quit_editor()


def request_finish(ok, message):
    global phase, at
    report.update(candidate_passed=ok, message=message)
    if pc and pc.is_warehouse_computer_open():
        pc.close_warehouse_computer()
    level.editor_request_end_play()
    phase, at = 'end_play', time.monotonic()


def validate_editor(named):
    assert 'WH_ShippingSystem' in named, 'Saved warehouse is not configured; launch with -ApplyShippingMap to apply after validation'
    system = named['WH_ShippingSystem']
    trucks = list(system.get_editor_property('trucks'))
    assert len(trucks) == 3 and len(set(trucks)) == 3
    zones = list(system.get_editor_property('zones'))
    inbound = next(z for z in zones if z.inbound)
    outbound = next(z for z in zones if not z.inbound)
    assert inbound.anchor and outbound.anchor and inbound.zone_id != outbound.zone_id
    assert len(inbound.slots) >= 6, ('Inbound handover capacity is smaller than its six-pallet manifest', len(inbound.slots))
    assert len(outbound.slots) >= 1
    assert len(trucks[1].get_editor_property('initial_pallets')) == 6
    assert not trucks[0].get_editor_property('initial_pallets') and not trucks[2].get_editor_property('initial_pallets')
    assert all(len(t.get_editor_property('load_slots')) == 6 and t.get_editor_property('dock_anchor') for t in trucks)
    assert len({str(t.get_editor_property('truck_id')) for t in trucks}) == 3
    validate_dock_clearance(named, trucks, 'editor')
    report.update(inbound_slots=len(inbound.slots), outbound_slots=len(outbound.slots), original_cartons=len(original))
    check('three trucks, independent dock assignments and enough receiving manifest slots')


def tick(dt):
    global live, pc, computer, phase, at, incoming, inbound_id, last_unloaded, last_unload_at
    try:
        if phase == 'saving':
            return  # Saving packages pumps Slate and can re-enter this callback.
        assert time.monotonic() - started < 1200, ('Saved-map validation timeout', phase)
        if phase == 'end_play':
            if editor.get_game_world() or time.monotonic() - at < 1:
                return
            phase = 'saving'
            for actor, auto_start, idle_parking in driver_states:
                actor.set_editor_property('auto_start', auto_start)
                actor.set_editor_property('idle_parking_enabled', idle_parking)
            play_settings.set_editor_property('NewWindowWidth', window_size[0])
            play_settings.set_editor_property('NewWindowHeight', window_size[1])
            if not report.get('candidate_passed'):
                complete(False, report['message'])
                return
            assert editor.get_editor_world().get_path_name().startswith(LEVEL + '.'), 'Refusing to save a test world'
            assert cargo_snapshots({a.get_actor_label(): a for a in actors.get_all_level_actors()}) == original
            if APPLY:
                assert level.save_current_level(), 'Failed to save validated warehouse level'
                assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True), 'Failed to save validated external actor packages'
                report['applied'] = True
                check('successful PIE ended before validated editor-world packages were saved')
            complete(True, report['message'])
            return
        if phase == 'start':
            if external_start:
                play_settings.set_editor_property('NewWindowWidth', 1600)
                play_settings.set_editor_property('NewWindowHeight', 900)
                print('SHIPPING_EXTERNAL_PIE_READY: start an InEditorFloating PIE through EditorAppToolset')
            else:
                level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            assert pc.set_simulation_speed(4)
        if phase == 'settle' and now >= 2:
            assert cargo_snapshots(live) == original, 'Runtime initialisation changed original parcel identities/recipes/masses/models/scales'
            system = live['WH_ShippingSystem']
            trucks = list(system.get_editor_property('trucks'))
            if not all(t.is_manifest_ready() for t in trucks):
                damage = live['WH_DamageSystem']
                for truck in trucks:
                    for i, pallet in enumerate(truck.get_editor_property('initial_pallets')):
                        slot = unreal.MathLibrary.compose_transforms(truck.get_editor_property('load_slots')[i], truck.get_actor_transform())
                        members = [pallet] + list(damage.get_supported_actors(pallet))
                        carried = list(truck.get_unit_cargo(pallet))
                        points = []
                        for a in members:
                            c, e = a.get_actor_bounds(True)
                            points += [slot.inverse_transform_location(c + unreal.Vector(x*e.x,y*e.y,z*e.z)) for x in (-1,1) for y in (-1,1) for z in (-1,1)]
                        bounds = [(axis, round(min(getattr(p,axis) for p in points),2),round(max(getattr(p,axis) for p in points),2)) for axis in 'xyz']
                        print('SHIPPING_INITIAL_UNIT_BOUNDS', i, len(members)-1, len(carried), bounds, pallet.get_actor_rotation(), [a.get_actor_label() for a in members[1:]])
            assert len(trucks) == 3 and all(t.is_manifest_ready() for t in trucks), [(t.get_editor_property('truck_id'), str(t.get_editor_property('state')), t.get_editor_property('status'), len(t.get_editor_property('manifest_pallets'))) for t in trucks]
            assert all(t.is_ready() for t in trucks), [(t.get_actor_label(), t.get_editor_property('status')) for t in trucks]
            incoming = next(t for t in trucks if t.get_editor_property('truck_id') == 'TRUCK-02')
            manifest = list(incoming.get_editor_property('manifest_pallets'))
            assert len(manifest) == 6 and len(set(manifest)) == 6
            assert all(incoming.is_unit_on_truck(p) for p in manifest)
            contents = [a for p in manifest for a in incoming.get_unit_cargo(p)]
            assert len(contents) == 48 and len(set(contents)) == 48
            assert {a.get_actor_label() for a in contents} == set(original)
            assert all(len(incoming.get_unit_cargo(p)) == 8 for p in manifest)
            expected = sum(r['mass_kg'] for r in original.values()) + 150
            assert abs(incoming.payload_mass_kg() - expected) < .1, (incoming.payload_mass_kg(), expected)
            assert all(not t.get_editor_property('manifest_pallets') for t in trucks if t != incoming)
            assert all(not a.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics() for a in contents)
            assert all(a.actor_has_tag('LogisticsInTransit') for a in contents + manifest)
            validate_dock_clearance(live, trucks, 'runtime')
            assert not system.get_editor_property('shipments'), 'Map startup fabricated a shipping order'
            report.update(inbound_pallets=len(manifest), manifest_cartons=len(contents), payload_kg=incoming.payload_mass_kg())
            check('real saved warehouse: six original pallet units / 48 unique unchanged cartons, expected payload and three ready docks')
            computer = live['WH_ManagementComputer']
            computer.refresh_state()
            inventory = list(computer.get_editor_property('inventory'))
            assert not any(r.cargo in contents for r in inventory), 'Unreceived truck stock was exposed as usable warehouse inventory'
            pawn = pc.get_controlled_pawn()
            assert pawn and not pc.is_warehouse_computer_open()
            pc.set_view_target_with_blend(pawn, 0)
            # Only this operator positioning is synthetic. It proves UI use, not AGV transport.
            pawn.set_actor_location(computer.get_actor_location() - computer.get_actor_forward_vector()*210 + unreal.Vector(0, 0, 96.5), False, True)
            eye = pawn.get_component_by_class(unreal.CameraComponent).get_world_location()
            screen = computer.get_actor_transform().transform_location(unreal.Vector(8.7, 0, 111))
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye, screen))
            unreal.SystemLibrary.execute_console_command(game, 'r.SetRes 1600x900w', pc)
            unreal.SystemLibrary.execute_console_command(game, 'DisableAllScreenMessages', pc)
            at, phase = now, 'aim'
        elif phase == 'aim' and now - at > 1:
            pawn = pc.get_controlled_pawn()
            assert pawn.get_interaction_target() == computer, ('Saved computer aim was blocked', pawn.get_interaction_target())
            pawn.toggle_carry()
            assert pc.is_warehouse_computer_open() and pc.is_move_input_ignored() and pc.is_look_input_ignored()
            assert not unreal.GameplayStatics.is_game_paused(game)
            report['refresh_before_ui'] = computer.get_editor_property('refresh_count')
            at, phase = now, 'ui'
        elif phase == 'ui' and now - at > 2:
            assert pc.is_warehouse_computer_open() and computer.get_editor_property('refresh_count') > report['refresh_before_ui']
            # Shot showui captures the actual Slate shipment form and inbound button.
            if not SKIP_CAPTURE:
                unreal.SystemLibrary.execute_console_command(game, 'Shot showui filename=' + image_path.as_posix() + ' -nosuffix', pc)
            at, phase = now, 'image'
        elif phase == 'image' and now - at > 1:
            if not SKIP_CAPTURE:
                assert image_path.is_file() and image_path.stat().st_size > 10000, 'Missing rendered shipment UI'
                assert struct.unpack('>II', image_path.read_bytes()[16:24]) == (1600, 900), 'Shipment UI capture has the wrong resolution'
            pc.close_warehouse_computer()
            assert not pc.is_warehouse_computer_open() and not pc.is_move_input_ignored() and not pc.is_look_input_ignored()
            check('real computer E handler, live inventory/UI and player return' + ('; 1600x900 rendered screen' if not SKIP_CAPTURE else '; rendering disabled for data reload regression'))
            system = live['WH_ShippingSystem']
            inbound_zone = next(z.zone_id for z in system.get_editor_property('zones') if z.inbound)
            received_units.extend(incoming.get_editor_property('manifest_pallets'))
            assert system.start_inbound(incoming, inbound_zone), system.get_editor_property('last_message')
            inbound_id = system.get_editor_property('shipments')[-1].shipment_id
            phase, at = 'receive', now
        elif phase == 'receive':
            system = live['WH_ShippingSystem']
            shipment = next(s for s in system.get_editor_property('shipments') if s.shipment_id == inbound_id)
            count = sum(line.arrived for line in shipment.lines)
            if count != last_unloaded:
                assert count == last_unloaded + 1, 'Saved warehouse unloaded multiple units in one step'
                if last_unload_at >= 0:
                    assert now-last_unload_at >= incoming.get_editor_property('transfer_interval_seconds')-.1
                last_unloaded, last_unload_at = count, now
                print('SHIPPING_SAVED_MAP_UNLOADED', count, shipment.wait_reason)
            assert cargo_snapshots(live) == original, 'Saved-map unloading changed parcel identities/data/models/scales'
            for line in shipment.lines:
                if line.arrived:
                    assert not line.pallet.actor_has_tag('LogisticsInTransit')
                    assert line.pallet.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
            if shipment.state == unreal.WarehouseShipmentState.COMPLETED:
                assert count == 6 and not incoming.get_editor_property('manifest_pallets')
                assert incoming.get_editor_property('state') == unreal.WarehouseTruckState.DEPARTED
                assert all(not live[name].actor_has_tag('LogisticsInTransit') for name in original)
                assert all(live[name].get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics() for name in original)
                report.update(received_pallets=count, received_cartons=len(original))
                check('real warehouse receives all six units / 48 original cartons one at a time, restores physics and departs with empty manifest')
                request_finish(True, 'Saved warehouse 48 original cartons / six units, live computer UI and sequential full-truck receiving/departure passed')
            assert now-at < 90, ('Saved warehouse receiving timeout', shipment.wait_reason, count)
    except Exception:
        failure = traceback.format_exc()
        if phase in ('end_play', 'saving'):
            complete(False, failure)
        else:
            request_finish(False, failure)


try:
    unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
    assert level.load_level(LEVEL)
    original = cargo_snapshots({a.get_actor_label(): a for a in actors.get_all_level_actors()})
    assert len(original) == 48 and len({r['id'] for r in original.values()}) == 48
    if not APPLY and snapshot_path.exists():
        assert json.loads(json.dumps(original)) == json.loads(snapshot_path.read_text(encoding='utf-8')), 'Reloaded map changed original parcel data/models'
    snapshot_path.write_text(json.dumps(original, ensure_ascii=False, indent=2), encoding='utf-8')
    if APPLY:
        configure()
    named = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    assert cargo_snapshots(named) == original, 'Configuration changed parcel data/model/scale'
    validate_editor(named)
    # This test validates authored stock and UI, not warehouse route planning.
    # Native AGV transport is exercised separately by verify_shipping_workflow_pie.
    # Restore these editor values before any save, including on a failed check.
    for actor in named.values():
        if isinstance(actor, unreal.WarehouseForklift):
            driver_states.append((actor, actor.get_editor_property('auto_start'), actor.get_editor_property('idle_parking_enabled')))
            actor.set_editor_property('auto_start', False)
            actor.set_editor_property('idle_parking_enabled', False)
    report['autonomous_dispatch_disabled_for_ui_test'] = True
    if not SKIP_CAPTURE and image_path.exists():
        image_path.unlink()
    handle = unreal.register_slate_post_tick_callback(tick)
except Exception:
    complete(False, traceback.format_exc())
