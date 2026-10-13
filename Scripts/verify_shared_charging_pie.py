"""Four physical AGVs, two shared chargers, two parking bays; no teleporting during PIE."""
import json, sys, time, traceback, unreal
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from apply_real_world_scale import fit
IDLE_ONLY='-sharedidleonly' in unreal.SystemLibrary.get_command_line().lower()

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
saved={a.get_actor_label():a for a in actors.get_all_level_actors()}
source=saved['WH_AutonomousForklift']
rig={c.get_name():(c.static_mesh,list(c.get_editor_property('override_materials'))) for c in source.get_components_by_class(unreal.StaticMeshComponent)}
poses={c.get_name():unreal.MathLibrary.make_relative_transform(c.get_world_transform(),source.get_actor_transform())
       for c in source.get_components_by_class(unreal.SceneComponent) if c!=source.root_component}
assert level.load_level('/Engine/Maps/Entry')
for a in list(actors.get_all_level_actors()):
    if not isinstance(a,(unreal.WorldSettings,unreal.LevelScriptActor)):actors.destroy_actor(a)
world=editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WorldSettings)[0].set_editor_property('default_game_mode',unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))

def spawn(cls,name,p):
    a=actors.spawn_actor_from_class(cls,unreal.Vector(*p));a.set_actor_label(name);return a

