"""Isolated native elevator call, deck lock and door actuator test. Never saves.

An empty vehicle uses its finite remote motor to align at the L2 waiting
pose, then calls the empty platform from L1. The test waits for real Chaos
steps; it does not translate bodies or manually advance
the state machine. It verifies the empty call and entry permission only,
not boarding or a completed loaded trip.
"""
import time
import traceback
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
settings.set_editor_property('default_game_mode', unreal.GameModeBase)
cube = unreal.load_asset('/Engine/BasicShapes/Cube')


def block(label, center, size):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center))
    actor.set_actor_label(label)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_collision_profile_name('BlockAll')
    actor.set_actor_scale3d(unreal.Vector(*(v / 100 for v in size)))
    return actor


# Same gap and pit geometry as the saved warehouse. Upper floor also has a hole.
for floor in (0, 1):
    z = floor * 400 - 10
    for label, x, y, width, depth in (
        ('South', 0, -288, 4000, 3424),
        ('West', -570.5, 1712, 2859, 576),
        ('East', 1570.5, 1712, 859, 576),
        ('North', 1000, 1888, 282, 224),
    ):
        block(f'NativeLiftL{floor + 1}{label}', (x, y, z), (width, depth, 20))
block('NativeLiftPitBase', (1000, 1600, -30), (282, 352, 20))
lift = actors.spawn_actor_from_class(unreal.WarehouseElevator, unreal.Vector(1000, 1600, 0), unreal.Rotator(yaw=90))
lift.set_actor_label('NativeLift')
vehicle = actors.spawn_actor_from_class(unreal.WarehouseForklift, unreal.Vector(1000, 1100, 400.1), unreal.Rotator(yaw=90))
vehicle.set_actor_label('NativeLiftWaitingVehicle')
vehicle.set_editor_property('auto_start', False)
vehicle.set_editor_property('pending_jobs', [])
vehicle.set_editor_property('elevator', lift)
operator = actors.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(1250, 1100, 400))
operator.set_actor_label('NativeLiftAlignmentOperator')
for component in operator.get_components_by_class(unreal.PrimitiveComponent):
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

started = time.monotonic()
phase = 0
at = 0
named = None
platform = None
gates = None
seen = set()
last_diagnostic = -10
aligned_at = None


def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def deck_top():
    # The platform has no tilt; its actual rigid-body centre plus half-thickness.
    return platform.get_world_location().z + 5


def assert_rig():
    assert platform.is_simulating_physics(), 'Platform stopped being a native body'
    assert platform.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
    assert abs(platform.get_mass() - 500) < .1, ('Platform mass changed', platform.get_mass())
    for gate in gates:
        assert gate.is_simulating_physics(), ('Door stopped being a native body', gate.get_name())
        assert gate.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
        assert abs(gate.get_mass() - 80) < .1, ('Door mass changed', gate.get_name(), gate.get_mass())


