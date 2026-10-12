"""Author runtime experiment groups; retain saved geometry, cargo and all simulation settings."""
import unreal

def configure_warehouse_layout():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named={a.get_actor_label():a for a in actors.get_all_level_actors()}
    editor=named.get('WH_LayoutEditor') or actors.spawn_actor_from_class(unreal.WarehouseLayoutEditor,unreal.Vector())
    editor.set_actor_label('WH_LayoutEditor');editor.set_editor_property('is_spatially_loaded',False);editor.modify()
    def tags(a,*values):
        a.modify();a.tags=list(dict.fromkeys([str(t) for t in a.tags]+list(values)))
    rack_members=set()
    strength=next(a for a in named.values() if isinstance(a,unreal.WarehouseDamageSystem))
    for spec in strength.get_editor_property('objects'):
        if spec.failure!=unreal.WarehouseFailure.RACK:continue
        members=[a for a in spec.members if a]
        if not members:continue
        floor=max(0,min(2,int((min(a.get_actor_bounds(False)[0].z-a.get_actor_bounds(False)[1].z for a in members)+10)/400)))
        for a in members:
            tags(a,'LayoutType:Rack','LayoutGroup:'+spec.name,'WarehouseFloor'+str(floor));rack_members.add(a)
    lift=named['MZ_AGVElevator'];tags(lift,'LayoutType:Elevator','LayoutGroup:Elevator')
    for label,a in named.items():
        if isinstance(a,unreal.WarehouseChargingStation):
            tags(a,'LayoutType:Charger')
            a.tags=[tag for tag in a.tags if str(tag)!='LayoutType:Other']
        if '_LiftLane_' in label or label.endswith(('_WaitLine','_LiftSign')) or (label.startswith('WH_AreaUI_') and 'Lift_' in label):
            tags(a,'LayoutType:Elevator','LayoutGroup:Elevator')
        if label=='Floor' or label=='WH_InboundFloor' or label.startswith('MZ_GroundRear') or (label.startswith(('MZ_L2_Deck_','MZ_L3_Deck_'))):
            tags(a,'ElevatorLayoutFloor','ElevatorFloorSource:'+label,'LayoutFixed')
        if 'Pit' in label:tags(a,'ElevatorLayoutPit','ElevatorPitSource:'+label,'LayoutFixed','LayoutGroup:Elevator','LayoutType:Elevator')
        if a in rack_members or isinstance(a,(unreal.WarehouseForklift,unreal.WarehouseElevator)):continue
        if label.startswith(('WH_Wall','WH_Roof','EXT_','MZ_L2_Deck_','MZ_L3_Deck_','MZ_Ground','WH_InboundFloor')) or label=='Floor':
            if not any(str(t).startswith('LayoutGroup:TruckDock') for t in a.tags):tags(a,'LayoutFixed')
            continue
        if isinstance(a,(unreal.WarehousePallet,unreal.WarehouseCargo,unreal.WarehouseWorker,unreal.WarehouseComputer)) or label.startswith(('WH_Utility_','WH_Column_')):
            tags(a,'LayoutType:Other')
    zone_data=[]
    for index,key,centre,half in [(1,'Inbound',(1100,-1640,0),(400,310,80)),(2,'Outbound',(-1000,-1640,0),(350,310,80))]:
        members=[a for label,a in named.items() if label.startswith('WH_InboundZone'+str(index)+'_') or label=='WH_AreaUI_'+key]
        if key=='Inbound':members.append(named['WH_InboundDispatch'])
        for a in members:tags(a,'LayoutGroup:Zone'+key,'LayoutType:Zone')
        zone_data.append(unreal.WarehouseLayoutZone(name=key,centre=unreal.Vector(*centre),half_size=unreal.Vector(*half),members=members))
    editor.set_editor_property('zones',zone_data)
    print('WAREHOUSE_LAYOUT_GROUPS',len(rack_members),'rack parts',len(zone_data),'zones')

if __name__=='__main__':
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    configure_warehouse_layout()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('WAREHOUSE_LAYOUT_METADATA_SAVED')
    unreal.SystemLibrary.quit_editor()
