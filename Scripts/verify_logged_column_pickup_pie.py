"""Run a reported column-side upper-rack pickup on the saved warehouse in PIE.

-ColumnLogRun=name labels the report. Cases 2/4 check real load withdrawal.
Cases 3/6 extend the same upper-rack scene through the two reported outbound
routes. Scene setup is performed before PIE and the start headings are inferred.
"""
import json
from pathlib import Path
import re
import time
import traceback

import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
command = unreal.SystemLibrary.get_command_line()
case_arg = re.search(r'-ColumnLogCase=([2346])\b', command, re.I)
case = int(case_arg[1]) if case_arg else 2
full_delivery = case in (3, 6)
destination = {3: (-1020, -1570, 0), 6: (-800, -1395, 0)}.get(case, (2250, 1150, 0))
run = re.search(r'-ColumnLogRun=([A-Za-z0-9_-]+)', command, re.I)
out = Path(unreal.Paths.project_saved_dir()) / 'Testing/LoggedColumnPickup_20261110' / (run[1] if run else 'Current')
out.mkdir(parents=True, exist_ok=True)
report = dict(passed=False, case=case, destination=destination, samples=[], saved_map_written=False, runtime_pose_forced=False)
started = time.monotonic()
phase = 'start'
vehicle = None
last = -1
accepted_at = None


def xyz(v):
    return [v.x, v.y, v.z]


def finish(passed, detail):
    report.update(passed=passed, detail=detail)
    if vehicle:
        report['reports'] = [dict(result=r.result, detail=r.detail, t=r.simulation_seconds) for r in vehicle.job_reports]
    (out / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('LOGGED_COLUMN_PICKUP_RESULT', passed, detail)
    unreal.unregister_slate_post_tick_callback(handle)
    if editor.get_game_world():
        level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def setup():
    unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    saved = actors.get_all_level_actors()
    source = unreal.Vector(215, 62.5, 140)
    pallet = min((a for a in saved if isinstance(a, unreal.WarehousePallet)), key=lambda a: (a.get_actor_location()-source).length())
    report['saved_pallet'] = dict(label=pallet.get_actor_label(), xyz=xyz(pallet.get_actor_location()), yaw=pallet.get_actor_rotation().yaw)
    assert (pallet.get_actor_location()-source).length() < 3, report['saved_pallet']
    for a in saved:
        if isinstance(a, unreal.WarehouseForklift):
            a.set_editor_property('auto_start', False)
            a.set_editor_property('idle_parking_enabled', False)
            a.set_editor_property('pending_jobs', [])
    rig = next(a for a in saved if isinstance(a, unreal.WarehouseForklift) and a.vehicle_name == '지게차 2호')
    if full_delivery:
        start, heading = unreal.Vector(-579.612, -1397.445, .1), 180
    elif case == 2:
        start, heading = unreal.Vector(2249.520, 1149.785, .1), 0
    else:
        start, heading = unreal.Vector(2579.913, -650.223, .1), 0
    rig.set_actor_location(start, False, True)
    rig.set_actor_rotation(unreal.Rotator(yaw=heading), True)
    if case == 4:
        pallet.set_actor_location(unreal.Vector(214.217638, 62.517432, 140), False, True)
        pallet.set_actor_rotation(unreal.Rotator(yaw=2.437714), True)
    report.update(vehicle_label=rig.get_actor_label(), start=xyz(start), start_yaw=heading,
                  pillar={a.get_actor_label(): xyz(a.get_actor_location()) for a in saved
                          if a.get_actor_label() in ('MZ_L2_Column_500_0', 'WS_RubberColumnWrap_450_4')})
    assert len(report['pillar']) == 2, report['pillar']


def tick(dt):
    global phase, vehicle, last, accepted_at
    try:
        assert time.monotonic()-started < 360, ('Wall timeout', phase)
        if phase == 'start':
            phase = 'setup'
            setup()
            phase = 'settle'
            level.editor_request_begin_play()
            return
        if phase == 'setup':
            return
        world = editor.get_game_world()
        if not world:
            return
        now = unreal.GameplayStatics.get_time_seconds(world)
        if phase == 'settle' and not vehicle:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)}
            vehicle = live[report['vehicle_label']]
            for director in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WarehouseFocusDirector):
                director.set_manual_operations(True, False)
            assert unreal.GameplayStatics.get_player_controller(world, 0).set_simulation_speed(4)
        if phase == 'settle':
            if now < 5:
                return
            pallet = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WarehousePallet)
                          if a.get_actor_label() == report['saved_pallet']['label'])
            report['settled_source'] = dict(xyz=xyz(pallet.get_actor_location()), yaw=pallet.get_actor_rotation().yaw)
            assert vehicle.submit_job(unreal.WarehouseWorkOrder(job_id='LOGGED-COLUMN-PICKUP', pallet=pallet,
                                      source_system='FMS-COMPUTER' if full_delivery else 'FMS-SIM',
                                      destination=unreal.Transform(location=unreal.Vector(*destination),
                                                                   rotation=unreal.Rotator(yaw=180 if case == 6 else 0)))), vehicle.status
            vehicle.toggle_power()
            phase, accepted_at = 'transport', now
        if now-last >= .25:
            last = now
            report['samples'].append(dict(t=now, state=str(vehicle.ai_state), status=vehicle.status,
                xyz=xyz(vehicle.get_actor_location()), yaw=vehicle.get_actor_rotation().yaw,
                yaw_rate=vehicle.root_component.get_physics_angular_velocity_in_degrees().z, speed=vehicle.current_speed_cm))
        if full_delivery and any(r.result == 'COMPLETED' for r in vehicle.job_reports):
            assert not vehicle.get_supported_pallet()
            finish(True, 'Physical rack pickup and delivery to the outbound slot completed')
        elif not full_delivery and vehicle.ai_state == unreal.WarehouseAIState.TRAVEL_HEIGHT:
            assert vehicle.get_supported_pallet() and vehicle.get_load_mass_kg() > 0
            finish(True, 'Physical rack pickup, lift and loaded withdrawal completed')
        elif vehicle.ai_state == unreal.WarehouseAIState.FAULT or any(r.result == 'DEFERRED' for r in vehicle.job_reports):
            finish(False, vehicle.status)
        elif now-accepted_at > 245:
            finish(False, 'Pickup did not finish within 245 simulation seconds')
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
