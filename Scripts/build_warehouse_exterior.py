"""Add a compact truck yard to the existing warehouse; rebuild EXT_ actors only.

Coordinates are centimetres. Warehouse floor=0; yard=-120. Two docks are
include one static Fab truck. No vehicle behaviour is created by this script.
"""
import math
import os
import runpy
import sys
import unreal
sys.path.insert(0,os.path.dirname(__file__))
from prepare_warehouse_assets import EXTERIOR_MESHES

LEVEL='/Game/FirstPerson/Lvl_FirstPerson'
PREFIX='EXT_'
ROAD_Z=-120.0
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level(LEVEL)
existing=actors.get_all_level_actors()
assert any(a.get_actor_label()=='WH_LoadingDoor_Header' for a in existing), 'Build the warehouse first'
for path in EXTERIOR_MESHES.values():
    assert unreal.EditorAssetLibrary.does_asset_exist(path), 'Install Fab pack: '+path
for actor in existing:
    if actor.get_actor_label().startswith(PREFIX):
        actors.destroy_actor(actor)

cube=unreal.load_asset('/Engine/BasicShapes/Cube')
cylinder=unreal.load_asset('/Engine/BasicShapes/Cylinder')
parent=unreal.load_asset('/Game/Warehouse/Materials/M_WarehouseSolid')
assert parent
asset_tools=unreal.AssetToolsHelpers.get_asset_tools()
folder='/Game/Warehouse/Exterior/Materials'

def material(name,color):
    path=folder+'/MI_'+name
    mat=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else asset_tools.create_asset('MI_'+name,folder,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mat,parent)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mat,'Color',unreal.LinearColor(*color,1))
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat

white=material('Cladding',(.72,.77,.78))
blue=material('BlueBand',(.025,.16,.26))
glass=material('Window',(.018,.075,.105))
steel=material('Steel',(.13,.18,.20))
yellow=unreal.load_asset('/Game/Warehouse/Materials/MI_SafetyYellow')
rubber=unreal.load_asset('/Game/Warehouse/Materials/MI_Rubber')
concrete=unreal.load_asset('/Game/Scene_Warehouse/Assets/MS/Surfaces/Ind_War_Floor_Concrete_Smooth_01/MI_Ind_War_Floor_Concrete_Smooth_01_A')
grass=material('Grass',(.105,.17,.07))
red=material('Red',(.5,.025,.012))

# World-space grain avoids stretching a texture over the large apron.
asphalt_path=folder+'/M_Asphalt'
asphalt=unreal.load_asset(asphalt_path) if unreal.EditorAssetLibrary.does_asset_exist(asphalt_path) else None
if not asphalt:
    asphalt=asset_tools.create_asset('M_Asphalt',folder,unreal.Material,unreal.MaterialFactoryNew())
    noise=unreal.MaterialEditingLibrary.create_material_expression(asphalt,unreal.MaterialExpressionNoise)
    noise.set_editor_property('scale',.25)
    noise.set_editor_property('levels',1)
    noise.set_editor_property('quality',1)
    noise.set_editor_property('output_min',0.0)
    noise.set_editor_property('output_max',1.0)
    mix=unreal.MaterialEditingLibrary.create_material_expression(asphalt,unreal.MaterialExpressionLinearInterpolate)
    for name,color in [('A',(.025,.03,.034)),('B',(.045,.05,.055))]:
        expr=unreal.MaterialEditingLibrary.create_material_expression(asphalt,unreal.MaterialExpressionConstant3Vector)
        expr.set_editor_property('constant',unreal.LinearColor(*color,1))
        unreal.MaterialEditingLibrary.connect_material_expressions(expr,'',mix,name)
    unreal.MaterialEditingLibrary.connect_material_expressions(noise,'',mix,'Alpha')
    unreal.MaterialEditingLibrary.connect_material_property(mix,'',unreal.MaterialProperty.MP_BASE_COLOR)
    rough=unreal.MaterialEditingLibrary.create_material_expression(asphalt,unreal.MaterialExpressionConstant)
    rough.set_editor_property('r',.95)
    unreal.MaterialEditingLibrary.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(asphalt)
    unreal.EditorAssetLibrary.save_loaded_asset(asphalt)

