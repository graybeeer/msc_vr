"""Remove only decorative exterior planting; preserve the yard and all logistics.

Saves the main map after checking every surviving actor's pose/render/collision
properties. Exact map packages are backed up under Saved before mutation.
Does not regenerate racks, cargo, truck bays or the building.
"""
import hashlib
import json
from pathlib import Path
import shutil
import time

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
LABELS = {f'EXT_Beech_{i}' for i in range(5)} | {
    f'EXT_Shrub_{i}_{j}' for i in range(5) for j in (-1, 0, 1)}
PLANT_MESHES = ('/Game/Warehouse/Collision/SM_EuropeanBeech_',
                '/Game/Warehouse/Collision/GV_Vol7_Shrub_',
                '/Game/EuropeanBeech/', '/Game/GV_FreeShrubsPack/')


def transform_values(transform):
    t, q, s = transform.translation, transform.rotation, transform.scale3d
    return [round(v, 5) for v in (t.x, t.y, t.z, q.x, q.y, q.z, q.w, s.x, s.y, s.z)]


def snapshot(actor):
    parts = []
    for c in actor.get_components_by_class(unreal.PrimitiveComponent):
        row = dict(name=c.get_name(), mobility=str(c.get_editor_property('mobility')),
                   collision=str(c.get_collision_enabled()),
                   profile=str(c.get_collision_profile_name()),
                   simulating=c.is_simulating_physics(),
                   navigation=c.get_editor_property('can_ever_affect_navigation'))
        if isinstance(c, unreal.StaticMeshComponent):
            row['mesh'] = c.static_mesh.get_path_name() if c.static_mesh else None
            row['materials'] = [c.get_material(i).get_path_name() if c.get_material(i) else None
                                for i in range(c.get_num_materials())]
        parts.append(row)
    return dict(label=actor.get_actor_label(), type=actor.get_class().get_path_name(),
                transform=transform_values(actor.get_actor_transform()),
                tags=sorted(str(t) for t in actor.tags),
                collision=actor.get_actor_enable_collision(), components=sorted(parts, key=lambda p: p['name']))


def strength_values(spec):
    values = {k: float(spec.get_editor_property(k)) for k in
              ('mass_kg', 'rated_load_kg', 'impact_yield_j', 'impact_failure_j',
               'level_capacity_kg', 'overload_seconds')}
    values.update(name=str(spec.name), failure=str(spec.failure),
                  members=[a.get_path_name() if a else None for a in spec.members],
                  physics=[m.get_path_name() if m else None for m in spec.physics_meshes],
                  heights=list(spec.shelf_heights_cm))
    return values


def simplify():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'Stop PIE first'
    assert level.load_level(LEVEL)
    all_actors = list(editor.get_all_level_actors())
    targets = [a for a in all_actors if a.get_actor_label() in LABELS]
    assert len(targets) in (0, 20), ('Unexpected planting layout; inspect before changing', len(targets))
    for actor in targets:
        assert isinstance(actor, unreal.StaticMeshActor), actor.get_actor_label()
        mesh = actor.static_mesh_component.static_mesh
        assert mesh and mesh.get_path_name().startswith(PLANT_MESHES), actor.get_actor_label()
    target_paths = {a.get_path_name() for a in targets}
    kept = {a.get_path_name(): snapshot(a) for a in all_actors if a not in targets}
    named = {a.get_actor_label(): a for a in all_actors}
    for name in ('EXT_Plot', 'EXT_Yard', 'EXT_AccessRoad', 'EXT_DockPlatform',
                 'EXT_ParkedTruck_01', 'EXT_ParkedTruck_02', 'EXT_ParkedTruck_03'):
        assert name in named, name
    system = named['WH_DamageSystem']
    original_profiles = list(system.get_editor_property('objects'))
    kept_profiles = []
    removed_profiles = []
    expected_profiles = []
    for spec in original_profiles:
        members = list(spec.members)
        if any(a and a.get_path_name() in target_paths for a in members):
            # These decorative plants have independent static-strength profiles.
            # Do not rewrite a mixed logistics group in this narrow migration.
            assert all(a and a.get_path_name() in target_paths for a in members), str(spec.name)
            removed_profiles.append(str(spec.name))
        else:
            assert str(spec.name) not in LABELS, ('Stale planting profile', str(spec.name))
            kept_profiles.append(spec)
            expected_profiles.append(strength_values(spec))

    out = ROOT / 'Saved/Testing/ExteriorSimplification'
    out.mkdir(parents=True, exist_ok=True)
    backup = out / f'Before_{time.time_ns()}'
    packages = [ROOT / 'Content/FirstPerson/Lvl_FirstPerson.umap']
    for folder in ('__ExternalActors__', '__ExternalObjects__'):
        packages += list((ROOT / 'Content' / folder / 'FirstPerson/Lvl_FirstPerson').rglob('*.uasset'))
    baseline = {}
    for path in packages:
        relative = path.relative_to(ROOT)
        baseline[str(relative)] = hashlib.sha256(path.read_bytes()).hexdigest()
        if targets:
            destination = backup / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)

    if targets:
        system.modify()
        system.set_editor_property('objects', kept_profiles)
        for actor in targets:
            assert editor.destroy_actor(actor), actor.get_actor_label()
    remaining = {a.get_path_name(): a for a in editor.get_all_level_actors()}
    assert set(remaining) == set(kept), 'Changed actors outside decorative planting'
    for path, expected in kept.items():
        assert snapshot(remaining[path]) == expected, ('Changed surviving actor', expected['label'])
    assert [strength_values(s) for s in system.get_editor_property('objects')] == expected_profiles
    assert not any(a.get_actor_label() in LABELS for a in remaining.values())
    if targets:
        assert level.save_current_level(), 'Map save failed; backup retained'
    assert level.load_level('/Engine/Maps/Entry')
    assert level.load_level(LEVEL)
    reloaded = {a.get_path_name(): a for a in editor.get_all_level_actors()}
    assert set(reloaded) == set(kept), 'Saved actor set differs'
    for path, expected in kept.items():
        assert snapshot(reloaded[path]) == expected, ('Saved surviving actor changed', expected['label'])
    restored_system = next(a for a in reloaded.values() if a.get_actor_label() == 'WH_DamageSystem')
    assert [strength_values(s) for s in restored_system.get_editor_property('objects')] == expected_profiles
    changed = []
    for relative, digest in baseline.items():
        path = ROOT / relative
        if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            changed.append(relative)
    report = dict(passed=True, removed_plants=len(targets), removed_strength_profiles=removed_profiles,
                  preserved_actors=len(kept), remaining_strength_profiles=len(kept_profiles),
                  surviving_actor_properties_equal_after_reload=True,
                  surviving_strength_profiles_equal=True, map_packages_changed=changed,
                  backup=str(backup) if targets else None, idempotent_run=not bool(targets),
                  scope='Saved main-map decorative planting removal; no vehicle or building regeneration')
    (out / ('ApplyReport.json' if targets else 'IdempotenceReport.json')).write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('EXTERIOR_SIMPLIFICATION_PASSED', len(targets), len(kept), len(changed))
    return report


if __name__ == '__main__':
    simplify()
