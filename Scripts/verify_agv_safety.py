"""Headless check of the safety scanner on Lvl_AgvMotionTest: obstacles on the lane while the vehicle drives body-first
on its own (LiDAR) localization. Measures the real gap between the body front and the obstacle. Spawned obstacles
are removed again and nothing is saved."""
import math
import unreal

LEVEL = '/Game/AgvTest/Lvl_AgvMotionTest'
DT = 1 / 60
STATE = unreal.AgvNavState
SAFETY = unreal.AgvSafetyState

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level(LEVEL), LEVEL
vehicle = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.AgvTestVehicle))
nav = vehicle.get_editor_property('navigator')
drive = vehicle.get_editor_property('drive')
safety = vehicle.get_editor_property('safety')
nav.set_editor_property('draw_debug', False)
safety.set_editor_property('draw_fields', False)
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
BODY_FRONT = -safety.get_editor_property('footprint').min.x  # body front; it leads toward world +X at yaw 180


def place(x=0, y=0, yaw=180, payload=0):
    nav.cancel()
    drive.clear_payload()
    if payload:
        drive.set_payload(payload, unreal.Vector(55, 0, 90), unreal.Vector2D(110, 110))
    vehicle.teleport_reference(unreal.Vector2D(x, y), yaw)
    for _ in range(150):  # settle 2.5 s: localization map built before any obstacle exists, a previous stop released
        vehicle.step_simulation(DT)
    assert safety.get_editor_property('state') == SAFETY.CLEAR


def obstacle(x, y, size):  # spawned at runtime, not part of the localization map
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, size[2] / 2))
    actor.static_mesh_component.set_static_mesh(cube)
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100, size[1] / 100, size[2] / 100))
    actor.set_actor_label('AGV_TestObstacle')
    return actor


def gap(near_face_x):
    return near_face_x - (vehicle.get_actor_location().x + BODY_FRONT)


def drive_until(seconds, stop_when=None):
    seen, t = set(), 0.0
    while t < seconds and nav.is_navigating():
        vehicle.step_simulation(DT)
        t += DT
        seen.add(safety.get_editor_property('state'))
        if stop_when and stop_when():
            break
    return seen, t


def standing():
    return safety.get_editor_property('state') == SAFETY.STOP and abs(drive.get_editor_property('speed_cm_s')) < 0.5


# 1. A person (40 x 40 x 170 cm) on the lane: warn, slow, stop short of them; once they step away, restart and arrive.
for payload in (0, 300):
    place(payload=payload)
    person = obstacle(900, 0, (40, 40, 170))
    assert nav.go_to_node('P2')
    seen, t = drive_until(40, standing)
    stopped_gap = gap(880)
    for _ in range(60):  # stays stopped while the person is there
        vehicle.step_simulation(DT)
    held = standing()
    actors.destroy_actor(person)
    seen2, t2 = drive_until(60)
    print('AGV_SAFETY_CASE person on lane, %d kg: warning %s, stopped %.0f cm short of them after %.1f s, held %s, '
          'restarted after removal and %s in %.1f s'
          % (payload, SAFETY.WARNING in seen, stopped_gap, t, held, nav.get_editor_property('status_text'), t2))
    assert SAFETY.WARNING in seen and SAFETY.STOP in seen, seen
    assert stopped_gap > 10, ('must stop clear of the person', stopped_gap)
    assert held, 'stays stopped while the field is occupied'
    assert nav.get_editor_property('state') == STATE.ARRIVED, nav.get_editor_property('status_text')

# 2. Something beside the lane, outside the fields: no reaction.
place()
beside = obstacle(900, 150, (40, 40, 170))
stops = safety.get_editor_property('safety_stops')
assert nav.go_to_node('P2')
seen, t = drive_until(40)
actors.destroy_actor(beside)
print('AGV_SAFETY_CASE object 130 cm beside the lane: states seen %s, arrived in %.1f s' % (sorted(str(s) for s in seen), t))
assert SAFETY.STOP not in seen and SAFETY.WARNING not in seen and nav.get_editor_property('state') == STATE.ARRIVED