def block(name,position,size,mat,rotation=None,mesh=None,collision=True):
    actor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*position),rotation or unreal.Rotator())
    actor.set_actor_label(PREFIX+name)
    actor.set_folder_path('Warehouse/Exterior')
    comp=actor.static_mesh_component
    comp.set_static_mesh(mesh or cube)
    comp.set_editor_property('override_materials',[mat])
    comp.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    actor.set_actor_scale3d(unreal.Vector(*(v/100 for v in size)))
    actor.set_editor_property('is_spatially_loaded',False)
    return actor

def sign(name,text,position,yaw=-90,size=40,color=unreal.Color(235,241,241,255)):
    actor=actors.spawn_actor_from_class(unreal.TextRenderActor,unreal.Vector(*position),unreal.Rotator(yaw=yaw))
    actor.set_actor_label(PREFIX+name)
    actor.set_folder_path('Warehouse/Exterior/Signs')
    comp=actor.get_component_by_class(unreal.TextRenderComponent)
    comp.set_text(text)
    comp.set_world_size(size)
    comp.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    comp.set_text_render_color(color)
    return actor

def line(name,x,y,sx,sy,mat=white):
    # Paint is visual only: no raised collision edges across the truck path.
    return block(name,(x,y,ROAD_Z+.15),(sx,sy,.2),mat,collision=False)

# 60 x 45 m working apron, short access road, and narrow side setbacks.
block('Plot',(0,-2700,-165),(7800,10800,70),grass)
block('Yard',(0,-4250,ROAD_Z-15),(6000,4500,30),asphalt)
block('AccessRoad',(0,-7100,ROAD_Z-15),(7400,1200,30),asphalt)
for side in (-1,1):
    block(f'SidePath_{side}',(side*2500,0,ROAD_Z-15),(1000,4000,30),concrete)
    block(f'FoundationSide_{side}',(side*1980,0,-65),(50,4000,130),blue)
block('FoundationRear',(0,1980,-65),(4000,50,130),blue)
for x in (-1350,1350):
    block(f'FoundationFront_{x}',(x,-1980,-65),(1300,50,130),blue)

# Existing 14 m opening remains open. Platform is continuous with its floor.
block('DockPlatform',(0,-2240,-65),(1400,520,130),concrete)
for index,x in enumerate((-350,350),1):
    line(f'Bay{index}_Left',x-190,-3230,8,1400)
    line(f'Bay{index}_Right',x+190,-3230,8,1400)
    line(f'Bay{index}_End',x,-3930,380,8)
    for y in range(-3800,-2600,200):
        line(f'Bay{index}_Center_{y}',x,y,5,85,yellow)
    for side in (-1,1):
        block(f'Dock{index}_Bumper_{side}',(x+side*140,-2508,-43),(25,20,82),rubber)
    sign(f'Dock{index}_Sign',f'DOCK 0{index}',(x,-2596,385),size=50)
    target=actors.spawn_actor_from_class(unreal.TargetPoint,unreal.Vector(x,-3120,ROAD_Z),unreal.Rotator(yaw=-90))
    target.set_actor_label(f'EXT_TruckDockTarget_0{index}')
    target.set_folder_path('Warehouse/Exterior/TruckTargets')
block('Canopy',(0,-2220,445),(1500,720,28),blue)
block('CanopyFascia',(0,-2580,410),(1500,25,80),blue)
for x in (-665,665):
    block(f'CanopyColumn_{x}',(x,-2410,217),(16,16,434),steel)
    block(f'DockGuard_{x}',(x,-2540,ROAD_Z+50),(22,22,100),yellow,mesh=cylinder)

