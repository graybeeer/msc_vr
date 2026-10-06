"""Fit existing warehouse actors to centimetre dimensions; do not rebuild the map."""
import re
import unreal
import os, sys
sys.path.insert(0,os.path.dirname(__file__))

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
SHELF_TOPS = (20.0, 140.0)
RACK_PITCH = 266.0
RACK_START = -1330.0
RACK_X = 215.0
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
                    jobs=list(vehicle.get_editor_property('pending_jobs'))
                    for job in jobs:
                        if job.get_editor_property('pallet')==training:
                            job.set_editor_property('pallet',replacement)
                    vehicle.set_editor_property('pending_jobs',jobs)
            actors.destroy_actor(training)
            replacement.set_actor_label('WH_TrainingPallet')
            named['WH_TrainingPallet'] = replacement
    for label, actor in named.items():
        if label.startswith(('WH_Pallet_', 'WH_DispatchPallet_')) or label in ('WH_Pickup_Pallet', 'WH_Drop_Pallet'):
            component = actor.get_component_by_class(unreal.StaticMeshComponent)
            actor.modify()
            component.set_static_mesh(pallet_mesh)
            component.set_editor_property('override_materials', [])
    # Two 110 cm pallets per 250 cm clear bay, with two reachable load levels.
    for label, actor in list(named.items()):
        if re.fullmatch(r'WH_(Pallet|Cargo)_(Left|Right)_\d+_2(?:_.*)?', label):
            actors.destroy_actor(actor)
            del named[label]
    box_mesh = unreal.load_asset('/Game/Warehouse/Physics/SM_Ind_War_Storage_Box_Cardboard_Worn_02')
    def ensure(label, mesh, cargo=False):
        if label not in named:
            actor = actors.spawn_actor_from_class(unreal.WarehouseCargo if cargo else unreal.StaticMeshActor, unreal.Vector())
            actor.set_actor_label(label)
            if cargo:
                actor.set_cargo_mesh(mesh)
            else:
                actor.static_mesh_component.set_static_mesh(mesh)
                actor.static_mesh_component.set_collision_profile_name('BlockAll')
            named[label] = actor
        return named[label]
    for side in ('Left', 'Right'):
        for row in range(11):
            for tier in range(2):
                for slot in ('', '_B'):
                    ensure(f'WH_Pallet_{side}_{row:02}_{tier}{slot}', pallet_mesh)
                    ensure(f'WH_Cargo_{side}_{row:02}_{tier}{slot}', box_mesh, True)
                    ensure(f'WH_Cargo_{side}_{row:02}_{tier}{slot}_Top', box_mesh, True)
    floor_center, floor_extent = named['Floor'].get_actor_bounds(False)
    ground = floor_center.z + floor_extent.z
    for label, actor in named.items():
        rack = re.fullmatch(r'WH_Rack_(Left|Right)_(\d+)_(\w)', label)
        stored = re.fullmatch(r'WH_(Pallet|Cargo)_(Left|Right)_(\d+)_(\d)(_B)?(_Top)?', label)
        if rack:
            side, row, part = rack.groups()
            x, y = (-RACK_X if side == 'Left' else RACK_X), RACK_START + int(row) * RACK_PITCH
            if part in 'GH':
                fit(actor, (100, 8, 250), (x, y + (129 if part == 'G' else -129), ground + 125))
            else:
                top = {'B': 20, 'D': 20, 'C': 140, 'E': 140, 'A': 250, 'F': 250}[part]
                fit(actor, (6, 258, 12), (x + (40.8 if part in 'ABC' else -40.8), y, ground + top - 6))
        elif stored:
            kind, side, row, tier, slot, stacked = stored.groups()
            x, y = (-RACK_X if side == 'Left' else RACK_X), RACK_START + int(row) * RACK_PITCH
            y += 62.5 if slot else -62.5
            bottom = ground + SHELF_TOPS[int(tier)]
            if kind == 'Pallet':
                fit(actor, (110, 110, 15), (x, y, bottom + 7.5))
            else:
                fit(actor, (60, 40, 40), (x, y, bottom + 35 + (40 if stacked else 0)))
        elif label.startswith('WH_DispatchPallet_') or label in ('WH_Pickup_Pallet', 'WH_Drop_Pallet'):
            # Retain staging positions while correcting dimensions and ground contact.
            pos, _ = actor.get_actor_bounds(False)
            fit(actor, (110, 110, 15), (pos.x, pos.y, ground + 7.5))
        elif label.startswith('WH_Sign_Rack'):
            match = re.fullmatch(r'WH_Sign_Rack([AB])(\d+)', label)
            if match:
                side, index = match.groups()
                actor.modify()
                actor.set_actor_location(unreal.Vector(-RACK_X if side == 'A' else RACK_X,
                                                       RACK_START - 133 + int(index) * 3 * RACK_PITCH, ground + 232), False, False)
                actor.get_component_by_class(unreal.TextRenderComponent).set_world_size(18)

    # Separate hand-picking racks use the supplier's medium/light dimensions.
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    steel = unreal.load_asset('/Game/Warehouse/Materials/MI_DarkSteel')
    for kind, depth, width, height, levels, ys in (
            ('Medium', 60, 180, 210, (20, 95, 170), (-700, -100)),
            ('Light', 45, 120, 180, (20, 85, 150), (650, 950))):
        for index, y in enumerate(ys):
            x = -1180
            for part in 'ABCDEFGH':
                mesh = named[f'WH_Rack_Left_00_{part}'].static_mesh_component.static_mesh
                actor = ensure(f'WH_{kind}Rack_{index}_{part}', mesh)
                actor.set_actor_rotation(unreal.Rotator(yaw=90), False)
                if part in 'GH':
                    fit(actor, (depth, 4, height), (x, y + (width/2-2)*(1 if part=='G' else -1), ground+height/2))
                else:
                    top=levels[{'B':0,'D':0,'C':1,'E':1,'A':2,'F':2}[part]]
                    fit(actor, (3,width-8,4), (x+(depth*.408)*(1 if part in 'ABC' else -1), y, ground+top-2))
            for tier, top in enumerate(levels):
                shelf=ensure(f'WH_{kind}Shelf_{index}_{tier}',cube)
                shelf.static_mesh_component.set_material(0,steel)
                fit(shelf,(depth-2,width-8,2),(x,y,ground+top-1))
                load=ensure(f'WH_{kind}Stock_{index}_{tier}',box_mesh,True)
                size=(40,30,25) if kind=='Medium' else (30,20,20)
                fit(load,size,(x,y,ground+top+size[2]/2))

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
    from configure_cargo_variety import configure_cargo_variety
    configure_cargo_variety()
    print('REAL_WORLD_SCALE_APPLIED', 'cm; AGV original-proportion body 237.73x99.4x215, clear pallet aisle 320, pallet 110x110x15, shelf tops 20/140, bay clear 250x100x250')


if __name__ == '__main__':
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    apply_scale()
    from configure_forklift_autonomy import configure_autonomy
    configure_autonomy()
    from configure_warehouse_strength import configure_strength
    configure_strength()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