# 3. Scan plane height: a 30 cm box is seen, a 10 cm one is under the beam (as with a real scanner at 17 cm).
for height, expect in ((30, True), (10, False)):
    place()
    box = obstacle(900, 0, (60, 60, height))
    assert nav.go_to_node('P2')
    seen, t = drive_until(40, standing)
    detected = SAFETY.STOP in seen
    print('AGV_SAFETY_CASE %d cm box on the lane: detected %s%s' % (height, detected, ', stopped %.0f cm short' % gap(870) if detected else ''))
    actors.destroy_actor(box)
    assert detected == expect, (height, detected)
    nav.cancel()

# 4. Someone steps in 200 cm ahead at full speed (straight into the protective field, no warning first).
for payload in (0, 300):
    place(payload=payload)
    assert nav.go_to_node('P2')
    while abs(drive.get_editor_property('speed_cm_s')) < 125:
        vehicle.step_simulation(DT)
    speed = abs(drive.get_editor_property('speed_cm_s'))
    face = vehicle.get_actor_location().x + BODY_FRONT + 200
    person = obstacle(face + 20, 0, (40, 40, 170))
    t = 0.0
    while abs(drive.get_editor_property('speed_cm_s')) > 0.5 and t < 5:  # the emergency stop itself
        vehicle.step_simulation(DT)
        t += DT
    stopped = gap(face)
    closest = stopped
    for _ in range(240):  # afterwards it may creep up at warning speed to the standstill protective field (~1 m)
        vehicle.step_simulation(DT)
        closest = min(closest, gap(face))
    actors.destroy_actor(person)
    print('AGV_SAFETY_CASE person steps in 200 cm ahead at %.0f cm/s, %d kg: stopped in %.2f s after %.0f cm, %.0f cm short of them; '
          'later creeps to %.0f cm' % (speed, payload, t, 200 - stopped, stopped, closest))
    assert stopped > 100, ('emergency stop must keep over 1 m', stopped)
    assert closest > 90, ('restart approach must respect the protective field', closest)
    nav.cancel()

# 5. Warning field and the map: turning on the spot about the rear axle at (400, 0) swings the forks past the mapped
#    pillar at (400, -172)
#    (about 27 cm clear): no slowdown for mapped structure. The same swing with an unmapped person-sized object in it
#    must still warn / stop (the protective field never uses the map).
def pivot_states(blocker=None):
    place(400, 0, 180)
    nav.set_editor_property('choose_leg_direction', False)  # turn round here rather than back up
    actor = obstacle(*blocker) if blocker else None
    assert nav.go_to_node('P0')
    seen, ignored = set(), 0
    for _ in range(int(40 / DT)):
        vehicle.step_simulation(DT)
        seen.add(safety.get_editor_property('state'))
        ignored = max(ignored, safety.get_editor_property('mapped_points_ignored'))
        if not nav.is_navigating():
            break
    if actor:
        actors.destroy_actor(actor)
    nav.cancel()
    nav.set_editor_property('choose_leg_direction', True)
    return seen, ignored

seen, ignored = pivot_states()
print('AGV_SAFETY_CASE pivot past the mapped pillar: states %s, up to %d mapped points ignored' % (sorted(str(x) for x in seen), ignored))
assert SAFETY.WARNING not in seen and SAFETY.STOP not in seen and ignored > 0
seen, _ = pivot_states((330, -95, (40, 40, 170)))
print('AGV_SAFETY_CASE same pivot with an unmapped person-sized object in the swing: states %s' % sorted(str(x) for x in seen))
assert SAFETY.STOP in seen or SAFETY.WARNING in seen

place()
print('AGV_SAFETY_VERIFIED')
