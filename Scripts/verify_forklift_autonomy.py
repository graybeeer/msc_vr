"""Unreal integration checks. All simulations are transient and never saved."""
import unreal
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from configure_forklift_autonomy import configure_autonomy
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
v=configure_autonomy(True)
S=unreal.WarehouseAIState

def run_until(vehicle, predicate, limit=20000):
    seen=set()
    for i in range(limit):
        vehicle.advance_simulation(.05)
        state=vehicle.get_editor_property('ai_state'); seen.add(state)
        if predicate(vehicle): return seen
        assert state!=S.FAULT, (i,state,vehicle.get_editor_property('status'),vehicle.get_actor_location())
    raise AssertionError(('Timeout',vehicle.get_editor_property('ai_state'),vehicle.get_editor_property('status'),vehicle.get_actor_location()))

# Power-on interlocks, including independent brake and steering faults.
for field in ['safety_scanner_healthy','e_stop_released','brake_healthy','steering_healthy','fork_sensor_healthy','localization_healthy','battery_healthy','pallet_sensor_healthy','load_sensor_healthy']:
    v.set_editor_property(field,False); v.toggle_power(); v.advance_simulation(.05)
    assert v.get_editor_property('ai_state')==S.FAULT, field
    v.set_editor_property(field,True)
# Resume self-check, explicit E pause must never auto-restart.
v.toggle_power(); v.advance_simulation(.05); v.toggle_power()
pos=v.get_actor_location()
for _ in range(50): v.advance_simulation(.05)
assert v.get_editor_property('ai_state')==S.PAUSED and v.get_actor_location()==pos
v.toggle_power()
seen=run_until(v,lambda a:'DEMO-001' in a.get_editor_property('completed_job_ids'))
required=[S.SELF_CHECK,S.READY,S.VALIDATE_JOB,S.PLAN_PICKUP,S.NAVIGATE_PICKUP,S.SLOW_APPROACH,S.DETECT_PALLET,S.ESTIMATE_POSE,S.ALIGN_VEHICLE,S.CORRECT_FORK,S.INSERT_FORK,S.VERIFY_INSERTION,S.LIFT_LOAD,S.VERIFY_LOAD,S.DEPART_PICKUP,S.TRAVEL_HEIGHT,S.PLAN_DELIVERY,S.NAVIGATE_DELIVERY,S.ALIGN_UNLOAD,S.LOWER_LOAD,S.WITHDRAW_FORK,S.VERIFY_UNLOAD,S.REPORT_COMPLETE]
assert all(s in seen for s in required),('Missing states',set(required)-seen)
reports=v.get_editor_property('job_reports')
assert sum(r.get_editor_property('result')=='COMPLETED' for r in reports)==1
p=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WH_TrainingPallet')
assert (p.get_actor_location()-unreal.Vector(1000,400,0)).length()<2
assert not p.get_attach_parent_actor()
for _ in range(50): v.advance_simulation(.05)
assert v.get_editor_property('ai_state')==S.READY
job=unreal.WarehouseWorkOrder(job_id='DEMO-001',pallet=p,destination=unreal.Transform(location=unreal.Vector(1000,-700,0),rotation=unreal.Rotator(yaw=90)))
assert not v.submit_job(job),'Duplicate completed order accepted'
print('AUTONOMY_TRANSPORT_PASSED',len(seen))
# Charging route now starts at delivery end, not the original spawn position.
v.set_editor_property('battery_percent',29)
v.set_editor_property('battery_time_scale',1)
charge=run_until(v,lambda a:S.CHARGING==a.get_editor_property('ai_state'))
assert S.NAVIGATE_CHARGE in charge and S.DOCK_CHARGE in charge
v.set_editor_property('battery_time_scale',3600)
run_until(v,lambda a:a.get_editor_property('ai_state')==S.LEAVE_CHARGER)
v.set_editor_property('battery_time_scale',1)
run_until(v,lambda a:a.get_editor_property('ai_state')==S.READY)
assert v.get_editor_property('battery_percent')>79
print('AUTONOMY_CHARGING_PASSED')




