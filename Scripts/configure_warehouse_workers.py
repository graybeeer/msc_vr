"""Place seven stationary workers using the player mannequin; preserves existing worker positions."""
import unreal

LEVEL='/Game/FirstPerson/Lvl_FirstPerson'
PLAYER='/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'
# Positions are feet positions in centimetres. Keep the central and right AGV lanes clear.
WORKERS=(
    ('Receiving','입고·검수',(760,1500,0),180),
    ('Picking','피킹',(-1030,-700,0),180),
    ('Packing','포장·출고 준비',(-720,-1390,0),130),
    ('Loading','상하차 담당',(-450,-2280,0),-90),
    ('Control','운영·관제',(-1280,1600,0),45),
    ('Inventory','재고 관리',(-1020,1060,0),-140),
    ('Maintenance','유지보수',(1320,-1500,0),180),
)

def configure_workers():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named={a.get_actor_label():a for a in actors.get_all_level_actors()}
    player=unreal.load_class(None,PLAYER)
    assert player
    result=[]
    for key,role,feet,yaw in WORKERS:
        label='WH_Worker_'+key
        worker=named.get(label)
        created=worker is None
        if created:
            worker=actors.spawn_actor_from_class(unreal.WarehouseWorker,unreal.Vector())
            worker.set_actor_label(label)
        assert isinstance(worker,unreal.WarehouseWorker),label+' is not a worker'
        worker.modify()
        assert worker.copy_player_appearance(player),'Player world mesh is missing'
        worker.set_editor_property('role_name',unreal.Text(role))
        worker.set_folder_path('Warehouse/Workers')
        worker.set_editor_property('is_spatially_loaded',False)
        if created:
            half=worker.get_component_by_class(unreal.CapsuleComponent).get_unscaled_capsule_half_height()
            worker.set_actor_location(unreal.Vector(feet[0],feet[1],feet[2]+half+.5),False,True)
            worker.set_actor_rotation(unreal.Rotator(yaw=yaw),True)
        assert worker.has_standing_clearance(),'Worker overlaps a prop: '+label
        result.append(worker)
        print('WORKER_PLACED',role,worker.get_actor_location())
    return result

if __name__=='__main__':
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    configure_workers()
    assert level.save_current_level()
    print('WAREHOUSE_WORKERS_CONFIGURED',len(WORKERS))
