"""Saved truck drivers plus a new/replacement truck: pose, layout and departure.

Only the isolated truck fixture is rearranged/departed. Original warehouse actors
and cargo are never saved, teleported, attached or changed to pass this check.
"""
import json
import time
import traceback
from pathlib import Path
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
out = Path(unreal.Paths.project_dir()).resolve() / 'Saved/Testing/TruckDriver'
out.mkdir(parents=True, exist_ok=True)
assert not editor.get_game_world()
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
named = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
source = named['WH_LogisticsTruck_01']
chassis = named['EXT_ParkedTruck_01']
chassis_pose = unreal.MathLibrary.make_relative_transform(chassis.get_actor_transform(), source.get_actor_transform())
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.WarehouseForklift):
        actor.set_editor_property('auto_start', False)
        actor.set_editor_property('pending_jobs', [])
        actor.set_editor_property('idle_parking_enabled', False)

pad = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(20000, 0, -10))
pad.set_actor_label('DriverFixturePad')
pad.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
pad.static_mesh_component.set_collision_profile_name('BlockAll')
pad.set_actor_scale3d(unreal.Vector(70, 70, .2))
pad.tags = ['WarehouseMapStructure', 'LayoutFixed']


def fixture(label, position):
    truck = actors.spawn_actor_from_class(unreal.WarehouseLogisticsTruck, position)
    truck.set_actor_label(label)
    truck.tags = ['LayoutType:Other', 'LayoutGroup:' + label, 'WarehouseFloor0']
    body = actors.spawn_actor_from_class(unreal.StaticMeshActor, position)
    body.set_actor_label(label + 'Chassis')
    body.static_mesh_component.set_static_mesh(chassis.static_mesh_component.static_mesh)
    body.static_mesh_component.set_editor_property('override_materials', list(chassis.static_mesh_component.get_editor_property('override_materials')))
    body.static_mesh_component.set_collision_profile_name(chassis.static_mesh_component.get_collision_profile_name())
    body.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    body.set_actor_transform(unreal.MathLibrary.compose_transforms(chassis_pose, truck.get_actor_transform()), False, True)
    body.tags = list(truck.tags)
    dock = actors.spawn_actor_from_class(unreal.TargetPoint, position)
    dock.tags = ['LayoutFixed', 'LogisticsFixedDock']
    for key, value in dict(truck_id=label, dock_id=label + '-DOCK', truck_parts=[body], dock_anchor=dock,
                           departure_route=[position + unreal.Vector(500, 0, 0), position + unreal.Vector(1000, 0, 0)]).items():
        truck.set_editor_property(key, value)
    truck.update_driver_presentation()
    return truck


fixture('DriverFixtureNewTruck', unreal.Vector(20000, 0, 0))
fixture('DriverFixtureReplacement', unreal.Vector(20000, 1700, 0))
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector())
camera.set_actor_label('TruckDriverCamera')
camera.camera_component.set_field_of_view(55)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
report = dict(date='2026-10-27', status='starting', tests=[], screenshots=[], os_input_tested=False,
              scope='Actual saved truck models; new/replacement native actors; isolated public layout/departure API; no map save')
started = time.monotonic()
phase = 'start'
phase_at = 0
busy = finished = False
game = pc = layout = truck = replacement = camera = None
original_pose = original_driver = None
cargo_snapshot = {}
capture = '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower()
image = None
photo = 0


def enter(name, now):
    global phase, phase_at
    phase, phase_at = name, now


