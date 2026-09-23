"""Mark existing warehouse boxes/crates as movable carry targets."""
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()

count = 0
for actor in actors:
    label = actor.get_actor_label()
    if not (label.startswith('WH_Cargo_') or label in ('WH_Pickup_Box', 'WH_Drop_Box')):
        continue
    assert isinstance(actor, unreal.StaticMeshActor), label
    component = actor.static_mesh_component
    component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    tags = list(actor.get_editor_property('tags'))
    if unreal.Name('Carryable') not in tags:
        tags.append(unreal.Name('Carryable'))
        actor.set_editor_property('tags', tags)
    count += 1

assert count == 68, f'Expected 66 rack cargo and 2 loose boxes, found {count}'
assert level.save_current_level()
print('CARRYABLE_CARGO_CONFIGURED', count)
