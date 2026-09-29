"""Fit existing warehouse actors to centimetre dimensions; do not rebuild the map."""
import re
import unreal

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
SHELF_TOPS = (20.0, 80.0, 140.0)
PALLET_MESH = '/Game/Warehouse/Physics/SM_Ind_War_Storage_Pallet_Wood_Worn_01'


def fit(actor, size, center):
    """Resize a cardinal-axis actor's mesh AND collision about its measured bounds."""
    rotation = actor.get_actor_rotation()
    assert abs(rotation.pitch) < .01 and abs(rotation.roll) < .01, actor.get_actor_label()
    quarter_turns = round(rotation.yaw / 90)
    assert abs(rotation.yaw - quarter_turns * 90) < .01, actor.get_actor_label()
    _, extent = actor.get_actor_bounds(False)
    ratios = [size[i] / (2 * getattr(extent, axis)) for i, axis in enumerate(('x', 'y', 'z'))]
    if quarter_turns % 2:
        ratios[0], ratios[1] = ratios[1], ratios[0]
    scale = actor.get_actor_scale3d()
    actor.modify()
    actor.set_actor_scale3d(unreal.Vector(scale.x * ratios[0], scale.y * ratios[1], scale.z * ratios[2]))
    actual, _ = actor.get_actor_bounds(False)
    actor.set_actor_location(actor.get_actor_location() + unreal.Vector(*center) - actual, False, False)


def apply_scale():
    """Apply to the loaded level. The caller owns loading/saving the level."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    pallet_mesh = unreal.load_asset(PALLET_MESH)
    assert pallet_mesh, 'Run prepare_warehouse_assets.py before applying pallet dimensions'
    training = named.get('WH_TrainingPallet')
    if training:
        parts = training.get_components_by_class(unreal.StaticMeshComponent)
        if len(parts) != 1 or parts[0].static_mesh != pallet_mesh:
            replacement = actors.spawn_actor_from_class(unreal.WarehousePallet, training.get_actor_location(), training.get_actor_rotation())
            replacement.set_editor_property('payload_mass_kg', training.get_editor_property('payload_mass_kg'))
            replacement.set_editor_property('pallet_mass_kg', training.get_editor_property('pallet_mass_kg'))
            replacement.set_editor_property('is_spatially_loaded', False)
            for vehicle in named.values():
                if isinstance(vehicle, unreal.WarehouseForklift) and vehicle.get_editor_property('target_pallet') == training:
                    vehicle.modify()
                    vehicle.set_editor_property('target_pallet', replacement)
            actors.destroy_actor(training)
            replacement.set_actor_label('WH_TrainingPallet')
            named['WH_TrainingPallet'] = replacement
    for label, actor in named.items():
        if label.startswith(('WH_Pallet_', 'WH_DispatchPallet_')) or label in ('WH_Pickup_Pallet', 'WH_Drop_Pallet'):
            component = actor.get_component_by_class(unreal.StaticMeshComponent)
            actor.modify()
            component.set_static_mesh(pallet_mesh)
            component.set_editor_property('override_materials', [])
    floor_center, floor_extent = named['Floor'].get_actor_bounds(False)
    ground = floor_center.z + floor_extent.z
    for label, actor in named.items():
        rack = re.fullmatch(r'WH_Rack_(Left|Right)_(\d+)_(\w)', label)
        stored = re.fullmatch(r'WH_(Pallet|Cargo)_(Left|Right)_(\d+)_(\d)', label)
        if rack:
            side, row, part = rack.groups()
            x, y = (-205 if side == 'Left' else 205), -750 + int(row) * 150
            if part in 'GH':
                fit(actor, (110, 5, 220), (x, y + (72.5 if part == 'G' else -72.5), ground + 110))
            else:
                top = {'B': 20, 'D': 20, 'C': 80, 'E': 80, 'A': 140, 'F': 140}[part]
                fit(actor, (6, 145, 12), (x + (50 if part in 'ABC' else -50), y, ground + top - 6))
        elif stored:
            kind, side, row, tier = stored.groups()
            x, y = (-205 if side == 'Left' else 205), -750 + int(row) * 150
            bottom = ground + SHELF_TOPS[int(tier)]
            if kind == 'Pallet':
                fit(actor, (110, 110, 15), (x, y, bottom + 7.5))
            else:
                fit(actor, (60, 40, 30), (x, y, bottom + 30))
        elif label.startswith('WH_DispatchPallet_') or label in ('WH_Pickup_Pallet', 'WH_Drop_Pallet'):
            # Retain staging positions while correcting dimensions and ground contact.
            pos, _ = actor.get_actor_bounds(False)
            fit(actor, (110, 110, 15), (pos.x, pos.y, ground + 7.5))
        elif label.startswith('WH_Sign_Rack'):
            match = re.fullmatch(r'WH_Sign_Rack([AB])(\d+)', label)
            if match:
                side, index = match.groups()
                actor.modify()
                actor.set_actor_location(unreal.Vector(-205 if side == 'A' else 205,
                                                       -825 + int(index) * 450, ground + 205), False, False)
                actor.get_component_by_class(unreal.TextRenderComponent).set_world_size(18)

    # Pallets are now in place; centre and stack cargo without relying on mesh pivots.
    for label, actor in named.items():
        stock = re.fullmatch(r'WH_Stock_(\d+)_(\d)_(\d)_(\d)', label)
        if stock:
            index, row, col, tier = map(int, stock.groups())
            center, _ = named[f'WH_DispatchPallet_{index}'].get_actor_bounds(False)
            fit(actor, (40, 30, 25), (center.x + (row - .5) * 43,
                                     center.y + (col - .5) * 34, ground + 27.5 + tier * 25))
        elif label in ('WH_Pickup_Box', 'WH_Drop_Box'):
            center, _ = named[label.replace('_Box', '_Pallet')].get_actor_bounds(False)
            fit(actor, (40, 30, 25), (center.x, center.y, ground + 27.5))

    # This Fab mesh is already authored in cm: body width 232.5, mirrors 306.3,
    # length 732.1, height 318.9. Preserve its proportions and reset accidental scale.
    truck = named.get('EXT_ParkedTruck_02')
    if truck:
        truck.modify()
        truck.set_actor_scale3d(unreal.Vector(1, 1, 1))
        center, extent = truck.get_actor_bounds(False)
        truck.set_actor_location(truck.get_actor_location() +
                                 unreal.Vector(350 - center.x, -2535 - center.y - extent.y,
                                               ground - 120 - center.z + extent.z), False, False)
    for actor in named.values():
        if isinstance(actor, unreal.PlayerStart):
            actor.modify()
            half_height = actor.get_component_by_class(unreal.CapsuleComponent).get_scaled_capsule_half_height()
            actor.set_actor_location(unreal.Vector(780, -1120, ground + half_height + 2), False, False)
            actor.set_actor_rotation(unreal.Rotator(yaw=30), False)
    forklift = named.get('WH_AutonomousForklift')
    if forklift:
        # Dimensions come from the mechanical rig, including its forks and wheels.
        forklift.modify()
        forklift.set_actor_scale3d(unreal.Vector(1, 1, 1))
    print('REAL_WORLD_SCALE_APPLIED', 'cm; AGV 164.2x99.4x215, aisle 300, pallet 110x110x15, shelf tops 20/80/140')


if __name__ == '__main__':
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    apply_scale()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
