"""Build the example-inspired warehouse and a guarded pallet training lane."""
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


import os
import sys
sys.path.insert(0, os.path.dirname(__file__))
from prepare_warehouse_assets import collision_mesh
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

def color_material(name, color):
    parent_path = '/Game/Warehouse/Materials/M_WarehouseSolid'
    parent = unreal.load_asset(parent_path) if unreal.EditorAssetLibrary.does_asset_exist(parent_path) else None
    if not parent:
        parent = asset_tools.create_asset('M_WarehouseSolid', '/Game/Warehouse/Materials', unreal.Material, unreal.MaterialFactoryNew())
        tint = unreal.MaterialEditingLibrary.create_material_expression(parent, unreal.MaterialExpressionVectorParameter)
        tint.set_editor_property('parameter_name', 'Color')
        tint.set_editor_property('default_value', unreal.LinearColor(1,1,1,1))
        unreal.MaterialEditingLibrary.connect_material_property(tint, '', unreal.MaterialProperty.MP_BASE_COLOR)
        roughness = unreal.MaterialEditingLibrary.create_material_expression(parent, unreal.MaterialExpressionConstant)
        roughness.set_editor_property('r', .7)
        unreal.MaterialEditingLibrary.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
        unreal.MaterialEditingLibrary.recompile_material(parent)
        unreal.EditorAssetLibrary.save_loaded_asset(parent)
    path = '/Game/Warehouse/Materials/' + name
    mat = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else asset_tools.create_asset(name, '/Game/Warehouse/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mat, parent)
    # UE 5.8's setter returns false even after applying the value; verify by reading it back.
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mat, 'Color', unreal.LinearColor(*color))
    actual = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(mat, 'Color')
    assert all(abs(a-b)<.0001 for a,b in zip((actual.r,actual.g,actual.b),color[:3]))
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat

yellow = color_material('MI_SafetyYellow', (1.0, .55, .025, 1))
steel = color_material('MI_DarkSteel', (.07, .09, .11, 1))
rubber = color_material('MI_Rubber', (.015, .018, .02, 1))
wood = color_material('MI_PalletWood', (.36, .19, .07, 1))
ground = 25.0
for actor in actors.get_all_level_actors():
    label = actor.get_actor_label()
    if label.startswith('EXT_'):
        continue
    if label.startswith('WH_'):
        actors.destroy_actor(actor)
    elif isinstance(actor, unreal.StaticMeshActor) and label not in ('Floor', 'SM_SkySphere'):
        actors.destroy_actor(actor)
    elif label == 'Floor':
        actor.modify()
        ground = actor.get_actor_bounds(False)[0].z + actor.get_actor_bounds(False)[1].z
        actor.static_mesh_component.set_editor_property('override_materials', [floor_mat])
        actor.static_mesh_component.set_collision_profile_name('BlockAll')
    elif label == 'PlayerStart':
        actor.set_actor_location(unreal.Vector(780, -1120, 125), False, False)
        actor.set_actor_rotation(unreal.Rotator(pitch=0, yaw=30, roll=0), False)

def place(label, mesh, xyz, scale=(1, 1, 1), yaw=0, material=None, visible=True, carryable=False):
    actor_class = cargo_class if carryable else unreal.StaticMeshActor
    actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*xyz), unreal.Rotator(pitch=0, yaw=yaw, roll=0))
    assert actor, 'Failed to spawn ' + label
    if carryable:
        actor.set_actor_scale3d(unreal.Vector(*scale))
        actor.set_cargo_mesh(collision_mesh(mesh, True))
        actor.set_actor_label('WH_' + label)
        return actor
    actor.set_actor_label('WH_' + label)
    component = actor.static_mesh_component
    component.set_static_mesh(mesh if mesh == cube else collision_mesh(mesh))
    component.set_collision_profile_name('BlockAll')
    component.set_visibility(visible)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    if material:
        component.set_editor_property('override_materials', [material])
    return actor

def sit_on(actor, height):
    center, extent = actor.get_actor_bounds(False)
    loc = actor.get_actor_location()
    actor.set_actor_location(unreal.Vector(loc.x, loc.y, loc.z + height - (center.z-extent.z)), False, False)
    return height + extent.z*2

