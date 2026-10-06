"""Restore only the original FBX body without compression or added braces.
Run with -RenderOffscreen, never -nullrhi: mesh merging otherwise replaces materials with WorldGrid."""
import unreal,json,re
from pathlib import Path
BASE='/Game/Warehouse/AGV'
ROOT=Path(unreal.Paths.project_dir()).resolve()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
meshes=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
assets=unreal.EditorAssetLibrary
options=unreal.FbxImportUI()
for k,v in dict(import_mesh=True,import_as_skeletal=False,import_materials=True,import_textures=False,import_animations=False,automated_import_should_detect_type=False,mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():options.set_editor_property(k,v)
for k,v in dict(combine_meshes=False,auto_generate_collision=False,transform_vertex_to_absolute=True,convert_scene=True,convert_scene_unit=True,import_rotation=unreal.Rotator(yaw=90)).items():options.static_mesh_import_data.set_editor_property(k,v)
if not assets.does_directory_exist(BASE+'/OriginalSource'):
 task=unreal.AssetImportTask()
 for k,v in dict(filename=str(ROOT/'SourceAssets/OrangeAGV/orange_agv.fbx'),destination_path=BASE+'/OriginalSource',automated=True,replace_existing=True,save=True,options=options,factory=unreal.FbxFactory()).items():task.set_editor_property(k,v)
 unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
groups={};pivots={}
def key(s):return re.sub(r'[^a-z0-9]','',s.lower())
palette={key(Path(path.split('.')[0]).name.removeprefix('M_')):unreal.load_asset(path) for path in assets.list_assets(BASE+'/Materials',recursive=False) if Path(path.split('.')[0]).name.startswith('M_')}
for path in assets.list_assets(BASE+'/OriginalSource'):
 mesh=unreal.load_asset(path)
 if not isinstance(mesh,unreal.StaticMesh):continue
 name=mesh.get_name().removeprefix('orange_agv_')
 if name=='Ground' or name.startswith('Blue_floor_boundary'):continue
 bounds=mesh.get_bounds();center=bounds.origin
 side=1 if center.y<0 else -1;suffix='L' if side<0 else 'R'
 group='Body'
 if name.startswith(('Driven_rubber_wheel','Drive_wheel_hub')):group='DriveWheel'+suffix
 elif name.startswith(('Front_polyurethane_load_roller','Roller_axle_cap')):group='LoadWheel'+suffix
 elif name.startswith('Outrigger_support_leg'):group='Outrigger'+suffix
 elif name.startswith(('Forged_fork_blade','Tapered_fork_nose')):group='Fork'+suffix
 elif name.startswith(('Vertical_fork_heel','Fork_carriage','Load_backrest','Carriage_locking')):group='Carriage'
 elif name.startswith(('Inner_sliding_mast','Chrome_lift_ram','Lift_chain')):group='LiftStage'
 elif name.startswith(('Outer_mast','Polished_mast','Mast_cross','Hydraulic_cylinder','Hydraulic_hose')):group='MastFrame'
 elif any(key in name for key in ('Navigation','Sensor_crossbar','Stereo_sensor','Lidar','HMI','Display_status','Upper_','Front_perception','Tower_fleet','Emergency_stop','Green_status','Side_status','Warning_')):group='SensorTower'
 if group!='Body':continue
 actor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(11.5,0,0),unreal.Rotator(yaw=180))
 actor.static_mesh_component.set_static_mesh(mesh)
 for i,slot in enumerate(mesh.static_materials):
  material_key=key(str(slot.material_slot_name))
  assert material_key in palette,(name,str(slot.material_slot_name),list(palette))
  mesh.set_material(i,palette[material_key])
  actor.static_mesh_component.set_material(i,palette[material_key])
 meshes.remove_collisions(mesh)
 if group.startswith('Fork'):meshes.add_simple_collisions(mesh,unreal.ScriptingCollisionShapeType.BOX)
 else:assert meshes.set_convex_decomposition_collisions(mesh,1,32,10000),name
 unreal.WarehouseCargo.configure_mesh_collision(mesh,False)
 groups.setdefault(group,[]).append(actor)
 # All changes above are rigid coordinate conversion, no vertex or component scaling.
 assert actor.get_actor_scale3d()==unreal.Vector(1,1,1)
for group,sources in groups.items():
 pivot=unreal.Vector()
 if 'Wheel' in group:
  lows=[];highs=[]
  for a in sources:
   c,e=a.get_actor_bounds(False);lows.append(c-e);highs.append(c+e)
  pivot=unreal.Vector(*[(min(getattr(v,ax) for v in lows)+max(getattr(v,ax) for v in highs))/2 for ax in ('x','y','z')])
  for a in sources:a.set_actor_location(a.get_actor_location()-pivot,False,False)
 opt=unreal.MergeStaticMeshActorsOptions()
 opt.base_package_name=BASE+'/Meshes/Original_AGV_'+group
 opt.destroy_source_actors=True;opt.spawn_merged_actor=True
 st=opt.mesh_merging_settings;st.pivot_type=unreal.MeshMergePivotType.WORLD_ORIGIN;st.merge_materials=False;st.merge_physics_data=True;st.generate_light_map_uv=False;opt.mesh_merging_settings=st
 merged=meshes.merge_static_mesh_actors(sources,opt);assert merged,group
 result=merged.static_mesh_component.static_mesh
 assert all(slot.material_interface in palette.values() for slot in result.static_materials),[(str(slot.material_slot_name),str(slot.material_interface)) for slot in result.static_materials]
 unreal.WarehouseCargo.configure_mesh_collision(result,False);assets.save_loaded_asset(result)
 pivots[group]=[pivot.x,pivot.y,pivot.z]
 print('ORIGINAL_PART',group,result.get_path_name(),pivots[group],result.get_bounds())
 actors.destroy_actor(merged)
(ROOT/'Saved/OriginalAGVPivots.json').write_text(json.dumps(pivots,indent=2))
print('ORIGINAL_AGV_RESTORED',len(groups))
