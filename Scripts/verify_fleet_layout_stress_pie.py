"""Bounded native layout/fleet stress matrix; never saves the warehouse.

Copies the saved AGV rig, rack parts, pallet and intact carton assets into fresh
Entry fixtures. Only editor setup changes poses. During PIE every vehicle and
load moves through the actual AI, central coordinator and Chaos contacts.
Two cases keep every saved warehouse object and rearrange native rack groups.
Transport cases must complete; impossible layouts must stop and warn instead.
Exception-only 1000 kg cargo may stop with a lift warning while normal jobs finish.
Optional -StressCases=1,4,10 selects case IDs for a focused rerun.
"""
import json
import math
import random
import re
import sys
import time
import traceback
from types import MappingProxyType
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from apply_real_world_scale import fit

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property(
    'bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
saved = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
source = saved['WH_AutonomousForklift']
rig = {c.get_name(): (c.get_relative_transform(), c.static_mesh, list(c.get_editor_property('override_materials')))
       for c in source.get_components_by_class(unreal.StaticMeshComponent)}
strength_source = saved['WH_DamageSystem']
strength_properties = ('rated_load_kg', 'impact_yield_j', 'impact_failure_j', 'overload_seconds')
vehicle_source = next(s for s in strength_source.get_editor_property('objects') if source in s.members)
vehicle_strength = {key: vehicle_source.get_editor_property(key) for key in strength_properties}
pallet_source = next(s for s in strength_source.get_editor_property('objects') if saved['WH_TrainingPallet'] in s.members)
pallet_strength = {key: pallet_source.get_editor_property(key) for key in strength_properties}
cargo_source = next(s for s in strength_source.get_editor_property('objects')
                    if any(isinstance(a, unreal.WarehouseCargo) and not a.get_actor_label().startswith('WH_HeavyTestCargo_') for a in s.members))
cargo_strength = {key: cargo_source.get_editor_property(key) for key in strength_properties}
rack_source = next(s for s in strength_source.get_editor_property('objects') if s.name == 'WH_EmptyRack_L1_1')
rack_members = list(rack_source.members)
assert len(rack_members) == 8, 'Expected the actual eight-part warehouse pallet rack'
centres = [a.get_actor_bounds(False) for a in rack_members]
rack_low = [min(getattr(c, axis)-getattr(e, axis) for c, e in centres) for axis in ('x', 'y', 'z')]
rack_high = [max(getattr(c, axis)+getattr(e, axis) for c, e in centres) for axis in ('x', 'y', 'z')]
rack_origin = unreal.Vector((rack_low[0]+rack_high[0])/2, (rack_low[1]+rack_high[1])/2, 0)
rack_parts = [(a.get_actor_location()-rack_origin, a.get_actor_rotation(), a.get_actor_scale3d(),
               a.static_mesh_component.static_mesh,
               [a.static_mesh_component.get_material(i) for i in range(a.static_mesh_component.get_num_materials())])
              for a in rack_members]
rack_strength = {key: rack_source.get_editor_property(key) for key in (
    'mass_kg', 'rated_load_kg', 'level_capacity_kg', 'impact_yield_j', 'impact_failure_j', 'overload_seconds')}
rack_physics = list(rack_source.physics_meshes)
pallet_mesh = saved['WH_TrainingPallet'].get_component_by_class(unreal.StaticMeshComponent).static_mesh
carton_mesh = unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2')
cube_mesh = unreal.load_asset('/Engine/BasicShapes/Cube')
assert carton_mesh and pallet_mesh, 'Install/prepare the actual intact carton and physical pallet assets'

CASES = [
    {'id': 1, 'name': 'saved_loaded_rack_wider_aisle', 'kind': 'saved', 'vehicles': 2, 'seed': 101, 'limit': 800, 'rack': 'WH_Rack_Left_00_A', 'deltas': [(-150, 0), (-300, 0), (0, -150)]},
    # Two physical insertion retries can consume the former 180-second budget
    # before a still-progressing delivery. Keep a finite completion deadline.
    {'id': 2, 'name': 'parallel_3_shifted_racks', 'kind': 'parallel', 'vehicles': 3, 'seed': 202, 'limit': 300},
    {'id': 3, 'name': 'parallel_4_heavy_exception', 'kind': 'parallel', 'vehicles': 4, 'seed': 303, 'limit': 210, 'heavy': True, 'expect': 'heavy_exception'},
    {'id': 4, 'name': 'head_on_retreat_2', 'kind': 'head_on', 'vehicles': 2, 'seed': 404, 'limit': 360},
    {'id': 5, 'name': 'saved_loaded_rack_shifted_aisle', 'kind': 'saved', 'vehicles': 2, 'seed': 505, 'limit': 800, 'rack': 'WH_Rack_Left_01_A', 'deltas': [(150, 0), (-150, 0), (0, 150)]},
    {'id': 6, 'name': 'crossing_3', 'kind': 'crossing', 'vehicles': 3, 'seed': 606, 'limit': 440},
    {'id': 7, 'name': 'crossing_4', 'kind': 'crossing', 'vehicles': 4, 'seed': 707, 'limit': 500},
    {'id': 8, 'name': 'parked_vehicle_reroute_3', 'kind': 'parked', 'vehicles': 3, 'seed': 808, 'limit': 300},
    {'id': 9, 'name': 'closed_section_reroute_3', 'kind': 'closed', 'vehicles': 3, 'seed': 909, 'limit': 300},
    {'id': 10, 'name': 'narrow_no_escape_2', 'kind': 'blocked', 'vehicles': 2, 'seed': 1010, 'limit': 100, 'expect': 'safe_halt'},
    {'id': 11, 'name': 'trapped_by_parked_fleet_4', 'kind': 'trapped', 'vehicles': 4, 'seed': 1111, 'limit': 100, 'expect': 'safe_halt'},
]
selection = re.search(r'-StressCases=([0-9,]+)', unreal.SystemLibrary.get_command_line(), re.I)
if selection:
    ids = {int(value) for value in selection.group(1).split(',')}
    assert ids <= {c['id'] for c in CASES}, ('Unknown stress case IDs', ids)
    CASES = [c for c in CASES if c['id'] in ids]
assert CASES
output = Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/FleetLayoutStress/Report.json')
output.parent.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
phase = 'setup'
index = 0
case = live = game = clock = manager = None
phase_wall = started
results = []
jobs = []
runtime = {}


def vector(point):
    return [point.x, point.y, point.z]


def body(actor):
    return actor.root_component if isinstance(actor.root_component, unreal.PrimitiveComponent) else actor.get_component_by_class(unreal.StaticMeshComponent)


def expected_load_masses(authored, names):
    expected = {}
    for name in names:
        actor = authored[name]
        if isinstance(actor, unreal.WarehousePallet):
            base, payload = float(actor.get_editor_property('pallet_mass_kg')), float(actor.get_editor_property('payload_mass_kg'))
            assert abs(base-25) < .01 and abs(payload) < .01, ('Authored pallet must be 25 kg without virtual cargo', name, base, payload)
            expected[name] = (25., 0.)
        else:
            expected[name] = (float(actor.get_editor_property('gross_mass_kg')), None)
    # The gameplay actor/property cannot redefine its expected starting mass.
    return MappingProxyType(expected)


def verify_load_mass(actor):
    name = actor.get_actor_label()
    base, payload = runtime['expected_load_masses'][name]
    expected = base+(payload or 0.)
    assert abs(body(actor).get_mass()-expected) < .1, ('Native load mass changed from authored value', name, body(actor).get_mass(), expected)
    if isinstance(actor, unreal.WarehousePallet):
        assert abs(actor.get_editor_property('pallet_mass_kg')-base) < .01 and abs(actor.get_editor_property('payload_mass_kg')-payload) < .01, ('Pallet mass properties changed', name)
    else:
        assert abs(actor.get_editor_property('gross_mass_kg')-base) < .1, ('Cargo mass property changed from authored value', name)


def verify_destination(job):
    pallet = live[job['pallet']]
    destination = unreal.Vector(*job['destination'])
    job['final_position_error_cm'] = (pallet.get_actor_location()-destination).length()
    job['final_yaw_error_degrees'] = abs((pallet.get_actor_rotation().yaw-job['yaw']+180)%360-180)
    if abs(destination.z-round(destination.z/400)*400) <= 5:
        pose = unreal.Transform(location=destination, rotation=unreal.Rotator(yaw=job['yaw']))
        assert live[job['vehicle']].is_floor_placement_valid(pallet, pose), ('Completed floor load is outside its reserved footprint, unsupported or unstable', job['job_id'])
        # Independent geometry check for the axis-aligned slots in this matrix.
        # Include the expected cargo even if a broken support query omitted it.
        assert abs(job['yaw']/90-round(job['yaw']/90)) < .001
        angle = math.radians(job['yaw'])
        for name in [job['pallet']]+job['cargo']:
            bounds = unreal.WarehouseDamageSystem.get_physical_bounds(live[name])
            assert bounds.is_valid, ('Missing native load bounds', name)
            for x in (bounds.min.x, bounds.max.x):
                for y in (bounds.min.y, bounds.max.y):
                    dx, dy = x-destination.x, y-destination.y
                    assert abs(dx*math.cos(angle)+dy*math.sin(angle)) <= 60.01 and abs(-dx*math.sin(angle)+dy*math.cos(angle)) <= 60.01, ('Actual load crossed its 120 cm reserved slot', name)
    else:
        assert job['final_position_error_cm'] <= 2, ('Completed rack pallet missed its destination', job['job_id'])
    assert body(pallet).get_physics_linear_velocity().length() < 5, ('Completed pallet not settled', job['job_id'])
    supported = live[runtime['strength']].get_supported_actors(pallet)
    assert all(live[name] in supported for name in job['cargo']), ('Transport lost cargo support', job['job_id'])
    for name in [job['pallet']]+job['cargo']:
        verify_load_mass(live[name])


def spawn(cls, label, point, yaw=0):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point), unreal.Rotator(yaw=yaw))
    actor.set_actor_label(label)
    actor.set_editor_property('is_spatially_loaded', False)
    return actor


