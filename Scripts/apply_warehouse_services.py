"""Import service modules, add owned decorative layout, save, and capture real editor views."""
import unreal,json,re,math,time,traceback
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/Testing/WarehouseServices';OUT.mkdir(parents=True,exist_ok=True)
SOURCE=ROOT/'Art/Environment/WarehouseServices';BASE='/Game/Warehouse/Services';PREFIX='WS_'
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert editor.get_game_world() is None;assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
assets=unreal.EditorAssetLibrary;tools=unreal.AssetToolsHelpers.get_asset_tools()
meta=json.loads((SOURCE/'modules.json').read_text(encoding='utf-8'));guard=json.loads((SOURCE/'ColumnProtection/modules.json').read_text(encoding='utf-8'))
values=meta['materials'].copy()
for n,c,m,r in [('CP Matte molded rubber',[.016,.022,.025,1],0,.74),('CP Yellow warning strip',[.94,.56,.022,1],.03,.43),('CP Recessed screw steel',[.26,.30,.31,1],.82,.39)]:values[n]={'Base Color':c,'Metallic':m,'Roughness':r,'Emission':[0,0,0,1],'Emission Strength':0}
materials={}
norm=lambda s:re.sub('[^a-z0-9]','',s.lower())
for name,v in values.items():
    safe='M_'+re.sub('[^A-Za-z0-9_]','_',name);path=BASE+'/Materials/'+safe
    mat=unreal.load_asset(path)
    if not mat:
        mat=tools.create_asset(safe,BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
        for index,(key,prop,vector) in enumerate([('Base Color',unreal.MaterialProperty.MP_BASE_COLOR,True),('Metallic',unreal.MaterialProperty.MP_METALLIC,False),('Roughness',unreal.MaterialProperty.MP_ROUGHNESS,False),('Emission',unreal.MaterialProperty.MP_EMISSIVE_COLOR,True)]):
            node=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector if vector else unreal.MaterialExpressionConstant,-300,index*120)
            value=v[key]
            if vector:
                strength=v['Emission Strength'] if key=='Emission' else 1
                node.set_editor_property('constant',unreal.LinearColor(value[0]*strength,value[1]*strength,value[2]*strength,1))
            else:node.set_editor_property('r',value)
            unreal.MaterialEditingLibrary.connect_material_property(node,'',prop)
        unreal.MaterialEditingLibrary.recompile_material(mat);assets.save_loaded_asset(mat)
    materials[norm(name)]=mat
meshes={};imports=[]
for row,folder in [(r,SOURCE) for r in meta['modules']]+[(r,SOURCE/'ColumnProtection') for r in guard['modules']]:
    name=row['name'];path=BASE+'/Meshes/'+name;mesh=unreal.load_asset(path)
    if not mesh:
        ui=unreal.FbxImportUI()
        for k,v in dict(import_mesh=True,import_as_skeletal=False,import_materials=False,import_textures=False,import_animations=False,automated_import_should_detect_type=False,mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():ui.set_editor_property(k,v)
        for k,v in dict(combine_meshes=True,auto_generate_collision=False,transform_vertex_to_absolute=True,convert_scene=True,convert_scene_unit=True,import_rotation=unreal.Rotator()).items():ui.static_mesh_import_data.set_editor_property(k,v)
        task=unreal.AssetImportTask()
        for k,v in dict(filename=str(folder/row['fbx']),destination_path=BASE+'/Meshes',destination_name=name,automated=True,replace_existing=False,save=True,options=ui,factory=unreal.FbxFactory()).items():task.set_editor_property(k,v)
        tools.import_asset_tasks([task]);mesh=unreal.load_asset(path)
    assert isinstance(mesh,unreal.StaticMesh),name
    b=mesh.get_bounds();expected=row['dimensions_cm'];actual=[b.box_extent.x*2,b.box_extent.y*2,b.box_extent.z*2]
    assert max(abs(a-b) for a,b in zip(actual,expected))<.15,(name,actual,expected)
    for i,slot in enumerate(mesh.static_materials):
        material=materials.get(norm(str(slot.material_slot_name)));assert material,(name,str(slot.material_slot_name));mesh.set_material(i,material)
    assets.save_loaded_asset(mesh);meshes[name.removeprefix('SM_WS_')]=mesh;imports.append(name)
existing=actors.get_all_level_actors();before={a.get_actor_label():a.get_actor_transform() for a in existing if not a.get_actor_label().startswith(PREFIX)}
for a in existing:
    if a.get_actor_label().startswith(PREFIX):actors.destroy_actor(a)
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
roof_c,roof_e=named['WH_Roof'].get_actor_bounds(False);roof_z=roof_c.z-roof_e.z
elevator=named['MZ_AGVElevator'];ec,ee=elevator.get_actor_bounds(False)
created=[];counts={};skipped=[]
def place(key,pos,yaw=0,scale=(1,1,1),floor=0,roof=False,pitch=0,detail=False):
    label=PREFIX+key+'_'+str(counts.get(key,0));a=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*pos),unreal.Rotator(pitch=pitch,yaw=yaw));a.set_actor_label(label)
    c=a.static_mesh_component;c.set_static_mesh(meshes[key]);c.set_collision_profile_name('NoCollision');a.set_actor_scale3d(unreal.Vector(*scale))
    c.set_editor_property('mobility',unreal.ComponentMobility.STATIC)
    c.set_editor_property('cast_shadow',not detail);c.set_editor_property('ld_max_draw_distance',7000 if detail else 15000)
    a.set_editor_property('is_spatially_loaded',False);a.set_folder_path('Warehouse/Services/'+('Roof' if roof else 'Floor'+str(floor+1)))
    a.tags=[unreal.Name('WarehouseServices'),unreal.Name('LayoutFixed'),unreal.Name('WarehouseFloor'+str(floor))]+([unreal.Name('ObserverRoof')] if roof else [])
    center,extent=a.get_actor_bounds(False)
    # Reserve the actual shaft envelope, with additional clearance around its doors.
    if abs(center.x-ec.x)<extent.x+ee.x+30 and abs(center.y-ec.y)<extent.y+ee.y+30 and center.z-extent.z<ec.z+ee.z+50:
        skipped.append(label);actors.destroy_actor(a);return None
    counts[key]=counts.get(key,0)+1;created.append(a);return a
