"""Safety of the AGV prototype (AGV_WH_Vehicle in Lvl_FirstPerson) in real PIE time on the Chaos rigid-body drive, on
the straight 31.6 m lane C (-380, 1580) <-> A (-380, -1580), body first at full speed (180 cm/s):
  1. a worker (80 kg WarehouseWorker) standing on the lane: warning slowdown, stop, held while there, restart when the
     worker is moved off the lane, arrival;
  2. a person-sized dummy appearing on the lane 300 cm ahead of the body at full speed (protective field ~410 cm):
     emergency stop distance and the clearance left;
  3. a 40 x 40 x 40 cm, 15 kg carton dropped from 250 cm onto the lane 300 cm ahead at full speed (real Chaos fall).
Clearance is between the vehicle footprint and the obstacle's bounds; <= 0 is contact. The teammates' forklifts are kept
parked. Fixtures are placed before PIE; the dummies wait below the floor and are moved into place when triggered
(fixture placement only; the vehicle is moved only by its drive). Never saves.

Run (one editor process):
UnrealEditor.exe msc_vr.uproject -ExecCmds="py <abs path>/verify_agv_safety_pie.py" -nullrhi -unattended -nosplash -nop4
Success line: AGV_SAFETY_PIE_PASSED."""
import math
import time
import traceback
import unreal

LANE_X = -380.0
WORKER_Y = -700.0
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.WarehouseForklift):
        actor.set_editor_property('auto_start', False)
        actor.set_editor_property('autonomous_mode', False)
unreal.SystemLibrary.execute_console_command(editor.get_editor_world(), 't.OverrideFPS 60')
cylinder = unreal.load_asset('/Engine/BasicShapes/Cylinder')
cube = unreal.load_asset('/Engine/BasicShapes/Cube')

worker = actors.spawn_actor_from_class(unreal.WarehouseWorker, unreal.Vector(LANE_X, WORKER_Y, 96))
worker.set_actor_label('AGVT_Worker')


def dummy(label, mesh, size):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(LANE_X, 0, -2000))
    actor.set_actor_label(label)
    component = actor.static_mesh_component
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_static_mesh(mesh)
    component.set_collision_profile_name('BlockAll')
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100, size[1] / 100, size[2] / 100))
    return actor


person = dummy('AGVT_PersonDummy', cylinder, (40, 40, 170))  # kinematic: it stands where it is put
carton = dummy('AGVT_Carton', cube, (40, 40, 40))
carton.static_mesh_component.set_mass_override_in_kg(unreal.Name(), 15.0, True)

began = time.monotonic()
phase, wait_until = 'begin', 0.0
v = nav = drive = safety = None
g_worker = g_person = g_carton = None
results = []
case = {}
FOOT = None


def clearance(target):
    """Gap (cm) between the vehicle footprint and the target's bounds, in the plane."""
    loc, yaw = v.get_actor_location(), math.radians(v.get_actor_rotation().yaw)
    o, e = target.get_actor_bounds(True)
    best = 1e9
    # sample the target's footprint rectangle
    for sx in (-1, 0, 1):
        for sy in (-1, 0, 1):
            px, py = o.x + sx * e.x - loc.x, o.y + sy * e.y - loc.y
            lx, ly = px * math.cos(yaw) + py * math.sin(yaw), -px * math.sin(yaw) + py * math.cos(yaw)
            best = min(best, math.hypot(max(FOOT[0] - lx, 0, lx - FOOT[2]), max(FOOT[1] - ly, 0, ly - FOOT[3])))
    # the target's own half-size already sampled at its corners and edges; centre inside the footprint = contact
    return best


def front_y():
    """Lane y of the body front (body leads, local -X)."""
    loc, yaw = v.get_actor_location(), math.radians(v.get_actor_rotation().yaw)
    return loc.y + math.sin(yaw) * FOOT[0]


def finish(passed, message):
    print('AGV_SAFETY_PIE_PASSED' if passed else 'AGV_SAFETY_PIE_FAILED', message)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def order(goal):
    assert nav.go_to_node(goal), (goal, nav.get_editor_property('status_text'))


