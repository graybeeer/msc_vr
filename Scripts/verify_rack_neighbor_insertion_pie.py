"""Native rack insertion beside dynamic stock; -RackCase=normal/intrusion/oversize.

No runtime transforms/velocities/state writes; incoming and neighbouring units
keep real physics. Normal must complete; intrusion/oversize must stop safely.
-RackLevel=20/140 selects the real lower/upper shelf; the source stays on floor.
-RackSide=front/back -RackRoundTrip verifies insertion and another vehicle's
physical retrieval from the opposite aisle, including the saved rack braces.
"""
import ast,json,re,time,traceback
from pathlib import Path
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
source=Path(__file__).with_name('verify_shipping_workflow_pie.py')
tree=ast.parse(source.read_text(encoding='utf-8-sig'))
end=next(i for i,n in enumerate(tree.body) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='out' for t in n.targets))
env=dict(__file__=str(source),__name__='__rack_neighbor_fixture__')
exec(compile(ast.Module(body=tree.body[:end],type_ignores=[]),str(source),'exec'),env)
actors,editor,level=(env[n] for n in ('actors','editor','level'))
command=unreal.SystemLibrary.get_command_line()
match=re.search(r'-RackSide=(front|back)',command,re.I)
side=match.group(1).lower() if match else 'front'
round_trip='-rackroundtrip' in command.lower()
match=re.search(r'-RackCase=(normal|intrusion|oversize)',command,re.I)
case=match.group(1).lower() if match else 'normal'
match=re.search(r'-RackLevel=(20|140)(?:\s|$)',command,re.I)
rack_level=int(match.group(1)) if match else 20
match=re.search(r'-RackRun=([A-Za-z0-9_-]+)',command,re.I)
out=Path(unreal.Paths.project_saved_dir())/'Testing/RackNeighborInsertion_20261105'/(match.group(1) if match else 'Final')/case
out.mkdir(parents=True,exist_ok=True)
for a in list(actors.get_all_level_actors()):
    if isinstance(a,(unreal.WarehouseCargo,unreal.WarehousePallet)) and a.get_actor_label() not in ('ExportBPallet','ExportBBox'):
        actors.destroy_actor(a)
    if a.get_actor_label()=='ShippingInboundBlocker':actors.destroy_actor(a)
lookup={a.get_actor_label():a for a in actors.get_all_level_actors()}
box=lookup['ExportBBox'];box.set_gross_mass_kg(25)
env['fit'](box,(80,150 if case=='oversize' else 80,30),(500,220,30.25))
neighbor_y=1015 if case=='intrusion' else 1025
neighbor=env['spawn'](unreal.WarehousePallet,'NeighborPallet',(1200,neighbor_y,rack_level+.1))
neighbor.set_actor_rotation(unreal.Rotator(yaw=7 if case=='intrusion' else 0),False)
parcel=env['spawn'](unreal.WarehouseCargo,'NeighborCargo',(1200,neighbor_y,rack_level+35.25))
parcel.set_cargo_mesh(box.get_component_by_class(unreal.StaticMeshComponent).static_mesh)
parcel.set_gross_mass_kg(20)
env['fit'](parcel,(60,60,40),(1200,neighbor_y,rack_level+35.25))
specs=list(env['strength'].objects)
for spec in specs:
    if box in spec.members:spec.mass_kg=25
specs.extend([unreal.WarehouseStrength(name='NeighborPallet',members=[neighbor],mass_kg=25,rated_load_kg=1500,impact_yield_j=200,impact_failure_j=1000),
    unreal.WarehouseStrength(name='NeighborCargo',members=[parcel],mass_kg=20,rated_load_kg=120,impact_yield_j=200,impact_failure_j=1000)])
if side=='back':
    lookup['ExportBPallet'].set_actor_location(unreal.Vector(2300,900,.1),False,True)
    env['fit'](box,(80,80,30),(2300,900,30.25))
    env['vehicle'].set_actor_location(unreal.Vector(1800,900,.1),False,True)
    lookup['ShippingOutboundInspection'].set_actor_location(unreal.Vector(2300,900,0),False,True)