def block(label, point, size):
    actor = spawn(unreal.StaticMeshActor, label, point)
    actor.static_mesh_component.set_static_mesh(cube_mesh)
    actor.static_mesh_component.set_collision_profile_name('BlockAll')
    actor.set_actor_scale3d(unreal.Vector(*size)/100.)
    return actor


def carton(label, point, mass, dimensions=(40, 30, 25)):
    actor = spawn(unreal.WarehouseCargo, label, point)
    actor.set_cargo_mesh(carton_mesh)
    actor.set_gross_mass_kg(mass)
    fit(actor, dimensions, point)
    return actor


def snapshot_report(ok=None, detail='In progress'):
    output.write_text(json.dumps({'ok': ok, 'detail': detail, 'scope': f'{len(CASES)} bounded seeded layouts; no universal safety claim',
        'model_source': '/Game/FirstPerson/Lvl_FirstPerson', 'requested_speed': 4,
        'world_step_seconds': 1/30, 'chaos_substep_seconds': 1/120,
        'wall_seconds': time.monotonic()-started, 'cases': results + ([case] if case else [])},
        ensure_ascii=False, indent=2), encoding='utf-8')


def failure_snapshot():
    """Read native poses/masses at failure; never repair the running fixture."""
    if not live:
        return []
    diagnostic = []
    for job in jobs:
        try:
            vehicle, pallet = live[job['vehicle']], live[job['pallet']]
            bounds = unreal.WarehouseDamageSystem.get_physical_bounds(pallet)
            vehicle_z = vehicle.get_actor_location().z
            # Entry fixtures have no elevator: the current native AI uses chassis Z.
            base = vehicle_z if not vehicle.get_editor_property('elevator') else None
            row = {'job_id': job['job_id'], 'vehicle': job['vehicle'], 'status': vehicle.status,
                'vehicle_position': vector(vehicle.get_actor_location()), 'vehicle_mass_kg': vehicle.get_physical_mass_kg(),
                'pallet_position': vector(pallet.get_actor_location()), 'pallet_mass_kg': body(pallet).get_mass(),
                'pallet_native_min': vector(bounds.min) if bounds.is_valid else None,
                'pallet_native_max': vector(bounds.max) if bounds.is_valid else None,
                'pallet_velocity': vector(body(pallet).get_physics_linear_velocity()),
                'pallet_yaw': pallet.get_actor_rotation().yaw, 'fork_height_cm': vehicle.get_fork_height_cm(),
                'max_fork_height_cm': vehicle.get_editor_property('max_fork_height_cm'),
                'rated_load_kg': vehicle.get_editor_property('rated_load_kg'),
                'reported_load_kg': vehicle.get_load_mass_kg(), 'destination': job['destination'],
                'destination_yaw': job['yaw'], 'active_job_id': vehicle.active_job.job_id,
                'active_destination': vector(vehicle.active_job.destination.translation),
                'ai_floor_base_without_elevator': base,
                'source_relative_height_cm': pallet.get_actor_location().z-base if base is not None else None,
                'destination_relative_height_cm': job['destination'][2]-base if base is not None else None,
                'cargo': [{'name': name, 'position': vector(live[name].get_actor_location()),
                           'mass_kg': body(live[name]).get_mass()} for name in job['cargo']]}
            diagnostic.append(row)
            print('FLEET_STRESS_FAILURE_POSE', json.dumps({k: v for k, v in row.items() if k != 'cargo'}, ensure_ascii=False))
        except Exception as error:
            diagnostic.append({'job_id': job.get('job_id'), 'diagnostic_error': str(error)})
    return diagnostic


