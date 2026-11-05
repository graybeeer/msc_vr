"""Native shared lift: stationary side passenger, intermediate stop, safe AGV exit.

-PassengerExitRun=name preserves a report; -PassengerExitBlocked adds a removable
human-shaped doorway obstruction. Actual player walks aboard and remains still.
No runtime vehicle/passenger transform, attachment, velocity or AI-state writes.
"""
import ast,json,re,time,traceback
from pathlib import Path
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
source=Path(__file__).with_name('verify_human_elevator_pie.py')
tree=ast.parse(source.read_text(encoding='utf-8-sig'))
end=next(i for i,n in enumerate(tree.body) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='capture' for t in n.targets))
env=dict(__file__=str(source),__name__='__stationary_passenger_fixture__')
exec(compile(ast.Module(body=tree.body[:end],type_ignores=[]),str(source),'exec'),env)
level,editor,actors=(env[n] for n in ('level','editor','actors'))
actors.destroy_actor(env['crate'])
command=unreal.SystemLibrary.get_command_line()
blocked='-passengerexitblocked' in command.lower()
match=re.search(r'-PassengerExitRun=([A-Za-z0-9_-]+)',command,re.I)
out=Path(unreal.Paths.project_saved_dir())/'Testing/ElevatorPassengerExit_20261105'/(match.group(1) if match else 'Clear')
out.mkdir(parents=True,exist_ok=True)
for name,pos,size in [('N',(0,1800,650),(3600,20,1400)),('S',(0,-1800,650),(3600,20,1400)),
                      ('W',(-1800,0,650),(20,3600,1400)),('E',(900,0,650),(20,3600,1400))]:
    env['block']('PassengerMap'+name,pos,size)
if blocked:
    obstacle=env['block']('ExitPersonProxy',(-320,0,885),(30,30,170))
    obstacle.tags=['WarehousePhysicalHuman']
    obstacle.static_mesh_component.set_collision_object_type(unreal.CollisionChannel.ECC_PAWN)
phase='start';at=0;began=time.monotonic();live=None;last_t=-1;last_state=None
checks=[];samples=[];intermediate=False;passenger_origin=None;blocked_at=None;exit_at=None

def enter(value,now):
    global phase,at
    phase,at=value,now
    print('PASSENGER_EXIT_PHASE',value,now)

