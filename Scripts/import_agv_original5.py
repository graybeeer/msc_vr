"""Import the five-wheel vehicle parts of SourceAssets/OrangeAGV/orange_agv.fbx (grouped and converted by
convert_agv_original5_cm.py) as static meshes into /Game/Warehouse/AGV/Original5. Each part keeps its own pivot
(wheel centres; everything else at the vehicle origin). The drive unit is the refined model's
(/Game/Warehouse/AGV/Refined). Does not touch the teammates' AGV Meshes/Materials/OriginalSource folders or any map."""
import subprocess
from pathlib import Path
import unreal

BLENDER = r'C:\Program Files\Blender Foundation\Blender 4.5\blender.exe'
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SRC = ROOT / 'SourceAssets/OrangeAGV/orange_agv.fbx'
CM_COPY = ROOT / 'Saved/AgvImport/orange_agv_original5.fbx'
DEST = '/Game/Warehouse/AGV/Original5'
PREFIX = 'orange_agv_original5_'

CM_COPY.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([BLENDER, '-b', '--factory-startup', '--python', str(ROOT / 'Scripts/convert_agv_original5_cm.py'),
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

# UE prefixes each split mesh with the file name: orange_agv_original5_SM_AGV5_X -> SM_AGV5_X.
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
        unreal.log(f'AGV5_IMPORT {asset.get_name()} min=({b.min.x:.2f},{b.min.y:.2f},{b.min.z:.2f}) '
                   f'max=({b.max.x:.2f},{b.max.y:.2f},{b.max.z:.2f}) mats={len(asset.static_materials)}')
unreal.log('AGV5_IMPORT_DONE')
