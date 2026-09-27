"""Upgrade warehouse cargo to generated convex collision meshes; preserves placements."""
import os
import sys
import unreal
sys.path.insert(0, os.path.dirname(__file__))
from prepare_warehouse_assets import collision_mesh

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
count=0
for old in actors.get_all_level_actors():
    label=old.get_actor_label()
    if not (label.startswith(('WH_Cargo_','WH_Stock_')) or label in ('WH_Pickup_Box','WH_Drop_Box')):
        continue
    comp=old.get_component_by_class(unreal.StaticMeshComponent)
    mesh=comp.get_editor_property('static_mesh')
    if isinstance(old,unreal.WarehouseCargo):
        cargo=old
    else:
        cargo=actors.spawn_actor_from_class(unreal.WarehouseCargo,old.get_actor_location(),old.get_actor_rotation())
        cargo.set_actor_scale3d(old.get_actor_scale3d())
        actors.destroy_actor(old)
        cargo.set_actor_label(label)
    cargo.set_cargo_mesh(collision_mesh(mesh,True))
    count+=1
assert level.save_current_level()
print('CARRYABLE_CARGO_CONFIGURED',count)
