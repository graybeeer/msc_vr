"""Place the AGV route network and test vehicle in /Game/FirstPerson/Lvl_FirstPerson (ground floor) and save ONLY the
new AGV_WH_ actors. The level uses one file per actor (__ExternalActors__), so this adds files and does not rewrite
the map or the teammates' actors. Rerun to rebuild them (old AGV_WH_ actors are deleted first, also from disk).

Lanes (2026-10-06 survey, >= 1 m from anything to the lane centre): around the central racks (x +-380), across their
ends (y +-1580), the central aisle (x 0, one node per pallet slot row), east to the teammate lane (x 1350).
Pallet staging nodes PL<bay><A|B> / PR<bay><A|B>: reference point 150.3 cm from the aisle-side pallet face (fork tips
30 cm short of it), facing the pallet; reached from the aisle node of the same row. Recomputed from the pallets, so
rerun after the racks move. Play, then the console: agv.goto C  /  agv.goto PL5A
Success line: AGV_WH_PLACED."""
import re
import unreal

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
PREFIX = 'AGV_WH_'
FORK_TIP, STANDOFF = 115.0, 30.0
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert les.load_level(LEVEL), LEVEL

old = [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith(PREFIX)]
old_packages = [a.get_outermost() for a in old]
for a in old:
    eas.destroy_actor(a)
if old_packages:
    unreal.EditorLoadingAndSavingUtils.save_packages(old_packages, False)  # deletes their actor files

LANES = {
    'A': (-380, -1580), 'B': (-380, 0), 'C': (-380, 1580), 'D': (0, 1580), 'E': (380, 1580), 'F': (380, 400),
    'G': (380, 0), 'H': (380, -1275), 'I': (380, -1580), 'J': (0, -1580), 'K': (1350, -1275), 'L': (1350, 400),
}
LINKS = [('A', 'B'), ('B', 'C'), ('C', 'D'), ('D', 'E'), ('E', 'F'), ('F', 'G'), ('G', 'H'), ('H', 'I'), ('I', 'J'),
         ('J', 'A'), ('F', 'L'), ('H', 'K'), ('K', 'L')]
spawned, nodes = [], {}

def node(nid, x, y, yaw=None):
    n = eas.spawn_actor_from_class(unreal.AgvRouteNode, unreal.Vector(x, y, 0), unreal.Rotator(0, 0, yaw or 0))
    n.set_actor_label(PREFIX + nid)
    n.set_folder_path('AGV')
    n.set_editor_property('node_id', nid)
    n.set_editor_property('align_on_arrival', yaw is not None)
    nodes[nid] = n
    spawned.append(n)
    return n

def link(a, b):
    nodes[a].set_editor_property('links', list(nodes[a].get_editor_property('links')) + [nodes[b]])

for nid, (x, y) in LANES.items():
    node(nid, x, y)
for a, b in LINKS:
    link(a, b)

vehicle_offset = unreal.get_default_object(unreal.AgvTestVehicle).get_editor_property('drive').get_editor_property('reference_offset_cm')
rows = {}
for a in eas.get_all_level_actors():
    m = re.fullmatch(r'WH_Pallet_(Left|Right)_(\d\d)_0(_B)?', a.get_actor_label())
    if not m:
        continue
    o, e = a.get_actor_bounds(False)
    left = m.group(1) == 'Left'
    face_x = o.x + e.x if left else o.x - e.x
    tag = '%d%s' % (int(m.group(2)), 'B' if m.group(3) else 'A')
    x = face_x + (FORK_TIP + STANDOFF - vehicle_offset) * (1 if left else -1)
    node('P%s%s' % ('L' if left else 'R', tag), x, o.y, 180.0 if left else 0.0)
    rows.setdefault(round(o.y), tag)
# The central aisle: J - one node per slot row - D, each row node linked to its two staging nodes.
previous = 'J'
for y in sorted(rows):
    tag = rows[y]
    node('X' + tag, 0, y)
    link(previous, 'X' + tag)
    for side in ('PL', 'PR'):
        if side + tag in nodes:
            link('X' + tag, side + tag)
    previous = 'X' + tag
link(previous, 'D')

v = eas.spawn_actor_from_class(unreal.AgvTestVehicle, unreal.Vector(0, 0, 0))
v.set_actor_label(PREFIX + 'Vehicle')
v.set_folder_path('AGV')
v.teleport_reference(unreal.Vector2D(*LANES['B']), -90)  # at B, body (travel direction) toward C
spawned.append(v)

assert unreal.EditorLoadingAndSavingUtils.save_packages([a.get_outermost() for a in spawned], False), 'save failed'
unreal.log('AGV_WH_PLACED %d nodes + vehicle (%d staging), saved as %d actor files; the map itself was not saved'
           % (len(nodes), sum(1 for k in nodes if k.startswith('P')), len(spawned)))
