"""Integration test of saved mezzanine geometry and FMS/lift handshakes (never saves)."""
import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
v=named['WH_AutonomousForklift'];lift=named['MZ_AGVElevator'];p=named['WH_TrainingPallet']
S=unreal.WarehouseAIState;E=unreal.WarehouseElevatorState
assert v.get_editor_property('elevator')==lift
assert named['WH_Roof'].get_actor_bounds(False)[0].z==1215
for f in (1,2):
    deck=named[f'MZ_L{f+1}_Deck_Right'];c,e=deck.get_actor_bounds(False);assert abs(c.z+e.z-f*400)<.1
    assert any(label.startswith(f'MZ_L{f+1}_Rack_') for label in named)
    assert lift.floor_at_height(f*400)==f
print('MEZZANINE_GEOMETRY_PASSED')

def run_until(predicate,limit=40000):
    seen=set();es=set()
    for i in range(limit):
        v.advance_simulation(.05);lift.advance_elevator(.05)
        s=v.get_editor_property('ai_state');seen.add(s);es.add(lift.get_editor_property('state'))
        if predicate():return seen,es
        if s==S.FAULT:
            print('BOARDING_DIAG',v.get_actor_bounds(True),v.get_transfer_mass_kg(),v.is_lift_at_travel_height(),v.get_editor_property('current_speed_cm'),lift.can_enter(v),lift.check_interlocks())
        assert s!=S.FAULT,(i,v.get_editor_property('status'),v.get_actor_location(),lift.get_editor_property('status'))
    raise AssertionError(('TIMEOUT',v.get_actor_location(),v.get_editor_property('status'),lift.get_editor_property('status')))

assert not lift.can_enter(v) and not lift.can_exit(v)
assert not lift.request_transfer(v,0,2),'Call allowed away from waiting point'
v.toggle_power()
run_until(lambda:v.get_editor_property('ai_state')==S.ELEVATOR_WAIT)
assert not lift.can_enter(v),'Permission before fully open'
run_until(lambda:v.get_editor_property('ai_state')==S.ELEVATOR_BOARD)
# Competing reservations and early boarding confirmation must fail.
other=actors.spawn_actor_from_class(unreal.WarehouseForklift,unreal.Vector(1500,-1200,0))
assert not lift.request_transfer(other,0,1)
actors.destroy_actor(other)
assert not lift.confirm_boarded(v)
run_until(lambda:lift.get_editor_property('state')==E.CLOSING_LOADED)
person=actors.spawn_actor_from_class(unreal.Character,unreal.Vector(1000,1395,96))
person.get_component_by_class(unreal.CapsuleComponent).set_collision_profile_name('Pawn')
start=v.get_actor_location()
for _ in range(70):v.advance_simulation(.05);lift.advance_elevator(.05)
assert lift.get_editor_property('state')==E.CLOSING_LOADED and v.get_actor_location()==start,'Door closed through a person'
actors.destroy_actor(person)
print('MEZZANINE_DOOR_PERSON_INTERLOCK_PASSED')
run_until(lambda:lift.get_editor_property('state')==E.TRAVELLING)
start=v.get_actor_location();v.toggle_power()
for _ in range(40):v.advance_simulation(.05);lift.advance_elevator(.05)
assert v.get_actor_location()==start,'Lift moved while vehicle paused'
v.toggle_power()
run_until(lambda:v.get_actor_location().z>100)
# Elevator E-stop latches the vehicle fault; maintenance must clear and explicitly restart.
lift.set_editor_property('e_stop_released',False)
v.advance_simulation(.05);lift.advance_elevator(.05)
assert v.get_editor_property('ai_state')==S.FAULT
start=v.get_actor_location()
for _ in range(40):v.advance_simulation(.05);lift.advance_elevator(.05)
assert v.get_actor_location()==start
lift.set_editor_property('e_stop_released',True);v.toggle_power()
seen,es=run_until(lambda:'DEMO-L1-L3' in v.get_editor_property('completed_job_ids'))
assert S.ELEVATOR_EXIT in seen and E.OPENING_EXIT in es
assert (p.get_actor_location()-unreal.Vector(1000,-500,800)).length()<2
assert lift.get_editor_property('completed_transfers')==1
cargo=named['MZ_TransferCargo'];assert not cargo.get_attach_parent_actor()
assert cargo.get_actor_bounds(False)[0].z>815
print('MEZZANINE_LOADED_L1_L3_PAUSE_ESTOP_PASSED')

# Return from the upper floor to the real ground-floor charger, leaving the pallet behind.
run_until(lambda:v.get_editor_property('ai_state')==S.READY)
v.set_editor_property('battery_percent',29)
run_until(lambda:v.get_editor_property('ai_state')==S.CHARGING)
assert abs(v.get_actor_location().z)<.1
v.set_editor_property('battery_time_scale',3600)
run_until(lambda:v.get_editor_property('ai_state')==S.LEAVE_CHARGER)
v.set_editor_property('battery_time_scale',1)
run_until(lambda:v.get_editor_property('ai_state')==S.READY)
assert v.get_editor_property('battery_percent')>79
assert lift.get_editor_property('completed_transfers')==2
print('MEZZANINE_UPPER_FLOOR_CHARGING_PASSED')

# Use the same real pallet for L3 -> L2 -> L1; do not teleport the vehicle.
for floor in (1,0):
    run_until(lambda:v.get_editor_property('ai_state')==S.READY)
    job=unreal.WarehouseWorkOrder(job_id=f'TEST-L{floor+1}',pallet=p,
        destination=unreal.Transform(location=unreal.Vector(1000,-500,floor*400),rotation=unreal.Rotator(yaw=90)))
    assert v.submit_job(job)
    run_until(lambda:job.job_id in v.get_editor_property('completed_job_ids'))
    assert abs(p.get_actor_location().z-floor*400)<2
    print('MEZZANINE_RETURN_PASSED',floor+1)
# Overload must fault before reserving/boarding, including chassis mass.
run_until(lambda:v.get_editor_property('ai_state')==S.READY)
p.set_editor_property('payload_mass_kg',1100)
job=unreal.WarehouseWorkOrder(job_id='OVERLOAD-LIFT',pallet=p,destination=unreal.Transform(location=unreal.Vector(1000,-500,800),rotation=unreal.Rotator(yaw=90)))
assert v.submit_job(job)
run_until(lambda:v.get_editor_property('ai_state')==S.FAULT)
assert 'ELEVATOR OVERLOAD' in v.get_editor_property('status'),v.get_editor_property('status')
assert lift.get_editor_property('completed_transfers')==5
print('MEZZANINE_TOTAL_MASS_INTERLOCK_PASSED')
print('MEZZANINE_VERIFY_PASSED')
