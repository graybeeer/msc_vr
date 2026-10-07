"""Rebuild a parked Fab truck's hollow cargo body; preserve the original asset and other map actors."""
import math,unreal
from pathlib import Path
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets=unreal.EditorAssetLibrary
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
truck=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='EXT_ParkedTruck_02')
for a in actors.get_all_level_actors():
    if a.get_actor_label().startswith('TRK_'):actors.destroy_actor(a)
path='/Game/Warehouse/Exterior/TruckInterior/SM_Truck_Open'
if assets.does_asset_exist(path):
    mesh=unreal.load_asset(path)
else:
    mesh=assets.duplicate_asset('/Game/VehicleVarietyPack/Meshes/SM_Truck_Box',path)
    d=mesh.get_static_mesh_description(0)
    # Cargo-box material also contains bumper details. Preserve everything below
    # the chassis top; replace the upper closed box with an accessible shell.
    polygons=list(d.get_polygon_group_polygons(unreal.PolygonGroupID(4)))
    removed=0
    for p in polygons:
        vs=[d.get_vertex_position(v) for v in d.get_polygon_vertices(p)]
        if max(v.z for v in vs)>110:
            d.delete_polygon(p);removed+=1
    assert removed>100,removed
    unreal.WarehouseForklift.rebuild_edited_mesh(mesh)
    unreal.WarehouseCargo.configure_mesh_collision(mesh,True)
    assert assets.save_loaded_asset(mesh)
    print('TRUCK_BOX_REMOVED',removed)
truck.modify();truck.static_mesh_component.set_static_mesh(mesh)
truck.static_mesh_component.set_collision_profile_name('BlockAll')
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
white=unreal.load_asset('/Game/Warehouse/Exterior/Materials/MI_Cladding')
steel=unreal.load_asset('/Game/Warehouse/AGV/Materials/M_Machined_steel')
floor_mat=unreal.load_asset('/Game/Warehouse/AGV/Materials/M_Graphite_powder-coated_steel')
assert all((white,steel,floor_mat))
pose=truck.get_actor_transform()
def block(name,pos,size,mat):
    a=actors.spawn_actor_from_class(unreal.StaticMeshActor,pose.transform_location(unreal.Vector(*pos)),truck.get_actor_rotation())
    a.set_actor_label('TRK_'+name);a.set_folder_path('Warehouse/Exterior/TruckInterior')
    a.set_editor_property('is_spatially_loaded',False)
    a.static_mesh_component.set_static_mesh(cube)
    a.static_mesh_component.set_material(0,mat)
    a.static_mesh_component.set_collision_profile_name('BlockAll')
    a.set_actor_scale3d(unreal.Vector(*(v/100 for v in size)))
    return a
# Measurements follow the installed Fab truck. cm, vehicle forward=+X.
# Exterior body: 493cm long, 232.46cm wide, floor top Z=100, ceiling bottom Z=316.
block('Floor',(-101.5,1.29,98),(493,232.46,4),floor_mat)
block('Wall_Left',(-101.5,-113.44,208),(493,3,216),white)
block('Wall_Right',(-101.5,116.02,208),(493,3,216),white)
block('FrontWall',(143.5,1.29,208),(3,226.46,216),white)
block('Roof',(-101.5,1.29,317.5),(493,232.46,3),white)
for i,x in enumerate((-180,60)):
    block(f'CeilingLamp_{i}',(x,1.29,313),(8,145,3),white)
    light=actors.spawn_actor_from_class(unreal.RectLight,pose.transform_location(unreal.Vector(x,1.29,310)),unreal.Rotator(pitch=-90))
    light.set_actor_label(f'TRK_WorkLight_{i}');light.set_folder_path('Warehouse/Exterior/TruckInterior')
    light.set_editor_property('is_spatially_loaded',False)
    c=light.get_component_by_class(unreal.RectLightComponent)
    c.set_editor_property('intensity',600);c.set_editor_property('attenuation_radius',550)
    c.set_editor_property('source_width',140);c.set_editor_property('source_height',6)
    c.set_editor_property('use_temperature',True);c.set_editor_property('temperature',4500)