# Five visual secondary girders, plus local connection plates; existing framing stays intact.
for y in [-1800,-1000,0,1000,1800]:
    place('IBeam_4m',(-1850,y,1170),scale=(11.25,1,.65),roof=True)
    for x in [-1850,2650]:place('BeamConnection',(x,y,1170),roof=True,detail=True)
# Roof longitudinal trunk trays and cable bundles.
for x in [-1350,0,2150]:
    for y in range(-1800,1800,400):
        place('TrayStraight_2m',(x,y,1118),yaw=90,scale=(2,1,1),roof=True)
        place('CableBundle_2m',(x,y,1118),yaw=90,scale=(2,1,1),roof=True,detail=True)
        place('TrayHanger',(x,y+200,1115),yaw=90,roof=True,detail=True)
    place('TrayEndCap',(x,-1800,1118),yaw=90,roof=True)
# Transverse fire mains and downward heads. These are visual representations only.
for y in [-1200,0,1200]:
    for x in range(-1800,2600,400):place('FirePipeStraight_2m',(x,y,1092),scale=(2,1,1),roof=True)
    for x in [-1600,-800,0,800,1600,2400]:
        place('SprinklerHead',(x,y,1090),roof=True,detail=True)
        place('PipeHanger',(x+80,y,1092),roof=True,scale=(1,1,1.4),detail=True)
# Perimeter ventilation runs, kept away from the central atrium and lift.
for x in [-1840,2590]:
    for y in range(-1650,1350,300):place('DuctStraight_1500',(x,y,1138),yaw=90,scale=(2,1,1),roof=True)
    place('DuctElbow90',(x,1350,1138),yaw=90,roof=True)
    place('DuctGrille',(x-45,1395,1138),yaw=90,roof=True)
# Under-deck services are grouped with the deck above for observer-mode hiding.
for f in [1,2]:
    z=f*400-65
    for x in [-1450,1450]:
        for y in range(-1650,1150,400):
            place('TrayStraight_2m',(x,y,z),yaw=90,scale=(2,.75,.75),floor=f)
            place('CableBundle_2m',(x,y,z),yaw=90,scale=(2,.75,.75),floor=f,detail=True)
            place('TrayHanger',(x,y+160,z-2),yaw=90,scale=(1,.75,1),floor=f,detail=True)
        for y in [-1300,-500,300,1100]:
            place('LEDLinear_1200',(x-180,y,z-24),floor=f)
            place('FirePipeStraight_2m',(x-300,y-100,z-5),yaw=90,scale=(2,.65,.65),floor=f)
            place('SprinklerHead',(x-300,y+60,z-6),floor=f,detail=True)
    for x in [-1100,0,1100,2200]:
        for y in [-1350,-400,600,1400]:place('LEDLinear_1200',(x,y,1074),roof=True)
