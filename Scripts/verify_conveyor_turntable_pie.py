"""Native 90-degree transfer: independent pallet/carton and loose carton.

Two unsaved Entry lines turn in opposite directions. After PIE starts, no
payload transform, velocity, mass, simulation flag or attachment is assigned.
The real rollers, centering controller and finite rotary joint must complete
the transfer. Public layout controls also prove safe empty-table settings.
"""
import json
import math
from pathlib import Path
import time
import traceback

import unreal


level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
output = Path(unreal.Paths.project_saved_dir()) / 'Testing/Conveyor'
output.mkdir(parents=True, exist_ok=True)
command = unreal.SystemLibrary.get_command_line().lower()
keep_open = '-keeptesteditoropen' in command
rendered = '-nullrhi' not in command
report = dict(passed=False, checks=[], samples=[], screenshots=[], rendered=rendered, nativePhysics=True, osInputTested=False,
              scope='Two real rotary mechanisms; left 25kg pallet with independent 12kg carton, right independent 12kg carton; native layout safety and endpoint stops')
started = time.monotonic()
phase = 'start'
handle = game = pc = layout = None
routes = []
last_sample = 0
done_at = None
finished = False
camera = None
captures = {}
capture_suffix = str(time.time_ns())
phase_at = started


def check(name, **data):
    report['checks'].append(dict(name=name, **data))
    print('CONVEYOR_TURNTABLE_CHECK', name, json.dumps(data, ensure_ascii=False))


def allof(cls):
    return unreal.GameplayStatics.get_all_actors_of_class(game, cls)


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def yaw_delta(actor, initial=0):
    return (actor.get_actor_rotation().yaw-initial+180) % 360-180


def machine_pose_and_link(actor):
    point, rotation = actor.get_actor_location(), actor.get_actor_rotation()
    return tuple(round(value, 5) for value in (point.x, point.y, point.z, rotation.pitch, rotation.yaw, rotation.roll)) + (actor.get_editor_property('next_conveyor'),)


def screenshot_ready(path):
    if not path.is_file():
        return None
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if len(data) < 1000 or data[:8] != b'\x89PNG\r\n\x1a\n' or data[-8:-4] != b'IEND':
        return None
    return dict(path=str(path), bytes=len(data), width=int.from_bytes(data[16:20], 'big'),
                height=int.from_bytes(data[20:24], 'big'))


def request_capture(name, sim, **state):
    if not rendered or name in captures:
        return
    # Never overlap high-resolution screenshot requests. A later native phase
    # lasts long enough to wait for the previous image's complete PNG footer.
    if any(not row.get('image') for row in captures.values()):
        return
    path = output / ('Turntable_' + name + '_' + capture_suffix + '.png')
    captures[name] = dict(path=path, requested_at=time.monotonic(), sim_seconds=sim, state=state)
    unreal.SystemLibrary.execute_console_command(game, 'HighResShot 1600x900 filename=' + path.as_posix(), pc)


def poll_captures():
    for name, row in captures.items():
        if row.get('image'):
            continue
        image = screenshot_ready(row['path'])
        assert image or time.monotonic()-row['requested_at'] < 45, ('Incomplete high-resolution PNG', name, str(row['path']))
        if image:
            assert (image['width'], image['height']) == (1600, 900), image
            row['image'] = image
            report['screenshots'].append(dict(stage=name, sim_seconds=row['sim_seconds'], state=row['state'], **image))
            check('rendered_native_'+name+'_1600x900_png_complete', **image)


