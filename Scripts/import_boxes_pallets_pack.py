"""Import locally installed Fab Boxes & Pallets Pack. Licensed binaries stay ignored by Git."""
from pathlib import Path
import unreal,re
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'SourceAssets/Cargo/BoxesPalletsPack/Box Cargo Collection FBX'
DEST='/Game/Warehouse/Cargo/BoxesPalletsPack'
tools=unreal.AssetToolsHelpers.get_asset_tools()
editor=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
lib=unreal.MaterialEditingLibrary

def imported(filename,destination,name=None,options=None):
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(filename),destination_path=destination,automated=True,replace_existing=True,save=True).items():task.set_editor_property(k,v)
    if name:task.destination_name=name
    if options:
        task.options=options;task.factory=unreal.FbxFactory()
    tools.import_asset_tasks([task])
    result=[unreal.load_asset(p) for p in task.imported_object_paths]
    assert result,str(filename)
    return result

def import_pack():
    assert (SOURCE/'Cargo.fbx').is_file(),'Extract Fab ZIP into '+str(SOURCE.parent)
    mappings={};current=None
    for line in (SOURCE/'Cargo.mtl').read_text().splitlines():
        if line.startswith('newmtl '):
            current=line[7:];mappings[current]={}
        elif line.startswith('map_Kd '):mappings[current]['Albedo']=line[7:]
        elif line.startswith('map_Ns '):mappings[current]['Roughness']=line[7:]
        elif line.startswith('map_Bump '):mappings[current]['Normal']=line.split('1.000000 ',1)[1]
    materials={};textures={}
    for name,maps in mappings.items():
        path=DEST+'/Materials/M_'+name
        material=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset('M_'+name,DEST+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
        lib.delete_all_material_expressions(material)
        for i,(kind,relative) in enumerate(maps.items()):
            if relative not in textures:
                safe=re.sub(r'[^A-Za-z0-9_]','_',relative.removesuffix('.png'))
                tex=imported(SOURCE/relative,DEST+'/Textures','T_'+safe)[0]
                tex.set_editor_property('srgb',kind=='Albedo')
                if kind=='Normal':
                    tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP)
                    tex.set_editor_property('flip_green_channel',True) # Blender/OpenGL normal map to Unreal.
                unreal.EditorAssetLibrary.save_loaded_asset(tex)
                textures[relative]=tex
            node=lib.create_material_expression(material,unreal.MaterialExpressionTextureSampleParameter2D,-400,i*220)
            node.set_editor_property('parameter_name',kind);node.texture=textures[relative]
            node.sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind=='Normal' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if kind=='Albedo' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
            prop={'Albedo':unreal.MaterialProperty.MP_BASE_COLOR,'Normal':unreal.MaterialProperty.MP_NORMAL,'Roughness':unreal.MaterialProperty.MP_ROUGHNESS}[kind]
            lib.connect_material_property(node,'R' if kind=='Roughness' else 'RGB',prop)
        lib.recompile_material(material);unreal.EditorAssetLibrary.save_loaded_asset(material)
        materials[re.sub(r'[^a-z0-9]','',name.lower())]=material
    options=unreal.FbxImportUI()
    for k,v in dict(import_mesh=True,import_as_skeletal=False,import_materials=False,import_textures=False,import_animations=False,automated_import_should_detect_type=False,mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():options.set_editor_property(k,v)
    for k,v in dict(combine_meshes=False,auto_generate_collision=False,transform_vertex_to_absolute=False,convert_scene=True,convert_scene_unit=True).items():options.static_mesh_import_data.set_editor_property(k,v)
    meshes=imported(SOURCE/'Cargo.fbx',DEST+'/Meshes',options=options)
    for mesh in meshes:
        if not isinstance(mesh,unreal.StaticMesh):continue
        for i,slot in enumerate(mesh.static_materials):
            key=re.sub(r'[^a-z0-9]','',str(slot.material_slot_name).lower())
            key=re.sub(r'ncl[0-9]+$','',key) # FBX duplicate-name suffix.
            assert key in materials,(mesh.get_name(),key)
            mesh.set_material(i,materials[key])
        editor.remove_collisions(mesh)
        # Only closed individual cartons will be deployed; all other models remain available as source assets.
        editor.add_simple_collisions(mesh,unreal.ScriptingCollisionShapeType.BOX)
        unreal.WarehouseCargo.configure_mesh_collision(mesh,False)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
        print('FAB_BOX_MESH',mesh.get_name(),mesh.get_bounds())
    print('FAB_BOXES_IMPORTED',len(meshes),'meshes',len(textures),'textures')

if __name__=='__main__':import_pack()
