"""Diagnose a real 140cm rack pickup; no runtime pose/state/velocity forcing.

-UpperShelfHeight=140.1 (or 20.1 for lower control), -UpperShelfRun=name.
Uses the saved ten-piece rack and vehicle in an unsaved scene. Logs submission,
actual settled source height, fork height, recognition, state and job reports.
"""
import ast,json,re,time,traceback
from pathlib import Path
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
source=Path(__file__).with_name('verify_shipping_workflow_pie.py')
tree=ast.parse(source.read_text(encoding='utf-8-sig'))
end=next(i for i,n in enumerate(tree.body) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='out' for t in n.targets))
env=dict(__file__=str(source),__name__='__upper_rack_fixture__')
fixture=ast.unparse(ast.Module(body=tree.body[:end],type_ignores=[]))
fixture=fixture.replace("assert level.load_level('/Engine/Maps/Entry')", """
main_upper_stock=[dict(name=a.get_actor_label(),z=a.get_actor_location().z)
    for a in saved.values() if isinstance(a,unreal.WarehousePallet) and 130<a.get_actor_location().z%400<170]
main_rigs=[dict(name=a.get_actor_label(),location=[a.get_actor_location().x,a.get_actor_location().y,a.get_actor_location().z],
    meshes={p.get_name():[p.relative_location.x,p.relative_location.y,p.relative_location.z]
        for p in a.get_components_by_class(unreal.StaticMeshComponent) if 'Wheel' in p.get_name()})
    for a in saved.values() if isinstance(a,unreal.WarehouseForklift)]
assert level.load_level('/Engine/Maps/Entry')
""")
exec(compile(fixture,str(source),'exec'),env)
level,editor,actors=(env[n] for n in ('level','editor','actors'))
command=unreal.SystemLibrary.get_command_line()
match=re.search(r'-UpperShelfHeight=([0-9.]+)',command,re.I)
height=float(match.group(1)) if match else 140.1
match=re.search(r'-UpperShelfRun=([A-Za-z0-9_-]+)',command,re.I)
out=Path(unreal.Paths.project_saved_dir())/'Testing/UpperRackPickup_20261105'/(match.group(1) if match else 'Before')
out.mkdir(parents=True,exist_ok=True)
(out/'SavedMapInputs.json').write_text(json.dumps(dict(upper_stock=env['main_upper_stock'],rigs=env['main_rigs']),ensure_ascii=False,indent=2),encoding='utf-8')
if height>100:
    for a in env['rack']:
        a.set_actor_location(a.get_actor_location()+unreal.Vector(0,0,height-140.1),False,True)
for a in list(actors.get_all_level_actors()):
    name=a.get_actor_label()
    if isinstance(a,(unreal.WarehousePallet,unreal.WarehouseCargo)) and name not in ('ExportBPallet','ExportBBox'):
        actors.destroy_actor(a)
    if name=='ShippingInboundBlocker':actors.destroy_actor(a)
for name in ('ExportBPallet','ExportBBox'):
    a=next(a for a in actors.get_all_level_actors() if a.get_actor_label()==name)
    a.set_actor_location(a.get_actor_location()+unreal.Vector(700,680,height-.1),False,True)
env['vehicle'].set_actor_location(unreal.Vector(0,900,.1),False,True)
env['spawn'](unreal.WarehouseInboundZone,'UpperRackInspection',(1200,900,height)).set_editor_property('half_size_cm',unreal.Vector(120,120,100))
for label,point,size in [('MapNorth',(0,2500,140),(6500,30,280)),('MapSouth',(0,-2500,140),(6500,30,280)),
                         ('MapEast',(3200,0,140),(30,5000,280)),('MapWest',(-3200,0,140),(30,5000,280))]:
    env['cube'](label,point,size).tags=['WarehouseMapStructure']
phase='start';live=None;began=time.monotonic();last=-1;rows=[];last_state=None;source_info={}

def finish(ok,detail):
    vehicle=live['ShippingAGV'] if live else None
    reports=[dict(result=r.result,detail=r.detail,t=r.simulation_seconds) for r in vehicle.job_reports] if vehicle else []
    record=dict(diagnosis_complete=ok,detail=detail,source=source_info,states=rows,reports=reports,
        savedMapWritten=False,runtimePoseForced=False,scope='Representative saved rack/rig in unsaved mapped fixture; upper shelf, not building floor two')
    (out/'Report.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
    print('UPPER_RACK_DIAGNOSIS',ok,detail)
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def tick(dt):
    global phase,live,last,last_state,source_info
    try:
        assert time.monotonic()-began<300,('Wall timeout',phase)
        if phase=='start':level.editor_request_begin_play();phase='settle';return
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            unreal.GameplayStatics.get_player_controller(game,0).set_simulation_speed(4)
        vehicle,pallet=live['ShippingAGV'],live['ExportBPallet']
        if phase=='settle':
            if now<5 or pallet.receipt_state!=unreal.WarehouseReceiptState.CONFIRMED:
                assert now<30,('Inspection did not confirm',pallet.receipt_state);return
            pos=pallet.get_actor_location()
            source_info=dict(authored_height_cm=height,settled_z_cm=pos.z,max_fork_top_cm=vehicle.max_fork_height_cm,
                legacy_validation_height_cm=pos.z+20,minimum_depth_range_cm=20,source=[pos.x,pos.y,pos.z])
            job=unreal.WarehouseWorkOrder(job_id='UPPER-RACK-DIAGNOSIS',pallet=pallet,destination=unreal.Transform(location=unreal.Vector(500,-600,0)))
            assert vehicle.submit_job(job),vehicle.status
            vehicle.toggle_power();phase='transport'
        if now-last>=.25 or vehicle.ai_state!=last_state:
            rows.append(dict(t=now,state=str(vehicle.ai_state),status=vehicle.status,fork_cm=vehicle.get_fork_height_cm(),
                target_z=pallet.get_actor_location().z,
                x=vehicle.get_actor_location().x,y=vehicle.get_actor_location().y))
            last=now;last_state=vehicle.ai_state
        reports=vehicle.job_reports
        if any(r.result=='DEFERRED' for r in reports):finish(True,'Task deferred; exact reason and pre-sensor validation recorded');return
        if 'UPPER-RACK-DIAGNOSIS' in vehicle.completed_job_ids:finish(True,'Actual pickup, lift, transport, unload and withdrawal completed');return
        if vehicle.ai_state==unreal.WarehouseAIState.FAULT:finish(True,'Actual machine fault recorded');return
        if now>260:finish(False,'Bounded observation ended without completion or deferral')
    except Exception:finish(False,traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
