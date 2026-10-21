"""Native AGV handovers and cinematic per-pallet truck receiving/shipping.

Never saves the warehouse. Fixture construction, actual layout transactions and
the explicitly authorised truck TransferUnit are the only pose-changing paths.
No transport assertion advances an AGV state or changes its native body motion.
"""
import json
from pathlib import Path
import sys
import time
import traceback
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from apply_real_world_scale import fit
from configure_inbound_area import RACK_CENTRES

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
saved = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
rig = {c.get_name(): (c.get_relative_transform(), c.static_mesh, list(c.get_editor_property('override_materials')))
       for c in saved['WH_AutonomousForklift'].get_components_by_class(unreal.StaticMeshComponent)}
rack_data = [(a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d(), a.static_mesh_component.static_mesh,
              list(a.static_mesh_component.get_editor_property('override_materials')))
             for a in [saved['WH_InboundRack_00_' + p] for p in list('ABCDEFGH') + ['Deck_20', 'Deck_140']]]
assert level.load_level('/Engine/Maps/Entry')
for a in list(actors.get_all_level_actors()):
    if not isinstance(a, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(a)
world = editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property('default_game_mode',
    unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))


def spawn(cls, label, point):
    a = actors.spawn_actor_from_class(cls, unreal.Vector(*point))
    a.set_actor_label(label)
    a.set_editor_property('is_spatially_loaded', False)
    return a


def cube(label, point, size):
    a = spawn(unreal.StaticMeshActor, label, point)
    a.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    a.set_actor_scale3d(unreal.Vector(*size) / 100)
    a.static_mesh_component.set_collision_profile_name('BlockAll')
    return a


cube('ShippingFloor', (0, 0, -10), (7200, 5400, 20)).tags = ['WarehouseMapStructure']
spawn(unreal.PlayerStart, 'ShippingStart', (-2200, -2000, 125))
vehicle = spawn(unreal.WarehouseForklift, 'ShippingAGV', (0, 0, .1))
vehicle.set_editor_property('auto_start', False)
vehicle.set_editor_property('pending_jobs', [])
vehicle.set_editor_property('idle_parking_enabled', False)
for c in vehicle.get_components_by_class(unreal.StaticMeshComponent):
    pose, mesh, materials = rig[c.get_name()]
    c.set_relative_transform(pose, False, True)
    c.set_static_mesh(mesh)
    c.set_editor_property('override_materials', materials)

mesh = unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2')
specs = []
for name, x, y, mass in [('ExportA', 500, 0, 12), ('ExportB', 500, 220, 18),
                          ('OtherShipment', 1200, 440, 9), ('ImportA', -400, 900, 15), ('ImportB', -400, 1120, 21),
                          ('DeletedInboundProbe', -400, 1700, 7)]:
    p = spawn(unreal.WarehousePallet, name + 'Pallet', (x, y, .1))
    b = spawn(unreal.WarehouseCargo, name + 'Box', (x, y, 30.25))
    b.set_cargo_mesh(mesh)
    b.set_gross_mass_kg(mass)
    b.set_editor_property('cargo_id', 'SHIPPING-' + name)
    fit(b, (40, 38, 30), (x, y, 30.25))
    if name.startswith('Import'):
        p.tags = ['LayoutGroup:TruckDock02', 'LayoutType:Other', 'TruckCargo']
        b.tags = ['LayoutGroup:TruckDock02', 'LayoutType:Other', 'TruckCargo']
    specs += [unreal.WarehouseStrength(name=p.get_actor_label(), members=[p], mass_kg=25, rated_load_kg=1500,
        impact_yield_j=200, impact_failure_j=1000), unreal.WarehouseStrength(name=b.get_actor_label(), members=[b],
        mass_kg=mass, rated_load_kg=120, impact_yield_j=200, impact_failure_j=1000)]

rack = []
for i, (point, rotation, scale, mesh_asset, materials) in enumerate(rack_data):
    a = spawn(unreal.StaticMeshActor, 'ShippingRack' + str(i),
        (point.x - RACK_CENTRES[0][0] + 1200, point.y - RACK_CENTRES[0][1] + 962.5, point.z))
    a.set_actor_rotation(rotation, False)
    a.set_actor_scale3d(scale)
    a.static_mesh_component.set_static_mesh(mesh_asset)
    a.static_mesh_component.set_editor_property('override_materials', materials)
    a.static_mesh_component.set_collision_profile_name('BlockAll')
    a.tags = ['LayoutType:Rack', 'LayoutGroup:ShippingRack']
    rack.append(a)
