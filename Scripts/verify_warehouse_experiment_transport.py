"""Fresh-world A/B repetitions using real AGV pickup/delivery. Saves only an ignored test map."""
import json, sys, time, traceback, hashlib
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0,str(root/'Scripts'))
from apply_real_world_scale import fit
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
output=root/'Saved/Testing/WarehouseExperiments'
output.mkdir(parents=True,exist_ok=True)
original=root/'Content/FirstPerson/Lvl_FirstPerson.umap'
original_hash=hashlib.sha256(original.read_bytes()).hexdigest()
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
rig={c.get_name():(c.get_relative_transform(),c.static_mesh,list(c.get_editor_property('override_materials')))
     for c in named['WH_AutonomousForklift'].get_components_by_class(unreal.StaticMeshComponent)}
assert unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
for a in list(actors.get_all_level_actors()):
    if not isinstance(a,(unreal.WorldSettings,unreal.LevelScriptActor)):actors.destroy_actor(a)
def spawn(cls,label,p,yaw=0):
    a=actors.spawn_actor_from_class(cls,unreal.Vector(*p),unreal.Rotator(yaw=yaw))
    a.set_actor_label(label)
    a.set_editor_property('is_spatially_loaded',False)
    return a
world=editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WorldSettings)[0].set_editor_property('default_game_mode',
    unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
floor=spawn(unreal.StaticMeshActor,'ExperimentFloor',(0,0,-10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(55,45,.2))
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.tags=[unreal.Name('WarehouseMapStructure')]
spawn(unreal.PlayerStart,'ExperimentStart',(-1000,-1500,96.5))
console=spawn(unreal.WarehouseComputer,'ExperimentComputer',(-900,-1600,0))
console.set_editor_property('outbound_centre',unreal.Vector(1200,0,0))
console.set_editor_property('outbound_half_size',unreal.Vector(150,1000,80))
console.set_editor_property('outbound_destinations',[unreal.Transform(location=unreal.Vector(1200,y,0)) for y in (-500,500)])
specs=[]
for index,y in enumerate((-500,500)):
    v=spawn(unreal.WarehouseForklift,f'ExperimentAGV{index}',(0,y,0))
    v.set_editor_property('auto_start',False)
    v.set_editor_property('vehicle_name',f'Experiment vehicle {index+1}')
    for c in v.get_components_by_class(unreal.StaticMeshComponent):
        t,m,materials=rig[c.get_name()]
        c.set_relative_transform(t,False,True)
        c.set_static_mesh(m)
        c.set_editor_property('override_materials',materials)
    p=spawn(unreal.WarehousePallet,f'ExperimentPallet{index}',(500,y,.1))
    c=spawn(unreal.WarehouseCargo,f'ExperimentCargo{index}',(500,y,30.2))
    c.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2'))
    assert c.apply_cargo_recipe(unreal.WarehouseCargo.generate_cargo_recipe(20261025,index,0,unreal.Vector(40,30,30),1))
    fit(c,(40,30,30),(500,y,30.2))
    specs += [unreal.WarehouseStrength(name=p.get_actor_label(),members=[p],mass_kg=25,rated_load_kg=1500),
              unreal.WarehouseStrength(name=c.get_actor_label(),members=[c],mass_kg=c.get_editor_property('gross_mass_kg'),rated_load_kg=120)]
spawn(unreal.WarehouseDamageSystem,'ExperimentStrength',(0,-1900,0)).set_editor_property('objects',specs)
spawn(unreal.DirectionalLight,'ExperimentSun',(0,0,600),-25)
assert unreal.EditorLoadingAndSavingUtils.save_map(world,'/Game/__ExperimentTests/Transport')
unreal.SystemLibrary.execute_console_command(editor.get_editor_world(),'t.OverrideFPS 0')
started=time.monotonic()
phase='settle'
sub=None
layout_ids=[]
last_log=0
baseline_results=0
test_tag='Transport_'+str(time.time_ns())

def finish(ok,error=''):
    result={'passed':ok,'error':error,'layout_ids':layout_ids,'wall_seconds':time.monotonic()-started,
            'original_map_unchanged':hashlib.sha256(original.read_bytes()).hexdigest()==original_hash}
    if sub:result['status']=json.loads(sub.get_status_json())
    (output/'Transport.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('WAREHOUSE_EXPERIMENT_TRANSPORT_'+('PASSED' if ok else 'FAILED'),error)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():unreal.SystemLibrary.quit_editor()

def run_tick(dt):
    global phase,sub,layout_ids,last_log,baseline_results
    try:
        assert time.monotonic()-started<1500,'Transport experiment wall timeout'
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        pc=unreal.GameplayStatics.get_player_controller(game,0)
        if sub is None:
            sub=next(s for s in unreal.ObjectIterator(unreal.WarehouseExperimentSubsystem)
                     if s.get_outer()==unreal.GameplayStatics.get_game_instance(game))
            assert sub
            baseline_results=json.loads(sub.get_status_json())['result_count']
        if time.monotonic()-last_log>15:
            last_log=time.monotonic()
            print('EXPERIMENT_PROGRESS',phase,sub.get_editor_property('last_message'))
        if phase=='settle':
            if now<4:return
            pc.set_layout_editing(True)
            assert sub.save_layout(test_tag+' A'),sub.get_editor_property('last_message')
            data=json.loads(sub.get_status_json())
            a=next(x['id'] for x in data['layouts'] if x['name']==test_tag+' A')
            # Move a complete native rig with the existing layout transaction, not isolated mesh bodies.
            layout=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseLayoutEditor)[0]
            vehicle=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseForklift)[0]
            p=vehicle.get_actor_location()
            layout.set_category(unreal.WarehouseLayoutCategory.FORKLIFT)
            assert layout.begin_drag(vehicle,p)
            layout.update_drag(p+unreal.Vector(-200,0,0))
            assert layout.end_drag(True),layout.get_editor_property('status')
            assert sub.save_layout(test_tag+' B'),sub.get_editor_property('last_message')
            data=json.loads(sub.get_status_json())
            b=next(x['id'] for x in data['layouts'] if x['name']==test_tag+' B')
            layout_ids=[a,b]
            assert len({x['cargo_hash'] for x in data['layouts'] if x['id'] in layout_ids})==1,'Layout movement changed cargo conditions'
            for source,destination in zip(data['sources'],data['destinations']):
                assert sub.add_order(source['id'],destination['id'],0,''),sub.get_editor_property('last_message')
            assert len(sub.get_editor_property('orders'))==2
            assert sub.save_workload('Transport fixed orders')
            assert sub.start_batch(layout_ids,2,180,42,4),sub.get_editor_property('last_message')
            phase='batch'
            return
        if phase=='batch':
            state=json.loads(sub.get_status_json())
            for v in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseForklift):
                assert abs(v.get_editor_property('empty_travel_speed_cm')-180)<.01,'Maximum speed changed'
            if state['running'] or state['loading']:return
            assert state['result_count']==baseline_results+4,state
            assert state['metrics']['summary']['completed']==2,state
            runs=[]
            for f in (root/'Saved/WarehouseExperiments/Runs').glob('*/Result.json'):
                r=json.loads(f.read_text(encoding='utf-8-sig'))
                if r['layout_id'] in layout_ids:runs.append(r)
            assert len(runs)==4
            for r in runs:
                s=r['summary']
                assert r['status']=='completed' and s['completed']==2 and s['failed']==0 and s['unfinished']==0,(r['layout_id'],s)
                assert abs(s['elapsed_seconds']-180)<.1
                assert abs(s['throughput_per_hour']-40)<.05
                for j in r['jobs']:
                    assert abs(sum(j['phase_seconds'].values())-j['elapsed_seconds'])<.05,j
                    assert j['phase_seconds']['loaded_travel']>0 and j['phase_seconds']['pickup']>0,j
                assert sum(v['loaded_distance_metres'] for v in r['vehicles'])>5
            assert len({r['scenario_fingerprint'] for r in runs})==1
            assert len({r['cargo_fingerprint'] for r in runs})==1
            assert sub.export_csv()
            assert hashlib.sha256(original.read_bytes()).hexdigest()==original_hash
            finish(True)
    except Exception:
        finish(False,traceback.format_exc())

next_poll=0
busy=False
def tick(dt):
    global next_poll,busy
    if busy or time.monotonic()<next_poll:return
    next_poll=time.monotonic()+.5
    busy=True
    try:run_tick(dt)
    finally:busy=False

handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
