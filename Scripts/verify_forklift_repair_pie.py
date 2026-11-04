"""Native repair/E interaction, broken joints, blocked origin and real lifted freight.

Unsaved fixture uses the project's actual forklift rig. Only explicit fault
stimuli, the test obstruction/player and the requested repair may change poses.
No transport state, completion or freight motion is forced to pass the check.
"""
import ast
import json
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
source = Path(__file__).with_name('verify_shipping_workflow_pie.py')
tree = ast.parse(source.read_text(encoding='utf-8-sig'))
end = next(i for i, n in enumerate(tree.body) if isinstance(n, ast.Assign) and
           any(isinstance(t, ast.Name) and t.id == 'out' for t in n.targets))
fixture = dict(__file__=str(source), __name__='__repair_fixture__')
exec(compile(ast.Module(body=tree.body[:end], type_ignores=[]), str(source), 'exec'), fixture)
level, editor = (fixture[n] for n in ('level', 'editor'))
vehicle, strength = (fixture[n] for n in ('vehicle', 'strength'))
profiles = list(strength.get_editor_property('objects'))
profiles.append(unreal.WarehouseStrength(name='RepairAGV', members=[vehicle], failure=unreal.WarehouseFailure.MACHINE,
    mass_kg=1000, rated_load_kg=1400, impact_yield_j=400, impact_failure_j=2400))
strength.set_editor_property('objects', profiles)
vehicle.set_editor_property('battery_percent', 64)
vehicle.set_editor_property('vehicle_name', '지게차 7호')
fixture['spawn'](unreal.TargetPoint, 'RepairOperator', (0, -150, 0))
obstruction = fixture['cube']('RepairObstruction', (-1600, -1600, 60), (80, 100, 120))
obstruction.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
for name, point, size in [('West', (-3570, 0, 150), (20, 5300, 300)),
                         ('East', (3570, 0, 150), (20, 5300, 300)),
                         ('South', (0, -2660, 150), (7160, 20, 300)),
                         ('North', (0, 2660, 150), (7160, 20, 300))]:
    fixture['cube']('RepairMap' + name, point, size).tags = ['WarehouseMapStructure']
out = Path(unreal.Paths.project_saved_dir()) / 'Testing/ForkliftRepair_20261104'
out.mkdir(parents=True, exist_ok=True)
checks = []
report = dict(passed=False, checks=checks, savedMapWritten=False, osInputTested=False,
              scope='Native E handler and repair API; actual Chaos rig, motion, damage, pickup and loose freight')
phase = 'start'
started = time.monotonic()
at = 0
live = None
scans = None


def enter(value, now):
    global phase, at
    phase, at = value, now


def check(message):
    checks.append(message)
    print('FORKLIFT_REPAIR_CHECK_PASSED', message)


