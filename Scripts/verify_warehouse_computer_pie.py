"""Native console UI/guards on the saved warehouse or actual transport completion in a small fixture. Never saves."""
import sys,time,traceback
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from configure_warehouse_computer import COMPUTER_POSITION,DESTINATIONS
from apply_real_world_scale import fit
SAVED='-computersavedmap' in unreal.SystemLibrary.get_command_line().lower()
SPEED=4 if '-computerspeed4' in unreal.SystemLibrary.get_command_line().lower() else 1
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
computer=named['WH_ManagementComputer']
assert sum(isinstance(a,unreal.WarehouseComputer) for a in named.values())==1
assert (computer.get_actor_location()-unreal.Vector(*COMPUTER_POSITION)).length()<.1
assert abs(computer.get_actor_rotation().yaw-90)<.1, 'Computer must face the outside apron'
centre,extent=computer.get_actor_bounds(True)
wall_centre,wall_extent=named['EXT_FrontCladding_1350'].get_actor_bounds(True)
assert centre.x-extent.x>800, 'Workstation blocks the loading doorway'
assert centre.y+extent.y<wall_centre.y-wall_extent.y, 'Workstation intersects the exterior wall'
assert len(computer.get_components_by_class(unreal.StaticMeshComponent))==12
assert len(computer.get_editor_property('outbound_destinations'))==2
for index,p in enumerate(DESTINATIONS):
    assert (computer.get_editor_property('outbound_destinations')[index].translation-unreal.Vector(*p)).length()<.1
assert any(s.name=='WH_ManagementComputer' and s.mass_kg==80 for s in named['WH_DamageSystem'].get_editor_property('objects'))
print('WAREHOUSE_COMPUTER_DISK_PASSED','saved workstation, collision, two outbound targets, strength profile')

def spawn(cls,label,p,yaw=0):
    a=actors.spawn_actor_from_class(cls,unreal.Vector(*p),unreal.Rotator(yaw=yaw))
    a.set_actor_label(label);a.set_editor_property('is_spatially_loaded',False);return a