def finish(error=None):
    global finished
    if finished:
        return
    finished = True
    report.update(status='failed' if error else 'passed', wall_seconds=time.monotonic() - started)
    if error:
        report['error'] = error
    (out / 'DriverReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('TRUCK_DRIVER_PIE_' + ('FAILED' if error else 'PASSED'), error or report['tests'])
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def driver_check(vehicle):
    visual = vehicle.get_driver_mesh()
    assert visual and vehicle.get_editor_property('driver_pose_ready'), ('Seated pose unavailable', vehicle.get_actor_label())
    assert visual.get_skinned_asset() == unreal.GameplayStatics.get_player_character(game, 0).get_component_by_class(unreal.SkeletalMeshComponent).get_skinned_asset()
    assert visual.get_attach_parent() == vehicle.root_component
    assert not unreal.WarehouseDamageSystem.is_physical_contact(visual)
    collision = dict(mode=str(visual.get_collision_enabled()),
                     overlaps=visual.get_editor_property('generate_overlap_events'),
                     profile=str(visual.get_collision_profile_name()))
    assert visual.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION and not collision['overlaps'], collision
    channels = (unreal.CollisionChannel.ECC_VISIBILITY, unreal.CollisionChannel.ECC_PAWN,
                unreal.CollisionChannel.ECC_VEHICLE, unreal.CollisionChannel.ECC_PHYSICS_BODY,
                unreal.CollisionChannel.ECC_WORLD_STATIC, unreal.CollisionChannel.ECC_WORLD_DYNAMIC)
    assert all(visual.get_collision_response_to_channel(c) == unreal.CollisionResponseType.ECR_IGNORE for c in channels), collision
    seat = unreal.MathLibrary.compose_transforms(vehicle.get_editor_property('driver_seat_pose'), vehicle.get_actor_transform())
    pelvis = visual.get_bone_location_by_name('pelvis', unreal.BoneSpaces.WORLD_SPACE)
    assert (pelvis - seat.translation).length() < 1, ('Pelvis missed seat', pelvis, seat.translation)
    for suffix in ('_l', '_r'):
        hip = seat.inverse_transform_location(visual.get_bone_location_by_name('thigh' + suffix, unreal.BoneSpaces.WORLD_SPACE))
        knee = seat.inverse_transform_location(visual.get_bone_location_by_name('calf' + suffix, unreal.BoneSpaces.WORLD_SPACE))
        foot = seat.inverse_transform_location(visual.get_bone_location_by_name('foot' + suffix, unreal.BoneSpaces.WORLD_SPACE))
        assert knee.x > hip.x + 20 and foot.z < knee.z - 15, ('Leg is not seated', hip, knee, foot)
    return unreal.MathLibrary.make_relative_transform(visual.get_world_transform(), vehicle.get_actor_transform())


def tick(dt):
    global busy, game, pc, layout, truck, replacement, camera, original_pose, original_driver, image, photo
    if busy or finished:
        return
    busy = True
    try:
        assert time.monotonic() - started < 600, ('Truck driver timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            enter('settle', 0)
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if pc is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            layout = live['WH_LayoutEditor']
            truck, replacement, camera = (live[n] for n in ('DriverFixtureNewTruck', 'DriverFixtureReplacement', 'TruckDriverCamera'))
        if phase == 'settle':
            if now < 4:
                return
            trucks = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseLogisticsTruck)
            assert len(trucks) >= 5
            for vehicle in trucks:
                driver_check(vehicle)
                cargo_snapshot[vehicle.get_actor_label()] = (vehicle.payload_mass_kg(), [p.get_name() for p in vehicle.get_editor_property('manifest_pallets')])
            original_pose = truck.get_actor_transform()
            original_driver = driver_check(truck)
            report['tests'].append('saved three trucks and new/replacement instances share player mesh, seated legs and non-collision visual')
            layout.set_editing(True, 0)
            layout.set_category(unreal.WarehouseLayoutCategory.OTHER)
            assert layout.begin_drag(truck, truck.get_actor_location()), layout.get_editor_property('status')
            layout.update_drag(truck.get_actor_location() + unreal.Vector(400, 100, 0))
            assert layout.end_drag(), layout.get_editor_property('status')
            moved = driver_check(truck)
            assert (moved.translation - original_driver.translation).length() < .01
            layout.set_selection([truck])
            assert layout.rotate_selection(90), layout.get_editor_property('status')
            driver_check(truck)
            assert layout.rotate_selection(-90), layout.get_editor_property('status')
            assert layout.begin_drag(truck, truck.get_actor_location()), layout.get_editor_property('status')
            layout.update_drag(original_pose.translation)
            assert layout.end_drag(), layout.get_editor_property('status')
            layout.set_editing(False, 0)
            report['tests'].append('actual group layout translation/rotation/restoration carries driver without extra collision')
            enter('ready', now)
        elif phase == 'ready':
            if now - phase_at < .5:
                return
            assert truck.is_ready()
            truck.set_editor_property('show_driver', False)
            truck.update_driver_presentation()
            assert truck.get_driver_mesh().get_editor_property('hidden_in_game')
            truck.set_editor_property('show_driver', True)
            truck.update_driver_presentation()
            driver_check(truck)
            report['tests'].append('driver visibility setting does not change truck manifest or cargo mass')
            enter('photo_setup' if capture else 'depart', now)
        elif phase == 'photo_setup':
            saved_truck = next(t for t in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseLogisticsTruck) if t.get_actor_label() == 'WH_LogisticsTruck_01')
            seat = saved_truck.get_editor_property('driver_seat_pose').translation
            frame = saved_truck.get_actor_transform()
            offset = unreal.Vector(200, -230, 70) if photo == 0 else unreal.Vector(85, -5, 55)
            eye = frame.transform_location(seat + offset)
            target = frame.transform_location(seat + unreal.Vector(5, 0, 50))
            camera.set_actor_location(eye, False, True)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, target), False)
            pc.set_view_target_with_blend(camera, 0)
            enter('photo_capture', now)
        elif phase == 'photo_capture':
            if now - phase_at < .6:
                return
            name = 'DriverExterior.png' if photo == 0 else 'DriverInterior.png'
            image = out / name
            image.unlink(missing_ok=True)
            enter('photo_wait', now)
            unreal.SystemLibrary.execute_console_command(game, 'HighResShot 1440x1000 filename=' + image.as_posix(), pc)
        elif phase == 'photo_wait':
            if not image.is_file() or image.stat().st_size < 1000:
                assert now - phase_at < 15, 'Driver screenshot writer did not finish'
                return
            data = image.read_bytes()
            assert data[-8:-4] == b'IEND'
            report['screenshots'].append(str(image))
            photo += 1
            enter('photo_setup' if photo < 2 else 'depart', now)
        elif phase == 'depart':
            for vehicle in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseLogisticsTruck):
                assert cargo_snapshot[vehicle.get_actor_label()] == (vehicle.payload_mass_kg(), [p.get_name() for p in vehicle.get_editor_property('manifest_pallets')])
            assert truck.depart(), truck.get_editor_property('status')
            enter('departing', now)
        elif phase == 'departing':
            driver_check(truck)
            assert replacement.get_driver_mesh().is_visible() and not replacement.get_driver_mesh().get_editor_property('hidden_in_game')
            if truck.get_editor_property('state') != unreal.WarehouseTruckState.DEPARTED:
                assert not truck.get_driver_mesh().get_editor_property('hidden_in_game')
                return
            assert truck.get_driver_mesh().get_editor_property('hidden_in_game')
            report['tests'].append('cinematic departure carries driver and hides only departed driver; replacement remains visible')
            finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        busy = False


handle = unreal.register_slate_post_tick_callback(tick)
