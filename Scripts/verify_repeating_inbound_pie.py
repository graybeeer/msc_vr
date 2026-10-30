"""Bounded repeating inbound visits, dock service and inventory preservation.

Runs an unsaved empty fixture. Only fixture setup and the explicitly documented
receiving-slot cleanup move actors; no AGV/production transport state is forced.
"""
import json
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
prepared_visit_check = '-repeatingpreparedvisit' in unreal.SystemLibrary.get_command_line().lower()
visit_offset = int(prepared_visit_check)

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property(
    'default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))


def spawn(cls, label, point):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
    actor.set_actor_label(label)
    actor.set_editor_property('is_spatially_loaded', False)
    return actor


def cube(label, point, size):
    actor = spawn(unreal.StaticMeshActor, label, point)
    actor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    actor.set_actor_scale3d(unreal.Vector(*size) / 100)
    actor.static_mesh_component.set_collision_profile_name('BlockAll')
    return actor


cube('RepeatFloor', (600, 0, -10), (4000, 2400, 20)).tags = ['WarehouseMapStructure']
spawn(unreal.PlayerStart, 'RepeatStart', (-900, -900, 125))
spawn(unreal.WarehouseDamageSystem, 'RepeatStrength', (-900, 900, 0))
anchor = spawn(unreal.TargetPoint, 'RepeatInboundAnchor', (0, 0, 0))
truck = spawn(unreal.WarehouseLogisticsTruck, 'RepeatTruck', (1400, 0, 0))
truck.set_editor_property('truck_id', 'REPEAT-TRUCK')
truck.set_editor_property('dock_id', 'REPEAT-DOCK')
truck.set_editor_property('dock_anchor', spawn(unreal.TargetPoint, 'RepeatDock', (1400, 0, 0)))
truck.set_editor_property('truck_parts', [cube('RepeatTruckFloor', (1400, 0, -10), (160, 160, 20))])
truck.set_editor_property('load_slots', [unreal.Transform()])
truck.set_editor_property('load_slot_size_cm', unreal.Vector(160, 130, 210))
truck.set_editor_property('transfer_interval_seconds', .75)
truck.set_editor_property('departure_route', [unreal.Vector(2200, 0, 0)])
truck.set_editor_property('show_driver', False)
shipping = spawn(unreal.WarehouseShippingSystem, 'RepeatShipping', (-900, 700, 0))
shipping.set_editor_property('zones', [unreal.WarehouseShippingZone(zone_id='IN-REPEAT', inbound=True,
    anchor=anchor, half_size=unreal.Vector(170, 170, 80), slots=[unreal.Transform()])])
shipping.set_editor_property('trucks', [truck])
shipping.set_editor_property('repeating_inbound_truck', truck)
shipping.set_editor_property('scenario_seed', 20261029)
shipping.set_editor_property('visit_interval_seconds', 5)
shipping.set_editor_property('max_generated_units', 2)
spawn(unreal.WarehouseLayoutEditor, 'RepeatLayout', (-900, 500, 0))
sun = spawn(unreal.DirectionalLight, 'RepeatSun', (0, 0, 1500))
sun.set_actor_rotation(unreal.Rotator(pitch=-65, yaw=-30), False)
sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(4)
camera = spawn(unreal.CameraActor, 'RepeatCamera', (-500, -1500, 1400))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(650, 0, 0)), False)

out = Path(unreal.Paths.project_saved_dir()) / 'Testing/RepeatingInbound'
out.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
phase = 'start'
at = 0
last_log = -10
live = None
checks = []
records = {}
observed_visits = set()


def check(message):
    checks.append(message)
    print('REPEATING_INBOUND_CHECK_PASSED', message)


def body(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)


def recipe_values(recipe):
    return dict(seed=recipe.delivery_seed, index=recipe.item_index, product=recipe.product_index,
        kind=str(recipe.product_kind), mass=round(recipe.gross_mass_kg, 4),
        dimensions=[round(recipe.size_cm.x, 4), round(recipe.size_cm.y, 4), round(recipe.size_cm.z, 4)],
        density=round(recipe.packed_density_kg_m3, 4), fragile=recipe.fragile)


