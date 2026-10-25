"""Build three fixed prototype warehouses from existing project assets. Never saves the administrator map."""
import hashlib, json, math, sys, traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0,str(ROOT/'Scripts'))
from apply_real_world_scale import fit
LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
OUT=ROOT/'Saved/Testing/WarehousePlay';OUT.mkdir(parents=True,exist_ok=True)
BASE='/Game/Warehouse/Play/Maps'
FIXED=['PlayFixed','LayoutFixed','WarehouseMapStructure','WarehouseFloor0']

def spawn(cls,label,pos=(0,0,0),yaw=0,tags=()):
    a=ACTORS.spawn_actor_from_class(cls,unreal.Vector(*pos),unreal.Rotator(yaw=yaw))
    a.set_actor_label(label);a.set_editor_property('is_spatially_loaded',False)
    a.tags=[unreal.Name(t) for t in tags];a.set_folder_path('Play/'+label.split('_')[0])
    return a

def mesh_actor(label,mesh,pos,rotation=None,scale=None,materials=None,tags=FIXED):
    a=spawn(unreal.StaticMeshActor,label,pos,tags=tags);c=a.static_mesh_component
    c.set_static_mesh(mesh);c.set_collision_profile_name('BlockAll')
    if rotation:a.set_actor_rotation(rotation,False)
    if scale:a.set_actor_scale3d(scale)
    if materials:c.set_editor_property('override_materials',materials)
    return a

def blank():
    assert unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    world=EDITOR.get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode',unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
    return world