if round_trip:
    reverse=env['spawn'](unreal.WarehouseForklift,'ReverseSideAGV',(2200 if side=='front' else 200,900,.1))
    reverse.set_actor_rotation(unreal.Rotator(yaw=180 if side=='front' else 0),False)
    reverse.set_editor_property('auto_start',False)
    reverse.set_editor_property('pending_jobs',[])
    reverse.set_editor_property('idle_parking_enabled',False)
    for part in reverse.get_components_by_class(unreal.StaticMeshComponent):
        pose,mesh,materials=env['rig'][part.get_name()]
        part.set_relative_transform(pose,False,True)
        part.set_static_mesh(mesh)
        part.set_editor_property('override_materials',materials)
    specs.append(unreal.WarehouseStrength(name='ReverseSideAGV',members=[reverse],failure=unreal.WarehouseFailure.MACHINE,
        mass_kg=1000,rated_load_kg=1400,impact_yield_j=400,impact_failure_j=2400))
env['strength'].set_editor_property('objects',specs)
for label,pos,size in [('N',(0,2500,140),(6400,20,280)),('S',(0,-2500,140),(6400,20,280)),
                       ('E',(3200,0,140),(20,5000,280)),('W',(-3200,0,140),(20,5000,280))]:
    env['cube']('RackMap'+label,pos,size).tags=['WarehouseMapStructure']
phase='start';live=None;began=time.monotonic();rows=[];baseline={};last=-1;last_state=None;stopped_at=None;accepted_at=None

def vector(p):return [p.x,p.y,p.z]

def assert_unit_in_slot(point):
    slot=unreal.Transform(location=point)
    for name in ('ExportBPallet','ExportBBox'):
        part=live[name].get_component_by_class(unreal.StaticMeshComponent)
        bounds=part.static_mesh.get_bounds()
        for x in (-1,1):
            for y in (-1,1):
                for z in (-1,1):
                    local=bounds.origin+bounds.box_extent*unreal.Vector(x,y,z)
                    world=unreal.MathLibrary.transform_location(part.get_world_transform(),local)
                    p=unreal.MathLibrary.inverse_transform_location(slot,world)
                    assert abs(p.x)<=58.5 and abs(p.y)<=58.5,('Unit outside reserved footprint',name,vector(p))
    assert abs(live['ExportBPallet'].get_actor_location().z-point.z)<.5
