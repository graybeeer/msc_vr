"""Import official CC0 models with Unreal PBR materials. Does not alter the level."""
import json,unreal
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='/Game/Warehouse/Cargo/PolyHaven'
tools=unreal.AssetToolsHelpers.get_asset_tools()
mesh_editor=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

def import_cargo_assets():
    result={}
    for asset in ('cardboard_box_01','plastic_crate_01'):
        folder=ROOT/'SourceAssets/Cargo/PolyHaven'/asset
        manifest=json.loads((folder/'source.json').read_text(encoding='utf-8-sig'))
        destination=BASE+'/'+asset
        textures={}
        for kind,entry in manifest['files'].items():
            task=unreal.AssetImportTask()
            for k,v in dict(filename=str(folder/entry['filename']),destination_path=destination,automated=True,replace_existing=True,save=True).items():task.set_editor_property(k,v)
            if kind=='mesh':
                options=unreal.FbxImportUI()
                for k,v in dict(import_mesh=True,import_as_skeletal=False,import_materials=False,import_textures=False,import_animations=False,automated_import_should_detect_type=False,mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():options.set_editor_property(k,v)
                for k,v in dict(combine_meshes=False,auto_generate_collision=False,transform_vertex_to_absolute=True,convert_scene=True,convert_scene_unit=True).items():options.static_mesh_import_data.set_editor_property(k,v)
                task.set_editor_property('options',options);task.set_editor_property('factory',unreal.FbxFactory())
            tools.import_asset_tasks([task])
            imported=[unreal.load_asset(p) for p in task.imported_object_paths]
            assert imported,(asset,kind)
            if kind!='mesh':textures[kind]=next(t for t in imported if isinstance(t,unreal.Texture2D))
        material_path=destination+'/M_'+asset
        material=unreal.load_asset(material_path) if unreal.EditorAssetLibrary.does_asset_exist(material_path) else tools.create_asset('M_'+asset,destination,unreal.Material,unreal.MaterialFactoryNew())
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
        for index,(kind,parameter,prop) in enumerate((('albedo','Albedo',unreal.MaterialProperty.MP_BASE_COLOR),('normal','Normal',unreal.MaterialProperty.MP_NORMAL),('roughness','Roughness',unreal.MaterialProperty.MP_ROUGHNESS))):
            texture=textures[kind]
            texture.set_editor_property('srgb',kind=='albedo')
            if kind=='normal':texture.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP)
            unreal.EditorAssetLibrary.save_loaded_asset(texture)
            node=unreal.MaterialEditingLibrary.create_material_expression(material,unreal.MaterialExpressionTextureSampleParameter2D,-400,index*220)
            node.set_editor_property('parameter_name',parameter);node.set_editor_property('texture',texture)
            node.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind=='normal' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if kind=='albedo' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
            unreal.MaterialEditingLibrary.connect_material_property(node,'RGB' if kind!='roughness' else 'R',prop)
        unreal.MaterialEditingLibrary.recompile_material(material)
        unreal.EditorAssetLibrary.save_loaded_asset(material)
        meshes=[]
        for path in unreal.EditorAssetLibrary.list_assets(destination):
            mesh=unreal.load_asset(path)
            if not isinstance(mesh,unreal.StaticMesh):continue
            for i in range(len(mesh.static_materials)):mesh.set_material(i,material)
            unreal.WarehouseCargo.configure_mesh_collision(mesh,False)
            assert mesh_editor.set_convex_decomposition_collisions(mesh,16 if asset.startswith('plastic') else 1,32,100000)
            unreal.EditorAssetLibrary.save_loaded_asset(mesh)
            meshes.append(mesh)
            print('CARGO_MESH_IMPORTED',mesh.get_path_name(),mesh.get_bounds())
        assert meshes,asset
        result[asset]=meshes
    return result

if __name__=='__main__':
    import_cargo_assets()
    print('CARGO_ASSETS_IMPORTED')
