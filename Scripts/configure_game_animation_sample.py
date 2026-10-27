"""Run in msc_vr after importing sample motions and building the native module.

Creates direction/speed blend spaces and replaces only the player's source
locomotion class. Existing first-person copy and carry/phone IK remain connected.
"""
import json
import shutil
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = '/Game/Warehouse/AnimationSample'
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
player_path = '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter'
player = unreal.load_asset(player_path)
defaults = unreal.get_default_object(player.generated_class())
mesh = defaults.get_editor_property('mesh')
skeleton = mesh.skeletal_mesh_asset.skeleton
backup = ROOT / 'Saved/Testing/AnimationSampleBackup/BP_FirstPersonCharacter.uasset'
if not backup.exists():
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / 'Content/FirstPerson/Blueprints/BP_FirstPersonCharacter.uasset', backup)

clips = {}
unused_transitions = {'M_Relaxed_Transition_Stand_to_Crouch', 'M_Relaxed_Transition_Crouch_to_Stand'}
for path in assets.list_assets(DIRECTORY + '/Sequences', recursive=True, include_folder=False):
    clip = unreal.load_asset(path)
    assert isinstance(clip, unreal.AnimSequence), path
    if clip.get_name() in unused_transitions:
        continue
    assert unreal.WarehouseLocomotionAnimInstance.prepare_motion_asset(clip, skeleton), path
    clip.set_preview_skeletal_mesh(mesh.skeletal_mesh_asset)
    # Keep the baked retarget reference pose; the sample-only source mesh is not
    # a runtime dependency. Do not call update_retarget_source_asset_data here.
    clip.clear_retarget_source_asset()
    clip.set_editor_property('enable_root_motion', False)
    clip.set_editor_property('force_root_lock', True)
    assert assets.save_loaded_asset(clip)
    clips[clip.get_name()] = clip
assert len(clips) == 30, len(clips)

directions = [(-180,'B'),(-135,'BL'),(-90,'LL'),(-45,'FL'),(0,'F'),(45,'FR'),(90,'RR'),(135,'BR'),(180,'B')]

def editor_struct(kind,**properties):
    value=kind()
    for name,data in properties.items():
        value.set_editor_property(name,data)
    return value

def create_space(name, crouching):
    path = DIRECTORY + '/' + name
    factory = unreal.BlendSpaceFactoryNew()
    factory.set_editor_property('target_skeleton',skeleton)
    factory.set_editor_property('preview_skeletal_mesh',mesh.skeletal_mesh_asset)
    space = unreal.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(name, DIRECTORY, unreal.BlendSpace, factory)
    parameters = list(space.get_editor_property('blend_parameters'))
    parameters[0] = editor_struct(unreal.BlendParameter,display_name='Direction', min=-180, max=180, grid_num=8, wrap_input=True)
    parameters[1] = editor_struct(unreal.BlendParameter,display_name='Speed (cm/s)', min=0, max=60 if crouching else 500, grid_num=5)
    space.set_editor_property('blend_parameters', parameters)
    samples = []
    for direction, suffix in directions:
        idle = clips['M_Neutral_Crouch_Idle_Loop' if crouching else 'M_Relaxed_Stand_Idle_Loop']
        samples.append(editor_struct(unreal.BlendSample,animation=idle,sample_value=unreal.Vector(direction,0,0)))
        for gait,speed,rate in ([('Crouch',60,1)] if crouching else [('Walk',160,1),('Run',350,1),('Run',500,500/350)]):
            tail = 'RR_offset' if gait in ('Walk','Run') and suffix=='RR' else suffix
            clip = clips[f'M_Relaxed_{gait}_Loop_{tail}']
            if speed==500 and suffix=='F':
                clip = clips['M_Relaxed_Sprint_Loop_F'];rate=1
            samples.append(editor_struct(unreal.BlendSample,animation=clip,sample_value=unreal.Vector(direction,speed,0),rate_scale=rate))
    space.set_editor_property('sample_data', samples)
    space.set_preview_skeletal_mesh(mesh.skeletal_mesh_asset)
    assert unreal.WarehouseLocomotionAnimInstance.prepare_motion_asset(space,skeleton)
    assert assets.save_loaded_asset(space)
    return len(samples)

counts = [create_space('BS_WarehouseStanding',False),create_space('BS_WarehouseCrouching',True)]
mesh.set_editor_property('anim_class',unreal.WarehouseLocomotionAnimInstance.static_class())
assert assets.save_loaded_asset(player)
report = dict(player=player_path,mesh=mesh.skeletal_mesh_asset.get_path_name(),skeleton=skeleton.get_path_name(),
              animation_class=mesh.get_editor_property('anim_class').get_path_name(),clips=len(clips),samples=counts,
              physical_movement='unchanged',root_motion='disabled; actual Chaos velocity drives animation')
(ROOT / 'Saved/Testing/GameAnimationSampleConfiguration.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('GAME_ANIMATION_SAMPLE_CONFIGURATION_PASSED',report)