# 1.2 m level difference; a 16 m long pedestrian ramp connects to the dock.
slope=math.degrees(math.atan2(120,1600))
ramp_length=math.hypot(1600,120)
block('PedestrianRamp',(-900,-3100,-60-10*math.cos(math.radians(slope))),(ramp_length,260,20),concrete,unreal.Rotator(pitch=slope,yaw=90))
block('RampLanding',(-890,-2310,-65),(380,180,130),concrete)
for x in (-1020,-780):
    block(f'RampRail_{x}',(x,-3100,35),(ramp_length,6,6),blue,unreal.Rotator(pitch=slope,yaw=90))
    for y in (-3860,-3350,-2850,-2350):
        bottom=-120+(y+3900)*120/1600
        block(f'RampPost_{x}_{y}',(x,y,bottom+47),(6,6,94),blue)

# Reference-inspired horizontal pale panels, blue plinth and ribbon glazing.
for x in (-1350,1350):
    block(f'FrontCladding_{x}',(x,-2006,340),(1300,16,680),white)
    block(f'FrontBlueBand_{x}',(x,-2017,92),(1300,8,110),blue)
    block(f'FrontWindow_{x}',(x,-2019,493),(1090,6,76),glass)
    for z in (240,365,615):
        block(f'FrontJoint_{x}_{z}',(x,-2018,z),(1300,3,2),steel)
    for offset in range(-540,541,180):
        block(f'FrontMullion_{x}_{offset}',(x+offset,-2024,493),(7,7,84),white)
for side in (-1,1):
    block(f'SideCladding_{side}',(side*2006,0,340),(16,4000,680),white)
    block(f'SideBlueBand_{side}',(side*2018,0,92),(8,4000,110),blue)
    block(f'SideWindow_{side}',(side*2019,0,493),(6,3700,76),glass)
    for y in range(-1800,1801,200):
        block(f'SideMullion_{side}_{y}',(side*2024,y,493),(7,7,84),white)
    block(f'SideParapet_{side}',(side*1995,0,723),(35,4040,55),white)
block('FrontParapet',(0,-2005,723),(4040,35,55),white)
block('HeaderCladding',(0,-2006,586),(1400,16,185),white)
sign('BuildingName','LOGISTICS 01',(0,-2020,565),size=68,color=unreal.Color(12,60,88,255))
for x in (-1200,1200):
    block(f'RoofVent_{x}',(x,-200,780),(350,500,160),steel)
    block(f'RoofVentTop_{x}',(x,-200,869),(380,530,18),white)

# Rail fence and kerbs enclose the compact site without invisible blockers.
def fence(name,start,end):
    dx,dy=end[0]-start[0],end[1]-start[1]
    length=math.hypot(dx,dy)
    yaw=math.degrees(math.atan2(dy,dx))
    mid=((start[0]+end[0])/2,(start[1]+end[1])/2)
    block(name+'_Kerb',(*mid,ROAD_Z+12),(length,24,24),concrete,unreal.Rotator(yaw=yaw))
    for z in (ROAD_Z+75,ROAD_Z+145):
        block(name+f'_Rail_{z}',(*mid,z),(length,6,6),steel,unreal.Rotator(yaw=yaw))
    count=math.ceil(length/250)
    for i in range(count+1):
        block(name+f'_Post_{i}',(start[0]+dx*i/count,start[1]+dy*i/count,ROAD_Z+78),(7,7,156),steel)
fence('WestFence',(-3000,-6500),(-3000,2050))
fence('EastFence',(3000,-6500),(3000,2050))
fence('RearFence',(-3000,2050),(3000,2050))
fence('GateLeft',(-3000,-6500),(1400,-6500))
fence('GateRight',(2600,-6500),(3000,-6500))

block('Gatehouse',(900,-6160,ROAD_Z+145),(450,360,290),white)
block('GatehouseRoof',(900,-6160,ROAD_Z+303),(490,400,26),blue)
block('GatehouseWindow',(900,-6343,ROAD_Z+175),(330,8,105),glass)
block('GatehouseSideWindow',(1128,-6160,ROAD_Z+175),(8,250,105),glass)
sign('GatehouseSign','ENTRY',(900,-6360,ROAD_Z+257),size=36,color=unreal.Color(10,50,75,255))
for x in (1380,2620):
    block(f'GateBollard_{x}',(x,-6500,ROAD_Z+55),(25,25,110),yellow,mesh=cylinder)
