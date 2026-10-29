"""Real native job deferral, cancellation and loaded-stop boundary in an unsaved fixture.

Only the existing shipping test's fixture construction is reused. No AGV pose,
state, fork support or transport result is forced. A stopped physical load must
be lowered/withdrawn before different work starts. Optional scenarios cover
inserted-only forks, a 20cm rack shelf and temporarily removed floor support.
"""
import ast
import json
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
fixture_path=Path(__file__).with_name('verify_shipping_workflow_pie.py')
tree=ast.parse(fixture_path.read_text(encoding='utf-8-sig'),str(fixture_path))
end=next(i for i,n in enumerate(tree.body) if isinstance(n,ast.Assign) and
         any(isinstance(t,ast.Name) and t.id=='out' for t in n.targets))
fixture={'__file__':str(fixture_path),'__name__':'__work_control_fixture__'}
exec(compile(ast.Module(body=tree.body[:end],type_ignores=[]),str(fixture_path),'exec'),fixture)
level,editor=(fixture[n] for n in ('level','editor'))
command_line=unreal.SystemLibrary.get_command_line()
insert_only='-RetainedInsertOnly' in command_line
rack_load='-RetainedRackLoad' in command_line
blocked_drop='-RetainedBlockedDrop' in command_line
scenario='Inserted' if insert_only else 'Rack' if rack_load else 'Blocked' if blocked_drop else 'Floor'
fixture['spawn'](unreal.TargetPoint,'WorkControlOperator',(0,-150,0))
if rack_load:
    delta=unreal.Vector(700,680,20)
    for name in ('ExportBPallet','ExportBBox'):
        actor=next(a for a in fixture['actors'].get_all_level_actors() if a.get_actor_label()==name)
        actor.set_actor_location(actor.get_actor_location()+delta,False,True)
    inspection=fixture['spawn'](unreal.WarehouseInboundZone,'RetainedRackInspection',(1200,900,20))
    inspection.set_editor_property('half_size_cm',unreal.Vector(120,120,100))
if blocked_drop:
    # A removable, map-published 110cm floor tile supports the pickup. All
    # chassis wheels remain on the surrounding real floor when it is removed.
    fixture['actors'].destroy_actor(next(a for a in fixture['actors'].get_all_level_actors() if a.get_actor_label()=='ShippingFloor'))
    for label,point,size in [
        ('FloorLeft',(-1577.5,0,-10),(4045,5400,20)),
        ('FloorRight',(2077.5,0,-10),(3045,5400,20)),
        ('FloorBottom',(500,-1267.5,-10),(110,2865,20)),
        ('FloorTop',(500,1487.5,-10),(110,2425,20)),
        ('RetainedDropTile',(500,220,-10),(110,110,20))]:
        fixture['cube'](label,point,size).tags=['WarehouseMapStructure']
out=Path(unreal.Paths.project_saved_dir())/('Testing/ForkliftWorkControl/'+scenario)
out.mkdir(parents=True,exist_ok=True)
checks=[]
report={'passed':False,'checks':checks,'transportStateForced':False,'savedMapWritten':False,
        'scenario':scenario,'scope':'Unsaved native fixture; normal job submission, real pallet pickup and stop API; no OS input'}
phase='start';started=time.monotonic();at=0;live=None;vehicle=None;loaded_position=None;clear_at=None;remote_scans=None

def check(message):
    checks.append(message);print('WORK_CONTROL_CHECK_PASSED',message)

def enter(value,now):
    global phase,at
    phase,at=value,now

def job(name,pallet,height=0):
    return unreal.WarehouseWorkOrder(job_id=name,source_system='FMS-SIM',pallet=pallet,
        destination=unreal.Transform(location=unreal.Vector(-500,500,height)))

