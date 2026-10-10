"""Apply the selected 1.8m/s travel limit to saved vehicles; preserve jobs and layout."""
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
vehicles = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.WarehouseForklift)]
assert len(vehicles) == 2, ('Expected the two existing warehouse vehicles', len(vehicles))
for vehicle in vehicles:
    vehicle.modify()
    vehicle.set_editor_property('empty_travel_speed_cm', 180)
    print('FORKLIFT_DRIVE_TUNING_APPLIED', vehicle.get_actor_label(), vehicle.empty_travel_speed_cm)
assert level.save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)

# Unit check: actor/mesh bounds are centimetres, independent of display units.
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
print('WAREHOUSE_WORLD_TO_METERS', settings.get_editor_property('world_to_meters'))
def physical_size(actor):
    points = []
    for mesh in actor.get_components_by_class(unreal.StaticMeshComponent):
        if not mesh.static_mesh or mesh.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
            continue
        minimum, maximum = mesh.get_local_bounds()
        for x in (minimum.x, maximum.x):
            for y in (minimum.y, maximum.y):
                for z in (minimum.z, maximum.z):
                    world_point = unreal.MathLibrary.transform_location(mesh.get_world_transform(), unreal.Vector(x, y, z))
                    points.append(unreal.MathLibrary.inverse_transform_direction(actor.get_actor_transform(), world_point - actor.get_actor_location()))
    return tuple(max(getattr(p, axis) for p in points) - min(getattr(p, axis) for p in points) for axis in ('x', 'y', 'z'))

for vehicle in vehicles:
    print('WAREHOUSE_VEHICLE_PHYSICAL_SIZE_CM', vehicle.get_actor_label(), physical_size(vehicle))
pallets = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.WarehousePallet)]
if pallets:
    print('WAREHOUSE_PALLET_PHYSICAL_SIZE_CM', physical_size(pallets[0]))
print('FORKLIFT_DRIVE_TUNING_SAVED')