for y in (-112.5,115.08):
    block('Rail_'+str(y),(-101.5,y,128),(487,3,7),steel)
# Rear doors open 180 degrees against the outside sides, leaving full entry width.
for side,y in (('Left',-118),('Right',120.58)):
    block('OpenDoor_'+side,(-291,y,208),(114,4,216),white)
    block('DoorLatch_'+side,(-291,y+(-3 if side=='Left' else 3),208),(3,3,180),steel)
    for x in (-348,-235):block('DoorTrim_'+side+str(x),(x,y,208),(2,5,216),steel)
block('RearThreshold',(-347,1.29,99),(4,226,2),steel)
# Dock leveller spans the gap and slopes to the measured floor, not a floating truck.
rear=pose.transform_location(unreal.Vector(-348,1.29,100))
end=unreal.Vector(rear.x,-2490,2)
inside=unreal.Vector(rear.x,rear.y-100,rear.z+2)
delta=end-inside;center=(end+inside)*.5
bridge=block('DockLeveller',(0,0,0),(delta.length(),200,4),steel)
bridge.set_actor_location_and_rotation(center,unreal.Rotator(pitch=math.degrees(math.atan2(delta.z,math.hypot(delta.x,delta.y))),yaw=90),False,True)
# 16 stacks, 3 layers each. Rear 150cm and a centre aisle stay open.
manifest=[]
for row,x in enumerate((-150,-80,-10,60)):
    for col,y in enumerate((-76,76)):
        for lane,dx in enumerate((-17,17)):
            limits=unreal.Vector(30,58,38);bottom=100.3
            for layer in range(3):
                i=((row*2+col)*2+lane)*3+layer
                recipe=unreal.WarehouseCargo.generate_cargo_recipe(20261006,i,-1,limits,1)
                assert recipe.valid
                a=actors.spawn_actor_from_class(unreal.WarehouseCargo,pose.transform_location(unreal.Vector(x+dx,y,bottom)),truck.get_actor_rotation())
                a.set_actor_label(f'TRK_Cargo_{i:03}');a.set_folder_path('Warehouse/Exterior/TruckCargo');a.set_editor_property('is_spatially_loaded',False)
                a.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_'+str(i%2+1)))
                assert a.apply_cargo_recipe(recipe)
                c=a.get_component_by_class(unreal.StaticMeshComponent);c.set_editor_property('override_materials',[])
                c.set_simulate_physics(False)
                a.set_actor_location(unreal.Vector(),False,True)
                b=a.get_actor_bounds(False);target=pose.transform_location(unreal.Vector(x+dx,y,bottom+recipe.size_cm.z/2))
                a.set_actor_location(target-b[0],False,True)
                a.set_editor_property('cargo_id',unreal.Name(f'Truck02_{i:03}'))
                bottom+=recipe.size_cm.z+.15
                limits=unreal.Vector(recipe.size_cm.x,recipe.size_cm.y,38)
                manifest.append(a)
assert len(manifest)==48
# Append cargo profiles without changing pre-existing warehouse damage settings.
system=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WH_DamageSystem')
specs=[s for s in system.get_editor_property('objects') if not str(s.name).startswith('TRK_')]
for a in manifest:
    s=unreal.WarehouseStrength();s.set_editor_property('name',a.get_actor_label());s.set_editor_property('members',[a]);s.set_editor_property('failure',unreal.WarehouseFailure.CRUSH)
    s.set_editor_property('physics_meshes',[a.get_component_by_class(unreal.StaticMeshComponent).static_mesh])
    s.set_editor_property('mass_kg',a.get_editor_property('gross_mass_kg'));s.set_editor_property('rated_load_kg',100)
    s.set_editor_property('impact_yield_j',150);s.set_editor_property('impact_failure_j',650)
    specs.append(s)
system.modify();system.set_editor_property('objects',specs)
assert level.save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
print('TRUCK_INTERIOR_APPLIED',len(manifest),'cargo, hollow shell, open doors, dock leveller')
