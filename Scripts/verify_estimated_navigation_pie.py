"""Run existing native regressions with real mapped localization, never forced AGV motion.

-EstimatedCase=shipping (default), remote, layout or elevator. Shipping and
remote get fixed perimeter walls before PIE so an exact-pose no-map fallback
cannot pass this test. Layout/elevator use the original saved warehouse.
"""
import ast
import json
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
root = Path(unreal.Paths.project_dir()).resolve()
command = unreal.SystemLibrary.get_command_line().lower()
case = next((n for n in ('remote', 'layout', 'elevator') if '-estimatedcase='+n in command), 'shipping')
scripts = dict(shipping='verify_shipping_workflow_pie.py', remote='verify_remote_drive_continuity_pie.py',
               layout='verify_layout_editor_pie.py', elevator='verify_cargo_elevator_pie.py')
path = root/'Scripts'/scripts[case]
out = root/'Saved/Testing/EstimatedNavigation_20261031'/case
out.mkdir(parents=True, exist_ok=True)
if case == 'elevator':
    config = root/'Saved/Testing/CargoElevator/TestCase.json'
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text(json.dumps(dict(from_floor=0, to_floor=2, saved_map=True)), encoding='utf-8')
env = dict(__file__=str(path), __name__='__estimated_regression__')
tree = ast.parse(path.read_text(encoding='utf-8-sig'), str(path))
if case in ('shipping', 'remote'):
    # Execute only construction first; the original callback is not registered
    # until the map has these explicitly published fixed structure components.
    marker = 'out' if case == 'shipping' else 'started'
    split = next(i for i,n in enumerate(tree.body) if isinstance(n,ast.Assign) and
                 any(isinstance(t,ast.Name) and t.id==marker for t in n.targets))
    exec(compile(ast.Module(body=tree.body[:split],type_ignores=[]),str(path),'exec'),env)
    for name,position,size in [
        ('West',(-3570,0,150),(20,5300,300)), ('East',(3570,0,150),(20,5300,300)),
        ('South',(0,-2660,150),(7160,20,300)), ('North',(0,2660,150),(7160,20,300))]:
        a=env['actors'].spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*position))
        a.set_actor_label('LocalizationMap'+name)
        a.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
        a.set_actor_scale3d(unreal.Vector(*size)/100)
        a.static_mesh_component.set_collision_profile_name('BlockAll')
        a.tags=['WarehouseMapStructure']
    exec(compile(ast.Module(body=tree.body[split:],type_ignores=[]),str(path),'exec'),env)
else:
    exec(compile(tree,str(path),'exec'),env)
if case=='layout' and '-nullrhi' in command:
    assert env['PHYSICAL_ONLY'],'Headless layout regression requires -LayoutPhysicalOnly'
if case=='elevator':
    operator=env['actors'].spawn_actor_from_class(unreal.TargetPoint,
        env['frame'].transform_location(unreal.Vector(-700,180,875)))
    operator.set_actor_label('LocalizationLandingOperator')
    for part in operator.get_components_by_class(unreal.PrimitiveComponent):
        part.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

original_finish=env['finish']
rows={}
remote_baselines={}
errors=[]
finished=False
started=time.monotonic()
last_write=0
landing_phase=None
landing={}

