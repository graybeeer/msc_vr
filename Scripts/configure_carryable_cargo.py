"""Convert existing Fab box/crate actors to cargo with simple box collision."""
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cargo_class = unreal.load_class(None, '/Script/msc_vr.WarehouseCargo')
assert cargo_class

old_cargo = []
existing_cargo = []
for actor in actors.get_all_level_actors():
    label = actor.get_actor_label()
    if not (label.startswith('WH_Cargo_') or label in ('WH_Pickup_Box', 'WH_Drop_Box')):
        continue
    (old_cargo if isinstance(actor, unreal.StaticMeshActor) else existing_cargo).append(actor)

assert len(old_cargo) + len(existing_cargo) == 68
for old in old_cargo:
    label = old.get_actor_label()
    cargo = actors.spawn_actor_from_class(cargo_class, old.get_actor_location(), old.get_actor_rotation())
    assert cargo, label
    cargo.set_actor_scale3d(old.get_actor_scale3d())
    cargo.set_cargo_mesh(old.static_mesh_component.get_editor_property('static_mesh'))
    actors.destroy_actor(old)
    cargo.set_actor_label(label)

assert level.save_current_level()
print('CARRYABLE_CARGO_CONFIGURED', len(old_cargo), 'converted', len(existing_cargo), 'already present')
