"""Configure two independent ground-floor AGVs without rebuilding warehouse actors.

Run after the editor target is built. Only this script's second AGV, pallet, carton,
charging bay are created; existing architecture, workers and stock stay.
"""
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(__file__))
from apply_real_world_scale import fit

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
FLEET = (
    ('WH_AutonomousForklift', 'WH_TrainingPallet', 'MZ_TransferCargo', 'WH_ChargingStation',
     1000, -1200, -650, 250, -1530, 'L1-V01'),
    ('WH_AutonomousForklift_02', 'WH_TrainingPallet_02', 'WH_TransferCargo_02', 'WH_ChargingStation_02',
     1450, -1000, -450, 450, -1250, 'L1-V02'),
)


def configure_dual_forklifts():
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    assert isinstance(named.get('WH_AutonomousForklift'), unreal.WarehouseForklift)
    assert isinstance(named.get('WH_TrainingPallet'), unreal.WarehousePallet)
    assert isinstance(named.get('MZ_TransferCargo'), unreal.WarehouseCargo)
    template = named['WH_AutonomousForklift']
    normalized_box = unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2')
    canonical_pallet = unreal.load_asset('/Game/Warehouse/Physics/SM_Pallet_110')
    assert normalized_box and canonical_pallet, 'Run prepare_intact_cargo_physics.py first'

    def ensure(cls, label):
        actor = named.get(label)
        if actor is None:
            actor = actors.spawn_actor_from_class(cls, unreal.Vector())
            actor.set_actor_label(label)
            named[label] = actor
        assert isinstance(actor, cls), label
        actor.modify()
        actor.set_editor_property('is_spatially_loaded', False)
        actor.set_folder_path('Warehouse/Fleet/L1')
        return actor

    def copy_rig(source, destination):
        sources = {c.get_name(): c for c in source.get_components_by_class(unreal.SceneComponent)}
        for part in destination.get_components_by_class(unreal.SceneComponent):
            if part == destination.root_component:
                continue
            reference = sources[part.get_name()]
            part.modify()
            part.set_relative_transform(reference.get_relative_transform(), False, True)
            if isinstance(part, unreal.StaticMeshComponent):
                part.set_static_mesh(reference.static_mesh)
                part.set_editor_property('override_materials', reference.get_editor_property('override_materials'))
                part.set_collision_enabled(reference.get_collision_enabled())
        for prop in ('vehicle_mass_kg', 'rated_load_kg', 'max_fork_height_cm', 'task_fork_height_cm',
                     'empty_travel_speed_cm', 'loaded_travel_speed_cm', 'fork_leading_speed_cm',
                     'battery_voltage', 'battery_capacity_ah', 'charge_below_percent', 'resume_at_percent',
                     'battery_time_scale', 'pallet_detection_range_cm'):
            destination.set_editor_property(prop, source.get_editor_property(prop))

    fleet = []
    for vehicle_label, pallet_label, cargo_label, station_label, x, start_y, source_y, destination_y, charger_y, job_id in FLEET:
        vehicle = ensure(unreal.WarehouseForklift, vehicle_label)
        pallet = ensure(unreal.WarehousePallet, pallet_label)
        cargo = ensure(unreal.WarehouseCargo, cargo_label)
        station = ensure(unreal.WarehouseChargingStation, station_label)
        if vehicle != template:
            copy_rig(template, vehicle)
        vehicle.set_actor_scale3d(unreal.Vector(1, 1, 1))
        vehicle.set_actor_location(unreal.Vector(x, start_y, 0), False, True)
        vehicle.set_actor_rotation(unreal.Rotator(yaw=90), True)
        vehicle.set_editor_property('autonomous_mode', True)
        vehicle.set_editor_property('auto_start', True)
        vehicle.set_editor_property('vehicle_name', f'지게차 {len(fleet)+1}호')
        vehicle.set_editor_property('target_pallet', pallet)
        vehicle.set_editor_property('charging_station', station)
        vehicle.set_editor_property('elevator', named.get('MZ_AGVElevator'))
        vehicle.set_editor_property('navigation_anchors', [
            unreal.Transform(location=unreal.Vector(x, y, 0), rotation=unreal.Rotator(yaw=90))
            for y in (charger_y + 160, start_y, source_y - 320, destination_y - 320)
        ])
        vehicle.set_editor_property('pending_jobs', [unreal.WarehouseWorkOrder(
            job_id=job_id, source_system='FMS-L1-FLEET', pallet=pallet,
            destination=unreal.Transform(location=unreal.Vector(x, destination_y, 0), rotation=unreal.Rotator(yaw=90)))])
        body = pallet.get_component_by_class(unreal.StaticMeshComponent)
        body.modify()
        body.set_static_mesh(canonical_pallet)
        body.set_relative_location(unreal.Vector(), False, True)
        body.set_relative_scale3d(unreal.Vector(1, 1, 1))
        body.set_collision_profile_name('PhysicsActor')
        pallet.set_actor_scale3d(unreal.Vector(1, 1, 1))
        old_pallet = pallet.get_actor_location()
        pallet.set_actor_location(unreal.Vector(x, source_y, .1), False, True)
        pallet.set_actor_rotation(unreal.Rotator(yaw=90), True)
        pallet.set_editor_property('payload_mass_kg', 0)
        pallet.set_editor_property('pallet_mass_kg', 25)
        if cargo_label == 'MZ_TransferCargo':
            # Retain the original product, mass and packing rather than rerolling it.
            cargo.set_actor_location(cargo.get_actor_location() + pallet.get_actor_location() - old_pallet, False, True)
        else:
            cargo.set_cargo_mesh(normalized_box)
            cargo.set_actor_rotation(unreal.Rotator(), True)
            recipe = unreal.WarehouseCargo.generate_cargo_recipe(20261010, 2, -1, unreal.Vector(40, 30, 28), 1)
            assert recipe.valid and cargo.apply_cargo_recipe(recipe)
            cargo.set_editor_property('cargo_id', unreal.Name('L1-AGV02-LOAD'))
            center, extent = pallet.get_actor_bounds(False)
            size = recipe.size_cm
            fit(cargo, (size.x, size.y, size.z), (center.x, center.y, center.z + extent.z + .1 + size.z / 2))
        cargo.get_component_by_class(unreal.StaticMeshComponent).set_collision_profile_name('PhysicsActor')
        station.set_actor_location(unreal.Vector(x, charger_y, 0), False, True)
        station.set_actor_rotation(unreal.Rotator(yaw=90), True)
        station.set_editor_property('assigned_vehicle', vehicle)
        fleet.append((vehicle, pallet, cargo))

    # Incremental registration retains all previous building/rack/cargo profiles.
    system = named['WH_DamageSystem']
    specs = list(system.get_editor_property('objects'))
    by_name = {s.name: i for i, s in enumerate(specs)}
    for vehicle, pallet, cargo in fleet:
        for actor, failure, mass, rated, yield_j, failure_j in (
            (vehicle, unreal.WarehouseFailure.MACHINE, vehicle.get_editor_property('vehicle_mass_kg'), vehicle.get_editor_property('rated_load_kg'), 400, 2400),
            (pallet, unreal.WarehouseFailure.CRUSH, 25, 1500, 120, 900),
            (cargo, unreal.WarehouseFailure.CRUSH, cargo.get_editor_property('gross_mass_kg'), 120, 35, 250),
        ):
            label = actor.get_actor_label()
            if label in by_name:
                continue
            components = actor.get_components_by_class(unreal.StaticMeshComponent)
            mesh = components[0].static_mesh if len(components) == 1 else None
            specs.append(unreal.WarehouseStrength(name=label, members=[actor], physics_meshes=[mesh],
                failure=failure, mass_kg=mass, rated_load_kg=rated,
                impact_yield_j=yield_j, impact_failure_j=failure_j, overload_seconds=10))
            by_name[label] = len(specs) - 1
    system.modify()
    system.set_editor_property('objects', specs)
    assert fleet[0][1] != fleet[1][1]
    assert fleet[0][0].get_editor_property('charging_station') != fleet[1][0].get_editor_property('charging_station')
    print('DUAL_FORKLIFTS_CONFIGURED', '2 independent L1 jobs, live load physics, separate chargers')
    return fleet


if __name__ == '__main__':
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    configure_dual_forklifts()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    print('DUAL_FORKLIFTS_SAVED')
