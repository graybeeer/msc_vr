"""Offline 512px PNG renders of real project models, imported as lightweight Slate UI textures."""
import json,math,time,traceback
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Art/UI/LayoutIcons';OUT.mkdir(parents=True,exist_ok=True)
REPORT=ROOT/'Saved/Testing/WarehousePlay';REPORT.mkdir(parents=True,exist_ok=True)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert editor.get_game_world() is None
assert '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower(),'Model icons require a real renderer'
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
catalog={};contexts=[]

def snapshot(key,items):
    parts=[]
    for a in items:
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            if c.static_mesh and c.is_visible():
                pose=c.get_world_transform()
                if key=='Charger':pose=unreal.MathLibrary.make_relative_transform(pose,a.get_actor_transform())
                parts.append((c.static_mesh,pose,list(c.get_editor_property('override_materials'))))
    assert parts,key
    catalog[key]=parts

snapshot('Rack',[named['WH_InboundRack_00_'+s] for s in list('ABCDEFGH')+['Deck_20','Deck_140']])
snapshot('Forklift',[named['WH_AutonomousForklift']])
snapshot('Pallet',[next(a for a in named.values() if isinstance(a,unreal.WarehousePallet))])
snapshot('Cargo',[next(a for a in named.values() if isinstance(a,unreal.WarehouseCargo) and 'Intact' in a.get_component_by_class(unreal.StaticMeshComponent).static_mesh.get_name())])
snapshot('Charger',[next(a for a in named.values() if isinstance(a,unreal.WarehouseChargingStation))])
snapshot('Computer',[next(a for a in named.values() if isinstance(a,unreal.WarehouseComputer))])
snapshot('Elevator',[next(a for a in named.values() if isinstance(a,unreal.WarehouseElevator))])
snapshot('PalletLift',[next(a for a in named.values() if isinstance(a,unreal.WarehousePalletLift))])
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
paint=named['WH_TrainingLine_650'].static_mesh_component.get_material(0)
outbound_paint=unreal.load_asset('/Game/Warehouse/AGV/Materials/M_Sensor_status_green')
floor_mat=named['Floor'].static_mesh_component.get_material(0)
# Keep the current default character mesh as the people-category representative.
worker=next(a for a in named.values() if isinstance(a,unreal.WarehouseWorker))
sk=next(c for c in worker.get_components_by_class(unreal.SkeletalMeshComponent) if c.get_editor_property('skeletal_mesh_asset'))
worker_mesh=sk.get_editor_property('skeletal_mesh_asset')
worker_mats=list(sk.get_editor_property('override_materials'))
assert unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
world=editor.get_editor_world()

def spawn(cls,pos=(0,0,0),rot=None):
    return actors.spawn_actor_from_class(cls,unreal.Vector(*pos),rot or unreal.Rotator())
for kind,key in [(unreal.WarehouseConveyorKind.PALLET_ROLLER,'ConveyorRoller'),(unreal.WarehouseConveyorKind.CARTON_BELT,'ConveyorBelt'),(unreal.WarehouseConveyorKind.ROTARY_TRANSFER,'ConveyorTurn')]:
    a=spawn(unreal.WarehouseConveyor);a.set_editor_property('kind',kind)
    if key=='ConveyorBelt':a.set_editor_property('width_cm',80)
    if key=='ConveyorTurn':a.set_editor_property('length_cm',200);a.set_editor_property('width_cm',200)
    assert a.rebuild_conveyor();snapshot(key,[a]);actors.destroy_actor(a)
bay=spawn(unreal.WarehouseWaitingBay);snapshot('WaitingBay',[bay]);actors.destroy_actor(bay)