def frame_finished_routes():
    # Fit the actual colliding machine/payload bounds, excluding the fixture's
    # floor, empty UI line and status text. Both opposite outlet directions
    # remain inside the image after changing the canonical left/right mapping.
    points = []
    for row in routes:
        for actor in [row['source'], row['table'], row['target']] + [actor for actor, _ in row['payload']]:
            center, extent = actor.get_actor_bounds(True)
            points.extend((center.x+sx*extent.x, center.y+sy*extent.y, center.z+sz*extent.z)
                          for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1))
    low = [min(point[axis] for point in points) for axis in range(3)]
    high = [max(point[axis] for point in points) for axis in range(3)]
    focus = [(low[axis]+high[axis])*.5 for axis in range(3)]
    pitch, yaw, fov = -52, 50, 70
    p, y = math.radians(pitch), math.radians(yaw)
    forward = (math.cos(p)*math.cos(y), math.cos(p)*math.sin(y), math.sin(p))
    right = (-math.sin(y), math.cos(y), 0)
    up = (-math.sin(p)*math.cos(y), -math.sin(p)*math.sin(y), math.cos(p))
    width, height = pc.get_viewport_size()
    aspect = max(1600/900, width/max(1, height))
    tan_horizontal = math.tan(math.radians(fov)*.5)
    tan_vertical = tan_horizontal/aspect
    distance = 700
    for point in points:
        offset = [point[axis]-focus[axis] for axis in range(3)]
        depth = sum(offset[axis]*forward[axis] for axis in range(3))
        horizontal = abs(sum(offset[axis]*right[axis] for axis in range(3)))/tan_horizontal
        vertical = abs(sum(offset[axis]*up[axis] for axis in range(3)))/tan_vertical
        distance = max(distance, horizontal-depth, vertical-depth)
    distance *= 1.15
    eye = unreal.Vector(*[focus[axis]-forward[axis]*distance for axis in range(3)])
    camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(fov)
    camera.set_actor_location(eye, False, True)
    camera.set_actor_rotation(unreal.Rotator(pitch=pitch, yaw=yaw), True)
    pc.set_view_target_with_blend(camera, 0)
    report['final_camera'] = dict(bounds_min_cm=low, bounds_max_cm=high, focus_cm=focus,
                                distance_cm=distance, horizontal_fov_degrees=fov, aspect=aspect)


