"""Default automatic office flow with unchanged native forklift transport.

Reuses only the existing shipping test's unsaved fixture construction. Its test
driver is not executed. Player power commands, layout transactions and the
shipping system's normal truck handovers are the only runtime movement paths.
Never writes the warehouse map or the user's manual-operations preference.
"""
import ast
import json
from pathlib import Path
import time
import traceback

import unreal


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
fixture_path = Path(__file__).with_name('verify_shipping_workflow_pie.py')
fixture_tree = ast.parse(fixture_path.read_text(encoding='utf-8-sig'), str(fixture_path))
fixture_end = next(i for i, node in enumerate(fixture_tree.body)
                   if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'out' for t in node.targets))
fixture = {'__file__': str(fixture_path), '__name__': '__focus_fixture__'}
exec(compile(ast.Module(body=fixture_tree.body[:fixture_end], type_ignores=[]), str(fixture_path), 'exec'), fixture)
level, actors, editor, spawn = (fixture[n] for n in ('level', 'actors', 'editor', 'spawn'))

# One outbound unit and one generated inbound unit are enough to exercise both
# chains. Remove unrelated cancellation/multi-manifest probes before PIE.
removed = {'ExportBPallet', 'ExportBBox', 'OtherShipmentPallet', 'OtherShipmentBox',
           'ImportAPallet', 'ImportABox', 'ImportBPallet', 'ImportBBox',
           'DeletedInboundProbePallet', 'DeletedInboundProbeBox',
           'ShippingOtherTruck', 'ShippingOtherTruckDock', 'ShippingOtherTruckFloor',
           'ShippingMutationTruck', 'ShippingMutationTruckDock', 'ShippingMutationTruckFloor'}
strength = fixture['strength']
strength.set_editor_property('objects', [s for s in strength.get_editor_property('objects') if str(s.name) not in removed])
for actor in list(actors.get_all_level_actors()):
    if actor.get_actor_label() in removed:
        actors.destroy_actor(actor)
shipping, incoming, outgoing = (fixture[n] for n in ('shipping', 'imports', 'export'))
shipping.set_editor_property('trucks', [incoming, outgoing])
shipping.set_editor_property('repeating_inbound_truck', incoming)
shipping.set_editor_property('max_generated_units', 1)
shipping.set_editor_property('visit_interval_seconds', 5)
for truck in (incoming, outgoing):
    truck.set_editor_property('show_driver', False)
spawn(unreal.WarehouseFocusDirector, 'FocusDirector', (-2100, -700, 0))
temporary_anchor = spawn(unreal.TargetPoint, 'FocusTemporaryZone', (-2400, 1400, 0))
temporary_anchor.set_actor_rotation(unreal.Rotator(yaw=90), False)
temporary_anchor.tags = ['LayoutType:Zone', 'LayoutGroup:FocusTemporaryZone']

out = Path(unreal.Paths.project_saved_dir()) / 'Testing/ForkliftFocus'
out.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
phase = 'start'
at = 0
wall_at = started
last_log = -10
live = None
pc = director = None
checks = []
summary = []
identities = {}
shipping_zones = []
shipment_ids = set()
outbound_id = ''
outbound_visit = ''
handovers = set()
inspection_seen = set()
wrap_seen = False
report = {'passed': False, 'checks': checks, 'scope': 'Unsaved native fixture; no OS input or full saved-map UI assertions',
          'savedMapWritten': False, 'settingsPersisted': False, 'transportStateForced': False}


