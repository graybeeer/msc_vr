"""Apply refined native rig defaults without replacing the vehicle or its job references."""
import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets=unreal.EditorAssetLibrary
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
vehicle=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WH_AutonomousForklift')
links={k:vehicle.get_editor_property(k) for k in ('target_pallet','charging_station','elevator','pending_jobs')}
template=actors.spawn_actor_from_class(unreal.WarehouseForklift,unreal.Vector(0,0,-10000))
sources={c.get_name():c for c in template.get_components_by_class(unreal.SceneComponent)}
vehicle.modify()
pose=vehicle.get_actor_transform()
for c in vehicle.get_components_by_class(unreal.SceneComponent):
    if c==vehicle.root_component:continue
    if c.get_name() not in sources:raise AssertionError('Obsolete rig component: '+c.get_name())
    source=sources[c.get_name()];c.modify()
    if isinstance(c,unreal.StaticMeshComponent):
        assert source.static_mesh,c.get_name()
        c.set_static_mesh(source.static_mesh)
        c.set_editor_property('override_materials',[])
        c.set_collision_enabled(source.get_collision_enabled())
        if c.get_name() in ('ForkL','ForkR'):
            unreal.WarehouseForklift.configure_fork_collision(c.static_mesh)
            assert assets.save_loaded_asset(c.static_mesh)
    c.set_relative_transform(source.get_relative_transform(),False,True)
actors.destroy_actor(template)
assert vehicle.get_actor_transform()==pose,'Vehicle placement changed'
station=vehicle.get_editor_property('charging_station')
station_pose=station.get_actor_transform()
reference=actors.spawn_actor_from_class(unreal.WarehouseChargingStation,unreal.Vector(0,0,-10000))
defaults={c.get_name():c for c in reference.get_components_by_class(unreal.SceneComponent)}
station.modify()
for c in station.get_components_by_class(unreal.SceneComponent):
    if c==station.root_component:continue
    c.modify();c.set_relative_transform(defaults[c.get_name()].get_relative_transform(),False,True)
actors.destroy_actor(reference)
assert station.get_actor_transform()==station_pose
for k,value in links.items():assert vehicle.get_editor_property(k)==value,k
assert level.save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
print('REFINED_AGV_APPLIED',vehicle.get_actor_bounds(True),'jobs and links preserved')