def finish(ok,detail):
    vehicle=live['ShippingAGV'] if live else None
    reports=[dict(t=r.simulation_seconds,result=r.result,detail=r.detail) for r in vehicle.job_reports] if vehicle else []
    reverse_reports=[dict(t=r.simulation_seconds,result=r.result,detail=r.detail) for r in live['ReverseSideAGV'].job_reports] if live and round_trip else []
    (out/'Report.json').write_text(json.dumps(dict(passed=ok,case=case,rack_side=side,round_trip=round_trip,rack_level_cm=rack_level,detail=detail,samples=rows,reports=reports,reverse_reports=reverse_reports,
        neighbor_baseline=baseline,saved_map_written=False,runtime_motion_forced=False,
        scope='Saved real rack/vehicle in unsaved mapped fixture; 50kg incoming unit, 45kg neighbouring stock; not all layouts/loads'),ensure_ascii=False,indent=2),encoding='utf-8')
    print('RACK_NEIGHBOR_RESULT',case,ok,detail)
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def tick(dt):
    global phase,live,last,last_state,baseline,stopped_at,accepted_at
    try:
        assert time.monotonic()-began<300,('Wall timeout',phase)
        if phase=='start':level.editor_request_begin_play();phase='settle';return
        world=editor.get_game_world()
        if not world:return
        now=unreal.GameplayStatics.get_time_seconds(world)
        if live is None:
            live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor)}
            unreal.GameplayStatics.get_player_controller(world,0).set_simulation_speed(4)
        vehicle,pallet=live['ReverseSideAGV' if phase=='retrieve' else 'ShippingAGV'],live['ExportBPallet']
        for name in ('ExportBPallet','ExportBBox','NeighborPallet','NeighborCargo'):
            assert live[name].root_component.is_simulating_physics(),('Physics disabled',name)
        if phase=='settle':
            if now<5 or pallet.receipt_state!=unreal.WarehouseReceiptState.CONFIRMED:
                assert now<30,('Receipt did not confirm',pallet.receipt_state);return
            baseline={name:vector(live[name].get_actor_location()) for name in ('NeighborPallet','NeighborCargo')}
            job=unreal.WarehouseWorkOrder(job_id='RACK-NEIGHBOR-'+case,pallet=pallet,rack_destination=True,
                destination=unreal.Transform(location=unreal.Vector(1200,900,rack_level)),destination_resource=live['ShippingRack0'],requires_destination_resource=True)
            assert vehicle.submit_job(job),vehicle.status
            vehicle.toggle_power();phase='transport';accepted_at=now
        for name,pos in baseline.items():
            delta=live[name].get_actor_location()-unreal.Vector(*pos)
            assert delta.length()<2,('Neighbour stock was hit/moved/fell',name,vector(delta),vehicle.status)
        if now-last>.2 or last_state!=vehicle.ai_state:
            rows.append(dict(t=now,leg=phase,state=str(vehicle.ai_state),status=vehicle.status,vehicle=vector(vehicle.get_actor_location()),
                pallet=vector(pallet.get_actor_location()),neighbor=vector(live['NeighborPallet'].get_actor_location()),speed=vehicle.current_speed_cm))
            last=now;last_state=vehicle.ai_state
        if vehicle.completed_job_ids:
            assert case=='normal','Unsafe rack insertion was reported complete'
            if phase=='retrieve':
                goal=unreal.Vector(2400 if side=='front' else 0,1500,0)
                assert_unit_in_slot(goal)
                entries=[r['vehicle'][0]-1200 for r in rows if r['leg']=='retrieve' and 'INSERT_FORK:' in r['state']]
                assert entries and (min(entries)>0 if side=='front' else max(entries)<0),('Wrong retrieval aisle',entries)
                finish(True,'Actual rack insertion and opposite-aisle retrieval completed; no neighbouring stock displacement');return
            assert_unit_in_slot(unreal.Vector(1200,900,rack_level))
            if round_trip:
                entries=[r['vehicle'][0]-1200 for r in rows if r['leg']=='transport' and 'LOWER_LOAD:' in r['state']]
                assert entries and (max(entries)<0 if side=='front' else min(entries)>0),('Wrong insertion aisle',entries)
                reverse=live['ReverseSideAGV']
                goal=unreal.Transform(location=unreal.Vector(2400 if side=='front' else 0,1500,0))
                assert reverse.submit_job(unreal.WarehouseWorkOrder(job_id='RACK-RETRIEVE-'+side,pallet=pallet,destination=goal)),reverse.status
                reverse.toggle_power();phase='retrieve';accepted_at=now;return
            finish(True,'Actual pickup, rack insertion, support handoff and fork withdrawal; neighbouring dynamic stock unchanged');return
        fault=vehicle.ai_state==unreal.WarehouseAIState.FAULT
        deferred=next((r for r in vehicle.job_reports if r.result=='DEFERRED'),None)
        if fault or deferred:
            assert case!='normal',('Normal rack transfer failed',vehicle.status)
            reason=vehicle.status if fault else deferred.detail
            assert any(word in reason for word in ('DESTINATION','RACK','DELIVERY','OBSERVATION','PRECISE')),('Not a rack-clearance rejection',reason)
            if stopped_at is None:stopped_at=now
            if now-stopped_at>=2:
                assert abs(vehicle.current_speed_cm)<3,'Unsafe insertion did not brake'
                finish(True,'Unsafe rack/whole-load clearance rejected and physically braked; no neighbouring stock displacement');return
        assert now-accepted_at<220,('Transport timeout',vehicle.status)
    except Exception:finish(False,traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
