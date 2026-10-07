"""Normalize intact cartons and the existing pallet for stable dynamic physics, without changing the level."""
import unreal
assets=unreal.EditorAssetLibrary
editor=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
base='/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/'
for i in (1,2):
    if not assets.does_asset_exist(base+f'Cargo_Box_V{i}_001'):continue
    path=base+'SM_Carton_Intact_'+str(i)
    if not assets.does_asset_exist(path):
        mesh=assets.duplicate_asset(base+f'Cargo_Box_V{i}_001',path)
        description=mesh.get_static_mesh_description(0)
        for n in range(description.get_vertex_count()):
            v=unreal.VertexID(n);description.set_vertex_position(v,description.get_vertex_position(v)*100)
        unreal.WarehouseForklift.rebuild_edited_mesh(mesh)
        editor.remove_collisions(mesh)
        editor.add_simple_collisions(mesh,unreal.ScriptingCollisionShapeType.BOX)
        unreal.WarehouseCargo.configure_mesh_collision(mesh,False)
        assert assets.save_loaded_asset(mesh)
    mesh=unreal.load_asset(path)
    if assets.get_metadata_tag(mesh,'CarryPhysicsVersion')!='centred-cm-v2':
        center=mesh.get_bounds().origin
        description=mesh.get_static_mesh_description(0)
        for n in range(description.get_vertex_count()):
            v=unreal.VertexID(n);description.set_vertex_position(v,description.get_vertex_position(v)-center)
        unreal.WarehouseForklift.rebuild_edited_mesh(mesh)
        editor.remove_collisions(mesh);editor.add_simple_collisions(mesh,unreal.ScriptingCollisionShapeType.BOX)
        unreal.WarehouseCargo.configure_mesh_collision(mesh,False)
        assets.set_metadata_tag(mesh,'CarryPhysicsVersion','centred-cm-v2')
        assert assets.save_loaded_asset(mesh)
path='/Game/Warehouse/Physics/SM_Pallet_110'
if not assets.does_asset_exist(path):
    source='/Game/Warehouse/Physics/SM_Ind_War_Storage_Pallet_Wood_Worn_01'
    mesh=assets.duplicate_asset(source,path);assert mesh,'Run prepare_warehouse_assets.py first'
    bounds=mesh.get_bounds();size=bounds.box_extent*2
    scale=unreal.Vector(110/size.x,110/size.y,15/size.z)
    offset=unreal.Vector(-bounds.origin.x*scale.x,-bounds.origin.y*scale.y,-(bounds.origin.z-bounds.box_extent.z)*scale.z)
    transform=unreal.Transform(location=offset,scale=scale)
    description=mesh.get_static_mesh_description(0)
    for n in range(description.get_vertex_count()):
        v=unreal.VertexID(n);description.set_vertex_position(v,transform.transform_location(description.get_vertex_position(v)))
    unreal.WarehouseForklift.rebuild_edited_mesh(mesh)
    unreal.WarehousePallet.transform_collision(mesh,transform)
    unreal.WarehouseCargo.configure_mesh_collision(mesh,False)
    assert assets.save_loaded_asset(mesh)
print('INTACT_CARGO_PHYSICS_READY')
