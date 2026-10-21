"""Called through the project's official MCP toolset; imports and applies the cargo lift."""
import json,re,hashlib
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'Saved/Testing/CargoElevator';OUT.mkdir(parents=True,exist_ok=True)
BASE='/Game/Warehouse/Elevator'
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets=unreal.EditorAssetLibrary
assert not editor.get_game_world()
assert editor.get_editor_world().get_path_name().startswith('/Game/FirstPerson/Lvl_FirstPerson')
manifest=json.loads((ROOT/'Art/Equipment/CargoElevator/export_manifest.json').read_text(encoding='utf-8'))
tools=unreal.AssetToolsHelpers.get_asset_tools()
materials={}
for name,values in manifest['materials'].items():
    safe='M_'+re.sub('[^A-Za-z0-9_]','_',name)
    path=BASE+'/Materials/'+safe
    mat=unreal.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(safe,BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
    for i,(key,prop,vector) in enumerate([('base_color',unreal.MaterialProperty.MP_BASE_COLOR,True),('metallic',unreal.MaterialProperty.MP_METALLIC,False),('roughness',unreal.MaterialProperty.MP_ROUGHNESS,False)]):
        node=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector if vector else unreal.MaterialExpressionConstant,-250,i*130)
        value=values[key]
        node.set_editor_property('constant' if vector else 'r',unreal.LinearColor(*value) if vector else value)
        unreal.MaterialEditingLibrary.connect_material_property(node,'',prop)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    assets.save_loaded_asset(mat)
    materials[re.sub('[^a-z0-9]','',name.lower())]=mat
meshes={}
for part in manifest['parts']:
    name=part['name'];path=BASE+'/Meshes/'+name
    source_file=ROOT/'Art/Equipment/CargoElevator'/part['file']
    source_hash=hashlib.sha256(source_file.read_bytes()).hexdigest()
    mesh=unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if not mesh or assets.get_metadata_tag(mesh,'ElevatorSourceHash')!=source_hash:
        options=unreal.FbxImportUI()
        for k,v in dict(import_mesh=True,import_as_skeletal=False,import_materials=False,import_textures=False,import_animations=False,automated_import_should_detect_type=False,mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():options.set_editor_property(k,v)
        for k,v in dict(combine_meshes=True,auto_generate_collision=False,transform_vertex_to_absolute=True,convert_scene=True,convert_scene_unit=True,import_rotation=unreal.Rotator()).items():options.static_mesh_import_data.set_editor_property(k,v)
        task=unreal.AssetImportTask()
        for k,v in dict(filename=str(source_file),destination_path=BASE+'/Meshes',destination_name=name,automated=True,replace_existing=True,save=True,options=options,factory=unreal.FbxFactory()).items():task.set_editor_property(k,v)
        tools.import_asset_tasks([task]);mesh=unreal.load_asset(path)
    assert isinstance(mesh,unreal.StaticMesh),name
    extent=mesh.get_bounds().box_extent
    dimensions=[extent.x*2,extent.y*2,extent.z*2]
    assert max(abs(a-b) for a,b in zip(dimensions,part['dimensions_cm']))<.2,(name,dimensions,part['dimensions_cm'])
    for i,slot in enumerate(mesh.static_materials):
        normalized=re.sub('[^a-z0-9]','',str(slot.material_slot_name).lower())
        assert normalized in materials,(name,normalized)
        mesh.set_material(i,materials[normalized])
    unreal.WarehouseElevator.configure_model_collision(mesh,name.removeprefix('SM_ELV_'))
    assets.set_metadata_tag(mesh,'ElevatorSourceHash',source_hash)
    assets.save_loaded_asset(mesh)
    meshes[name]=mesh
lifts=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseElevator)]
assert len(lifts)==1
lift=lifts[0];before=lift.get_actor_transform()
lift.modify();lift.set_editor_property('use_cargo_model',True);lift.apply_cargo_model()
stands=[]
for floor in range(3):
    label=f'MZ_L{floor+1}_LiftCallStand'
    matches=[a for a in actors.get_all_level_actors() if a.get_actor_label()==label]
    assert len(matches)<=1,label
    part=next(p for p in manifest['parts'] if p['name']==f'SM_ELV_CallStand_L{floor+1}')
    local=unreal.Vector(*part['pivot_cm'])
    stand=matches[0] if matches else actors.spawn_actor_from_class(unreal.StaticMeshActor,before.transform_location(local),lift.get_actor_rotation())
    stand.modify();stand.set_actor_label(label)
    stand.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    stand.static_mesh_component.set_static_mesh(meshes[part['name']])
    stand.static_mesh_component.set_collision_profile_name('BlockAllDynamic')
    stand.set_actor_transform(unreal.MathLibrary.compose_transforms(unreal.Transform(location=local),before),False,False)
    assert stand.attach_to_actor(lift,'',unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,False)
    stand.tags=[unreal.Name('LayoutFixed'),unreal.Name('ElevatorCallStand')]+[t for t in lift.tags if str(t).startswith('LayoutGroup:')]
    assert stand.static_mesh_component.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION
    centre,extent=stand.get_actor_bounds(False)
    assert abs(centre.z-extent.z-floor*400)<.2,(label,centre,extent)
    stands.append({'label':label,'local_base_cm':part['pivot_cm'],'position':str(stand.get_actor_location()),'collision':'QueryAndPhysics'})
