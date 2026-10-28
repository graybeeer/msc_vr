"""Compare lowered/raised production forks on the actual saved floor mesh.

Unsaved fixture, real finite motors and Chaos. Native input API only; no runtime
vehicle teleport/velocity injection, no sensor or collision disabling.
"""
import json
import time
import traceback
from pathlib import Path
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not editor.get_game_world()
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
authored = actors.get_all_level_actors()
template = next(a for a in authored if a.get_actor_label() == 'WH_AutonomousForklift')
floor = next(a for a in authored if a.get_actor_label() == 'Floor')
floor_origin, floor_extent = floor.get_actor_bounds(False)
floor_pose = floor.get_actor_transform()
floor_mesh = floor.static_mesh_component.static_mesh.get_path_name()
floor_translation = floor_pose.translation - floor_origin
floor_translation.z -= floor_extent.z
floor_pose.translation = floor_translation
part_poses = {p.get_name(): unreal.MathLibrary.make_relative_transform(p.get_world_transform(), template.get_actor_transform())
              for p in template.get_components_by_class(unreal.SceneComponent) if p != template.root_component}
part_assets = {p.get_name(): p.static_mesh.get_path_name() for p in template.get_components_by_class(unreal.StaticMeshComponent) if p.static_mesh}
drive_values = {n: template.get_editor_property(n) for n in ('empty_travel_speed_cm', 'loaded_travel_speed_cm', 'vehicle_mass_kg',
    'drive_acceleration_cm', 'drive_torque_nm', 'brake_torque_nm', 'drive_power_watts', 'max_fork_height_cm')}
assert floor_extent.x > 100 and floor_extent.y > 800, (floor_extent, 'Actual floor too small for this fixture')
# Preserve the authored mesh/scale and include its actual slab seams.
tile_radius = max(1, int(2600/(2*floor_extent.x)) + 1)
duration = 12.
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property('default_game_mode',
    unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
for tile in range(-tile_radius, tile_radius+1):
    floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector())
    floor.set_actor_label('ProductionFloor_' + str(tile))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset(floor_mesh))
    floor.static_mesh_component.set_collision_profile_name('BlockAll')
    floor_pose.translation = floor_translation + unreal.Vector(2*tile*floor_extent.x, 0, 0)
    floor.set_actor_transform(floor_pose, False, True)
    floor.tags = ['WarehouseMapStructure']
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, -700, 125))
for index, lane in enumerate((-350, 350)):
    rig = actors.spawn_actor_from_class(unreal.WarehouseForklift, unreal.Vector(0, lane, 0))
    rig.set_actor_label('LowFork' if index == 0 else 'RaisedFork')
    rig.set_editor_property('vehicle_name', 'FLOOR_PROBE_' + str(index))
    for part in rig.get_components_by_class(unreal.StaticMeshComponent):
        if part.get_name() in part_assets:
            part.set_static_mesh(unreal.load_asset(part_assets[part.get_name()]))
    def depth(part):
        result = 0
        while part.get_attach_parent():
            part = part.get_attach_parent()
            result += 1
        return result
    for part in sorted(rig.get_components_by_class(unreal.SceneComponent), key=depth):
        if part != rig.root_component:
            part.set_world_transform(unreal.MathLibrary.compose_transforms(part_poses[part.get_name()], rig.get_actor_transform()), False, True)
    for name, value in drive_values.items():
        rig.set_editor_property(name, value)
    rig.set_editor_property('auto_start', False)
    rig.set_editor_property('idle_parking_enabled', False)
    operator = actors.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, lane-180, 70))
    operator.set_actor_label('Operator' + str(index))
    for part in operator.get_components_by_class(unreal.PrimitiveComponent):
        part.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
saved = Path(unreal.Paths.project_saved_dir()) / 'Testing/RemoteFloorContact'
saved.mkdir(parents=True, exist_ok=True)
stamp = str(time.time_ns())
report = dict(floor_mesh=floor_mesh, floor_extent=str(floor_extent), duration=duration, rows=[], phases=[], tests=[],
              scope='Unsaved actual-floor/production-rig native input. No OS keys, no runtime poses or velocities written.')
started = time.monotonic()
phase = 'start'
at = 0.
rigs = None
last_time = -1.
last_sample = -1.
busy = False


def enter(name, now):
    global phase, at
    phase, at = name, now
    report['phases'].append(dict(phase=name, t=now))
    print('REMOTE_FLOOR_PHASE', name, now)


def finish(error=None):
    report.update(passed=error is None, error=error, wall_seconds=time.monotonic()-started)
    (saved / ('Report_' + stamp + '.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('REMOTE_FLOOR_PASSED' if error is None else 'REMOTE_FLOOR_FAILED', error or report['tests'])
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def tick(delta):
    global rigs, last_time, last_sample, busy
    if busy:
        return
    busy = True
    try:
        assert time.monotonic()-started < 240, ('Timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            enter('settle', 0)
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if now == last_time:
            return
        last_time = now
        if rigs is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            rigs = [live[n] for n in ('LowFork', 'RaisedFork')]
            operators = [live['Operator' + str(i)] for i in range(2)]
            for rig, operator in zip(rigs, operators):
                assert rig.begin_remote_control(operator), rig.get_editor_property('status')
        age = now-at
        for index, rig in enumerate(rigs):
            assert rig.get_editor_property('obstacle_check_count') == 0
            assert rig.get_remote_operator() and not rig.get_editor_property('mechanical_failure')
            assert abs(rig.get_physical_mass_kg()-1000) < .1
            forward = 1 if phase == 'forward' else -1 if phase == 'reverse' else 0
            rig.set_remote_input(forward, 0, 1 if index == 1 and phase == 'raise' else 0)
            if now-last_sample >= .066:
                parts = {p.get_name(): p for p in rig.get_components_by_class(unreal.StaticMeshComponent)}
                bounds = [unreal.SystemLibrary.get_component_bounds(parts[n]) for n in ('ForkL', 'ForkR')]
                # Visual bounds are telemetry, not used as a collision exception.
                report['rows'].append(dict(t=now, phase=phase, vehicle=index, pos=str(rig.get_actor_location()),
                    speed=rig.get_editor_property('current_speed_cm'), pitch=rig.get_actor_rotation().pitch,
                    fork_height=rig.get_fork_height_cm(), fork_visual_min_z=min(b[0].z-b[1].z for b in bounds),
                    status=rig.get_editor_property('status')))
        if now-last_sample >= .066:
            last_sample = now
        if phase == 'settle' and age > 2:
            enter('raise', now)
        elif phase == 'raise' and age > 1.5:
            enter('forward', now)
        elif phase == 'forward' and age >= duration:
            enter('brake', now)
        elif phase == 'brake' and age > 2:
            enter('reverse', now)
        elif phase == 'reverse' and age >= duration:
            enter('stop', now)
        elif phase == 'stop' and age > 2:
            for direction in ('forward', 'reverse'):
                start = next(s['t'] for s in report['phases'] if s['phase'] == direction)
                for index in range(2):
                    values = [abs(r['speed']) for r in report['rows'] if r['phase']==direction and r['vehicle']==index and r['t']-start>2]
                    assert values and min(values)>150, (direction,index,'Sustained-drive stall',min(values) if values else None)
                    report['tests'].append(dict(direction=direction, vehicle=index, min_speed=min(values), max_speed=max(values)))
            finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        busy = False


handle = unreal.register_slate_post_tick_callback(tick)
