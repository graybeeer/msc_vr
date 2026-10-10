"""Headless editor integration checks; never saves the transient test changes."""
import unreal
import math

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
vehicle = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'WH_AutonomousForklift')
vehicle.set_editor_property('autonomous_mode',False)
pallet = vehicle.get_editor_property('target_pallet')
# Straight-cycle regression uses an empty pallet. Actual loaded cargo is covered
# by verify_mezzanine / verify_refined_agv and the autonomous cargo attachment path.
for cargo in actors.get_all_level_actors():
    if cargo.get_actor_label()=='MZ_TransferCargo':actors.destroy_actor(cargo)
start = vehicle.get_actor_location()
target_start = pallet.get_actor_location()
rotation = vehicle.get_actor_rotation()
parts = {p.get_name(): p for p in vehicle.get_components_by_class(unreal.StaticMeshComponent)}
assert len(parts)==12, ('Mechanical rig missing',list(parts))
for part in parts.values():
    assert part.static_mesh.get_path_name().startswith('/Game/Warehouse/AGV/Meshes/')
    mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    assert mesh_editor.get_simple_collision_count(part.static_mesh)+mesh_editor.get_convex_collision_count(part.static_mesh)>0
wheels = [parts[name] for name in ('DriveWheel','LoadWheelL','LoadWheelR')]
minimum=[float('inf')]*3
maximum=[-float('inf')]*3
for part in parts.values():
    bounds=part.static_mesh.get_bounds()
    scale=part.get_editor_property('relative_scale3d')
    offset=part.get_editor_property('relative_location')
    for i,axis in enumerate(('x','y','z')):
        minimum[i]=min(minimum[i],getattr(offset,axis)+(getattr(bounds.origin,axis)-getattr(bounds.box_extent,axis))*getattr(scale,axis))
        maximum[i]=max(maximum[i],getattr(offset,axis)+(getattr(bounds.origin,axis)+getattr(bounds.box_extent,axis))*getattr(scale,axis))
assert all(abs(maximum[i]-minimum[i]-dimension)<.05 for i,dimension in enumerate((115+128*215/282.55,99.4,215))), (minimum,maximum)
for wheel in wheels:
    bounds = wheel.static_mesh.get_bounds()
    assert abs(wheel.get_editor_property('relative_location').z+(bounds.origin.z-bounds.box_extent.z)*wheel.get_editor_property('relative_scale3d').z)<.05, ('Wheel not on ground',wheel.get_name())
wheel_start = [w.get_editor_property('relative_rotation').pitch for w in wheels]
for name,side in [('ForkL',1),('ForkR',-1)]:
    bounds=parts[name].static_mesh.get_bounds()
    assert abs(bounds.origin.x+bounds.box_extent.x-115)<.01
    assert abs(bounds.origin.y-side*25)<.01 and abs(bounds.box_extent.y-9)<.01
    assert abs(bounds.origin.z-bounds.box_extent.z-3.5)<.01

def frame(x=-60, y=0, z=0, yaw=0):
    p = pallet.get_actor_transform().transform_location(unreal.Vector(x,y,z))
    return unreal.Transform(location=p, rotation=unreal.Rotator(pitch=0, yaw=rotation.yaw+yaw, roll=0), scale=unreal.Vector(1,1,1))

assert pallet.can_engage(frame()), ('Aligned full insertion rejected',frame(),pallet.get_actor_transform())
for args in [(-200,0,0,0),(-60,20,0,0),(-60,0,1,0),(-60,0,8,0),(-60,0,0,5),(-40,0,0,0)]:
    assert not pallet.can_engage(frame(*args)), ('Invalid insertion accepted', args)
vehicle.advance_simulation(1/60)
assert abs(vehicle.get_actor_location().y-start.y)<.01, 'Moved before E start'
vehicle.toggle_power()
vehicle.advance_simulation(1/60)
vehicle.toggle_power()
for wheel,angle in zip(wheels,wheel_start):
    radius=wheel.static_mesh.get_bounds().box_extent.z*wheel.get_editor_property('relative_scale3d').z
    assert abs(wheel.get_editor_property('relative_rotation').pitch-angle+math.degrees((vehicle.get_actor_location().y-start.y)/radius))<.01, 'Wheel rolling radius/sign incorrect'
paused = vehicle.get_actor_location()
paused_wheels = [w.get_editor_property('relative_rotation') for w in wheels]
vehicle.advance_simulation(1/60)
assert abs(vehicle.get_actor_location().y-paused.y)<.01, 'Pause failed'
assert [w.get_editor_property('relative_rotation') for w in wheels]==paused_wheels, 'Wheels moved while paused'

# A human capsule in the approach corridor must stop the vehicle before contact.
person = actors.spawn_actor_from_class(unreal.Character, unreal.Vector(1000,-690,start.z+96))
person.get_component_by_class(unreal.CapsuleComponent).set_collision_profile_name('Pawn')
vehicle.toggle_power()
for _ in range(120):
    vehicle.advance_simulation(1/60)
    if not vehicle.get_editor_property('powered'):
        break
