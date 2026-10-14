"""Native query support graph: ordered descendants, shared parents and explicit freshness.

Loads Engine Entry, creates an unsaved fixture and runs checks inside one Slate
callback. Movable QueryAndPhysics cubes deliberately remain non-simulating so
their native query geometry can move synchronously without advancing Chaos.
Real dynamic load/mass/damage behavior is covered by the separate native damage
and fleet workflow regressions. Additional dynamic pallets exercise the native
120cm floor handoff boundary, rotated slots, cargo overhang, tilt, missing floor
and actual blocking geometry. Their poses/masses/velocities are never corrected
at runtime; the wall's native collision is enabled for one same-frame query.
"""
import time
import traceback
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
settings.set_editor_property('default_game_mode', unreal.GameModeBase)
cube = unreal.load_asset('/Engine/BasicShapes/Cube')


def block(label, point, size):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*point))
    actor.set_actor_label(label)
    actor.set_editor_property('is_spatially_loaded', False)
    part = actor.static_mesh_component
    part.set_static_mesh(cube)
    part.set_mobility(unreal.ComponentMobility.MOVABLE)
    part.set_collision_profile_name('BlockAll')
    part.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    part.set_simulate_physics(False)
    actor.set_actor_scale3d(unreal.Vector(*(v / 100 for v in size)))
    return actor


fixture = {
    'Floor': block('SupportFloor', (0, 0, -10), (2000, 2000, 20)),
    'Left': block('SupportLeft', (-20, 0, 10), (40, 60, 20)),
    'Right': block('SupportRight', (20, 0, 10), (40, 60, 20)),
    'Middle': block('SupportMiddle', (0, 0, 30), (80, 60, 20)),
    'TopA': block('SupportTopA', (-20, 0, 50), (20, 20, 20)),
    'TopB': block('SupportTopB', (20, 0, 50), (20, 20, 20)),
}
# Interleave profile indices so ordering is genuinely breadth first, then by
# child profile index; the shared Middle and its cartons must be returned once.
order = [('Floor', 10000), ('TopB', 6), ('Right', 20), ('TopA', 4), ('Middle', 30), ('Left', 10)]
strength = actors.spawn_actor_from_class(unreal.WarehouseDamageSystem, unreal.Vector())
strength.set_actor_label('SupportSnapshotSystem')
profiles = [
    unreal.WarehouseStrength(name=name, members=[fixture[name]], physics_meshes=[cube],
        mass_kg=mass, rated_load_kg=100000, impact_yield_j=100000, impact_failure_j=1000000,
        failure=unreal.WarehouseFailure.STRUCTURE if name == 'Floor' else unreal.WarehouseFailure.RIGID)
    for name, mass in order
]

# Actual dynamic handoff fixtures, well away from the synchronous graph cubes.
pallet_mesh = unreal.load_asset('/Game/Warehouse/Physics/SM_Pallet_110')
assert pallet_mesh, 'Prepare the documented 110cm native pallet mesh first'
floor_fixtures = {}
for name, x in [('Inside', 4000), ('Overhang', 4700), ('Tilted', 6100)]:
    floor_fixtures[name+'Floor'] = block('Placement'+name+'Floor', (x, 0, -10), (400, 400, 20))
# The dynamic pallet naturally lands on a real 4-degree bearing surface.
# No constraint, runtime pose correction or disabled pallet physics holds it.
floor_fixtures['TiltedFloor'].set_actor_rotation(unreal.Rotator(pitch=4), False)
for name, x, z, yaw, pitch in [('Inside', 4000, .1, 45, 0), ('Overhang', 4700, .1, 0, 0),
                              ('Unsupported', 5400, .1, 0, 0), ('Tilted', 6100, 4.5, 0, 4)]:
    actor = actors.spawn_actor_from_class(unreal.WarehousePallet, unreal.Vector(x, 0, z), unreal.Rotator(pitch=pitch, yaw=yaw))
    actor.set_actor_label('Placement'+name)
    actor.get_component_by_class(unreal.StaticMeshComponent).set_static_mesh(pallet_mesh)
    floor_fixtures[name] = actor
    profiles.append(unreal.WarehouseStrength(name='Placement'+name, members=[actor], physics_meshes=[pallet_mesh],
        mass_kg=25, rated_load_kg=100000, impact_yield_j=100000, impact_failure_j=1000000, failure=unreal.WarehouseFailure.CRUSH))
for name, x, width, yaw in [('Inside', 4000, 40, 45), ('Overhang', 4700, 140, 0)]:
    actor = actors.spawn_actor_from_class(unreal.WarehouseCargo, unreal.Vector(x, 0, 27.7), unreal.Rotator(yaw=yaw))
    actor.set_actor_label('Placement'+name+'Cargo')
    actor.set_cargo_mesh(cube)
    actor.set_gross_mass_kg(12)
    actor.set_actor_scale3d(unreal.Vector(width/100, .3, .25))
    floor_fixtures[name+'Cargo'] = actor
    profiles.append(unreal.WarehouseStrength(name='Placement'+name+'Cargo', members=[actor], physics_meshes=[cube],
        mass_kg=12, rated_load_kg=100000, impact_yield_j=100000, impact_failure_j=1000000, failure=unreal.WarehouseFailure.CRUSH))