# A 1 cm seam can generate speculative corner contacts on descent, then release
# stored guide force. Trim only the three approach slabs and ground rear edge;
# retain their material, thickness and all surrounding supported floor.
trimmed=[]
for actor in actors.get_all_level_actors():
    label=actor.get_actor_label()
    if label not in ('Floor','MZ_L2_Deck_Right','MZ_L3_Deck_Right','MZ_GroundRear'):continue
    centre,extent=actor.get_actor_bounds(False)
    low,high=centre.y-extent.y,centre.y+extent.y
    if label=='MZ_GroundRear':
        assert abs(low-1776)<.2 or abs(low-1781)<.2,(label,low)
        newlow,newhigh=1781,high
    else:
        assert abs(high-1424)<.2 or abs(high-1419)<.2,(label,high)
        newlow,newhigh=low,1419
    actor.modify()
    scale=actor.get_actor_scale3d();scale.y*=(newhigh-newlow)/(high-low)
    actor.set_actor_scale3d(scale)
    position=actor.get_actor_location();position.y+=(newlow+newhigh)/2-centre.y
    actor.set_actor_location(position,False,False)
    trimmed.append(label)
assert len(trimmed)==4,trimmed
lift.set_editor_property('ground_opening_size_cm',unreal.Vector2D(282,362))
lift.set_editor_property('upper_opening_size_cm',unreal.Vector2D(282,381))
lift.set_editor_property('upper_opening_offset_cm',unreal.Vector2D(0,9.5))
# The new face already contains floor/load labels; retire only the old lift signs.
retired=[]
for actor in actors.get_all_level_actors():
    if actor.get_actor_label() in ('MZ_L1_LiftSign','MZ_L2_LiftSign','MZ_L3_LiftSign'):
        actor.modify();actor.set_actor_hidden_in_game(True)
        for c in actor.get_components_by_class(unreal.TextRenderComponent):
            c.modify();c.set_visibility(False);c.set_hidden_in_game(True)
        retired.append(actor.get_actor_label())
assert unreal.MathLibrary.nearly_equal_transform_transform(before,lift.get_actor_transform(),.001,.001,.001)
assert level.save_current_level()
assets.save_directory(BASE,False,True)
report={'date':'2026-10-21','map_saved':True,'lift':lift.get_actor_label(),'meshes':list(meshes),'retired_signs':retired,'call_stands':stands,
    'trimmed_slabs':trimmed,'ground_opening_cm':[282,362],'upper_opening_cm':[282,381],
    'upper_opening_offset_cm':[0,9.5],'physics_test_complete':False}
(OUT/'ApplyReport.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
eye=lift.get_actor_transform().transform_location(unreal.Vector(-720,-420,230))
target=lift.get_actor_transform().transform_location(unreal.Vector(-120,0,150))
editor.set_level_viewport_camera_info(eye,unreal.MathLibrary.find_look_at_rotation(eye,target))
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
print('CARGO_ELEVATOR_APPLIED',json.dumps(report))
# Capture through the engine after materials settle; the preview camera is never saved.
import time,traceback
camera=actors.spawn_actor_from_class(unreal.CameraActor,eye,unreal.MathLibrary.find_look_at_rotation(eye,target))
camera.set_actor_label('TEMP_CargoElevatorCapture');camera.camera_component.set_field_of_view(65)
capture_started=time.monotonic();capture_phase=0
def capture_tick(dt):
    global capture_phase
    try:
        if time.monotonic()-capture_started<5:return
        if capture_phase==0:
            unreal.AutomationLibrary.take_high_res_screenshot(1600,1100,str(OUT/'ElevatorInWarehouse.png'),camera=camera,delay=.3)
            capture_phase=1;return
        if time.monotonic()-capture_started<9:return
        actors.destroy_actor(camera);unreal.unregister_slate_post_tick_callback(capture_handle)
    except Exception:
        unreal.log_warning(traceback.format_exc());unreal.unregister_slate_post_tick_callback(capture_handle)
capture_handle=unreal.register_slate_post_tick_callback(capture_tick)