def check(message):
    checks.append(message)
    print('FORKLIFT_FOCUS_CHECK_PASSED', message)


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def finish(ok, message):
    report.update(passed=ok, message=message, wall_seconds=round(time.monotonic() - started, 2), summaries=summary)
    (out / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('FORKLIFT_FOCUS_PIE_' + ('PASSED' if ok else 'FAILED'), message)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def enter(value, now):
    global phase, at, wall_at
    phase, at, wall_at = value, now, time.monotonic()


def zones_copy(system):
    return [unreal.WarehouseShippingZone(zone_id=z.zone_id, inbound=z.inbound, anchor=z.anchor,
            half_size=z.half_size, slots=list(z.slots)) for z in system.get_editor_property('zones')]


def verify_layout_catalog(system, layout):
    before = zones_copy(system)
    original_areas = [unreal.WarehouseLayoutZone(name=z.name, centre=z.centre, half_size=z.half_size,
                      members=list(z.members), destinations=list(z.destinations)) for z in layout.get_editor_property('zones')]
    anchor = live['FocusTemporaryZone']
    centre = anchor.get_actor_location()
    requested = [unreal.Transform(location=centre + unreal.Vector(0, y, 0)) for y in (-100, 100)]
    added = unreal.WarehouseLayoutZone(name='Outbound', centre=centre, half_size=unreal.Vector(200, 250, 80),
                                      members=[anchor], destinations=requested)
    layout.set_editor_property('zones', original_areas + [added])
    layout.notify_structure_changed()
    catalog = list(system.get_editor_property('zones'))
    assert {z.zone_id for z in before} <= {z.zone_id for z in catalog}
    matches = [z for z in catalog if z.anchor == anchor]
    assert len(matches) == 1 and not matches[0].inbound
    area = next(z for z in layout.get_editor_property('zones') if anchor in z.members)
    slots = list(matches[0].slots)
    assert 2 <= len(slots) <= 6 and len(slots) == len(area.destinations)
    world_slots = [unreal.MathLibrary.compose_transforms(slot, anchor.get_actor_transform()) for slot in slots]
    for pose, destination in zip(world_slots, area.destinations):
        assert (pose.translation - destination.translation).length() < .01, 'Catalog/world destination mismatch'
        a, b = pose.rotation, destination.rotation
        assert abs(a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w) > .9999, 'Catalog slot rotation mismatch'
    assert all(any((p.translation - d.translation).length() < .01 for d in world_slots) for p in requested)
    for previous in before:
        current = next(z for z in catalog if z.zone_id == previous.zone_id)
        assert current.anchor == previous.anchor and len(current.slots) == len(previous.slots)
        assert all((a.translation - b.translation).length() < .01 for a, b in zip(current.slots, previous.slots))
    layout.set_editor_property('zones', original_areas)
    anchor.destroy_actor()
    layout.notify_structure_changed()
    assert {z.zone_id for z in system.get_editor_property('zones')} == {z.zone_id for z in before}
    check('layout-created rotated staging area shares world slots with shipping, preserves existing IDs, and removes deleted anchors')


def tick(dt):
    global live, pc, director, last_log, shipping_zones, shipment_ids, outbound_id, outbound_visit, wrap_seen
    try:
        assert time.monotonic() - started < 1200, ('Wall timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            enter('manual', 0)
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            director = live['FocusDirector']
            defaults = unreal.get_default_object(unreal.WarehouseOperationsSettings)
            assert not defaults.is_manual_operations(), 'Fresh default must use automatic office coordination'
            director.set_manual_operations(True, False)
            assert pc.set_simulation_speed(4)
            pc.set_view_target_with_blend(live['ShippingCamera'], 0)
            report['defaultManualOperations'] = defaults.is_manual_operations()
        system, vehicle, inbound, export, zone, damage, layout = [live[n] for n in
            ('ShippingSystem', 'ShippingAGV', 'ShippingImportTruck', 'ShippingExportTruck', 'ShippingInboundZone',
             'ShippingStrength', 'ShippingLayout')]
        pallet = live['ExportAPallet']
        shipments = list(system.get_editor_property('shipments'))
        assert not vehicle.get_editor_property('mechanical_failure'), vehicle.get_editor_property('status')
        assert vehicle.get_editor_property('ai_state') != unreal.WarehouseAIState.FAULT, vehicle.get_editor_property('status')
        assert system.get_editor_property('generated_units') <= 1
        for box in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseCargo):
            identity = (str(box.get_editor_property('cargo_id')), box.get_editor_property('gross_mass_kg'))
            key = box.get_name()
            if key in identities:
                assert identities[key] == identity, ('Cargo identity/mass changed', key)
            else:
                identities[key] = identity
        for p in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehousePallet):
            if p != pallet:
                inspection_seen.add(str(p.get_editor_property('receiving_state')))
                wrap_seen |= p.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED
                if vehicle.get_supported_pallet() == p:
                    handovers.add('inbound')
        if vehicle.get_supported_pallet() == pallet:
            handovers.add('outbound')
        if now - last_log >= 10:
            last_log = now
            status = director.get_summary()
            summary.append({'time': round(now, 2), 'phase': phase, 'summary': status})
            print('FORKLIFT_FOCUS_PROGRESS', phase, round(now, 2), vehicle.get_editor_property('status'), status,
                  [(s.shipment_id, str(s.state), s.wait_reason) for s in shipments])
        if phase == 'manual' and now >= 4:
            assert director.is_manual_operations() and not shipments
            assert not system.get_editor_property('continuous_operations') and system.get_editor_property('generated_units') == 0
            director.set_manual_operations(False, False)
            enter('power_off', now)
            check('manual option suppresses new automatic office work without persisting user settings')
        elif phase == 'power_off' and now - at >= 3:
            assert not director.is_manual_operations()
            assert not shipments and system.get_editor_property('generated_units') == 0
            layout.set_editing(True, 0)
            layout.set_category(unreal.WarehouseLayoutCategory.FORKLIFT)
            layout.set_selection([vehicle])
            assert layout.get_editor_property('selection_count') == 1
            enter('layout', now)
            check('automatic mode waits for the user to start forklifts; layout selection remains available')
        elif phase == 'layout' and time.monotonic() - wall_at >= .8:
            assert layout.get_editor_property('editing') and system.get_editor_property('generated_units') == 0
            assert not shipments
            verify_layout_catalog(system, layout)
            layout.set_editing(False, 0)
            shipping_zones = zones_copy(system)
            system.set_editor_property('zones', [])
            assert director.set_fleet_running(True), director.get_editor_property('last_message')
            enter('missing_zone', now)
            check('layout pause preserves cargo and produces no office jobs or shipments')
        elif phase == 'missing_zone' and now - at >= 3:
            assert system.get_editor_property('generated_units') == 0 and not shipments
            assert director.get_summary()
            system.set_editor_property('zones', shipping_zones)
            enter('arriving', now)
            check('missing staging zones stop supply without spawning orphan cargo; restored zones are reused')
        elif phase == 'arriving' and now - at >= 4:
            assert system.get_editor_property('generated_units') == 0 and not shipments
            assert '점유' in system.get_editor_property('operations_status'), system.get_editor_property('operations_status')
            live['ShippingInboundBlocker'].destroy_actor()
            enter('await_manifest', now)
            check('occupied receiving slot blocks new supply before spawning any cargo')
        elif phase == 'await_manifest':
            inbound_shipments = [s for s in shipments if s.direction == unreal.WarehouseShipmentDirection.INBOUND]
            if inbound_shipments and pallet.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED:
                assert system.get_editor_property('generated_units') == 1
                assert director.request_outbound(pallet), director.get_editor_property('last_message')
                assert not director.request_outbound(pallet), 'Duplicate forklift request accepted'
                enter('order_accepted', now)
                check('background supply creates a real manifest; user transport request is deduplicated')
            assert now - at < 45, 'Automatic arrival / native wrapping did not start'
        elif phase == 'order_accepted':
            exports = [s for s in shipments if s.direction == unreal.WarehouseShipmentDirection.OUTBOUND]
            if exports:
                assert len(exports) == 1 and len(exports[0].lines) == 1 and exports[0].lines[0].pallet == pallet
                outbound_id, outbound_visit = exports[0].shipment_id, exports[0].truck_visit_id
                assert exports[0].truck == export and exports[0].truck != inbound
                shipment_ids = {s.shipment_id for s in shipments}
                director.set_manual_operations(True, False)
                enter('manual_inflight', now)
                check('user pallet request automatically chooses an outbound truck without reusing the inbound vehicle')
            assert now - at < 20, director.get_summary()
        elif phase == 'manual_inflight' and now - at >= 4:
            assert director.is_manual_operations() and not system.get_editor_property('continuous_operations')
            assert {s.shipment_id for s in shipments} == shipment_ids
            assert all(s.state != unreal.WarehouseShipmentState.CANCELLED for s in shipments)
            director.set_manual_operations(False, False)
            enter('transport', now)
            check('switching to manual retains in-flight shipments and actual transport; automatic mode can resume')
        elif phase == 'transport':
            shipped = next((s for s in shipments if s.shipment_id == outbound_id), None)
            received = [s for s in shipments if s.direction == unreal.WarehouseShipmentDirection.INBOUND]
            if (shipped and shipped.state == unreal.WarehouseShipmentState.COMPLETED and
                    zone.get_editor_property('completed_count') >= 1 and received and
                    all(s.state == unreal.WarehouseShipmentState.COMPLETED for s in received)):
                assert {'inbound', 'outbound'} <= handovers
                assert wrap_seen and any('INSPECTING' in state.upper() for state in inspection_seen), inspection_seen
                assert len(shipped.lines) == 1 and shipped.lines[0].arrived and shipped.lines[0].prepared and shipped.lines[0].loaded
                assert shipped.lines[0].job_id in vehicle.get_editor_property('completed_job_ids')
                assert all(line.arrived for s in received for line in s.lines)
                assert zone.get_editor_property('confirmed_count') == 1 and zone.get_editor_property('dispatched_count') == 1
                assert body(received[0].lines[0].pallet).is_simulating_physics()
                assert pallet.get_editor_property('hidden')
                assert shipped.elapsed_seconds > 0
                report['outboundSimulationSeconds'] = shipped.elapsed_seconds
                enter('return', now)
                check('native AGV carries both units; receiving inspection/wrap/rack storage and outbound handover/labels/loading complete')
            assert now - at < 900, ('Native transport timeout', director.get_summary(), vehicle.get_editor_property('status'))
        elif phase == 'return':
            if export.get_visit_id() != outbound_visit and export.is_ready():
                assert not export.get_editor_property('manifest_pallets')
                assert system.get_editor_property('generated_units') == 1
                assert len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseCargo)) == 2
                assert len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehousePallet)) == 2
                assert director.set_fleet_running(False)
                report['cargoIdentities'] = identities
                report['observedInspectionStates'] = sorted(inspection_seen)
                check('outbound vehicle automatically returns empty; finite supply preserves unique stock and fleet stop remains user-controlled')
                finish(True, 'Default office coordination, option boundaries, layout pause, equipment absence, actual inbound/outbound transport and truck recycling passed')
            assert now - at < 90, ('Automatic outbound return timeout', director.get_summary())
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
