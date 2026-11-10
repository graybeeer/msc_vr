"""Native manual-drive regression in unsaved Entry; no vehicle teleports or fake velocities.

Checks immediate input, no remote scanner/AI stops, slab seams, real wall contact,
reverse escape, high-fork travel, pivoting, deadman braking and autonomous scan cadence.
SetRemoteInput is exercised directly; this is not an OS keyboard/focus test.
-RemoteCombinedSteering checks full simultaneous forward/reverse and left/right
inputs, including repeated steering changes. -RemoteRun=name preserves a report.
-RemotePivotOnly checks held A/D separately; -RemotePivotPulse checks 0.5s presses.
"""
import json
import math
import re
import time
import traceback
from pathlib import Path
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
template = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'WH_AutonomousForklift')
part_poses = {p.get_name(): unreal.MathLibrary.make_relative_transform(p.get_world_transform(), template.get_actor_transform())
              for p in template.get_components_by_class(unreal.SceneComponent) if p != template.root_component}
part_assets = {p.get_name(): (p.static_mesh.get_path_name(), [p.get_material(i).get_path_name() if p.get_material(i) else None for i in range(p.get_num_materials())])
               for p in template.get_components_by_class(unreal.StaticMeshComponent) if p.static_mesh}
drive_values = {name: template.get_editor_property(name) for name in ('empty_travel_speed_cm', 'loaded_travel_speed_cm',
    'vehicle_mass_kg', 'drive_acceleration_cm', 'drive_torque_nm', 'brake_torque_nm', 'drive_power_watts', 'max_fork_height_cm')}
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
unreal.GameplayStatics.get_all_actors_of_class(editor.get_editor_world(), unreal.WorldSettings)[0].set_editor_property(
    'default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
# The production controller starts the fixed simulation clock before Chaos ticks.
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(3000, -3000, 125))
cube = unreal.load_asset('/Engine/BasicShapes/Cube')


def block(label, position, size):
    a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position))
    a.set_actor_label(label)
    a.static_mesh_component.set_static_mesh(cube)
    a.static_mesh_component.set_collision_profile_name('BlockAll')
    a.set_actor_scale3d(unreal.Vector(*(v / 100 for v in size)))
    return a


def production_rig(label, position):
    a = actors.spawn_actor_from_class(unreal.WarehouseForklift, unreal.Vector(*position))
    a.set_actor_label(label)
    for p in a.get_components_by_class(unreal.StaticMeshComponent):
        mesh, materials = part_assets[p.get_name()]
        p.set_static_mesh(unreal.load_asset(mesh))
        for i, material in enumerate(materials):
            if material:
                p.set_material(i, unreal.load_asset(material))
    def depth(p):
        result = 0
        while p.get_attach_parent():
            p = p.get_attach_parent()
            result += 1
        return result
    for p in sorted(a.get_components_by_class(unreal.SceneComponent), key=depth):
        if p != a.root_component:
            p.set_world_transform(unreal.MathLibrary.compose_transforms(part_poses[p.get_name()], a.get_actor_transform()), False, True)
    for name, value in drive_values.items():
        a.set_editor_property(name, value)
    return a


floors = [block('ManualFloorA', (-1850, 0, -10), (4300, 8000, 20)),
          block('ManualFloorB', (2150, 0, -10), (3700, 8000, 20))]
for floor in floors:
    floor.tags = ['WarehouseMapStructure']
block('ManualWall', (1200, 0, 100), (40, 600, 200))
# Physical Pawn beside the aisle, inside the old scanner envelope but clear of the vehicle.
person = block('ManualPerson', (180, 82, 85), (30, 30, 170))
person.static_mesh_component.set_collision_object_type(unreal.CollisionChannel.ECC_PAWN)
person.tags = ['WarehousePhysicalHuman']
vehicle = production_rig('ManualVehicle', (0, 0, 0))
vehicle.set_editor_property('auto_start', False)
vehicle.set_editor_property('autonomous_mode', True)
vehicle.set_editor_property('idle_parking_enabled', False)
operator = actors.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, -220, 75))
operator.set_actor_label('ManualOperator')
for p in operator.get_components_by_class(unreal.PrimitiveComponent):
    p.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
