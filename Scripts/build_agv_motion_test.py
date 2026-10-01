"""Create the standalone AGV navigation test level. Does not touch Lvl_FirstPerson or any warehouse asset."""
import unreal

LEVEL = '/Game/AgvTest/Lvl_AgvMotionTest'
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# NodeId: (x, y, corner radius cm [-1 = navigator default, 0 = pivot], align yaw or None)
NODES = {
    'P0': (0, 0, -1, None),
    'P1': (800, 0, -1, None),
    'P2': (1600, 0, -1, None),
    'P3': (800, 600, -1, None),
    'P4': (1600, 600, 0, None),
    'P5': (0, 700, -1, None),
    'S1': (1600, 1000, -1, 0),
    'ISO': (-500, -500, -1, None),  # deliberately unlinked: route search must reject it
}
LINKS = [('P0', 'P1'), ('P1', 'P2'), ('P1', 'P3'), ('P3', 'P4'), ('P5', 'P3'), ('P5', 'P0'), ('P4', 'S1')]


def spawn(cls, label, location=(0, 0, 0), yaw=0, pitch=0):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*location), unreal.Rotator(roll=0, pitch=pitch, yaw=yaw))
    assert actor, label
    actor.set_actor_label('AGV_' + label)
    return actor


if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
    assert levels.load_level(LEVEL), LEVEL
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label().startswith('AGV_'):
            actors.destroy_actor(actor)
else:
    assert levels.new_level(LEVEL), LEVEL

floor = spawn(unreal.StaticMeshActor, 'Floor', (800, 300, -5))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(40, 30, 0.1))
spawn(unreal.DirectionalLight, 'Sun', (0, 0, 800), yaw=30, pitch=-50)
spawn(unreal.SkyAtmosphere, 'Atmosphere')
spawn(unreal.SkyLight, 'SkyLight', (0, 0, 600)).light_component.set_editor_property('real_time_capture', True)
spawn(unreal.PlayerStart, 'PlayerStart', (-350, -350, 100), yaw=35)

# Static structure for LiDAR localization: outer walls, three pallet racks (uprights + beams) and two pillars,
# clear of every lane, pivot sweep and test start position.
cube = unreal.load_asset('/Engine/BasicShapes/Cube')


def block(label, centre, size):
    actor = spawn(unreal.StaticMeshActor, label, centre)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100, size[1] / 100, size[2] / 100))


for name, centre, size in (('WallW', (-1250, 300, 200), (20, 3120, 400)), ('WallE', (2850, 300, 200), (20, 3120, 400)),
                           ('WallS', (800, -1250, 200), (4100, 20, 400)), ('WallN', (800, 1850, 200), (4100, 20, 400)),
                           ('Pillar1', (400, 300, 200), (40, 40, 400)), ('Pillar2', (1200, 300, 200), (40, 40, 400))):
    block(name, centre, size)

# Racks: two lines of uprights 100 cm apart, every 270 cm, beams at 100 and 200 cm.
RACKS = {'RackS': ((-200, -550), (1, 0), 9), 'RackN': ((-200, 1350), (1, 0), 9), 'RackE': ((2250, -200), (0, 1), 6)}
for name, ((x0, y0), (dx, dy), bays) in RACKS.items():
    side = (dy * 100, dx * 100)  # depth direction
    for face in (0, 1):
        ox, oy = x0 + side[0] * face, y0 + side[1] * face
        for i in range(bays + 1):
            block('%s_Post%d_%d' % (name, face, i), (ox + dx * 270 * i, oy + dy * 270 * i, 125), (10, 10, 250))
        length = 270 * bays
        for level in (100, 200):
            block('%s_Beam%d_%d' % (name, face, level), (ox + dx * length / 2, oy + dy * length / 2, level),
                  (max(dx * length, 6), max(dy * length, 6), 10))

nodes = {}
for node_id, (x, y, radius, align) in NODES.items():
    node = spawn(unreal.AgvRouteNode, node_id, (x, y, 0), yaw=align or 0)
    node.set_editor_property('node_id', node_id)
    node.set_editor_property('corner_radius_cm', radius)
    node.set_editor_property('align_on_arrival', align is not None)
    nodes[node_id] = node
for a, b in LINKS:
    nodes[a].set_editor_property('links', list(nodes[a].get_editor_property('links')) + [nodes[b]])

# Reference point on P0, local -X (the travel direction) facing P1.
vehicle = spawn(unreal.AgvTestVehicle, 'TestVehicle')
vehicle.teleport_reference(unreal.Vector2D(0, 0), 180)

assert levels.save_current_level(), 'save failed'
print('AGV_MOTION_TEST_LEVEL_READY')