block('BarrierCabinet',(1370,-6460,ROAD_Z+55),(45,45,110),blue)
block('BarrierOpen',(1370,-6460,ROAD_Z+365),(12,14,520),white)
for z in range(int(ROAD_Z)+150,int(ROAD_Z)+610,90):
    block(f'BarrierStripe_{z}',(1370,-6469,z),(13,3,34),red,collision=False)
line('GateStop',2000,-6700,1100,18)
for x in range(-3500,3501,350):
    line(f'RoadDash_{x}',x,-7100,180,10,yellow)
for y in (-6530,-7660):
    line(f'RoadEdge_{y}',0,y,7350,10)
# Clear entry and turning apron; ground arrows point north toward the docks.
for y in (-6000,-4700):
    line(f'ArrowStem_{y}',2000,y,15,180)
    for side in (-1,1):
        block(f'ArrowHead_{y}_{side}',(2000+side*36,y+74,ROAD_Z+.2),(100,14,.2),white,unreal.Rotator(yaw=-side*45),collision=False)
for x in range(-1250,1251,150):
    line(f'PedestrianCrossing_{x}',x,-4300,60,220)
line('PedestrianRoute',-1300,-5200,8,2300,yellow)
sign('SpeedSign','10\nSLOW',(1240,-6460,125),size=32,color=unreal.Color(250,240,190,255))
block('SpeedSignPost',(1240,-6450,-2),(7,7,236),steel)

# A few site lights and planting strips finish the edges without a large landscape.
for index,(x,y) in enumerate(((-2800,-5800),(2800,-5800),(-2800,-3000),(2800,-3000))):
    block(f'LightPost_{index}',(x,y,ROAD_Z+270),(15,15,540),steel,mesh=cylinder)
    block(f'LightHead_{index}',(x,y,ROAD_Z+545),(100,55,18),white)
    light=actors.spawn_actor_from_class(unreal.PointLight,unreal.Vector(x,y,ROAD_Z+515))
    light.set_actor_label(f'EXT_YardLight_{index}')
    light.point_light_component.set_editor_property('intensity_units',unreal.LightUnits.LUMENS)
    light.point_light_component.set_editor_property('intensity',4000.0)
    light.point_light_component.set_editor_property('attenuation_radius',1500.0)
for index,(x,y) in enumerate(((-3400,-5600),(-3400,-3400),(-3400,-1200),(3400,-4600),(3400,-2200))):
    block(f'PlantBed_{index}',(x,y,-125),(520,500,10),grass)

entry=actors.spawn_actor_from_class(unreal.TargetPoint,unreal.Vector(2000,-7000,ROAD_Z),unreal.Rotator(yaw=90))
entry.set_actor_label('EXT_TruckEntryTarget')
entry.set_folder_path('Warehouse/Exterior/TruckTargets')
# Illuminate the south loading facade with the existing environment lights.
for actor in actors.get_all_level_actors():
    if isinstance(actor,unreal.DirectionalLight):
        actor.modify()
        actor.set_actor_rotation(unreal.Rotator(pitch=-40,yaw=125),False)
    elif isinstance(actor,unreal.SkyLight):
        actor.modify()
        sky=actor.get_component_by_class(unreal.SkyLightComponent)
        sky.set_mobility(unreal.ComponentMobility.MOVABLE)
        sky.set_intensity(2.0)
        sky.recapture_sky()
from configure_observer_view import configure_observer_view
configure_observer_view()
assert level.save_current_level()
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
print('WAREHOUSE_EXTERIOR_BUILT',len([a for a in actors.get_all_level_actors() if a.get_actor_label().startswith(PREFIX)]),'actors')
runpy.run_path(os.path.join(os.path.dirname(__file__),'apply_exterior_fab_assets.py'),run_name='__main__')