# Wall cabinet groups on the new east-side service aisle and north/west walls.
for x,y,yaw in [(2728,-800,-90),(2728,-300,-90),(-1910,100,90),(-1910,1000,90),(1700,1925,0)]:
    place('DistributionCabinet',(x,y,140),yaw=yaw)
    # Local hinge offset transformed with the same wall orientation.
    a=math.radians(yaw);dx=-28*math.cos(a);dy=-28*math.sin(a)
    place('DistributionDoor',(x+dx,y+dy,140),yaw=yaw)
    for u in [-18,0,18]:
        xx=x+u*math.cos(a);yy=y+u*math.sin(a)
        for z in [185,285]:place('ConduitStraight_1m',(xx,yy,z),yaw=yaw,pitch=90,detail=True)
    place('JunctionBox',(x+55*math.cos(a),y+55*math.sin(a),190),yaw=yaw)
# Column guards fit existing rectangular column dimensions, without altering column collision.
for name,a in named.items():
    if 'Column_' not in name:continue
    c,e=a.get_actor_bounds(False)
    if c.z-e.z>10 or e.x>30 or e.y>30:continue
    # Skip the west stair envelope and boundaries where a wrap would enter the exterior wall.
    if c.x<-1650 and -1320<c.y<-300:continue
    if name.startswith('WH_Column_'):continue
    place('RubberColumnWrap_450',(c.x,c.y,0),scale=((2*e.x+.6)/45,(2*e.y+.6)/45,1))
# All original actors keep their authored pose; no cargo/AGV/elevator is moved.
after={a.get_actor_label():a for a in actors.get_all_level_actors()}
for label,transform in before.items():assert label in after and unreal.MathLibrary.nearly_equal_transform_transform(after[label].get_actor_transform(),transform,.001,.001,.001),label
assert created
assets.save_directory(BASE,False,True);assert level.save_current_level()
report={'date':'2026-10-14','module_assets':len(imports),'placed_actors':len(created),'counts':counts,'shaft_exclusions':len(skipped),'original_actor_transforms_preserved':True,'decorative_collision':'NoCollision','runtime_and_performance_tested':False,'map_saved':True}
(OUT/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print('WAREHOUSE_SERVICES_APPLIED',json.dumps(report))
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
views=[('Atrium',(-80,-1050,220),(250,1000,780),95),('UpperGallery',(-950,-1380,960),(900,900,1000),90),('Overview',(4200,-4500,3800),(400,200,500),75)]
camera=actors.spawn_actor_from_class(unreal.CameraActor,unreal.Vector());camera.set_actor_label('TEMP_ServicesCapture')
started=time.monotonic();phase=0;at=started;hidden=[]
def tick(dt):
    global phase,at
    try:
        if time.monotonic()-started<30:return
        if time.monotonic()-at<8:return
        if phase>=len(views):
            for a in hidden:a.set_is_temporarily_hidden_in_editor(False)
            actors.destroy_actor(camera);unreal.unregister_slate_post_tick_callback(handle);print('WAREHOUSE_SERVICES_CAPTURE_DONE');unreal.SystemLibrary.quit_editor();return
        name,eye,target,fov=views[phase]
        if name=='Overview':
            for label,a in named.items():
                if label=='WH_Roof' or label.startswith('WH_Wall_S_') or label=='WH_Wall_E':a.set_is_temporarily_hidden_in_editor(True);hidden.append(a)
        eye=unreal.Vector(*eye);target=unreal.Vector(*target);rot=unreal.MathLibrary.find_look_at_rotation(eye,target)
        camera.set_actor_location(eye,False,True);camera.set_actor_rotation(rot,False);camera.camera_component.set_field_of_view(fov)
        editor.set_level_viewport_camera_info(eye,rot)
        unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,str(OUT/(name+'.png')),camera=camera,delay=1)
        phase+=1;at=time.monotonic()
    except Exception:
        (OUT/'CaptureError.txt').write_text(traceback.format_exc(),encoding='utf-8');unreal.unregister_slate_post_tick_callback(handle)
handle=unreal.register_slate_post_tick_callback(tick)