floor=spawn(unreal.StaticMeshActor,'SharedFloor',(200,1300,-10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.static_mesh_component.set_collision_profile_name('BlockAll');floor.set_actor_scale3d(unreal.Vector(50,55,.2))
spawn(unreal.PlayerStart,'SharedPlayer',(-1500,-1000,96.5))
vehicles=[]
for i,battery in enumerate((100,100,100,100) if IDLE_ONLY else (20,25,28,60)):
    start=(650,i*900,0) if IDLE_ONLY and i<2 else (-350,i*900,0) if IDLE_ONLY else (0,i*900,0)
    v=spawn(unreal.WarehouseForklift,'SharedAGV%d'%i,start);v.set_editor_property('auto_start',False)
    v.set_editor_property('vehicle_name','공유 충전 시험 %d'%(i+1));v.set_editor_property('battery_percent',battery)
    v.set_editor_property('pending_jobs',[]);v.set_editor_property('target_pallet',None);v.set_editor_property('charging_station',None)
    v.set_editor_property('opportunity_charge_idle_delay_seconds',1)
    for c in v.get_components_by_class(unreal.StaticMeshComponent):
        mesh,materials=rig[c.get_name()];c.set_static_mesh(mesh);c.set_editor_property('override_materials',materials)
    def depth(c):
        result=0
        while c.get_attach_parent():result+=1;c=c.get_attach_parent()
        return result
    for c in sorted(v.get_components_by_class(unreal.SceneComponent),key=depth):
        if c==v.root_component:continue
        assert c.get_name() in poses
        c.set_world_transform(unreal.MathLibrary.compose_transforms(poses[c.get_name()],v.get_actor_transform()),False,True)
        assert unreal.MathLibrary.nearly_equal_transform_transform(unreal.MathLibrary.make_relative_transform(c.get_world_transform(),v.get_actor_transform()),poses[c.get_name()],.001,.000001,.000001)
    vehicles.append(v)
for i in range(2):
    charger=spawn(unreal.WarehouseChargingStation,'SharedCharger%d'%i,(650,i*900,0))
    if IDLE_ONLY:
        vehicles[i].set_editor_property('charging_station',charger);charger.set_editor_property('assigned_vehicle',vehicles[i])
for i in range(2):
    b=spawn(unreal.WarehouseWaitingBay,'SharedWaiting%d'%i,(-350,(i+2)*900,0));vehicles[i+2].set_editor_property('waiting_bay',b)
pallet=spawn(unreal.WarehousePallet,'SharedWorkPallet',(500,2700,.1))
cargo=spawn(unreal.WarehouseCargo,'SharedWorkCargo',(500,2700,25.2))
cargo.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2'));cargo.set_gross_mass_kg(12)
fit(cargo,(30,25,20),(500,2700,25.2))
damage=spawn(unreal.WarehouseDamageSystem,'SharedStrength',(0,0,0))
specs=[unreal.WarehouseStrength(name='SharedFloor',members=[floor],mass_kg=50000,rated_load_kg=100000,impact_yield_j=100000,impact_failure_j=1000000,failure=unreal.WarehouseFailure.STRUCTURE)]
specs += [unreal.WarehouseStrength(name='SharedPallet',members=[pallet],mass_kg=25,rated_load_kg=1500),unreal.WarehouseStrength(name='SharedCargo',members=[cargo],mass_kg=12,rated_load_kg=120)]
damage.set_editor_property('objects',specs)

output=Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/SharedCharging');output.mkdir(parents=True,exist_ok=True)
started=time.monotonic();phase='start';game=live=None;at=0;last=-1;records=[];charged=set();saw_busy=saw_parking=opportunity_submitted=opportunity_left=False

def finish(ok,detail):
    (output/('IdleReport.json' if IDLE_ONLY else 'Report.json')).write_text(json.dumps(dict(ok=ok,detail=detail,charged=sorted(charged),saw_busy=saw_busy,saw_parking=saw_parking,opportunity_submitted=opportunity_submitted,opportunity_left=opportunity_left,samples=records,wall_seconds=time.monotonic()-started),ensure_ascii=False,indent=2),encoding='utf-8')
    print('SHARED_CHARGING_PASSED' if ok else 'SHARED_CHARGING_FAILED',detail)
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def tick(dt):
    global phase,game,live,at,last,saw_busy,saw_parking,opportunity_submitted,opportunity_left
    try:
        assert time.monotonic()-started<650,('Wall timeout',phase)
        if phase=='start':level.editor_request_begin_play();phase='settle';return
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase=='settle':
            if now<2:return
            live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseSimulationClock)[0].set_speed(4)
            for i in range(4):live['SharedAGV%d'%i].toggle_power()
            at=now;phase='run';return
        current=[live['SharedAGV%d'%i] for i in range(4)]
        stations=[live['SharedCharger%d'%i] for i in range(2)]
        occupied=[s.get_editor_property('occupant') for s in stations if s.get_editor_property('occupant')]
        assert len(set(occupied))==len(occupied) and len(occupied)<=2
        for i,v in enumerate(current):
            assert v.root_component.is_simulating_physics() and abs(v.get_physical_mass_kg()-1000)<.5
            assert not v.get_editor_property('mechanical_failure'),v.get_editor_property('status')
            assert v.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,v.get_editor_property('status')
            state=v.get_editor_property('ai_state')
            if i<3:
                v.set_editor_property('battery_time_scale',3600 if state==unreal.WarehouseAIState.CHARGING else 1)
                if v.get_editor_property('battery_percent')>=79.9:charged.add(i)
            saw_busy |= v.get_editor_property('last_error_code')=='CHARGERS_BUSY'
            bay=v.get_editor_property('waiting_bay')
            saw_parking |= bool(bay and i>=2 and (v.get_actor_location()-bay.get_actor_location()).length()<5)
        # The first two required charging reservations go to the lower batteries.
        if 2<now-at<6 and len(occupied)==2:
            assert set(occupied)==set(current[:2]),[(v.get_actor_label(),v.get_editor_property('battery_percent')) for v in occupied]
        fourth=current[3]
        if IDLE_ONLY:
            if now-at<8:return
            assert all(v.get_editor_property('ai_state')==unreal.WarehouseAIState.READY and v.get_editor_property('battery_percent')>99 for v in current),'Idle parking altered state or discarded battery'
            assert all(stations[i].is_docked(current[i]) for i in range(2)),'High-battery dock parking left its pose'
            bays=[live['SharedWaiting%d'%i] for i in range(2)]
            assert all(bays[i].get_editor_property('occupant')==current[i+2] and (current[i+2].get_actor_location()-bays[i].get_actor_location()).length()<5 for i in range(2)),'Physically occupied waiting spots were stolen'
            finish(True,'4 physical 100% AGVs idle in 2 charging spots + 2 ordinary waiting spots; occupied parking cannot be stolen and no battery clamping/charging')
            return
        if fourth.get_editor_property('ai_state')==unreal.WarehouseAIState.CHARGING and not opportunity_submitted:
            assert fourth.can_accept_new_work(),'Opportunity charging incorrectly excludes a new job'
            assert fourth.submit_job(unreal.WarehouseWorkOrder(job_id='OPPORTUNITY-PREEMPT',pallet=live['SharedWorkPallet'],destination=unreal.Transform(location=unreal.Vector(950,2700,0))))
            opportunity_submitted=True
        if opportunity_submitted and fourth.get_editor_property('ai_state')!=unreal.WarehouseAIState.CHARGING:opportunity_left=True
        if now-last>=2:
            last=now
            row=dict(time=round(now,2),vehicles=[dict(id=i,state=str(v.get_editor_property('ai_state')),battery=round(v.get_editor_property('battery_percent'),3),status=v.get_editor_property('status'),position=str(v.get_actor_location()),yaw=v.get_actor_rotation().yaw,speed=v.get_editor_property('current_speed_cm')) for i,v in enumerate(current)],occupants=[s.get_editor_property('occupant').get_actor_label() if s.get_editor_property('occupant') else None for s in stations])
            records.append(row);print('SHARED_CHARGING_SAMPLE',json.dumps(row,ensure_ascii=False))
        if len(charged)==3 and opportunity_submitted and opportunity_left and 'OPPORTUNITY-PREEMPT' in fourth.get_editor_property('completed_job_ids'):
            assert saw_busy and saw_parking
            assert live['SharedWorkCargo'] in live['SharedStrength'].get_supported_actors(live['SharedWorkPallet'])
            assert fourth.is_floor_placement_valid(live['SharedWorkPallet'],unreal.Transform(location=unreal.Vector(950,2700,0)))
            finish(True,'4 real AGVs / 2 shared chargers / 2 waiting bays; lower-battery allocation, occupied waiting, native docking and charge, opportunity-charge preemption and completed physical transport')
        assert now-at<750,('Simulation timeout',records[-1] if records else None)
    except Exception:finish(False,traceback.format_exc())

handle=unreal.register_slate_post_tick_callback(tick)
