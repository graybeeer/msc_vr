"""Import locally edited four-way pallet into the existing shared mesh; keep materials/size.

Input and original backup: Saved/Testing/PalletFourWay_20261104/. The Blender
build script operates on an export of the old mesh; it never uploads assets.
"""
from pathlib import Path
import json
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
folder=root/'Saved/Testing/PalletFourWay_20261104'
source=folder/'FourWay.fbx'
assert source.is_file(),source
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Engine/Maps/Entry')
path='/Game/Warehouse/Physics/SM_Pallet_110'
old=unreal.load_asset(path)
materials=list(old.get_editor_property('static_materials'))
nanite=old.get_editor_property('nanite_settings')
options=unreal.FbxImportUI()
for key,value in dict(import_mesh=True,import_as_skeletal=False,import_materials=False,import_textures=False,
                     import_animations=False,automated_import_should_detect_type=False,
                     mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():options.set_editor_property(key,value)
for key,value in dict(combine_meshes=True,auto_generate_collision=False,transform_vertex_to_absolute=True,
                     convert_scene=True,convert_scene_unit=False,import_uniform_scale=1.0).items():options.static_mesh_import_data.set_editor_property(key,value)
task=unreal.AssetImportTask()
task.filename=str(source);task.destination_path='/Game/Warehouse/Physics';task.destination_name='SM_Pallet_110'
task.automated=True;task.replace_existing=True;task.replace_existing_settings=True;task.save=False;task.options=options;task.factory=unreal.FbxFactory()
previous=unreal.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.FBX 0')
try:unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
finally:unreal.SystemLibrary.execute_console_command(world,f'Interchange.FeatureFlags.Import.FBX {previous}')
mesh=unreal.load_asset(path)
size=mesh.get_bounds().box_extent*2
assert abs(size.x-110)<.1 and abs(size.y-110)<.1 and abs(size.z-15)<.1,size
mesh.set_editor_property('static_materials',materials)
mesh.set_editor_property('nanite_settings',nanite)
editor=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
assert editor.set_convex_decomposition_collisions(mesh,64,32,1000000)
unreal.WarehousePallet.fit_collision_bounds(mesh)
unreal.WarehouseCargo.configure_mesh_collision(mesh,False)
p=actors.spawn_actor_from_class(unreal.WarehousePallet,unreal.Vector(0,0,0))
checks=[]
for yaw in (0,90,180,270):
    r=unreal.Rotator(yaw=yaw)
    f=unreal.MathLibrary.get_forward_vector(r)
    frame=unreal.Transform(location=-f*65,rotation=r)
    offset=p.get_support_lift_offset(frame)
    assert p.can_engage(frame),(yaw,offset)
    checks.append(dict(yaw=yaw,supportLiftCm=offset))
assert not p.can_engage(unreal.Transform(location=unreal.Vector(-65,12,0)))
assert not p.can_engage(unreal.Transform(location=unreal.Vector(-65,0,2)))
assert not p.can_engage(unreal.Transform(location=unreal.Vector(-90,0,0)))
unreal.EditorAssetLibrary.set_metadata_tag(mesh,'PalletEntryVersion','four-way-20261104')
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh)
(folder/'ImportedGeometry.json').write_text(json.dumps(dict(passed=True,sizeCm=str(size),entries=checks),indent=2),encoding='utf-8')
print('FOUR_WAY_PALLET_IMPORTED',checks)
unreal.SystemLibrary.quit_editor()
