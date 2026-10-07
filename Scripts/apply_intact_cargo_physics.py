"""Apply intact cartons without changing cargo dimensions, mass, metadata or placements."""
import unreal,zlib
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
meshes=[unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_'+str(i)) for i in (1,2)]
pallet_mesh=unreal.load_asset('/Game/Warehouse/Physics/SM_Pallet_110')
assert all(meshes) and pallet_mesh
count=0;pallets=0
system=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WH_DamageSystem')
specs=list(system.get_editor_property('objects'))
for a in actors.get_all_level_actors():
    if isinstance(a,unreal.WarehouseCargo):
        c=a.get_component_by_class(unreal.StaticMeshComponent)
        old=c.static_mesh.get_bounds().box_extent*2
        scale=a.get_actor_scale3d();dimensions=unreal.Vector(old.x*scale.x,old.y*scale.y,old.z*scale.z)
        center,extent=a.get_actor_bounds(False);mass=a.get_editor_property('gross_mass_kg')
        snapshot=globals().get('cargo_snapshots',{}).get(a.get_actor_label())
        if snapshot:dimensions,center,extent=snapshot
        mesh=meshes[zlib.crc32(a.get_actor_label().encode())%2];size=mesh.get_bounds().box_extent*2
        a.modify();a.set_cargo_mesh(mesh);c.set_editor_property('override_materials',[])
        a.set_actor_scale3d(unreal.Vector(dimensions.x/size.x,dimensions.y/size.y,dimensions.z/size.z))
        new_center,new_extent=a.get_actor_bounds(False)
        target=unreal.Vector(center.x,center.y,center.z-extent.z+new_extent.z)
        a.set_actor_location(a.get_actor_location()+target-new_center,False,True)
        a.set_gross_mass_kg(mass)
        for spec in specs:
            if a in spec.get_editor_property('members'):
                spec.set_editor_property('physics_meshes',[mesh]);break
        count+=1
    elif isinstance(a,unreal.StaticMeshActor) and a.static_mesh_component.static_mesh and 'Storage_Pallet_Wood_Worn_01' in a.static_mesh_component.static_mesh.get_name():
        center,extent=a.get_actor_bounds(False)
        replacement=actors.spawn_actor_from_class(unreal.WarehousePallet,unreal.Vector(center.x,center.y,center.z-extent.z),a.get_actor_rotation())
        label=a.get_actor_label();replacement.set_folder_path(a.get_folder_path());replacement.set_editor_property('is_spatially_loaded',False)
        for spec in specs:
            members=list(spec.get_editor_property('members'))
            if a in members:
                index=members.index(a);members[index]=replacement;spec.set_editor_property('members',members)
                physical=list(spec.get_editor_property('physics_meshes'))
                if index<len(physical):physical[index]=pallet_mesh;spec.set_editor_property('physics_meshes',physical)
        actors.destroy_actor(a);replacement.set_actor_label(label);pallets+=1
    elif isinstance(a,unreal.WarehousePallet):
        a.modify();c=a.get_component_by_class(unreal.StaticMeshComponent)
        c.set_static_mesh(pallet_mesh);c.set_relative_location(unreal.Vector(),False,True)
        c.set_relative_scale3d(unreal.Vector(1,1,1));c.set_collision_profile_name('PhysicsActor')
        for spec in specs:
            if a in spec.get_editor_property('members'):
                spec.set_editor_property('physics_meshes',[pallet_mesh]);break
        pallets+=1
# The small dispatch stack originally sat over an open gap in the pallet deck.
# Place it at the front frame/central board intersection, without filling the gap.
stack=sorted([a for a in actors.get_all_level_actors() if a.get_actor_label().startswith('WH_Stock_5_0_0_')],key=lambda a:a.get_actor_label())
bottom=15.1
for a in stack:
    center,extent=a.get_actor_bounds(False)
    a.modify();a.set_actor_location(a.get_actor_location()+unreal.Vector(1400,1350,bottom+extent.z)-center,False,True)
    bottom+=extent.z*2+.1
system.modify();system.set_editor_property('objects',specs)
assert level.save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
print('INTACT_CARGO_PHYSICS_APPLIED',count,'cartons',pallets,'pallets')
