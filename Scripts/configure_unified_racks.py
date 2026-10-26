"""Use the first-floor Fab pallet rack for every storage bay, without rebuilding the map."""
import math
import re
import unreal

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
SHELF_TOPS = (20, 140)


def rotate_xy(v, yaw):
    angle = math.radians(yaw)
    return unreal.Vector(v.x*math.cos(angle)-v.y*math.sin(angle), v.x*math.sin(angle)+v.y*math.cos(angle), v.z)


def normalize_rack_geometry(create_legacy=False):
    """Preserve bay addresses and transforms; return the common strength groups."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    reference = [named.get('WH_Rack_Left_00_' + p) for p in 'ABCDEFGH']
    assert all(reference), 'The first-floor reference rack must exist'
    rc, rext = reference[6].get_actor_bounds(False)
    hc, hext = reference[7].get_actor_bounds(False)
    origin = unreal.Vector((rc.x + hc.x)/2, (rc.y + hc.y)/2, min(rc.z-rext.z, hc.z-hext.z))
    reference_yaw = math.degrees(math.atan2(rc.y-hc.y, rc.x-hc.x))-90
    templates = [(rotate_xy(a.get_actor_location()-origin, -reference_yaw), a.get_actor_rotation(), a.get_actor_scale3d(),
                  a.static_mesh_component.static_mesh,
                  [a.static_mesh_component.get_material(i) for i in range(a.static_mesh_component.get_num_materials())])
                 for a in reference]
    groups = {}
    pattern = r'(WH_Rack_(?:Left|Right)_\d+|WH_(?:Medium|Light)Rack_\d+|MZ_L[234]_Rack_(?:Left|Right)_\d+|WH_EmptyRack_L[1234]_\d+|WH_InboundRack_\d+)_([A-H])'
    for label, actor in named.items():
        match = re.fullmatch(pattern, label)
        if match:
            groups.setdefault(match[1], {})[match[2]] = actor
    if create_legacy:
        for kind in ('Medium', 'Light'):
            for index in range(2):
                groups.setdefault(f'WH_{kind}Rack_{index}', {})
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    deck = named.get('WH_InboundRack_00_Deck_20')
    pallet = named.get('WH_TrainingPallet')
    material = (deck or pallet).get_component_by_class(unreal.StaticMeshComponent).get_material(0)
    result = []
    for prefix, parts in sorted(groups.items()):
        legacy = re.fullmatch(r'WH_(Medium|Light)Rack_(\d+)', prefix)
        had_old_shelves = legacy and any(f'WH_{legacy[1]}Shelf_{legacy[2]}_{i}' in named for i in range(3))
        if parts:
            assert set(parts) == set('ABCDEFGH'), ('Incomplete rack', prefix)
            gc, ge = parts['G'].get_actor_bounds(False)
            hc, he = parts['H'].get_actor_bounds(False)
            base = unreal.Vector((gc.x+hc.x)/2, (gc.y+hc.y)/2, min(gc.z-ge.z, hc.z-he.z))
            yaw = math.degrees(math.atan2(gc.y-hc.y, gc.x-hc.x))-90
        else:
            ys = (-700, -100) if legacy[1] == 'Medium' else (650, 950)
            base, yaw = unreal.Vector(-1180, ys[int(legacy[2])], 0), 0
        name = prefix.replace('WH_Rack_', 'PalletRack_', 1) if prefix.startswith('WH_Rack_') else prefix.removeprefix('WH_') if legacy else prefix
        floor = max(0, min(3, round(base.z/400)))
        def setup(a):
            a.modify()
            a.tags = [unreal.Name(t) for t in dict.fromkeys(
                [str(t) for t in a.tags if not str(t).startswith(('LayoutGroup:', 'WarehouseFloor', 'RackRole:')) and str(t) != 'LayoutType:Other']
                + ['LayoutType:Rack', 'LayoutGroup:'+name, 'WarehouseFloor'+str(floor), 'RackRole:Storage'])]
        members = []
        for part, (offset, rot, scale, mesh, materials) in zip('ABCDEFGH', templates):
            a = parts.get(part)
            if not a:
                a = actors.spawn_actor_from_class(unreal.StaticMeshActor, base)
                a.set_actor_label(prefix+'_'+part)
                a.set_editor_property('is_spatially_loaded', False)
                parts[part] = a
            setup(a)
            c = a.static_mesh_component
            c.set_static_mesh(mesh)
            c.set_editor_property('override_materials', materials)
            c.set_collision_profile_name('BlockAll')
            a.set_actor_rotation(unreal.Rotator(pitch=rot.pitch, yaw=rot.yaw-reference_yaw+yaw, roll=rot.roll), False)
            a.set_actor_scale3d(scale)
            a.set_actor_location(base+rotate_xy(offset, yaw), False, False)
            members.append(a)
        for top in SHELF_TOPS:
            label = prefix+'_Deck_'+str(top)
            a = named.get(label)
            if not a:
                a = actors.spawn_actor_from_class(unreal.StaticMeshActor, base)
                a.set_actor_label(label)
                a.set_editor_property('is_spatially_loaded', False)
                a.set_folder_path('Warehouse/Racks')
                named[label] = a
            setup(a)
            a.static_mesh_component.set_static_mesh(cube)
            a.static_mesh_component.set_material(0, material)
            a.static_mesh_component.set_collision_profile_name('BlockAll')
            a.set_actor_rotation(unreal.Rotator(yaw=yaw), False)
            a.set_actor_scale3d(unreal.Vector(1, 2.5, .02))
            a.set_actor_location(base+unreal.Vector(0, 0, top-1), False, False)
            members.append(a)
        if legacy and (had_old_shelves or create_legacy):
            # Migrate existing cartons once. Their recipes, IDs, meshes and sizes stay intact.
            for i in range(3):
                cargo = named.get(f'WH_{legacy[1]}Stock_{legacy[2]}_{i}')
                created = not cargo and create_legacy
                if created:
                    label=f'WH_{legacy[1]}Stock_{legacy[2]}_{i}'
                    mesh=unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1')
                    assert mesh, 'Install the intact carton models before rebuilding the warehouse'
                    cargo=actors.spawn_actor_from_class(unreal.WarehouseCargo,base)
                    cargo.set_actor_label(label)
                    cargo.set_cargo_mesh(mesh)
                    cargo.set_editor_property('cargo_id',unreal.Name(label.removeprefix('WH_')))
                    cargo.set_editor_property('is_spatially_loaded',False)
                    _, extent=cargo.get_actor_bounds(False)
                    size=(40,30,25) if legacy[1]=='Medium' else (30,20,20)
                    cargo.set_actor_scale3d(unreal.Vector(*(size[j]/(2*getattr(extent,axis)) for j,axis in enumerate('xyz'))))
                    named[label]=cargo
                if cargo and (had_old_shelves or created):
                    center, extent = cargo.get_actor_bounds(False)
                    assert extent.x <= 49.9 and extent.y <= 54.9 and extent.z*2 < 100, cargo.get_actor_label()
                    offset = unreal.Vector(0, (-62.5, 62.5, 0)[i], SHELF_TOPS[int(i == 2)]+extent.z+.1)
                    cargo.modify()
                    cargo.set_actor_location(cargo.get_actor_location()+base+rotate_xy(offset, yaw)-center, False, False)
                    cargo.tags = list(dict.fromkeys(list(cargo.tags)+[unreal.Name('UnifiedRackCargo')]))
                old = named.get(f'WH_{legacy[1]}Shelf_{legacy[2]}_{i}')
                if old:
                    assert actors.destroy_actor(old), old.get_actor_label()
        result.append({'name': name, 'members': members, 'shelf_heights_cm': [base.z+z for z in SHELF_TOPS]})
    return result


def configure_unified_racks():
    """Update only rack profiles and role labels; preserve other equipment data."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    damage = named['WH_DamageSystem']
    specs = list(damage.get_editor_property('objects'))
    reference = next(s for s in specs if s.name == 'PalletRack_Left_00')
    physics = list(reference.physics_meshes)[:8]+[unreal.load_asset('/Engine/BasicShapes/Cube')]*2
    groups = normalize_rack_geometry()
    replacements = {g['name']: unreal.WarehouseStrength(name=g['name'], members=g['members'], physics_meshes=physics,
        failure=unreal.WarehouseFailure.RACK, mass_kg=250, rated_load_kg=4000, level_capacity_kg=2000,
        impact_yield_j=250, impact_failure_j=1600, overload_seconds=10, shelf_heights_cm=g['shelf_heights_cm']) for g in groups}
    damage.modify()
    damage.set_editor_property('objects', [replacements.pop(s.name) if s.name in replacements else s for s in specs]+list(replacements.values()))
    for label, prop, text in [('WH_AreaUI_Putaway', 'area_name', '팔레트 보관 랙'),
                              ('WH_AreaUI_Picking', 'area_name', '보관·보충 구역'),
                              ('WH_Worker_Picking', 'role_name', '보관·보충')]:
        if label in named:
            named[label].modify()
            named[label].set_editor_property(prop, text)
    return groups


if __name__ == '__main__':
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    groups = configure_unified_racks()
    assert len(groups) >= 1
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    print('UNIFIED_RACKS_SAVED', len(groups), 'single pallet storage model; picking deferred')
