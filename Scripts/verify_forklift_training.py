"""Headless editor integration checks; never saves the transient test changes."""
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
vehicle = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'WH_AutonomousForklift')
pallet = vehicle.get_editor_property('target_pallet')
start = vehicle.get_actor_location()
target_start = pallet.get_actor_location()
rotation = vehicle.get_actor_rotation()

def frame(x=-120, y=0, z=0, yaw=0):
    p = pallet.get_actor_transform().transform_location(unreal.Vector(x,y,z))
    return unreal.Transform(location=p, rotation=unreal.Rotator(pitch=0, yaw=rotation.yaw+yaw, roll=0), scale=unreal.Vector(1,1,1))

assert pallet.can_engage(frame()), ('Aligned full insertion rejected',frame(),pallet.get_actor_transform())
for args in [(-200,0,0,0),(-120,20,0,0),(-120,0,8,0),(-120,0,0,5),(-100,0,0,0)]:
    assert not pallet.can_engage(frame(*args)), ('Invalid insertion accepted', args)
vehicle.advance_simulation(1/60)
assert abs(vehicle.get_actor_location().y-start.y)<.01, 'Moved before E start'
vehicle.toggle_power()
vehicle.advance_simulation(1/60)
vehicle.toggle_power()
paused = vehicle.get_actor_location()
vehicle.advance_simulation(1/60)
assert abs(vehicle.get_actor_location().y-paused.y)<.01, 'Pause failed'

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
for _ in range(6000):
    vehicle.advance_simulation(1/60)
    states.add(str(vehicle.get_editor_property('state')))
    if not vehicle.get_editor_property('powered'):
        break
assert 'COMPLETE' in str(vehicle.get_editor_property('state')), (vehicle.get_editor_property('status'), states, vehicle.get_actor_location())
assert abs(pallet.get_actor_location().z-target_start.z)<.2, 'Pallet did not return to floor'
assert abs(pallet.get_actor_location().y-(start.y+120))<.2, 'Pallet delivery position incorrect'
assert abs(vehicle.get_actor_location().y-(start.y-150))<.2, 'Fork withdrawal incomplete'
print('FORKLIFT_TRAINING_VERIFIED', 'insertion rejection, power, pause, human safety, obstacle, full lift/delivery cycle', sorted(states))
