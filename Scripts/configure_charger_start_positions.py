"""Author the two existing AGV start poses at their assigned charging bays.

Editor-only, before PIE. Preserves actor identities, models, jobs and stations;
does not move a running vehicle or rebuild the warehouse.
"""
import unreal

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'


def configure_charger_start_positions():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert editor.get_game_world() is None, 'End PIE before changing authored start positions'
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    vehicles = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.WarehouseForklift)]
    assert len(vehicles) == 2, ('Expected the two existing warehouse AGVs', len(vehicles))
    pairs = []
    for vehicle in vehicles:
        station = vehicle.get_editor_property('charging_station')
        assert isinstance(station, unreal.WarehouseChargingStation), ('Missing assigned charging bay', vehicle.get_actor_label())
        assert station.get_editor_property('assigned_vehicle') == vehicle, ('Inconsistent charging association', vehicle.get_actor_label())
        assert station not in [s for _, s in pairs], 'The two initial AGVs require separate charging bays'
        assert not vehicle.get_editor_property('powered'), 'Do not move a powered editor vehicle'
        assert abs(station.get_actor_location().z) < .1, 'Initial warehouse fleet must remain on floor 1'
        pairs.append((vehicle, station))
    for vehicle, station in pairs:
        previous = vehicle.get_actor_transform()
        def depth(part):
            value = 0
            while part.get_attach_parent():
                part = part.get_attach_parent()
                value += 1
            return value
        original_scale = vehicle.get_actor_scale3d()
        parts = [(part, unreal.MathLibrary.make_relative_transform(part.get_world_transform(), previous), part.get_world_scale())
                 for part in sorted(vehicle.get_components_by_class(unreal.SceneComponent), key=depth)
                 if part != vehicle.root_component]
        target = unreal.Transform(location=station.get_actor_location(), rotation=station.get_actor_rotation(),
                                  scale=original_scale)
        vehicle.modify()
        assert vehicle.set_actor_transform(target, False, True)
        for part, local, original_part_scale in parts:
            part.modify()
            part.set_world_transform(unreal.MathLibrary.compose_transforms(local, target), False, True)
            assert (part.get_world_scale() - original_part_scale).length() < .000001, ('Model part scale changed', part.get_name())
            actual_local = unreal.MathLibrary.make_relative_transform(part.get_world_transform(), target)
            assert unreal.MathLibrary.nearly_equal_transform_transform(actual_local, local, .001, .000001, .000001), ('Model part actor-local pose changed', part.get_name())
        assert (vehicle.get_actor_scale3d() - original_scale).length() < .000001, 'Vehicle model scale changed'
        assert station.is_docked(vehicle), 'Authored vehicle pose does not match its assigned dock'
        print('CHARGER_START_POSITION', vehicle.get_actor_label(), 'from', previous.translation,
              'to', vehicle.get_actor_location(), 'bay', station.get_actor_label(), 'model parts preserved', len(parts))


if __name__ == '__main__':
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    configure_charger_start_positions()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
    print('CHARGER_START_POSITIONS_SAVED', '2 existing AGVs; no runtime relocation')
    unreal.SystemLibrary.quit_editor()
