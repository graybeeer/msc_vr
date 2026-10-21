"""Unsaved native conveyor layout: free angles, slopes, ports and rejection.

Real production controller/layout APIs and real empty roller rigs are used.
The occupied negative fixture settles by Chaos before editing is paused; its
parcel's pose, velocity and attachment are never assigned after PIE starts.
No OS-input, corner-transfer or elevator-transfer claim is made by this test.
"""
import json
from pathlib import Path
import time
import traceback

import unreal


level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
output = Path(unreal.Paths.project_saved_dir()) / 'Testing/Conveyor'
output.mkdir(parents=True, exist_ok=True)
keep_open = '-keeptesteditoropen' in unreal.SystemLibrary.get_command_line().lower()
started = time.monotonic()
phase = 'start'
handle = None
game = pc = layout = source = receiver = occupied = parcel = None
native_source = native_receiver = native_box = None
native_at = last_sample = 0
stable_at = None
seen_source = seen_receiver = False
report = dict(passed=False, checks=[], samples=[], osInputTested=False,
              excluded=['Corner transfer', 'Pallet lift handshakes', 'Rendered numeric panel'],
              scope='Unsaved Entry native PC/LayoutEditor, real roller mechanisms, 3D port geometry and occupied/collision rejection')
finished = False


def check(name, **data):
    report['checks'].append(dict(name=name, **data))
    print('CONVEYOR_LAYOUT_CHECK', name, json.dumps(data, ensure_ascii=False))


def allof(cls):
    return unreal.GameplayStatics.get_all_actors_of_class(game, cls)


def pose(actor):
    location, rotation = actor.get_actor_location(), actor.get_actor_rotation()
    return tuple(round(value, 4) for value in (location.x, location.y, location.z, rotation.pitch, rotation.yaw, rotation.roll))


def geometry(actor):
    return (pose(actor), actor.get_editor_property('length_cm'), actor.get_editor_property('width_cm'),
            actor.get_editor_property('surface_height_cm'), actor.get_editor_property('speed_cm'),
            len(actor.get_editor_property('rollers')), actor.get_editor_property('next_conveyor'), actor.get_editor_property('lift'),
            actor.get_editor_property('lift_floor'), actor.get_editor_property('destination_floor'), actor.get_editor_property('lift_infeed'))