def check_native_floor(actor):
    """Check the real body bottom against its actual level/road/bed at current XY."""
    bounds = unreal.WarehouseDamageSystem.get_physical_bounds(actor)
    name = actor.get_actor_label()
    assert bounds.is_valid, ('Load has no native collision bounds', name)
    if 'floor_ignored' not in runtime:
        ignored = {a for a in live.values() if isinstance(a, (unreal.WarehouseCargo, unreal.WarehousePallet,
            unreal.WarehouseForklift, unreal.WarehouseWorker, unreal.Character))}
        for spec in live[runtime['strength']].get_editor_property('objects'):
            if spec.failure == unreal.WarehouseFailure.RACK:
                ignored.update(spec.members)
        runtime['floor_ignored'] = list(ignored)
        runtime['floor_references'] = {}
        case['floor_references'] = runtime['floor_references']
    reference = runtime['floor_references'].get(name)
    x, y = (bounds.min.x+bounds.max.x)/2, (bounds.min.y+bounds.max.y)/2
    # First locate the real surface below the body. Subsequently trace around
    # that same level, so a fall through an upper deck cannot select a lower one.
    top = reference['height_cm']+10 if reference else bounds.max.z+10
    bottom = reference['height_cm']-30 if reference else bounds.min.z-450
    result = unreal.SystemLibrary.line_trace_single(game, unreal.Vector(x, y, top), unreal.Vector(x, y, bottom),
        unreal.TraceTypeQuery.ECC_VISIBILITY, False, runtime['floor_ignored'], unreal.DrawDebugTrace.NONE, True)
    native = result if isinstance(result, unreal.HitResult) else next(
        (value for value in (result or ()) if isinstance(value, unreal.HitResult)), None)
    assert native is not None, ('Native floor missing at load XY', name, [x, y], reference)
    hit = native.to_tuple()
    assert hit[0] and hit[7].z > .98 and hit[9] is not None, ('Native floor unsupported/nonhorizontal', name, reference)
    floor_bounds = unreal.WarehouseDamageSystem.get_physical_bounds(hit[9])
    assert floor_bounds.is_valid, ('Floor trace hit a query-only decoration', name, hit[9].get_actor_label())
    height = hit[5].z
    if reference is None:
        reference = {'height_cm': height, 'surface': hit[9].get_actor_label(), 'initial_xy': [x, y],
            'initial_native_min': vector(bounds.min), 'initial_native_max': vector(bounds.max)}
        runtime['floor_references'][name] = reference
    assert abs(height-reference['height_cm']) < 2, ('Native floor level changed unexpectedly', name, height, reference)
    clearance = bounds.min.z-height
    reference.update(last_xy=[x, y], last_surface=hit[9].get_actor_label(), last_height_cm=height,
        last_native_min=vector(bounds.min), last_native_max=vector(bounds.max), last_clearance_cm=clearance)
    assert clearance > -2, ('Load penetrated its native floor', name, clearance, reference)
    return clearance


