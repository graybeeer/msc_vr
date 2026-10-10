"""Add eight empty Fab rack bays and six physical pallet stacks without rebuilding the map."""
import unreal

LEVEL='/Game/FirstPerson/Lvl_FirstPerson'
PREFIX='WH_Empty'
RACKS=[(0,i,-1590,y) for i,y in enumerate((-1700,-50,500,1150))]
RACKS += [(floor,i,x,1150) for floor in (1,2) for i,x in enumerate((-1550,1550))]
STACKS=[(x,y) for y in (1560,1710,1860) for x in (1650,1850)]
STACK_HEIGHT=6


def configure_empty_storage():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named={a.get_actor_label():a for a in actors.get_all_level_actors()}
    system=named['WH_DamageSystem']
    specs=list(system.get_editor_property('objects'))
    profile_names={s.name for s in specs}
    reference=next(s for s in specs if s.name=='PalletRack_Left_00')

    def setup(actor,label,floor):
        actor.set_actor_label(label)
        actor.set_folder_path('Warehouse/EmptyStorage')
        actor.set_editor_property('is_spatially_loaded',False)
        actor.tags=[unreal.Name('WarehouseFloor'+str(floor)),unreal.Name('EmptyStorage')]
        named[label]=actor
        return actor

    for floor,index,x,y in RACKS:
        prefix=f'{PREFIX}Rack_L{floor+1}_{index}'
        members=[]
        for part in 'ABCDEFGH':
            label=prefix+'_'+part
            actor=named.get(label)
            if not actor:
                template=named['WH_Rack_Left_00_'+part]
                actor=setup(actors.spawn_actor_from_class(unreal.StaticMeshActor,
                    template.get_actor_location()+unreal.Vector(x+215,y+1330,floor*400),
                    template.get_actor_rotation()),label,floor)
                actor.static_mesh_component.set_static_mesh(template.static_mesh_component.static_mesh)
                actor.static_mesh_component.set_collision_profile_name('BlockAll')
                for i in range(template.static_mesh_component.get_num_materials()):
                    actor.static_mesh_component.set_material(i,template.static_mesh_component.get_material(i))
                actor.set_actor_scale3d(template.get_actor_scale3d())
            members.append(actor)
        if prefix not in profile_names:
            specs.append(unreal.WarehouseStrength(name=prefix,members=members,
                physics_meshes=list(reference.physics_meshes),failure=unreal.WarehouseFailure.RACK,
                mass_kg=reference.mass_kg,rated_load_kg=reference.rated_load_kg,
                level_capacity_kg=reference.level_capacity_kg,
                impact_yield_j=reference.impact_yield_j,impact_failure_j=reference.impact_failure_j,
                overload_seconds=reference.overload_seconds,
                shelf_heights_cm=[floor*400+20,floor*400+140]))

    for stack,(x,y) in enumerate(STACKS):
        for tier in range(STACK_HEIGHT):
            label=f'{PREFIX}Pallet_{stack:02}_{tier}'
            pallet=named.get(label)
            if not pallet:
                pallet=setup(actors.spawn_actor_from_class(unreal.WarehousePallet,
                    unreal.Vector(x,y,.1+tier*15.1)),label,0)
                pallet.set_editor_property('pallet_mass_kg',25.)
                pallet.set_editor_property('payload_mass_kg',0.)
            if label not in profile_names:
                specs.append(unreal.WarehouseStrength(name=label,members=[pallet],
                    physics_meshes=[pallet.get_component_by_class(unreal.StaticMeshComponent).static_mesh],
                    failure=unreal.WarehouseFailure.CRUSH,mass_kg=25.,rated_load_kg=1500.,
                    impact_yield_j=120.,impact_failure_j=900.,overload_seconds=10.))
    system.modify()
    system.set_editor_property('objects',specs)
    print('EMPTY_STORAGE_CONFIGURED',len(RACKS),'empty bays;',len(STACKS)*STACK_HEIGHT,'empty pallets')
    return named


if __name__=='__main__':
    import traceback
    try:
        level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        assert level.load_level(LEVEL)
        configure_empty_storage()
        assert level.save_current_level()
        assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
        print('EMPTY_STORAGE_SAVED')
    except Exception:
        print('EMPTY_STORAGE_FAILED',traceback.format_exc())
    unreal.SystemLibrary.quit_editor()
