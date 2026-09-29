"""Apply VNSL14 sizing and its charging bay without rebuilding the warehouse."""
import unreal
import os, sys
sys.path.insert(0,os.path.dirname(__file__))

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
old = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'WH_AutonomousForklift')
assert not old.get_editor_property('powered'), 'Stop the training cycle before applying a model'
old_pallet = old.get_editor_property('target_pallet')
assert old_pallet
# Recreate native components so existing level instances receive the new 110 cm pallet geometry.
pallet = actors.spawn_actor_from_class(unreal.WarehousePallet,old_pallet.get_actor_location(),old_pallet.get_actor_rotation())
pallet.set_editor_property('payload_mass_kg',old_pallet.get_editor_property('payload_mass_kg'))
pallet.set_editor_property('is_spatially_loaded',False)
actors.destroy_actor(old_pallet)
pallet.set_actor_label('WH_TrainingPallet')
new = actors.spawn_actor_from_class(unreal.WarehouseForklift,old.get_actor_location(),old.get_actor_rotation())
new.set_editor_property('target_pallet',pallet)
new.set_editor_property('is_spatially_loaded',False)
parts = new.get_components_by_class(unreal.StaticMeshComponent)
assert len(parts)==13, len(parts)
assert all(p.static_mesh and p.static_mesh.get_path_name().startswith('/Game/Warehouse/AGV/Meshes/') for p in parts)
actors.destroy_actor(old)
new.set_actor_label('WH_AutonomousForklift')
for existing in actors.get_all_level_actors():
    if existing.get_actor_label()=='WH_ChargingStation':
        actors.destroy_actor(existing)
station=actors.spawn_actor_from_class(unreal.WarehouseChargingStation,unreal.Vector(1000,-1530,new.get_actor_location().z),new.get_actor_rotation())
station.set_actor_label('WH_ChargingStation')
station.set_editor_property('is_spatially_loaded',False)
station.set_editor_property('assigned_vehicle',new)
new.set_editor_property('charging_station',station)
from configure_forklift_autonomy import configure_autonomy
configure_autonomy()
from configure_warehouse_strength import configure_strength
configure_strength()
assert level.save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
print('ORANGE_AGV_APPLIED',new.get_path_name())