def finish(ok, detail):
    global finished
    if finished:
        return
    finished = True
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    try:
        if layout and layout.get_editor_property('editing'):
            layout.set_editing(False, 0)
        if editor.get_game_world():
            level.editor_request_end_play()
    except Exception:
        ok = False
        detail += '\nCleanup failed:\n' + traceback.format_exc()
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    report.update(passed=ok, detail=detail, wall_seconds=round(time.monotonic()-started, 3))
    (output / 'TurntableReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('CONVEYOR_TURNTABLE_PIE_PASSED' if ok else 'CONVEYOR_TURNTABLE_PIE_FAILED', detail)
    if not keep_open:
        unreal.SystemLibrary.quit_editor()


def fixture():
    assert level.load_level('/Engine/Maps/Entry')
    for actor in list(actors.get_all_level_actors()):
        if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
            actors.destroy_actor(actor)
    world = editor.get_editor_world()
    settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
    settings.set_editor_property('default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')

    def spawn(cls, name, point):
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
        actor.set_actor_label(name)
        return actor

    def line(name, point, width=125):
        actor = spawn(unreal.WarehouseConveyor, name, point)
        assert actor.configure_geometry(300, width, 40, 20), actor.get_editor_property('status')
        return actor

    def carton(name, point, size):
        actor = spawn(unreal.WarehouseCargo, name, point)
        actor.set_cargo_mesh(cube)
        actor.set_actor_scale3d(unreal.Vector(*[value/100 for value in size]))
        actor.set_gross_mass_kg(12)
        return actor

    floor = spawn(unreal.StaticMeshActor, 'TurntableFixtureFloor', (0, 0, -10))
    floor.static_mesh_component.set_static_mesh(cube)
    floor.static_mesh_component.set_collision_profile_name('BlockAll')
    floor.set_actor_scale3d(unreal.Vector(60, 60, .2))
    floor.tags = ['WarehouseMapStructure', 'LayoutFixed', 'ObserverArea']
    if rendered:
        key = spawn(unreal.DirectionalLight, 'TurntableFixtureSun', (0, 0, 1800))
        key.set_actor_rotation(unreal.Rotator(-55, -35, 0), True)
        key.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property('intensity', 6)
        sky = spawn(unreal.SkyLight, 'TurntableFixtureSky', (0, 0, 1400))
        sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property('intensity', 1.5)
        fill = spawn(unreal.PointLight, 'TurntableFixtureFill', (-1000, -1200, 850))
        fill_component = fill.get_component_by_class(unreal.PointLightComponent)
        fill_component.set_editor_property('intensity', 25000)
        fill_component.set_editor_property('attenuation_radius', 4500)
        view = spawn(unreal.CameraActor, 'TurntableFixtureCamera', (-950, -1850, 850))
        view.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(view.get_actor_location(), unreal.Vector(-250, -950, 50)), True)
        view.get_component_by_class(unreal.CameraComponent).set_field_of_view(60)
        view.get_component_by_class(unreal.CameraComponent).set_editor_property('constrain_aspect_ratio', False)
    spawn(unreal.PlayerStart, 'TurntableSafePlayerStart', (-1800, -2400, 100))
    spawn(unreal.load_class(None, '/Script/msc_vr.WarehouseSimulationClock'), 'TurntableClock', (-1800, -2400, 0))
    payloads = []
    # An independent empty assembly exercises the actual in-game connection
    # transaction. Loaded lines below must reject editing instead of being
    # treated as an empty-fixture success.
    ui_source = line('EmptyUISource', (1500, 1800, 0))
    ui_table = spawn(unreal.WarehouseConveyor, 'EmptyUIRotary', (2000, 1800, 0))
    ui_table.set_editor_property('kind', unreal.WarehouseConveyorKind.ROTARY_TRANSFER)
    assert ui_table.configure_geometry(200, 200, 40, 20), ui_table.get_editor_property('status')
    assert ui_table.snap_entrance_to(ui_source), ui_table.get_editor_property('status')
    ui_target = line('EmptyUITarget', (2000, 1300, 0))
    assert ui_target.set_conveyor_pose(-90, 0, 0)
    assert ui_target.snap_entrance_to(ui_table), ui_target.get_editor_property('status')
    for name, row_y, right in [('Pallet', -1000, False), ('Carton', 1000, True)]:
        source = line(name+'Source', (-500, row_y, 0), 125 if name == 'Pallet' else 80)
        if name == 'Carton':
            source.set_editor_property('kind', unreal.WarehouseConveyorKind.CARTON_BELT)
            assert source.rebuild_conveyor()
        table = spawn(unreal.WarehouseConveyor, name+'Rotary', (0, row_y, 0))
        table.set_editor_property('kind', unreal.WarehouseConveyorKind.ROTARY_TRANSFER)
        assert table.configure_geometry(200, 200, 40, 20), table.get_editor_property('status')
        assert table.set_turn_direction(right), table.get_editor_property('status')
        assert table.snap_entrance_to(source), table.get_editor_property('status')
        target = line(name+'Target', (0, row_y+(500 if right else -500), 0), 125 if name == 'Pallet' else 80)
        if name == 'Carton':
            target.set_editor_property('kind', unreal.WarehouseConveyorKind.CARTON_BELT)
            assert target.rebuild_conveyor()
        assert target.set_conveyor_pose(90 if right else -90, 0, 0)
        assert target.snap_entrance_to(table), target.get_editor_property('status')
        assert (target.get_entrance_position()-table.get_exit_position()-table.get_exit_direction()*3).length() < .1
        start = source.get_actor_transform().transform_location(unreal.Vector(-90, 0, 40.2))
        if name == 'Pallet':
            pallet = spawn(unreal.WarehousePallet, 'TurnPallet', (start.x, start.y, start.z))
            payloads.append((pallet, 25))
            payloads.append((carton('TurnPalletCarton', (start.x, start.y, 65.3), (30, 24, 20)), 12))
        else:
            payloads.append((carton('TurnLooseCarton', (start.x, start.y, 60.2), (40, 35, 40)), 12))
    strength = spawn(unreal.WarehouseDamageSystem, 'TurntableStrength', (-1800, -2000, 0))
    strength.set_editor_property('objects', [unreal.WarehouseStrength(name=actor.get_actor_label(), members=[actor],
        mass_kg=mass, rated_load_kg=100000, impact_yield_j=100000, impact_failure_j=1000000) for actor, mass in payloads])


def assert_payload(route):
    for actor, mass in route['payload']:
        component = body(actor)
        assert component.is_simulating_physics() and component.is_gravity_enabled(), ('Payload simulation disabled', actor.get_actor_label())
        assert component.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
        assert abs(component.get_mass()-mass) < .1, ('Payload mass changed', actor.get_actor_label())
        assert not actor.get_attach_parent_actor(), ('Payload attached to machine', actor.get_actor_label())
        assert actor.get_actor_location().z > 30, ('Payload fell to the floor', actor.get_actor_label(), actor.get_actor_location())
    carrier = route['table'].get_editor_property('rotary_carrier')
    assert carrier and carrier.is_simulating_physics() and carrier.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS


