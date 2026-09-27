"""Validate the saved warehouse collision configuration and training setup."""
import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
meshes=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
labels=[a.get_actor_label() for a in actors]
assert labels.count('WH_AutonomousForklift')==1
assert len([s for s in labels if s.startswith('WH_Rack_')])==176
assert not any(s.startswith('WH_Collider_') for s in labels)
cargo=[a for a in actors if isinstance(a,unreal.WarehouseCargo)]
assert len(cargo)==140,len(cargo)
for a in cargo:
    c=a.get_component_by_class(unreal.StaticMeshComponent)
    mesh=c.get_editor_property('static_mesh')
    assert c.get_collision_profile_name()=='PhysicsActor'
    assert meshes.get_convex_collision_count(mesh)>0,mesh.get_path_name()
    assert meshes.get_collision_complexity(mesh)==unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX
vehicle=next(a for a in actors if isinstance(a,unreal.WarehouseForklift))
assert not vehicle.get_editor_property('powered')
assert vehicle.get_editor_property('target_pallet')
assert not vehicle.get_editor_property('is_spatially_loaded')
assert vehicle.get_actor_up_vector().z>.999, 'Vehicle must stand upright'
assert vehicle.get_editor_property('target_pallet').get_actor_up_vector().z>.999
for a in actors:
    if isinstance(a,unreal.StaticMeshActor) and a.get_actor_label().startswith('WH_'):
        c=a.static_mesh_component
        assert c.get_collision_profile_name()=='BlockAll'
        mesh=c.get_editor_property('static_mesh')
        if mesh.get_path_name().startswith('/Game/Warehouse/Collision/'):
            assert meshes.get_collision_complexity(mesh)==unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
for actor in (vehicle,vehicle.get_editor_property('target_pallet')):
    assert all(c.get_collision_profile_name()=='BlockAllDynamic' for c in actor.get_components_by_class(unreal.StaticMeshComponent))
    assert all(c.get_material(0).get_path_name().startswith('/Game/Warehouse/Materials/') for c in actor.get_components_by_class(unreal.StaticMeshComponent)), 'Training materials must survive reload'
floor=next(a for a in actors if a.get_actor_label()=='Floor')
assert 'Floor_Concrete' in floor.static_mesh_component.get_material(0).get_path_name()
print('WAREHOUSE_VERIFIED',len(actors),'actors, 140 convex cargo, no bounding proxies')
