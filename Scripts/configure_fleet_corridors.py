"""Apply clear handoff spacing and shared travel lanes without rebuilding warehouse assets."""
from pathlib import Path
import sys,unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from warehouse_staging_layout import PALLET_POSES,VEHICLE_POSES,OUTBOUND_PALLETS,NAVIGATION_ANCHORS

def configure_fleet_corridors():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named={a.get_actor_label():a for a in actors.get_all_level_actors()}
    for label,position in {**PALLET_POSES,**OUTBOUND_PALLETS}.items():
        pallet=named[label];old=pallet.get_actor_location();pallet.modify()
        pallet.set_actor_location(unreal.Vector(*position),False,False)
        cargo={'WH_TrainingPallet':'MZ_TransferCargo','WH_TrainingPallet_02':'WH_TransferCargo_02'}.get(label)
        if cargo:
            box=named[cargo];box.modify();box.set_actor_location(box.get_actor_location()+pallet.get_actor_location()-old,False,False)
    anchors=[unreal.Transform(location=unreal.Vector(x,y,0),rotation=unreal.Rotator(yaw=yaw)) for x,y,yaw in NAVIGATION_ANCHORS]
    for label,position in VEHICLE_POSES.items():
        vehicle=named[label];vehicle.modify()
        vehicle.set_actor_location(unreal.Vector(*position),False,False);vehicle.set_actor_rotation(unreal.Rotator(),False)
        old=list(vehicle.get_editor_property('navigation_anchors'))
        vehicle.set_editor_property('navigation_anchors',anchors+[p for p in old if p.translation.z>1])
    print('FLEET_CORRIDORS_CONFIGURED',len(anchors),'shared lanes; live cargo appearance and mass retained')

if __name__=='__main__':
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    configure_fleet_corridors()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('FLEET_CORRIDORS_SAVED')
    unreal.SystemLibrary.quit_editor()