autonomous = production_rig('ScanVehicle', (0, 1600, 0))
autonomous.set_editor_property('auto_start', True)
autonomous.set_editor_property('idle_parking_enabled', False)
pallet = actors.spawn_actor_from_class(unreal.WarehousePallet, unreal.Vector(1000, 1600, .1))
pallet.set_actor_label('ScanPallet')
job = unreal.WarehouseWorkOrder(job_id='MANUAL-REGRESSION-AUTO', pallet=pallet, destination=unreal.Transform(location=unreal.Vector(1700, 1600, 0)))
autonomous.set_editor_property('pending_jobs', [job])
damage = actors.spawn_actor_from_class(unreal.WarehouseDamageSystem, unreal.Vector())
damage.set_editor_property('objects', [
    unreal.WarehouseStrength(name='Floors', members=floors, mass_kg=100000, rated_load_kg=100000,
        impact_yield_j=100000, impact_failure_j=1000000, failure=unreal.WarehouseFailure.STRUCTURE),
    unreal.WarehouseStrength(name='Pallet', members=[pallet], mass_kg=25, rated_load_kg=100000,
        impact_yield_j=100000, impact_failure_j=1000000, failure=unreal.WarehouseFailure.CRUSH)])

command = unreal.SystemLibrary.get_command_line()
combined = '-remotecombinedsteering' in command.lower()
pivot_only = '-remotepivotonly' in command.lower()
pivot_pulse = '-remotepivotpulse' in command.lower()
pivot_only = pivot_only or pivot_pulse
combined_cases = [('forward_left', 1, -1), ('forward_right', 1, 1),
                  ('reverse_left', -1, -1), ('reverse_right', -1, 1), ('steering_changes', 1, 0)]
combined_results = []
combined_speeds = []
combined_yaw = 0
previous_yaw = 0
if combined or pivot_only:
    # Author a separate clear test lane before PIE; never force a running body.
    vehicle.set_actor_location(unreal.Vector(-1800, -1600, 0), False, True)
    operator.set_actor_location(unreal.Vector(-1800, -1820, 75), False, True)
    person.set_actor_location(unreal.Vector(3500, -3500, 85), False, True)

started = time.monotonic()
phase = 'start'
at = 0
rig = dummy = auto = game = None
origin = None
origin_yaw = 0
straight_results = []
speeds = []
rows = []
scan_times = []
last_scan = 0
contact_since = None
yaw_start = 0
sensor_flags = ('safety_scanner_healthy', 'fork_sensor_healthy', 'localization_healthy', 'pallet_sensor_healthy', 'load_sensor_healthy')


def enter(name, now):
    global phase, at, origin, origin_yaw, speeds
    phase, at = name, now
    origin = rig.get_actor_location()
    origin_yaw = rig.get_actor_rotation().yaw
    speeds = []
    print('MANUAL_DRIVE_PHASE', name, now)


def check_straight(max_cross_cm, max_yaw_deg):
    samples = [row for row in rows if row['phase'] == phase]
    yaw_rad = math.radians(origin_yaw)
    cross = max(abs((row['y'] - origin.y) * math.cos(yaw_rad) - (row['x'] - origin.x) * math.sin(yaw_rad)) for row in samples)
    yaw = max(abs((row['yaw'] - origin_yaw + 180) % 360 - 180) for row in samples)
    straight_results.append(dict(phase=phase, peak_cross_track_cm=cross, peak_heading_error_deg=yaw))
    assert cross <= max_cross_cm and yaw <= max_yaw_deg, (phase, cross, yaw)