specs.append(unreal.WarehouseStrength(name='ShippingRack', members=rack, failure=unreal.WarehouseFailure.RACK,
    mass_kg=250, rated_load_kg=4000, level_capacity_kg=2000, shelf_heights_cm=[20, 140]))
strength = spawn(unreal.WarehouseDamageSystem, 'ShippingStrength', (0, -2100, 0))
strength.set_editor_property('objects', specs)

out_anchor = spawn(unreal.TargetPoint, 'ShippingOutboundAnchor', (1200, 220, 0))
in_zone = spawn(unreal.WarehouseInboundZone, 'ShippingInboundZone', (500, 1010, 0))
in_zone.set_editor_property('half_size_cm', unreal.Vector(200, 280, 80))
in_zone.set_editor_property('rack_slots', [unreal.WarehouseRackSlot(name='ShippingRackLower', rack=rack[0],
    pose=unreal.Transform(location=unreal.Vector(1200, 900, 20)), clear_height_cm=110, capacity_kg=2000)])
# The only receiving slot is occupied until the test removes this ordinary scenery actor.
cube('ShippingInboundBlocker', (500, 900, 35), (100, 100, 70))


def make_truck(label, point):
    t = spawn(unreal.WarehouseLogisticsTruck, label, point)
    t.set_editor_property('truck_id', label)
    t.set_editor_property('dock_id', label + '-DOCK')
    dock = spawn(unreal.TargetPoint, label + 'Dock', point)
    frame = cube(label + 'Floor', (point[0], point[1], point[2] - 10), (200, 500, 20))
    t.set_editor_property('dock_anchor', dock)
    t.set_editor_property('truck_parts', [frame])
    t.set_editor_property('load_slots', [unreal.Transform(location=unreal.Vector(0, y, 0)) for y in (-110, 110)])
    t.set_editor_property('load_slot_size_cm', unreal.Vector(160, 114, 210))
    t.set_editor_property('transfer_interval_seconds', .75)
    t.set_editor_property('departure_route', [unreal.Vector(point[0] + 800, point[1], point[2])])
    return t


export = make_truck('ShippingExportTruck', (2200, 0, 0))
other = make_truck('ShippingOtherTruck', (2200, -1100, 0))
imports = make_truck('ShippingImportTruck', (2200, 1100, 0))
mutation_truck = make_truck('ShippingMutationTruck', (2200, -2000, 0))
shipping = spawn(unreal.WarehouseShippingSystem, 'ShippingSystem', (-2100, 0, 0))
shipping.set_editor_property('zones', [unreal.WarehouseShippingZone(zone_id='OUT-TEST', anchor=out_anchor,
    half_size=unreal.Vector(200, 430, 80), slots=[unreal.Transform(location=unreal.Vector(0, y, 0)) for y in (-220, 0, 220)]),
    unreal.WarehouseShippingZone(zone_id='IN-TEST', inbound=True, anchor=in_zone, half_size=unreal.Vector(200, 280, 80),
        slots=[unreal.Transform(location=unreal.Vector(0, y, 0)) for y in (-110, 110)])])
shipping.set_editor_property('trucks', [export, other, imports, mutation_truck])
layout = spawn(unreal.WarehouseLayoutEditor, 'ShippingLayout', (-2100, -500, 0))
layout.set_editor_property('zones', [unreal.WarehouseLayoutZone(name='Outbound', centre=unreal.Vector(1200, 220, 0),
    half_size=unreal.Vector(200, 430, 80), members=[out_anchor],
    destinations=[unreal.Transform(location=unreal.Vector(1200, y, 0)) for y in (0, 220, 440)])])
console = spawn(unreal.WarehouseComputer, 'ShippingConsole', (-1800, -1600, 0))
console_spec = unreal.WarehouseStrength(name='ShippingConsole', members=[console], failure=unreal.WarehouseFailure.STRUCTURE,
    mass_kg=80, rated_load_kg=150, impact_yield_j=1000, impact_failure_j=5000)