def setup(definition):
    global jobs, runtime
    assert not editor.get_game_world(), 'Fixture setup attempted while physics was running'
    if definition['kind'] == 'saved':
        assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
        saved_actors = actors.get_all_level_actors()
        vehicles = [a for a in saved_actors if isinstance(a, unreal.WarehouseForklift)]
        assert len(vehicles) == 2, 'Saved warehouse must retain its default two vehicles'
        for vehicle in vehicles:
            vehicle.set_editor_property('auto_start', False)
        jobs = []
        runtime = {'vehicles': [a.get_actor_label() for a in vehicles], 'strength': 'WH_DamageSystem',
            'goods': [a.get_actor_label() for a in saved_actors if isinstance(a, (unreal.WarehousePallet, unreal.WarehouseCargo))],
            'racks': [], 'powered': False, 'last_sample': -5., 'initial': {}, 'last_time': None,
            'fault_at': None, 'accepted_at': None, 'saved_stage': 'relocate', 'outbound_job': None, 'rack_return_job': None}
        runtime['expected_load_masses'] = expected_load_masses({a.get_actor_label(): a for a in saved_actors}, runtime['goods'])
        return {'id': definition['id'], 'name': definition['name'], 'seed': definition['seed'], 'kind': 'saved',
            'expectation': 'transport', 'vehicle_count': 2, 'ok': None, 'jobs': jobs, 'samples': [], 'layout_transactions': []}
    assert level.load_level('/Engine/Maps/Entry')
    for actor in list(actors.get_all_level_actors()):
        if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
            actors.destroy_actor(actor)
    world = editor.get_editor_world()
    unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property(
        'default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
    block('StressFloor', (0, 0, -10), (9000, 9000, 20))
    spawn(unreal.PlayerStart, 'StressPlayerStart', (3000, -3500, 100))
    spawn(unreal.WarehouseComputer, 'StressConsole', (2900, -3500, 0))
    rng = random.Random(definition['seed'])
    specs = []
    goods = []
    racks = []
    borders = {'parallel': [(0, -700), (0, 300)], 'head_on': [(0, -650), (0, 650)],
        'crossing': [(-900, 900), (900, -900)], 'parked': [(0, -1000), (0, 1000)],
        'closed': [(300, -700), (300, 700)], 'blocked': [(0, -400), (0, 400)],
        'trapped': [(600, -350), (600, 350)]}
    rack_origins = []
    for rack_index, (x, y) in enumerate(borders[definition['kind']]):
        origin = unreal.Vector(x+rng.randint(-60, 60), y+rng.randint(-20, 20), 0)
        rack_origins.append(vector(origin))
        members = []
        for part_index, (relative, rotation, scale, mesh, materials) in enumerate(rack_parts):
            member = spawn(unreal.StaticMeshActor, f'StressRack{rack_index}_{part_index}', vector(origin+relative))
            member.set_actor_rotation(rotation, False)
            member.set_actor_scale3d(scale)
            member.static_mesh_component.set_static_mesh(mesh)
            member.static_mesh_component.set_collision_profile_name('BlockAll')
            for slot, material in enumerate(materials):
                member.static_mesh_component.set_material(slot, material)
            members.append(member)
            racks.append(member.get_actor_label())
        specs.append(unreal.WarehouseStrength(name=f'StressRack{rack_index}', members=members,
            physics_meshes=rack_physics, failure=unreal.WarehouseFailure.RACK,
            shelf_heights_cm=[20., 140.], **rack_strength))
        stock = spawn(unreal.WarehousePallet, f'StressRackStock{rack_index}', (origin.x, origin.y-62.5, 20.1))
        parcel = carton(f'StressRackStockCargo{rack_index}', (origin.x, origin.y-62.5, 47.7), 20.)
        goods.extend((stock.get_actor_label(), parcel.get_actor_label()))
        specs.extend((unreal.WarehouseStrength(name=stock.get_actor_label(), members=[stock], mass_kg=25., failure=unreal.WarehouseFailure.CRUSH, **pallet_strength),
                      unreal.WarehouseStrength(name=parcel.get_actor_label(), members=[parcel], mass_kg=20., failure=unreal.WarehouseFailure.CRUSH, **cargo_strength)))
    jobs = []
    vehicles = []
    kind = definition['kind']
    ox, oy = definition.get('offset', (0, 0))
    positions = []
    work = []
    anchors = []
    if kind == 'parallel':
        lanes = [-1200, -200, 800, 1800][:definition['vehicles']]
        for vehicle_index, y in enumerate(lanes):
            y += rng.randint(-70, 70)
            # Isolate the exception-only heavy lift at its normal 220 cm
            # pickup waiting point. Other vehicles still navigate from 500 cm.
            start_x = -920 if definition.get('heavy') and vehicle_index == 0 else -1200
            positions.append((start_x, y, 0))
            work.append((vehicle_index, (-700, y, .1), (700, y, 0), 0, 0, 0))
    elif kind == 'head_on':
        positions = [(-120+ox, oy, 0), (400+ox, oy, 180)]
        work = [(0, (150+ox, oy, .1), (1100+ox, oy, 0), 0, 0, 0),
                (1, (-1400+ox, oy, .1), (-2300+ox, oy, 0), 180, 180, 'after_yield')]
        anchors = [(x+ox, y+oy, yaw) for x, y, yaw in ((-200, -400, 0), (650, -400, 0), (1000, -400, 0),
            (-200, 400, 0), (650, 400, 0), (1000, 400, 0), (-200, 0, 0), (650, 0, 0))]
    elif kind == 'crossing':
        positions = [(-1800, -350, 0), (1800, 350, 180), (-350, 1800, -90), (350, -1800, 90)][:definition['vehicles']]
        work = [(0, (-1300, -350, .1), (1100, -350, 0), 0, 0, 0),
                (1, (1300, 350, .1), (-1100, 350, 0), 180, 180, .3),
                (2, (-350, 1300, .1), (-350, -1100, 0), -90, -90, .6),
                (3, (350, -1300, .1), (350, 1100, 0), 90, 90, .9)][:definition['vehicles']]
        anchors = [(x, y, yaw) for x in (-1100, 0, 1100) for y in (-1100, 0, 1100) for yaw in (0, 90, 180, -90)]
    elif kind in ('parked', 'closed'):
        positions = [(-1200, 0, 0), (0 if kind == 'parked' else 1700, 0 if kind == 'parked' else 1400, 90), (-1200, -1600, 0)]
        work = [(0, (-700, 0, .1), (1100, 0, 0), 0, 0, 0),
                (2, (-700, -1600, .1), (700, -1600, 0), 0, 0, .5)]
        anchors = [(x, y, yaw) for x in (-700, -200, 650, 1000) for y in (-450, 450) for yaw in (0, 180)]
    elif kind == 'blocked':
        positions = [(-500, 0, 0), (500, 0, 180)]
        work = [(0, (700, 0, .1), (1600, 0, 0), 0, 0, 0)]
        block('StressNorthWall', (0, 120, 125), (1800, 20, 250))
        block('StressSouthWall', (0, -120, 125), (1800, 20, 250))
        block('StressBackWall', (800, 0, 125), (30, 260, 250))
    elif kind == 'trapped':
        positions = [(0, 0, 0), (350, 0, 180), (-700, 0, 0), (1100, 0, 180)]
        work = [(0, (700, 0, .1), (1700, 0, 0), 0, 0, 0)]
        block('StressNorthWall', (500, 120, 125), (1900, 20, 250))
        block('StressSouthWall', (500, -120, 125), (1900, 20, 250))
        block('StressBehindActive', (-180, 0, 125), (30, 260, 250))
    assert len(positions) == definition['vehicles']
    for vehicle_index, (x, y, yaw) in enumerate(positions):
        name = f'StressAGV{vehicle_index}'
        vehicle = spawn(unreal.WarehouseForklift, name, (x, y, 0), yaw)
        vehicle.set_editor_property('vehicle_name', name)
        vehicle.set_editor_property('auto_start', False)
        for component in vehicle.get_components_by_class(unreal.StaticMeshComponent):
            transform, mesh, materials = rig[component.get_name()]
            component.set_relative_transform(transform, False, True)
            component.set_static_mesh(mesh)
            component.set_editor_property('override_materials', materials)
        vehicle.set_editor_property('navigation_anchors', [unreal.Transform(location=unreal.Vector(x, y, 0), rotation=unreal.Rotator(yaw=yaw)) for x, y, yaw in anchors])
        specs.append(unreal.WarehouseStrength(name=name, members=[vehicle], mass_kg=1000., failure=unreal.WarehouseFailure.MACHINE, **vehicle_strength))
        vehicles.append(name)
    for vehicle_index, pickup, destination, source_yaw, goal_yaw, delay in work:
        pallet_name = f'StressPallet{vehicle_index}'
        pallet = spawn(unreal.WarehousePallet, pallet_name, pickup, source_yaw)
        body(pallet).set_static_mesh(pallet_mesh)
        boxes = []
        heavy = definition.get('heavy') and vehicle_index == 0
        offsets = [(-25, -25), (-25, 25), (25, -25), (25, 25)] if heavy else [(0, 0)]
        for cargo_index, (dx, dy) in enumerate(offsets):
            rotation = unreal.Rotator(yaw=source_yaw)
            # This local planar rotation is fixture authoring, before any physics.
            angle = source_yaw * 3.141592653589793 / 180.
            cx = pickup[0] + math.cos(angle)*dx - math.sin(angle)*dy
            cy = pickup[1] + math.sin(angle)*dx + math.cos(angle)*dy
            cargo = carton(f'StressCargo{vehicle_index}_{cargo_index}', (cx, cy, 30.2 if heavy else 27.7),
                250. if heavy else rng.choice((12., 40., 80.)), (45, 45, 30) if heavy else (40, 30, 25))
            cargo.set_actor_rotation(rotation, False)
            boxes.append(cargo.get_actor_label())
            specs.append(unreal.WarehouseStrength(name=cargo.get_actor_label(), members=[cargo], mass_kg=cargo.get_editor_property('gross_mass_kg'),
                failure=unreal.WarehouseFailure.CRUSH, **cargo_strength))
        specs.append(unreal.WarehouseStrength(name=pallet_name, members=[pallet], mass_kg=25., failure=unreal.WarehouseFailure.CRUSH, **pallet_strength))
        goods.extend([pallet_name]+boxes)
        jobs.append({'vehicle': vehicles[vehicle_index], 'pallet': pallet_name, 'cargo': boxes, 'destination': list(destination),
            'yaw': goal_yaw, 'delay': delay, 'job_id': f"STRESS-{definition['id']}-{vehicle_index}", 'submitted': None, 'completed': None})
    damage = spawn(unreal.WarehouseDamageSystem, 'StressStrength', (3000, 3000, 0))
    damage.set_editor_property('objects', specs)
    runtime = {'vehicles': vehicles, 'goods': goods, 'racks': racks, 'strength': 'StressStrength', 'powered': False, 'last_sample': -5., 'initial': {},
               'last_time': None, 'fault_at': None, 'accepted_at': None}
    runtime['expected_load_masses'] = expected_load_masses({a.get_actor_label(): a for a in actors.get_all_level_actors()}, goods)
    return {'id': definition['id'], 'name': definition['name'], 'seed': definition['seed'], 'kind': kind,
        'expectation': definition.get('expect', 'transport'), 'vehicle_count': len(vehicles), 'ok': None,
        'jobs': jobs, 'samples': [], 'rack_origins': rack_origins}


def relocate_saved(definition):
    layouts = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseLayoutEditor)
    assert len(layouts) == 1, 'Actual warehouse layout transaction system missing'
    layout = layouts[0]
    strength = live[runtime['strength']]
    layout.set_editing(True, 0)
    assert unreal.GameplayStatics.is_game_paused(game), 'Layout changes did not pause native physics'
    layout.set_category(unreal.WarehouseLayoutCategory.RACK)
    rack = live[definition['rack']]
    group = next(str(tag) for tag in rack.tags if str(tag).startswith('LayoutGroup:'))
    members = [a for a in live.values() if group in {str(tag) for tag in a.tags}]
    for member in list(members):
        for supported in strength.get_supported_actors(member, member == members[0]):
            if isinstance(supported, (unreal.WarehousePallet, unreal.WarehouseCargo)) and supported not in members:
                members.append(supported)
    contents = [a for a in members if isinstance(a, (unreal.WarehousePallet, unreal.WarehouseCargo))]
    assert any(isinstance(a, unreal.WarehousePallet) for a in contents) and any(isinstance(a, unreal.WarehouseCargo) for a in contents), 'Selected rack had no actual stock'
    before = {a: a.get_actor_location() for a in members}
    point = rack.get_actor_location()
    accepted = False
    for x, y in definition['deltas']:
        assert layout.begin_drag(rack, point), layout.status
        layout.update_drag(point+unreal.Vector(x, y, 0))
        assert all(not a.get_actor_enable_collision() for a in members), 'A moving rack/content collider remained active'
        accepted = layout.end_drag(True)
        if accepted:
            break
        assert all((a.get_actor_location()-before[a]).length() < .2 for a in members), 'Rejected layout failed to roll back stock'
    assert accepted, ('No safe proposed rack layout accepted', definition['rack'], layout.status)
    delta = rack.get_actor_location()-point
    assert delta.length() >= 100, ('Rack arrangement did not meaningfully change', delta)
    for actor in members:
        assert (actor.get_actor_location()-before[actor]-delta).length() < .2, ('Rack stock did not move as a group', actor.get_actor_label())
        assert actor.get_actor_enable_collision(), ('Rack/content collision was not restored', actor.get_actor_label())
        if actor in contents:
            assert body(actor).is_simulating_physics() and not actor.get_attach_parent_actor(), ('Rack content physics/attachments changed', actor.get_actor_label())
    case['layout_transactions'].append({'rack': definition['rack'], 'delta': vector(delta), 'members': len(members), 'contents': len(contents)})
    layout.clear_selection()
    layout.set_category(unreal.WarehouseLayoutCategory.OTHER)
    pallet = live['WH_InboundPallet_Empty']
    point = pallet.get_actor_location()
    accepted = False
    proposals = [(0, 50), (-50, 0), (0, -50)] if definition['id'] == 1 else [(0, -50), (50, 0), (0, 50)]
    for x, y in proposals:
        assert layout.begin_drag(pallet, point), layout.status
        layout.update_drag(point+unreal.Vector(x, y, 0))
        accepted = layout.end_drag(True)
        if accepted:
            break
    assert accepted and body(pallet).is_simulating_physics(), ('Empty pallet arrangement was not accepted/restored', layout.status)
    case['layout_transactions'].append({'object': pallet.get_actor_label(), 'delta': vector(pallet.get_actor_location()-point)})
    layout.set_editing(False, 0)
    assert not unreal.GameplayStatics.is_game_paused(game), 'Layout edit did not restore simulation'


