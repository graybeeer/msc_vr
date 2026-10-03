"""Hazard report on Lvl_AgvMotionTest with the project's real WarehouseWorker pawns: does the AGV stop for people and
falling cargo in the situations that matter on a shop floor? The vehicle drives on its own LiDAR localization.
Contact is measured between the real footprint (body + forks) and the worker's capsule. Each scenario reports
SAFE / CONTACT with the closest clearance. Nothing is saved."""
import math
import unreal

LEVEL = '/Game/AgvTest/Lvl_AgvMotionTest'
DT = 1 / 60
STATE = unreal.AgvNavState

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
OFFSET = drive.get_editor_property('reference_offset_cm')
# Footprint in the actor frame (cm): body and the two forks.
FOOTPRINT = [((-94, -61), (37, 61)), ((37, -40), (182, -22)), ((37, 22), (182, 40))]
results = []


def place(x, y, yaw, forks_lead=False):
    nav.cancel()
    nav.set_editor_property('drive_reversed', not forks_lead)
    vehicle.teleport_reference(unreal.Vector2D(x, y), yaw)
    for _ in range(150):
        vehicle.step_simulation(DT)


def worker(x, y):
    actor = actors.spawn_actor_from_class(unreal.WarehouseWorker, unreal.Vector(x, y, 96))
    actor.set_actor_label('AGV_TestWorker')
    return actor


def box(x, y, z, size):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, z))
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100, size[1] / 100, size[2] / 100))
    actor.set_actor_label('AGV_TestCargo')
    return actor


def clearance(target, radius):
    """Distance from the vehicle footprint to a circle (cm); <= 0 is contact."""
    location, yaw = vehicle.get_actor_location(), math.radians(vehicle.get_actor_rotation().yaw)
    p = target.get_actor_location()
    dx, dy = p.x - location.x, p.y - location.y
    lx, ly = dx * math.cos(yaw) + dy * math.sin(yaw), -dx * math.sin(yaw) + dy * math.cos(yaw)
    best = 1e9
    for (x0, y0), (x1, y1) in FOOTPRINT:
        best = min(best, math.hypot(max(x0 - lx, 0, lx - x1), max(y0 - ly, 0, ly - y1)))
    return best - radius


def watch(target, radius, seconds, each_frame=None):
    closest, stopped, t = 1e9, False, 0.0
    while t < seconds and nav.is_navigating():
        if each_frame:
            each_frame(t)
        vehicle.step_simulation(DT)
        t += DT
        closest = min(closest, clearance(target, radius))
        stopped = stopped or safety.get_editor_property('state') == unreal.AgvSafetyState.STOP
    return closest, stopped


def report(name, closest, stopped, note=''):
    verdict = 'SAFE' if closest > 0 else 'CONTACT'
    results.append((name, verdict))
    print('AGV_HAZARD %-58s %-7s closest %6.0f cm, safety stop %-5s %s' % (name, verdict, closest, stopped, note))


# A. Worker standing on the lane, vehicle at full speed body-first.
place(0, 0, 180)
person = worker(900, 0)
assert nav.go_to_node('P2')
report('A worker standing on the lane', *watch(person, 34, 25))
actors.destroy_actor(person)

# B. Worker walks across the lane at 1.4 m/s, timed to be in the path 250 cm ahead of the body at full speed.
place(0, 0, 180)
assert nav.go_to_node('P2')
while abs(drive.get_editor_property('speed_cm_s')) < 125:
    vehicle.step_simulation(DT)
front = vehicle.get_actor_location().x + 94
cross_x = front + 125 * 2.0 + 250  # where the body front would be in 2 s, plus 250 cm
person = worker(cross_x, -280)
walk = lambda t: person.set_actor_location(unreal.Vector(cross_x, -280 + 140 * t, 96), False, True)
report('B worker walks across the lane ahead (1.4 m/s)', *watch(person, 34, 12, walk))
actors.destroy_actor(person)

# C. Worker on the lane just past a corner (out of view until the vehicle turns).
place(0, 0, 180)
person = worker(800, 260)
assert nav.go_to_node('P3')
report('C worker on the lane just around a corner', *watch(person, 34, 30))
actors.destroy_actor(person)

# D. Worker beside the lane (outside every field on the approach) where the forks end up after the pivot at P4:
#    turning from heading east to north body-first swings the forks through the south-west quadrant.
place(1200, 600, 180)
person = worker(1552, 468)
assert nav.go_to_node('S1')
report('D worker in the fork swing of a pivot turn', *watch(person, 34, 40), '(fork sensors turn with the vehicle: late view)')
actors.destroy_actor(person)

# E. Forks-first travel toward a worker.
place(0, 0, 0, forks_lead=True)
person = worker(900, 0)
assert nav.go_to_node('P2')
report('E forks-first travel toward a worker', *watch(person, 34, 25), '(forks-first capped at 50 cm/s)')
actors.destroy_actor(person)
nav.set_editor_property('drive_reversed', True)

# F. A carton (60 x 40 x 40 cm) falls off a 250 cm rack level into the lane 300 cm ahead of the body at full speed.
place(0, 0, 180)
assert nav.go_to_node('P2')
while abs(drive.get_editor_property('speed_cm_s')) < 125:
    vehicle.step_simulation(DT)
drop_x = vehicle.get_actor_location().x + 94 + 300
carton = box(drop_x, 0, 270, (60, 40, 40))
fall = lambda t: carton.set_actor_location(unreal.Vector(drop_x, 0, max(20.0, 270 - 0.5 * 981 * t * t)), False, True)
report('F carton falls from the rack into the lane ahead', *watch(carton, 30, 12, fall))
actors.destroy_actor(carton)

nav.cancel()
place(0, 0, 180)
print('AGV_HAZARD_SUMMARY %d / %d safe: %s' % (sum(v == 'SAFE' for _, v in results), len(results), ', '.join('%s=%s' % (n[0], v) for n, v in results)))