strength.set_editor_property('objects', list(strength.get_editor_property('objects')) + [console_spec])
sun = spawn(unreal.DirectionalLight, 'ShippingSun', (0, 0, 1800))
sun.set_actor_rotation(unreal.Rotator(pitch=-55, yaw=-30), False)
sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(4)
spawn(unreal.SkyAtmosphere, 'ShippingSky', (0, 0, 0))
camera = spawn(unreal.CameraActor, 'ShippingCamera', (1600, -1800, 1900))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), unreal.Vector(900, 200, 0)), False)

out = Path(unreal.Paths.project_saved_dir()) / 'Testing/ShippingWorkflow'
out.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
phase = 'start'
at = 0
live = None
checks = []
last_log = -10
shipment_id = ''
inbound_id = ''
last_loaded = 0
last_load_at = -1
last_unloaded = 0
last_unload_at = -1
handover_seen = set()
identities = {}


def check(name):
    checks.append(name)
    print('SHIPPING_WORKFLOW_CHECK_PASSED', name)


def body(a):
    return a.get_component_by_class(unreal.StaticMeshComponent)


def finish(ok, message):
    (out / 'Report.json').write_text(json.dumps({'passed': ok, 'checks': checks, 'message': message,
        'wall_seconds': round(time.monotonic() - started, 2)}, ensure_ascii=False, indent=2), encoding='utf-8')
    print('SHIPPING_WORKFLOW_PIE_' + ('PASSED' if ok else 'FAILED'), message)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():
        unreal.SystemLibrary.quit_editor()


def get_shipment(system, identifier):
    return next(s for s in system.get_editor_property('shipments') if s.shipment_id == identifier)


def zone_copies(system):
    # Native property structs are live views. Own the restored values before mutating an array.
    return [unreal.WarehouseShippingZone(zone_id=z.zone_id, inbound=z.inbound, anchor=z.anchor,
        half_size=z.half_size, slots=list(z.slots)) for z in system.get_editor_property('zones')]