def _tick_impl(delta_seconds):
    global game, pc, layout, phase, routes, last_sample, done_at, camera, phase_at
    try:
        assert time.monotonic()-started < 300, ('Turntable wall timeout', phase)
        if phase == 'start':
            fixture()
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        sim = unreal.GameplayStatics.get_time_seconds(game)
        assert sim < 180, ('Turntable native simulation timeout', [(row['name'], row['table'].get_editor_property('status')) for row in routes])
        if rendered:
            poll_captures()
        if phase == 'settle':
            if sim < .5:
                return
            named = {actor.get_actor_label(): actor for actor in allof(unreal.Actor)}
            routes = [dict(name='pallet_left', source=named['PalletSource'], table=named['PalletRotary'], target=named['PalletTarget'],
                           payload=[(named['TurnPallet'], 25), (named['TurnPalletCarton'], 12)], base=named['TurnPallet'], expected_yaw=-90,
                           phases=set(), max_carrier_yaw=0, max_payload_yaw=0, seen_source=False, seen_target=False),
                      dict(name='carton_right', source=named['CartonSource'], table=named['CartonRotary'], target=named['CartonTarget'],
                           payload=[(named['TurnLooseCarton'], 12)], base=named['TurnLooseCarton'], expected_yaw=90,
                           phases=set(), max_carrier_yaw=0, max_payload_yaw=0, seen_source=False, seen_target=False)]
            for row in routes:
                assert_payload(row)
                assert row['base'] in row['source'].get_physical_loads(), ('Source not supporting its payload', row['name'])
                row['seen_source'] = True
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            pc.set_observer_floor(0)
            pc.set_layout_editing(True)
            layout = allof(unreal.WarehouseLayoutEditor)[0]
            layout.set_category(unreal.WarehouseLayoutCategory.CONVEYOR)
            for row in routes:
                before = [machine_pose_and_link(actor) for actor in (row['source'], row['table'])]
                layout.set_selection([row['source'], row['table']])
                assert not layout.connect_selected_conveyors(), ('A loaded source was reconnected', row['name'])
                assert [machine_pose_and_link(actor) for actor in (row['source'], row['table'])] == before
            check('loaded_source_connection_is_rejected_with_actual_pose_and_links_unchanged_for_both_routes')
            ui_source, table, ui_target = [named[name] for name in ('EmptyUISource', 'EmptyUIRotary', 'EmptyUITarget')]
            # Public native layout validation checks real machine bodies,
            # not merely the pre-authored algebraic port coincidence.
            for previous, following in ((ui_source, table), (table, ui_target)):
                layout.set_selection([previous, following])
                assert layout.connect_selected_conveyors(), (previous.get_actor_label(), following.get_actor_label(), layout.get_editor_property('status'))
                clearance = following.get_entrance_position()-previous.get_exit_position()
                assert (clearance-previous.get_exit_direction()*3).length() < .1
                assert previous.get_editor_property('next_conveyor') == following
            check('empty_native_layout_connect_validates_actual_source_rotary_and_outlet_frames_and_three_cm_ports')
            layout.set_selection([table])
            assert layout.set_selected_conveyor_turn_direction(True), layout.get_editor_property('status')
            assert table.get_editor_property('turn_right')
            assert (table.get_exit_direction()-table.get_actor_right_vector()).length() < .001, 'Right outlet must point to the machine right side'
            assert layout.set_selected_conveyor_turn_direction(False), layout.get_editor_property('status')
            assert not table.get_editor_property('turn_right')
            assert (table.get_exit_direction()+table.get_actor_right_vector()).length() < .001, 'Left outlet must point to the machine left side'
            assert not layout.configure_selected_conveyor(0, 5, 0, 200, 200, 20), 'Rotary table accepted an incline'
            assert not layout.configure_selected_conveyor(0, 0, 0, 180, 200, 20), 'Rotary table accepted a nonstandard deck length'
            assert table.get_editor_property('length_cm') == 200 and table.get_editor_property('width_cm') == 200
            check('native_empty_table_left_right_outlet_controls_and_fixed_horizontal_two_metre_deck')
            pc.toggle_layout_editing()
            assert not unreal.GameplayStatics.is_game_paused(game)
            if rendered:
                camera = named['TurntableFixtureCamera']
                pc.set_view_target_with_blend(camera, 0)
            phase = 'transport'
            return
        if phase == 'transport':
            ready = True
            for row in routes:
                assert_payload(row)
                table, target, base = row['table'], row['target'], row['base']
                rotary_phase = table.get_editor_property('rotary_phase')
                row['phases'].add(str(rotary_phase))
                if rendered and row['name'] == 'pallet_left':
                    carrier_yaw = table.get_editor_property('carrier_yaw_degrees')
                    if rotary_phase == unreal.WarehouseRotaryPhase.CENTERING and base in table.get_physical_loads():
                        request_capture('PalletCentering', sim, phase=str(rotary_phase), carrier_yaw=carrier_yaw, payload_yaw=yaw_delta(base))
                    if rotary_phase == unreal.WarehouseRotaryPhase.ROTATING and 25 < abs(carrier_yaw) < 70:
                        request_capture('PalletRotating', sim, phase=str(rotary_phase), carrier_yaw=carrier_yaw, payload_yaw=yaw_delta(base))
                row['max_carrier_yaw'] = max(row['max_carrier_yaw'], abs(table.get_editor_property('carrier_yaw_degrees')))
                row['max_payload_yaw'] = max(row['max_payload_yaw'], abs(yaw_delta(base)))
                row['seen_target'] = row['seen_target'] or base in target.get_physical_loads()
                local = target.get_actor_transform().inverse_transform_location(base.get_actor_location())
                turn_error = abs((yaw_delta(base)-row['expected_yaw']+180) % 360-180)
                stopped = (row['seen_target'] and target.get_editor_property('state') == unreal.WarehouseConveyorState.PICKUP_READY and
                           local.x > 65 and abs(local.y) < 30 and body(base).get_physics_linear_velocity().length() < 5)
                route_ready = (stopped and table.get_editor_property('completed_rotary_transfers') >= 1 and
                               rotary_phase == unreal.WarehouseRotaryPhase.EMPTY and
                               row['max_carrier_yaw'] > 75 and row['max_payload_yaw'] > 70 and turn_error < 20)
                ready = ready and route_ready
                if route_ready:
                    row['final_local'] = [local.x, local.y, local.z]
                    row['final_yaw'] = yaw_delta(base)
            if sim-last_sample >= 2:
                report['samples'].append(dict(sim_seconds=round(sim, 2), routes=[dict(name=row['name'],
                    phase=str(row['table'].get_editor_property('rotary_phase')), carrier_yaw=row['table'].get_editor_property('carrier_yaw_degrees'),
                    payload_yaw=yaw_delta(row['base']), position_cm=[row['base'].get_actor_location().x, row['base'].get_actor_location().y, row['base'].get_actor_location().z],
                    table_status=row['table'].get_editor_property('status'), target_status=row['target'].get_editor_property('status'),
                    completed=row['table'].get_editor_property('completed_rotary_transfers')) for row in routes]))
                last_sample = sim
            if ready:
                if done_at is None:
                    done_at = sim
                if sim-done_at >= 1:
                    for row in routes:
                        check(row['name']+'_native_center_rotate_discharge_and_endpoint_stop',
                              phases=sorted(row['phases']), maximum_carrier_yaw=row['max_carrier_yaw'],
                              maximum_payload_yaw=row['max_payload_yaw'], final_yaw=row['final_yaw'], final_local_cm=row['final_local'])
                    if rendered:
                        assert 'PalletCentering' in captures and 'PalletRotating' in captures, 'Native pallet phases were not captured'
                        frame_finished_routes()
                        phase = 'capture_final'
                        phase_at = time.monotonic()
                    else:
                        finish(True, 'Independent 25+12kg pallet load turns left and 12kg loose carton turns right by native roller/joint contact, discharges and stops; safe fixed-deck UI controls; no payload runtime transforms/velocities/attachments assigned')
            else:
                done_at = None
            return
        if phase == 'capture_final':
            for row in routes:
                assert_payload(row)
                assert row['table'].get_editor_property('rotary_phase') == unreal.WarehouseRotaryPhase.EMPTY
                assert row['target'].get_editor_property('state') == unreal.WarehouseConveyorState.PICKUP_READY
            if time.monotonic()-phase_at < .5:
                return
            request_capture('FinalResetComplete', sim, routes=[dict(name=row['name'], final_yaw=yaw_delta(row['base']),
                carrier_yaw=row['table'].get_editor_property('carrier_yaw_degrees'), completed=row['table'].get_editor_property('completed_rotary_transfers')) for row in routes])
            if len(captures) == 3 and all(row.get('image') for row in captures.values()):
                finish(True, 'Native independent pallet/carton left turn and loose-carton right turn, physical discharge/end stops and carrier reset; three actual rendered 1600x900 phase images with complete PNG footers; no OS-input claim or payload runtime transforms/velocities/attachments assigned')
    except Exception:
        finish(False, traceback.format_exc())


_callback_busy = False


def tick(delta_seconds):
    # LoadLevel, spawning and geometry rebuilds may synchronously pump Slate.
    # Reentrant callbacks must not repeat a transaction or load another map.
    global _callback_busy
    if _callback_busy:
        return
    _callback_busy = True
    try:
        return _tick_impl(delta_seconds)
    finally:
        _callback_busy = False


handle = unreal.register_slate_post_tick_callback(tick)
