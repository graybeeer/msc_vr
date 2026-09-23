"""Build a tall pallet-rack aisle in the existing first-person starter level."""
import unreal

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
PACK = '/Game/Scene_Warehouse/Assets/MS/3D/'
RACK = PACK + 'Ind_War_Pallet_Shelf_Metal_Modular_01/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_'

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level(LEVEL), 'Could not load first-person level'

cube = unreal.load_asset('/Engine/BasicShapes/Cube')
rack_parts = {part: unreal.load_asset(RACK + part) for part in 'ABCDEFGH'}
pallet = unreal.load_asset(PACK + 'Ind_War_Storage_Pallet_Wood_Worn_01/SM_Ind_War_Storage_Pallet_Wood_Worn_01')
box = unreal.load_asset(PACK + 'Ind_War_Storage_Box_Cardboard_Worn_02/SM_Ind_War_Storage_Box_Cardboard_Worn_02')
crate = unreal.load_asset(PACK + 'Ind_War_Storage_Crate_Plastic_Blue_01/SM_Ind_War_Storage_Crate_Plastic_Blue_01')
lamp = unreal.load_asset(PACK + 'Ind_War_Light_Ceiling_Metal_Hanging_01/SM_Ind_War_Light_Ceiling_Metal_Hanging_01')
floor_mat = unreal.load_asset('/Game/Scene_Warehouse/Assets/MS/Surfaces/Ind_War_Floor_Concrete_Smooth_01/MI_Ind_War_Floor_Concrete_Smooth_01_A')
wall_mat = unreal.load_asset('/Game/Scene_Warehouse/Assets/MS/Surfaces/Ind_War_Wall_Facade_Concrete_New_01/MI_Ind_War_Wall_Facade_Concrete_New_01_A')
cargo_class = unreal.load_class(None, '/Script/msc_vr.WarehouseCargo')
assert all([cube, pallet, box, crate, lamp, floor_mat, wall_mat, cargo_class, *rack_parts.values()]), 'Warehouse pack or cargo class is incomplete'

for actor in actors.get_all_level_actors():
    label = actor.get_actor_label()
    if label.startswith('WH_'):
        actors.destroy_actor(actor)
    elif isinstance(actor, unreal.StaticMeshActor) and label not in ('Floor', 'SM_SkySphere'):
        actors.destroy_actor(actor)
    elif label == 'Floor':
        actor.static_mesh_component.set_material(0, floor_mat)
        actor.static_mesh_component.set_collision_profile_name('BlockAll')
    elif label == 'PlayerStart':
        actor.set_actor_location(unreal.Vector(0, -1830, 125), False, False)
        actor.set_actor_rotation(unreal.Rotator(0, 90, 0), False)

def place(label, mesh, xyz, scale=(1, 1, 1), yaw=0, material=None, visible=True, carryable=False):
    actor_class = cargo_class if carryable else unreal.StaticMeshActor
    actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*xyz), unreal.Rotator(0, yaw, 0))
    assert actor, 'Failed to spawn ' + label
    if carryable:
        actor.set_actor_scale3d(unreal.Vector(*scale))
        actor.set_cargo_mesh(mesh)
        actor.set_actor_label('WH_' + label)
        return actor
    actor.set_actor_label('WH_' + label)
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_collision_profile_name('BlockAll')
    component.set_visibility(visible)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    if material:
        component.set_material(0, material)
    return actor

# The original floor is 40 x 40 m. A high roof leaves room for five-metre racks.
for label, pos, scale in (
    ('Wall_N', (0, 1980, 350), (40, 0.35, 6.6)),
    ('Wall_S_Left', (-1350, -1980, 350), (13, 0.35, 6.6)),
    ('Wall_S_Right', (1350, -1980, 350), (13, 0.35, 6.6)),
    ('LoadingDoor_Header', (0, -1980, 585), (14, 0.35, 1.9)),
    ('Wall_E', (1980, 0, 350), (0.35, 40, 6.6)),
    ('Wall_W', (-1980, 0, 350), (0.35, 40, 6.6)),
    ('Roof', (0, 0, 695), (40, 40, 0.3)),
):
    place(label, cube, pos, scale, material=wall_mat)

# Each Fab rack module has eight textured parts: orange beams and blue frames.
# One hidden cube provides reliable simple collision for the whole loaded bay.
for side_name, side in (('Left', -1), ('Right', 1)):
    for row, y in enumerate(range(-1050, 1051, 210)):
        x = side * 290
        label = f'Rack_{side_name}_{row:02d}'
        for part, mesh in rack_parts.items():
            place(f'{label}_{part}', mesh, (x, y, 25), (1.35, 1.5, 2), 90)
        place(f'Collider_{label}', cube, (x, y, 285), (1.6, 2.05, 5.2), visible=False)
        for level_index, z in enumerate((90, 285, 490)):
            place(f'Pallet_{side_name}_{row:02d}_{level_index}', pallet, (x, y, z), yaw=90)
            cargo = crate if (row + level_index) % 4 == 0 else box
            place(f'Cargo_{side_name}_{row:02d}_{level_index}', cargo, (x, y, z + 24), (1.15, 1.15, 1.15), 90, carryable=True)

# Loose pallet loads near the ends of the aisle show the pickup/drop zones.
for label, x, y in (('Pickup', 550, 1550), ('Drop', -550, -1550)):
    place(f'{label}_Pallet', pallet, (x, y, 25))
    place(f'{label}_Box', box, (x, y, 50), (1.1, 1.1, 1.1), carryable=True)
    place(f'Collider_{label}', cube, (x, y, 75), (1.25, 0.95, 1), visible=False)

for index, y in enumerate(range(-1500, 1501, 600)):
    place(f'LightFixture_{index}', lamp, (0, y, 610))
    place(f'Collider_LightFixture_{index}', cube, (0, y, 610), (0.18, 0.55, 0.14), visible=False)
    light = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(0, y, 570))
    light.set_actor_label(f'WH_Light_{index}')
    light.point_light_component.set_editor_property('intensity', 8000.0)
    light.point_light_component.set_editor_property('attenuation_radius', 900.0)

forklift_class = unreal.load_class(None, '/Script/msc_vr.WarehouseForklift')
assert forklift_class, 'Build the msc_vrEditor C++ target first'
forklift = actors.spawn_actor_from_class(forklift_class, unreal.Vector(0, -1500, 95), unreal.Rotator(0, 90, 0))
forklift.set_actor_label('WH_AutonomousForklift')
forklift.set_editor_property('is_spatially_loaded', False)
forklift.get_component_by_class(unreal.BoxComponent).set_box_extent(unreal.Vector(230, 68, 70), True)

assert level.save_current_level(), 'Could not save the default first-person level'
print('WAREHOUSE_BUILT', LEVEL, 'actors', len(actors.get_all_level_actors()))