# The original floor is 40 x 40 m. A high roof leaves room for five-metre racks.
for label, pos, scale in (
    ('Wall_N', (0, 1980, 340), (40, 0.35, 6.8)),
    ('Wall_S_Left', (-1350, -1980, 340), (13, 0.35, 6.8)),
    ('Wall_S_Right', (1350, -1980, 340), (13, 0.35, 6.8)),
    ('LoadingDoor_Header', (0, -1980, 585), (14, 0.35, 1.9)),
    ('Wall_E', (1980, 0, 340), (0.35, 40, 6.8)),
    ('Wall_W', (-1980, 0, 340), (0.35, 40, 6.8)),
    ('Roof', (0, 0, 695), (40, 40, 0.3)),
):
    place(label, cube, pos, scale, material=wall_mat)

# Each Fab rack module has eight textured parts: orange beams and blue frames.
# Triangle surface collision leaves all rack openings accessible.
for side_name, side in (('Left', -1), ('Right', 1)):
    for row, y in enumerate(range(-1050, 1051, 210)):
        x = side * 290
        label = f'Rack_{side_name}_{row:02d}'
        for part, mesh in rack_parts.items():
            place(f'{label}_{part}', mesh, (x, y, ground), (1.35, 1.5, 2), 90)
        for level_index, z in enumerate((82, 270, 479)):
            support = place(f'Pallet_{side_name}_{row:02d}_{level_index}', pallet, (x, y, z), (1.2,1.2,1))
            top = sit_on(support, z)
            cargo = crate if (row + level_index) % 4 == 0 else box
            load = place(f'Cargo_{side_name}_{row:02d}_{level_index}', cargo, (x, y, top), carryable=True)
            sit_on(load, top)

# Loose pallet loads near the ends of the aisle show the pickup/drop zones.
for label, x, y in (('Pickup', 550, 1550), ('Drop', -550, -1550)):
    support = place(f'{label}_Pallet', pallet, (x, y, ground), (1.2,1.2,1))
    top = sit_on(support, ground)
    sit_on(place(f'{label}_Box', box, (x, y, top), carryable=True), top)

for index, y in enumerate(range(-1500, 1501, 600)):
    place(f'LightFixture_{index}', lamp, (0, y, 610))
    light = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(0, y, 570))
    light.set_actor_label(f'WH_Light_{index}')
    light.point_light_component.set_editor_property('intensity_units', unreal.LightUnits.LUMENS)
    light.point_light_component.set_editor_property('intensity', 12000.0)
    light.point_light_component.set_editor_property('attenuation_radius', 1600.0)
    light.point_light_component.set_editor_property('source_radius', 25.0)

for x in (-1100,1100):
    for y in (-1300,0,1300):
        place(f'SideFixture_{x}_{y}',lamp,(x,y,610))
        light=actors.spawn_actor_from_class(unreal.PointLight,unreal.Vector(x,y,570))
        light.set_actor_label(f'WH_WorkLight_{x}_{y}')
        light.point_light_component.set_editor_property('intensity_units',unreal.LightUnits.LUMENS)
        light.point_light_component.set_editor_property('intensity',18000.0)
        light.point_light_component.set_editor_property('attenuation_radius',2000.0)
        light.point_light_component.set_editor_property('source_radius',40.0)

forklift_class = unreal.load_class(None, '/Script/msc_vr.WarehouseForklift')
assert forklift_class, 'Build the msc_vrEditor C++ target first'
forklift = actors.spawn_actor_from_class(forklift_class, unreal.Vector(1000, -1000, ground), unreal.Rotator(pitch=0, yaw=90, roll=0))
forklift.set_actor_label('WH_AutonomousForklift')
forklift.set_editor_property('is_spatially_loaded', False)


