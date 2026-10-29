"""Real native job deferral, cancellation and loaded-stop boundary in an unsaved fixture.

Only the existing shipping test's fixture construction is reused. No AGV pose,
state, fork support or transport result is forced. The intentionally invalid
lift destination exercises native work validation, while the next job must
physically engage a pallet. Loaded cancellation must preserve those bodies.
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
out=Path(unreal.Paths.project_saved_dir())/'Testing/ForkliftWorkControl'
out.mkdir(parents=True,exist_ok=True)
checks=[]
report={'passed':False,'checks':checks,'transportStateForced':False,'savedMapWritten':False,
        'scope':'Unsaved native fixture; normal job submission, real pallet pickup and stop API; no OS input'}
phase='start';started=time.monotonic();at=0;live=None;vehicle=None;loaded_position=None

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
    global live,vehicle,loaded_position
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
            if vehicle.get_supported_pallet()==b:
                loaded_position=b.get_actor_location()
                report['physicalPickupJob']=vehicle.get_editor_property('active_job').job_id
                assert report['physicalPickupJob']=='FOLLOWING-VALID'
                assert vehicle.stop_current_work()
                assert not vehicle.get_editor_property('powered')
                assert not vehicle.get_editor_property('active_job').job_id and not vehicle.get_editor_property('pending_jobs')
                assert vehicle.get_editor_property('work_stop_needs_recovery')
                assert vehicle.get_supported_pallet()==b
                assert b.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
                assert b.get_attach_parent_actor() is None
                vehicle.toggle_power()
                assert not vehicle.get_editor_property('powered'),'Loaded work stop resumed before manual unloading'
                enter('held_stop',now)
                check('following task physically picks up freight; loaded stop cancels work while retaining real dynamic pallet support')
            assert now-at<250,('Following work did not reach actual pickup',vehicle.get_editor_property('status'))
        elif phase=='held_stop' and now-at>=2:
            assert not vehicle.get_editor_property('powered') and not vehicle.get_editor_property('active_job').job_id
            assert vehicle.get_supported_pallet()==b
            assert (loaded_position-b.get_actor_location()).length()<30,'Stopped pallet was teleported or released'
            assert not any(r.job_id in ('STOP-QUEUED','FOLLOWING-VALID') and r.result=='COMPLETED'
                           for r in vehicle.get_editor_property('job_reports'))
            assert any(r.job_id=='FOLLOWING-VALID' and r.result=='CANCELLED' and '[USER_STOP]' in r.detail
                       for r in vehicle.get_editor_property('job_reports'))
            report['heldStopDriftCm']=(loaded_position-b.get_actor_location()).length()
            check('loaded stop stays cancelled and braked; E cannot restart it or silently drop/complete the load')
            finish(True,'Task deferral, next real pickup, queue cancellation and physically retained loaded stop passed')
    except Exception:
        finish(False,traceback.format_exc())

handle=unreal.register_slate_post_tick_callback(tick)