def saved_tick(now, definition):
    comp = live['WH_ManagementComputer']
    if runtime['saved_stage'] == 'relocate':
        if now < 2:
            return
        relocate_saved(definition)
        runtime['startup_goods'] = len(runtime['goods'])
        heavy = [a for a in live.values() if isinstance(a, unreal.WarehouseCargo) and a.get_actor_label().startswith('WH_HeavyTestCargo_')]
        assert len(heavy) == 4 and all(abs(body(a).get_mass()-1000) < .1 for a in heavy), 'Actual four 1000 kg cargo bodies missing'
        runtime['heavy'] = {a.get_actor_label(): vector(a.get_actor_location()) for a in heavy}
        runtime['accepted_at'] = now
        for name in runtime['vehicles']:
            live[name].toggle_power()
        runtime['saved_stage'] = 'outbound'
        return
    if runtime['saved_stage'] == 'outbound' and now-runtime['accepted_at'] >= 8:
        assert comp.issue_outbound(live['WH_Drop_Box'], None, 0), comp.get_editor_property('last_message')
        runtime['outbound_job'] = [task.job_id for task in comp.get_editor_property('tasks') if task.job_id.startswith('CONSOLE-')][-1]
        runtime['saved_stage'] = 'three_jobs'
    for name in runtime['vehicles']:
        vehicle = live[name]
        for report in vehicle.job_reports:
            job = next((value for value in jobs if value['job_id'] == report.job_id), None)
            if report.result == 'ACCEPTED' and job is None:
                active = vehicle.active_job
                assert active.job_id == report.job_id, 'Missed native active work-order metadata'
                task = next(task for task in comp.get_editor_property('tasks') if task.job_id == report.job_id)
                assert task.submission_time_known
                pallet = active.pallet
                contents = [a.get_actor_label() for a in live[runtime['strength']].get_supported_actors(pallet) if isinstance(a, unreal.WarehouseCargo)]
                job = {'job_id': report.job_id, 'vehicle': name, 'pallet': pallet.get_actor_label(), 'cargo': contents,
                    'destination': vector(active.destination.translation), 'yaw': active.destination.rotation.rotator().yaw,
                    'submitted': task.submitted_simulation_seconds, 'accepted': report.simulation_seconds, 'completed': None}
                jobs.append(job)
            if report.result == 'COMPLETED' and job and job['completed'] is None:
                task = next(task for task in comp.get_editor_property('tasks') if task.job_id == report.job_id)
                assert task.completed and abs(task.finished_simulation_seconds-report.simulation_seconds) < .001
                job.update(completed=report.simulation_seconds, duration=report.simulation_seconds-job['submitted'])
    for name in runtime['vehicles']:
        assert live[name].ai_state != unreal.WarehouseAIState.FAULT, (name, live[name].status)
    if runtime['saved_stage'] == 'three_jobs':
        if live['WH_InboundDispatch'].get_editor_property('completed_count') < 2 or not any(job['job_id'] == runtime['outbound_job'] and job['completed'] is not None for job in jobs):
            return
        comp.refresh_state()
        item = next(item for item in comp.inventory if item.cargo == live['MZ_TransferCargo'])
        assert '랙' in item.location, 'Inbound cargo did not reach a real rack'
        assert comp.issue_outbound(item.cargo, None, 1), comp.get_editor_property('last_message')
        runtime['rack_return_job'] = [task.job_id for task in comp.get_editor_property('tasks') if task.job_id.startswith('CONSOLE-')][-1]
        runtime['saved_stage'] = 'four_jobs'
    if runtime['saved_stage'] != 'four_jobs' or len(jobs) != 4 or not all(job['completed'] is not None for job in jobs):
        return
    if now-max(job['completed'] for job in jobs) < 2:
        return
    for job in jobs:
        # A returned pallet has a later legitimate destination than its earlier inbound task.
        if any(other['pallet'] == job['pallet'] and other['completed'] > job['completed'] for other in jobs):
            continue
        verify_destination(job)
    for label, initial in runtime['heavy'].items():
        assert (live[label].get_actor_location()-unreal.Vector(*initial)).length() < 8 and abs(body(live[label]).get_mass()-1000) < .1, ('Unrelated heavy stack moved or changed mass', label)
    assert len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseCargo))+len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehousePallet)) == runtime['startup_goods'], 'Warehouse layout lost a saved cargo/pallet'
    complete_case(True, 'Actual loaded rack/contents and empty pallet rearranged through native transactions; two inbound, dock outbound and rack outbound completed; full saved cargo and four 1000 kg bodies retained')


