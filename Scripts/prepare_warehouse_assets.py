"""Rebuild ignored collision assets from the installed Fab pack; does not modify the map."""
import unreal

mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
collision_cache = {}
EXTERIOR_MESHES = {
    'truck': '/Game/VehicleVarietyPack/Meshes/SM_Truck_Box',
    'tree': '/Game/EuropeanBeech/Geometry/SimpleWind/SM_EuropeanBeech_Sapling_01',
    'shrub': '/Game/GV_FreeShrubsPack/Meshes/Shrubs/Wind/Shrub_I/GV_Vol7_Shrub_I_full_type1',
}
def collision_mesh(mesh, dynamic=False):
    assert mesh, 'Required mesh missing: install the documented Fab packs first'
    key = (mesh.get_path_name(), dynamic)
    if key not in collision_cache:
        folder = '/Game/Warehouse/' + ('Physics' if dynamic else 'Collision')
        path = folder + '/' + mesh.get_name()
        result = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.EditorAssetLibrary.duplicate_asset(mesh.get_path_name(), path)
        assert result, path
        if dynamic or mesh.get_path_name().split('.')[0] in (EXTERIOR_MESHES['tree'], EXTERIOR_MESHES['shrub']):
            # Carried translucency and small leaves both need conventional fallback geometry.
            settings = mesh_editor.get_nanite_settings(result)
            fraction = 1.0 if dynamic or mesh.get_name().startswith('SM_EuropeanBeech') else .25
            if settings.generate_fallback != unreal.NaniteGenerateFallback.ENABLED or settings.fallback_target != unreal.NaniteFallbackTarget.PERCENT_TRIANGLES or settings.fallback_percent_triangles != fraction:
                settings.generate_fallback = unreal.NaniteGenerateFallback.ENABLED
                settings.fallback_target = unreal.NaniteFallbackTarget.PERCENT_TRIANGLES
                settings.fallback_percent_triangles = fraction
                mesh_editor.set_nanite_settings(result, settings)
        expected = unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX if dynamic else unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
        if mesh_editor.get_collision_complexity(result) != expected:
            unreal.WarehouseCargo.configure_mesh_collision(result, not dynamic)
        if dynamic and mesh_editor.get_convex_collision_count(result) == 0:
            assert mesh_editor.set_convex_decomposition_collisions(result, 32, 32, 100000), path
        if dynamic and mesh.get_name() == 'SM_Ind_War_Storage_Pallet_Wood_Worn_01':
            unreal.WarehousePallet.fit_collision_bounds(result)
        unreal.EditorAssetLibrary.save_loaded_asset(result)
        collision_cache[key] = result
    return collision_cache[key]


if __name__ == '__main__':
    pack = '/Game/Scene_Warehouse/Assets/MS/3D/'
    names = ['Ind_War_Storage_Pallet_Wood_Worn_01',
             'Ind_War_Light_Ceiling_Metal_Hanging_01',
             'Ind_War_Storage_Barrel_Plastic_Blue_01',
             'Ind_Fac_Container_Drum_Metal_Worn_01',
             'Ind_War_Cabinet_Electric_Metal_Dirty_01',
             'Ind_War_Equipment_Ladder_Metal_Worn_01']
    for name in names:
        collision_mesh(unreal.load_asset(pack+name+'/SM_'+name), name == 'Ind_War_Storage_Pallet_Wood_Worn_01')
    rack='Ind_War_Pallet_Shelf_Metal_Modular_01'
    for part in 'ABCDEFGH':
        collision_mesh(unreal.load_asset(pack+rack+'/SM_'+rack+'_'+part))
    for name in ['Ind_War_Storage_Box_Cardboard_Worn_02','Ind_War_Storage_Crate_Plastic_Blue_01']:
        collision_mesh(unreal.load_asset(pack+name+'/SM_'+name), True)
    for path in EXTERIOR_MESHES.values():
        assert unreal.EditorAssetLibrary.does_asset_exist(path), 'Install Fab pack: ' + path
        collision_mesh(unreal.load_asset(path))
    print('WAREHOUSE_COLLISION_ASSETS_READY')