def finish(ok, detail):
    report = dict(passed=ok, detail=detail, phase=phase, samples=rows, autonomous_scan_times=scan_times,
                  combined_cases=combined_results, straight_cases=straight_results,
                  scope='Native C++ rig in unsaved Entry; direct input API, actual Chaos contact. No OS input claim.')
    path = Path(unreal.Paths.project_saved_dir()) / 'Testing/RemoteDriveContinuity_20261016.json'
    run = re.search(r'-RemoteRun=([A-Za-z0-9_-]+)', command, re.I)
    if run:
        path = Path(unreal.Paths.project_saved_dir()) / 'Testing/RemoteCombined_20261103' / (run.group(1)+'.json')
        path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('REMOTE_DRIVE_CONTINUITY_PIE_PASSED' if ok else 'REMOTE_DRIVE_CONTINUITY_PIE_FAILED', detail)
    if ok:
        print('OBSTACLE_INTERVAL_PIE_PASSED autonomous one-second sampling; manual operation performs no scanner queries')
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    global phase, rig, dummy, auto, game, last_scan, contact_since, yaw_start
    global combined_yaw, previous_yaw
    try:
        assert time.monotonic() - started < 180, ('Manual regression timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'wait'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if phase == 'wait':
            if now < 2:
                return
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            rig, dummy, auto = [live[n] for n in ('ManualVehicle', 'ManualOperator', 'ScanVehicle')]
            assert rig.begin_remote_control(dummy), rig.status
            rig.set_remote_input(0 if pivot_only else 1, 0, 0)
            previous_yaw = rig.get_actor_rotation().yaw
            enter('pivot_right' if pivot_only else combined_cases[0][0] if combined else 'immediate', now)
            return
        assert rig.root_component.is_simulating_physics()
        assert abs(rig.get_physical_mass_kg() - 1000) < .1
        assert not rig.get_editor_property('mechanical_failure'), rig.status
        assert rig.get_editor_property('obstacle_check_count') == 0, 'Manual input invoked the autonomous scanner'
        count = auto.get_editor_property('obstacle_check_count')
        if count != last_scan:
            stamp = auto.get_editor_property('last_obstacle_check_seconds')
            if scan_times:
                assert stamp - scan_times[-1] >= .999, scan_times
            scan_times.append(stamp)
            last_scan = count
        age = now - at
        speed = rig.current_speed_cm
        if not rows or now - rows[-1]['time'] >= .1:
            rows.append(dict(time=now, phase=phase, x=rig.get_actor_location().x,
                             y=rig.get_actor_location().y, yaw=rig.get_actor_rotation().yaw,
                             yaw_rate_deg_s=rig.root_component.get_physics_angular_velocity_in_degrees().z,
                             speed=speed, status=rig.status))
        if pivot_only:
            direction = 1 if phase == 'pivot_right' else -1 if phase == 'pivot_left' else 0
            rig.set_remote_input(0, direction, 0)
            yaw = rig.get_actor_rotation().yaw
            combined_yaw += (yaw-previous_yaw+180) % 360-180
            previous_yaw = yaw
            if direction and age >= (.5 if pivot_pulse else 8):
                assert combined_yaw*direction > (1 if pivot_pulse else 140), ('Pure steering response failed', phase, combined_yaw, rig.status)
                combined_results.append(dict(phase=phase,yaw_degrees=combined_yaw))
                combined_yaw=0
                enter('right_brake' if direction==1 else 'left_brake',now)
            elif not direction and age>=2:
                assert abs(speed)<5, ('Pivot release did not brake',speed)
                # A single instantaneous angular sample includes native tyre/
                # spindle settling. Bound measured heading drift as well as
                # linear braking, without zeroing any physical velocity.
                settled=[p for p in rows if p['phase']==phase and p['time']>=now-.5]
                release=[p for p in rows if p['phase']==phase]
                signed=lambda yaw:(yaw-release[0]['yaw']+180)%360-180
                assert max(abs(signed(p['yaw'])) for p in release)<5, 'Unbounded heading drift after release'
                changes=[signed(p['yaw']) for p in settled]
                assert max(changes)-min(changes)<1, 'Heading did not settle after release'
                combined_results.append(dict(phase=phase,settled_heading_span_deg=max(changes)-min(changes),
                    final_yaw_rate_deg_s=rig.root_component.get_physics_angular_velocity_in_degrees().z))
                combined_yaw=0
                if phase=='left_brake':finish(True,('Both 0.5s A/D-only presses' if pivot_pulse else 'Both held A/D-only pivots')+' and release brakes; actual native rig, no scanner queries')
                else:enter('pivot_left',now)
            return
        if combined:
            if phase == 'straight_after_changes':
                rig.set_remote_input(1, 0, 0)
                if age >= 3:
                    check_straight(14, 5)
                    enter('straight_reverse', now)
                return
            if phase == 'straight_reverse':
                rig.set_remote_input(-1, 0, 0)
                if age >= 4:
                    assert speed < -165, ('Full reverse failed to reach travel speed', speed)
                    check_straight(14, 3)
                    enter('combined_brake', now)
                return
            if phase == 'combined_brake':
                rig.set_remote_input(0, 0, 0)
                if age >= 1.5:
                    assert abs(speed) < 5, ('Combined-input release failed to brake', speed)
                    finish(True, 'Full forward/reverse + left/right curves and repeated steering retain travel; release brakes; native physics and autonomous 1s scans retained')
                return
            index = next(i for i, case in enumerate(combined_cases) if case[0] == phase)
            _, forward, steering = combined_cases[index]
            if phase == 'steering_changes':
                steering = 1 if int(age/.25) % 2 == 0 else -1
            rig.set_remote_input(forward, steering, 0)
            yaw = rig.get_actor_rotation().yaw
            combined_yaw += (yaw-previous_yaw+180) % 360-180
            previous_yaw = yaw
            if age >= (2.5 if phase == 'reverse_left' else 1):
                combined_speeds.append(forward*speed)
                assert forward*speed > 10, ('Steering interrupted held travel', phase, age, speed, rig.status)
            if age >= 4:
                if phase != 'steering_changes':
                    assert combined_yaw*forward*steering > 40, ('Wrong or missing curve direction', phase, combined_yaw)
                points = [p for p in rows if p['phase'] == phase]
                distance = sum(((b['x']-a['x'])**2+(b['y']-a['y'])**2)**.5 for a, b in zip(points, points[1:]))
                assert distance > 100, ('No physical travel', phase, distance) # A full circle can finish near its start.
                peak_rate = max(abs(p['yaw_rate_deg_s']) for p in points)
                combined_results.append(dict(phase=phase, min_signed_speed_cm_s=min(combined_speeds), yaw_degrees=combined_yaw,
                    peak_yaw_rate_deg_s=peak_rate, distance_cm=distance))
                assert peak_rate < 65, ('Rolling steering too abrupt', phase, peak_rate)
                combined_speeds.clear()
                combined_yaw = 0
                enter(combined_cases[index+1][0] if index+1 < len(combined_cases) else 'straight_after_changes', now)
            return
        if phase == 'immediate':
            rig.set_remote_input(1, 0, 0)
            if age < .4:
                return
            assert speed > 5, ('Initial input was delayed', speed, rig.status)
            rig.end_remote_control(dummy)
            for name in sensor_flags:
                rig.set_editor_property(name, False)
            assert rig.begin_remote_control(dummy), ('Sensor faults blocked manual connection', rig.status)
            assert rig.check_systems() and not rig.check_systems(False), 'Autonomous and manual system checks were not separated'
            enter('continuous', now)
        elif phase == 'continuous':
            rig.set_remote_input(1, 0, 0)
            if age > 1:
                speeds.append(speed)
            assert 'REMOTE STOP' not in rig.status, rig.status
            if age < 4:
                return
            assert min(speeds) > 50 and max(speeds) >= 165, ('Periodic stop / speed', speeds)
            assert rig.get_actor_location().x > 300, 'Did not cross the physical slab seam'
            check_straight(12, 2.5)
            print('REMOTE_CONTINUOUS_DRIVE_PASSED', 'minimum/max cm/s', min(speeds), max(speeds))
            enter('brake', now)
        elif phase == 'brake':
            rig.set_remote_input(0, 0, 0)
            if age < 1.3:
                return
            assert abs(speed) < 5, ('Manual brake did not stop the actual body', speed)
            enter('raise', now)
        elif phase == 'raise':
            rig.set_remote_input(0, 0, 1)
            if age < 2.8:
                return
            assert rig.get_fork_height_cm() > 25, rig.status
            enter('raised_drive', now)
        elif phase == 'raised_drive':
            rig.set_remote_input(.3, 0, 0)
            if age < 1:
                return
            assert (rig.get_actor_location() - origin).length() > 15, ('AI high-fork travel interlock', rig.status)
            enter('lower', now)
        elif phase == 'lower':
            rig.set_remote_input(0, 0, -1)
            if age < 2.8:
                return
            enter('wall_contact', now)
        elif phase == 'wall_contact':
            rig.set_remote_input(.25, 0, 0)
            assert 'REMOTE STOP' not in rig.status, rig.status
            if abs(speed) < 4 and rig.get_actor_location().x > 1000:
                if contact_since is None:
                    contact_since = now
                if now - contact_since > .7:
                    assert rig.get_actor_location().x < 1190, 'Vehicle crossed the solid wall'
                    print('REMOTE_NATIVE_WALL_CONTACT_PASSED', rig.get_actor_location())
                    enter('withdraw', now)
            else:
                contact_since = None
            assert age < 20, ('No actual wall contact', rig.get_actor_location(), rig.status)
        elif phase == 'withdraw':
            rig.set_remote_input(-.7, 0, 0)
            if age < 2.5:
                return
            assert origin.x - rig.get_actor_location().x > 80, ('Could not reverse out of actual contact', rig.status)
            check_straight(15, 4)
            enter('pivot_brake', now)
        elif phase == 'pivot_brake':
            rig.set_remote_input(0, 0, 0)
            if age < 1.3:
                return
            yaw_start = rig.get_actor_rotation().yaw
            enter('pivot', now)
        elif phase == 'pivot':
            rig.set_remote_input(0, 1, 0)
            if age < 3.5:
                return
            yaw = (rig.get_actor_rotation().yaw - yaw_start + 180) % 360 - 180
            assert yaw > 35, ('Manual pivot stopped', yaw, rig.status)
            enter('curve', now)
        elif phase == 'curve':
            rig.set_remote_input(.5, -.35, 0)
            # Leaving a 90-degree pivot requires finite spindle travel
            # and acceleration. Measure continuity after that physical settling.
            if age > 1.8:
                speeds.append(abs(speed))
            if age < 3:
                return
            assert speeds and min(speeds) > 10, ('Continuous steering stalled', speeds, rig.status)
            assert (rig.get_actor_location() - origin).length() > 30, 'Curved manual travel made no physical progress'
            rig.set_remote_input(.5, 0, 0)
            enter('input_timeout', now)
        elif phase == 'input_timeout':
            # Deliberately stop heartbeats, retaining the connection-loss brake.
            if age < 2:
                return
            assert 'REMOTE INPUT TIMEOUT' in rig.status and abs(speed) < 5, rig.status
            assert len(scan_times) >= 3, ('Autonomous scanner stopped too', scan_times)
            rig.set_editor_property('e_stop_released', False)
            enter('e_stop', now)
        elif phase == 'e_stop':
            rig.set_remote_input(1, 0, 0)
            if age < 1:
                return
            assert 'E-STOP ACTIVE' in rig.status and abs(speed) < 5, rig.status
            rig.set_editor_property('e_stop_released', True)
            enter('hardware_resume', now)
        elif phase == 'hardware_resume':
            rig.set_remote_input(.5, 0, 0)
            if age < .4:
                return
            assert speed > 5, ('Input did not resume after hardware stop release', speed, rig.status)
            rig.end_remote_control(dummy)
            assert not rig.is_remote_controlled() and not rig.powered
            finish(True, 'Immediate/continuous manual travel, sensor faults ignored, seam crossing, real contact/reverse, raised forks, pivot, deadman brake; autonomous 1s sampling retained')
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