def tick(dt):
    global phase, wait_until, v, nav, drive, safety, g_worker, g_person, g_carton, FOOT, case
    try:
        assert time.monotonic() - began < 1500, 'PIE timeout'
        if phase == 'begin':
            if time.monotonic() - began > .5:
                level.editor_request_begin_play()
                phase = 'find'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if phase == 'find':
            found = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.AgvTestVehicle)
            if not found:
                return
            v = found[0]
            nav, drive, safety = (v.get_editor_property(k) for k in ('navigator', 'drive', 'safety'))
            nav.set_editor_property('draw_debug', False)
            safety.set_editor_property('draw_fields', False)
            fp = safety.get_editor_property('footprint')
            FOOT = (fp.min.x, fp.min.y, fp.max.x, fp.max.y)
            named = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            g_worker, g_person, g_carton = named['AGVT_Worker'], named['AGVT_PersonDummy'], named['AGVT_Carton']
            wait_until, phase = now + 3.0, 'to_c'
            return
        if now < wait_until:
            return
        speed = abs(drive.get_editor_property('speed_cm_s'))
        state = safety.get_editor_property('state')
        if phase == 'to_c':
            order('C')
            phase = 'to_c_wait'
        elif phase == 'to_c_wait':
            if not nav.is_navigating():
                assert nav.get_editor_property('state') == unreal.AgvNavState.ARRIVED, ('setup C', nav.get_editor_property('status_text'))
                case = dict(name='1 worker standing on the lane', start=now, closest=1e9, top=0.0, warned=False, stopped_at=None)
                order('A')
                phase = 'worker'
        elif phase == 'worker':
            case['closest'] = min(case['closest'], clearance(g_worker))
            case['top'] = max(case['top'], speed)
            case['warned'] = case['warned'] or state == unreal.AgvSafetyState.WARNING
            if state == unreal.AgvSafetyState.STOP and speed < 1 and case['stopped_at'] is None:
                case['stopped_at'] = now
            if case['stopped_at'] is not None and now - case['stopped_at'] > 4.0:
                case['held'] = abs(drive.get_editor_property('speed_cm_s')) < 1
                case['gap_stopped'] = clearance(g_worker)
                g_worker.set_actor_location(unreal.Vector(LANE_X - 2000, WORKER_Y, 96), False, True)  # walk away (fixture)
                case['cleared_at'] = now
                phase = 'worker_resume'
            assert now - case['start'] < 90, ('worker case timeout', nav.get_editor_property('status_text'))
        elif phase == 'worker_resume':
            if not nav.is_navigating():
                case['arrived'] = nav.get_editor_property('state') == unreal.AgvNavState.ARRIVED
                case['resume'] = now - case['cleared_at']
                ok = case['closest'] > 0 and case['warned'] and case['held'] and case['arrived'] and case['top'] > 170
                results.append((case['name'], ok))
                print('AGV_SAFETY_PIE_CASE %s: %s top speed %.0f cm/s, warning %s, stopped %.0f cm short, held %s, '
                      'restart and arrival %s (%.1f s after it left), closest %.0f cm'
                      % (case['name'], 'SAFE' if ok else 'FAIL', case['top'], case['warned'], case['gap_stopped'], case['held'],
                         case['arrived'], case['resume'], case['closest']))
                case = dict(name='2 person appears 300 cm ahead at full speed', start=now, closest=1e9, triggered=None)
                order('C')
                phase = 'popup'
        elif phase == 'popup':
            if case['triggered'] is None:
                if speed >= 175:
                    y = front_y() + 300 + 20  # lane is driven north (+y); 300 cm from the body front to the dummy's face
                    g_person.set_actor_location(unreal.Vector(LANE_X, y, 85), False, True)
                    case.update(triggered=now, speed=speed, from_y=v.get_actor_location().y, stop_time=None)
            else:
                case['closest'] = min(case['closest'], clearance(g_person))
                if case['stop_time'] is None and speed < 1:
                    case['stop_time'] = now - case['triggered']
                    case['travel'] = abs(v.get_actor_location().y - case['from_y'])
                if case['stop_time'] is not None and now - case['triggered'] > case['stop_time'] + 1.0:
                    ok = case['closest'] > 0
                    results.append((case['name'], ok))
                    print('AGV_SAFETY_PIE_CASE %s: %s from %.0f cm/s stopped in %.2f s after %.0f cm, closest %.0f cm'
                          % (case['name'], 'SAFE' if ok else 'CONTACT', case['speed'], case['stop_time'], case['travel'], case['closest']))
                    g_person.set_actor_location(unreal.Vector(LANE_X, 0, -2000), False, True)
                    phase = 'popup_resume'
            assert now - case['start'] < 90, ('popup case timeout', nav.get_editor_property('status_text'))
        elif phase == 'popup_resume':
            if not nav.is_navigating():
                assert nav.get_editor_property('state') == unreal.AgvNavState.ARRIVED, ('after popup', nav.get_editor_property('status_text'))
                case = dict(name='3 carton falls onto the lane 300 cm ahead at full speed', start=now, closest=1e9, triggered=None)
                order('A')
                phase = 'carton'
        elif phase == 'carton':
            if case['triggered'] is None:
                if speed >= 175:
                    y = front_y() - 300 - 20  # driven south (-y)
                    g_carton.set_actor_location(unreal.Vector(LANE_X, y, 250), False, True)
                    g_carton.static_mesh_component.set_simulate_physics(True)
                    case.update(triggered=now, speed=speed, from_y=v.get_actor_location().y, stop_time=None)
            else:
                case['closest'] = min(case['closest'], clearance(g_carton))
                if case['stop_time'] is None and speed < 1:
                    case['stop_time'] = now - case['triggered']
                    case['travel'] = abs(v.get_actor_location().y - case['from_y'])
                if case['stop_time'] is not None and now - case['triggered'] > case['stop_time'] + 1.0:
                    ok = case['closest'] > 0
                    results.append((case['name'], ok))
                    print('AGV_SAFETY_PIE_CASE %s: %s from %.0f cm/s stopped in %.2f s after %.0f cm, closest %.0f cm, carton z %.0f'
                          % (case['name'], 'SAFE' if ok else 'CONTACT', case['speed'], case['stop_time'], case['travel'], case['closest'],
                             g_carton.get_actor_location().z))
                    finish(all(ok for _, ok in results), '%d / %d safe; Chaos drive, 60 Hz, NullRHI' % (sum(ok for _, ok in results), len(results)))
                    return
            assert now - case['start'] < 90, ('carton case timeout', nav.get_editor_property('status_text'))
    except Exception:
        print('AGV_SAFETY_PIE_FAILED', traceback.format_exc())
        try:
            unreal.unregister_slate_post_tick_callback(handle)
            level.editor_request_end_play()
        finally:
            unreal.SystemLibrary.quit_editor()


handle = unreal.register_slate_post_tick_callback(tick)
