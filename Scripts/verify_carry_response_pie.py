"""Native Chaos carry response, extraction, placement and cancellation. Never saves.

Uses the saved player Blueprint, intact carton and real pallet. Fixtures are
prepared before PIE; moving cargo is never teleported or velocity-corrected.
Static test obstructions are opened/closed to exercise interruption/retry.
"""
import os
import sys
import time
import traceback
import unreal

sys.path.insert(0, os.path.dirname(__file__))
from apply_real_world_scale import fit

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
settings.set_editor_property('default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
player_class = unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
carton_mesh = unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1')


def spawn(cls, name, pos):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*pos))
    actor.set_actor_label(name)
    return actor


def solid(name, pos, size, enabled=True):
    actor = spawn(unreal.StaticMeshActor, name, pos)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.set_actor_scale3d(unreal.Vector(*(s / 100 for s in size)))
    actor.static_mesh_component.set_collision_profile_name('BlockAll' if enabled else 'NoCollision')
    return actor


def carton(name, pos, mass):
    actor = spawn(unreal.WarehouseCargo, name, pos)
    actor.set_cargo_mesh(carton_mesh)
    fit(actor, (40, 30, 25), pos)
    actor.set_gross_mass_kg(mass)
    return actor


solid('CarryFloor', (0, 2000, -10), (1800, 6500, 20))
for i, mass in enumerate((12, 30, 12, 12, 12)):
    y = i * 800
    person = spawn(player_class, f'CarryPerson{i}', (-140, y, 96))
    person.set_editor_property('auto_possess_player', unreal.AutoReceiveInput.DISABLED)
    person.set_editor_property('auto_possess_ai', unreal.AutoPossessAI.DISABLED)
    person.set_editor_property('ai_controller_class', None)
    carton(f'CarryBox{i}', (-30, y - 110, 12.7), mass)
    spawn(unreal.WarehousePallet, f'CarrySupport{i}', (0, y, .1))
carton('CarryTopBox', (-30, -110, 38), 8)

# Box 2 is confined to a real, close-fitting cage. The player remains outside.
for suffix, pos, size in (
    ('X1', (-54, 1490, 25), (6, 50, 50)),
    ('X2', (-6, 1490, 25), (6, 50, 50)),
    ('Y1', (-30, 1471, 25), (54, 6, 50)),
    ('Y2', (-30, 1509, 25), (54, 6, 50)),
    ('Lid', (-30, 1490, 29), (54, 44, 6)),
):
    solid('CarryCage' + suffix, pos, size)
# A later obstruction enters the lowering path, without touching the held box.
solid('CarryPlacementBlocker', (0, 2400, 65), (110, 110, 5), False)

began = time.monotonic()
phase = 'start'
at = 0
game = pc = person = box = None
named = {}
case = 0
pickup_reached = None
peak_speed = 0
near_deck_speed = 0
release_at = None
report = []
trace_at = 0


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def begin_pickup(index, now):
    global person, box, case, at, phase, pickup_reached, peak_speed, near_deck_speed
    case, at, phase = index, now, 'pickup'
    person, box = named[f'CarryPerson{index}'], named[f'CarryBox{index}']
    pc.possess(person)
    eye = person.get_component_by_class(unreal.CameraComponent).get_world_location()
    pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye, box.get_actor_bounds(False)[0]))
    assert person.try_pickup_cargo(box), ('Pickup rejected', index)
    pickup_reached, peak_speed = None, 0
    near_deck_speed = 0


