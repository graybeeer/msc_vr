"""Check that native inbound/shipping producers respect an explicit work stop.

Uses only the shipping test's unsaved fixture setup. No vehicle AI state,
runtime pallet pose, job report or completion state is forced. These checks
exercise real producer submissions and cancellation, not full physical delivery.
"""
import ast
import json
from pathlib import Path
import time
import traceback

import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
fixture_path = Path(__file__).with_name('verify_shipping_workflow_pie.py')
tree = ast.parse(fixture_path.read_text(encoding='utf-8-sig'), str(fixture_path))
boundary = next(i for i, node in enumerate(tree.body) if isinstance(node, ast.Assign)
                and any(isinstance(target, ast.Name) and target.id == 'out' for target in node.targets))
fixture = {'__file__': str(fixture_path), '__name__': '__work_control_fixture__'}
exec(compile(ast.Module(body=tree.body[:boundary], type_ignores=[]), str(fixture_path), 'exec'), fixture)
level, actors, editor = (fixture[name] for name in ('level', 'actors', 'editor'))

# One stationary unit is sufficient. Configure its receiving origin before PIE.
keep = {'ExportAPallet', 'ExportABox'}
remove = {actor.get_actor_label() for actor in actors.get_all_level_actors()
          if isinstance(actor, (unreal.WarehousePallet, unreal.WarehouseCargo)) and actor.get_actor_label() not in keep}
strength = fixture['strength']
strength.set_editor_property('objects', [row for row in strength.get_editor_property('objects') if str(row.name) not in remove])
for actor in list(actors.get_all_level_actors()):
    if actor.get_actor_label() in remove or actor.get_actor_label() in ('ShippingInboundBlocker', 'ShippingOutboundInspection'):
        actors.destroy_actor(actor)
zone = fixture['in_zone']
zone.set_actor_location(unreal.Vector(500, 0, 0), False, True)
zone.set_editor_property('half_size_cm', unreal.Vector(190, 190, 80))
zone.set_editor_property('inspection_seconds', .5)
for truck in fixture['shipping'].get_editor_property('trucks'):
    truck.set_editor_property('show_driver', False)

OUT = Path(unreal.Paths.project_saved_dir()) / 'Testing/WorkControlDispatch'
OUT.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
phase = 'start'
at = 0
live = None
old_inbound = old_shipping = ''
checks = []
report = dict(passed=False, checks=checks, savedMapWritten=False, runtimePosesForced=False,
              vehicleAIStateForced=False, fullTransportTested=False,
              scope='Unsaved fixture; actual inbound/shipping producer submissions, user stop and explicit re-request')


def check(message):
    checks.append(message)
    print('WORK_CONTROL_DISPATCH_CHECK_PASSED', message)


def enter(next_phase, now):
    global phase, at
    phase, at = next_phase, now


