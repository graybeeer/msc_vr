"""Bind the existing authored truck doors to closed poses; no new meshes."""
import unreal


def configure_truck(truck):
    poses = []
    frame = truck.get_actor_transform()
    for part in truck.get_editor_property('truck_parts'):
        name = part.get_actor_label()
        if not any(token in name for token in ('OpenDoor_', 'DoorLatch_', 'DoorTrim_')):
            continue
        left = 'Left' in name
        # Installed truck dimensions, cm. Rotate panel and latch/trim together
        # from the folded side position across the rear opening. Close the
        # 10.58cm clearance between the exterior folded-door mounting planes.
        hinge = unreal.Vector(-348, -118 if left else 120.58, 208)
        turn = unreal.Rotator(yaw=90 if left else -90)
        local = unreal.MathLibrary.make_relative_transform(part.get_actor_transform(), frame)
        location = hinge + unreal.MathLibrary.rotate_angle_axis(local.translation-hinge, turn.yaw, unreal.Vector(0, 0, 1))
        location.y += 5.29 if left else -5.29
        if 'DoorLatch_' in name:
            location.x = -352  # Keep the latch on the outside rear face.
        rotation = unreal.MathLibrary.compose_rotators(local.rotation.rotator(), turn)
        closed = unreal.Transform(location=location, rotation=rotation, scale=local.scale3d)
        poses.append(unreal.WarehouseTruckDoorPose(part=part, closed_pose=closed))
    truck.set_editor_property('rear_doors', poses)
    return len(poses)


if __name__ == '__main__':
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    trucks = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.WarehouseLogisticsTruck)]
    assert len(trucks) == 3
    for truck in trucks:
        assert configure_truck(truck) == 8, truck.get_actor_label()
    assert level.save_current_level()
    print('TRUCK_DEPARTURE_DOORS_CONFIGURED', len(trucks))
    unreal.SystemLibrary.quit_editor()
