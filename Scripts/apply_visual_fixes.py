"""Update only the saved forklift chassis transform; preserve jobs, charge links and cargo."""
import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
vehicle=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WH_AutonomousForklift')
unchanged={c.get_name():(c.static_mesh,c.get_relative_transform()) for c in vehicle.get_components_by_class(unreal.StaticMeshComponent) if c.get_name()!='Body'}
template=actors.spawn_actor_from_class(unreal.WarehouseForklift,unreal.Vector(0,0,-10000))
parts={c.get_name():c for c in template.get_components_by_class(unreal.StaticMeshComponent)}
vehicle.modify()
for target in vehicle.get_components_by_class(unreal.StaticMeshComponent):
 if target.get_name()!='Body':continue
 source=parts[target.get_name()]
 target.modify();target.set_static_mesh(source.static_mesh)
 target.set_editor_property('override_materials',[])
 target.set_relative_transform(source.get_relative_transform(),False,True)
 scale=target.get_editor_property('relative_scale3d')
 assert abs(scale.x-scale.y)<.0001 and abs(scale.y-scale.z)<.0001
actors.destroy_actor(template)
for c in vehicle.get_components_by_class(unreal.StaticMeshComponent):
 if c.get_name() in unchanged:assert (c.static_mesh,c.get_relative_transform())==unchanged[c.get_name()],c.get_name()
print('OTHER_12_PARTS_UNCHANGED')
station=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WH_ChargingStation')
reference=actors.spawn_actor_from_class(unreal.WarehouseChargingStation,unreal.Vector(0,0,-10000))
reference_parts={c.get_name():c for c in reference.get_components_by_class(unreal.SceneComponent)}
station.modify()
for c in station.get_components_by_class(unreal.SceneComponent):
 if c.get_name()=='DockPose':continue
 c.modify();c.set_relative_transform(reference_parts[c.get_name()].get_relative_transform(),False,True)
actors.destroy_actor(reference)
assert level.save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
print('FORKLIFT_BODY_RESTORED',vehicle.get_actor_bounds(False))