if not SAVED:
    rig_data={c.get_name():(c.get_relative_transform(),c.static_mesh,list(c.get_editor_property('override_materials')))
        for c in named['WH_AutonomousForklift'].get_components_by_class(unreal.StaticMeshComponent)}
    assert level.load_level('/Engine/Maps/Entry')
    for a in list(actors.get_all_level_actors()):
        if not isinstance(a,(unreal.WorldSettings,unreal.LevelScriptActor)):actors.destroy_actor(a)
    world=editor.get_editor_world()
    unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WorldSettings)[0].set_editor_property('default_game_mode',
        unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
    floor=spawn(unreal.StaticMeshActor,'ComputerTestFloor',(0,0,-10));floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.set_actor_scale3d(unreal.Vector(50,25,.2));floor.static_mesh_component.set_collision_profile_name('BlockAll')
    spawn(unreal.PlayerStart,'ComputerTestStart',(-950,-700,96.5))
    comp=spawn(unreal.WarehouseComputer,'ComputerTestConsole',(-700,-700,0))
    comp.set_editor_property('outbound_centre',unreal.Vector(1200,0,0));comp.set_editor_property('outbound_half_size',unreal.Vector(150,350,80))
    comp.set_editor_property('outbound_destinations',[unreal.Transform(location=unreal.Vector(1200,y,0)) for y in (0,220)])
    vehicle=spawn(unreal.WarehouseForklift,'ComputerTestAGV',(0,0,0));vehicle.set_editor_property('auto_start',True)
    for c in vehicle.get_components_by_class(unreal.StaticMeshComponent):
        transform,mesh,materials=rig_data[c.get_name()];c.set_relative_transform(transform,False,True)
        c.set_static_mesh(mesh);c.set_editor_property('override_materials',materials)
    specs=[]
    for index,y in enumerate((0,220)):
        pallet=spawn(unreal.WarehousePallet,f'ComputerTestPallet{index}',(500,y,.1))
        cargo=spawn(unreal.WarehouseCargo,f'ComputerTestCargo{index}',(500,y,30.2))
        cargo.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2'))
        recipe=unreal.WarehouseCargo.generate_cargo_recipe(20261011,index,0,unreal.Vector(40,30,30),1)
        assert cargo.apply_cargo_recipe(recipe);fit(cargo,(40,30,30),(500,y,30.2))
        specs += [unreal.WarehouseStrength(name=pallet.get_actor_label(),members=[pallet],mass_kg=25,rated_load_kg=1500),
            unreal.WarehouseStrength(name=cargo.get_actor_label(),members=[cargo],mass_kg=cargo.get_editor_property('gross_mass_kg'),rated_load_kg=120)]
    damage=spawn(unreal.WarehouseDamageSystem,'ComputerTestStrength',(0,-1000,0));damage.set_editor_property('objects',specs)
    spawn(unreal.DirectionalLight,'ComputerTestSun',(0,0,600),-25)
unreal.SystemLibrary.execute_console_command(editor.get_editor_world(),'t.OverrideFPS 0')
started=time.monotonic();phase='settle';phase_at=0.;live=None;pc=None;pawn=None;comp=None;vehicle=None;cargo=None;job_id=None;refresh_start=None;entry_pose=None;last_log=-10;cancelled_elapsed=None;completion_refresh=None
def enter(value,now):
    global phase,phase_at
    phase=value;phase_at=now
def finish(ok):
    print('WAREHOUSE_COMPUTER_'+('SAVED' if SAVED else 'FIXTURE')+'_PIE_'+('PASSED' if ok else 'FAILED'))
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()
def tick(dt):
    global live,pc,pawn,comp,vehicle,cargo,job_id,refresh_start,entry_pose,last_log,cancelled_elapsed,completion_refresh
    try:
        assert time.monotonic()-started<1200,'Computer wall timeout'
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            pc=unreal.GameplayStatics.get_player_controller(game,0);pawn=pc.get_controlled_pawn()
            clocks=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseSimulationClock)
            assert len(clocks)==1 and clocks[0].set_speed(SPEED),'Computer test speed setup rejected'
            comp=live['WH_ManagementComputer' if SAVED else 'ComputerTestConsole']
            vehicle=live['WH_AutonomousForklift_02' if SAVED else 'ComputerTestAGV']
            cargo=live['WH_Drop_Box' if SAVED else 'ComputerTestCargo0']
            unreal.SystemLibrary.execute_console_command(game,'DisableAllScreenMessages',pc)
        if now-last_log>10:
            last_log=now;print('WAREHOUSE_COMPUTER_PROGRESS',round(now,1),phase,vehicle.get_editor_property('status'))
        age=now-phase_at
        if phase=='settle':
            if now<4:return
            inventory=comp.get_editor_property('inventory')
            assert len(inventory)==(360 if SAVED else 2)
            assert len({r.cargo_id for r in inventory})==len(inventory)
            row=next(r for r in inventory if r.cargo==cargo);assert row.pallet is not None and row.mass_kg>0
            if SAVED:
                assert not pc.open_warehouse_computer(comp),'Computer opened out of range'
                pawn.set_actor_location(comp.get_actor_location()-comp.get_actor_forward_vector()*210+unreal.Vector(0,0,96.5),False,True)
            camera=pawn.get_components_by_class(unreal.CameraComponent)[0]
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_world_location(),comp.get_actor_location()+unreal.Vector(0,0,111)))
            enter('aim',now);return
        if phase=='aim':
            if age<.5:return
            assert pawn.get_interaction_target()==comp,('Computer not selected',pawn.get_interaction_target())
            pawn.toggle_carry()
            assert pc.is_warehouse_computer_open() and pc.is_move_input_ignored() and pc.is_look_input_ignored()
            assert not unreal.GameplayStatics.is_game_paused(game),'Computer paused the simulation'
            assert pc.get_view_target()==pawn and pc.get_controlled_pawn()==pawn
            refresh_start=comp.get_editor_property('refresh_count');entry_pose=pawn.get_actor_location()
            enter('open',now);return
        if phase=='open':
            if age<2:return
            assert comp.get_editor_property('refresh_count')>refresh_start,'Inventory stopped updating while console was open'
            assert (pawn.get_actor_location()-entry_pose).length()<10,'Operator moved while using computer'
            if SAVED:
                unreal.SystemLibrary.execute_console_command(game,'Shot showui filename=C:/msc_UnrealProject/msc_vr/Saved/Testing/WarehouseComputer_UI.png -nosuffix',pc)
            # Invalid load, repeated request and destination reservation all use the same real UI backend.
            rated=vehicle.get_editor_property('rated_load_kg');vehicle.set_editor_property('rated_load_kg',1)
            assert not comp.issue_outbound(cargo,vehicle,0),'Overweight order accepted'
            vehicle.set_editor_property('rated_load_kg',rated)
            assert comp.issue_outbound(cargo,vehicle,0),comp.get_editor_property('last_message')
            job=next(j for j in vehicle.get_editor_property('pending_jobs') if j.source_system=='FMS-COMPUTER');job_id=job.job_id
            task=next(t for t in comp.get_editor_property('tasks') if t.job_id==job_id)
            assert task.submission_time_known and abs(task.submitted_simulation_seconds-now)<.1,'Submission clock was not captured immediately'
            assert sum(t.job_id==job_id for t in comp.get_editor_property('tasks'))==1
            assert not comp.issue_outbound(cargo,vehicle,0),'Duplicate pallet accepted'
            if not SAVED:assert not comp.issue_outbound(live['ComputerTestCargo1'],vehicle,0),'Destination double reserved'
            assert comp.set_vehicle_running(vehicle,False),'Could not hold a queued order'
            enter('queue_timing',now);return
        if phase=='queue_timing':
            if age<2:return
            task=next(t for t in comp.get_editor_property('tasks') if t.job_id==job_id)
            assert not task.completed and not task.cancelled
            elapsed=comp.get_task_elapsed_seconds(job_id)
            assert elapsed>=2 and abs(elapsed-(now-task.submitted_simulation_seconds))<.01,'Queue wait not included in simulation elapsed'
            assert comp.cancel_queued_job(job_id),'Queued console order could not cancel'
            assert not any(j.job_id==job_id for j in vehicle.get_editor_property('pending_jobs'))
            task=next(t for t in comp.get_editor_property('tasks') if t.job_id==job_id)
            assert task.cancelled and not task.completed and abs(task.finished_simulation_seconds-now)<.01
            cancelled_elapsed=comp.get_task_elapsed_seconds(job_id)
            assert comp.set_vehicle_running(vehicle,True),'Could not resume after timing check'
            enter('cancelled_timing',now);return
        if phase=='cancelled_timing':
            if age<1:return
            assert abs(comp.get_task_elapsed_seconds(job_id)-cancelled_elapsed)<.001,'Cancelled duration kept growing'
            assert not comp.cancel_queued_job('unknown-job')
            print('WAREHOUSE_COMPUTER_QUEUE_TIMING_PASSED','immediate dispatch capture, powered-off queue wait, unique record, cancellation duration frozen')
            print('WAREHOUSE_COMPUTER_GUARDS_PASSED','overweight, duplicate source, queue cancellation, destination reservation (fixture)')
            if SAVED:
                assert comp.set_vehicle_running(vehicle,False) and not vehicle.get_editor_property('powered')
                assert comp.set_vehicle_running(vehicle,True) and vehicle.get_editor_property('powered')
                enter('close',now);return
            assert comp.issue_outbound(cargo,vehicle,0)
            job_id=next(j.job_id for j in vehicle.get_editor_property('pending_jobs') if j.source_system=='FMS-COMPUTER')
            enter('transport',now);return
        if phase=='transport':
            assert not vehicle.get_editor_property('mechanical_failure')
            assert vehicle.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,vehicle.get_editor_property('status')
            if job_id in vehicle.get_editor_property('completed_job_ids'):
                if not any(t.job_id==job_id and t.completed for t in comp.get_editor_property('tasks')):return
                task=next(t for t in comp.get_editor_property('tasks') if t.job_id==job_id)
                completed=next(r for r in vehicle.get_editor_property('job_reports') if r.job_id==job_id and r.result=='COMPLETED')
                assert task.submission_time_known and abs(task.finished_simulation_seconds-completed.simulation_seconds)<.001,'Completion timestamp was polled instead of reported'
                assert abs(comp.get_task_elapsed_seconds(job_id)-(completed.simulation_seconds-task.submitted_simulation_seconds))<.001
                assert task.elapsed_simulation_seconds>0
                print('WAREHOUSE_COMPUTER_COMPLETION_TIMING_PASSED',round(task.elapsed_simulation_seconds,2),'simulation seconds, dispatch-to-report including queue')
                completion_refresh=comp.get_editor_property('refresh_count')
                enter('completion_inventory',now);return
            if age>150:raise AssertionError('Native computer transport timeout')
        if phase=='completion_inventory':
            # Completion is an immediate delegate; physical inventory intentionally polls once per second.
            if comp.get_editor_property('refresh_count')<=completion_refresh:
                assert age<1.25,'Inventory failed to refresh within its advertised one-second interval'
                return
            row=next(r for r in comp.get_editor_property('inventory') if r.cargo==cargo)
            assert row.location=='출고 대기 구역' and not row.reserved and row.pallet is not None,(row.location,row.reserved,row.pallet)
            assert (row.pallet.get_actor_location()-unreal.Vector(1200,0,0)).length()<3
            task=next(t for t in comp.get_editor_property('tasks') if t.job_id==job_id)
            assert abs(comp.get_task_elapsed_seconds(job_id)-task.elapsed_simulation_seconds)<.001,'Completed duration changed during the inventory refresh'
            print('WAREHOUSE_COMPUTER_COMPLETE_PASSED','native AI insertion, lift, transport, unload, completion report and physical inventory-location update')
            enter('close',now);return
        if phase=='close':
            if age<1:return
            pc.close_warehouse_computer()
            assert not pc.is_warehouse_computer_open() and not pc.is_move_input_ignored() and not pc.is_look_input_ignored()
            assert pc.get_controlled_pawn()==pawn and pc.get_view_target()==pawn
            print('WAREHOUSE_COMPUTER_VIEW_RETURN_PASSED')
            if SAVED:assert Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/WarehouseComputer_UI.png').is_file()
            finish(True)
    except Exception:
        print(traceback.format_exc());finish(False)
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
