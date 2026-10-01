"""Headless check of AGV navigation on Lvl_AgvMotionTest. Steps the simulation directly; saves nothing."""
import math
import unreal

LEVEL = '/Game/AgvTest/Lvl_AgvMotionTest'
DT = 1 / 60
STATE = unreal.AgvNavState

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL), LEVEL
all_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
vehicle = next(a for a in all_actors if isinstance(a, unreal.AgvTestVehicle))
nav = vehicle.get_editor_property('navigator')
drive = vehicle.get_editor_property('drive')
localizer = vehicle.get_editor_property('localizer')
localizer.set_editor_property('ideal_pose', True)  # navigation logic only; LiDAR is checked in verify_agv_localization.py
nav.set_editor_property('draw_debug', False)
OFFSET = drive.get_editor_property('reference_offset_cm')


def reference():
    location, yaw = vehicle.get_actor_location(), math.radians(vehicle.get_actor_rotation().yaw)
    return location.x + math.cos(yaw) * OFFSET, location.y + math.sin(yaw) * OFFSET


def place(x, y, yaw):
    nav.cancel()
    vehicle.teleport_reference(unreal.Vector2D(x, y), yaw)
    localizer.set_editor_property('available', True)
    localizer.set_editor_property('position_bias_cm', unreal.Vector2D(0, 0))


def state():
    return nav.get_editor_property('state')


def drive_to(node, goal, position_cm=1.0, track_cm=5.0, limit=120.0):  # 5 cm: steering lag at straight-to-arc entries
    assert nav.go_to_node(node), nav.get_editor_property('status_text')
    seconds = vehicle.simulate_until_idle(limit, DT)
    assert state() == STATE.ARRIVED, (node, nav.get_editor_property('status_text'))
    x, y = reference()
    miss = math.hypot(x - goal[0], y - goal[1])
    worst = nav.get_editor_property('max_true_cross_track_error_cm')
    assert miss <= position_cm, (node, 'stop error', miss)
    assert worst <= track_cm, (node, 'tracking error', worst)
    print('AGV_NAV_CASE %s: %.1fs stop %.2fcm track %.2fcm via %s' % (node, seconds, miss, worst, [str(n) for n in nav.get_editor_property('planned_node_ids')]))
    return seconds


# 1. Straight lane, forks trailing.
place(0, 0, 180)
drive_to('P2', (1600, 0), track_cm=0.5)

# 2. One arc corner.
place(0, 0, 180)
drive_to('P3', (800, 600))
assert [str(n) for n in nav.get_editor_property('planned_node_ids')] == ['P1', 'P3'], 'shortest route'

# 3. Two arcs, a pivot corner (P4 radius 0) and alignment to the station yaw.
place(0, 0, 180)
drive_to('S1', (1600, 1000))
assert abs(unreal.MathLibrary.normalize_axis(vehicle.get_actor_rotation().yaw)) <= 0.5, 'station yaw'

# 4. Forks leading.
nav.set_editor_property('drive_reversed', False)
place(0, 0, 0)
drive_to('P3', (800, 600))
nav.set_editor_property('drive_reversed', True)

# 5. Starting mid-lane drives straight to the goal instead of detouring to a node.
place(400, 0, 180)
assert drive_to('P0', (0, 0), track_cm=0.5) < 14  # a detour via a node takes far longer

# 6. Off the network: joins at the nearest node first.
place(300, -300, 180)
drive_to('P2', (1600, 0))

# 7. A biased position estimate: the vehicle believes it is on the lane while really 5 cm beside it.
place(0, 0, 180)
localizer.set_editor_property('position_bias_cm', unreal.Vector2D(0, 5))
assert nav.go_to_node('P2')
vehicle.simulate_until_idle(120, DT)
assert state() == STATE.ARRIVED
assert abs(nav.get_editor_property('cross_track_error_cm')) < 0.5, 'estimated error'
assert 4.5 < abs(reference()[1]) < 5.5, ('true offset', reference())

# 8. Rejected orders leave the vehicle idle.
place(0, 0, 180)
assert not nav.go_to_node('NOPE') and 'UNKNOWN' in nav.get_editor_property('status_text')
assert not nav.go_to_node('ISO') and 'NO ROUTE' in nav.get_editor_property('status_text')
assert state() == STATE.IDLE

# 9. Pushed off the lane while driving -> PATH DEVIATION fault and stop.
place(0, 0, 180)
assert nav.go_to_node('P2')
for _ in range(240):
    vehicle.step_simulation(DT)
x, y = reference()
vehicle.teleport_reference(unreal.Vector2D(x, y + 30), 180)
vehicle.step_simulation(DT)
assert state() == STATE.FAULT and nav.get_editor_property('status_text') == 'PATH DEVIATION'

# 10. Losing the position estimate while driving -> fault.
place(0, 0, 180)
assert nav.go_to_node('P2')
for _ in range(120):
    vehicle.step_simulation(DT)
localizer.set_editor_property('available', False)
vehicle.step_simulation(DT)
assert state() == STATE.FAULT and nav.get_editor_property('status_text') == 'LOCALIZATION LOST'

# 11. Wheels (tricycle drive). Stop and steer: facing the wrong way, the drive wheel turns to 90 deg
#     before the vehicle starts to pivot.
def wheels():
    support = drive.get_editor_property('passive_wheels')  # [0] = local +Y, [1] = local -Y
    return (drive.get_editor_property('steer_angle_deg'), drive.get_editor_property('drive_wheel_travel_cm'),
            support[0].get_editor_property('travel_cm'), support[1].get_editor_property('travel_cm'))


place(0, 0, 0)
start_yaw = vehicle.get_actor_rotation().yaw
assert nav.go_to_node('P2')
for _ in range(120):
    vehicle.step_simulation(DT)
    if abs(wheels()[0]) >= 90 - drive.get_editor_property('steer_align_tolerance_deg'):
        break
    assert abs(vehicle.get_actor_rotation().yaw - start_yaw) < 0.01, 'turned before steering'
assert abs(wheels()[0]) > 80, ('steer for pivot', wheels()[0])
vehicle.simulate_until_idle(120, DT)
assert state() == STATE.ARRIVED

# 12. Straight run: the wheels roll exactly the distance travelled; the steering ends straight.
place(0, 0, 180)
before = wheels()
drive_to('P2', (1600, 0), track_cm=0.5)
after = wheels()
rolled = [abs(after[i] - before[i]) for i in (1, 2, 3)]
assert all(abs(r - 1600) < 2 for r in rolled), ('straight roll', rolled)
assert abs(after[0]) < 0.5, ('steer after straight', after[0])

# 13. Corner: the steering turns into the arc and the outer support wheel rolls farther than the inner one.
place(0, 0, 180)
before = wheels()
assert nav.go_to_node('P3')
max_steer = 0.0
while nav.is_navigating():
    vehicle.step_simulation(DT)
    max_steer = max(max_steer, abs(wheels()[0]))
after = wheels()
pos_y, neg_y = abs(after[2] - before[2]), abs(after[3] - before[3])
assert max_steer > 20, ('steer in corner', max_steer)
assert abs(pos_y - neg_y) > 20, ('support wheels', pos_y, neg_y)
print('AGV_NAV_CASE wheels: corner steer %.1fdeg, support wheels %.0f / %.0f cm' % (max_steer, pos_y, neg_y))

print('AGV_NAV_VERIFIED')