def tick(dt):
    global phase, at, named, platform, gates, last_diagnostic, aligned_at
    try:
        assert time.monotonic() - started < 180, 'Native elevator test timeout'
        if phase == 0:
            if time.monotonic() - started < .5:
                return
            level.editor_request_begin_play()
            phase = 1
            return
        game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if named is None:
            named = {actor.get_actor_label(): actor for actor in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            parts = named['NativeLift'].get_components_by_class(unreal.StaticMeshComponent)
            platform = next(part for part in parts if part.get_name() == 'LiftPlatform')
            gates = [part for part in parts if part.get_name().startswith('Gate_')]
            assert len(gates) == 6
        current_lift = named['NativeLift']
        waiting = named['NativeLiftWaitingVehicle']
        state = current_lift.get_editor_property('state')
        seen.add(str(state))
        if now - last_diagnostic >= 2:
            print('NATIVE_ELEVATOR_DIAGNOSTIC', 'time', now, 'state', state,
                'status', current_lift.get_editor_property('status'), 'deck top', deck_top(),
                'deck velocity', platform.get_physics_linear_velocity(), 'deck awake', platform.is_any_rigid_body_awake(),
                'vehicle', waiting.get_actor_location(), 'vehicle powered', waiting.get_editor_property('powered'),
                'vehicle AI', waiting.get_editor_property('ai_state'), 'vehicle fault', waiting.check_systems(),
                'doors', [(gate.get_name(), gate.get_world_location()) for gate in gates])
            last_diagnostic = now
        assert_rig()
        if phase == 1:
            if now < 2:
                return
            assert abs(deck_top()) < .5, ('Ground floor deck lock moved', deck_top())
            assert platform.get_physics_linear_velocity().length() < 1
            assert abs(waiting.get_physical_mass_kg() - 1000) < .1
            print('NATIVE_ELEVATOR_DECK_LOCK_PASSED', 'actual top', deck_top(), 'platform mass', platform.get_mass())
            assert waiting.begin_remote_control(named['NativeLiftAlignmentOperator'])
            waiting.set_remote_input(0, 0, 0)
            at = now
            phase = 20
            return
        if phase == 20:
            assert now - at < 25, ('Native waiting-pose alignment timed out', waiting.get_actor_location(), waiting.get_editor_property('status'))
            # Remote has its own one-second self check. Powering the physical
            # lift actuator can settle the chassis; align after that completes.
            if now - at < 1.5:
                waiting.set_remote_input(0, 0, 0)
                return
            target = current_lift.waiting_pose(1).translation
            delta = target - waiting.get_actor_location()
            forward = waiting.get_actor_forward_vector()
            along = delta.x * forward.x + delta.y * forward.y + delta.z * forward.z
            measured_speed = waiting.get_editor_property('current_speed_cm')
            # A slow outer position loop damps the native motor's speed loop.
            # An aggressive position-only command oscillated across this 1 cm dock.
            speed = max(-2, min(2, along * .5 - measured_speed * .8))
            # Remote forward/reverse limits are 30/130 cm/s for an empty main AGV.
            command = speed / (30 if speed >= 0 else 130)
            if delta.length() < .3:
                command = 0
            waiting.set_remote_input(command, 0, 0)
            if delta.length() < .3 and abs(waiting.get_editor_property('current_speed_cm')) < .5:
                if aligned_at is None:
                    aligned_at = now
                if now - aligned_at >= .5:
                    waiting.end_remote_control(named['NativeLiftAlignmentOperator'])
                    waiting.toggle_power()
                    print('NATIVE_ELEVATOR_WAITING_ALIGNMENT_PASSED', 'native motor, error', delta.length())
                    at = now
                    phase = 2
            else:
                aligned_at = None
            return
        if phase == 2:
            if waiting.get_editor_property('ai_state') != unreal.WarehouseAIState.READY:
                assert now - at < 15, ('Waiting vehicle self check failed', waiting.get_editor_property('status'))
                return
            assert waiting.check_systems() == ''
            assert current_lift.request_transfer(waiting, 1, 2), ('L2 call rejected', waiting.get_actor_location(), current_lift.waiting_pose(1))
            at = now
            phase = 3
            return
        if phase == 3:
            assert now - at < 80, ('Empty platform call did not finish', state, current_lift.get_editor_property('status'), deck_top())
            if state != unreal.WarehouseElevatorState.AWAIT_BOARDING:
                return
            assert current_lift.get_editor_property('current_floor') == 1
            assert abs(deck_top() - 400) < 1
            assert platform.get_physics_linear_velocity().length() < 2
            assert current_lift.can_enter(waiting), 'Arrival and open native doors did not permit entry'
            # The caller still waits outside; do not bypass precise boarding checks.
            assert not current_lift.confirm_boarded(waiting), 'Outside waiting vehicle was incorrectly accepted as boarded'
            assert current_lift.get_editor_property('completed_transfers') == 0
            print('NATIVE_ELEVATOR_EMPTY_CALL_PASSED', 'actual deck top', deck_top(), 'actual open doors',
                [(gate.get_name(), gate.get_world_location()) for gate in gates if gate.get_name().startswith('Gate_1_')])
            at = now
            phase = 4
            return
        if phase == 4:
            if now - at < 2:
                return
            assert abs(deck_top() - 400) < .5, ('Upper floor deck lock drifted', deck_top())
            assert platform.get_physics_linear_velocity().length() < 1
            assert current_lift.can_enter(waiting)
            print('NATIVE_ELEVATOR_PIE_PASSED', 'native floor locks, empty L1->L2 call, native doors, entry permission; no loaded boarding/trip tested', sorted(seen))
            finish()
    except Exception:
        print('NATIVE_ELEVATOR_PIE_FAILED', traceback.format_exc())
        finish()


handle = unreal.register_slate_post_tick_callback(tick)
