"""Fresh native 1/2/4x PIE transports: fixed physics steps, timing, wall pacing.

Run via an editor Python bootstrap after compiling. Never saves. Fixture poses
are set in the editor only; gameplay movement uses the normal AGV AI and Chaos.
Hardware saturation is reported separately from transport equivalence.
"""
import json
import sys
import time
import traceback
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from apply_real_world_scale import fit

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
source = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'WH_AutonomousForklift')
rig = {c.get_name(): (c.get_relative_transform(), c.static_mesh, list(c.get_editor_property('override_materials')))
       for c in source.get_components_by_class(unreal.StaticMeshComponent)}
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 't.OverrideFPS 0')
unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property(
    'default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))


def spawn(cls, label, point):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
    actor.set_actor_label(label)
    actor.set_editor_property('is_spatially_loaded', False)
    return actor


floor = spawn(unreal.StaticMeshActor, 'SpeedTestFloor', (0, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.set_actor_scale3d(unreal.Vector(50, 25, .2))
spawn(unreal.PlayerStart, 'SpeedTestStart', (-950, -700, 96.5))
computer = spawn(unreal.WarehouseComputer, 'SpeedTestConsole', (-700, -700, 0))
vehicle = spawn(unreal.WarehouseForklift, 'SpeedTestAGV', (0, 0, 0))
vehicle.set_editor_property('auto_start', False)
vehicle.set_editor_property('vehicle_name', 'SpeedTestAGV')
for component in vehicle.get_components_by_class(unreal.StaticMeshComponent):
    transform, mesh, materials = rig[component.get_name()]
    component.set_relative_transform(transform, False, True)
    component.set_static_mesh(mesh)
    component.set_editor_property('override_materials', materials)
pallet = spawn(unreal.WarehousePallet, 'SpeedTestPallet', (500, 0, .1))
cargo = spawn(unreal.WarehouseCargo, 'SpeedTestCargo', (500, 0, 27.7))
intact = unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2')
assert intact, 'Prepare the intact carton asset before this test'
cargo.set_cargo_mesh(intact)
recipe = unreal.WarehouseCargo.generate_cargo_recipe(20261012, 7, 0, unreal.Vector(40, 30, 25), 1)
assert cargo.apply_cargo_recipe(recipe)
fit(cargo, (40, 30, 25), (500, 0, 27.7))
damage = spawn(unreal.WarehouseDamageSystem, 'SpeedTestStrength', (0, -1000, 0))
damage.set_editor_property('objects', [
    unreal.WarehouseStrength(name='SpeedTestPallet', members=[pallet], mass_kg=25, rated_load_kg=1500),
    unreal.WarehouseStrength(name='SpeedTestCargo', members=[cargo], mass_kg=cargo.get_editor_property('gross_mass_kg'), rated_load_kg=120)])
spawn(unreal.DirectionalLight, 'SpeedTestSun', (0, 0, 600))

SPEEDS = (1, 2, 4)
DESTINATION = unreal.Vector(950, 0, 0)
JOB_ID = 'SPEED-EQUIVALENCE'
output = Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/SimulationSpeed/Report.json')
output.parent.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
phase = 'request_play'
phase_wall = started
index = 0
live = None
clock_actor = None
game = None
submitted = False
submitted_at = submitted_wall = 0.
completed_at = completed_wall = None
last_simulation = None
last_sample = -1.
case = None
results = []


def vector(v):
    return [v.x, v.y, v.z]


def dynamic(actor):
    body = actor.get_component_by_class(unreal.StaticMeshComponent)
    assert body.is_simulating_physics(), (actor.get_name(), 'Physics disabled')
    assert body.get_collision_enabled() != unreal.CollisionEnabled.NO_COLLISION, (actor.get_name(), 'Collision disabled')
    assert not actor.get_attach_parent_actor(), (actor.get_name(), 'Load attached instead of physically supported')
    return body


def write_report(ok, detail):
    output.write_text(json.dumps({'ok': ok, 'detail': detail, 'fixture_seed': 20261012,
        'simulated_step_seconds': 1 / 30, 'cases': results, 'wall_seconds': time.monotonic() - started},
        ensure_ascii=False, indent=2), encoding='utf-8')


def finish(ok, detail):
    write_report(ok, detail)
    print('SIMULATION_SPEED_PIE_PASSED' if ok else 'SIMULATION_SPEED_PIE_FAILED', detail)
    unreal.unregister_slate_post_tick_callback(handle)
    if editor.get_game_world():
        level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def compare():
    base = results[0]
    for result in results[1:]:
        assert abs(result['simulation_duration'] - base['simulation_duration']) <= max(3., base['simulation_duration'] * .05), (
            'Simulated transport duration changed with wall speed',
            (base['requested_speed'], base['simulation_duration']),
            (result['requested_speed'], result['simulation_duration']))
        for name in ('pallet_position', 'cargo_position', 'vehicle_position'):
            delta = sum((a - b) ** 2 for a, b in zip(base[name], result[name])) ** .5
            assert delta < 5., ('Final physical pose changed with speed', name, delta)
        assert abs((result['pallet_yaw'] - base['pallet_yaw'] + 180) % 360 - 180) < 2., 'Pallet heading changed with speed'
        assert result['recovery_count'] == base['recovery_count'], 'Different recovery branch at another speed'
    print('SIMULATION_SPEED_EQUIVALENCE_PASSED', 'same seeded transport; completion, dynamic support, pose, duration and recovery count')


def tick(dt):
    global phase, phase_wall, index, live, clock_actor, game, submitted, submitted_at, submitted_wall
    global completed_at, completed_wall, last_simulation, last_sample, case
    try:
        wall = time.monotonic()
        assert wall - started < 600, 'Overall wall-clock test timeout'
        if phase == 'request_play':
            if wall - phase_wall < .5:
                return
            level.editor_request_begin_play()
            phase = 'run'
            return
        game = editor.get_game_world()
        if phase == 'await_stop':
            if game:
                return
            index += 1
            if index == len(SPEEDS):
                compare()
                finish(True, 'Fresh native 1/2/4x PIE transports retain identical 1/30 world steps and 1/120 Chaos substeps; requested and achieved wall speeds reported separately')
                return
            live = clock_actor = case = None
            submitted = False
            completed_at = completed_wall = last_simulation = None
            last_sample = -1.
            phase = 'request_play'
            phase_wall = wall
            return
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            clocks = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseSimulationClock)
            assert len(clocks) == 1, ('Expected exactly one runtime simulation clock', len(clocks))
            clock_actor = clocks[0]
            assert not clock_actor.set_speed(3), 'Unsupported speed accepted'
            assert clock_actor.set_speed(SPEEDS[index]), 'Requested supported speed rejected'
            assert clock_actor.get_editor_property('requested_speed') == SPEEDS[index]
            case = {'requested_speed': SPEEDS[index], 'samples': []}
            print('SIMULATION_SPEED_CASE_STARTED', SPEEDS[index])
        if last_simulation is not None and now > 2:
            step = now - last_simulation
            assert abs(step - 1 / 30) < .0001, ('Physics frame duration changed with wall speed', SPEEDS[index], step)
        last_simulation = now
        v, p, c, comp, strength = [live[label] for label in (
            'SpeedTestAGV', 'SpeedTestPallet', 'SpeedTestCargo', 'SpeedTestConsole', 'SpeedTestStrength')]
        assert not v.get_editor_property('mechanical_failure'), v.get_editor_property('status')
        assert v.get_editor_property('ai_state') != unreal.WarehouseAIState.FAULT, v.get_editor_property('status')
        if now - last_sample >= 1:
            last_sample = now
            for actor in (v, p, c):
                dynamic(actor)
            sample = {'simulation': now, 'wall': wall - phase_wall, 'state': str(v.get_editor_property('ai_state')),
                'pallet': vector(p.get_actor_location()), 'vehicle': vector(v.get_actor_location()),
                'speed_cm_s': v.get_editor_property('current_speed_cm'), 'achieved_speed': clock_actor.get_editor_property('achieved_speed')}
            case['samples'].append(sample)
            if len(case['samples']) % 10 == 0:
                print('SIMULATION_SPEED_PROGRESS', SPEEDS[index], round(now, 2), v.get_editor_property('status'),
                    'achieved', round(sample['achieved_speed'], 2))
        if not submitted:
            if now < 2:
                return
            assert (p.get_actor_location() - unreal.Vector(500, 0, 0)).length() < 2, 'Same seeded pallet did not settle on floor'
            assert c in strength.get_supported_actors(p), 'Cargo did not settle on the pallet'
            assert abs(v.get_physical_mass_kg() - 1000) < .5, 'Unexpected vehicle physical mass'
            v.toggle_power()
            job = unreal.WarehouseWorkOrder(job_id=JOB_ID, source_system='FMS-SPEED-CHECK', pallet=p,
                destination=unreal.Transform(location=DESTINATION))
            assert v.submit_job(job), 'Fixture work order rejected'
            task = next(t for t in comp.get_editor_property('tasks') if t.job_id == JOB_ID)
            assert task.submission_time_known and abs(task.submitted_simulation_seconds - now) < .001, 'Dispatch timestamp not captured'
            submitted_at, submitted_wall, submitted = now, wall, True
            return
        assert now - submitted_at < 110, ('Native transport exceeded the fixture simulation-time limit', v.get_editor_property('status'))
        if completed_at is None:
            if JOB_ID not in v.get_editor_property('completed_job_ids'):
                return
            report = next(r for r in v.get_editor_property('job_reports') if r.job_id == JOB_ID and r.result == 'COMPLETED')
            task = next(t for t in comp.get_editor_property('tasks') if t.job_id == JOB_ID)
            assert task.completed and task.submission_time_known
            assert abs(task.finished_simulation_seconds - report.simulation_seconds) < .001, 'Computer used polling time rather than completion report'
            completed_at, completed_wall = report.simulation_seconds, wall
            case.update(simulation_duration=completed_at - submitted_at, wall_duration=completed_wall - submitted_wall)
            case['achieved_multiplier'] = case['simulation_duration'] / case['wall_duration']
            case['hardware_saturated'] = case['achieved_multiplier'] < SPEEDS[index] * .85
            assert abs(comp.get_task_elapsed_seconds(JOB_ID) - case['simulation_duration']) < .001
            return
        if now - completed_at < 2:
            return
        assert (p.get_actor_location() - DESTINATION).length() <= 2., 'Completed pallet does not occupy its destination'
        assert c in strength.get_supported_actors(p) and not strength.has_failed(c), 'Completed cargo lost its physical support'
        assert abs(p.get_actor_rotation().pitch) < 2 and abs(p.get_actor_rotation().roll) < 2
        assert dynamic(p).get_physics_linear_velocity().length() < 5, 'Completed pallet did not settle'
        assert abs(comp.get_task_elapsed_seconds(JOB_ID) - case['simulation_duration']) < .001, 'Completed elapsed time kept growing'
        assert len([t for t in comp.get_editor_property('tasks') if t.job_id == JOB_ID]) == 1
        case.update(pallet_position=vector(p.get_actor_location()), cargo_position=vector(c.get_actor_location()),
            vehicle_position=vector(v.get_actor_location()), pallet_yaw=p.get_actor_rotation().yaw,
            recovery_count=v.get_editor_property('recovery_count'), physical_load_kg=p.get_editor_property('pallet_mass_kg') + c.get_editor_property('gross_mass_kg'))
        results.append(case)
        print('SIMULATION_SPEED_CASE_PASSED', json.dumps({k: value for k, value in case.items() if k != 'samples'}, ensure_ascii=False))
        write_report(None, 'In progress')
        level.editor_request_end_play()
        phase = 'await_stop'
    except Exception:
        detail = traceback.format_exc()
        if case and case not in results:
            results.append(case)
        finish(False, detail)


handle = unreal.register_slate_post_tick_callback(tick)
