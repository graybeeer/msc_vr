"""Warehouse tour of the AGV prototype (AGV_WH_Vehicle in Lvl_FirstPerson) in real PIE time on the Chaos rigid-body
drive: arrival, stop error at the node, tracking, LiDAR localization error against the actual pose, safety stops,
tipping. The teammates' forklifts are kept parked (auto start off, editor world only) so they do not cross the tour;
the warehouse workers stay as they are. Never saves.

Run (one editor process per check):
UnrealEditor.exe msc_vr.uproject -ExecCmds="py <abs path>/verify_agv_warehouse_pie.py" -nullrhi -unattended -nosplash -nop4
Success line: AGV_WAREHOUSE_PIE_PASSED."""
import math
import time
import traceback
import unreal

TOUR = ['PL5A', 'E', 'PL5A', 'C', 'L', 'PR8B', 'A', 'K', 'I', 'PL2A', 'PR10B', 'D', 'B']
LEG_TIMEOUT = 180.0
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

began = time.monotonic()
phase, settle_until = 0, 0.0
v = nav = drive = loc = safety = None
nodes = {}
leg = None
results = []


def truth():
    p, yaw = v.get_actor_location(), v.get_actor_rotation().yaw
    offset = drive.get_editor_property('reference_offset_cm')
    return p.x + math.cos(math.radians(yaw)) * offset, p.y + math.sin(math.radians(yaw)) * offset, yaw


def est_error():
    res = loc.get_estimated_pose()
    if res is None:
        return 1e9
    ok, pos, _ = res if len(res) == 3 else (True, *res)
    x, y, _ = truth()
    return math.hypot(pos.x - x, pos.y - y) if ok else 1e9


def surroundings(game):
    """Actors within 3 m of the vehicle when a leg fails (what the safety field may be holding for)."""
    here = v.get_actor_location()
    for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor):
        if a == v:
            continue
        o, ext = a.get_actor_bounds(True)
        if ext.x > 2000 or ext.y > 2000:
            continue
        gap = math.hypot(max(abs(o.x - here.x) - ext.x, 0), max(abs(o.y - here.y) - ext.y, 0))
        if gap < 300 and o.z - ext.z < here.z + 220 and o.z + ext.z > here.z + 5:
            print('AGV_WAREHOUSE_PIE_NEAR %s (%s) gap %.0f cm at %s' % (a.get_actor_label(), a.get_class().get_name(), gap, o))


def finish(passed, message):
    print('AGV_WAREHOUSE_PIE_PASSED' if passed else 'AGV_WAREHOUSE_PIE_FAILED', message)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def start_leg(game, now):
    global leg
    goal = TOUR[len(results)]
    assert nav.go_to_node(goal), (goal, nav.get_editor_property('status_text'))
    leg = dict(goal=goal, start=now, stops=safety.get_editor_property('safety_stops'), worst=0.0, tip=False, tilt=0.0, inlier=1.0,
               rejected=loc.get_editor_property('rejected_matches'))


def tick(dt):
    global phase, settle_until, v, nav, drive, loc, safety, nodes
    try:
        assert time.monotonic() - began < 1800, 'PIE timeout'
        if phase == 0:
            if time.monotonic() - began > .5:
                level.editor_request_begin_play()
                phase = 1
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if phase == 1:
            found = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.AgvTestVehicle)
            if not found:
                return
            v = found[0]
            nav, drive, loc, safety = (v.get_editor_property(k) for k in ('navigator', 'drive', 'localizer', 'safety'))
            nav.set_editor_property('draw_debug', False)
            safety.set_editor_property('draw_fields', False)
            nodes = {str(a.get_editor_property('node_id')): a.get_actor_location()
                     for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.AgvRouteNode)}
            settle_until = now + 3.0  # let the body settle on its tyres and the first scans come in
            phase = 2
            return
        if phase == 2:
            if now >= settle_until:
                start_leg(game, now)
                phase = 3
            return
        # phase 3: driving the tour
        leg['worst'] = max(leg['worst'], est_error())
        leg['tip'] = leg['tip'] or drive.get_editor_property('tip_over')
        rot = v.get_actor_rotation()
        leg['tilt'] = max(leg['tilt'], abs(rot.pitch), abs(rot.roll))
        leg['inlier'] = min(leg['inlier'], loc.get_editor_property('last_inlier_ratio'))
        if nav.is_navigating() and now - leg['start'] < LEG_TIMEOUT:
            return
        x, y, yaw = truth()
        n = nodes[leg['goal']]
        state = nav.get_editor_property('state')
        r = dict(goal=leg['goal'], arrived=state == unreal.AgvNavState.ARRIVED, status=str(nav.get_editor_property('status_text')),
                 seconds=now - leg['start'], stop=math.hypot(x - n.x, y - n.y), yaw=yaw,
                 stops=safety.get_editor_property('safety_stops') - leg['stops'], worst=leg['worst'],
                 track=nav.get_editor_property('max_true_cross_track_error_cm'), tip=leg['tip'], tilt=leg['tilt'], inlier=leg['inlier'],
                 rejected=loc.get_editor_property('rejected_matches') - leg['rejected'])
        results.append(r)
        print('AGV_WAREHOUSE_PIE_CASE %(goal)s: %(status)s in %(seconds).1f s, stop error %(stop).1f cm, heading %(yaw).1f deg, '
              'safety stops %(stops)d, track %(track).1f cm, localization max %(worst).1f cm, tip %(tip)s, '
              'tilt max %(tilt).2f deg, inlier min %(inlier).2f, rejected matches %(rejected)d' % r)
        if not r['arrived'] or r['tip']:
            surroundings(game)
            finish(False, 'leg %s' % r['goal'])
            return
        if len(results) == len(TOUR):
            worst = max(c['worst'] for c in results)
            stop = max(c['stop'] for c in results)
            stops = sum(c['stops'] for c in results)
            total = sum(c['seconds'] for c in results)
            print('AGV_WAREHOUSE_PIE_SUMMARY %d legs in %.0f s, stop error max %.1f cm, localization max %.1f cm, safety stops %d'
                  % (len(results), total, stop, worst, stops))
            finish(worst < 10.0 and stop < 10.0, 'Chaos drive, 60 Hz, NullRHI; teammate forklifts parked')
            return
        start_leg(game, now)
    except Exception:
        print('AGV_WAREHOUSE_PIE_FAILED', traceback.format_exc())
        try:
            unreal.unregister_slate_post_tick_callback(handle)
            level.editor_request_end_play()
        finally:
            unreal.SystemLibrary.quit_editor()


handle = unreal.register_slate_post_tick_callback(tick)
