"""Import and calibrate the supplied refined AGV. Raw imports are rebuildable; final rig is tracked."""
from pathlib import Path
import json, re, unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
BASE='/Game/Warehouse/AGV'
assets=unreal.EditorAssetLibrary
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
meshes=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

def import_source():
    options=unreal.FbxImportUI()
    for k,v in dict(import_mesh=True,import_as_skeletal=False,import_materials=True,import_textures=False,
                    import_animations=False,automated_import_should_detect_type=False,mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():
        options.set_editor_property(k,v)
    for k,v in dict(combine_meshes=False,auto_generate_collision=False,transform_vertex_to_absolute=True,
                    convert_scene=True,convert_scene_unit=True,import_rotation=unreal.Rotator(yaw=90)).items():
        options.static_mesh_import_data.set_editor_property(k,v)
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(ROOT/'SourceAssets/OrangeAGV/orange_agv_refined.fbx'),destination_path=BASE+'/RefinedSource',
                    automated=True,replace_existing=True,save=True,options=options,factory=unreal.FbxFactory()).items():
        task.set_editor_property(k,v)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

def inspect():
    rows=[]
    for path in assets.list_assets(BASE+'/RefinedSource'):
        mesh=unreal.load_asset(path)
        if not isinstance(mesh,unreal.StaticMesh) or '/Working/' in path:continue
        b=mesh.get_bounds()
        row={'name':mesh.get_name(),'path':path,'center':[b.origin.x,b.origin.y,b.origin.z],
             'size':[b.box_extent.x*2,b.box_extent.y*2,b.box_extent.z*2],
             'materials':[str(s.material_slot_name) for s in mesh.static_materials]}
        rows.append(row);print('REFINED_SOURCE_PART',json.dumps(row))
    assert rows,'FBX import produced no meshes'
    (ROOT/'Saved/RefinedAGVSource.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
    return rows

def build(rows):
    source={r['name'].removeprefix('orange_agv_refined_SM_AGV_'):r for r in rows}
    chassis=source['Chassis']
    scale=215/(chassis['center'][2]+chassis['size'][2]/2)
    # The front face of the upright fork heel is at source Y=34cm. It defines runtime X=0.
    origin_x=34*scale
    fork_drop=3.5-23.5*scale
    palette={re.sub('[^a-z0-9]','',unreal.load_asset(p).get_name().removeprefix('M_').lower()):unreal.load_asset(p)
             for p in assets.list_assets(BASE+'/Materials',recursive=False) if isinstance(unreal.load_asset(p),unreal.MaterialInterface)}
    groups={};wheel_pivots={};report={'uniform_scale':scale,'origin_x':origin_x,'fork_drop':fork_drop,'parts':{}}
    for name,row in source.items():
        group={'Chassis':'Body','Fork_L':'ForkL','Fork_R':'ForkR','LiftCarriage':'Carriage','MastInner':'LiftStage',
               'LiftRam':'LiftRam','LiftPulley':'LiftPulley','LiftChains':'LiftChains','DriveSteer':'DriveSteer',
               'Wheel_Drive':'DriveWheel','Wheel_Support_L':'LoadWheelL','Wheel_Support_R':'LoadWheelR'}.get(name,'Body')
        working=BASE+'/RefinedSource/Working/'+name
        if assets.does_asset_exist(working):assets.delete_asset(working)
        mesh=assets.duplicate_asset(row['path'].split('.')[0],working)
        assert mesh,name
        for i,slot in enumerate(mesh.static_materials):
            key=re.sub('[^a-z0-9]','',str(slot.material_slot_name).lower())
            if key.startswith('agvwheelpolyurethane'):key='polyurethaneroller'
            assert key in palette,(name,key)
            mesh.set_material(i,palette[key])
        description=mesh.get_static_mesh_description(0)
        assert description,name
        pivot=unreal.Vector()
        if name.startswith('Wheel_'):
            raw=row['center'];pivot=unreal.Vector(raw[1]*scale-origin_x,-raw[0]*scale,raw[2]*scale)
            if name.startswith('Wheel_Support'):
                # Keep round wheels at uniform scale. Their outer faces define the 99.4cm vehicle envelope.
                pivot.y=(1 if name.endswith('_L') else -1)*(49.7-row['size'][0]*scale/2)
            wheel_pivots[group]=[pivot.x,pivot.y,pivot.z]
        chain_top=(row['center'][2]+row['size'][2]/2)*scale if name=='LiftChains' else 0
        chain_height=row['size'][2]*scale if name=='LiftChains' else 0
        if name=='LiftChains':
            pivot=unreal.Vector(0,0,chain_top-chain_height+fork_drop)
            wheel_pivots[group]=[pivot.x,pivot.y,pivot.z]
        for i in range(description.get_vertex_count()):
            vid=unreal.VertexID(i)
            v=description.get_vertex_position(vid)
            out=unreal.Vector(v.y*scale-origin_x,-v.x*scale,v.z*scale)
            if name.startswith('Fork_'):
                # Resize only the tine's horizontal reach; the upright heel keeps its thickness.
                out.x=(v.y-34)*scale if v.y<=34 else (v.y-34)*115/(182-34)
                out.y=(25 if name.endswith('_L') else -25)-(v.x-row['center'][0])
                out.z=3.5+(v.z-23.5)*6/6.75 if v.z<=30.25 else 9.5+(v.z-30.25)*scale
            elif name=='LiftCarriage':out.z+=fork_drop
            elif name=='LiftChains':
                out.z=chain_top-(chain_top-out.z)*(chain_height-fork_drop)/chain_height
                out.z-=pivot.z
            if name.startswith('Wheel_'):
                raw=row['center']
                out=unreal.Vector((v.y-raw[1])*scale,-(v.x-raw[0])*scale,(v.z-raw[2])*scale)
            description.set_vertex_position(vid,out)
        unreal.WarehouseForklift.rebuild_edited_mesh(mesh)
        settings=meshes.get_lod_build_settings(mesh,0)
        settings.recompute_normals=True;settings.recompute_tangents=True
        meshes.set_lod_build_settings(mesh,0,settings)
        meshes.remove_collisions(mesh)
        assert meshes.set_convex_decomposition_collisions(mesh,32 if group=='Body' else 2,32,100000),name
        unreal.WarehouseCargo.configure_mesh_collision(mesh,False)
        unreal.WarehousePallet.fit_collision_bounds(mesh)
        a=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector())
        a.static_mesh_component.set_static_mesh(mesh)
        # Wheel geometry is centred at its rotation pivot, baked into the final mesh.
        groups.setdefault(group,[]).append(a)
    for group,items in groups.items():
        opt=unreal.MergeStaticMeshActorsOptions()
        opt.base_package_name=BASE+'/Meshes/Refined_AGV_'+group
        opt.destroy_source_actors=True;opt.spawn_merged_actor=True
        st=opt.mesh_merging_settings
        st.pivot_type=unreal.MeshMergePivotType.WORLD_ORIGIN
        st.merge_materials=False;st.merge_physics_data=True;st.generate_light_map_uv=False
        opt.mesh_merging_settings=st
        merged=meshes.merge_static_mesh_actors(items,opt);assert merged,group
        mesh=merged.static_mesh_component.static_mesh
        assert all(s.material_interface in palette.values() for s in mesh.static_materials),group
        unreal.WarehouseCargo.configure_mesh_collision(mesh,False)
        if group in ('ForkL','ForkR'):unreal.WarehouseForklift.configure_fork_collision(mesh)
        assets.save_loaded_asset(mesh)
        b=mesh.get_bounds()
        report['parts'][group]={'path':mesh.get_path_name(),'center':[b.origin.x,b.origin.y,b.origin.z],
                               'size':[b.box_extent.x*2,b.box_extent.y*2,b.box_extent.z*2],
                               'pivot':wheel_pivots.get(group,[0,0,0])}
        actors.destroy_actor(merged)
        print('REFINED_RIG_PART',group,report['parts'][group])
    (ROOT/'Saved/RefinedAGVCalibration.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('REFINED_AGV_PREPARED',len(groups),'groups, uniform chassis scale',scale)

if __name__=='__main__':
    if not assets.does_directory_exist(BASE+'/RefinedSource'):import_source()
    build(inspect())