def fresh(job_id='TEST', destination=(1000,400,0), yaw=90):
    global v,p
    actors.destroy_actor(v)
    p.set_actor_location(unreal.Vector(1000,-700,0),False,True)
    p.set_actor_rotation(unreal.Rotator(yaw=90),True)
    v=actors.spawn_actor_from_class(unreal.WarehouseForklift,unreal.Vector(1000,-1000,0),unreal.Rotator(yaw=90))
    v.set_editor_property('target_pallet',p)
    job=unreal.WarehouseWorkOrder(job_id=job_id,pallet=p,destination=unreal.Transform(location=unreal.Vector(*destination),rotation=unreal.Rotator(yaw=yaw)))
    assert v.submit_job(job)
    assert not v.submit_job(job)
    v.toggle_power()
    return v

# A person causes an automatic safety wait; removing the person allows resumption.
fresh('PERSON')
run_until(v,lambda a:a.get_editor_property('ai_state')==S.INSERT_FORK)
person=actors.spawn_actor_from_class(unreal.Character,unreal.Vector(1000,-710,96))
person.get_component_by_class(unreal.CapsuleComponent).set_collision_profile_name('Pawn')
pos=v.get_actor_location()
run_until(v,lambda a:a.get_editor_property('ai_state')==S.WAITING_OBSTACLE)
assert v.get_actor_location()==pos
actors.destroy_actor(person)
run_until(v,lambda a:a.get_editor_property('ai_state')==S.INSERT_FORK)
# A persistent steering fault must not forget the interrupted job during repeated failed self-checks.
v.set_editor_property('steering_healthy',False);v.advance_simulation(.05)
assert v.get_editor_property('ai_state')==S.FAULT
v.toggle_power();v.advance_simulation(.05)
assert v.get_editor_property('ai_state')==S.FAULT
v.set_editor_property('steering_healthy',True);v.toggle_power()
# Pausing again during the recovery self-check must retain the interrupted insertion.
v.advance_simulation(.05);v.toggle_power();v.toggle_power()
run_until(v,lambda a:'PERSON' in a.get_editor_property('completed_job_ids'))
print('AUTONOMY_SAFETY_RESUME_PASSED')

# Deliberately short insertion must fail the real geometry check, not report completion.
fresh('BIAS')
v.set_editor_property('pallet_position_bias_cm',unreal.Vector(0,-20,0))
run_until(v,lambda a:a.get_editor_property('ai_state')==S.FAULT)
assert 'INSERTION' in v.get_editor_property('status'),v.get_editor_property('status')
assert not p.get_attach_parent_actor() and not v.get_editor_property('completed_job_ids')
print('AUTONOMY_BAD_INSERTION_PASSED')

# A quarter-turn delivery with an actual supported cargo box tests load transfer and turn planning.
fresh('TURN',(1500,200,0),0)
cargo=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WH_Cargo_Left_02_1_Top')
body=cargo.get_component_by_class(unreal.StaticMeshComponent)
body.set_simulate_physics(False)
center,extent=cargo.get_actor_bounds(False)
body.set_world_location(body.get_world_location()+unreal.Vector(1000,-700,15+extent.z)-center,False,True)
run_until(v,lambda a:a.get_editor_property('ai_state')==S.VERIFY_LOAD)
assert cargo.get_attach_parent_actor()==p,'Supported cargo was not carried'
run_until(v,lambda a:a.get_editor_property('ai_state')==S.NAVIGATE_DELIVERY)
route=v.get_editor_property('planned_route')
assert len(route)>10
angles=[point.get_editor_property('pose').rotation.rotator().yaw for point in route]
assert max(angles)-min(angles)>60,'Route did not turn'
run_until(v,lambda a:'TURN' in a.get_editor_property('completed_job_ids'))
assert abs(cargo.get_actor_location().x-1500)<70 and abs(cargo.get_actor_location().y-200)<70
assert not cargo.get_attach_parent_actor() and body.is_simulating_physics()
print('AUTONOMY_TURN_CARGO_PASSED')
# Occupied destinations are rejected before any movement.
fresh('OCCUPIED',(1500,200,0),0)
obstacle=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(1500,200,50))
obstacle.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
obstacle.static_mesh_component.set_collision_profile_name('BlockAll')
run_until(v,lambda a:a.get_editor_property('ai_state')==S.FAULT)
assert 'DESTINATION' in v.get_editor_property('status')
assert v.get_actor_location()==unreal.Vector(1000,-1000,0)
print('AUTONOMY_DESTINATION_GUARD_PASSED')
print('AUTONOMY_VERIFY_PASSED')
