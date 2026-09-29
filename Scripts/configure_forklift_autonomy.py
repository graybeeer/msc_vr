"""Install one local FMS demonstration order without rebuilding warehouse actors."""
import unreal
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))

def configure_autonomy(reset=False):
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    # Painted floor markings have no physical raised edge.
    for actor in actors:
        if actor.get_actor_label().startswith(('WH_TrainingLine_', 'WH_TrainingEnd_', 'WH_Walkway_', 'WH_Crosswalk_')):
            actor.modify()
            actor.static_mesh_component.set_collision_profile_name('NoCollision')
    vehicle=next(a for a in actors if a.get_actor_label()=='WH_AutonomousForklift')
    pallet=next(a for a in actors if a.get_actor_label()=='WH_TrainingPallet')
    vehicle.modify()
    vehicle.set_editor_property('autonomous_mode',True)
    vehicle.set_editor_property('target_pallet',pallet)
    if reset or not vehicle.get_editor_property('pending_jobs'):
        job=unreal.WarehouseWorkOrder()
        job.set_editor_property('job_id','DEMO-001')
        job.set_editor_property('source_system','FMS-SIM')
        job.set_editor_property('pallet',pallet)
        job.set_editor_property('destination',unreal.Transform(location=unreal.Vector(1000,400,0),rotation=unreal.Rotator(yaw=90)))
        vehicle.set_editor_property('pending_jobs',[job])
    # Right-side operating lane; pose headings matter for nonholonomic route edges.
    anchors=[unreal.Transform(location=unreal.Vector(x,y,0),rotation=unreal.Rotator(yaw=yaw))
             for x,y,yaw in [(1000,-1100,90),(1000,0,90),(1000,750,90),(1450,-900,0),(1450,500,180)]]
    if reset or not vehicle.get_editor_property('navigation_anchors'):
        vehicle.set_editor_property('navigation_anchors',anchors)
    return vehicle

if __name__=='__main__':
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    configure_autonomy()
    from configure_warehouse_strength import configure_strength
    configure_strength()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('AUTONOMY_CONFIGURED')
