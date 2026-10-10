"""Restore only front axle placements on saved warehouse vehicles. Preserve jobs and rig."""
import unreal

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
vehicles=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseForklift)]
assert len(vehicles)==2, len(vehicles)
scale=215/282.55
for vehicle in vehicles:
    parts={c.get_name():c for c in vehicle.get_components_by_class(unreal.StaticMeshComponent)}
    for name,sign in (('LoadWheelL',1),('LoadWheelR',-1)):
        wheel=parts[name]
        wheel.modify()
        wheel.set_relative_location(unreal.Vector(-7*scale,sign*45*scale,10.5*scale),False,True)
    print('ORIGINAL_FRONT_WHEELS_RESTORED',vehicle.get_actor_label())
assert level.save_current_level()
print('ORIGINAL_FRONT_WHEELS_SAVED',len(vehicles),'vehicles; no other rig or job changes')