# A dimensioned pallet with two real openings. Its boards are visible collision bodies.
target = actors.spawn_actor_from_class(unreal.WarehousePallet, unreal.Vector(1000, -500, ground), unreal.Rotator(pitch=0, yaw=90, roll=0))
target.set_actor_label('WH_TrainingPallet')
target.set_editor_property('is_spatially_loaded', False)
forklift.set_editor_property('target_pallet', target)
for comp in target.get_components_by_class(unreal.StaticMeshComponent):
    comp.set_editor_property('override_materials', [wood])
for comp in forklift.get_components_by_class(unreal.StaticMeshComponent):
    name = comp.get_name()
    comp.set_editor_property('override_materials', [rubber if name.startswith('Wheel') else (yellow if name in ('Chassis', 'Battery') else steel)])

def sign(label, text, xyz, yaw=90, size=32):
    actor = actors.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(*xyz), unreal.Rotator(pitch=0, yaw=yaw, roll=0))
    actor.set_actor_label('WH_Sign_' + label)
    comp = actor.get_component_by_class(unreal.TextRenderComponent)
    comp.set_text(text)
    comp.set_world_size(size)
    comp.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    return actor

# Example map vocabulary: dense stock, steel roof frame, utility corner and dispatch lanes.
for x in (-1850, 1850):
    for y in (-1500, -500, 500, 1500):
        place(f'Column_{x}_{y}', cube, (x,y,335), (.2,.2,6.7), material=steel)
for y in (-1500,-500,500,1500):
    place(f'RoofBeam_{y}',cube,(0,y,655),(38,.2,.25),material=steel)
for x in (650,1350):
    place(f'TrainingLine_{x}',cube,(x,-450,ground+.2),(.06,23,.004),material=yellow)
for y in (-1600,700):
    place(f'TrainingEnd_{y}',cube,(1000,y,ground+.2),(7,.06,.004),material=yellow)
# Pedestrian route along the west wall.
for x in (-1800,-1250):
    place(f'Walkway_{x}',cube,(x,0,ground+.2),(.06,35,.004),material=yellow)
for i in range(12):
    place(f'Crosswalk_{i}',cube,(-1150+i*150,-1740,ground+.3),(.55,2.4,.006),material=yellow)
for index, (x,y) in enumerate([(-900,-1200),(-900,-500),(-900,200),(-900,900),(1000,1150),(1400,1400)]):
    top=sit_on(place(f'DispatchPallet_{index}',pallet,(x,y,ground),(1.2,1.2,1)),ground)
    for row in range(2):
        for col in range(2):
            for tier in range(3):
                load=place(f'Stock_{index}_{row}_{col}_{tier}',box,(x+(row-.5)*43,y+(col-.5)*34,top),(.32,.32,.32),carryable=True)
                height=load.get_actor_bounds(False)[1].z*2
                sit_on(load,top+tier*height)
for index, name in enumerate(('Ind_War_Storage_Barrel_Plastic_Blue_01','Ind_Fac_Container_Drum_Metal_Worn_01','Ind_War_Cabinet_Electric_Metal_Dirty_01','Ind_War_Equipment_Ladder_Metal_Worn_01')):
    mesh=unreal.load_asset(PACK+name+'/SM_'+name)
    assert mesh, name
    prop=place(f'Utility_{index}',mesh,(-1700+index*300,1770,ground))
    center, extent=prop.get_actor_bounds(False)
    loc=prop.get_actor_location()
    prop.set_actor_location(unreal.Vector(loc.x,loc.y,loc.z+ground-(center.z-extent.z)),False,False)
sign('Training', 'AUTONOMOUS PALLET TRAINING', (1000,750,340), -90, 28)
sign('Controls', 'E : START / PAUSE   |   KEEP CLEAR', (1000,-1500,200), -90, 17)
sign('Dispatch','DISPATCH / STAGING',(-800,-1920,360),90,30)
for side,x in [('A',-290),('B',290)]:
    for i,y in enumerate((-1050,-420,210,840)):
        sign(f'Rack{side}{i}',f'{side}-{i+1:02}',(x,y-100,530),-90,24)
assert level.save_current_level(), 'Could not save the default first-person level'
print('WAREHOUSE_BUILT', LEVEL, 'actors', len(actors.get_all_level_actors()), 'ground',ground)
