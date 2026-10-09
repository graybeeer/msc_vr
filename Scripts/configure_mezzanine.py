"""Add a three-level mezzanine and a custom AGV lift without rebuilding ground-floor stock.
Floor elevations 0/400/800 cm, roof 1200 cm. MZ_ actors belong to this script.
"""
import os,sys,math
import unreal
sys.path.insert(0,os.path.dirname(__file__))
from apply_real_world_scale import fit

LEVEL='/Game/FirstPerson/Lvl_FirstPerson'

def configure_mezzanine():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing={a.get_actor_label():a for a in actors.get_all_level_actors()}
    vehicle=existing['WH_AutonomousForklift']
    # Clear references before replacing script-owned actors.
    vehicle.modify();vehicle.set_editor_property('elevator',None)
    for label,a in existing.items():
        if label.startswith('MZ_'):actors.destroy_actor(a)
    named={a.get_actor_label():a for a in actors.get_all_level_actors()}
    cube=unreal.load_asset('/Engine/BasicShapes/Cube')
    blue=unreal.load_asset('/Game/Warehouse/Exterior/Materials/MI_BlueBand')
    yellow=unreal.load_asset('/Game/Warehouse/Materials/MI_SafetyYellow')
    steel=unreal.load_asset('/Game/Warehouse/AGV/Materials/M_Graphite_powder-coated_steel')
    floor_mat=unreal.load_asset('/Game/Scene_Warehouse/Assets/MS/Surfaces/Ind_War_Floor_Concrete_Smooth_01/MI_Ind_War_Floor_Concrete_Smooth_01_A')
    wall_mat=named['WH_Wall_N'].static_mesh_component.get_material(0)
    pallet_mesh=unreal.load_asset('/Game/Warehouse/Physics/SM_Ind_War_Storage_Pallet_Wood_Worn_01')
    cargo_mesh=unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/Cargo_Box_V1_001')
    assert all((cube,blue,yellow,steel,floor_mat,pallet_mesh,cargo_mesh))
    def setup(a,label,floor):
        a.set_actor_label('MZ_'+label);a.set_folder_path('Warehouse/Mezzanine/L'+str(floor+1))
        a.set_editor_property('is_spatially_loaded',False)
        a.tags=[unreal.Name('WarehouseFloor'+str(floor))]
        return a
    def block(label,center,size,mat=blue,floor=0,collision=True):
        a=setup(actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*center)),label,floor)
        c=a.static_mesh_component;c.set_static_mesh(cube);c.set_material(0,mat)
        c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
        a.set_actor_scale3d(unreal.Vector(*(s/100 for s in size)))
        return a
    def sign(label,text,loc,floor=0,yaw=-90,size=22):
        a=setup(actors.spawn_actor_from_class(unreal.TextRenderActor,unreal.Vector(*loc),unreal.Rotator(yaw=yaw)),label,floor)
        c=a.get_component_by_class(unreal.TextRenderComponent);c.set_text(text);c.set_world_size(size)
        c.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
        return a
    def rail(label,start,end,z,floor):
        dx,dy=end[0]-start[0],end[1]-start[1];length=math.hypot(dx,dy)
        yaw=math.degrees(math.atan2(dy,dx));cx,cy=(start[0]+end[0])/2,(start[1]+end[1])/2
        for height,thick in ((8,16),(55,4),(110,5)):
            a=block(label+'_Bar'+str(height),(cx,cy,z+height),(length,4,thick),yellow,floor)
            a.set_actor_rotation(unreal.Rotator(yaw=yaw),False)
        n=math.ceil(length/180)
        for i in range(n+1):block(label+'_Post'+str(i),(start[0]+dx*i/n,start[1]+dy*i/n,z+55),(5,5,110),yellow,floor)
    # A flush lift needs a real pit: split the original ground slab instead of letting
    # the moving platform clip through an unbroken collision floor when descending.
    old_floor=named['Floor'];c,e=old_floor.get_actor_bounds(False)
    # The native 350 x 240 cm deck is rotated 90 degrees: world 240 x 350 cm.
    # Leave 1 cm at both travel-axis ends instead of an exact slab/deck edge contact.
    fit(old_floor,(4000,3424,e.z*2),(0,-288,-e.z))
    for label,cx,cy,sx,sy in [('GroundRearWest',-570.5,1712,2859,576),
                             ('GroundRearEast',1570.5,1712,859,576),
                             ('GroundRear',1000,1888,282,224)]:
        a=block(label,(cx,cy,-e.z),(sx,sy,e.z*2),floor_mat)
        a.tags=list(a.tags)+[unreal.Name('ObserverArea')]
    block('LiftPitBase',(1000,1600,-30),(282,352,20),steel)
    # Raise the shell by measured bounds, preserving exterior docks and truck dimensions.
    for label in ('WH_Wall_N','WH_Wall_E','WH_Wall_W','WH_Wall_S_Left','WH_Wall_S_Right'):
        a=named[label];c,e=a.get_actor_bounds(False);fit(a,(e.x*2,e.y*2,1200),(c.x,c.y,600))
    fit(named['WH_LoadingDoor_Header'],(1400,35,710),(0,-1980,845))
    fit(named['WH_Roof'],(4000,4000,30),(0,0,1215))
    for label,a in named.items():
        if label.startswith('WH_Column_'):
            c,e=a.get_actor_bounds(False);fit(a,(40,40,1180),(1940 if c.x>0 else -1940,c.y,590))
        elif label.startswith('WH_RoofBeam_'):
            p=a.get_actor_location();fit(a,(3880,20,30),(0,p.y,1170))
        elif label.startswith(('WH_LightFixture_','WH_SideFixture_')):
            p=a.get_actor_location();a.modify();a.set_actor_location(unreal.Vector(p.x,p.y,1140),False,False)
        elif label.startswith(('WH_Light_','WH_WorkLight_')):
            p=a.get_actor_location()
            actors.destroy_actor(a)
            light=actors.spawn_actor_from_class(unreal.SpotLight,unreal.Vector(p.x,p.y,1090),unreal.Rotator(pitch=-90))
            light.set_actor_label(label);light.set_editor_property('is_spatially_loaded',False)
            c=light.spot_light_component;c.set_editor_property('intensity_units',unreal.LightUnits.LUMENS)
            c.set_editor_property('intensity',50000. if abs(p.x)<1 else 12000.)
            c.set_editor_property('attenuation_radius',1450. if abs(p.x)<1 else 850.)
            c.set_editor_property('outer_cone_angle',65.);c.set_editor_property('inner_cone_angle',45.)
            c.set_editor_property('source_radius',30.)
        elif label.startswith(('EXT_FrontCladding_','EXT_SideCladding_')):
            c,e=a.get_actor_bounds(False);fit(a,(e.x*2,e.y*2,1200),(c.x,c.y,600))
        elif label=='EXT_HeaderCladding':fit(a,(1400,16,710),(0,-2006,845))
        elif label.startswith(('EXT_SideParapet_','EXT_FrontParapet')):
            p=a.get_actor_location();a.modify();a.set_actor_location(unreal.Vector(p.x,p.y,1243),False,False)
        elif label.startswith('EXT_RoofVent'):
            p=a.get_actor_location();a.modify();a.set_actor_location(unreal.Vector(p.x,p.y,1389 if 'Top' in label else 1300),False,False)
    # Move one staging group away from the lift waiting zone; keep the original models/metadata.
    pallet=named['WH_DispatchPallet_4'];c,e=pallet.get_actor_bounds(False)
    delta=unreal.Vector(1580,1100,7.5)-c
    for label,a in named.items():
        if label=='WH_DispatchPallet_4' or label.startswith('WH_Stock_4_'):
            a.modify();a.set_actor_location(a.get_actor_location()+delta,False,False)
    for floor in (1,2):
        z=floor*400
        # U-shaped galleries around a 10 x 26 m atrium. The lift shaft has a real opening.
        for name,cx,cy,sx,sy in [('Left',-1150,0,1300,3600),('Right',1150,-188,1300,3224),
                               ('RearWest',679.5,1612,359,376),('RearEast',1470.5,1612,659,376),
                               ('North',0,1550,1000,500),('South',0,-1550,1000,500)]:
            block(f'L{floor+1}_Deck_{name}',(cx,cy,z-10),(sx,sy,20),floor_mat,floor)
        for x in (-1775,-500,500,1775):
            for y in (-1750,-875,0,875,1750):
                block(f'L{floor+1}_Column_{x}_{y}',(x,y,z-200),(22,22,400),yellow,floor)
            # Visible I-beam flanges and web below the deck.
            for h,thick,width in ((-23,4,26),(-38,26,7),(-53,4,26)):
                block(f'L{floor+1}_Beam_{x}_{h}',(x,0,z+h),(width,3550,thick),blue,floor)
        for y in (-1750,-875,0,875,1750):
            for x in (-1150,1150):
                if x==1150 and y==1750:
                    for cx,width in ((675,350),(1475,650)):
                        block(f'L{floor+1}_CrossBeam_{cx}_{y}',(cx,y,z-38),(width,18,30),blue,floor)
                else:block(f'L{floor+1}_CrossBeam_{x}_{y}',(x,y,z-38),(1280,18,30),blue,floor)
        for name,start,end in [('InnerLeft',(-500,-1300),(-500,1300)),('InnerRight',(500,-1300),(500,1300)),
                              ('InnerSouth',(-500,-1300),(500,-1300)),('InnerNorth',(-500,1300),(500,1300)),
                              ('OuterEast',(1800,-1800),(1800,1800)),('OuterSouth',(-1800,-1800),(1800,-1800)),
                              ('OuterNorth',(-1800,1800),(1800,1800)),
                              ]:
            rail(f'L{floor+1}_{name}',start,end,z,floor)
        west_segments=[(-1800,-1292),(-1152,-480),(-340,1800)] if floor==1 else [(-1800,-480),(-340,1800)]
        for i,(ya,yb) in enumerate(west_segments):rail(f'L{floor+1}_OuterWest_{i}',(-1800,ya),(-1800,yb),z,floor)
        block(f'L{floor+1}_StairBaseLanding',(-1820,-1222,z-410),(250,140,20),floor_mat,floor-1)
        rail(f'L{floor+1}_StairBaseBack',(-1945,-1292),(-1695,-1292),z-400,floor-1)
        rail(f'L{floor+1}_StairBaseSide',(-1945,-1292),(-1945,-1152),z-400,floor-1)
        rail(f'L{floor+1}_StairTopSide',(-1945,-480),(-1945,-340),z,floor)
        # 24 physical steps (16.7 cm rise, 28 cm tread), 130 cm wide, stacked flights.
        for i in range(24):
            top=z-400+(i+1)*400/24
            block(f'L{floor+1}_Step_{i}',(-1880,-1138+i*28,top-8.333),(130,28,16.667),steel,floor)
        block(f'L{floor+1}_StairLanding',(-1820,-410,z-10),(250,140,20),floor_mat,floor)
        rail(f'L{floor+1}_StairLandingRail',(-1945,-340),(-1695,-340),z,floor)
        for x in (-1945,-1815):
            start=unreal.Vector(x,-1152,z-400+110);end=unreal.Vector(x,-480,z+110)
            vec=end-start;a=block(f'L{floor+1}_StairHandrail_{x}',tuple((getattr(start,k)+getattr(end,k))/2 for k in 'xyz'),(vec.length(),5,5),yellow,floor)
            a.set_actor_rotation(unreal.Rotator(pitch=math.degrees(math.atan2(400,672)),yaw=90),False)
            for i in range(0,24,4):block(f'L{floor+1}_StairPost_{x}_{i}',(x,-1138+i*28,z-400+(i+1)*400/24+55),(5,5,110),yellow,floor)
        # Existing eight-part Fab rack modules, same 250 cm bay / 100 cm depth / 250 cm height.
        for side,x in [('Left',-1550),('Right',1550)]:
            for row,y in enumerate((-950,-150,650)):
                for part in 'ABCDEFGH':
                    template=named[f'WH_Rack_{side}_00_{part}'];tc,te=template.get_actor_bounds(False)
                    a=setup(actors.spawn_actor_from_class(unreal.StaticMeshActor,template.get_actor_location()+unreal.Vector(x-tc.x,y+1330,z),template.get_actor_rotation()),f'L{floor+1}_Rack_{side}_{row}_{part}',floor)
                    a.static_mesh_component.set_static_mesh(template.static_mesh_component.static_mesh)
                    a.static_mesh_component.set_collision_profile_name('BlockAll')
                    a.set_actor_scale3d(template.get_actor_scale3d())
                    # Copy exact calibrated geometry including beam side offsets.
                    dx=x-(-215 if side=='Left' else 215)
                    a.set_actor_location(template.get_actor_location()+unreal.Vector(dx,y+1330,z),False,False)
                for tier,top in enumerate((20,140)):
                    for slot,dy in enumerate((-62.5,62.5)):
                        label=f'L{floor+1}_{side}_{row}_{tier}_{slot}'
                        a=setup(actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector()),'Pallet_'+label,floor)
                        a.static_mesh_component.set_static_mesh(pallet_mesh);a.static_mesh_component.set_collision_profile_name('BlockAll')
                        fit(a,(110,110,15),(x,y+dy,z+top+7.5))
                        box=setup(actors.spawn_actor_from_class(unreal.WarehouseCargo,unreal.Vector()),'Cargo_'+label,floor)
                        box.set_cargo_mesh(cargo_mesh);fit(box,(40,30,28),(x,y+dy,z+top+29))
        # Ground and L2 fixtures below each gallery; ceiling fixtures serve L3.
        for x in (-1080,1080):
            for y in (-1300,-500,300,1100):
                template=named['WH_LightFixture_0'];a=setup(actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(x,y,z-68),template.get_actor_rotation()),f'L{floor}_Lamp_{x}_{y}',floor)
                a.static_mesh_component.set_static_mesh(template.static_mesh_component.static_mesh)
                a.static_mesh_component.set_collision_profile_name('BlockAll')
                light=setup(actors.spawn_actor_from_class(unreal.SpotLight,unreal.Vector(x,y,z-95),unreal.Rotator(pitch=-90)),f'L{floor}_Light_{x}_{y}',floor)
                c=light.spot_light_component;c.set_editor_property('intensity_units',unreal.LightUnits.LUMENS);c.set_editor_property('intensity',7000.)
                c.set_editor_property('attenuation_radius',750.);c.set_editor_property('source_radius',30.)
                c.set_editor_property('outer_cone_angle',70.);c.set_editor_property('inner_cone_angle',50.)
        sign(f'L{floor+1}_LevelSign',f'L{floor+1} / STORAGE & TRANSFER',(0,1310,z+65),floor,size=35)
        # Extra upper cladding joints/windows keep the taller exterior consistent.
        for side in (-1,1):
            block(f'L{floor+1}_FacadeBand_{side}',(side*2020,0,z+175),(8,3700,75),blue,floor)
    lift=actors.spawn_actor_from_class(unreal.WarehouseElevator,unreal.Vector(1000,1600,0),unreal.Rotator(yaw=90))
    lift.set_actor_label('MZ_AGVElevator');lift.set_editor_property('is_spatially_loaded',False)
    lift.set_folder_path('Warehouse/Mezzanine/Elevator')
    vehicle.set_editor_property('elevator',lift)
    anchors=[]
    for f in range(3):
        for x,y,yaw in [(1000,-1100,90),(1000,0,90),(1000,1100,90),(1350,-1050,90),(1350,-650,90),(1350,300,90),(1350,1100,90),(1000,-1450,90)]:
            anchors.append(unreal.Transform(location=unreal.Vector(x,y,f*400),rotation=unreal.Rotator(yaw=yaw)))
        for x in (825,1175):block(f'L{f+1}_LiftLane_{x}',(x,990,f*400+.2),(5,500,.3),yellow,f,False)
        block(f'L{f+1}_WaitLine',(1000,1250,f*400+.2),(350,6,.3),yellow,f,False)
        sign(f'L{f+1}_LiftSign',f'L{f+1} / AGV LIFT - WAIT\n2000 kg incl. vehicle',(1000,1390,f*400+315),f,size=17)
    vehicle.set_editor_property('navigation_anchors',anchors)
    # A real supported closed carton travels with the existing training pallet.
    training=named['WH_TrainingPallet']
    box=setup(actors.spawn_actor_from_class(unreal.WarehouseCargo,unreal.Vector()),'TransferCargo',0)
    box.set_cargo_mesh(cargo_mesh);p=training.get_actor_location();fit(box,(45,35,30),(p.x,p.y,p.z+30))
    job=unreal.WarehouseWorkOrder(job_id='DEMO-L1-L3',source_system='FMS-SIM',pallet=training,
        destination=unreal.Transform(location=unreal.Vector(1000,-500,800),rotation=unreal.Rotator(yaw=90)))
    vehicle.set_editor_property('pending_jobs',[job]);vehicle.set_editor_property('autonomous_mode',True)
    from configure_cargo_variety import configure_cargo_variety
    configure_cargo_variety()
    from configure_warehouse_strength import configure_strength
    configure_strength()
    from configure_observer_view import configure_observer_view
    configure_observer_view()
    print('MEZZANINE_CONFIGURED','3 floors 0/400/800 cm; roof 1200 cm; custom 350x240cm 2000kg AGV lift')
    return lift

if __name__=='__main__':
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    configure_mezzanine()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('MEZZANINE_SAVED')