def sample(dt):
    global last_write,landing_phase
    if finished: return
    try:
        game=env['editor'].get_game_world()
        if not game: return
        now=unreal.GameplayStatics.get_time_seconds(game)
        for v in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseForklift):
            loc=v.get_component_by_class(unreal.AgvForkliftLocalizerComponent)
            assert loc, ('No localizer attached',v.get_name())
            label=v.get_actor_label()
            row=rows.setdefault(label,dict(mapped_matches=0,estimated_travel_frames=0,remote_frames=0,
                                           floors=[],max_error_cm=0,scans=0,last_scan_time=None))
            previous_matches=row.get('accepted',0)
            row.update(accepted=loc.accepted_matches,rejected=loc.rejected_matches,
                       map_builds=loc.map_builds,map_ms=loc.map_build_milliseconds,
                       max_error_cm=loc.max_error_cm,mean_error_cm=loc.mean_error_cm,
                       p95_error_cm=loc.p95_error_cm,match_ms=loc.last_match_milliseconds,
                       reinitializations=loc.reinitializations,status=loc.status_text)
            row['mapped_matches']=max(row['mapped_matches'],loc.accepted_matches)
            kit=v.get_component_by_class(unreal.AgvShadowSensorsComponent)
            if kit and kit.snapshot_count!=row['scans']:
                if row['last_scan_time'] is not None:
                    assert kit.last_snapshot_seconds-row['last_scan_time']>=.999,('Scan faster than 1s',label)
                row.update(scans=kit.snapshot_count,last_scan_time=kit.last_snapshot_seconds)
            if v.is_remote_controlled():
                counts=(loc.accepted_matches,loc.rejected_matches,kit.snapshot_count if kit else 0)
                baseline=remote_baselines.setdefault(label,counts)
                # The original test starts remote after this component's tick.
                # Only completed remote ticks are compared, including faults.
                if loc.status_text=='REMOTE - LOCALIZATION SUSPENDED':
                    assert counts==baseline,('Remote sensor work',label)
                    row['remote_frames']+=1
                continue
            remote_baselines.pop(label,None)
            if kit and kit.snapshot_valid and loc.accepted_matches>previous_matches:
                floor=round(kit.snapshot_floor_z_cm/400)
                if floor not in row['floors']: row['floors'].append(floor)
            route=v.planned_route
            if v.ai_state in (unreal.WarehouseAIState.NAVIGATE_PICKUP,unreal.WarehouseAIState.NAVIGATE_DELIVERY) and route:
                distance=(route[-1].pose.translation-v.get_actor_location()).length()
                if distance>100 and loc.accepted_matches and v.use_agv_localization:
                    delta=(v.navigation_transform().translation-v.get_actor_location()).length()
                    if delta>.001: row['estimated_travel_frames']+=1
        if landing_phase:
            vehicle=env['vehicle']
            loc=vehicle.get_component_by_class(unreal.AgvForkliftLocalizerComponent)
            if landing_phase=='remote':
                vehicle.set_remote_input(-.6,0,0)
                if (vehicle.get_actor_location()-landing['origin']).length()>=150:
                    vehicle.set_remote_input(0,0,0)
                    vehicle.end_remote_control(landing['operator'])
                    assert vehicle.stop_current_work()
                    vehicle.toggle_power()
                    landing.update(at=now,accepted=loc.accepted_matches)
                    landing_phase='match'
            elif landing_phase=='match' and loc.accepted_matches>landing['accepted']:
                landing.update(reacquired=True,ratio=loc.last_inlier_ratio,sim_seconds=now-landing['at'])
                landing_phase='done'
                finish()
                return
            if landing_phase!='done':
                assert now-landing['at']<12,('Landing reacquisition timeout',landing_phase,loc.status_text,loc.last_inlier_ratio)
        if time.monotonic()-last_write>2:
            (out/'Progress.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
            last_write=time.monotonic()
    except Exception:
        errors.append(traceback.format_exc())
        finish('Localization monitor failed' if case=='elevator' else False,
               *([] if case=='elevator' else [errors[-1]]))

def finish(*args):
    global finished,landing_phase
    if finished: return
    ok=(not args or args[0] is None) if case=='elevator' else bool(args[0])
    if case=='elevator' and ok and landing_phase is None:
        # Shaft/door returns are not in the fixed building map. Once the real
        # transfer is complete, use ordinary remote reverse to leave that view,
        # cancel the unfinished pickup and verify a NEW match on the upper map.
        # Neither the AGV transform nor velocity/AI state is assigned here.
        unreal.unregister_slate_post_tick_callback(env['handle'])
        game=env['editor'].get_game_world()
        vehicle=env['vehicle']
        operator=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)
                      if a.get_actor_label()=='LocalizationLandingOperator')
        assert vehicle.begin_remote_control(operator),vehicle.status
        landing.update(operator=operator,origin=vehicle.get_actor_location(),at=unreal.GameplayStatics.get_time_seconds(game))
        landing_phase='remote'
        return
    finished=True
    try:
        assert ok,('Native regression failed',args)
        assert not errors,errors
        # The existing layout test resumes its idle vehicle for just two
        # seconds; require its fresh map match, not three seconds of driving.
        minimum_matches=1 if case=='layout' else 3
        assert any(r['mapped_matches']>=minimum_matches for r in rows.values()),'No actual scan-to-map matches'
        if case=='shipping': assert any(r['estimated_travel_frames']>100 for r in rows.values()),'No estimated route travel'
        if case=='remote': assert any(r['remote_frames']>100 for r in rows.values()),'No remote localization suspension samples'
        if case=='elevator':
            assert any(r['reinitializations']>=1 and len(r['floors'])>=2 for r in rows.values()),'No floor-map handoff'
    except Exception:
        ok=False
        errors.append(traceback.format_exc())
    report=dict(date='2026-10-31',case=case,passed=ok,native_result=str(args),errors=errors,vehicles=rows,
                wall_seconds=round(time.monotonic()-started,2),runtime_motion_forced=False,saved_map_written=False)
    if case=='elevator': report['landing_reacquisition']={k:v for k,v in landing.items() if k not in ('operator','origin')}
    (out/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('ESTIMATED_NAVIGATION_'+('PASSED' if ok else 'FAILED'),case,errors)
    unreal.unregister_slate_post_tick_callback(observer)
    if case=='elevator':
        original_finish(None if ok else str(errors or args))
        unreal.SystemLibrary.quit_editor()
    else:
        original_finish(ok,str(args[1] if len(args)>1 else '')+(' '+str(errors) if errors else ''))

env['finish']=finish
observer=unreal.register_slate_post_tick_callback(sample)