def build():
    assert EDITOR.get_game_world() is None
    originals=[ROOT/'Content/FirstPerson/Lvl_FirstPerson.umap']+list((ROOT/'Content/__ExternalActors__/FirstPerson/Lvl_FirstPerson').rglob('*.uasset'))
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in originals}
    assert LEVEL.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    named={a.get_actor_label():a for a in ACTORS.get_all_level_actors()}
    rig={c.get_name():(c.get_relative_transform(),c.static_mesh,list(c.get_editor_property('override_materials')))
         for c in named['WH_AutonomousForklift'].get_components_by_class(unreal.StaticMeshComponent)}
    rack_names=['WH_InboundRack_00_'+suffix for suffix in list('ABCDEFGH')+['Deck_20','Deck_140']]
    assert all(n in named for n in rack_names),rack_names
    center,_=named['WH_InboundRack_00_Deck_20'].get_actor_bounds(False)
    anchor=unreal.Vector(center.x,center.y,0)
    rack_parts=[(named[n].get_actor_location()-anchor,named[n].get_actor_rotation(),named[n].get_actor_scale3d(),
                 named[n].static_mesh_component.static_mesh,list(named[n].static_mesh_component.get_editor_property('override_materials'))) for n in rack_names]
    floor_mat=named['Floor'].static_mesh_component.get_material(0)
    wall_mat=named['WH_Wall_E'].static_mesh_component.get_material(0)
    paint_mat=named['WH_TrainingLine_650'].static_mesh_component.get_material(0)
    cube=unreal.load_asset('/Engine/BasicShapes/Cube')
    metal=unreal.load_asset('/Game/Warehouse/Services/Materials/M_WS_Galvanized_steel')
    cable=unreal.load_asset('/Game/Warehouse/Services/Materials/M_WS_Cable_black')
    carton=unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2')
    assert carton and floor_mat and wall_mat and metal and cable
    service={k:unreal.load_asset('/Game/Warehouse/Services/Meshes/SM_WS_'+k) for k in
             ['IBeam_4m','TrayStraight_2m','CableBundle_2m','FirePipeStraight_2m','SprinklerHead',
              'DuctStraight_1500','DuctElbow90','DuctGrille','LEDLinear_1200','DistributionCabinet',
              'DistributionDoor','JunctionBox','ConduitStraight_1m','RubberColumnWrap_450']}
    assert all(service.values())
    # Existing service meshes already contain the shared solid collision policy.
    mesh_editor=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for key,m in service.items():
        assert mesh_editor.get_collision_complexity(m)==unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE,key

    def block(label,pos,size,mat,tags=FIXED):
        return mesh_actor(label,cube,pos,scale=unreal.Vector(*(v/100 for v in size)),materials=[mat],tags=tags)

    world=blank()
    spawn(unreal.WarehousePlayStage,'MainMenu_Rules').set_editor_property('stage_index',0)
    block('Menu_Floor',(0,0,-10),(1000,1000,20),floor_mat)
    spawn(unreal.PlayerStart,'Menu_Start',(0,0,100))
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,BASE+'/MainMenu')
    reports=[]
    for index,hx,hy,ceiling,budget,goal in [(1,1800,1400,600,20000000,2),(2,2200,1800,650,65000000,3),(3,2600,2100,700,85000000,4)]:
        world=blank();created=[];mounts=[]
        block('Building_Floor',(0,0,-15),(hx*2+60,hy*2+60,30),floor_mat,FIXED+['ObserverArea'])
        roof=block('Building_Roof',(0,0,ceiling+15),(hx*2+60,hy*2+60,30),wall_mat,FIXED+['ObserverRoof'])
        for side,x,y,w,d in [('W',-hx-15,0,30,hy*2),('E',hx+15,0,30,hy*2),('N',0,hy+15,hx*2+60,30),('S',0,-hy-15,hx*2+60,30)]:
            block('Building_Wall_'+side,(x,y,ceiling/2),(w,d,ceiling),wall_mat,FIXED+['ObserverFadeWall'])
        if index>=2:
            for y in (-hy+350,hy-350):
                block('Building_Column_'+str(y),(150,y,ceiling/2),(35,35,ceiling),metal)
                mesh_actor('Services_ColumnGuard_'+str(y),service['RubberColumnWrap_450'],(150,y,0),scale=unreal.Vector(.8,.8,1))
        if index==3:
            block('Building_ZoneDivider',(400,1500,125),(30,400,250),wall_mat)

        def part(key,pos,yaw=0,scale=(1,1,1),pitch=0,roof=True):
            a=mesh_actor('Services_'+key+'_'+str(len(created)),service[key],pos,
                         unreal.Rotator(pitch=pitch,yaw=yaw),unreal.Vector(*scale),tags=FIXED+['WarehouseServices']+(['ObserverRoof'] if roof else []))
            created.append(a);return a
        def anchor_rod(x,y,z):
            assert 0<z<ceiling
            a=block('Services_RoofSupport_'+str(len(created)),(x,y,(z+ceiling)/2),(1.2,1.2,ceiling-z),metal,FIXED+['WarehouseServices','ObserverRoof'])
            created.append(a);mounts.append({'kind':'ceiling','x':x,'y':y,'bottom_z':z,'top_z':ceiling})
        # Main sprinkler runs, paired electrical trays and fixtures, all sized to this roof.
        for y in (-hy*.58,0,hy*.58):
            part('IBeam_4m',(-hx,y,ceiling-12),scale=(hx*2/400,1,.55))
        for y in (-hy*.5,hy*.5):
            part('FirePipeStraight_2m',(-hx+35,y,ceiling-80),scale=((hx*2-70)/200,1,1))
            part('TrayStraight_2m',(-hx+35,y+105,ceiling-55),scale=((hx*2-70)/200,1,1))
            part('CableBundle_2m',(-hx+35,y+105,ceiling-55),scale=((hx*2-70)/200,1,1))
            for x in range(-hx+180,hx-100,500):
                anchor_rod(x,y,ceiling-76)
                for offset in (-22,22):anchor_rod(x,y+105+offset,ceiling-51)
                part('SprinklerHead',(x,y,ceiling-84))
        for x in range(-hx+350,hx-200,700):
            for y in (-hy*.58,0,hy*.58):
                part('LEDLinear_1200',(x-60,y,ceiling-35))
                for dx in (-54,54):anchor_rod(x+dx,y,ceiling-30.5)
                light=spawn(unreal.PointLight,'Lighting_Work_'+str(x)+'_'+str(y),(x,y,ceiling-50),tags=['LayoutFixed','PlayFixed','WarehouseFloor0'])
                light.light_component.set_editor_property('intensity',16000)
                light.light_component.set_editor_property('attenuation_radius',1400)
                light.light_component.set_editor_property('cast_shadows',False)
                light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        # Duct beside the north wall; the grille ends before either side wall.
        part('DuctStraight_1500',(-hx+120,hy-90,ceiling-48),scale=((hx*2-240)/150,1,1))
        for x in range(-hx+250,hx-100,700):anchor_rod(x,hy-90,ceiling-35)
        part('DuctGrille',(hx-120,hy-90,ceiling-48),yaw=90)
        # Cabinet backs touch the north wall; matching conduits continue to the ceiling trunk.
        for x in (-hx+450,hx-450):
            m=service['DistributionCabinet'];b=m.get_bounds();back=b.origin.y+b.box_extent.y
            y=hy-back+.1
            part('DistributionCabinet',(x,y,145),roof=False)
            # Fit the imported door's rear face to the cabinet's front face.
            db=service['DistributionDoor'].get_bounds()
            front=y+b.origin.y-b.box_extent.y
            part('DistributionDoor',(x-28,front-db.origin.y-db.box_extent.y+.1,145),roof=False)
            part('JunctionBox',(x+65,hy-5,220),roof=False)
            for dx in (-18,0,18):
                part('ConduitStraight_1m',(x+dx,hy-2,190),pitch=90,scale=((ceiling-245)/100,1,1),roof=False)
            mounts.append({'kind':'wall','x':x,'wall_y':hy,'cabinet_back_y':y+back})

        rack_x=850 if index==1 else 1100 if index==2 else 1400
        # Outer stage-3 pallet slots align with the outer inbound lanes.
        rack_ys=[0] if index==1 else [-650,650] if index==2 else [-1087.5,0,1087.5]
        specs=[];slots=[]
        for ri,y in enumerate(rack_ys):
            members=[];meshes=[];group=f'PlayRack{ri+1:02d}'
            for pi,(offset,rot,scale,m,mats) in enumerate(rack_parts):
                p=unreal.Vector(rack_x,y,0)+offset
                a=mesh_actor(f'Rack_{ri+1:02d}_{pi}',m,(p.x,p.y,p.z),rot,scale,mats,
                             ['LayoutType:Rack','LayoutGroup:'+group,'RackRole:Storage','WarehouseFloor0'])
                members.append(a);meshes.append(m)
            specs.append(unreal.WarehouseStrength(name=group,members=members,physics_meshes=meshes,failure=unreal.WarehouseFailure.RACK,
                         mass_kg=250,rated_load_kg=4000,level_capacity_kg=2000,shelf_heights_cm=[20,140],impact_yield_j=250,impact_failure_j=1600,overload_seconds=10))
            for z in (20,140):
                for si,dy in enumerate((-62.5,62.5)):
                    slots.append(unreal.WarehouseRackSlot(name=f'R{ri+1:02d}-{int(z)}-{si}',rack=members[0],
                        pose=unreal.Transform(location=unreal.Vector(rack_x,y+dy,z)),clear_height_cm=110 if z==20 else 100,capacity_kg=2000))
        spawn(unreal.WarehouseDamageSystem,'Operations_Strength').set_editor_property('objects',specs)
        vehicle=spawn(unreal.WarehouseForklift,'Operations_AGV',(-hx+550,-hy+450,0),tags=['WarehouseFloor0'])
        for c in vehicle.get_components_by_class(unreal.StaticMeshComponent):
            t,m,mats=rig[c.get_name()];c.set_relative_transform(t,False,True);c.set_static_mesh(m);c.set_editor_property('override_materials',mats)
        vehicle.set_editor_property('auto_start',False);vehicle.set_editor_property('vehicle_name','지게차 1호')
        station=spawn(unreal.WarehouseChargingStation,'Operations_Charger',(-hx+550,-hy+450,0),tags=['WarehouseFloor0'])
        # The copied chassis settles about 1.02cm on the floor. Use a declared contact tolerance.
        station.set_editor_property('dock_position_tolerance_cm',2)
        vehicle.set_editor_property('charging_station',station)
        incoming_x=-550 if index==1 else -650 if index==2 else -800
        arrival_ys=[-450,450] if index==1 else [-800,0,800] if index==2 else [-1150,-400,400,1150]
        inbound=spawn(unreal.WarehouseInboundZone,'Operations_Inbound',(incoming_x,0,0),tags=['LayoutFixed','PlayFixed','WarehouseFloor0'])
        inbound.set_editor_property('half_size_cm',unreal.Vector(180,max(abs(y) for y in arrival_ys)+130,80))
        inbound.set_editor_property('rack_slots',slots);inbound.set_editor_property('fleet',[vehicle])
        for si,y in enumerate(arrival_ys):
            for suffix,pos,size in [('W',(incoming_x-62,y,.15),(4,124,.2)),('E',(incoming_x+62,y,.15),(4,124,.2)),('N',(incoming_x,y+62,.15),(124,4,.2)),('S',(incoming_x,y-62,.15),(124,4,.2))]:
                a=block(f'Marking_Arrival{si}_{suffix}',pos,size,paint_mat,['LayoutFixed','PlayFixed','WarehouseFloor0','WarehouseVisualOnly'])
                a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        sign=spawn(unreal.WarehouseAreaLabel,'UI_Inbound',(incoming_x,0,210),tags=['LayoutFixed','PlayFixed','WarehouseFloor0'])
        sign.set_editor_property('area_name','입고 대기 구역')
        console=spawn(unreal.WarehouseComputer,'Operations_Computer',(-hx+160,hy-140,0),yaw=180,tags=['LayoutFixed','PlayFixed','WarehouseFloor0'])
        console.set_editor_property('outbound_centre',unreal.Vector(0,-hy+250,0))
        console.set_editor_property('outbound_destinations',[unreal.Transform(location=unreal.Vector(x,-hy+250,0)) for x in (-250,0,250)])
        stage=spawn(unreal.WarehousePlayStage,'Operations_Stage')
        for k,v in dict(stage_index=index,starting_budget_won=budget,delivery_goal=goal,half_size_cm=unreal.Vector2D(hx,hy),ceiling_cm=ceiling,
                        inbound=inbound,carton_mesh=carton,arrival_points=[unreal.Vector(incoming_x,y,0) for y in arrival_ys]).items():stage.set_editor_property(k,v)
        spawn(unreal.PlayerStart,'Operations_Player',(0,-hy+150,100),yaw=90,tags=['LayoutFixed','PlayFixed'])
        # Validate the mounted services before saving; ceiling elements never enter AGV clearance.
        for a in created:
            c,e=a.get_actor_bounds(False)
            assert -hx-1<c.x-e.x and c.x+e.x<hx+1 and -hy-1<c.y-e.y and c.y+e.y<hy+2,(a.get_actor_label(),c,e)
            assert c.z+e.z<=ceiling+1,(a.get_actor_label(),c,e)
            if a.actor_has_tag('ObserverRoof'):assert c.z-e.z>350,a.get_actor_label()
            assert unreal.WarehouseDamageSystem.is_physical_contact(a.static_mesh_component)
        path=BASE+f'/Stage{index:02d}'
        assert unreal.EditorLoadingAndSavingUtils.save_map(world,path)
        reports.append(dict(stage=index,map=path,dimensions_m=[hx/50,hy/50,ceiling/100],budget=budget,goal=goal,rack_slots=len(slots),
                            service_actors=len(created),mounts=mounts,all_service_collision=True))
    # Verify the authored contact setting survives a map reload.
    for report in reports:
        assert LEVEL.load_level(report['map'])
        saved_station=next(a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.WarehouseChargingStation))
        assert abs(saved_station.get_editor_property('dock_position_tolerance_cm')-2)<.01
        report['charger_contact_setting_persisted']=True
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items()),'Administrator map changed'
    result=dict(passed=True,administrator_files_unchanged=len(hashes),stages=reports)
    (OUT/'Build.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('WAREHOUSE_PLAY_MAPS_BUILT',len(reports))

if __name__=='__main__':
    try:build()
    except Exception:
        (OUT/'Build.json').write_text(json.dumps({'passed':False,'error':traceback.format_exc()},ensure_ascii=False,indent=2),encoding='utf-8')
        print(traceback.format_exc())
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():unreal.SystemLibrary.quit_editor()