def assert_rig(actor):
    frame = actor.get_editor_property('frame')
    assert frame.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
    rollers = actor.get_editor_property('rollers')
    assert rollers and all(roller.is_simulating_physics() and roller.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS for roller in rollers)


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
        detail += '\nCleanup: ' + traceback.format_exc()
    performance.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    report.update(passed=ok, detail=detail, wall_seconds=round(time.monotonic()-started, 3))
    (output / 'LayoutReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('CONVEYOR_LAYOUT_PIE_PASSED' if ok else 'CONVEYOR_LAYOUT_PIE_FAILED', detail)
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

    floor = spawn(unreal.StaticMeshActor, 'ConveyorLayoutFloor', (0, 0, -10))
    floor.static_mesh_component.set_static_mesh(cube)
    floor.static_mesh_component.set_collision_profile_name('BlockAll')
    floor.set_actor_scale3d(unreal.Vector(120, 100, .2))
    floor.tags = ['WarehouseMapStructure', 'LayoutFixed', 'ObserverArea']
    for name, point in [('ConveyorSource', (0, 0, 0)), ('ConveyorReceiver', (1300, 0, 0)), ('ConveyorOccupied', (-1800, 0, 0))]:
        conveyor = spawn(unreal.WarehouseConveyor, name, point)
        conveyor.set_editor_property('enabled', False)
    box = spawn(unreal.WarehouseCargo, 'ConveyorOccupiedParcel', (-1800, 0, 60))
    box.set_cargo_mesh(cube)
    box.set_actor_scale3d(unreal.Vector(.4, .4, .4))
    box.set_gross_mass_kg(12)
    # Author two collinear inclined machines and their independent parcel before
    # PIE. The subsequent transfer is driven solely by native roller contact.
    line_a = spawn(unreal.WarehouseConveyor, 'NativeAngledSource', (-3500, 1800, 100))
    line_a.set_editor_property('kind', unreal.WarehouseConveyorKind.CARTON_BELT)
    assert line_a.configure_geometry(300, 100, 40, 20)
    assert line_a.set_conveyor_pose(30, 5, 100)
    line_b = spawn(unreal.WarehouseConveyor, 'NativeAngledReceiver', (-2500, 1800, 100))
    line_b.set_editor_property('kind', unreal.WarehouseConveyorKind.CARTON_BELT)
    assert line_b.configure_geometry(300, 100, 40, 20)
    assert line_b.set_conveyor_pose(30, 5, 100)
    assert line_b.snap_entrance_to(line_a), line_b.get_editor_property('status')
    clearance = line_b.get_entrance_position()-line_a.get_exit_position()
    assert abs(clearance.length()-3) < .1 and (clearance-line_a.get_actor_forward_vector()*3).length() < .1
    point = line_a.get_actor_transform().transform_location(unreal.Vector(-90, 0, 60.5))
    moving_box = spawn(unreal.WarehouseCargo, 'NativeAngledParcel', (point.x, point.y, point.z))
    moving_box.set_cargo_mesh(cube)
    moving_box.set_actor_scale3d(unreal.Vector(.4, .35, .4))
    moving_box.set_actor_rotation(line_a.get_actor_rotation(), True)
    moving_box.set_gross_mass_kg(12)
    blocker = spawn(unreal.StaticMeshActor, 'ConveyorConfigurationBlocker', (500, 0, 75))
    blocker.static_mesh_component.set_static_mesh(cube)
    blocker.static_mesh_component.set_collision_profile_name('BlockAll')
    blocker.set_actor_scale3d(unreal.Vector(1, 1.8, 1.5))
    blocker.tags = ['LayoutType:Other']
    lift = spawn(unreal.WarehousePalletLift, 'LayoutLinkLift', (3000, -1500, 0))
    lift.set_editor_property('floor_count', 4)
    for name, point in [('LayoutLiftSender', (2400, -2500, 0)), ('LayoutLiftReceiver', (2400, -2000, 1200)),
                        ('LayoutDuplicatePort', (2300, -2700, 0))]:
        port = spawn(unreal.WarehouseConveyor, name, point)
        port.set_editor_property('enabled', False)
    upper = spawn(unreal.StaticMeshActor, 'LayoutLiftFourthFloorPad', (3000, -1500, 1190))
    upper.static_mesh_component.set_static_mesh(cube)
    upper.static_mesh_component.set_collision_profile_name('BlockAll')
    upper.set_actor_scale3d(unreal.Vector(20, 20, .2))
    upper.tags = ['WarehouseMapStructure', 'LayoutFixed', 'WarehouseFloor3']
    foot_blocker = spawn(unreal.StaticMeshActor, 'LayoutLiftPortFootBlocker', (2916.5, -1565.5, 2.5))
    foot_blocker.static_mesh_component.set_static_mesh(cube)
    foot_blocker.static_mesh_component.set_collision_profile_name('BlockAll')
    foot_blocker.set_actor_scale3d(unreal.Vector(.2, .2, .05))
    foot_blocker.tags = ['LayoutFixed', 'WarehouseMapStructure']
    spawn(unreal.PlayerStart, 'ConveyorSafePlayerStart', (-3500, -2500, 100))
    spawn(unreal.load_class(None, '/Script/msc_vr.WarehouseSimulationClock'), 'ConveyorLayoutClock', (0, -3000, 0))


def select(*conveyors):
    layout.set_selection(list(conveyors))
    assert layout.get_selection() == list(conveyors), 'The public selection path did not retain the requested conveyors'


def configure(actor, yaw, incline, height, length=300, width=125, speed=20):
    select(actor)
    return layout.configure_selected_conveyor(yaw, incline, height, length, width, speed)


def check_lift_link_controls():
    named = {actor.get_actor_label(): actor for actor in allof(unreal.Actor)}
    lift, sender, receiver, duplicate = [named[name] for name in
                                        ('LayoutLinkLift', 'LayoutLiftSender', 'LayoutLiftReceiver', 'LayoutDuplicatePort')]
    assert not lift.get_editor_property('powered_conveyor')
    select(sender)
    previous = geometry(sender)
    assert not layout.link_selected_conveyor_to_nearest_lift(True, 3), 'A real floor-level obstruction at the port foot was ignored'
    assert geometry(sender) == previous and not lift.get_editor_property('powered_conveyor')
    assert abs(lift.get_editor_property('platform').get_mass()-120) < .1
    assert not [part for part in lift.get_components_by_class(unreal.StaticMeshComponent)
                if part.get_name().startswith('PoweredCarrierRoller_') and part.is_simulating_physics()]
    check('blocked_lift_port_restores_original_line_and_manual_120kg_carrier', reason=layout.get_editor_property('status'))
    named['LayoutLiftPortFootBlocker'].destroy_actor()
    assert layout.link_selected_conveyor_to_nearest_lift(True, 3), layout.get_editor_property('status')
    assert sender.get_editor_property('lift') == lift and sender.get_editor_property('lift_floor') == 0
    assert sender.get_editor_property('destination_floor') == 3 and sender.get_editor_property('lift_infeed')
    assert lift.get_editor_property('powered_conveyor')
    rollers = [part for part in lift.get_components_by_class(unreal.StaticMeshComponent)
               if part.get_name().startswith('PoweredCarrierRoller_') and part.is_simulating_physics()]
    assert len(rollers) == 13, ('Powered carrier did not create thirteen real rollers', len(rollers))
    total_mass = lift.get_editor_property('platform').get_mass()+sum(part.get_mass() for part in rollers)
    assert abs(total_mass-120) < .1 and all(part.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS for part in rollers)
    port = lift.get_actor_transform().inverse_transform_location(sender.get_exit_position())
    assert abs(port.x+65.5) < .1 and abs(port.y) < .1 and abs(port.z-40) < .1
    check('native_nearest_lift_input_link_upgrades_existing_empty_carrier_to_thirteen_rollers_120kg', carrier_mass_kg=total_mass)
    before = geometry(sender)
    assert not layout.link_selected_conveyor_to_lift(lift, True, 0), 'A same-floor lift destination was accepted'
    assert geometry(sender) == before and lift.get_editor_property('powered_conveyor')
    select(duplicate)
    duplicate_before = geometry(duplicate)
    assert not layout.link_selected_conveyor_to_lift(lift, True, 3), 'Two conveyors occupied the same floor port'
    assert '이미' in layout.get_editor_property('status') and geometry(duplicate) == duplicate_before
    select(sender)
    assert lift.reserve_conveyor(sender)
    assert not layout.link_selected_conveyor_to_lift(lift, True, 1), 'An actively reserved lift was relinked'
    assert not lift.set_powered_conveyor(False), 'An actively reserved lift changed its drive rig'
    assert geometry(sender) == before and lift.get_editor_property('powered_conveyor')
    lift.release_conveyor(sender)
    check('invalid_destination_duplicate_port_and_busy_lift_reject_without_pose_link_or_power_mode_changes')
    pc.set_observer_floor(3)
    select(receiver)
    assert layout.link_selected_conveyor_to_lift(lift, False, 0), layout.get_editor_property('status')
    assert receiver.get_editor_property('lift') == lift and receiver.get_editor_property('lift_floor') == 3
    assert not receiver.get_editor_property('lift_infeed') and receiver.get_editor_property('destination_floor') == 0
    port = lift.get_actor_transform().inverse_transform_location(receiver.get_exit_position())
    assert abs(port.x+65.5) < .1 and abs(port.y) < .1 and abs(port.z-1240) < .1
    check('native_fourth_floor_receiver_links_to_same_lift_with_correct_port_height_and_direction')
    pc.set_observer_floor(0)
    select(sender)
    position = pose(sender)
    assert layout.disconnect_selected_conveyor_lift()
    assert not sender.get_editor_property('lift') and pose(sender) == position and lift.get_editor_property('powered_conveyor')
    assert layout.link_selected_conveyor_to_lift(lift, True, 3), layout.get_editor_property('status')
    select(occupied)
    before_loaded = geometry(occupied)
    assert not layout.link_selected_conveyor_to_lift(lift, True, 1)
    assert geometry(occupied) == before_loaded
    check('idle_link_disconnect_relink_preserves_geometry_and_occupied_conveyor_link_is_rejected')


def _tick_impl(delta_seconds):
    global game, pc, layout, source, receiver, occupied, parcel, phase
    global native_source, native_receiver, native_box, native_at, last_sample, stable_at, seen_source, seen_receiver
    try:
        assert time.monotonic()-started < 180, ('Conveyor layout test timed out', phase)
        if phase == 'start':
            fixture()
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if phase == 'settle':
            if now < 1.5:
                return
            named = {actor.get_actor_label(): actor for actor in allof(unreal.Actor)}
            source, receiver, occupied, parcel = [named[name] for name in
                                                  ('ConveyorSource', 'ConveyorReceiver', 'ConveyorOccupied', 'ConveyorOccupiedParcel')]
            native_source, native_receiver, native_box = [named[name] for name in
                                                          ('NativeAngledSource', 'NativeAngledReceiver', 'NativeAngledParcel')]
            if parcel not in occupied.get_physical_loads() and now < 5:
                return
            assert parcel in occupied.get_physical_loads(), 'Actual parcel never settled onto the occupied conveyor rollers'
            link_lift = named['LayoutLinkLift']
            if not link_lift.is_idle_for_layout() and now < 8:
                return
            assert link_lift.is_idle_for_layout(), link_lift.get_editor_property('status')
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            assert pc and hasattr(pc, 'configure_layout_conveyor')
            pc.set_observer_floor(0)
            layout = allof(unreal.WarehouseLayoutEditor)[0]
            assert pc.is_observer_view() and pc.is_layout_editing() and layout.get_editor_property('editing')
            assert unreal.GameplayStatics.is_game_paused(game), 'Default observer entry did not enable paused layout editing'
            check('default_observer_entry_opens_paused_layout_editing', floor=pc.get_editor_property('observer_floor'))
            pc.set_layout_editing(True)
            layout.set_category(unreal.WarehouseLayoutCategory.CONVEYOR)
            assert unreal.GameplayStatics.is_game_paused(game)
            for conveyor in (source, receiver, occupied):
                assert conveyor.actor_has_tag('WarehouseMapStructure')
                assert_rig(conveyor)
            check('production_controller_and_three_native_conveyor_rigs_with_real_settled_parcel')
            seen_source = native_box in native_source.get_physical_loads()
            assert seen_source, 'Inclined native parcel did not settle on the first conveyor'
            phase = 'checks'
            return
        if phase == 'checks':
            assert configure(source, 90, 10, 100), layout.get_editor_property('status')
            assert abs(source.get_actor_rotation().yaw-90) < .01 and abs(source.get_actor_rotation().pitch-10) < .01
            assert abs(source.get_actor_location().z-100) < .01
            assert configure(receiver, 90, 10, 100), layout.get_editor_property('status')
            select(source, receiver)
            assert layout.can_connect_selected_conveyors()
            assert layout.connect_selected_conveyors(), layout.get_editor_property('status')
            clearance = receiver.get_entrance_position()-source.get_exit_position()
            gap = clearance.length()
            assert abs(gap-3) < .1 and (clearance-source.get_actor_forward_vector()*3).length() < .1 and source.get_editor_property('next_conveyor') == receiver, ('3D conveyor ports lost their physical transfer clearance', gap)
            assert_rig(source)
            assert_rig(receiver)
            check('yaw_ninety_pitch_ten_and_real_three_dimensional_port_snap', port_gap_cm=gap, receiver_pose=pose(receiver))
            # Move the second rig away through the regular layout transaction,
            # then prove incompatible angle rejection without body overlap.
            select(receiver)
            point = receiver.get_actor_location()
            assert layout.begin_drag(receiver, point), layout.get_editor_property('status')
            layout.update_drag(point + unreal.Vector(1500, 1500, 0))
            assert layout.end_drag(), layout.get_editor_property('status')
            for yaw, incline, name in [(100, 10, 'ten_degree_heading_mismatch'), (90, 0, 'ten_degree_incline_mismatch')]:
                assert configure(receiver, yaw, incline, 100), layout.get_editor_property('status')
                select(source, receiver)
                before_source, before_receiver = geometry(source), geometry(receiver)
                assert not layout.connect_selected_conveyors(), ('A noncollinear direct joint connected', name)
                reason = layout.get_editor_property('status')
                assert ('1도' in reason or '1°' in reason) and '직결' in reason, reason
                assert geometry(source) == before_source and geometry(receiver) == before_receiver
                check(name+'_rejected_without_pose_geometry_or_link_changes', reason=reason)
            assert configure(source, 37, -15, 250, 450, 90, 35), layout.get_editor_property('status')
            assert abs(source.get_actor_rotation().yaw-37) < .01 and abs(source.get_actor_rotation().pitch+15) < .01
            assert source.get_editor_property('length_cm') == 450 and source.get_editor_property('width_cm') == 90
            assert source.get_editor_property('speed_cm') == 35
            check('arbitrary_yaw_and_negative_incline_size_speed_configuration')
            select(source)
            before = geometry(source)
            assert not layout.configure_selected_conveyor(0, 0, 0, 1200, 125, 20), 'Configuration overlapped a real solid blocker'
            reason = layout.get_editor_property('status')
            assert ('겹침' in reason or '화물이 있거나 승강기를 사용 중이라 규격을 바꿀 수 없습니다' in reason or
                    '화물·장애물 또는 승강기 인계가 있어 규격을 바꿀 수 없습니다' in reason), reason
            assert geometry(source) == before, 'Collision rollback changed conveyor settings, pose or link'
            assert_rig(source)
            check('real_solid_blocker_preflight_or_shape_query_rejects_and_restores_geometry_pose_and_link', reason=reason)
            for values in [(37, 21, 250, 450, 90, 35), (37, -21, 250, 450, 90, 35),
                           (37, 0, -1, 450, 90, 35), (37, 0, 2001, 450, 90, 35),
                           (37, 0, 250, 124, 90, 35), (37, 0, 250, 450, 151, 35)]:
                assert not layout.configure_selected_conveyor(*values), ('Invalid configuration accepted', values)
                assert geometry(source) == before
            check('incline_elevation_length_width_limits_reject_without_configuration_loss')
            select(occupied)
            loaded_pose = pose(parcel)
            loaded_geometry = geometry(occupied)
            assert not layout.configure_selected_conveyor(45, 10, 100, 125, 80, 30)
            assert geometry(occupied) == loaded_geometry and pose(parcel) == loaded_pose
            assert not layout.begin_drag(occupied, occupied.get_actor_location())
            assert not layout.delete_selection()
            assert parcel in occupied.get_physical_loads() and parcel.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
            assert not parcel.get_attach_parent_actor()
            check('actual_loaded_conveyor_resize_move_and_delete_are_blocked_preserving_independent_cargo')
            check_lift_link_controls()
            report['native_geometry'] = dict(source_pose_cm_degrees=pose(native_source), receiver_pose_cm_degrees=pose(native_receiver),
                source_exit_cm=[native_source.get_exit_position().x, native_source.get_exit_position().y, native_source.get_exit_position().z],
                receiver_entrance_cm=[native_receiver.get_entrance_position().x, native_receiver.get_entrance_position().y, native_receiver.get_entrance_position().z])
            pc.toggle_layout_editing()
            assert not unreal.GameplayStatics.is_game_paused(game)
            native_at = last_sample = now
            phase = 'native_transport'
            return
        if phase == 'native_transport':
            assert now-native_at < 90, ('Collinear inclined parcel transfer timed out', native_source.get_editor_property('status'),
                                        native_receiver.get_editor_property('status'), native_box.get_actor_location())
            body = native_box.get_component_by_class(unreal.StaticMeshComponent)
            assert body.is_simulating_physics() and body.is_gravity_enabled() and not native_box.get_attach_parent_actor()
            assert abs(body.get_mass()-12) < .1
            assert native_box.get_actor_location().z > 60, ('The parcel fell from the elevated conveyor',
                pose(native_box), native_source.get_editor_property('status'), native_receiver.get_editor_property('status'))
            seen_source = seen_source or native_box in native_source.get_physical_loads()
            seen_receiver = seen_receiver or native_box in native_receiver.get_physical_loads()
            if now-last_sample >= 2:
                point = native_box.get_actor_location()
                source_local = native_source.get_actor_transform().inverse_transform_location(point)
                receiver_local = native_receiver.get_actor_transform().inverse_transform_location(point)
                velocity = body.get_physics_linear_velocity()
                report['samples'].append(dict(sim_seconds=round(now-native_at, 2), position_cm=[point.x, point.y, point.z],
                                              source_status=native_source.get_editor_property('status'),
                                              receiver_status=native_receiver.get_editor_property('status'),
                                              source_local_cm=[source_local.x, source_local.y, source_local.z],
                                              receiver_local_cm=[receiver_local.x, receiver_local.y, receiver_local.z],
                                              box_rotation_degrees=[native_box.get_actor_rotation().pitch, native_box.get_actor_rotation().yaw, native_box.get_actor_rotation().roll],
                                              velocity_cm_s=[velocity.x, velocity.y, velocity.z],
                                              source_bearing=native_box in native_source.get_physical_loads(),
                                              receiver_bearing=native_box in native_receiver.get_physical_loads(),
                                              speed_cm_s=velocity.length()))
                last_sample = now
            local = native_receiver.get_actor_transform().inverse_transform_location(native_box.get_actor_location())
            ready = (seen_source and seen_receiver and local.x > 85 and abs(local.y) < 30 and
                     native_receiver.get_editor_property('state') == unreal.WarehouseConveyorState.PICKUP_READY and
                     body.get_physics_linear_velocity().length() < 5)
            if ready:
                if stable_at is None:
                    stable_at = now
                if now-stable_at >= 1:
                    check('native_independent_parcel_crosses_two_collinear_five_degree_inclines_and_stops_at_receiver',
                          sim_seconds=round(now-native_at, 2), final_local_cm=[local.x, local.y, local.z],
                          final_speed_cm_s=body.get_physics_linear_velocity().length())
                    finish(True, 'Native configure/free yaw/incline, collinear 3D ports, real five-degree inclined parcel transfer and endpoint stop; noncollinear joint/collision/range rejection with full rollback and occupied machine protection; no map saved')
            else:
                stable_at = None
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
