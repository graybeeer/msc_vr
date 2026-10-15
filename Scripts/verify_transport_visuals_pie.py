"""Rendered native job/route visualization regression. Temporary Entry fixture; never saves a map.

Uses actual SubmitJob/cancel/route planner/remote APIs. Completion/deletion are
isolated lifecycle fixtures, not a claim that a full loaded transport was replayed.
"""
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
template = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'WH_AutonomousForklift')
rig = {c.get_name(): (c.get_relative_transform(), c.static_mesh, list(c.get_editor_property('override_materials')))
       for c in template.get_components_by_class(unreal.StaticMeshComponent)}
assert level.load_level('/Engine/Maps/Entry')
for a in list(actors.get_all_level_actors()):
    if not isinstance(a, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(a)
world = editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property(
    'default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))


def spawn(cls, label, p, rotation=unreal.Rotator()):
    a = actors.spawn_actor_from_class(cls, unreal.Vector(*p), rotation)
    a.set_actor_label(label)
    a.set_editor_property('is_spatially_loaded', False)
    return a


floor = spawn(unreal.StaticMeshActor, 'VisualTestFloor', (400, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.set_actor_scale3d(unreal.Vector(40, 40, .2))
spawn(unreal.PlayerStart, 'VisualTestStart', (-1100, -1000, 96.5))
sun = spawn(unreal.DirectionalLight, 'VisualTestSun', (0, 0, 1400), unreal.Rotator(pitch=-55, yaw=-30))
sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(3)
sky = spawn(unreal.SkyLight, 'VisualTestSky', (0, 0, 1500))
sky.get_component_by_class(unreal.SkyLightComponent).set_intensity(.5)
camera = spawn(unreal.CameraActor, 'VisualTestCamera', (200, -1800, 1300))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(550, 0, 30)), False)
camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(55)
console = spawn(unreal.WarehouseComputer, 'VisualTestConsole', (-1100, 1100, 0))
specs = []
for i, y in enumerate((-330, 330, 1100)):
    vehicle = spawn(unreal.WarehouseForklift, 'VisualTestAGV%d' % i, (0, y, 0))
    vehicle.set_editor_property('vehicle_name', 'Vehicle %d' % (i + 1))
    vehicle.set_editor_property('auto_start', False)
    vehicle.set_editor_property('pending_jobs', [])
    vehicle.set_editor_property('idle_parking_enabled', False)
    for c in vehicle.get_components_by_class(unreal.StaticMeshComponent):
        transform, mesh, materials = rig[c.get_name()]
        c.set_relative_transform(transform, False, True)
        c.set_static_mesh(mesh)
        c.set_editor_property('override_materials', materials)
    pallet = spawn(unreal.WarehousePallet, 'VisualTestPallet%d' % i, (900, y, .1))
    cargo = spawn(unreal.WarehouseCargo, 'VisualTestCargo%d' % i, (900, y, 35.2))
    cargo.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2'))
    assert cargo.apply_cargo_recipe(unreal.WarehouseCargo.generate_cargo_recipe(20261015, i, 0, unreal.Vector(60, 50, 40), 1))
    fit(cargo, (60, 50, 40), (900, y, 35.2))
    specs += [unreal.WarehouseStrength(name=pallet.get_actor_label(), members=[pallet], mass_kg=25, rated_load_kg=1500),
              unreal.WarehouseStrength(name=cargo.get_actor_label(), members=[cargo], mass_kg=cargo.get_editor_property('gross_mass_kg'), rated_load_kg=150)]
damage = spawn(unreal.WarehouseDamageSystem, 'VisualTestStrength', (-1200, 1000, 0))
damage.set_editor_property('objects', specs)
out = Path(unreal.Paths.project_saved_dir()) / 'Testing/TransportVisuals'
out.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
phase = 'start'
at = started
live = None
baseline = {}
routes = []


def enter(value):
    global phase, at
    phase, at = value, time.monotonic()
    print('TRANSPORT_VISUAL_PHASE', phase)


def body(a):
    return a.get_component_by_class(unreal.StaticMeshComponent)


def physical(part):
    return (part.get_collision_enabled(), part.get_mass(), part.is_simulating_physics(),
            part.is_gravity_enabled(), part.static_mesh, tuple(part.get_material(i) for i in range(part.get_num_materials())))


def arrows(pc):
    display = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(pc, unreal.Actor) if a.actor_has_tag('TransportDisplay'))
    return sorted([c for c in display.get_components_by_class(unreal.InstancedStaticMeshComponent)
                   if c.component_has_tag('TransportRouteVisual')], key=lambda c: c.get_editor_property('custom_depth_stencil_value'))


def job(i):
    return unreal.WarehouseWorkOrder(job_id='VISUAL-%d' % i, source_system='FMS-COMPUTER', pallet=live['VisualTestPallet%d' % i],
                                    destination=unreal.Transform(location=unreal.Vector(1400, -330 + i * 660, 0)))


def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    global live, routes
    try:
        assert time.monotonic() - started < 240, ('Visualization timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            enter('settle')
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        age = time.monotonic() - at
        if live is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
        pc = unreal.GameplayStatics.get_player_controller(game, 0)
        vehicles = [live['VisualTestAGV%d' % i] for i in range(3)]
        pallets = [live['VisualTestPallet%d' % i] for i in range(3)]
        cargo = [live['VisualTestCargo%d' % i] for i in range(3)]
        console = live['VisualTestConsole']
        if phase == 'settle':
            if now < 3:
                return
            pc.set_view_target_with_blend(live['VisualTestCamera'])
            for i in range(3):
                assert cargo[i] in live['VisualTestStrength'].get_supported_actors(pallets[i]), ('Cargo not resting on pallet', i)
                for a in (pallets[i], cargo[i]):
                    baseline[a.get_actor_label()] = physical(body(a))
            body(pallets[0]).set_render_custom_depth(True)
            body(pallets[0]).set_custom_depth_stencil_value(7)
            for i in range(3):
                assert vehicles[i].submit_job(job(i))
            enter('assigned')
        elif phase == 'assigned':
            if age < .7:
                return
            for i in range(3):
                for a in (pallets[i], cargo[i]):
                    part = body(a)
                    assert part.get_editor_property('render_custom_depth') and part.get_editor_property('custom_depth_stencil_value') == 192 + i
                    assert physical(part) == baseline[a.get_actor_label()], 'Display changed physical state/material'
            assert not body(live['VisualTestFloor']).get_editor_property('render_custom_depth')
            for i in range(3):
                assert console.cancel_queued_job('VISUAL-%d' % i)
            enter('cancelled')
        elif phase == 'cancelled':
            if age < .7:
                return
            assert body(pallets[0]).get_editor_property('render_custom_depth')
            assert body(pallets[0]).get_editor_property('custom_depth_stencil_value') == 7, 'Prior depth state was lost'
            for i in range(3):
                assert not body(cargo[i]).get_editor_property('render_custom_depth')
                assert vehicles[i].submit_job(job(i))
                vehicles[i].toggle_power()
            enter('navigate')
        elif phase == 'navigate':
            if not all(len(v.get_display_route()) > 1 for v in vehicles):
                assert age < 25, [(str(v.get_editor_property('ai_state')), str(v.get_editor_property('status'))) for v in vehicles]
                return
            for v in vehicles:
                v.set_actor_tick_enabled(False)
            routes = [v.get_display_route() for v in vehicles]
            enter('render')
        elif phase == 'render':
            if age < 1:
                return
            mesh = arrows(pc)
            assert len(mesh) == 3 and all(m.get_instance_count() >= 2 for m in mesh)
            for i, part in enumerate(mesh):
                assert part.get_editor_property('custom_depth_stencil_value') == 128 + i
                assert part.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
                assert not unreal.WarehouseDamageSystem.is_physical_contact(part)
            unreal.SystemLibrary.execute_console_command(game, 'HighResShot 1600x900 filename=' + (out / 'RoutesAndTargets.png').as_posix(), pc)
            for i, v in enumerate(vehicles):
                assert v.plan_autonomous_route(unreal.Transform(location=unreal.Vector(600, (-330 + i * 660) - 150, v.get_actor_location().z)))
                assert v.get_display_route() != routes[i], 'Planner did not change route'
            enter('replanned')
        elif phase == 'replanned':
            if age < 1:
                return
            assert (out / 'RoutesAndTargets.png').is_file()
            unreal.SystemLibrary.execute_console_command(game, 'HighResShot 1600x900 filename=' + (out / 'ReplannedRoutes.png').as_posix(), pc)
            pc.get_controlled_pawn().set_actor_location(vehicles[0].get_actor_location() + unreal.Vector(0, -200, 96.5), False, True)
            assert vehicles[0].begin_remote_control(pc.get_controlled_pawn())
            assert not vehicles[0].get_display_route(), 'Remote mode exposed autonomous arrows'
            # Completion fixture keeps the old work order to exercise stale-state filtering.
            vehicles[1].get_editor_property('completed_job_ids').append('VISUAL-1')
            pallets[2].destroy_actor()
            enter('cleanup')
        elif phase == 'cleanup':
            if age < 1:
                return
            mesh = arrows(pc)
            assert mesh[0].get_instance_count() == 0
            for a in (pallets[1], cargo[1], cargo[2]):
                assert not body(a).get_editor_property('render_custom_depth'), 'Completed/deleted target remained outlined'
            # Clearing the remaining work restores the pre-existing stencil, including enabled=true.
            vehicles[1].get_editor_property('active_job').job_id = ''
            vehicles[1].get_editor_property('active_job').pallet = None
            vehicles[1].get_editor_property('completed_job_ids').clear()
            vehicles[0].end_remote_control(pc.get_controlled_pawn())
            vehicles[0].get_editor_property('active_job').job_id = ''
            vehicles[0].get_editor_property('active_job').pallet = None
            enter('restored')
        elif phase == 'restored':
            if age < .7:
                return
            assert body(pallets[0]).get_editor_property('custom_depth_stencil_value') == 7
            assert body(pallets[0]).get_editor_property('render_custom_depth')
            assert not body(cargo[0]).get_editor_property('render_custom_depth')
            assert (out / 'ReplannedRoutes.png').is_file()
            transfer = job(0)
            transfer.job_id = 'VISUAL-TRANSFER'
            assert vehicles[1].submit_job(transfer)
            enter('reassigned')
        elif phase == 'reassigned':
            if age < .7:
                return
            for a in (pallets[0], cargo[0]):
                assert body(a).get_editor_property('custom_depth_stencil_value') == 193, 'Reassigned cargo kept the previous owner color'
                assert physical(body(a)) == baseline[a.get_actor_label()]
            assert console.cancel_queued_job('VISUAL-TRANSFER')
            enter('transfer_cancelled')
        elif phase == 'transfer_cancelled':
            if age < .7:
                return
            assert body(pallets[0]).get_editor_property('custom_depth_stencil_value') == 7
            assert body(pallets[0]).get_editor_property('render_custom_depth')
            assert not body(cargo[0]).get_editor_property('render_custom_depth')
            print('TRANSPORT_VISUALS_PIE_PASSED: three native job owners; shared arrow/outline colors; real planning/replanning; queue cancellation/reassignment; remote suppression; completion/deletion fixtures; original depth/physics/material restoration; 1600x900 screenshots')
            finish()
    except Exception:
        unreal.log_error(traceback.format_exc())
        finish()


handle = unreal.register_slate_post_tick_callback(tick)