assert 'PERSON' in vehicle.get_editor_property('status'), vehicle.get_editor_property('status')
assert vehicle.get_actor_location().y < -860, 'Human stop occurred too late'
actors.destroy_actor(person)

# Solid obstacle detection includes the exposed forks.
obstacle = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(1000,-750,start.z+40))
obstacle.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
obstacle.set_actor_scale3d(unreal.Vector(.8,.3,.8))
obstacle.static_mesh_component.set_collision_profile_name('BlockAll')
vehicle.toggle_power()
for _ in range(600):
    vehicle.advance_simulation(1/60)
    if not vehicle.get_editor_property('powered'):
        break
assert 'OBSTACLE' in vehicle.get_editor_property('status'), vehicle.get_editor_property('status')
actors.destroy_actor(obstacle)

vehicle.toggle_power()
states = set()
max_lift = 0
for _ in range(6000):
    vehicle.advance_simulation(1/60)
    carriage_z = parts['Carriage'].get_attach_parent().get_editor_property('relative_location').z
    assert abs(parts['LiftStage'].get_editor_property('relative_location').z-carriage_z*.5)<.01, 'Ram/carriage linkage desynchronized'
    max_lift = max(max_lift,carriage_z)
    states.add(str(vehicle.get_editor_property('state')))
    if not vehicle.get_editor_property('powered'):
        break
assert 'COMPLETE' in str(vehicle.get_editor_property('state')), (vehicle.get_editor_property('status'), states, vehicle.get_actor_location())
assert abs(pallet.get_actor_location().z-target_start.z)<.2, 'Pallet did not return to floor'
assert abs(pallet.get_actor_location().y-(start.y+60))<.2, 'Pallet delivery position incorrect'
assert abs(vehicle.get_actor_location().y-(start.y-130))<.2, 'Fork withdrawal incomplete'
assert abs(max_lift-10.5)<.01, ('Lift did not reach its full stroke',max_lift)
for wheel in wheels:
    radius=wheel.static_mesh.get_bounds().box_extent.z*wheel.get_editor_property('relative_scale3d').z
    wheel_rotation = wheel.get_editor_property('relative_rotation')
    pitch,yaw = math.radians(wheel_rotation.pitch),math.radians(wheel_rotation.yaw)
    print('WHEEL_FINAL',wheel.get_name(),wheel_rotation,'distance',vehicle.get_actor_location().y-start.y,'radius',radius)
    assert abs(math.cos(pitch)*math.cos(yaw)-math.cos((start.y-vehicle.get_actor_location().y)/radius))<.002, (wheel.get_name(),wheel_rotation,radius)
    assert abs(math.sin(pitch)-math.sin((start.y-vehicle.get_actor_location().y)/radius))<.002, 'Reverse wheel rotation incorrect'
print('FORKLIFT_TRAINING_VERIFIED', 'insertion rejection, power, pause, human safety, obstacle, full lift/delivery cycle', sorted(states))

# Charging tests use the saved bay, then temporary vehicles; no test state is saved.
station=vehicle.get_editor_property('charging_station')
assert station and station.get_editor_property('assigned_vehicle')==vehicle
assert abs(vehicle.get_battery_energy_wh()-4320*vehicle.get_editor_property('battery_percent')/100)<.01
assert vehicle.get_editor_property('energy_consumed_wh')>0
assert vehicle.get_editor_property('max_fork_height_cm')==160
assert vehicle.get_editor_property('empty_travel_speed_cm')==180

def advance_until(v, condition, limit=12000):
    for _ in range(limit):
        v.advance_simulation(1/60)
        if condition(): return
        assert v.get_editor_property('powered'), (v.get_editor_property('state'),v.get_editor_property('status'),v.get_actor_location())
    raise AssertionError(('Timed out',v.get_editor_property('state'),v.get_editor_property('status')))

