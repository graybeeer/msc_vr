"""Headless check of the force-driven AGV drive on Lvl_AgvMotionTest: payload effects, grip, tipping, determinism.
Steps the simulation directly; saves nothing. Payload on the forks: centre (100, 0, 90) cm, a seated 1.1 m pallet."""
import math
import unreal

LEVEL = '/Game/AgvTest/Lvl_AgvMotionTest'
DT = 1 / 60
FORK_LOAD = unreal.Vector(100, 0, 90)

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL), LEVEL
all_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
vehicle = next(a for a in all_actors if isinstance(a, unreal.AgvTestVehicle))
nav = vehicle.get_editor_property('navigator')
drive = vehicle.get_editor_property('drive')
vehicle.get_editor_property('localizer').set_editor_property('ideal_pose', True)  # vehicle behaviour only
vehicle.get_editor_property('safety').set_editor_property('enabled', False)  # bare vehicle: no safety fields or speed caps
nav.set_editor_property('draw_debug', False)
OFFSET = drive.get_editor_property('reference_offset_cm')


def get(name):
    return drive.get_editor_property(name)


def reference():
    location, yaw = vehicle.get_actor_location(), math.radians(vehicle.get_actor_rotation().yaw)
    return location.x + math.cos(yaw) * OFFSET, location.y + math.sin(yaw) * OFFSET


def reset(payload_kg=0, x=-1000, y=-1000, yaw=0):
    nav.cancel()
    vehicle.teleport_reference(unreal.Vector2D(x, y), yaw)
    drive.clear_payload()
    if payload_kg:
        drive.set_payload(payload_kg, FORK_LOAD, unreal.Vector2D(110, 110))


def launch(payload_kg, dt=DT, speed=130):
    """Full-power start to `speed` (forks leading), then a stop command: what the vehicle itself can do."""
    reset(payload_kg)
    drive.set_command(speed, 0)
    reach, slip, margin = None, 0.0, 1.0
    for frame in range(round(6 / dt)):  # whole frames: the same 6 s at any frame rate
        vehicle.step_simulation(dt)
        t = (frame + 1) * dt
        slip = max(slip, abs(get('drive_wheel_slip_cm_s')))
        margin = min(margin, get('stability_margin'))
        if reach is None and get('speed_cm_s') >= speed * 0.95:
            reach = t
    start = reference()[0]
    drive.set_command(0, 0)
    t = 0.0
    while abs(get('speed_cm_s')) > 0.5 and t < 20:
        vehicle.step_simulation(dt)
        t += dt
        slip = max(slip, abs(get('drive_wheel_slip_cm_s')))
        margin = min(margin, get('stability_margin'))
    return dict(reach=reach, stop=reference()[0] - start, stop_time=t, slip=slip, margin=margin, end=reference(), tip=get('tip_over'))


def report(name, r):
    print('AGV_DYN_CASE %s: 0->95%% %s s, stop %.1f cm in %.2f s, max wheel slip %.1f cm/s, min stability %.2f, tip %s'
          % (name, 'never' if r['reach'] is None else '%.2f' % r['reach'], r['stop'], r['stop_time'], r['slip'], r['margin'], r['tip']))


# 1. Fixed substeps: the same manoeuvre at 30 and 120 fps ends in the same place.
a, b = launch(0, 1 / 30), launch(0, 1 / 120)
assert math.dist(a['end'], b['end']) < 0.05, ('frame-rate dependence', a['end'], b['end'])

# 2. Empty vs loaded: same command, the heavier vehicle accelerates later and stops longer.
empty, light, heavy = launch(0), launch(200), launch(400)
for name, r in (('empty', empty), ('200kg', light), ('400kg', heavy)):
    report(name, r)
assert empty['reach'] < light['reach'] < (heavy['reach'] or 99), 'acceleration vs payload'
assert empty['stop'] < light['stop'] < heavy['stop'], 'stopping distance vs payload'
assert empty['slip'] < 2.0, ('empty vehicle should keep grip', empty['slip'])

# 3. Grip: with the load on the forks the drive wheel (behind the support axle) carries little weight and spins.
assert heavy['slip'] > 5.0, ('loaded drive wheel should slip at full power', heavy['slip'])
assert not empty['tip'] and not heavy['tip']

# 4. Static capacity on the forks before the drive wheel lifts (no load wheels under the forks in this model).
def static_margin(kg):
    reset(kg)
    vehicle.step_simulation(DT)
    return get('stability_margin')

low, high = 0.0, 1400.0
for _ in range(30):
    mid = (low + high) / 2
    low, high = (mid, high) if static_margin(mid) > 0 else (low, mid)
print('AGV_DYN_CASE capacity: drive wheel lifts above %.0f kg on the forks (VNSL14 rating 1400 kg with fork load wheels)' % low)
reset(1000)
vehicle.step_simulation(DT)
assert get('tip_over'), '1000 kg on the forks must tip this wheel layout'

# 5. Route with a payload: arrives; report how precision changes.
for kg in (0, 300):
    reset(kg, 0, 0, 180)
    assert nav.go_to_node('P3')
    seconds = vehicle.simulate_until_idle(120, DT)
    assert nav.get_editor_property('state') == unreal.AgvNavState.ARRIVED, (kg, nav.get_editor_property('status_text'))
    x, y = reference()
    print('AGV_DYN_CASE route %d kg: %.1f s, stop %.2f cm, track %.2f cm, tip %s' % (
        kg, seconds, math.hypot(x - 800, y - 600), nav.get_editor_property('max_true_cross_track_error_cm'), get('tip_over')))

reset()
print('AGV_DYN_VERIFIED')