def tick(dt):
    global phase, at, game, pc, named, person, box, pickup_reached, peak_speed, release_at, trace_at, near_deck_speed
    try:
        assert time.monotonic() - began < 240, 'Wall-clock timeout'
        if phase == 'start':
            if time.monotonic() - began < 2:
                return
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if not named:
            named = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
        if phase == 'settle':
            if now < 2:
                return
            begin_pickup(0, now)
            return
        mesh = body(box)
        center, extent = box.get_actor_bounds(False)
        speed = mesh.get_physics_linear_velocity().length()
        peak_speed = max(peak_speed, speed)
        assert mesh.is_simulating_physics() and mesh.is_gravity_enabled() and not box.get_attach_parent_actor()
        assert mesh.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
        assert abs(mesh.get_mass() - (30 if case == 1 else 12)) < .05
        assert center.z - extent.z > -1, ('Floor penetration', center, extent)
        assert not person.is_physical_ragdoll(), ('Operator fell', phase, case)
        assert peak_speed < 650, ('Cargo launched', phase, peak_speed)
        if phase == 'pickup':
            if case == 2:
                if person.has_held_cargo():
                    assert now - at < 3.2, 'Confined pickup did not cancel'
                    return
                assert now - at <= 3.1 and not person.is_placing_cargo()
                assert abs(center.x + 30) < 3 and abs(center.y - 1490) < 3 and center.z < 16, ('Cage bypassed', center)
                print('CARRY_BLOCKED_PICKUP_CANCELLED', now - at)
                for name, actor in named.items():
                    if name.startswith('CarryCage'):
                        body(actor).set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
                phase, at = 'retry_settle', now
                return
            assert person.has_held_cargo(), ('Grip lost', case, now - at)
            # Waist-height carry, not eye level; inspect actual motion rather than target timers.
            if center.z > person.get_actor_location().z + 12 and speed < 25 and pickup_reached is None:
                pickup_reached = now - at
            if now - at < 1.5:
                return
            assert pickup_reached is not None and pickup_reached < 1.3, ('Pickup too slow', case, center, speed)
            for pose in person.get_components_by_class(unreal.SkeletalMeshComponent):
                if pose.get_name() in ('TwoHandCarryMesh', 'RemoteOperatorWorldMesh'):
                    for i, hand in enumerate(('hand_l', 'hand_r')):
                        assert (pose.get_socket_location(hand) - person.get_carry_hand_location(i)).length() < 6, ('Controlled carry hand contact', case, pose.get_name(), hand)
            print('CARRY_PICKUP_RESPONSE', case, pickup_reached, 's', 'peak', peak_speed)
            if case == 0:
                assert body(named['CarryTopBox']).is_simulating_physics()
                assert named['CarryTopBox'].get_actor_bounds(False)[0].z < 22, 'Upper carton did not fall after extraction'
            # Aim and place through the same native method used by E.
            pc.set_control_rotation(unreal.Rotator(yaw=0))
            support = named[f'CarrySupport{case}']
            assert person.try_place_on_pallet(support, unreal.Vector(0, case * 800, 15)), ('Placement rejected', case)
            if case == 3:
                body(named['CarryPlacementBlocker']).set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
            if case == 4:
                before = mesh.get_physics_linear_velocity()
                person.toggle_carry()
                assert not person.is_placing_cargo() and not person.has_held_cargo(), 'E did not cancel placement'
                assert (mesh.get_physics_linear_velocity() - before).length() < .001, 'Cancellation erased momentum'
                assert person.try_pickup_cargo(box), 'Could not re-grip after E cancellation'
                person.drop_cargo()
                print('CARRY_E_CANCEL_RETRY_PASSED')
                print('CARRY_RESPONSE_PIE_PASSED', report)
                finish()
                return
            phase, at, release_at = 'place', now, None
            return
        if phase == 'retry_settle':
            if now - at < 1:
                return
            assert person.try_pickup_cargo(box), 'Retry after cancelled pickup failed'
            phase, at = 'retry', now
            return
        if phase == 'retry':
            assert person.has_held_cargo(), 'Retry grip failed'
            if now - at < 1.5:
                return
            assert center.z > 100, ('Retry never lifted', center)
            person.drop_cargo()
            print('CARRY_CANCEL_RETRY_PASSED')
            begin_pickup(3, now)
            return
        if phase == 'place':
            if case in (0, 1) and 0 < center.z - extent.z - 15 < 20:
                near_deck_speed = max(near_deck_speed, -mesh.get_physics_linear_velocity().z)
            if now - trace_at > .3:
                print('CARRY_PLACEMENT_STEP', case, round(now - at, 2), center, box.get_actor_rotation(), speed)
                trace_at = now
            if person.has_held_cargo():
                assert now - at < 6.2, ('Placement has no deadline', case)
                return
            if release_at is None:
                release_at = now
                report.append((case, round(now - at, 3), round(peak_speed, 2)))
                assert not person.is_placing_cargo()
                print('CARRY_PLACEMENT_RELEASE', case, now - at, center)
            if now - release_at < 1:
                return
            if case == 3:
                assert now - at < 7.1 and center.z - extent.z >= 65, ('Lowering bypassed blocker', center)
                print('CARRY_BLOCKED_PLACEMENT_CANCELLED', release_at - at)
                begin_pickup(4, now)
                return
            assert release_at - at < 4, ('Placement too slow', case, release_at - at)
            assert near_deck_speed < 200, ('Cargo driven hard into deck', case, near_deck_speed)
            assert abs(center.x) < 4 and abs(center.y - case * 800) < 4 and abs(center.z - extent.z - 15) < 2, ('Did not stack', case, center)
            print('CARRY_STACK_RESPONSE_PASSED', case, 'near-deck descent cm/s', near_deck_speed)
            begin_pickup(case + 1, now)
    except Exception:
        print('CARRY_RESPONSE_PIE_FAILED', traceback.format_exc())
        finish()


handle = unreal.register_slate_post_tick_callback(tick)