def finish(ok, detail):
    report.update(passed=ok, message=detail, wall_seconds=round(time.monotonic() - started, 2))
    (OUT / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('WORK_CONTROL_DISPATCH_' + ('PASSED' if ok else 'FAILED'), detail)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def orders(vehicle):
    active = vehicle.get_editor_property('active_job')
    result = list(vehicle.get_editor_property('pending_jobs'))
    if active.job_id:
        result.append(active)
    return result


def cancelled(vehicle, job_id):
    return any(row.job_id == job_id and row.result == 'CANCELLED' and '[USER_STOP]' in row.detail
               for row in vehicle.get_editor_property('job_reports'))


def tick(dt):
    global live, old_inbound, old_shipping
    try:
        assert time.monotonic() - started < 180, ('Work control dispatch timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            enter('confirm', 0)
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live = {actor.get_actor_label(): actor for actor in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            assert unreal.GameplayStatics.get_player_controller(game, 0).set_simulation_speed(4)
        zone, vehicle, pallet, shipping, truck = [live[name] for name in
            ('ShippingInboundZone', 'ShippingAGV', 'ExportAPallet', 'ShippingSystem', 'ShippingExportTruck')]
        if phase == 'confirm':
            if pallet.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED:
                assert pallet.get_editor_property('receiving_state') == unreal.WarehouseReceivingState.ACCEPTED
                assert not orders(vehicle)
                assert zone.request_transport(pallet), zone.get_editor_property('status')
                assert zone.is_transport_requested(pallet)
                vehicle.toggle_power()
                enter('inbound_job', now)
                check('Native inspection/wrapping accepts the stationary receiving unit; an explicit request enters the real producer queue')
            assert now < 40, ('Inspection did not finish', zone.get_editor_property('status'))
        elif phase == 'inbound_job':
            jobs = [job for job in orders(vehicle) if job.pallet == pallet]
            if jobs:
                assert len(jobs) == 1 and jobs[0].source_system.startswith('FMS-INBOUND-')
                old_inbound = jobs[0].job_id
                assert vehicle.stop_current_work(), vehicle.get_editor_property('work_stop_status')
                zone.scan_inbound()
                assert not orders(vehicle) and cancelled(vehicle, old_inbound)
                assert not zone.is_transport_requested(pallet), 'Stopped inbound reservation remained claimed'
                vehicle.toggle_power()
                enter('stopped_inbound', now)
                check('User stop cancels the actual inbound instruction and releases the zone reservation')
            assert now - at < 30, ('No inbound instruction', zone.get_editor_property('status'), vehicle.get_editor_property('status'))
        elif phase == 'stopped_inbound' and now - at >= 5:
            zone.scan_inbound()
            assert not orders(vehicle) and not zone.is_transport_requested(pallet), 'Producer automatically recreated explicitly stopped work'
            assert zone.request_transport(pallet)
            zone.scan_inbound()
            jobs = [job for job in orders(vehicle) if job.pallet == pallet]
            assert len(jobs) == 1 and jobs[0].job_id != old_inbound, ('Explicit inbound retry failed', zone.get_editor_property('status'))
            assert vehicle.stop_current_work()
            zone.scan_inbound()
            zone.set_actor_tick_enabled(False)
            assert not orders(vehicle)
            vehicle.toggle_power()
            enter('outbound_create', now)
            check('Stopped inbound work remains stopped after resume; a new explicit request can use the same pallet and rack slot')
        elif phase == 'outbound_create' and now - at >= 3:
            assert shipping.create_outbound('Stop outbound probe', [pallet], 'OUT-TEST', truck), shipping.get_editor_property('last_message')
            enter('outbound_job', now)
        elif phase == 'outbound_job':
            shipping.refresh_shipping()
            jobs = [job for job in orders(vehicle) if job.pallet == pallet]
            if jobs:
                assert len(jobs) == 1 and jobs[0].source_system == 'FMS-COMPUTER'
                old_shipping = jobs[0].job_id
                assert vehicle.stop_current_work()
                assert cancelled(vehicle, old_shipping)
                shipping.refresh_shipping()
                current = [shipment for shipment in shipping.get_editor_property('shipments') if shipment.name == 'Stop outbound probe']
                assert len(current) == 1 and current[0].state == unreal.WarehouseShipmentState.CANCELLED
                assert not shipping.get_pallet_shipment(pallet) and not orders(vehicle)
                enter('stopped_outbound', now)
                check('Shipping observes the user cancellation, cancels the single-unit shipment and releases its pallet reservation')
            assert now - at < 30, ('No native outbound instruction', shipping.get_editor_property('last_message'), vehicle.get_editor_property('status'))
        elif phase == 'stopped_outbound' and now - at >= 3:
            shipping.refresh_shipping()
            assert not orders(vehicle), 'Shipping reissued the cancelled instruction'
            assert shipping.create_outbound('Explicit outbound retry', [pallet], 'OUT-TEST', truck), shipping.get_editor_property('last_message')
            assert len(shipping.get_editor_property('shipments')) == 2
            assert not vehicle.get_editor_property('completed_job_ids')
            assert zone.get_editor_property('completed_count') == 0
            check('Cancelled outbound work remains cancelled; a fresh explicit shipment can reserve the same unit without pretending it was delivered')
            finish(True, 'Inbound and outbound producer cancellation, no automatic reissue, reservation release and explicit retries passed')
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
