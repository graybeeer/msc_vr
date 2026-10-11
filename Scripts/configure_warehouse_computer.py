"""Add one temporary dock-side WMS/FMS computer; preserve the saved warehouse."""
import sys
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from warehouse_staging_layout import OUTBOUND
LEVEL='/Game/FirstPerson/Lvl_FirstPerson'
COMPUTER_POSITION=(1050,-2065,0)
DESTINATIONS=[(-800,-1570,0),(-800,-1395,0)]

def configure_computer():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named={a.get_actor_label():a for a in actors.get_all_level_actors()}
    label='WH_ManagementComputer'
    computer=named.get(label) or actors.spawn_actor_from_class(unreal.WarehouseComputer,unreal.Vector(*COMPUTER_POSITION),unreal.Rotator(yaw=90))
    assert isinstance(computer,unreal.WarehouseComputer)
    computer.modify();computer.set_actor_label(label);computer.set_folder_path('Warehouse/Management')
    computer.set_editor_property('is_spatially_loaded',False)
    computer.set_actor_location(unreal.Vector(*COMPUTER_POSITION),False,False)
    computer.set_actor_rotation(unreal.Rotator(yaw=90),False)
    computer.set_editor_property('outbound_centre',unreal.Vector(*OUTBOUND[:2],0))
    computer.set_editor_property('outbound_half_size',unreal.Vector(OUTBOUND[2],OUTBOUND[3],80))
    computer.set_editor_property('outbound_destinations',[unreal.Transform(location=unreal.Vector(*p),rotation=unreal.Rotator(yaw=180)) for p in DESTINATIONS])
    computer.tags=list(dict.fromkeys(list(computer.tags)+[unreal.Name('WarehouseFloor0')]))
    # Mark truck inventory without relying on editor-only labels in a packaged game.
    for name,a in named.items():
        if name.startswith('TRK_Cargo_') and not a.actor_has_tag('TruckCargo'):
            a.modify();a.tags=list(a.tags)+[unreal.Name('TruckCargo')]
    system=named['WH_DamageSystem'];system.modify()
    specs=[s for s in system.get_editor_property('objects') if s.name!=label]
    parts=computer.get_components_by_class(unreal.StaticMeshComponent)
    specs.append(unreal.WarehouseStrength(name=label,members=[computer],physics_meshes=[parts[0].static_mesh],
        failure=unreal.WarehouseFailure.RIGID,mass_kg=80,rated_load_kg=150,impact_yield_j=1000,impact_failure_j=5000))
    system.set_editor_property('objects',specs)
    print('WAREHOUSE_COMPUTER_CONFIGURED',COMPUTER_POSITION,DESTINATIONS)
    return computer

if __name__=='__main__':
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    configure_computer()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('WAREHOUSE_COMPUTER_SAVED')
    unreal.SystemLibrary.quit_editor()