def complete_case(ok, detail):
    global phase, phase_wall, case
    if ok:
        for name in runtime['goods']:
            verify_load_mass(live[name])
    case.update(ok=ok, detail=detail, wall_seconds=time.monotonic()-phase_wall)
    if game:
        case['simulation_seconds'] = unreal.GameplayStatics.get_time_seconds(game)
        case['manager'] = {key: manager.get_editor_property(key) for key in ('wait_count', 'yield_count', 'detour_count', 'closed_section_count')} if manager else None
    results.append(case)
    print('FLEET_LAYOUT_STRESS_CASE_PASSED' if ok else 'FLEET_LAYOUT_STRESS_CASE_FAILED', case['id'], case['name'], detail)
    case = None
    snapshot_report()
    if editor.get_game_world():
        level.editor_request_end_play()
    phase = 'await_stop'
    phase_wall = time.monotonic()


def finish():
    ok = len(results) == len(CASES) and all(result['ok'] for result in results)
    detail = f"{sum(bool(result['ok']) for result in results)}/{len(CASES)} cases passed; transport and expected safe halts are separate results"
    snapshot_report(ok, detail)
    print('FLEET_LAYOUT_STRESS_PIE_PASSED' if ok else 'FLEET_LAYOUT_STRESS_PIE_FAILED', detail)
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    global phase, phase_wall, index, case, live, game, clock, manager
    try:
        wall = time.monotonic()
        if phase == 'setup':
            live = game = clock = manager = None
            case = setup(CASES[index])
            snapshot_report()
            phase = 'request_play'
            phase_wall = wall
            return
        if phase == 'request_play':
            if wall-phase_wall < .5:
                return
            level.editor_request_begin_play()
            phase = 'run'
            phase_wall = wall
            return
        game = editor.get_game_world()
        if phase == 'await_stop':
            if game:
                assert wall-phase_wall < 30, 'PIE did not stop between independent fixtures'
                return
            index += 1
            if index == len(CASES):
                finish()
            else:
                phase = 'setup'
            return
        if not game:
            assert wall-phase_wall < 30, 'PIE world failed to start'
            return
        assert wall-phase_wall < (1000 if CASES[index]['kind'] == 'saved' else 300), 'Case wall-clock limit exceeded'
        now = unreal.GameplayStatics.get_time_seconds(game)
        definition = CASES[index]
        assert now < definition['limit'], ('Case simulation timeout', [live[name].status for name in runtime['vehicles']] if live else None)
        if live is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            clocks = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseSimulationClock)
            assert len(clocks) == 1 and clocks[0].set_speed(4), 'Runtime fixed-step clock unavailable'
            clock = clocks[0]
            world_settings = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WorldSettings)
            assert len(world_settings) == 1, 'Expected one native world settings actor'
            runtime['world_settings'] = world_settings[0]
            managers = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseFleetCoordinator)
            assert len(managers) == 1, 'Expected one central fleet coordinator'
            manager = managers[0]
            print('FLEET_LAYOUT_STRESS_CASE_STARTED', case['id'], case['name'], 'vehicles', len(runtime['vehicles']))
        previous_time = runtime['last_time']
        frame_count = unreal.SystemLibrary.get_frame_count()
        previous_frame = runtime.get('last_frame_count')
        timing = {
            'simulation_seconds': now, 'previous_simulation_seconds': previous_time,
            'callback_simulation_delta': now-previous_time if previous_time is not None else None,
            'slate_delta_seconds': dt, 'world_delta_seconds': unreal.GameplayStatics.get_world_delta_seconds(game),
            'frame_count': frame_count, 'previous_frame_count': previous_frame,
            'frame_gap': frame_count-previous_frame if previous_frame is not None else None,
            'paused': unreal.GameplayStatics.is_game_paused(game),
            'time_dilation': unreal.GameplayStatics.get_global_time_dilation(game),
            'minimum_world_step': runtime['world_settings'].get_editor_property('min_undilated_frame_time'),
            'maximum_world_step': runtime['world_settings'].get_editor_property('max_undilated_frame_time'),
        }
        case['clock_observation'] = timing
        if now > 2:
            duplicate_observation = previous_time is not None and now == previous_time
            callback_valid = previous_time is None or abs(now-previous_time-1/30) < .0001
            native_step_valid = abs(timing['world_delta_seconds']-1/30) < .000001
            clamps_valid = all(abs(timing[name]-1/30) < .000001 for name in ('minimum_world_step', 'maximum_world_step'))
            if not ((callback_valid or duplicate_observation) and native_step_valid and clamps_valid):
                print('FLEET_STRESS_CLOCK_DIAGNOSTIC', json.dumps(timing, ensure_ascii=False))
            assert native_step_valid, ('Native world step changed under multi-vehicle load', timing)
            assert clamps_valid, ('Native fixed-step world clamps changed', timing)
            if duplicate_observation:
                case['duplicate_world_observation_count'] = case.get('duplicate_world_observation_count', 0)+1
                case.setdefault('first_duplicate_world_observation', dict(timing))
                case['last_duplicate_world_observation'] = dict(timing)
                runtime['last_frame_count'] = frame_count
                print('FLEET_STRESS_DUPLICATE_WORLD_OBSERVATION', case['duplicate_world_observation_count'], json.dumps(timing, ensure_ascii=False))
                return  # No world progress: do not execute test actions twice.
            # Every positive observed world step retains the original expectation.
            assert callback_valid, ('Simulation step changed under multi-vehicle load', timing)
        runtime['last_time'] = now
        runtime['last_frame_count'] = frame_count
        if now-runtime['last_sample'] >= 2:
            runtime['last_sample'] = now
            sample = {'t': now, 'fleet': [], 'achieved_speed': clock.get_editor_property('achieved_speed')}
            for name in runtime['vehicles']:
                vehicle = live[name]
                assert body(vehicle).is_simulating_physics() and vehicle.get_actor_enable_collision(), ('Vehicle physics/collision disabled', name)
                assert abs(vehicle.get_physical_mass_kg()-1000) < .5, ('Vehicle mass changed', name)
                assert vehicle.get_actor_up_vector().z > .8, ('Vehicle tipped', name)
                assert not vehicle.get_editor_property('mechanical_failure'), ('Vehicle damaged', name, vehicle.status)
                sample['fleet'].append({'name': name, 'position': vector(vehicle.get_actor_location()), 'yaw': vehicle.get_actor_rotation().yaw,
                    'state': str(vehicle.ai_state), 'status': vehicle.status, 'speed': vehicle.current_speed_cm,
                    'emergency': vehicle.get_editor_property('emergency_blocked'), 'yielding': vehicle.get_editor_property('traffic_yielding')})
            for name in runtime['goods']:
                actor = live[name]
                assert body(actor).is_simulating_physics() and actor.get_actor_enable_collision(), ('Load physics/collision disabled', name)
                assert not actor.get_attach_parent_actor(), ('Load attached to a vehicle/rack', name)
                clearance = check_native_floor(actor)
                sample['loads_checked'] = sample.get('loads_checked', 0)+1
                sample['minimum_native_floor_clearance_cm'] = min(sample.get('minimum_native_floor_clearance_cm', clearance), clearance)
                assert not live[runtime['strength']].has_failed(actor), ('Load damaged', name)
                verify_load_mass(actor)
            case['samples'].append(sample)
            if len(case['samples']) % 5 == 0:
                snapshot_report()
                print('FLEET_LAYOUT_STRESS_PROGRESS', case['id'], round(now, 2), [v['status'] for v in sample['fleet']])
        if definition['kind'] == 'saved':
            saved_tick(now, definition)
            return
        if not runtime['powered']:
            if now < 2:
                return
            for name in runtime['vehicles']:
                runtime['initial'][name] = vector(live[name].get_actor_location())
                # Parked blockers remain actual 1000 kg dynamic colliders.
                if definition['kind'] in ('parked', 'closed', 'trapped') and name not in {job['vehicle'] for job in jobs}:
                    continue
                live[name].toggle_power()
            if definition['kind'] == 'closed':
                manager.close_section('StressClosedLane', unreal.Vector(250, -120, 1), unreal.Vector(520, 120, 240), 'Stress blocked aisle')
            runtime['powered'] = True
            runtime['accepted_at'] = now
        for job in jobs:
            if job['submitted'] is None:
                if job['delay'] == 'after_yield':
                    if manager.get_editor_property('yield_count') < 1:
                        continue
                elif now-runtime['accepted_at'] < job['delay']:
                    continue
                pallet = live[job['pallet']]
                strength = live[runtime['strength']]
                supported = strength.get_supported_actors(pallet)
                assert all(live[name] in supported for name in job['cargo']), ('Fixture cargo did not settle on its pallet', job['job_id'])
                order = unreal.WarehouseWorkOrder(job_id=job['job_id'], source_system='FMS-LAYOUT-STRESS', pallet=pallet,
                    destination=unreal.Transform(location=unreal.Vector(*job['destination']), rotation=unreal.Rotator(yaw=job['yaw'])))
                assert live[job['vehicle']].submit_job(order), ('Native order rejected', job['job_id'])
                job['submitted'] = now
                continue
            vehicle = live[job['vehicle']]
            if job['completed'] is None and job['job_id'] in vehicle.completed_job_ids:
                report = next(r for r in vehicle.job_reports if r.job_id == job['job_id'] and r.result == 'COMPLETED')
                job['completed'] = report.simulation_seconds
                job['duration'] = report.simulation_seconds-job['submitted']
        if definition.get('expect') == 'safe_halt':
            active = live[jobs[0]['vehicle']]
            if not active.get_editor_property('emergency_blocked'):
                return
            assert active.ai_state == unreal.WarehouseAIState.FAULT and active.get_editor_property('emergency_reason'), 'Expected explicit operator recovery request'
            if runtime['fault_at'] is None:
                runtime['fault_at'] = now
                runtime['halted'] = {name: vector(live[name].get_actor_location()) for name in runtime['vehicles']}
                return
            if now-runtime['fault_at'] < 1:
                return
            assert unreal.GameplayStatics.get_player_controller(game, 0).get_editor_property('emergency_alarm_count') >= 1, 'Operator warning alarm missing'
            if 'halt_hold' not in case:
                case['halt_hold'] = {'started': now, 'vehicles': {name: {'max_displacement_cm': 0., 'peak_speed_cm': 0.,
                    'peak_linear_speed_cm': 0., 'peak_angular_speed_degrees': 0.}
                    for name in runtime['halted']}}
            for name, before in runtime['halted'].items():
                vehicle = live[name]
                displacement = (vehicle.get_actor_location()-unreal.Vector(*before)).length()
                speed = abs(vehicle.current_speed_cm)
                record = case['halt_hold']['vehicles'][name]
                record['max_displacement_cm'] = max(record['max_displacement_cm'], displacement)
                record['peak_speed_cm'] = max(record['peak_speed_cm'], speed)
                record['peak_linear_speed_cm'] = max(record['peak_linear_speed_cm'], body(vehicle).get_physics_linear_velocity().length())
                record['peak_angular_speed_degrees'] = max(record['peak_angular_speed_degrees'], body(vehicle).get_physics_angular_velocity_in_degrees().length())
                assert body(vehicle).is_simulating_physics() and vehicle.get_actor_enable_collision() and not vehicle.get_attach_parent_actor(), ('Halted body physics manipulated', name)
                assert displacement < 3, ('Unsafe displacement after no-escape warning', name, displacement)
                if name != jobs[0]['vehicle'] and vehicle.ai_state in (unreal.WarehouseAIState.OFF, unreal.WarehouseAIState.READY):
                    # Uncommanded rigs can have alternating contact velocity even
                    # while their position holds. Measure peak speed and the full
                    # 3-second displacement instead of one instantaneous sample.
                    assert not vehicle.is_remote_controlled() and not vehicle.pending_jobs and not vehicle.active_job.job_id and not vehicle.get_editor_property('traffic_yielding'), ('Idle vehicle received a drive request', name)
                    record['mode'] = 'idle_no_orders_remote_or_yield'
                elif vehicle.ai_state == unreal.WarehouseAIState.FAULT:
                    # An operator fault disables propulsion. Native contact
                    # braking can still produce instantaneous solver velocity;
                    # observe the unchanged 3cm bound continuously for 3 seconds.
                    assert not vehicle.get_editor_property('powered') and not vehicle.is_remote_controlled() and not vehicle.get_editor_property('traffic_yielding'), ('Faulted vehicle still accepts a drive request', name)
                    record['mode'] = 'fault_power_off_remote_off_bounded_native_hold'
                else:
                    assert speed < 2, ('Unsafe active speed after no-escape warning', name, speed)
                    record['mode'] = 'active_fault_or_powered_idle_strict_speed'
            assert not any(job['completed'] for job in jobs), 'Impossible transport was marked completed'
            if now-case['halt_hold']['started'] < 3:
                return
            case['halt_hold']['seconds'] = now-case['halt_hold']['started']
            complete_case(True, 'Expected safe halt: explicit fault, operator warning and stable native braking; transport did not complete')
            return
        transport_jobs = jobs
        heavy_exception = definition.get('expect') == 'heavy_exception'
        if heavy_exception:
            # User scope: 1000 kg is exception-only cargo, not a required normal
            # transport. A failed lift must warn and stop; other jobs still finish.
            heavy_job = jobs[0]
            heavy_vehicle = live[heavy_job['vehicle']]
            if heavy_vehicle.ai_state == unreal.WarehouseAIState.FAULT:
                assert heavy_vehicle.get_editor_property('emergency_blocked') and 'LIFT' in heavy_vehicle.get_editor_property('emergency_reason'), ('Heavy exception did not produce a lift recovery warning', heavy_vehicle.status)
                assert heavy_job['completed'] is None, 'Failed heavy lift reported completion'
                # FaultAI powers off and BrakeDrive zeroes both drive targets;
                # the native rig then applies its finite brake, not propulsion.
                assert not heavy_vehicle.get_editor_property('powered') and not heavy_vehicle.is_remote_controlled(), 'Heavy lift fault still accepts powered/remote drive'
                if 'heavy_exception' not in case:
                    case['heavy_exception'] = {'fault_time': now, 'vehicle_position': vector(heavy_vehicle.get_actor_location()),
                        'reason': heavy_vehicle.get_editor_property('emergency_reason'), 'reported_load_kg': heavy_vehicle.get_load_mass_kg(),
                        'actual_cargo_mass_kg': sum(body(live[name]).get_mass() for name in heavy_job['cargo']),
                        'max_displacement_cm': 0., 'peak_forward_speed_cm': 0., 'peak_linear_speed_cm': 0.,
                        'peak_angular_speed_degrees': 0., 'powered': False, 'transport_completed': False}
                record = case['heavy_exception']
                displacement = (heavy_vehicle.get_actor_location()-unreal.Vector(*record['vehicle_position'])).length()
                record['max_displacement_cm'] = max(record['max_displacement_cm'], displacement)
                record['peak_forward_speed_cm'] = max(record['peak_forward_speed_cm'], abs(heavy_vehicle.current_speed_cm))
                record['peak_linear_speed_cm'] = max(record['peak_linear_speed_cm'], body(heavy_vehicle).get_physics_linear_velocity().length())
                record['peak_angular_speed_degrees'] = max(record['peak_angular_speed_degrees'], body(heavy_vehicle).get_physics_angular_velocity_in_degrees().length())
                record['hold_observation_seconds'] = now-record['fault_time']
                assert displacement < 3, ('Heavy lift fault did not hold its position', displacement)
                if now-record['fault_time'] < 4:
                    return
                assert unreal.GameplayStatics.get_player_controller(game, 0).get_editor_property('emergency_alarm_count') >= 1, 'Heavy lift warning alarm missing'
                transport_jobs = jobs[1:]
            elif heavy_job['completed'] is None:
                return
        for name in runtime['vehicles']:
            if 'heavy_exception' in case and name == jobs[0]['vehicle']:
                continue
            assert live[name].ai_state != unreal.WarehouseAIState.FAULT, (name, live[name].status)
        if definition['kind'] == 'closed':
            active = live['StressAGV0']
            p = active.get_actor_location()
            assert not (250 < p.x < 520 and abs(p.y) < 120), 'Vehicle entered the central closed volume'
        if not all(job['completed'] is not None for job in transport_jobs):
            return
        if now-max(job['completed'] for job in transport_jobs) < 2:
            return
        for job in transport_jobs:
            verify_destination(job)
        if definition['kind'] == 'head_on':
            assert manager.get_editor_property('yield_count') >= 1 and not any(live[name].get_editor_property('traffic_yielding') for name in runtime['vehicles']), 'Head-on retreat/pass/resume was not completed'
        if definition['kind'] == 'parked':
            blocker = live['StressAGV1']
            assert (blocker.get_actor_location()-unreal.Vector(*runtime['initial']['StressAGV1'])).length() < 5, 'Route shoved the parked blocker aside'
            assert max(abs(sample['fleet'][0]['position'][1]) for sample in case['samples']) > 150, 'Vehicle did not physically detour around parked blocker'
        if 'heavy_exception' in case:
            complete_case(True, 'Exception-only 1000 kg lift stopped with an operator warning; three normal transports completed; heavy transport did not complete')
        else:
            complete_case(True, 'All native work orders completed with dynamic cargo support, destination tolerance, mass and collision retained')
    except Exception:
        detail = traceback.format_exc()
        if case is None:
            case = {'id': CASES[index]['id'], 'name': CASES[index]['name'], 'samples': [], 'jobs': jobs, 'expectation': CASES[index].get('expect', 'transport')}
        case['failure_snapshot'] = failure_snapshot()
        complete_case(False, detail)


handle = unreal.register_slate_post_tick_callback(tick)
