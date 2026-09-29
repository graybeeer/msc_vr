"""Check actual skeletal hand contacts and release. Does not save test actors."""
import math
import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
character=actors.spawn_actor_from_class(unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'),unreal.Vector(780,-1100,96))
camera=character.get_component_by_class(unreal.CameraComponent)
parts={c.get_name():c for c in character.get_components_by_class(unreal.SkeletalMeshComponent)}
carry=parts['TwoHandCarryMesh']
movement=character.get_component_by_class(unreal.CharacterMovementComponent)
normal_speed=movement.get_editor_property('max_walk_speed')
mesh=unreal.load_asset('/Game/Warehouse/Physics/SM_Ind_War_Storage_Box_Cardboard_Worn_02')
assert mesh
def distance(a,b):
    return math.sqrt((a.x-b.x)**2+(a.y-b.y)**2+(a.z-b.z)**2)
for size in (.32,1.0):
    eye=camera.get_world_location()
    cargo=actors.spawn_actor_from_class(unreal.WarehouseCargo,eye+unreal.Vector(100,0,-30))
    cargo.set_actor_scale3d(unreal.Vector(size,size,size))
    cargo.set_cargo_mesh(mesh)
    assert character.try_pickup_cargo(cargo)
    assert not character.try_pickup_cargo(cargo)
    for _ in range(60):
        character.update_carry_pose(1/60)
    assert abs(character.get_carry_blend()-1)<.01
    assert carry.is_visible()
    assert movement.get_editor_property('max_walk_speed')<=260
    center,extent=cargo.get_actor_bounds(False)
    waist=character.get_actor_location().z
    print('CARRY_HEIGHT',size,'bottom',center.z-extent.z,'top',center.z+extent.z,'eye',eye.z,'waist',waist)
    assert waist-5 < center.z-extent.z < waist+20, 'Load must rest at waist height'
    if size==.32:
        assert center.z+extent.z < eye.z-10, 'Normal box must leave forward eye line clear'
    for i,bone in enumerate(('hand_l','hand_r')):
        actual=carry.get_socket_location(bone)
        target=character.get_carry_hand_location(i)
        error=distance(actual,target)
        print('CARRY_CONTACT',size,bone,'error_cm',error,'actual',actual,'target',target)
        assert error<3.0, (bone,'hand is not supporting the box',error)
        assert waist-10 < actual.z < waist+20, (bone,'hand must stay beside the waist',actual)
        elbow=carry.get_socket_location('lowerarm_l' if i==0 else 'lowerarm_r')
        shoulder=carry.get_socket_location('upperarm_l' if i==0 else 'upperarm_r')
        assert elbow.z<shoulder.z-10, 'Elbows should hang below shoulders'
        suffix='_l' if i==0 else '_r'
        knuckle=carry.get_socket_location('middle_01'+suffix)
        finger=carry.get_socket_location('middle_03'+suffix)
        assert finger.z>knuckle.z, (bone,'Fingers must curl up under the load, not hang down',knuckle,finger)
    # Head movement must not lift the box back toward the eyes.
    held=cargo.get_actor_location()
    camera_location=camera.get_world_location()
    camera_rotation=camera.get_world_rotation()
    for pitch in (-55,55):
        camera.set_world_location(camera_location+unreal.Vector(0,0,15),False,False)
        camera.set_world_rotation(unreal.Rotator(pitch=pitch,yaw=camera_rotation.yaw),False,False)
        character.update_carry_pose(1/60)
        assert distance(held,cargo.get_actor_location())<.01, 'Head motion moved carried load'
        assert abs(cargo.get_actor_rotation().pitch)<.01
    camera.set_world_location(camera_location,False,False)
    camera.set_world_rotation(camera_rotation,False,False)
    original=cargo.get_actor_location()
    movement.set_editor_property('velocity',unreal.Vector(200,0,0))
    for _ in range(12):
        character.update_carry_pose(1/60)
    assert distance(original,cargo.get_actor_location())>.05
    for i,bone in enumerate(('hand_l','hand_r')):
        assert distance(carry.get_socket_location(bone),character.get_carry_hand_location(i))<3
    character.drop_cargo()
    character.update_carry_pose(.3)
    body=cargo.get_component_by_class(unreal.StaticMeshComponent)
    assert body.is_simulating_physics()
    assert body.get_collision_profile_name()=='PhysicsActor'
    assert not carry.is_visible()
    assert abs(movement.get_editor_property('max_walk_speed')-normal_speed)<.01
    actors.destroy_actor(cargo)
    movement.set_editor_property('velocity',unreal.Vector())
actors.destroy_actor(character)
print('TWO_HAND_CARRY_VERIFIED','waist-height load, clear eye line, head-independent pose, both hands, walking sway and release')
