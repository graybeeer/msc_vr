"""Incremental 48x40m ground floor and yellow inbound staging; preserves docks and mezzanine."""
import sys
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from apply_real_world_scale import fit

LEVEL='/Game/FirstPerson/Lvl_FirstPerson'
RACK_CENTRES=[(2620,y) for y in (-1600,-950,-300,350)]

def configure_inbound_area():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named={a.get_actor_label():a for a in actors.get_all_level_actors()}
    system=named['WH_DamageSystem']
    specs=list(system.get_editor_property('objects'))
    by_name={s.name:i for i,s in enumerate(specs)}
    additions=[]
    def setup(a,label):
        a.set_actor_label(label); a.set_folder_path('Warehouse/Inbound')
        a.set_editor_property('is_spatially_loaded',False)
        a.tags=[unreal.Name('WarehouseFloor0')]
        named[label]=a
        return a
    def clone(label,template):
        if label in named:return named[label]
        a=setup(actors.spawn_actor_from_class(unreal.StaticMeshActor,template.get_actor_location(),template.get_actor_rotation()),label)
        part=a.static_mesh_component; source=template.static_mesh_component
        part.set_static_mesh(source.static_mesh)
        part.set_collision_profile_name(source.get_collision_profile_name())
        part.set_editor_property('override_materials',source.get_editor_property('override_materials'))
        a.set_actor_scale3d(template.get_actor_scale3d())
        return a
    def resize(label,size,center):
        fit(named[label],size,center)
    resize('WH_Wall_E',(35,4000,1200),(2780,0,600))
    resize('WH_Wall_N',(4800,35,1200),(400,1980,600))
    resize('WH_Wall_S_Right',(2100,35,1200),(1750,-1980,600))
    resize('WH_Roof',(4800,4000,30),(400,0,1215))
    resize('EXT_FoundationSide_1',(50,4000,130),(2780,0,-65))
    resize('EXT_FoundationRear',(4800,50,130),(400,1980,-65))
    resize('EXT_FoundationFront_1350',(2100,50,130),(1750,-1980,-65))
    for label in ('EXT_FrontCladding_1350','EXT_FrontBlueBand_1350','EXT_FrontWindow_1350'):
        a=named[label]; c,e=a.get_actor_bounds(False)
        fit(a,(1890 if label=='EXT_FrontWindow_1350' else 2100,2*e.y,2*e.z),(1750,c.y,c.z))
    for label,a in list(named.items()):
        if label.startswith('EXT_FrontJoint_1350_'):
            c,e=a.get_actor_bounds(False);fit(a,(2100,2*e.y,2*e.z),(1750,c.y,c.z))
        if label.startswith('EXT_FrontMullion_1350_') and 'InboundExpanded' not in [str(t) for t in a.tags]:
            p=a.get_actor_location();a.set_actor_location(unreal.Vector(700+(p.x-700)*2100/1300,p.y,p.z),False,False)
            a.tags=list(a.tags)+[unreal.Name('InboundExpanded')]
        if label.startswith(('EXT_SideCladding_1','EXT_SideBlueBand_1','EXT_SideWindow_1','EXT_SideMullion_1_','EXT_SideParapet_1')) and 'InboundExpanded' not in [str(t) for t in a.tags]:
            a.set_actor_location(a.get_actor_location()+unreal.Vector(800,0,0),False,False)
            a.tags=list(a.tags)+[unreal.Name('InboundExpanded')]
    c,e=named['EXT_FrontParapet'].get_actor_bounds(False)
    resize('EXT_FrontParapet',(4840,2*e.y,2*e.z),(400,c.y,c.z))
    c,e=named['EXT_SidePath_1'].get_actor_bounds(False)
    resize('EXT_SidePath_1',(200,4000,2*e.z),(2900,0,c.z))
    floor=clone('WH_InboundFloor',named['Floor']);fit(floor,(800,4000,50),(2400,0,-25))
    floor.tags=[unreal.Name('ObserverArea')];additions.append(floor)
    for y in (-1500,-500,500,1500):
        beam=clone(f'WH_InboundRoofBeam_{y}',named[f'WH_RoofBeam_{y}']);fit(beam,(800,20,30),(2340,y,1170))
        beam.tags=[unreal.Name('ObserverRoof')];additions.append(beam)
        column=clone(f'WH_InboundColumn_{y}',named[f'WH_Column_1850_{y}']);fit(column,(40,40,1180),(2740,y,590));additions.append(column)
    for y in (-1600,-650,300,1250):
        label=f'WH_InboundLight_{y}'
        light=named.get(label) or setup(actors.spawn_actor_from_class(unreal.PointLight,unreal.Vector(2300,y,1000)),label)
        part=light.point_light_component
        part.set_mobility(unreal.ComponentMobility.MOVABLE)
        part.set_editor_property('intensity_units',unreal.LightUnits.LUMENS)
        part.set_editor_property('intensity',18000.0)
        part.set_editor_property('attenuation_radius',1900.0)
        part.set_editor_property('source_radius',40.0)
    building=specs[by_name['WarehouseBuilding']]
    members=list(building.members); meshes=list(building.physics_meshes)
    for a in additions:
        if a not in members:
            label=a.get_actor_label()
            template=named['Floor'] if a==floor else named[label.replace('WH_InboundRoofBeam_','WH_RoofBeam_').replace('WH_InboundColumn_','WH_Column_1850_')]
            meshes.append(meshes[members.index(template)])
            members.append(a)
    building.members=members;building.physics_meshes=meshes
    specs[by_name['WarehouseBuilding']]=building
    reference=specs[by_name['PalletRack_Left_00']]
    slots=[]
    for index,(x,y) in enumerate(RACK_CENTRES):
        label=f'WH_InboundRack_{index:02}'; rack_parts=[]
        for part in 'ABCDEFGH':
            template=named['WH_Rack_Left_00_'+part]
            a=clone(label+'_'+part,template)
            # Retain original mesh proportions; translating measured template is idempotent.
            a.set_actor_location(template.get_actor_location()+unreal.Vector(x+215,y+1330,0),False,False)
            rack_parts.append(a)
        # Flat 20mm plywood decking distributes worn pallet feet across both beams.
        # A rack configured for these pallets includes decking, not only a bare frame.
        cube=unreal.load_asset('/Engine/BasicShapes/Cube')
        for z in (20,140):
            deck_label=f'{label}_Deck_{z}'
            deck=named.get(deck_label) or setup(actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(x,y,z-1)),deck_label)
            deck.static_mesh_component.set_static_mesh(cube)
            deck.static_mesh_component.set_material(0,named['WH_TrainingPallet'].get_component_by_class(unreal.StaticMeshComponent).get_material(0))
            deck.static_mesh_component.set_collision_profile_name('BlockAll')
            fit(deck,(100,250,2),(x,y,z-1));rack_parts.append(deck)
        rack_spec=unreal.WarehouseStrength(name=label,members=rack_parts,physics_meshes=list(reference.physics_meshes)+[cube,cube],
                failure=unreal.WarehouseFailure.RACK,mass_kg=250,rated_load_kg=reference.rated_load_kg,
                level_capacity_kg=reference.level_capacity_kg,shelf_heights_cm=[20,140],impact_yield_j=reference.impact_yield_j,
                impact_failure_j=reference.impact_failure_j,overload_seconds=reference.overload_seconds)
        if label not in by_name:specs.append(rack_spec)
        else:specs[by_name[label]]=rack_spec
        for z in (20,140):
            for position,offset in enumerate((-62.5,62.5)):
                slots.append(unreal.WarehouseRackSlot(name=f'{label}-{z}-{position}',rack=rack_parts[0],
                    pose=unreal.Transform(location=unreal.Vector(x,y+offset,z),rotation=unreal.Rotator()),
                    clear_height_cm=110 if z==20 else 100,capacity_kg=reference.level_capacity_kg))
    line_template=named['WH_TrainingLine_650']
    for index,(cx,cy,hx,hy) in enumerate(((2350,-1640,190,270),(2350,-1080,190,210)),1):
        for side,center,size in [('W',(cx-hx,cy,.12),(8,2*hy, .2)),('E',(cx+hx,cy,.12),(8,2*hy,.2)),
                                 ('S',(cx,cy-hy,.12),(2*hx,8,.2)),('N',(cx,cy+hy,.12),(2*hx,8,.2))]:
            a=clone(f'WH_InboundZone{index}_Paint_{side}',line_template);fit(a,size,center)
            a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        label=f'WH_InboundZone{index}_Label'
        sign=named.get(label) or setup(actors.spawn_actor_from_class(unreal.TextRenderActor,unreal.Vector(cx,cy,1)),label)
        sign.set_actor_location(unreal.Vector(cx,cy,1),False,False)
        sign.set_actor_rotation(unreal.Rotator(pitch=90,yaw=-90),False)
        text=sign.get_component_by_class(unreal.TextRenderComponent)
        text.set_text('1  INBOUND > RACK' if index==1 else '2  BUFFER')
        text.set_world_size(22);text.set_text_render_color(unreal.Color(r=255,g=190,b=20,a=255))
        sign.set_actor_enable_collision(False)
    fleet=[named['WH_AutonomousForklift'],named['WH_AutonomousForklift_02']]
    canonical=unreal.load_asset('/Game/Warehouse/Physics/SM_Pallet_110')
    for index,(vehicle,pallet_label,cargo_label,y) in enumerate(zip(fleet,['WH_TrainingPallet','WH_TrainingPallet_02'],['MZ_TransferCargo','WH_TransferCargo_02'],[-1662.5,-1537.5])):
        pallet=named[pallet_label]; cargo=named[cargo_label]
        if 'InboundStraight' not in [str(t) for t in pallet.tags]:
            old=pallet.get_actor_location();pallet.set_actor_location(unreal.Vector(2445,y,.1),False,False)
            pallet.set_actor_rotation(unreal.Rotator(),False)
            cargo.set_actor_location(cargo.get_actor_location()+pallet.get_actor_location()-old,False,False)
            pallet.tags=list(pallet.tags)+[unreal.Name('InboundStraight')]
        vehicle.set_editor_property('pending_jobs',[]);vehicle.set_editor_property('target_pallet',None)
        if 'InboundStraight' not in [str(t) for t in vehicle.tags]:
            vehicle.set_actor_location(unreal.Vector(2225,-1537.5 if index==0 else -1662.5,0),False,False)
            vehicle.set_actor_rotation(unreal.Rotator(),False)
            vehicle.tags=list(vehicle.tags)+[unreal.Name('InboundStraight')]
        vehicle.set_editor_property('auto_start',True)
        anchors=[unreal.Transform(location=unreal.Vector(x,y,0),rotation=unreal.Rotator(yaw=yaw))
                 for x,y,yaw in [(2225,-1790,0),(2225,-1490,0),(2110,-1000,-90),(2250,-650,90),
                    (2250,-200,90),(2250,300,90),(2110,-1300,-90),(1650,-1790,0),(1100,-1790,0)]]
        vehicle.set_editor_property('navigation_anchors',list(vehicle.get_editor_property('navigation_anchors'))+[
            p for p in anchors if not any((p.translation-a.translation).length()<1 for a in vehicle.get_editor_property('navigation_anchors'))])
    # A third empty pallet remains available for the player's first new inbound load.
    label='WH_InboundPallet_Empty'
    pallet=named.get(label)
    if not pallet:
        pallet=setup(actors.spawn_actor_from_class(unreal.WarehousePallet,unreal.Vector(2445,-1640,.1)),label)
        pallet.get_component_by_class(unreal.StaticMeshComponent).set_static_mesh(canonical)
        specs.append(unreal.WarehouseStrength(name=label,members=[pallet],physics_meshes=[canonical],failure=unreal.WarehouseFailure.CRUSH,
            mass_kg=25,rated_load_kg=1500,impact_yield_j=120,impact_failure_j=900,overload_seconds=10))
    if 'InboundStraight' not in [str(t) for t in pallet.tags]:
        pallet.set_actor_location(unreal.Vector(2445,-1840,.1),False,False)
        pallet.tags=list(pallet.tags)+[unreal.Name('InboundStraight')]
    zone=named.get('WH_InboundDispatch') or setup(actors.spawn_actor_from_class(unreal.WarehouseInboundZone,unreal.Vector(2350,-1640,0)),'WH_InboundDispatch')
    zone.set_actor_location(unreal.Vector(2350,-1640,0),False,False)
    zone.set_editor_property('half_size_cm',unreal.Vector(190,270,80));zone.set_editor_property('fleet',fleet);zone.set_editor_property('rack_slots',slots)
    system.modify();system.set_editor_property('objects',specs)
    print('INBOUND_AREA_CONFIGURED','48x40m, 2 yellow zones, 3 staging pallets, 4 racks / 16 slots, 2 AGVs')
    return zone

if __name__=='__main__':
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    configure_inbound_area()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('INBOUND_AREA_SAVED')
    unreal.SystemLibrary.quit_editor()