# Reusable neutral studio backdrop and stable exposure. No AI-generated substitutes.
assets=unreal.EditorAssetLibrary;tools=unreal.AssetToolsHelpers.get_asset_tools()
mat_path='/Game/Warehouse/UI/Icons/M_IconBackdrop'
backdrop=unreal.load_asset(mat_path)
if not backdrop:
    backdrop=tools.create_asset('M_IconBackdrop','/Game/Warehouse/UI/Icons',unreal.Material,unreal.MaterialFactoryNew())
    color=unreal.MaterialEditingLibrary.create_material_expression(backdrop,unreal.MaterialExpressionConstant3Vector,-250,0)
    color.set_editor_property('constant',unreal.LinearColor(.055,.075,.095,1))
    unreal.MaterialEditingLibrary.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
    rough=unreal.MaterialEditingLibrary.create_material_expression(backdrop,unreal.MaterialExpressionConstant,-250,150);rough.set_editor_property('r',.85)
    unreal.MaterialEditingLibrary.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(backdrop);assets.save_loaded_asset(backdrop)
floor=spawn(unreal.StaticMeshActor,(0,0,-3));floor.static_mesh_component.set_static_mesh(cube);floor.static_mesh_component.set_material(0,backdrop);floor.set_actor_scale3d(unreal.Vector(1000,1000,.05))
for yaw,pitch,intensity in [(-35,-50,5),(145,-30,2.5),(240,-60,1.5)]:
    light=spawn(unreal.DirectionalLight,rot=unreal.Rotator(pitch=pitch,yaw=yaw));light.light_component.set_intensity(intensity);light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
camera=spawn(unreal.CameraActor);component=camera.camera_component
component.set_editor_property('projection_mode',unreal.CameraProjectionMode.ORTHOGRAPHIC)
component.set_editor_property('aspect_ratio',1.0)
component.set_editor_property('constrain_aspect_ratio',True)
settings=component.get_editor_property('post_process_settings')
for k,v in dict(override_auto_exposure_min_brightness=True,override_auto_exposure_max_brightness=True,auto_exposure_min_brightness=1.0,auto_exposure_max_brightness=1.0,
                override_auto_exposure_bias=True,auto_exposure_bias=0.0,override_vignette_intensity=True,vignette_intensity=0.0,
                override_bloom_intensity=True,bloom_intensity=.15).items():settings.set_editor_property(k,v)
component.set_editor_property('post_process_settings',settings);component.set_editor_property('post_process_blend_weight',1.0)
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
unreal.SystemLibrary.execute_console_command(world,'r.ScreenPercentage 100')
keys=['Rack','Forklift','Cargo','Pallet','Charger','WaitingBay','Elevator','PalletLift','ConveyorRoller','ConveyorBelt','ConveyorTurn','Computer','Worker','ZoneInbound','ZoneOutbound','All']
selection=REPORT/'RenderOnly.json'
if selection.exists():
    requested=json.loads(selection.read_text(encoding='utf-8-sig'))
    keys=[key for key in keys if key in requested]
current=[];index=0;phase='setup';at=time.monotonic();started=at;render_task=None;results=[]

def clone(key,offset=(0,0,0),scale=1):
    group=[]
    for mesh,pose,mats in catalog[key]:
        a=spawn(unreal.StaticMeshActor);a.static_mesh_component.set_static_mesh(mesh);a.static_mesh_component.set_editor_property('override_materials',mats)
        a.set_actor_transform(pose,False,True);group.append(a)
    low=[min(getattr(a.get_actor_bounds(False)[0]-a.get_actor_bounds(False)[1],k) for a in group) for k in 'xyz']
    high=[max(getattr(a.get_actor_bounds(False)[0]+a.get_actor_bounds(False)[1],k) for a in group) for k in 'xyz']
    origin=unreal.Vector((low[0]+high[0])/2,(low[1]+high[1])/2,low[2])
    for a in group:
        a.set_actor_location((a.get_actor_location()-origin)*scale+unreal.Vector(*offset),False,True)
        a.set_actor_scale3d(a.get_actor_scale3d()*scale)
    current.extend(group)

def finish(ok,error=''):
    (REPORT/'Icons.json').write_text(json.dumps(dict(passed=ok,error=error,icons=results,wall_seconds=time.monotonic()-started),ensure_ascii=False,indent=2),encoding='utf-8')
    print('WAREHOUSE_ICONS_'+('PASSED' if ok else 'FAILED'),error)
    unreal.unregister_slate_post_tick_callback(handle)
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():unreal.SystemLibrary.quit_editor()

