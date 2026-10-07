"""Headless check of the AGV placed in Lvl_FirstPerson by place_agv_in_warehouse.py: the saved vehicle drives the
ground-floor lanes and stops at pallet staging nodes on its LiDAR estimate, with the safety system on. Saves nothing.
Success line: AGV_WAREHOUSE_VERIFIED."""
import math
import unreal

DT = 1 / 60
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert les.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors = eas.get_all_level_actors()
v = next((a for a in actors if a.get_actor_label() == 'AGV_WH_Vehicle'), None)
assert v, 'run place_agv_in_warehouse.py first'
nodes = {str(a.get_editor_property('node_id')): a for a in actors if a.get_actor_label().startswith('AGV_WH_') and isinstance(a, unreal.AgvRouteNode)}
nav, drive, loc, safety = (v.get_editor_property(k) for k in ('navigator', 'drive', 'localizer', 'safety'))
nav.set_editor_property('draw_debug', False)
safety.set_editor_property('draw_fields', False)
OFFSET = drive.get_editor_property('reference_offset_cm')

def truth():
    p, yaw = v.get_actor_location(), v.get_actor_rotation().yaw
    r = math.radians(yaw)
    return p.x + math.cos(r) * OFFSET, p.y + math.sin(r) * OFFSET, yaw

def est_error():
    res = loc.get_estimated_pose()
    ok, pos, _ = res if len(res) == 3 else (True, *res)
    x, y, _ = truth()
    return math.hypot(pos.x - x, pos.y - y) if ok else 1e9

for _ in range(60):
    v.step_simulation(DT)
# Starts from the saved pose (B, facing north) and includes trips back the way the vehicle came, which need it to back
# up (forks first) rather than turn round in a narrow lane.
TOUR = ['PL5A', 'E', 'PL5A', 'C', 'L', 'PR8B', 'A', 'K', 'I', 'PL2A', 'PR10B', 'D', 'B']
worst_err, stops = 0.0, 0
for goal in TOUR:
    assert goal in nodes, goal
    assert nav.go_to_node(goal), (goal, nav.get_editor_property('status_text'))
    before = safety.get_editor_property('safety_stops')
    t = 0.0
    while nav.is_navigating() and t < 180:
        v.step_simulation(DT)
        t += DT
        worst_err = max(worst_err, est_error())
    x, y, yaw = truth()
    n = nodes[goal].get_actor_location()
    arrived = nav.get_editor_property('state') == unreal.AgvNavState.ARRIVED
    stops += safety.get_editor_property('safety_stops') - before
    print('AGV_WAREHOUSE_CASE %s: %s in %.1f s, stop error %.1f cm, heading %.1f deg, safety stops %d, track %.1f cm' % (
        goal, nav.get_editor_property('status_text'), t, math.hypot(x - n.x, y - n.y), yaw,
        safety.get_editor_property('safety_stops') - before, nav.get_editor_property('max_true_cross_track_error_cm')))
    assert arrived, goal
print('AGV_WAREHOUSE_CASE localization max error %.1f cm, safety stops %d' % (worst_err, stops))
assert worst_err < 10.0

# The warehouse changes all the time: cargo is taken away and moved, pallets go, racks empty. Localization uses only the
# permanent structure, so the same tour must stay as accurate. Deterministic changes (seeded); nothing is saved.
import random
rng = random.Random(7)
cargo = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.WarehouseCargo)]
pallets = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.WarehousePallet)]
removed = moved = 0
for a in cargo:
    r = rng.random()
    if r < 0.4:
        eas.destroy_actor(a); removed += 1
    elif r < 0.7:
        ang, dist = rng.uniform(0, 2 * math.pi), rng.uniform(10, 40)
        a.set_actor_location(a.get_actor_location() + unreal.Vector(math.cos(ang) * dist, math.sin(ang) * dist, 0), False, True); moved += 1
gone = 0
for a in pallets:
    if rng.random() < 0.2:
        eas.destroy_actor(a); gone += 1
changed_err = 0.0
for goal in ['PL5A', 'C', 'L', 'PR8B', 'A', 'K', 'B']:
    assert nav.go_to_node(goal), (goal, nav.get_editor_property('status_text'))
    t = 0.0
    while nav.is_navigating() and t < 180:
        v.step_simulation(DT)
        t += DT
        changed_err = max(changed_err, est_error())
    assert nav.get_editor_property('state') == unreal.AgvNavState.ARRIVED, ('changed warehouse', goal)
print('AGV_WAREHOUSE_CASE changed warehouse (%d cargo removed, %d moved, %d pallets removed): localization max error %.1f cm' % (
    removed, moved, gone, changed_err))
assert changed_err < 10.0
print('AGV_WAREHOUSE_VERIFIED')
