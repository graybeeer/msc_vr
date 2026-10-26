"""Saved-warehouse regression: concurrent jobs, E pallet stacking and native physics. Never saves."""
import unreal,time,json,math,traceback
from pathlib import Path

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
hands_only='-fleethandsonly' in unreal.SystemLibrary.get_command_line().lower()
parking_only='-fleetparkingonly' in unreal.SystemLibrary.get_command_line().lower()
simulation_speed=4 if '-fleetsimulationspeed4' in unreal.SystemLibrary.get_command_line().lower() else 1
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
if hands_only:
    for a in actors.get_all_level_actors():
        if isinstance(a,unreal.WarehouseForklift):a.set_editor_property('auto_start',False)
root=Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/FleetWorkflow');root.mkdir(exist_ok=True)
started=time.monotonic();phase='start';at=0;game=None;live=None;pc=None;pawn=None;clock=0;walk_points=[];next_phase=None;walk_at=0;sample_at=-10;job_id=None;rack_job_id=None;held=None;base=None;base_start=None;done_at=None
report={'hands_only':hands_only,'parking_only':parking_only,'requested_speed':simulation_speed,'events':[],'samples':[]};failure_detail=None;parking_done_at=None

def pos(v):return [round(v.x,2),round(v.y,2),round(v.z,2)]
def event(name,**data):
    row={'t':round(clock,2),'event':name,**data};report['events'].append(row);print('FLEET_CHECK',json.dumps(row,ensure_ascii=False))
def enter(value):
    global phase,at
    phase=value;at=clock;event('phase',value=value)
def walk(points,value):
    global walk_points,next_phase,walk_at
    walk_points=list(points);next_phase=value;walk_at=clock;enter('walk')
def aim(point):
    camera=pawn.get_component_by_class(unreal.CameraComponent)
    pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_world_location(),point))