return_position=vehicle.get_actor_location()
vehicle.set_editor_property('battery_percent',29)
vehicle.toggle_power()
advance_until(vehicle,lambda: vehicle.get_editor_property('state')==unreal.WarehouseCycle.CHARGING)
assert station.is_docked(vehicle)
before=vehicle.get_editor_property('battery_percent')
vehicle.advance_simulation(.05)
expected=100*2400*.9*.05/(3600*4320)
assert abs(vehicle.get_editor_property('battery_percent')-before-expected)<.00001
# E pauses charging as well as movement; loss of power or alignment cannot charge.
vehicle.toggle_power()
before=vehicle.get_editor_property('battery_percent')
vehicle.advance_simulation(.05)
assert vehicle.get_editor_property('battery_percent')==before
vehicle.toggle_power()
station.set_editor_property('mains_power',False)
vehicle.advance_simulation(.05)
assert not vehicle.get_editor_property('powered') and vehicle.get_editor_property('battery_percent')==before
station.set_editor_property('mains_power',True)
vehicle.toggle_power()
vehicle.set_actor_location(vehicle.get_actor_location()+unreal.Vector(3,0,0),False,False)
vehicle.advance_simulation(.05)
assert not vehicle.get_editor_property('powered') and vehicle.get_editor_property('battery_percent')==before
vehicle.set_actor_location(station.get_actor_location(),False,False)
vehicle.toggle_power()
vehicle.set_editor_property('battery_time_scale',3600)
advance_until(vehicle,lambda: vehicle.get_editor_property('state')==unreal.WarehouseCycle.RETURNING)
assert abs(vehicle.get_editor_property('battery_percent')-80)<.01
vehicle.set_editor_property('battery_time_scale',1)
advance_until(vehicle,lambda: vehicle.get_editor_property('state')==unreal.WarehouseCycle.COMPLETE)
assert (vehicle.get_actor_location()-return_position).length()<.01
assert not station.get_editor_property('occupant')
# Operator request on the station charges to 100%, including recovery at an empty dock.
vehicle.set_actor_location(station.get_actor_location(),False,False)
vehicle.set_editor_property('battery_percent',0)
station.request_manual_charge()
assert vehicle.get_editor_property('state')==unreal.WarehouseCycle.CHARGING
vehicle.set_editor_property('battery_time_scale',3600)
advance_until(vehicle,lambda: vehicle.get_editor_property('state')==unreal.WarehouseCycle.RETURNING)
assert vehicle.get_editor_property('battery_percent')==100
vehicle.set_editor_property('battery_time_scale',1)
advance_until(vehicle,lambda: vehicle.get_editor_property('state')==unreal.WarehouseCycle.COMPLETE)
actors.destroy_actor(vehicle)

def new_vehicle():
    pallet.set_actor_location(target_start,False,False)
    v=actors.spawn_actor_from_class(unreal.WarehouseForklift,start,rotation)
    v.set_editor_property('autonomous_mode',False)
    v.set_editor_property('target_pallet',pallet)
    v.set_editor_property('charging_station',station)
    station.set_editor_property('assigned_vehicle',v)
    return v

v=new_vehicle()
pallet.set_editor_property('payload_mass_kg',1400) # 1400 + pallet tare is over the rated total.
v.toggle_power(); v.advance_simulation(.05)
assert 'OVERLOAD' in v.get_editor_property('status')
assert v.get_actor_location()==start
pallet.set_editor_property('payload_mass_kg',0)
v.set_editor_property('task_fork_height_cm',161)
v.toggle_power(); v.advance_simulation(.05)
assert 'LIFT LIMIT' in v.get_editor_property('status')
actors.destroy_actor(v)

v=new_vehicle()
v.set_editor_property('task_fork_height_cm',160)
v.toggle_power()
v.advance_simulation(.05)
v.set_editor_property('battery_percent',29)
seen=set()
peak=0
for _ in range(18000):
    v.advance_simulation(1/60)
    state=v.get_editor_property('state')
    seen.add(state)
    fork=next(c for c in v.get_components_by_class(unreal.StaticMeshComponent) if c.get_name()=='ForkL')
    peak=max(peak,fork.get_attach_parent().get_editor_property('relative_location').z+9.5)
    if state==unreal.WarehouseCycle.TO_CHARGER: break
    assert v.get_editor_property('powered'), v.get_editor_property('status')
assert unreal.WarehouseCycle.WITHDRAW in seen, 'Low battery interrupted the current load'
assert abs(peak-160)<.01
assert abs(pallet.get_actor_location().z-target_start.z)<.2
assert v.get_editor_property('state')==unreal.WarehouseCycle.TO_CHARGER
actors.destroy_actor(v)
v=new_vehicle()
v.set_editor_property('battery_percent',29)
v.toggle_power()
assert v.get_editor_property('state')==unreal.WarehouseCycle.TO_CHARGER
person=actors.spawn_actor_from_class(unreal.Character,unreal.Vector(1000,-1250,start.z+96))
person.get_component_by_class(unreal.CapsuleComponent).set_collision_profile_name('Pawn')
for _ in range(120):
    v.advance_simulation(1/60)
    if not v.get_editor_property('powered'): break
assert 'PERSON' in v.get_editor_property('status'), 'Charging route ignored a person'
actors.destroy_actor(person)
v.toggle_power()
advance_until(v,lambda: v.get_editor_property('state')==unreal.WarehouseCycle.CHARGING)
v.set_editor_property('battery_time_scale',3600)
advance_until(v,lambda: v.get_editor_property('state')==unreal.WarehouseCycle.RETURNING)
v.set_editor_property('battery_time_scale',1)
advance_until(v,lambda: v.get_editor_property('state')==unreal.WarehouseCycle.APPROACH)
assert (v.get_actor_location()-start).length()<.01
assert v.get_editor_property('powered'), 'Pending work was not resumed after charging'
actors.destroy_actor(v)
print('VNSL14_CHARGING_VERIFIED','dimensions, overload, lift limit, consumption, dock alignment, pause, mains loss, 80% return, manual 100%, finish load before charging')
