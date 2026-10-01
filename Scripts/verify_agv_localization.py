"""Headless check of LiDAR localization on Lvl_AgvMotionTest: the vehicle drives on its own estimate (wheel odometry
with calibration errors, corrected by the top LiDAR against the map) and every frame the estimate is scored against
the actual pose, which only the simulator knows. Saves nothing."""
import math
import time
import unreal

LEVEL = '/Game/AgvTest/Lvl_AgvMotionTest'
DT = 1 / 60
STATE = unreal.AgvNavState

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL), LEVEL
all_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
vehicle = next(a for a in all_actors if isinstance(a, unreal.AgvTestVehicle))
nav = vehicle.get_editor_property('navigator')
drive = vehicle.get_editor_property('drive')
loc = vehicle.get_editor_property('localizer')
lidar = vehicle.get_editor_property('top_lidar')
nav.set_editor_property('draw_debug', False)
OFFSET = drive.get_editor_property('reference_offset_cm')
DEFAULTS = {k: loc.get_editor_property(k) for k in ('use_lidar', 'wheel_radius_error_percent', 'steer_offset_deg')}


def truth():
    location, yaw = vehicle.get_actor_location(), vehicle.get_actor_rotation().yaw
    return location.x + math.cos(math.radians(yaw)) * OFFSET, location.y + math.sin(math.radians(yaw)) * OFFSET, yaw


def error():
    """(position error cm, heading error deg) of the estimate, or None when there is none."""
    result = loc.get_estimated_pose()
    valid, position, yaw = result if len(result) == 3 else (True, *result)
    if not valid:
        return None
    x, y, true_yaw = truth()
    return math.hypot(position.x - x, position.y - y), abs(unreal.MathLibrary.normalize_axis(yaw - true_yaw))


def place(x, y, yaw, **settings):
    nav.cancel()
    drive.clear_payload()
    drive.set_editor_property('friction_coefficient', 0.6)
    lidar.set_editor_property('dropout_probability', 0.02)
    for key, value in dict(DEFAULTS, **settings).items():
        loc.set_editor_property(key, value)
    vehicle.teleport_reference(unreal.Vector2D(x, y), yaw)


def run(nodes, limit=120):
    """Drives through the nodes one order after another; returns per-frame errors and the final true stop error."""
    errors, lidar_ms, frames = [], 0.0, 0
    for node in nodes:
        assert nav.go_to_node(node), nav.get_editor_property('status_text')
        t = 0.0
        while nav.is_navigating() and t < limit:
            vehicle.step_simulation(DT)
            t += DT
            frames += 1
            lidar_ms += lidar.get_editor_property('last_step_milliseconds')
            e = error()
            if e:
                errors.append(e)
        if nav.get_editor_property('state') != STATE.ARRIVED:
            break
    return errors, lidar_ms / max(frames, 1)


def summary(errors):
    position = [e[0] for e in errors]
    return max(position), sum(position) / len(position), max(e[1] for e in errors)


GOALS = {'P0': (0, 0), 'P2': (1600, 0), 'S1': (1600, 1000), 'P5': (0, 700)}

# 1. Map and a standing vehicle.
place(0, 0, 180)
for _ in range(90):
    vehicle.step_simulation(DT)
print('AGV_LOC_CASE map: %d occupied cells, built in %.0f ms; standing still: error %.2f cm %.2f deg, inliers %.0f%%, %d points, match %.1f ms'
      % (loc.get_editor_property('map_occupied_cells'), loc.get_editor_property('map_build_milliseconds'), *error(),
         100 * loc.get_editor_property('last_inlier_ratio'), loc.get_editor_property('last_match_points'), loc.get_editor_property('last_match_milliseconds')))
assert loc.get_editor_property('map_occupied_cells') > 1000, 'map built'
assert loc.get_editor_property('accepted_matches') >= 1 and error()[0] < 3.0

# 2. A long tour on the estimate: odometry only vs odometry + LiDAR.
TOUR = ['S1', 'P5', 'P2', 'P0']
results = {}
for name, use_lidar in (('odometry only', False), ('lidar', True)):
    place(0, 0, 180, use_lidar=use_lidar)
    errors, lidar_ms = run(TOUR)
    arrived = nav.get_editor_property('state') == STATE.ARRIVED
    x, y, _ = truth()
    stop = math.hypot(x - GOALS['P0'][0], y - GOALS['P0'][1])
    worst, mean, heading = summary(errors)
    results[name] = (worst, stop, arrived)
    print('AGV_LOC_CASE tour %s: arrived %s, estimate error max %.1f / mean %.1f cm, heading max %.2f deg, true stop error %.1f cm, '
          'matches %d ok / %d rejected, LiDAR %.2f ms per frame'
          % (name, arrived, worst, mean, heading, stop, loc.get_editor_property('accepted_matches'), loc.get_editor_property('rejected_matches'), lidar_ms))
assert results['lidar'][2], 'arrives with LiDAR'
assert results['lidar'][0] < 5.0, ('estimate error with LiDAR', results['lidar'])
assert results['odometry only'][0] > 2 * results['lidar'][0], 'LiDAR must beat odometry alone'

# 3. Slippery floor and a load: the drive wheel spins, the encoder counts it; LiDAR keeps the estimate.
for name, use_lidar in (('odometry only', False), ('lidar', True)):
    place(0, 0, 180, use_lidar=use_lidar)
    drive.set_editor_property('friction_coefficient', 0.15)
    drive.set_payload(300, unreal.Vector(100, 0, 90), unreal.Vector2D(110, 110))
    errors, _ = run(['P2', 'P0'])
    worst, mean, heading = summary(errors)
    arrived = nav.get_editor_property('state') == STATE.ARRIVED
    print('AGV_LOC_CASE slippery (mu 0.15) + 300 kg, %s: arrived %s, estimate error max %.1f / mean %.1f / final %.1f cm'
          % (name, arrived, worst, mean, errors[-1][0]))
    results['slip ' + name] = (worst, errors[-1][0], arrived)
# Wheel spin between scans briefly fools the odometry; LiDAR bounds it, recovers, and the vehicle still arrives.
assert results['slip lidar'][2] and results['slip lidar'][0] < 25.0 and results['slip lidar'][1] < 5.0, results['slip lidar']
assert results['slip odometry only'][0] > 5 * results['slip lidar'][0]

# 4. Wrong initial pose (20 cm, 3 deg): the LiDAR pulls the estimate back while standing.
place(800, 0, 180)
loc.offset_estimate(unreal.Vector2D(14, -14), 3)
before = error()
for _ in range(180):
    vehicle.step_simulation(DT)
after = error()
print('AGV_LOC_CASE wrong start pose: %.1f cm %.1f deg -> %.2f cm %.2f deg after 3 s' % (*before, *after))
assert after[0] < 3.0 and after[1] < 0.5

# 5. Blind LiDAR: odometry carries on, then the estimate is declared lost and the vehicle stops.
place(0, 0, 180)
lidar.set_editor_property('dropout_probability', 1.0)
assert nav.go_to_node('P2')
vehicle.simulate_until_idle(60, DT)
print('AGV_LOC_CASE blind LiDAR: %s / %s after %.0f cm' % (nav.get_editor_property('status_text'), loc.get_editor_property('status_text'),
                                                          loc.get_editor_property('travel_since_match_cm')))
assert nav.get_editor_property('status_text') == 'LOCALIZATION LOST'

place(0, 0, 180)
print('AGV_LOC_VERIFIED')