def shot(name):unreal.SystemLibrary.execute_console_command(game,f'Shot showui filename={root.as_posix()}/{name}.png -nosuffix',pc)
def finish(ok,detail):
    if live:
        timings=[]
        for label in ('WH_AutonomousForklift','WH_AutonomousForklift_02'):
            v=live[label];accepted={}
            for r in v.get_editor_property('job_reports'):
                if r.result=='ACCEPTED':accepted[r.job_id]=r.simulation_seconds
                if r.result=='COMPLETED' and r.job_id in accepted:
                    timings.append({'vehicle':v.get_editor_property('vehicle_name'),'job':r.job_id,'direction':'rack_to_outbound' if r.job_id==rack_job_id else 'dock_to_outbound' if r.job_id.startswith('CONSOLE-') else 'inbound_to_rack','accepted':round(accepted[r.job_id],2),'completed':round(r.simulation_seconds,2),'seconds':round(r.simulation_seconds-accepted[r.job_id],2)})
        report['completed_job_timings']=timings
        print('FLEET_TIMINGS',json.dumps(timings,ensure_ascii=False))
    report.update(ok=ok,detail=detail,simulation_seconds=round(clock,2),wall_seconds=round(time.monotonic()-started,2))
    (root/('ParkingReport.json' if parking_only else 'HandsReport.json' if hands_only else 'FullReport.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('FLEET_WORKFLOW_PASSED' if ok else 'FLEET_WORKFLOW_FAILED',detail)
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def tick(dt):
    global game,live,pc,pawn,clock,sample_at,job_id,rack_job_id,held,base,base_start,done_at,failure_detail,parking_done_at
    try:
        assert time.monotonic()-started<1200,'Wall-clock timeout'
        game=editor.get_game_world()
        if not game:return
        clock=unreal.GameplayStatics.get_time_seconds(game)
        if phase=='failure_capture':
            if clock-at>.5:finish(False,failure_detail)
            return
        if live is None:
            live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            if parking_only:
                for dispatcher in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseInboundZone):dispatcher.set_actor_tick_enabled(False)
            pawn=unreal.GameplayStatics.get_player_character(game,0);pc=unreal.GameplayStatics.get_player_controller(game,0)
            clocks=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseSimulationClock)
            assert len(clocks)==1 and clocks[0].set_speed(simulation_speed),'Simulation speed setup rejected'
            unreal.SystemLibrary.execute_console_command(game,'DisableAllScreenMessages',pc)
            (root/'ActorNames.json').write_text(json.dumps({a.get_name():label for label,a in live.items()},ensure_ascii=False),encoding='utf-8')
            event('startup',cargo=len(unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseCargo)),pallets=len(unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehousePallet)))
        fleet=[live['WH_AutonomousForklift'],live['WH_AutonomousForklift_02']]
        if clock-sample_at>=5:
            sample_at=clock
            row={'t':round(clock,2),'phase':phase,'player':pos(pawn.get_actor_location()),'fleet':[{'name':v.get_editor_property('vehicle_name'),'state':str(v.get_editor_property('ai_state')),'status':v.get_editor_property('status'),'pos':pos(v.get_actor_location()),'yaw':v.get_actor_rotation().yaw,'tail':[{'p':pos(r.pose.translation),'yaw':r.pose.rotation.rotator().yaw,'reverse':r.get_editor_property('bReverse'),'pivot':r.get_editor_property('bPivot')} for r in list(v.get_editor_property('planned_route'))[-3:]],'speed':v.get_editor_property('current_speed_cm'),'jobs':list(v.get_editor_property('completed_job_ids')),'traffic_wait':v.get_editor_property('traffic_waiting'),'yielding':v.get_editor_property('traffic_yielding')} for v in fleet],'inbound_done':live['WH_InboundDispatch'].get_editor_property('completed_count')}
            report['samples'].append(row);print('FLEET_SAMPLE',json.dumps(row,ensure_ascii=False))
            if held:event('pallet_physics',held_pos=pos(held.get_actor_location()),held_rot=str(held.get_actor_rotation()),base_pos=pos(base.get_actor_location()),still_held=pawn.get_held_cargo()==held)
        assert not pawn.is_physical_ragdoll(),'Player lost balance'
        for v in fleet:
            assert v.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,v.get_editor_property('vehicle_name')+': '+v.get_editor_property('status')
        age=clock-at
        if parking_only:
            parked=True
            for vehicle in fleet:
                bay=vehicle.get_editor_property('waiting_bay');assert bay,'Saved vehicle has no assigned ordinary waiting bay'
                tolerance=bay.get_editor_property('position_tolerance_cm')
                angle=abs(unreal.MathLibrary.normalized_delta_rotator(vehicle.get_actor_rotation(),bay.get_actor_rotation()).yaw)
                parked &= vehicle.get_editor_property('ai_state')==unreal.WarehouseAIState.READY and (vehicle.get_actor_location()-bay.get_actor_location()).length()<=tolerance and angle<=bay.get_editor_property('heading_tolerance_degrees') and abs(vehicle.get_editor_property('current_speed_cm'))<1
                assert vehicle.get_editor_property('battery_percent')>98 and not vehicle.get_editor_property('pending_jobs'),'Idle parking unexpectedly charged or acquired a job'
            if parked:
                if parking_done_at is None:parking_done_at=clock
                if clock-parking_done_at>=8:finish(True,'Saved warehouse: both vehicles physically drove from their chargers to distinct ordinary waiting bays and maintained READY / finite braking for 8 simulation seconds');return
            else:parking_done_at=None
            assert clock<240,'Both vehicles failed to settle at their ordinary waiting bays'
            return
        if phase=='start':
            if clock<2:return
            shot('Start')
            if hands_only:enter('start_hands')
            else:walk([(545,-2150,0),(1050,-2275,0)],'computer_aim')
            return
        if phase=='walk':
            if held:assert pawn.get_held_cargo()==held,'Pallet grip slipped while walking/turning'
            goal=unreal.Vector(*walk_points[0]);diff=goal-pawn.get_actor_location();diff.z=0
            if diff.length()<40:
                walk_points.pop(0)
                if not walk_points:enter(next_phase);return
                goal=unreal.Vector(*walk_points[0]);diff=goal-pawn.get_actor_location();diff.z=0
            pc.set_control_rotation(unreal.Rotator(yaw=math.degrees(math.atan2(diff.y,diff.x))));pawn.do_move(0,1)
            assert clock-walk_at<25,f'Player path blocked toward {goal}, actual {pawn.get_actor_location()}'
            return
        if phase=='computer_aim':
            if age<.8:return
            aim(live['WH_ManagementComputer'].get_actor_location()+unreal.Vector(0,0,111));enter('computer_enter');return
        if phase=='computer_enter':
            if age<.5:return
            assert pawn.get_interaction_target()==live['WH_ManagementComputer'],'Computer target missed'
            pawn.toggle_carry();assert pc.is_warehouse_computer_open(),'E failed to open console'
            enter('computer');return
        if phase=='computer':
            if age<2:return
            shot('Computer');enter('computer_submit');return
        if phase=='computer_submit':
            if age<.4:return
            comp=live['WH_ManagementComputer']
            expected={a.get_path_name() for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseCargo)}
            inventory=comp.get_editor_property('inventory')
            assert len(inventory)==len(expected) and {row.cargo.get_path_name() for row in inventory}==expected,'Console inventory must include every saved-map cargo exactly once'
            assert comp.issue_outbound(live['WH_Drop_Box'],None,0),comp.get_editor_property('last_message')
            job_id=[t.job_id for t in comp.get_editor_property('tasks') if t.job_id.startswith('CONSOLE-')][-1]
            event('outbound_accepted',job=job_id);pc.close_warehouse_computer()
            assert not pc.is_move_input_ignored() and pc.get_view_target()==pawn,'Console did not restore player'
            walk([(545,-2200,0),(545,-1880,0),(780,-1200,0)],'observe');return
        if phase=='observe':
            pc.toggle_observer_view();pc.set_observer_floor(0);pc.set_layout_editing(False);enter('jobs');return
        if phase=='jobs':
            inbound=live['WH_InboundDispatch'].get_editor_property('completed_count')
            outbound=any(job_id in v.get_editor_property('completed_job_ids') for v in fleet)
            if inbound>=2 and outbound:
                event('three_jobs_completed',inbound=inbound,outbound=outbound);shot('CompletedJobs');enter('rack_return_submit');return
            assert clock<420,'Concurrent inbound/outbound did not complete'
            return
        if phase=='rack_return_submit':
            if age<1:return
            comp=live['WH_ManagementComputer'];comp.refresh_state()
            item=next(i for i in comp.get_editor_property('inventory') if i.cargo==live['MZ_TransferCargo'])
            assert '랙' in item.location,'Return cargo is not physically supported by a rack'
            assert comp.issue_outbound(item.cargo,None,1),comp.get_editor_property('last_message')
            rack_job_id=[t.job_id for t in comp.get_editor_property('tasks') if t.job_id.startswith('CONSOLE-')][-1]
            event('rack_return_accepted',job=rack_job_id,source=item.location);enter('rack_return');return
        if phase=='rack_return':
            if any(rack_job_id in v.get_editor_property('completed_job_ids') for v in fleet):
                event('rack_return_completed',job=rack_job_id);enter('jobs_captured');return
            assert age<300,'Real rack-to-outbound job did not complete'
            return
        if phase=='jobs_captured':
            if age<.5:return
            pc.toggle_observer_view();enter('start_hands');return
        if phase=='start_hands':
            for v in fleet:
                if v.get_editor_property('powered'):v.toggle_power()
            walk([(700,-1920,0),(-1190,-1920,0),(-1190,-1555,0)],'pickup_aim');return
        if phase=='pickup_aim':
            if age<.8:return
            held=live['WH_OutboundPallet_Empty_02'];base=live['WH_OutboundPallet_Empty_01'];base_start=base.get_actor_location()
            aim(held.get_actor_location()+unreal.Vector(0,0,12));enter('pickup');return
        if phase=='pickup':
            if age<.5:return
            assert pawn.get_interaction_target()==held,'Pallet pickup target missed'
            pawn.toggle_carry();assert pawn.get_held_cargo()==held,'E pallet pickup failed'
            enter('held');return
        if phase=='held':
            if age<4:return
            assert pawn.get_held_cargo()==held,'Pallet grip slipped'
            walk([(-1100,-1540,0)],'placement_aim');return
        if phase=='placement_aim':
            if age<1:return
            aim(base.get_actor_location()+unreal.Vector(-20,-20,15));enter('place');return
        if phase=='place':
            if age<.5:return
            assert pawn.get_held_cargo()==held,'Carried pallet lost before E'
            pawn.toggle_carry();assert pawn.is_placing_cargo(),'E did not start placement on the support'
            event('E_placement',player=pos(pawn.get_actor_location()),base=pos(base.get_actor_location()));enter('settle');return
        if phase=='settle':
            b=base.get_component_by_class(unreal.StaticMeshComponent);h=held.get_component_by_class(unreal.StaticMeshComponent)
            assert b.is_simulating_physics() and h.is_simulating_physics(),'Pallet physics was disabled'
            assert not held.get_attach_parent_actor() and not base.get_attach_parent_actor(),'Pallet was attached'
            assert (base.get_actor_location()-base_start).length()<8,'Support pallet moved during stacking'
            if pawn.get_held_cargo() is None and done_at is None:done_at=clock
            if done_at is not None and clock-done_at>2:
                hc,he=held.get_actor_bounds(False);bc,be=base.get_actor_bounds(False)
                assert abs(hc.x-bc.x)<4 and abs(hc.y-bc.y)<4 and abs(hc.z-he.z-bc.z-be.z)<2,('Pallet did not stack',hc,bc)
                assert abs(held.get_actor_rotation().pitch)<3 and abs(held.get_actor_rotation().roll)<3,'Pallet not horizontal'
                event('E_stack_completed',held_pos=pos(held.get_actor_location()),base_pos=pos(base.get_actor_location()));shot('PalletStack');enter('finish');return
            assert age<16,'Pallet placement did not complete'
            return
        if phase=='finish':
            if age<.5:return
            finish(True,'Saved warehouse physics and E stacking'+('' if hands_only else '; 2 inbound + dock outbound + real rack outbound completed'))
    except Exception:
        detail=traceback.format_exc();event('failure',detail=detail)
        if pc and game:
            failure_detail=detail;shot('Failure');enter('failure_capture')
        else:finish(False,detail)

handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
