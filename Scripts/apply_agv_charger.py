"""Import concept charger, replace existing station component meshes, save and capture."""
import unreal,json,re,time,traceback
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/Testing/ChargerModel';OUT.mkdir(parents=True,exist_ok=True)
BASE='/Game/Warehouse/ChargingStation'
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert editor.get_game_world() is None
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
tools=unreal.AssetToolsHelpers.get_asset_tools();assets=unreal.EditorAssetLibrary
manifest=json.loads((ROOT/'Art/Equipment/AGVCharger/assembly_manifest.json').read_text(encoding='utf-8'))
materials={}
for name,values in manifest['materials'].items():
    safe='M_'+re.sub('[^A-Za-z0-9_]','_',name);path=BASE+'/Materials/'+safe
    m=unreal.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(safe,BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(m)
    for index,(key,prop,vector) in enumerate([('Base Color',unreal.MaterialProperty.MP_BASE_COLOR,True),('Metallic',unreal.MaterialProperty.MP_METALLIC,False),('Roughness',unreal.MaterialProperty.MP_ROUGHNESS,False),('Emission',unreal.MaterialProperty.MP_EMISSIVE_COLOR,True)]):
        n=unreal.MaterialEditingLibrary.create_material_expression(m,unreal.MaterialExpressionConstant3Vector if vector else unreal.MaterialExpressionConstant,-300,index*140)
        value=values[key]
        if vector:
            strength=values['Emission Strength'] if key=='Emission' else 1
            n.set_editor_property('constant',unreal.LinearColor(value[0]*strength,value[1]*strength,value[2]*strength,1))
        else:n.set_editor_property('r',value)
        unreal.MaterialEditingLibrary.connect_material_property(n,'',prop)
    unreal.MaterialEditingLibrary.recompile_material(m);assets.save_loaded_asset(m);materials[name]=m
meshes={}
for suffix in ['Cabinet','Contact','Status']:
    name='SM_Charger_Game'+suffix;task=unreal.AssetImportTask();options=unreal.FbxImportUI()
    for k,v in dict(import_mesh=True,import_as_skeletal=False,import_materials=False,import_textures=False,import_animations=False,automated_import_should_detect_type=False,mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():options.set_editor_property(k,v)
    for k,v in dict(combine_meshes=True,auto_generate_collision=True,transform_vertex_to_absolute=True,convert_scene=True,convert_scene_unit=True,import_rotation=unreal.Rotator()).items():options.static_mesh_import_data.set_editor_property(k,v)
    final_name=name+'_Final'
    for k,v in dict(filename=str(ROOT/'Art/Equipment/AGVCharger'/(name+'.fbx')),destination_path=BASE+'/Meshes',destination_name=final_name,automated=True,replace_existing=True,save=True,options=options,factory=unreal.FbxFactory()).items():task.set_editor_property(k,v)
    # Reuse validated imports: UE reimport can retain prior axis/unit settings.
    mesh=unreal.load_asset(BASE+'/Meshes/'+final_name)
    if not mesh:
        tools.import_asset_tasks([task]);mesh=unreal.load_asset(BASE+'/Meshes/'+final_name)
    assert isinstance(mesh,unreal.StaticMesh)
    bounds=mesh.get_bounds()
    assert bounds.box_extent.z>20,('Unexpected FBX unit scale',suffix,bounds)
    if suffix=='Cabinet':assert abs(bounds.origin.x+26.75)<.1 and abs(bounds.box_extent.z-80.5)<.1,('Unexpected FBX orientation or size',bounds)
    for i,slot in enumerate(mesh.static_materials):
        slotname=str(slot.material_slot_name)
        normalized=lambda x:re.sub('[^a-z0-9]','',x.lower())
        match=next((m for n,m in materials.items() if normalized(n)==normalized(slotname)),None)
        assert match,('Unknown slot',slotname,list(materials));mesh.set_material(i,match)
    assets.save_loaded_asset(mesh);meshes[suffix]=mesh
    print('CHARGER_MESH',suffix,mesh.get_bounds())
stations=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseChargingStation)];assert stations
records=[];offset=manifest['suggested_location_relative_to_existing_DockPose_cm']
for station in stations:
    station.modify();before=station.get_actor_transform();vehicle=station.get_editor_property('assigned_vehicle')
    components={c.get_name():c for c in station.get_components_by_class(unreal.StaticMeshComponent)}
    for name,suffix in [('Cabinet','Cabinet'),('ContactPlate','Contact'),('StatusLight','Status')]:
        c=components[name];c.modify();c.set_static_mesh(meshes[suffix]);c.set_relative_location(unreal.Vector(*offset),False,True);c.set_relative_rotation(unreal.Rotator(),False,True);c.set_relative_scale3d(unreal.Vector(1,1,1))
        for i,slot in enumerate(meshes[suffix].static_materials):c.set_material(i,slot.material_interface)
        c.set_collision_profile_name('BlockAllDynamic' if name=='Cabinet' else 'NoCollision')
    for display in station.get_components_by_class(unreal.TextRenderComponent):
        display.modify();display.set_visibility(False);display.set_hidden_in_game(True)
    assert unreal.MathLibrary.nearly_equal_transform_transform(station.get_actor_transform(),before,.001,.001,.001)
    records.append({'station':station.get_actor_label(),'assigned_vehicle':vehicle.get_actor_label() if vehicle else None,'docked':station.is_docked(vehicle) if vehicle else None})
assert level.save_current_level();assets.save_directory(BASE,False,True)
report={'date':'2026-10-14','stations':records,'map_saved':True,'runtime_charge_tested':False}
(OUT/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('CHARGER_MAP_APPLIED',json.dumps(report))
# An unsaved camera is solely for the editor screenshot.
station=stations[0];origin=station.get_actor_location();fwd=station.get_actor_forward_vector();right=station.get_actor_right_vector()
target=origin+fwd*5+unreal.Vector(0,0,95)
eye=origin+fwd*80-right*360+unreal.Vector(0,0,195)
camera=actors.spawn_actor_from_class(unreal.CameraActor,eye,unreal.MathLibrary.find_look_at_rotation(eye,target));camera.set_actor_label('TEMP_ChargerCapture')
camera.camera_component.set_field_of_view(60)
editor.set_level_viewport_camera_info(eye,unreal.MathLibrary.find_look_at_rotation(eye,target))
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
started=time.monotonic();phase=0
def tick(dt):
    global phase
    try:
        if time.monotonic()-started<30:return
        if phase==0:
            unreal.AutomationLibrary.take_high_res_screenshot(1600,1100,str(OUT/'ChargerInWarehouse.png'),camera=camera,delay=1.0)
            phase=1;return
        if time.monotonic()-started<42:return
        print('CHARGER_CAPTURE_COMPLETE',str(OUT/'ChargerInWarehouse.png'))
        actors.destroy_actor(camera);unreal.unregister_slate_post_tick_callback(handle)
    except Exception:
        (OUT/'CaptureError.txt').write_text(traceback.format_exc(),encoding='utf-8');unreal.unregister_slate_post_tick_callback(handle)
handle=unreal.register_slate_post_tick_callback(tick)
