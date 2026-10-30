"""Author the two existing AGV start poses at their assigned charging bays.

Editor-only, before PIE. Preserves actor identities, models, jobs and stations;
does not move a running vehicle or rebuild the warehouse.
"""
import unreal

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'


def move_authored_actor(actor, target):
    """Keep detached wheels and every model part in the same actor-local pose."""
    previous=actor.get_actor_transform()
    def depth(part):
        value=0
        while part.get_attach_parent():
            part=part.get_attach_parent()
            value+=1
        return value
    parts=[(part,unreal.MathLibrary.make_relative_transform(part.get_world_transform(),previous),part.get_world_scale())
           for part in sorted(actor.get_components_by_class(unreal.SceneComponent),key=depth) if part!=actor.root_component]
    actor.modify()
    assert actor.set_actor_transform(target,False,True)
    for part,local,scale in parts:
        part.modify()
        part.set_world_transform(unreal.MathLibrary.compose_transforms(local,target),False,True)
        assert (part.get_world_scale()-scale).length()<.000001,('Model scale changed',part.get_name())
        actual=unreal.MathLibrary.make_relative_transform(part.get_world_transform(),target)
        assert unreal.MathLibrary.nearly_equal_transform_transform(actual,local,.001,.000001,.000001),part.get_name()


def configure_wall_chargers():
    """Initial map authoring only. Two free gaps between the east-wall racks."""
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named={a.get_actor_label():a for a in actors.get_all_level_actors()}
    wall=unreal.WarehouseDamageSystem.get_physical_bounds(named['WH_Wall_E'])
    for label,y in [('WH_ChargingStation',-1350),('WH_ChargingStation_02',-650)]:
        station=named[label]
        pose=unreal.Transform(location=unreal.Vector(station.get_actor_location().x,y,0),rotation=unreal.Rotator(yaw=180),scale=station.get_actor_scale3d())
        move_authored_actor(station,pose)
        # Measure the actual cabinet back, rather than treating the dock root as the wall mount.
        back=unreal.WarehouseDamageSystem.get_physical_bounds(station).max.x
        pose.translation+=unreal.Vector(wall.min.x-5-back,0,0)
        move_authored_actor(station,pose)
        assert abs(wall.min.x-unreal.WarehouseDamageSystem.get_physical_bounds(station).max.x-5)<.01
    label=named.get('WH_AreaUI_Service')
    if label:
        label.modify()
        label.set_actor_location(unreal.Vector(named['WH_ChargingStation'].get_actor_location().x,-1000,260),False,True)


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
        original_scale = vehicle.get_actor_scale3d()
        target = unreal.Transform(location=station.get_actor_location(), rotation=station.get_actor_rotation(),
                                  scale=original_scale)
        move_authored_actor(vehicle,target)
        assert (vehicle.get_actor_scale3d() - original_scale).length() < .000001, 'Vehicle model scale changed'
        assert station.is_docked(vehicle), 'Authored vehicle pose does not match its assigned dock'
        print('CHARGER_START_POSITION', vehicle.get_actor_label(), 'from', previous.translation,
              'to', vehicle.get_actor_location(), 'bay', station.get_actor_label(), 'model parts preserved')


if __name__ == '__main__':
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    configure_wall_chargers()
    configure_charger_start_positions()
    assert level.save_current_level()
    print('CHARGER_START_POSITIONS_SAVED', '2 existing AGVs; no runtime relocation')
    unreal.SystemLibrary.quit_editor()