def finish(ok, detail):
    report.update(passed=ok, message=detail, wallSeconds=round(time.monotonic() - started, 2))
    if live:
        report['reports'] = [dict(job=r.job_id, result=r.result, detail=r.detail)
                             for r in live['ShippingAGV'].get_editor_property('job_reports')]
    (out / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('FORKLIFT_REPAIR_' + ('PASSED' if ok else 'FAILED'), detail)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def machine_profile():
    return next(p for p in live['ShippingStrength'].get_editor_property('objects') if p.name == 'RepairAGV')


def assert_repaired(v):
    assert not v.get_editor_property('mechanical_failure'), v.status
    assert not v.get_editor_property('powered') and not v.is_remote_controlled()
    assert not v.get_editor_property('active_job').job_id and not v.get_editor_property('pending_jobs')
    assert not v.get_supported_pallet()
    assert (v.get_actor_location() - v.get_editor_property('initial_placement').translation).length() < .01
    assert abs(v.get_physical_mass_kg() - 1000) < .5, v.get_physical_mass_kg()
    assert abs(v.get_editor_property('CurrentSpeedCm')) < .01
    assert all(not j.is_broken() for j in v.get_components_by_class(unreal.PhysicsConstraintComponent))
    p = machine_profile()
    assert not p.failed and p.damage == 0
    assert v.check_systems() == '', v.check_systems()
    assert not v.repair_and_reset(), 'Healthy vehicle accepted an unnecessary reset'


def tick(dt):
    global live, scans
    try:
        assert time.monotonic() - started < 600, ('Wall timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            enter('settle', 0)
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            assert unreal.GameplayStatics.get_player_controller(game, 0).set_simulation_speed(4)
        v, s = live['ShippingAGV'], live['ShippingStrength']
        operator = live['RepairOperator']
        b = live['ExportBPallet']
        if phase == 'settle':
            assert str(v.get_interaction_text()).startswith('지게차 7호\n'), 'Healthy interaction lost its vehicle number'
            if b.get_editor_property('receipt_state') != unreal.WarehouseReceiptState.CONFIRMED:
                assert now < 25, 'Fixture receiving did not confirm the pallet'
                return
            assert not v.repair_and_reset()
            assert v.submit_job(unreal.WarehouseWorkOrder(job_id='REPAIR-QUEUED', source_system='FMS-SIM', pallet=b,
                destination=unreal.Transform(location=unreal.Vector(-500, 500, 0)))), v.get_editor_property('last_error_message')
            assert v.begin_remote_control(operator)
            enter('drive_before_fault', now)
        elif phase == 'drive_before_fault':
            v.set_remote_input(1, 0, 0)
            if now - at < 1.5:
                return
            assert v.get_actor_location().x > 80, 'Vehicle did not move physically away from its initial pose'
            v.end_remote_control(operator)
            next(j for j in v.get_components_by_class(unreal.PhysicsConstraintComponent)
                 if j.get_name() == 'InnerMastHydraulicJoint').break_constraint()
            s.apply_impact(v, 3000)
            assert v.get_editor_property('mechanical_failure') and s.has_failed(v)
            assert str(v.get_interaction_text()).startswith('지게차 7호\n'), 'Failed interaction lost its vehicle number'
            assert '상태:고장, E:수리 후 재배치' in str(v.get_interaction_text())
            enter('blocked_origin', now)
        elif phase == 'blocked_origin':
            before = v.get_actor_transform()
            obstruction = live['RepairObstruction']
            assert obstruction.set_actor_location(unreal.Vector(-65, 0, 60), False, True)
            assert abs(obstruction.get_actor_location().x + 65) < .01
            assert not v.repair_and_reset(), 'Repair overlapped an obstruction at the initial pose'
            assert (v.get_actor_location() - before.translation).length() < .001
            assert v.get_editor_property('pending_jobs') and s.has_failed(v)
            assert str(v.get_editor_property('last_error_code')) == 'REPAIR_LOCATION_BLOCKED'
            assert v.get_editor_property('last_error_message') in str(v.get_interaction_text())
            report['blockedReason'] = v.get_editor_property('last_error_message')
            assert obstruction.set_actor_location(unreal.Vector(-1600, -1600, 60), False, True)
            floor = live['ShippingFloor']
            floor.set_actor_enable_collision(False)
            try:
                assert not v.repair_and_reset(), 'Repair accepted missing initial floor support'
            finally:
                floor.set_actor_enable_collision(True)
            check('Blocked initial pose and missing support reject without cancelling work, repairing damage or moving vehicle')
            # Native E handler, using its real line-of-sight target. No OS input injection.
            pawn = unreal.GameplayStatics.get_player_character(game, 0)
            pawn.set_actor_location(v.get_actor_location() + unreal.Vector(0, -180, 100), False, True)
            camera = pawn.get_component_by_class(unreal.CameraComponent)
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_world_location(), v.get_actor_location() + unreal.Vector(-20, 0, 95)))
            enter('interact_e', now)
        elif phase == 'interact_e' and now - at > .15:
            pawn = unreal.GameplayStatics.get_player_character(game, 0)
            assert pawn.get_interaction_target() == v, 'Native E targeting did not select the failed forklift'
            s.apply_impact(live['OtherShipmentBox'], 300)
            other_damage = next(p.damage for p in s.get_editor_property('objects') if p.name == 'OtherShipmentBox')
            pawn.toggle_carry()
            assert_repaired(v)
            assert next(p.damage for p in s.get_editor_property('objects') if p.name == 'OtherShipmentBox') == other_damage
            assert v.get_editor_property('battery_percent') <= 64, 'Repair silently recharged the battery'
            assert any(r.job_id == 'REPAIR-QUEUED' and r.result == 'CANCELLED' for r in v.get_editor_property('job_reports'))
            check('E restores actual broken mast joint, machine damage, mass and initial pose; cancels queue and stays paused')
            pawn.set_actor_location(unreal.Vector(-2200, -2000, 110), False, True)
            enter('settle_repaired', now)
        elif phase == 'settle_repaired' and now - at > 3:
            assert not v.get_editor_property('mechanical_failure'), v.status
            assert v.get_actor_up_vector().z > .99
            assert v.submit_job(unreal.WarehouseWorkOrder(job_id='REPAIR-ACTIVE', source_system='FMS-SIM', pallet=b,
                destination=unreal.Transform(location=unreal.Vector(-500, 500, 0))))
            v.toggle_power()
            enter('pickup', now)
        elif phase == 'pickup':
            assert not v.get_editor_property('mechanical_failure'), v.status
            if v.get_supported_pallet() != b or v.get_editor_property('ai_state') != unreal.WarehouseAIState.VERIFY_LOAD:
                assert now - at < 250, ('Actual pickup timeout', v.status)
                return
            original = {name: live[name].get_actor_location() for name in ('ExportBPallet', 'ExportBBox')}
            s.apply_impact(v, 3000)
            assert v.get_editor_property('mechanical_failure') and v.get_supported_pallet() == b
            assert v.repair_and_reset(), v.status
            assert_repaired(v)
            for name, position in original.items():
                assert (live[name].get_actor_location() - position).length() < .01, ('Repair moved freight', name)
                assert live[name].get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
            assert any(r.job_id == 'REPAIR-ACTIVE' and r.result == 'CANCELLED' for r in v.get_editor_property('job_reports'))
            assert not any(r.result == 'COMPLETED' for r in v.get_editor_property('job_reports'))
            check('Real lifted freight stays in world with physics; active work cancels without false completion on second repair')
            enter('remote_after_repair', now)
        elif phase == 'remote_after_repair' and now - at > 3:
            assert not v.get_editor_property('mechanical_failure'), v.status
            operator.set_actor_location(v.get_actor_location() + unreal.Vector(0, -150, 0), False, True)
            assert v.begin_remote_control(operator), v.status
            scans = v.get_component_by_class(unreal.AgvShadowSensorsComponent).get_editor_property('snapshot_count')
            enter('remote_drive', now)
        elif phase == 'remote_drive':
            v.set_remote_input(-1, .25, 0)
            assert not v.get_editor_property('mechanical_failure'), v.status
            assert v.get_component_by_class(unreal.AgvShadowSensorsComponent).get_editor_property('snapshot_count') == scans
            if now - at > 2:
                assert abs(v.get_editor_property('CurrentSpeedCm')) > 50, v.status
                assert abs(v.get_editor_property('CurrentSpeedCm')) <= 185
                v.end_remote_control(operator)
                check('Repaired vehicle drives and steers through native forces; remote sampling stays disabled and speed cap retained')
                finish(True, 'Repair/initial reset, E selection, blocked reset, queued/active cancellation, actual freight and post-repair drive passed')
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