def verify_queue_cancellation_mutations(game, system, v, damage, console, foreign):
    """Queue via the real dispatcher, break/delete its console, then restore that resource.

    Power toggles are public operator controls. This callback never advances an
    AI state/time, moves its body, clears its queue, or edits a completion record.
    """
    assert console.set_vehicle_running(v, True)
    assert system.create_outbound('Cancellation resource probe', [foreign], 'OUT-TEST', live['ShippingOtherTruck'])
    probe_id = system.get_editor_property('shipments')[-1].shipment_id
    system.refresh_shipping()
    line = get_shipment(system, probe_id).lines[0]
    assert line.job_id and any(j.job_id == line.job_id for j in v.get_editor_property('pending_jobs'))
    assert not v.get_editor_property('active_job').job_id
    assert console.set_vehicle_running(v, False)
    damage.apply_impact(console, 50000)
    assert not console.can_use()
    assert not system.cancel_shipment(probe_id), 'Damaged console pretended to remove an actual queued instruction'
    assert get_shipment(system, probe_id).state != unreal.WarehouseShipmentState.CANCELLED
    assert system.get_pallet_shipment(foreign) == probe_id
    assert any(j.job_id == line.job_id for j in v.get_editor_property('pending_jobs'))
    pose = console.get_actor_transform()
    console.destroy_actor()
    assert not system.cancel_shipment(probe_id), 'Deleted console pretended to remove an actual queued instruction'
    assert system.get_pallet_shipment(foreign) == probe_id
    assert any(j.job_id == line.job_id for j in v.get_editor_property('pending_jobs'))
    # Engine marks these native spawning functions BlueprintInternalUseOnly,
    # so Python has no generated convenience methods; invoke their reflection API.
    library = unreal.get_default_object(unreal.GameplayStatics.static_class())
    replacement = library.call_method('BeginDeferredActorSpawnFromClass', args=(game, unreal.WarehouseComputer.static_class(), pose,
        unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    assert replacement
    replacement = library.call_method('FinishSpawningActor', args=(replacement, pose, unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    replacement.set_actor_label('ShippingConsole')
    live['ShippingConsole'] = replacement
    replacement.refresh_state()
    assert replacement.can_use() and system.cancel_shipment(probe_id)
    assert not any(j.job_id == line.job_id for j in v.get_editor_property('pending_jobs'))
    assert not system.get_pallet_shipment(foreign)
    check('damaged/deleted console cannot falsely cancel a real queue; reservation retained until console replacement removes it')
    return replacement


def tick(dt):
    global live, phase, at, last_log, shipment_id, inbound_id, last_loaded, last_load_at, last_unloaded, last_unload_at
    try:
        assert time.monotonic() - started < 1800, ('Shipping workflow wall timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if live is None:
            live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
            pc = unreal.GameplayStatics.get_player_controller(game, 0)
            assert pc.set_simulation_speed(4)
            pc.set_view_target_with_blend(live['ShippingCamera'], 0)
            for label in ('ExportA', 'ExportB', 'ImportA', 'ImportB', 'OtherShipment'):
                b = live[label + 'Box']
                identities[label] = (b, b.get_editor_property('cargo_id'), b.get_editor_property('gross_mass_kg'))
        system, v, incoming, outgoing, zone, damage, console = [live[n] for n in
            ('ShippingSystem', 'ShippingAGV', 'ShippingImportTruck', 'ShippingExportTruck', 'ShippingInboundZone', 'ShippingStrength', 'ShippingConsole')]
        p0, p1, foreign, i0, i1 = [live[n + 'Pallet'] for n in ('ExportA', 'ExportB', 'OtherShipment', 'ImportA', 'ImportB')]
        assert v.get_editor_property('ai_state') != unreal.WarehouseAIState.FAULT, v.get_editor_property('status')
        assert not v.get_editor_property('mechanical_failure')
        for label, (actor, cargo_id, mass) in identities.items():
            assert live[label + 'Box'] == actor and actor.get_editor_property('cargo_id') == cargo_id
            assert abs(actor.get_editor_property('gross_mass_kg') - mass) < .01
        if now - last_log > 10:
            last_log = now
            print('SHIPPING_WORKFLOW_PROGRESS', phase, round(now, 2), v.get_editor_property('status'),
                [(s.shipment_id, str(s.state), s.wait_reason, [(l.pallet_label, l.arrived, l.loaded) for l in s.lines])
                 for s in system.get_editor_property('shipments')])
        if phase == 'settle' and now > 4:
            assert incoming.is_ready() and outgoing.is_ready()
            assert all(body(p).is_simulating_physics() for p in (p0, p1, foreign, i0, i1))
            console = verify_queue_cancellation_mutations(game, system, v, damage, console, foreign)
            assert not system.create_outbound('Duplicate entries', [p0, p0], 'OUT-TEST', outgoing)
            assert system.create_outbound('Native dispatch', [p0, p1], 'OUT-TEST', outgoing), system.get_editor_property('last_message')
            shipment_id = system.get_editor_property('shipments')[-1].shipment_id
            assert system.create_outbound('Other delivery', [foreign], 'OUT-TEST', live['ShippingOtherTruck'])
            second = system.get_editor_property('shipments')[-1].shipment_id
            assert second != shipment_id and system.get_pallet_shipment(foreign) == second
            assert not system.create_outbound('Double booking', [p0], 'OUT-TEST', live['ShippingOtherTruck'])
            system.refresh_shipping()
            assert not any(line.arrived for s in system.get_editor_property('shipments') for line in s.lines), 'Position alone registered an arrival'
            assert not get_shipment(system, shipment_id).cargo_ready and not outgoing.get_editor_property('manifest_pallets')
            assert system.cancel_shipment(second)
            assert not system.get_pallet_shipment(foreign)
            restored_zones = zone_copies(system)
            edited_zones = zone_copies(system)
            next(z for z in edited_zones if z.zone_id == 'OUT-TEST').set_editor_property('slots', [])
            system.set_editor_property('zones', edited_zones)
            system.refresh_shipping()
            assert 'MISSING_STAGING_SLOT' in get_shipment(system, shipment_id).wait_reason
            assert not get_shipment(system, shipment_id).cargo_ready and not outgoing.get_editor_property('manifest_pallets')
            system.set_editor_property('zones', restored_zones)
            system.refresh_shipping()
            assert 'MISSING_STAGING_SLOT' not in get_shipment(system, shipment_id).wait_reason
            check('deleted outbound staging slots wait safely and resume after their exact restoration')
            assert live['ShippingMutationTruck'].transfer_unit(live['DeletedInboundProbePallet'], 0, True, unreal.Transform())
            assert incoming.transfer_unit(i0, 0, True, unreal.Transform()), incoming.get_editor_property('status')
            # A dock moved away represents a truck that has not reached its designated docking pose.
            live['ShippingExportTruckDock'].set_actor_location(unreal.Vector(2200, 160, 0), False, True)
            at, phase = now, 'stock_second'
            check('unique manifests, duplicate booking rejection and no arrival from mere zone presence')
        elif phase == 'stock_second' and now - at > 1:
            mutation_truck = live['ShippingMutationTruck']
            assert mutation_truck.is_ready()
            assert system.start_inbound(mutation_truck, 'IN-TEST')
            probe_id = system.get_editor_property('shipments')[-1].shipment_id
            live['DeletedInboundProbePallet'].destroy_actor()
            system.refresh_shipping()
            assert 'MISSING_PALLET' in get_shipment(system, probe_id).wait_reason
            assert not get_shipment(system, probe_id).cargo_ready and not any(l.arrived for l in get_shipment(system, probe_id).lines)
            assert system.cancel_shipment(probe_id)
            check('deleted inbound manifest pallet waits with MISSING_PALLET and remains cancellable without a crash')
            assert incoming.is_ready()
            assert incoming.transfer_unit(i1, 1, True, unreal.Transform()), incoming.get_editor_property('status')
            assert incoming.is_unit_on_truck(i0) and incoming.is_unit_on_truck(i1)
            assert abs(incoming.payload_mass_kg() - 86) < .1
            assert console.set_vehicle_running(v, True)
            at, phase = now, 'outbound_drive'
            check('inbound truck manifest uses two existing complete pallet units without cloning')
        elif phase == 'outbound_drive':
            shipment = get_shipment(system, shipment_id)
            assert len(shipment.lines) == 2 and all(line.pallet != foreign for line in shipment.lines)
            assert not shipment.truck_ready and not any(line.loaded for line in shipment.lines)
            for line in shipment.lines:
                if line.job_id and line.job_id not in v.get_editor_property('completed_job_ids'):
                    assert not line.arrived, 'Unfinished AGV delivery registered as arrival'
                    if v.get_supported_pallet() == line.pallet and line.pallet.get_actor_location().x > 1000:
                        handover_seen.add(line.pallet)
            if all(line.arrived for line in shipment.lines):
                assert shipment.cargo_ready and not shipment.truck_ready
                assert len(handover_seen) == 2, 'No in-zone loaded approach was observed'
                assert all(line.job_id in v.get_editor_property('completed_job_ids') for line in shipment.lines)
                assert all(v.get_supported_pallet() != line.pallet for line in shipment.lines)
                assert not outgoing.get_editor_property('manifest_pallets')
                outgoing.set_editor_property('load_slot_size_cm', unreal.Vector(50, 50, 100))
                live['ShippingExportTruckDock'].set_actor_location(unreal.Vector(2200, 0, 0), False, True)
                at, phase = now, 'space_guard'
                check('two native insertion/lift/navigation/lower/withdraw completions, cargo ready waits for assigned truck')
            assert now - at < 650, 'Native outbound delivery timeout'
        elif phase == 'space_guard' and now - at > 2:
            shipment = get_shipment(system, shipment_id)
            assert shipment.truck_ready and not any(line.loaded for line in shipment.lines)
            assert shipment.wait_reason, 'Missing truck-space rejection reason'
            outgoing.set_editor_property('load_slot_size_cm', unreal.Vector(160, 114, 210))
            outgoing.set_editor_property('rated_payload_kg', 1)
            at, phase = now, 'weight_guard'
            check('insufficient real truck slot dimensions block loading')
        elif phase == 'weight_guard' and now - at > 2:
            shipment = get_shipment(system, shipment_id)
            assert not any(line.loaded for line in shipment.lines) and shipment.wait_reason
            outgoing.set_editor_property('rated_payload_kg', 6000)
            at, phase = now, 'loading'
            check('truck payload limit blocks loading without dropping or duplicating warehouse cargo')
        elif phase == 'loading':
            shipment = get_shipment(system, shipment_id)
            count = sum(line.loaded for line in shipment.lines)
            if count != last_loaded:
                assert count == last_loaded + 1, 'More than one unit loaded in a transfer step'
                if last_load_at >= 0:
                    assert now - last_load_at >= .70, 'Truck transfer interval ignored'
                last_loaded, last_load_at = count, now
            assert set(outgoing.get_editor_property('manifest_pallets')) <= {p0, p1}
            assert foreign not in outgoing.get_editor_property('manifest_pallets')
            if shipment.state == unreal.WarehouseShipmentState.COMPLETED:
                assert count == 2 and outgoing.get_editor_property('state') == unreal.WarehouseTruckState.DEPARTED
                assert set(outgoing.get_editor_property('manifest_pallets')) == {p0, p1}
                assert shipment.finished_at >= shipment.started_at and shipment.elapsed_seconds > 0
                assert all(p.get_editor_property('hidden') for p in (p0, p1))
                assert system.start_inbound(incoming, 'IN-TEST'), system.get_editor_property('last_message')
                inbound_id = system.get_editor_property('shipments')[-1].shipment_id
                assert not system.start_inbound(incoming, 'IN-TEST'), 'Same inbound visit started twice'
                at, phase = now, 'inbound_blocked'
                check('one-by-one loading, exact manifest, recorded duration and truck departure')
            assert now - at < 40, 'Cinematic export/departure timeout'
        elif phase == 'inbound_blocked' and now - at > 2:
            shipment = get_shipment(system, inbound_id)
            assert not any(line.arrived for line in shipment.lines)
            assert set(incoming.get_editor_property('manifest_pallets')) == {i0, i1}
            assert shipment.wait_reason, 'Occupied receiving slot has no wait reason'
            live['ShippingInboundBlocker'].destroy_actor()
            at, phase = now, 'unloading'
            check('occupied inbound handover slot prevents unloading and retains original manifest')
        elif phase == 'unloading':
            shipment = get_shipment(system, inbound_id)
            count = sum(line.arrived for line in shipment.lines)
            if count != last_unloaded:
                assert count == last_unloaded + 1, 'More than one unit unloaded in a transfer step'
                if last_unload_at >= 0:
                    assert now - last_unload_at >= .70, 'Inbound transfer interval ignored'
                last_unloaded, last_unload_at = count, now
            for line in shipment.lines:
                if line.arrived:
                    assert not incoming.is_unit_on_truck(line.pallet)
                    assert body(line.pallet).is_simulating_physics() and body(line.pallet).is_gravity_enabled()
                    parcel = live['ImportABox' if line.pallet == i0 else 'ImportBBox']
                    assert body(parcel).is_simulating_physics()
                    assert all(not str(tag).startswith('LayoutGroup:TruckDock') for a in (line.pallet, parcel) for tag in a.tags)
                    assert all(not a.actor_has_tag('TruckCargo') for a in (line.pallet, parcel)), 'Unloaded stock still labelled as truck inventory'
            if shipment.state == unreal.WarehouseShipmentState.COMPLETED and zone.get_editor_property('completed_count') >= 1:
                assert count == 2 and not incoming.get_editor_property('manifest_pallets')
                assert incoming.get_editor_property('state') == unreal.WarehouseTruckState.DEPARTED
                assert zone.get_editor_property('confirmed_count') == 2
                assert zone.get_editor_property('dispatched_count') == 1, 'Occupied rack was double allocated'
                assert all(p.get_editor_property('receipt_state') == unreal.WarehouseReceiptState.CONFIRMED for p in (i0, i1))
                stored = next(p for p in (i0, i1) if p in damage.get_supported_actors(live['ShippingRack0']))
                assert abs(damage.get_supported_mass(stored) - (15 if stored == i0 else 21)) < .1
                assert v.get_supported_pallet() is None
                console.refresh_state()
                rows = [r for r in console.get_editor_property('inventory') if r.pallet in (i0, i1)]
                assert len(rows) == 2 and all('입고 확정' in r.receipt_status for r in rows)
                check('one-by-one inbound unload, native physics restore, NPC wrapping and actual AGV rack storage')
                finish(True, 'Outbound manifests/physical AGV handovers/truck readiness/space/weight/sequential loading/departure; inbound occupancy/sequential unloading/NPC wrap/native rack delivery passed')
            assert now - at < 600, 'Inbound unload / native rack delivery timeout'
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