def run_tick(dt):
    global index,phase,at,render_task
    try:
        assert time.monotonic()-started<900,'Icon render timeout'
        if phase=='setup':
            for a in current:actors.destroy_actor(a)
            current.clear()
            if index>=len(keys):finish(True);return
            key=keys[index]
            if key in catalog:clone(key)
            elif key=='Worker':
                a=spawn(unreal.SkeletalMeshActor);a.skeletal_mesh_component.set_skeletal_mesh_asset(worker_mesh);a.skeletal_mesh_component.set_editor_property('override_materials',worker_mats);current.append(a)
            elif key=='All':
                clone('Rack',(-160,120,0),.65);clone('Forklift',(90,-20,0),.65);clone('Cargo',(-60,-120,0),1.6)
            else:
                clone('Pallet');clone('Cargo',(0,0,15),1.4)
                for pos,size in [((-85,0,.2),(5,170,.4)),((85,0,.2),(5,170,.4)),((0,-85,.2),(170,5,.4)),((0,85,.2),(170,5,.4))]:
                    a=spawn(unreal.StaticMeshActor,pos);a.static_mesh_component.set_static_mesh(cube);a.static_mesh_component.set_material(0,outbound_paint if key=='ZoneOutbound' else paint);a.set_actor_scale3d(unreal.Vector(*(v/100 for v in size)));current.append(a)
            low=[min(getattr(a.get_actor_bounds(False)[0]-a.get_actor_bounds(False)[1],k) for a in current) for k in 'xyz']
            high=[max(getattr(a.get_actor_bounds(False)[0]+a.get_actor_bounds(False)[1],k) for a in current) for k in 'xyz']
            center=unreal.Vector(*[(a+b)/2 for a,b in zip(low,high)]);radius=math.sqrt(sum((b-a)**2 for a,b in zip(low,high)))/2
            eye=center+unreal.Vector(1.8 if key=='Charger' else -1.4,-1.4 if key=='Charger' else -1.8,1.05 if key=='Charger' else 1.25)*max(500,radius*3)
            rotation=unreal.MathLibrary.find_look_at_rotation(eye,center)
            camera.set_actor_location(eye,False,True);camera.set_actor_rotation(rotation,False)
            component.set_editor_property('ortho_width',max(50,radius*2.3))
            editor.set_level_viewport_camera_info(eye,rotation)
            at=time.monotonic();phase='warmup';return
        if phase=='warmup':
            if time.monotonic()-at<5:return
            (OUT/(keys[index]+'.png')).unlink(missing_ok=True)
            render_task=unreal.AutomationLibrary.take_high_res_screenshot(512,512,str(OUT/(keys[index]+'.png')),camera=camera,delay=1)
            at=time.monotonic();phase='capture';return
        if phase=='capture':
            file=OUT/(keys[index]+'.png')
            if time.monotonic()-at<3 or not file.exists():return
            task=unreal.AssetImportTask()
            for k,v in dict(filename=str(file),destination_path='/Game/Warehouse/UI/Icons',destination_name='T_Icon_'+keys[index],automated=True,replace_existing=True,save=True).items():task.set_editor_property(k,v)
            tools.import_asset_tasks([task])
            texture=unreal.load_asset('/Game/Warehouse/UI/Icons/T_Icon_'+keys[index]);assert isinstance(texture,unreal.Texture2D)
            texture.set_editor_property('lod_group',unreal.TextureGroup.TEXTUREGROUP_UI)
            texture.set_editor_property('mip_gen_settings',unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            texture.set_editor_property('never_stream',True);assets.save_loaded_asset(texture)
            results.append(dict(key=keys[index],png=str(file.relative_to(ROOT)),texture=texture.get_path_name(),resolution=512,model_parts=len(current)))
            print('ICON_RENDERED',keys[index]);index+=1;phase='setup'
    except Exception:finish(False,traceback.format_exc())

busy=False
def tick(dt):
    global busy
    if busy:return
    busy=True
    try:run_tick(dt)
    finally:busy=False
handle=unreal.register_slate_post_tick_callback(tick)