wall = block('PlacementOverlapWall', (4048, 0, 7.5), (8, 20, 15))
wall.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
floor_fixtures['OverlapWall'] = wall
vehicle = actors.spawn_actor_from_class(unreal.WarehouseForklift, unreal.Vector(4000, -500, 0))
vehicle.set_actor_label('PlacementObserver')
vehicle.set_editor_property('auto_start', False)
strength.set_editor_property('objects', profiles)

started = time.monotonic()
requested_play = False
checked = False


def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    global requested_play, checked
    try:
        assert time.monotonic() - started < 90, 'Support snapshot PIE timeout'
        if not requested_play:
            if time.monotonic() - started < .5:
                return
            level.editor_request_begin_play()
            requested_play = True
            return
        game = editor.get_game_world()
        if not game or checked or unreal.GameplayStatics.get_time_seconds(game) < 3:
            return
        live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
        system = live['SupportSnapshotSystem']
        parts = {name: live['Support' + name] for name, _ in order}
        start_simulation_seconds = unreal.GameplayStatics.get_time_seconds(game)
        for actor in parts.values():
            part = actor.static_mesh_component
            assert not part.is_simulating_physics(), 'Query fixture unexpectedly became a dynamic body'
            assert part.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
            assert part.get_editor_property('mobility') == unreal.ComponentMobility.MOVABLE

        def descendants(name, refresh=True):
            return [a.get_actor_label().removeprefix('Support') for a in system.get_supported_actors(parts[name], refresh)]

        def masses(expected):
            for name, mass in expected.items():
                actual = system.get_supported_mass(parts[name])
                assert abs(actual - mass) < .001, ('Support mass changed', name, actual, mass)

        def move(name, point):
            parts[name].set_actor_location(point, False, True)
            assert (parts[name].get_actor_location() - point).length() < .001, ('Fixture move failed', name)

        expected_floor = ['Right', 'Left', 'Middle', 'TopB', 'TopA']
        # Two periodic consumers must share a current graph, while the explicit
        # same-frame geometry changes below still require unconditional refresh.
        system.ensure_support_snapshot()
        version=system.get_editor_property('support_snapshot_version')
        system.ensure_support_snapshot()
        system.ensure_support_snapshot()
        assert system.get_editor_property('support_snapshot_version')==version,'Periodic readers rebuilt the same graph'
        print('SUPPORT_PERIODIC_SNAPSHOT_REUSE_PASSED',version)
        assert descendants('Floor') == expected_floor, 'Breadth-first profile order changed or shared child duplicated'
        assert descendants('Left', False) == ['Middle', 'TopB', 'TopA']
        assert descendants('Right', False) == ['Middle', 'TopB', 'TopA']
        assert descendants('Middle', False) == ['TopB', 'TopA']
        masses({'Floor': 70, 'Left': 20, 'Right': 20, 'Middle': 10, 'TopA': 0, 'TopB': 0})
        print('SUPPORT_SHARED_PARENT_ORDER_PASSED', expected_floor, '70 kg floor; 40 kg shared middle splits equally')

        original_top = parts['TopA'].get_actor_location()
        move('TopA', unreal.Vector(500, 0, 50))
        assert descendants('Left', False) == ['Middle', 'TopB', 'TopA'], 'Explicit reuse did not retain its snapshot'
        assert descendants('Left') == ['Middle', 'TopB'], 'Explicit fresh query ignored a same-frame geometry move'
        assert descendants('Floor', False) == ['Right', 'Left', 'Middle', 'TopB']
        masses({'Floor': 66, 'Left': 18, 'Right': 18, 'Middle': 6})
        move('TopA', original_top)
        assert descendants('Floor') == expected_floor, 'Same-frame restoration did not refresh support'
        masses({'Floor': 70, 'Left': 20, 'Right': 20, 'Middle': 10})
        print('SUPPORT_SAME_FRAME_FRESHNESS_PASSED', 'moved top: 66 kg; restored top: 70 kg')

        move('Middle', unreal.Vector(500, 0, 30))
        assert descendants('Middle', False) == ['TopB', 'TopA'], 'Snapshot was silently invalidated before explicit refresh'
        assert descendants('Floor') == ['Right', 'Left'], 'Former descendants survived removal of their physical support'
        assert descendants('Middle', False) == [] and descendants('Left', False) == [] and descendants('Right', False) == []
        masses({'Floor': 30, 'Left': 0, 'Right': 0, 'Middle': 0})
        assert unreal.GameplayStatics.get_time_seconds(game) == start_simulation_seconds, 'Checks crossed a simulation frame'
        assert all(not spec.failed and spec.damage == 0 for spec in system.get_editor_property('objects'))

        observer = live['PlacementObserver']
        inside = live['PlacementInside']
        cargo = live['PlacementInsideCargo']
        assert cargo in system.get_supported_actors(inside), 'Actual carton did not settle on its dynamic pallet'
        assert abs(inside.get_actor_rotation().yaw-45) < .5, 'Diagonal fixture unexpectedly rotated'

        def body(actor):
            return actor.get_component_by_class(unreal.StaticMeshComponent)

        for label in ('PlacementInside', 'PlacementOverhang', 'PlacementUnsupported', 'PlacementTilted'):
            actor = live[label]
            bounds = unreal.WarehouseDamageSystem.get_physical_bounds(actor)
            print('FLOOR_PLACEMENT_NATIVE_POSE', label, 'position', actor.get_actor_location(),
                'rotation', actor.get_actor_rotation(), 'native_min', bounds.min,
                'linear_velocity', body(actor).get_physics_linear_velocity(),
                'angular_velocity', body(actor).get_physics_angular_velocity_in_degrees(), 'mass_kg', body(actor).get_mass())
        print('FLOOR_PLACEMENT_TILTED_BEARING', live['PlacementTiltedFloor'].get_actor_location(), live['PlacementTiltedFloor'].get_actor_rotation())

        def slot(actor, offset=0, extra_yaw=0, z=0):
            yaw = actor.get_actor_rotation().yaw+extra_yaw
            forward = unreal.MathLibrary.get_forward_vector(unreal.Rotator(yaw=yaw))
            position = actor.get_actor_location()-forward*offset
            position.z = z
            return unreal.Transform(location=position, rotation=unreal.Rotator(yaw=yaw))

        assert observer.is_floor_placement_valid(inside, slot(inside)), 'Centred native load rejected'
        assert observer.is_floor_placement_valid(inside, slot(inside, 3.3)), 'Safe 3.3cm slip in a diagonal slot rejected'
        assert observer.is_floor_placement_valid(inside, slot(inside, 4.7)), 'Inside edge of the actual 120cm square rejected'
        assert not observer.is_floor_placement_valid(inside, slot(inside, 5.5)), 'Pallet outside the 120cm square accepted'
        assert not observer.is_floor_placement_valid(inside, slot(inside, 4, 2)), 'Rotated corner outside the square accepted'
        assert not observer.is_floor_placement_valid(inside, slot(inside, z=3)), 'Unsupported destination height accepted'
        overhang = live['PlacementOverhang']
        assert live['PlacementOverhangCargo'] in system.get_supported_actors(overhang), 'Overhang negative fixture lost its carton'
        assert not observer.is_floor_placement_valid(overhang, slot(overhang)), 'Supported cargo overhang outside the slot accepted'
        assert not observer.is_floor_footprint_valid(overhang, overhang.get_actor_transform()), 'Assignment accepted oversized cargo'
        unsupported = live['PlacementUnsupported']
        assert unsupported.get_actor_location().z < -50, 'Unsupported dynamic fixture did not actually fall'
        assert not observer.is_floor_placement_valid(unsupported, slot(unsupported)), 'Missing native floor accepted'
        tilted = live['PlacementTilted']
        assert abs(tilted.get_actor_rotation().pitch) > 3, 'Native angular fixture lost its actual tilt'
        assert not observer.is_floor_placement_valid(tilted, slot(tilted)), 'Tilted load accepted'
        for label, expected_mass in [('PlacementInside', 25), ('PlacementInsideCargo', 12),
                                     ('PlacementOverhang', 25), ('PlacementOverhangCargo', 12),
                                     ('PlacementUnsupported', 25), ('PlacementTilted', 25)]:
            actor = live[label]
            assert body(actor).is_simulating_physics() and body(actor).is_gravity_enabled(), ('Dynamic physics disabled', label)
            assert abs(body(actor).get_mass()-expected_mass) < .01 and not actor.get_attach_parent_actor(), ('Body mass/attachment changed', label)
        live['PlacementOverlapWall'].static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        assert not observer.is_floor_placement_valid(inside, slot(inside)), 'Native wall intrusion accepted'
        assert unreal.GameplayStatics.get_time_seconds(game) == start_simulation_seconds, 'Boundary queries crossed a physics frame'
        print('FLOOR_PLACEMENT_NATIVE_PASSED actual dynamic 110cm pallet + carton; diagonal slot; safe 3.3/4.7cm; outside/yaw/height/overhang/unsupported/tilt/native-wall rejection; no pose or mass correction')
        checked = True
        print('SUPPORT_SNAPSHOT_PIE_PASSED native query geometry; shared children once; ordered BFS; explicit same-frame refresh; conserved loads')
        finish()
    except Exception:
        unreal.log_error(traceback.format_exc())
        finish()


handle = unreal.register_slate_post_tick_callback(tick)
