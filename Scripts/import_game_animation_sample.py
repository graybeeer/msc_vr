"""Run in the downloaded UE 5.8 GameAnimationSample with -run=pythonscript.

Retarget a selected locomotion set to the sample's UE5 Quinn skeleton, remove
sample-only gameplay notifies, then migrate the result and its dependencies.
Original motion tracks and the warehouse map are not edited.
"""
import json
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = '/Game/Warehouse/AnimationSample/Sequences'
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
names = ['M_Relaxed_Stand_Idle_Loop', 'M_Neutral_Crouch_Idle_Loop',
         'M_Relaxed_Jump_Loop_Fall', 'M_Neutral_Jump_F_Start_Stand_Lfoot',
         'M_Neutral_Jump_F_Land_Stand_Light_Lfoot']
for gait in ['Walk', 'Run', 'Crouch']:
    for direction in ['F', 'B', 'LL', 'RR', 'FL', 'FR', 'BL', 'BR']:
        # Relaxed lateral loops use a different name for the right-facing stride.
        suffix = 'RR_offset' if gait in ('Walk', 'Run') and direction == 'RR' else direction
        names.append(f'M_Relaxed_{gait}_Loop_{suffix}')
names.append('M_Relaxed_Sprint_Loop_F')
data = registry.get_assets_by_path('/Game/Characters/UEFN_Mannequin/Animations', recursive=True)
selected = [a for a in data if str(a.asset_name) in names and str(a.asset_class_path.asset_name) == 'AnimSequence']
assert set(names) == {str(a.asset_name) for a in selected}, sorted(set(names)-{str(a.asset_name) for a in selected})
inputs = unreal.IKRetargetBatchOperationInputs(
    assets_to_retarget=selected,
    source_mesh=unreal.load_asset('/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin'),
    target_mesh=unreal.load_asset('/Game/Characters/UE5_Mannequins/Meshes/SKM_Quinn_Simple'),
    ik_retarget_asset=unreal.load_asset('/Game/Characters/UE5_Mannequins/Rigs/RTG_UEFN_to_UE5_Mannequin'),
    target_path=OUTPUT, include_referenced_assets=False, overwrite_existing_files=True)
existing = [a for a in registry.get_assets_by_path(OUTPUT, recursive=True) if str(a.asset_name) in names]
result = existing if {str(a.asset_name) for a in existing} == set(names) else unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
assert len(result) == len(selected), (len(result), len(selected))
packages = []
for asset_data in result:
    clip = asset_data.get_asset()
    unreal.AnimationLibrary.remove_all_animation_notify_tracks(clip)
    clip.set_editor_property('enable_root_motion', False)
    clip.set_editor_property('force_root_lock', True)
    # Animation modifiers and pose-search annotations belong to the sample's
    # gameplay framework. Only the baked skeletal motion is reused here.
    clip.set_editor_property('asset_user_data', [])
    clip.set_editor_property('meta_data', [])
    assert unreal.EditorAssetLibrary.save_loaded_asset(clip)
    packages.append(str(asset_data.package_name))
options = unreal.MigrationOptions(prompt=False, asset_conflict=unreal.AssetMigrationConflict.SKIP)
# Newly duplicated animation packages may not yet have dependency entries in the
# registry. Include the baked target skeleton explicitly for a clean first load.
migration_packages=packages+['/Game/Characters/UE5_Mannequins/Meshes/SK_Mannequin']
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(migration_packages, str(ROOT / 'Content'), options)
report = dict(source_project=unreal.Paths.get_project_file_path(), source_names=names, packages=packages)
(ROOT / 'Saved/Testing/GameAnimationSampleImport.json').write_text(json.dumps(report,indent=2), encoding='utf-8')
print('GAME_ANIMATION_SAMPLE_IMPORT_PASSED', len(packages))