def finish(ok,detail):
    record=dict(passed=ok,detail=detail,checks=checks,samples=samples,blocked_case=blocked,
        intermediate_stop_seen=intermediate,runtime_motion_forced=False,saved_map_written=False,
        scope='Actual stationary production player, saved vehicle/model, native shared lift and sensors; doorway blocker is a labelled human collision proxy, not NPC behaviour')
    (out/'Report.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
    print('PASSENGER_EXIT_RESULT',ok,detail)
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def tick(dt):
    global live,last_t,last_state,intermediate,passenger_origin,blocked_at,exit_at
    try:
        assert time.monotonic()-began<360,('Wall timeout',phase)
        if phase=='start':level.editor_request_begin_play();enter('settle',0);return
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            assert unreal.GameplayStatics.get_player_controller(game,0).set_simulation_speed(4)
        vehicle,lift=live['HumanLiftAGV'],live['HumanLift']
        pawn=unreal.GameplayStatics.get_player_character(game,0)
        state=vehicle.ai_state
        if now-last_t>=.25 or state!=last_state:
            p=vehicle.get_actor_location();h=pawn.get_actor_location()
            samples.append(dict(t=now,phase=phase,state=str(state),status=vehicle.status,floor=lift.current_floor,
                lift_state=str(lift.state),vehicle=[p.x,p.y,p.z],human=[h.x,h.y,h.z],speed_cm_s=vehicle.current_speed_cm))
            last_t=now;last_state=state
        assert state!=unreal.WarehouseAIState.FAULT,(vehicle.status,lift.status)
        if phase=='settle':
            if now<3:return
            job=unreal.WarehouseWorkOrder(job_id='STATIONARY-PASSENGER-L1-L3',pallet=live['SharedLiftTarget'],
                destination=unreal.Transform(location=unreal.Vector(-900,-900,800)))
            assert vehicle.submit_job(job),vehicle.status
            vehicle.toggle_power();enter('vehicle_board',now)
        elif phase=='vehicle_board':
            assert now-at<90,vehicle.status
            if state==unreal.WarehouseAIState.ELEVATOR_RIDE:enter('human_board',now)
        elif phase=='human_board':
            assert lift.current_floor==0,'Lift left before the passenger boarded'
            pos=pawn.get_actor_location()
            goal=unreal.Vector(-260,82,pos.z) if pos.x< -230 and abs(pos.y-82)>3 else unreal.Vector(125,82,pos.z)
            delta=goal-pos
            if delta.length()>.75:
                pawn.add_movement_input(delta/delta.length(),min(1,max(.025,delta.length()/70)),True)
                if lift.is_human_inside(pawn):lift.request_human_destination(pawn,0)
                else:lift.request_human_call(pawn,0)
                assert now-at<30,('Player could not walk aboard',pos)
                return
            assert lift.is_human_inside(pawn),pos
            assert lift.request_human_destination(pawn,1),lift.human_status
            passenger_origin=pos
            checks.append('Production player walked into the side space and selected intermediate L2; AGV destination remains L3')
            enter('ride',now)
        elif phase=='ride':
            h=pawn.get_actor_location()
            assert (unreal.Vector(h.x,h.y,passenger_origin.z)-passenger_origin).length()<3,'Stationary passenger moved horizontally'
            assert lift.is_human_inside(pawn),'Passenger lost platform support'
            if lift.current_floor==1 and lift.state==unreal.WarehouseElevatorState.AWAIT_EXIT:
                intermediate=True
                assert state==unreal.WarehouseAIState.ELEVATOR_RIDE and not lift.can_exit(vehicle),'AGV exited at another passenger destination'
            if lift.current_floor!=2 or lift.state!=unreal.WarehouseElevatorState.AWAIT_EXIT:
                assert now-at<170,(vehicle.status,lift.status)
                return
            assert intermediate and lift.can_exit(vehicle)
            checks.append('Human-only L2 stop kept AGV braked; L3 doors opened with the passenger still inside')
            exit_at=now;enter('exit',now)
        elif phase=='exit':
            h=pawn.get_actor_location()
            assert lift.is_human_inside(pawn) and abs(h.x-passenger_origin.x)<3 and abs(h.y-passenger_origin.y)<3,'Passenger had to leave or move to permit AGV exit'
            if blocked and 'ExitPersonProxy' in live:
                obstacle=live['ExitPersonProxy'];kit=vehicle.get_component_by_class(unreal.AgvShadowSensorsComponent)
                if kit.has_observed_actor(obstacle) and abs(vehicle.current_speed_cm)<3 and ('PERSON' in vehicle.status or 'OBSERVATION' in vehicle.status):
                    bounds=unreal.WarehouseDamageSystem.get_physical_bounds(vehicle)
                    wall=unreal.WarehouseDamageSystem.get_physical_bounds(obstacle)
                    assert bounds.min.x>wall.max.x,'AGV contacted/crossed the human-shaped blocker'
                    if blocked_at is None:blocked_at=now
                    if now-blocked_at>2:
                        checks.append('Observed human-shaped exit obstruction stopped AGV before contact for two simulation seconds')
                        obstacle.destroy_actor();del live['ExitPersonProxy']
                assert now-exit_at<25,('Did not demonstrate safe blocked exit',vehicle.status)
                return
            if lift.completed_transfers==1:
                assert state not in (unreal.WarehouseAIState.ELEVATOR_RIDE,unreal.WarehouseAIState.ELEVATOR_EXIT)
                if blocked:assert blocked_at is not None
                checks.append('Actual AGV motor exit and ConfirmExited completed while the same passenger remained stationary inside')
                finish(True,'Stationary side passenger permits exit; destination/interlocks, observed path obstacles and physical contacts retained')
                return
            # The native waiting point is 500cm away: 25s at the existing
            # 20cm/s, plus fresh observation, acceleration and stopped handoff.
            assert now-at<45,('Could not exit with a stationary side passenger',vehicle.status,lift.status)
    except Exception:finish(False,traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
