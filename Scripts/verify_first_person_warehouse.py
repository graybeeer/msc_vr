import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
labels = [actor.get_actor_label() for actor in actors]
assert labels.count('WH_AutonomousForklift') == 1
assert len([name for name in labels if name.startswith('WH_Rack_')]) == 176
assert len([name for name in labels if name.startswith('WH_Collider_Rack_')]) == 22
assert len([name for name in labels if name.startswith('WH_Collider_LightFixture_')]) == 6
assert 'WH_Roof' in labels and 'WH_LoadingDoor_Header' in labels
assert 'PlayerStart' in labels and 'Floor' in labels
assert not any(name.startswith(('SM_Cube', 'SM_Ramp', 'SM_QuarterCylinder')) for name in labels)
forklift = next(actor for actor in actors if actor.get_actor_label() == 'WH_AutonomousForklift')
assert not forklift.get_editor_property('is_spatially_loaded')
assert forklift.get_component_by_class(unreal.BoxComponent).get_unscaled_box_extent().x == 230
for actor in actors:
    if isinstance(actor, unreal.StaticMeshActor) and (actor.get_actor_label().startswith('WH_') or actor.get_actor_label() == 'Floor'):
        profile = actor.static_mesh_component.get_collision_profile_name()
        assert profile in (('BlockAll', 'BlockAllDynamic') if actor.get_actor_label() == 'Floor' else ('BlockAll',)), (actor.get_actor_label(), profile)
    if actor.get_actor_label().startswith('WH_Collider_'):
        assert not actor.static_mesh_component.is_visible()
print('WAREHOUSE_VERIFIED', len(actors), 'actors', '176 rack parts', '30 collision proxies')