def finish(ok,detail):
    report.update(passed=ok,message=detail,wall_seconds=round(time.monotonic()-started,2))
    if vehicle:
        report['reports']=[dict(job=r.job_id,result=r.result,detail=r.detail) for r in vehicle.get_editor_property('job_reports')]
    (out/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('WORK_CONTROL_PIE_'+('PASSED' if ok else 'FAILED'),detail)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(dt):
    global live,vehicle,loaded_position,clear_at,remote_scans
    try:
        assert time.monotonic()-started<600,('Wall timeout',phase)
        if phase=='start':
            level.editor_request_begin_play();enter('settle',0);return
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            vehicle=live['ShippingAGV']
            pc=unreal.GameplayStatics.get_player_controller(game,0)
            assert pc.set_simulation_speed(4)
            assert vehicle.get_editor_property('empty_travel_speed_cm')==180
        a,b=live['ExportAPallet'],live['ExportBPallet']
        if phase=='settle':
            if (a.get_editor_property('receipt_state')!=unreal.WarehouseReceiptState.CONFIRMED or
                    b.get_editor_property('receipt_state')!=unreal.WarehouseReceiptState.CONFIRMED):
                assert now<25,'Native receiving/wrapping did not settle';return
            # Queued cancellation must not revive on the next power cycle.
            assert vehicle.submit_job(job('STOP-QUEUED',a))
            assert vehicle.stop_current_work()
            assert not vehicle.get_editor_property('pending_jobs') and not vehicle.get_editor_property('active_job').job_id
            reports=vehicle.get_editor_property('job_reports')
            assert any(r.job_id=='STOP-QUEUED' and r.result=='CANCELLED' and '[USER_STOP]' in r.detail for r in reports)
            assert vehicle.submit_job(job('DEFER-INVALID-LIFT',a,300))
            assert vehicle.submit_job(job('FOLLOWING-VALID',b))
            vehicle.toggle_power();enter('following',now)
            check('queued stop cancels its instruction and a fresh power cycle accepts unrelated work')
        elif phase=='following':
            reports=vehicle.get_editor_property('job_reports')
            if any(r.job_id=='DEFER-INVALID-LIFT' and r.result=='DEFERRED' for r in reports):
                assert vehicle.is_work_deferred(a)
                assert not vehicle.submit_job(job('EARLY-RETRY',a))
                assert vehicle.get_editor_property('powered')
                assert vehicle.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT
                enter('pickup',now)
                check('impossible task is reported as deferred, locally throttled, and does not fault the healthy truck')
            assert now-at<15,'Invalid lift task did not defer'
        elif phase=='pickup':
            assert vehicle.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,vehicle.get_editor_property('status')
            assert not any(r.job_id=='FOLLOWING-VALID' and r.result=='DEFERRED' for r in vehicle.get_editor_property('job_reports')),'Fixture pickup was deferred before the retained-load scenario'
            state=vehicle.get_editor_property('ai_state')
            if ((not insert_only and vehicle.get_supported_pallet()==b and state==unreal.WarehouseAIState.VERIFY_LOAD) or
                    (insert_only and state==unreal.WarehouseAIState.VERIFY_INSERTION)):
                loaded_position=b.get_actor_location()
                report['physicalPickupJob']=vehicle.get_editor_property('active_job').job_id
                assert report['physicalPickupJob']=='FOLLOWING-VALID'
                assert vehicle.stop_current_work()
                assert not vehicle.get_editor_property('powered')
                assert not vehicle.get_editor_property('active_job').job_id and not vehicle.get_editor_property('pending_jobs')
                assert vehicle.get_editor_property('work_stop_needs_recovery')
                assert insert_only or vehicle.get_supported_pallet()==b
                assert b.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
                assert b.get_attach_parent_actor() is None
                enter('held_stop',now)
                check('fully inserted forks stop without claiming a lifted load' if insert_only else
                      'following task physically lifts freight; loaded stop retains real dynamic pallet support')
            assert now-at<250,('Following work did not reach actual pickup',vehicle.get_editor_property('status'))
        elif phase=='held_stop' and now-at>=2:
            assert not vehicle.get_editor_property('powered') and not vehicle.get_editor_property('active_job').job_id
            assert insert_only or vehicle.get_supported_pallet()==b
            assert (loaded_position-b.get_actor_location()).length()<30,'Stopped pallet was teleported or released'
            assert not any(r.job_id in ('STOP-QUEUED','FOLLOWING-VALID') and r.result=='COMPLETED'
                           for r in vehicle.get_editor_property('job_reports'))
            assert any(r.job_id=='FOLLOWING-VALID' and r.result=='CANCELLED' and '[USER_STOP]' in r.detail
                       for r in vehicle.get_editor_property('job_reports'))
            report['heldStopDriftCm']=(loaded_position-b.get_actor_location()).length()
            check('loaded stop stays cancelled and braked without silently dropping or completing the load')
            if blocked_drop:
                live['RetainedDropTile'].set_actor_enable_collision(False)
                vehicle.refresh_after_layout(False)
            assert vehicle.submit_job(job('AFTER-RETAINED-LOAD',a))
            if scenario=='Floor':
                operator=live['WorkControlOperator']
                operator.set_actor_location(vehicle.get_actor_location()+unreal.Vector(0,-150,0),False,True)
                assert vehicle.begin_remote_control(operator)
                remote_scans=vehicle.get_component_by_class(unreal.AgvShadowSensorsComponent).get_editor_property('snapshot_count')
                enter('remote_wait',now)
                return
            vehicle.toggle_power()
            assert vehicle.get_editor_property('powered'),'Fresh work could not start retained-load cleanup'
            enter('blocked' if blocked_drop else 'clear_retained',now)
        elif phase=='remote_wait':
            vehicle.set_remote_input(0,0,0)
            assert not vehicle.get_editor_property('clearing_forks_for_work')
            assert not vehicle.get_editor_property('active_job').job_id
            assert vehicle.get_component_by_class(unreal.AgvShadowSensorsComponent).get_editor_property('snapshot_count')==remote_scans
            if now-at>=3:
                vehicle.end_remote_control(live['WorkControlOperator'])
                vehicle.toggle_power()
                assert vehicle.get_editor_property('powered')
                check('queued replacement triggers neither sensor sampling nor automatic unloading during remote control')
                enter('clear_retained',now)
        elif phase=='blocked':
            assert not vehicle.get_editor_property('active_job').job_id
            assert vehicle.get_editor_property('pending_jobs')[0].job_id=='AFTER-RETAINED-LOAD'
            assert not vehicle.get_editor_property('clearing_forks_for_work')
            assert not any(r.result=='FORKS_CLEARED' for r in vehicle.get_editor_property('job_reports'))
            assert (loaded_position-b.get_actor_location()).length()<35
            if now-at>6:
                assert str(vehicle.get_editor_property('last_error_code'))=='FORK_CLEARANCE_BLOCKED',vehicle.get_editor_property('status')
                check('missing drop support keeps the new order queued and retains the physical old load with a clear warning')
                live['RetainedDropTile'].set_actor_enable_collision(True)
                vehicle.refresh_after_layout(False)
                enter('clear_retained',now)
        elif phase=='clear_retained':
            assert b.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
            assert b.get_attach_parent_actor() is None
            assert vehicle.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,vehicle.get_editor_property('status')
            reports=vehicle.get_editor_property('job_reports')
            cleared=[r for r in reports if r.result=='FORKS_CLEARED']
            accepted=[r for r in reports if r.job_id=='AFTER-RETAINED-LOAD' and r.result=='ACCEPTED']
            if not cleared:
                assert not accepted,'New cargo became active while the old pallet was still on the forks'
            else:
                clear_at=cleared[-1].simulation_seconds
                assert not cleared[-1].job_id,'Internal unloading was falsely counted as a work order'
                if not rack_load:assert vehicle.is_floor_placement_valid(b,b.get_actor_transform())
                else:assert abs(b.get_actor_location().z-20)<2
                assert (loaded_position-b.get_actor_location()).length()<35,'Existing load was moved instead of physically lowered in place'
                assert not any(r.job_id=='FOLLOWING-VALID' and r.result=='COMPLETED' for r in reports)
                check('old load physically settles on its fixed support and forks withdraw before the queued replacement is accepted')
                report['clearanceSeconds']=now-at
                report['oldPalletDisplacementCm']=(loaded_position-b.get_actor_location()).length()
                enter('replacement',now)
            assert now-at<100,('Retained load did not clear',vehicle.get_editor_property('status'))
        elif phase=='replacement':
            reports=vehicle.get_editor_property('job_reports')
            accepted=[r for r in reports if r.job_id=='AFTER-RETAINED-LOAD' and r.result=='ACCEPTED']
            assert not accepted or accepted[-1].simulation_seconds>=clear_at
            assert vehicle.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,vehicle.get_editor_property('status')
            if vehicle.get_supported_pallet()==a:
                assert vehicle.get_editor_property('active_job').job_id=='AFTER-RETAINED-LOAD'
                if not rack_load:assert vehicle.is_floor_placement_valid(b,b.get_actor_transform())
                else:assert abs(b.get_actor_location().z-20)<2
                check('replacement work really inserts/lifts its different pallet after clearing the cancelled physical load')
                finish(True,'Deferral, stopped load retention, physical unloading/withdrawal and subsequent real pickup passed')
            assert now-at<250,('Replacement work did not pick up its pallet',vehicle.get_editor_property('status'))
    except Exception:
        finish(False,traceback.format_exc())

handle=unreal.register_slate_post_tick_callback(tick)
