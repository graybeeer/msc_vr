"""Incremental shipment metadata and dock stock; importing this file has no editor side effects.

configure() changes the loaded editor level without saving. The explicit CLI path
loads/saves the warehouse. Existing parcel actors, recipes, IDs and masses survive.
"""
import unreal

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'


def configure():
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    layout = named['WH_LayoutEditor']
    strength = named['WH_DamageSystem']

    def ensure(label, cls, position=None, rotation=None):
        actor = named.get(label)
        if actor is None:
            actor = actors.spawn_actor_from_class(cls, position or unreal.Vector(), rotation or unreal.Rotator())
            actor.set_actor_label(label)
            actor.set_editor_property('is_spatially_loaded', False)
            named[label] = actor
        assert isinstance(actor, cls), label
        actor.modify()
        actor.set_folder_path('Warehouse/Logistics')
        return actor

    def tag(actor, *values):
        actor.modify()
        actor.tags = [unreal.Name(t) for t in dict.fromkeys([str(t) for t in actor.tags] + list(values))]

    def local_bounds(actor, frame):
        center, extent = actor.get_actor_bounds(False)
        points = [frame.inverse_transform_location(center + unreal.Vector(x*extent.x, y*extent.y, z*extent.z))
                  for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
        return (unreal.Vector(*(min(getattr(p, axis) for p in points) for axis in 'xyz')),
                unreal.Vector(*(max(getattr(p, axis) for p in points) for axis in 'xyz')))

    def bottom_at(actor, bottom, rotation):
        actor.modify()
        actor.set_actor_rotation(rotation, False)
        center, extent = actor.get_actor_bounds(False)
        actor.set_actor_location(actor.get_actor_location() + bottom - unreal.Vector(center.x, center.y, center.z-extent.z), False, True)

    shipping = ensure('WH_ShippingSystem', unreal.WarehouseShippingSystem)
    truck_data = []
    for index in (1, 2, 3):
        chassis = named[f'EXT_ParkedTruck_{index:02}']
        group = f'LayoutGroup:TruckDock{index:02}'
        prefix = 'TRK_' if index == 2 else f'TRK_Dock{index:02}_'
        parts = [a for a in named.values() if group in [str(t) for t in a.tags]
                 and not isinstance(a, (unreal.WarehouseCargo, unreal.WarehousePallet, unreal.WarehouseLogisticsTruck, unreal.TargetPoint))
                 and 'DockLeveller' not in a.get_actor_label()]
        leveller = named[prefix + 'DockLeveller']
        tag(leveller, 'LogisticsFixedDock')
        # A fixed bridge belongs to the dock, never to the moving truck assembly.
        leveller.tags = [t for t in leveller.tags if str(t) != group]
        tag(leveller, 'LayoutFixed')
        assert chassis in parts and len(parts) >= 19, (index, len(parts))
        frame = chassis.get_actor_transform()
        truck = ensure(f'WH_LogisticsTruck_{index:02}', unreal.WarehouseLogisticsTruck,
                       chassis.get_actor_location(), chassis.get_actor_rotation())
        truck.set_actor_transform(frame, False, True)
        tag(truck, group, 'LayoutType:Other', 'WarehouseFloor0')
        dock = ensure(f'WH_LogisticsDock_{index:02}', unreal.TargetPoint,
                      chassis.get_actor_location(), chassis.get_actor_rotation())
        dock.set_actor_transform(frame, False, True)
        tag(dock, 'LayoutFixed', 'LogisticsFixedDock')
        for actor in parts:
            for component in actor.get_components_by_class(unreal.SceneComponent):
                component.set_mobility(unreal.ComponentMobility.MOVABLE)
        floor = named[prefix + 'Floor']
        low, high = local_bounds(floor, frame)
        roof_low, _ = local_bounds(named[prefix + 'Roof'], frame)
        width, length = high.y-low.y, high.x-low.x
        assert width >= 225 and length >= 450 and roof_low.z-high.z >= 200
        columns = ((low.y+high.y)/2-56, (low.y+high.y)/2+56)
        rows = ((low.x+high.x)/2-160, (low.x+high.x)/2, (low.x+high.x)/2+160)
        slots = [unreal.Transform(location=unreal.Vector(x, y, high.z+.15)) for x in rows for y in columns]
        truck.set_editor_property('truck_id', f'TRUCK-{index:02}')
        truck.set_editor_property('dock_id', f'DOCK-{index:02}')
        truck.set_editor_property('truck_parts', parts)
        truck.set_editor_property('dock_anchor', dock)
        truck.set_editor_property('load_slots', slots)
        truck.set_editor_property('load_slot_size_cm', unreal.Vector(158, 111, roof_low.z-high.z-.3))
        truck.set_editor_property('rated_payload_kg', 6000)
        # Presentation-only straight departure through the existing unobstructed bay.
        # It stops outside the visible apron; this is not a road traffic simulation.
        start = truck.get_actor_location()
        forward = truck.get_actor_forward_vector()
        truck.set_editor_property('departure_route', [start+forward*900, start+forward*1900, start+forward*3000])
        truck_data.append(truck)

    # The installed 48 intact parcels fit six 110 cm pallets, eight parcels each.
    # No model, recipe, CargoId, dimensions or gross mass is changed.
    incoming = sorted((a for label, a in named.items() if label.startswith('TRK_Cargo_') and isinstance(a, unreal.WarehouseCargo)),
                      key=lambda a: a.get_actor_label())
    assert len(incoming) == 48, len(incoming)
    inbound_truck = truck_data[1]
    frame = inbound_truck.get_actor_transform()
    specs = list(strength.get_editor_property('objects'))
    spec_names = {str(s.name) for s in specs}
    initial_manifest = []
    for slot_index, slot in enumerate(inbound_truck.get_editor_property('load_slots')):
        pallet = ensure(f'TRK_InboundPallet_{slot_index:02}', unreal.WarehousePallet)
        bottom = frame.transform_location(slot.translation)
        rotation = inbound_truck.get_actor_rotation()
        bottom_at(pallet, bottom, rotation)
        tag(pallet, 'LayoutGroup:TruckDock02', 'LayoutType:Other', 'WarehouseFloor0')
        cargo = incoming[slot_index*8:(slot_index+1)*8]
        column_tops = [15.2, 15.2]
        for layer in range(4):
            pair = cargo[layer*2:layer*2+2]
            dimensions = [a.get_editor_property('packing').size_cm for a in pair]
            assert all(d.x <= 30.1 and d.y <= 58.1 and d.z <= 38.1 for d in dimensions), dimensions
            for column, actor in enumerate(pair):
                p = frame.transform_location(slot.translation + unreal.Vector((-18 if column == 0 else 18), 0, column_tops[column]))
                bottom_at(actor, p, rotation)
                actor.get_component_by_class(unreal.StaticMeshComponent).set_simulate_physics(False)
                tag(actor, 'LayoutGroup:TruckDock02', 'LayoutType:Other', 'WarehouseFloor0')
                column_tops[column] += dimensions[column].z + .15
        assert max(column_tops) < inbound_truck.get_editor_property('load_slot_size_cm').z
        if pallet.get_actor_label() not in spec_names:
            spec = unreal.WarehouseStrength()
            spec.set_editor_property('name', pallet.get_actor_label())
            spec.set_editor_property('members', [pallet])
            spec.set_editor_property('physics_meshes', [pallet.get_component_by_class(unreal.StaticMeshComponent).static_mesh])
            spec.set_editor_property('failure', unreal.WarehouseFailure.RIGID)
            spec.set_editor_property('mass_kg', 25)
            spec.set_editor_property('rated_load_kg', 1500)
            spec.set_editor_property('impact_yield_j', 500)
            spec.set_editor_property('impact_failure_j', 3000)
            specs.append(spec)
        initial_manifest.append(pallet)
    strength.modify()
    strength.set_editor_property('objects', specs)
    inbound_truck.set_editor_property('initial_pallets', initial_manifest)

    def clear_slot(point, ignore):
        for actor in named.values():
            if actor in ignore:
                continue
            physical = [c for c in actor.get_components_by_class(unreal.PrimitiveComponent)
                        if unreal.WarehouseDamageSystem.is_physical_contact(c)]
            if not physical:
                continue
            center, extent = actor.get_actor_bounds(True)
            if center.z+extent.z <= point.z+1 or center.z-extent.z >= point.z+210:
                continue
            if abs(center.x-point.x) < extent.x+58 and abs(center.y-point.y) < extent.y+58:
                return False
        return True

    zones = list(layout.get_editor_property('zones'))
    shipping_zones = []
    for zone in zones:
        if zone.name not in ('Inbound', 'Outbound'):
            continue
        inbound = zone.name == 'Inbound'
        anchor = ensure('WH_ShippingZone_'+zone.name, unreal.TargetPoint, zone.centre)
        anchor.set_actor_location(zone.centre, False, True)
        tag(anchor, 'LayoutGroup:Zone'+zone.name, 'LayoutType:Zone', 'WarehouseFloor0')
        members = list(zone.members)
        if anchor not in members:
            members.append(anchor)
        zone.set_editor_property('members', members)
        options = list(zone.destinations)
        for y in range(int(-zone.half_size.y+85), int(zone.half_size.y-84), 140):
            for x in range(int(-zone.half_size.x+85), int(zone.half_size.x-84), 140):
                options.append(unreal.Transform(location=zone.centre+unreal.Vector(x,y,.15), rotation=unreal.Rotator(yaw=180)))
        destinations = []
        for pose in options:
            point = pose.translation
            if abs(point.x-zone.centre.x) > zone.half_size.x-60 or abs(point.y-zone.centre.y) > zone.half_size.y-60:
                continue
            if any((point-p.translation).length() < 125 for p in destinations) or not clear_slot(point, members):
                continue
            destinations.append(pose)
            if len(destinations) >= 6:
                break
        assert destinations, ('No clear pallet handover slot', zone.name)
        if not inbound:
            zone.set_editor_property('destinations', destinations)
        data = unreal.WarehouseShippingZone()
        data.set_editor_property('zone_id', zone.name.upper())
        data.set_editor_property('inbound', inbound)
        data.set_editor_property('anchor', anchor)
        data.set_editor_property('half_size', zone.half_size)
        data.set_editor_property('slots', [unreal.MathLibrary.make_relative_transform(p, anchor.get_actor_transform()) for p in destinations])
        shipping_zones.append(data)
    assert len(shipping_zones) == 2
    layout.modify()
    layout.set_editor_property('zones', zones)
    shipping.set_editor_property('zones', shipping_zones)
    shipping.set_editor_property('trucks', truck_data)
    print('SHIPPING_CONFIGURED', len(truck_data), 'trucks;', len(initial_manifest), 'inbound pallets;', len(incoming), 'original cartons;',
          [(z.zone_id, len(z.slots)) for z in shipping_zones])
    return shipping


if __name__ == '__main__':
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    configure()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    print('SHIPPING_CONFIGURATION_SAVED')
