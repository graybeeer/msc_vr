"""Import SourceAssets/OrangeAGV/orange_agv_refined.fbx as separate static meshes into /Game/Warehouse/AGV/Refined.
Each part keeps its own pivot (wheel centre, steer axis, mast/carriage origin); placement offsets are in
Source/msc_vr/Agv/FORKLIFT_NAVIGATION.md. Does not touch the existing AGV Meshes/Materials folders or any map."""
import subprocess
from pathlib import Path
import unreal

BLENDER = r'C:\Program Files\Blender Foundation\Blender 4.5\blender.exe'
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SRC = ROOT / 'SourceAssets/OrangeAGV/orange_agv_refined.fbx'
CM_COPY = ROOT / 'Saved/AgvImport/orange_agv_refined.fbx'
DEST = '/Game/Warehouse/AGV/Refined'
PREFIX = 'orange_agv_refined_'

CM_COPY.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([BLENDER, '-b', '--factory-startup', '--python', str(ROOT / 'Scripts/convert_agv_refined_cm.py'),
                '--', str(SRC), str(CM_COPY)], check=True)

# The folder holds only this import; clearing it lets the prefix rename below succeed on reruns.
if unreal.EditorAssetLibrary.does_directory_exist(DEST):
    unreal.EditorAssetLibrary.delete_directory(DEST)

options = unreal.FbxImportUI()
for key, value in dict(import_mesh=True, import_as_skeletal=False, import_materials=True, import_textures=False,
                       import_animations=False, automated_import_should_detect_type=False,
                       mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():
    options.set_editor_property(key, value)
for key, value in dict(combine_meshes=False, auto_generate_collision=False, transform_vertex_to_absolute=False,
                       bake_pivot_in_vertex=False, convert_scene=True, convert_scene_unit=True).items():
    options.static_mesh_import_data.set_editor_property(key, value)
task = unreal.AssetImportTask()
for key, value in dict(filename=str(CM_COPY), destination_path=DEST, automated=True, replace_existing=True,
                       save=True, options=options, factory=unreal.FbxFactory()).items():
    task.set_editor_property(key, value)
tools = unreal.AssetToolsHelpers.get_asset_tools()
tools.import_asset_tasks([task])

# UE prefixes each split mesh with the file name: orange_agv_refined_SM_AGV_X -> SM_AGV_X.
renames = []
for path in task.get_editor_property('imported_object_paths'):
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.StaticMesh) and asset.get_name().startswith(PREFIX):
        renames.append(unreal.AssetRenameData(asset, DEST, asset.get_name()[len(PREFIX):]))
tools.rename_assets(renames)
unreal.EditorAssetLibrary.save_directory(DEST)

for path in sorted(unreal.EditorAssetLibrary.list_assets(DEST)):
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.StaticMesh):
        b = asset.get_bounding_box()
        unreal.log(f'AGV_IMPORT {asset.get_name()} min=({b.min.x:.2f},{b.min.y:.2f},{b.min.z:.2f}) '
                   f'max=({b.max.x:.2f},{b.max.y:.2f},{b.max.z:.2f})')
unreal.log('AGV_REFINED_IMPORT_DONE')