def finish(ok, message):
    report = {'passed': ok, 'checks': checks, 'message': message, 'wall_seconds': round(time.monotonic() - started, 2),
        'records': {str(index): {'cargo_id': record['id'], 'visit': record['visit'], 'recipe': record['recipe']}
                    for index, record in records.items()}}
    (out / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('REPEATING_INBOUND_PIE_' + ('PASSED' if ok else 'FAILED'), message)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def record_manifest(t):
    manifest = list(t.get_editor_property('manifest_pallets'))
    if not manifest or not t.is_manifest_ready():
        return
    assert len(manifest) == 1, 'Visit contains more than one generated pallet'
    pallet = manifest[0]
    cargo = list(t.get_unit_cargo(pallet))
    assert len(cargo) == 1, 'Generated carton was not captured in the native arriving manifest'
    box = cargo[0]
    recipe = box.get_editor_property('packing')
    index = recipe.item_index
    expected = unreal.WarehouseCargo.generate_cargo_recipe(20261029, index,
        index % unreal.WarehouseCargo.get_cargo_profile_count(), unreal.Vector(75, 65, 65), 1.)
    again = unreal.WarehouseCargo.generate_cargo_recipe(20261029, index,
        index % unreal.WarehouseCargo.get_cargo_profile_count(), unreal.Vector(75, 65, 65), 1.)
    assert recipe_values(recipe) == recipe_values(expected) == recipe_values(again), 'Generated recipe does not match its seed/index'
    assert pallet.get_editor_property('expected_carton_count') == 1
    assert t.is_unit_on_truck(pallet) and not body(box).is_simulating_physics()
    assert abs(t.payload_mass_kg() - pallet.get_editor_property('pallet_mass_kg') - recipe.gross_mass_kg) < .1
    if index not in records:
        records[index] = dict(pallet=pallet, box=box, id=str(box.get_editor_property('cargo_id')),
            visit=t.get_visit_id(), recipe=recipe_values(recipe))
        check('native arrival manifest %d contains one original carton with deterministic size/kind/mass' % index)


def clear_received_fixture_slot(record):
    # TEST FIXTURE ONLY: move the already-unloaded whole stack aside so this
    # narrow dock test needs no AGV. Production receiving never uses this path.
    for actor in (record['pallet'], record['box']):
        assert not actor.actor_has_tag('LogisticsInTransit') and body(actor).is_simulating_physics()
        actor.set_actor_location(actor.get_actor_location() + unreal.Vector(-650, 0, 0), False, True)
        body(actor).set_physics_linear_velocity(unreal.Vector())
        body(actor).set_physics_angular_velocity_in_degrees(unreal.Vector())


def tick(dt):
    global phase, live, at, last_log
    try:
        assert time.monotonic() - started < 240, ('Wall timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'off'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live = {actor.get_actor_label(): actor for actor in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            assert pc.set_simulation_speed(4)
            pc.set_view_target_with_blend(live['RepeatCamera'], 0)
        s, t = live['RepeatShipping'], live['RepeatTruck']
        assert t.get_editor_property('state') != unreal.WarehouseTruckState.UNAVAILABLE, t.get_editor_property('status')
        assert now < 150, ('Simulation timeout', phase, s.get_editor_property('operations_status'), t.get_editor_property('status'))
        record_manifest(t)
        observed_visits.add(t.get_visit_id())
        shipments = list(s.get_editor_property('shipments'))
        if now - last_log > 5:
            last_log = now
            print('REPEATING_INBOUND_PROGRESS', phase, round(now, 2), t.get_visit_id(),
                s.get_editor_property('generated_units'), s.get_editor_property('operations_status'),
                [(x.shipment_id, str(x.state), x.wait_reason) for x in shipments])
        if phase == 'off' and now > 5:
            assert not s.get_editor_property('continuous_operations') and s.get_editor_property('generated_units') == 0
            assert not shipments and not unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseCargo)
            saved_zone = unreal.WarehouseShippingZone(zone_id='IN-REPEAT', inbound=True, anchor=live['RepeatInboundAnchor'],
                half_size=unreal.Vector(170, 170, 80), slots=[unreal.Transform()])
            s.set_editor_property('zones', [])
            assert not s.set_continuous_operations(True) and 'NO_STAGING_ZONE' in s.get_editor_property('operations_status')
            assert not s.get_editor_property('continuous_operations')
            s.set_editor_property('zones', [saved_zone])
            if prepared_visit_check:
                assert t.prepare_dock(), t.get_editor_property('status')
            assert s.set_continuous_operations(True)
            at, phase = now, 'first_receiving'
            check('default off creates no cargo; missing inbound zone rejects enable without side effects')
        elif phase == 'first_receiving':
            if prepared_visit_check and t.get_editor_property('visit_number') == 1:
                assert s.get_editor_property('generated_units') == 0
                assert not unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseCargo), 'Prepared empty visit repeatedly spawned rejected cargo'
            if shipments and shipments[0].state == unreal.WarehouseShipmentState.COMPLETED:
                assert 0 in records and shipments[0].truck_visit_id == records[0]['visit']
                assert len(shipments[0].lines) == 1 and shipments[0].lines[0].arrived
                for actor in (records[0]['pallet'], records[0]['box']):
                    assert body(actor).is_simulating_physics() and not actor.get_editor_property('hidden')
                    assert not actor.actor_has_tag('LogisticsInTransit')
                assert not t.get_editor_property('manifest_pallets')
                at, phase = now, 'return_occupied'
                check('inbound handover restores original pallet/carton physics and empty truck leaves')
        elif phase == 'return_occupied':
            if t.get_editor_property('visit_number') == 2 + visit_offset and t.is_ready():
                at, phase = now, 'blocked'
                check('same empty truck returns through its road route with a fresh visit identity')
        elif phase == 'blocked' and now - at > 3:
            assert s.get_editor_property('generated_units') == 1 and len(shipments) == 1
            assert '점유' in s.get_editor_property('operations_status'), s.get_editor_property('operations_status')
            assert s.set_continuous_operations(False)
            clear_received_fixture_slot(records[0])
            at, phase = now, 'off_again'
            check('occupied receiving slot blocks the second arrival without duplicating inventory')
        elif phase == 'off_again' and now - at > 3:
            assert s.get_editor_property('generated_units') == 1 and len(shipments) == 1
            assert s.set_continuous_operations(True)
            at, phase = now, 'second_receiving'
            check('off switch prevents additional cargo even after the fixture receiving slot is freed')
        elif phase == 'second_receiving':
            if len(shipments) == 2 and all(x.state == unreal.WarehouseShipmentState.COMPLETED for x in shipments):
                assert s.get_editor_property('generated_units') == 2 and set(records) == {0, 1}
                assert records[0]['id'] != records[1]['id'] and records[0]['pallet'] != records[1]['pallet']
                assert records[0]['visit'] != records[1]['visit'] and len(observed_visits) == 2 + visit_offset
                assert records[0]['recipe']['mass'] != records[1]['recipe']['mass'], 'Product rules produced identical sample masses'
                assert records[0]['recipe']['kind'] != records[1]['recipe']['kind']
                assert shipments[1].truck_visit_id == records[1]['visit']
                at, phase = now, 'cap'
        elif phase == 'cap' and now - at > 8:
            assert s.get_editor_property('generated_units') == 2 and not s.get_editor_property('continuous_operations')
            assert not s.set_continuous_operations(True), 'Reached generation cap accepted another visit'
            assert len(shipments) == 2 and t.get_editor_property('visit_number') == 2 + visit_offset
            assert len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseCargo)) == 2
            assert len(unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehousePallet)) == 2
            for index, record in records.items():
                assert str(record['box'].get_editor_property('cargo_id')) == record['id']
                assert abs(record['box'].get_editor_property('gross_mass_kg') - record['recipe']['mass']) < .01
                for actor in (record['pallet'], record['box']):
                    assert not actor.get_editor_property('hidden') and actor.get_actor_enable_collision()
                    assert body(actor).is_simulating_physics() and not actor.actor_has_tag('LogisticsInTransit')
                    assert actor.get_actor_location().z > -2 and actor.get_actor_location().x < 150
            assert live['RepeatTruckFloor'].get_editor_property('hidden'), 'Finished inbound vehicle should be outside the dock'
            check('two visits preserve two distinct visible warehouse loads; capped supply stays off and deterministic recipes differ by item')
            if prepared_visit_check:
                check('previously prepared empty visit clears/departs/returns before spawning any new cargo')
            finish(True, 'Default-off/missing-zone/one-carton manifests/dock service/receiving/empty return/occupied-slot/off-switch/seed recipes/two-visit cap passed')
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
