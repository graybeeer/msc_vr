"""Tag the warehouse viewing area and roofs; preserve geometry, materials and collision."""
import unreal

LEVEL='/Game/FirstPerson/Lvl_FirstPerson'
ROOFS=('WH_Roof','EXT_Canopy','EXT_CanopyFascia','EXT_GatehouseRoof','EXT_FrontParapet')
ROOF_PREFIXES=('WH_RoofBeam_','EXT_RoofVent_','EXT_RoofVentTop_','EXT_SideParapet_')

def configure_observer_view():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    named={a.get_actor_label():a for a in actors}
    assert 'Floor' in named and 'WH_Roof' in named,'Warehouse floor/roof missing'
    roofs=[]
    for name,actor in named.items():
        tag='ObserverArea' if name=='Floor' else 'ObserverRoof' if name in ROOFS or name.startswith(ROOF_PREFIXES) else None
        if not tag:continue
        tags=list(actor.tags)
        if tag not in [str(t) for t in tags]:
            actor.modify()
            actor.tags=tags+[unreal.Name(tag)]
        if tag=='ObserverRoof':roofs.append(actor)
    print('OBSERVER_TAGS_CONFIGURED',len(roofs),'roofs, 1 viewing area')
    return roofs

if __name__=='__main__':
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    configure_observer_view()
    assert level.save_current_level()
    print('OBSERVER_VIEW_SAVED')
