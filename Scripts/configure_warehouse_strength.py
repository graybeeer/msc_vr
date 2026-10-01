"""Install editable mass/strength profiles and convex collapse meshes; preserves the existing map."""
import os
import re
import sys
import unreal

sys.path.insert(0, os.path.dirname(__file__))
from prepare_warehouse_assets import collision_mesh


def configure_strength():
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    all_actors = actors.get_all_level_actors()
    named = {a.get_actor_label(): a for a in all_actors}
    system = named.get('WH_DamageSystem')
    if not system:
        system = actors.spawn_actor_from_class(unreal.WarehouseDamageSystem, unreal.Vector())
        system.set_actor_label('WH_DamageSystem')
        system.set_editor_property('is_spatially_loaded', False)
    # Retain calibrated ratings/masses when a map is rebuilt. Runtime damage is never saved.
    previous = {s.name: s for s in system.get_editor_property('objects')}
    specs, grouped = [], set()
    failure = unreal.WarehouseFailure

    def add(name, members, mode, mass, capacity, impact_yield, impact_failure, level=0, heights=()):
        spec = unreal.WarehouseStrength()
        spec.set_editor_property('name', name)
        spec.set_editor_property('members', members)
        spec.set_editor_property('failure', mode)
        for key, value in [('mass_kg', mass), ('rated_load_kg', capacity), ('impact_yield_j', impact_yield),
                           ('impact_failure_j', impact_failure), ('level_capacity_kg', level), ('overload_seconds', 10)]:
            spec.set_editor_property(key, previous[name].get_editor_property(key) if name in previous else value)
        spec.set_editor_property('shelf_heights_cm', heights)
        physics = []
        for actor in members:
            grouped.add(actor.get_path_name())
            parts = actor.get_components_by_class(unreal.StaticMeshComponent)
            mesh = parts[0].static_mesh if len(parts) == 1 else None
            if mesh and mode not in (failure.STRUCTURE, failure.MACHINE):
                if mesh.get_path_name().startswith(('/Game/Warehouse/Physics/', '/Engine/BasicShapes/')):
                    physics.append(mesh)
                else:
                    physics.append(collision_mesh(mesh, True))
            else:
                physics.append(None)
        spec.set_editor_property('physics_meshes', physics)
        specs.append(spec)

    if 'MZ_AGVElevator' in named:
        # The connected building is one fixed structure. Assigning concrete slab dead
        # load to each thin decorative flange/mullion produced false startup failures.
        building_prefixes=('WH_Wall_','WH_Column_','WH_Roof','WH_LoadingDoor_Header',
            'EXT_Foundation','EXT_FrontCladding','EXT_SideCladding','EXT_HeaderCladding',
            'EXT_FrontBlueBand','EXT_SideBlueBand','EXT_FrontWindow','EXT_SideWindow',
            'EXT_FrontJoint','EXT_FrontMullion','EXT_SideMullion','EXT_SideParapet',
            'EXT_FrontParapet','EXT_RoofVent','EXT_Canopy')
        building=[]
        for label,actor in named.items():
            mezz=label.startswith(('MZ_L','MZ_Ground')) and '_Rack_' not in label
            if label=='Floor' or mezz or label.startswith(building_prefixes):
                if any(c.static_mesh and c.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION
                       for c in actor.get_components_by_class(unreal.StaticMeshComponent)):
                    building.append(actor)
        # Educational lumped structure: about 2051 m2 of upper decks at 500 kg/m2.
        # It does not calculate individual beam stresses or certify a building design.
        add('WarehouseBuilding',building,failure.STRUCTURE,1100000,1025500,20000,200000)
    for side in ('Left', 'Right'):
        for row in range(11):
            members = [named[f'WH_Rack_{side}_{row:02}_{p}'] for p in 'ABCDEFGH']
            add(f'PalletRack_{side}_{row:02}', members, failure.RACK, 180, 4000, 250, 1600,
                2000, (20, 140))
    for kind, mass, capacity, level, heights in [('Medium', 60, 750, 250, (20,95,170)),
                                               ('Light', 30, 300, 100, (20,85,150))]:
        for index in range(2):
            members = [named[f'WH_{kind}Rack_{index}_{p}'] for p in 'ABCDEFGH']
            members += [named[f'WH_{kind}Shelf_{index}_{tier}'] for tier in range(3)]
            add(f'{kind}Rack_{index}', members, failure.RACK, mass, capacity, 80, 600, level, heights)
    # Upper-floor copies retain the same per-bay limits; support heights are world Z.
    for floor in (1,2):
        for side in ('Left','Right'):
            for row in range(3):
                prefix=f'MZ_L{floor+1}_Rack_{side}_{row}'
                members=[named.get(prefix+'_'+p) for p in 'ABCDEFGH']
                if all(members):add(prefix,members,failure.RACK,180,4000,250,1600,2000,(floor*400+20,floor*400+140))
    for label, actor in named.items():
        if actor.get_path_name() in grouped or not actor.get_actor_enable_collision():
            continue
        parts = [p for p in actor.get_components_by_class(unreal.StaticMeshComponent)
                 if p.static_mesh and p.get_collision_enabled() != unreal.CollisionEnabled.NO_COLLISION]
        if not parts:
            continue
        if isinstance(actor, unreal.WarehouseForklift):
            add(label, [actor], failure.MACHINE, actor.get_editor_property('vehicle_mass_kg'),
                actor.get_editor_property('rated_load_kg'), 400, 2400)
        elif isinstance(actor, unreal.WarehousePallet) or 'Pallet' in label:
            add(label, [actor], failure.CRUSH, 25, 1500, 120, 900)
        elif isinstance(actor, unreal.WarehouseCargo):
            mass = actor.get_editor_property('gross_mass_kg')
            add(label, [actor], failure.CRUSH, mass, 120, 35, 250)
            specs[-1].set_editor_property('mass_kg', mass)
        elif label.startswith('WH_Utility_'):
            index = int(label.rsplit('_', 1)[1])
            add(label, [actor], failure.RIGID, (55,80,90,15)[index], (150,200,150,120)[index], 100, 900)
        elif label == 'EXT_ParkedTruck_02':
            add(label, [actor], failure.STRUCTURE, 6000, 4000, 1500, 15000)
        else:
            # Fixed architecture: keep collision after damage; no unvalidated whole-building collapse.
            _, extent = actor.get_actor_bounds(False)
            volume = max(.001, extent.x*extent.y*extent.z*8/1e6)
            plant = any(word in label.lower() for word in ('tree', 'beech', 'shrub'))
            mass = (2 if 'shrub' in label.lower() else 50) if plant else max(2, volume*600)
            add(label, [actor], failure.STRUCTURE, mass, max(500, mass*10), 2000, 20000)
    system.modify()
    system.set_editor_property('objects', specs)
    print('WAREHOUSE_STRENGTH_CONFIGURED', len(specs), 'profiles', len(grouped), 'actors')
    return system


if __name__ == '__main__':
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    configure_strength()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
