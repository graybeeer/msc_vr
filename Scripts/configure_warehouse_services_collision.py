"""Enable solid, fixed service geometry without rebuilding its authored layout."""
import json
from pathlib import Path
import unreal

BASE = '/Game/Warehouse/Services/'
_prepared = set()


def configure_service_mesh(mesh):
    path = mesh.get_path_name()
    if path.startswith(BASE) and path not in _prepared:
        mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        # Fixed fittings need triangle collision: a single hull would fill tray,
        # guard and merged support gaps. Never change shared engine primitives.
        settings = mesh_editor.get_nanite_settings(mesh)
        if settings.get_editor_property('enabled'):
            settings.generate_fallback = unreal.NaniteGenerateFallback.ENABLED
            settings.fallback_target = unreal.NaniteFallbackTarget.PERCENT_TRIANGLES
            settings.fallback_percent_triangles = 1.0
            mesh_editor.set_nanite_settings(mesh, settings)
        # FBX imported with collision disabled can also exclude every section
        # from triangle cooking. A component profile alone does not restore it.
        for lod in range(mesh_editor.get_lod_count(mesh)):
            for section in range(mesh.get_num_sections(lod)):
                if not mesh_editor.is_section_collision_enabled(mesh, lod, section):
                    mesh_editor.enable_section_collision(mesh, True, lod, section)
        unreal.WarehouseCargo.configure_mesh_collision(mesh, True)
        assert mesh_editor.get_collision_complexity(mesh) == unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE, path
        _prepared.add(path)


def configure_service_collision(part):
    mesh = part.static_mesh
    assert mesh, part.get_path_name()
    configure_service_mesh(mesh)
    part.set_simulate_physics(False)
    part.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    part.set_collision_profile_name('BlockAll')
    part.set_editor_property('generate_overlap_events', False)
    assert unreal.WarehouseDamageSystem.is_physical_contact(part), part.get_path_name()


def main():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert editor.get_game_world() is None
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    before = {a.get_path_name(): a.get_actor_transform() for a in actors.get_all_level_actors()}
    services = [a for a in actors.get_all_level_actors() if a.actor_has_tag('WarehouseServices')]
    assert services, 'No authored warehouse services found'
    # Include currently unused library modules so manual placement is solid too.
    for path in unreal.EditorAssetLibrary.list_assets(BASE, True, False):
        asset = unreal.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            configure_service_mesh(asset)
    for actor in services:
        actor.modify()
        for part in actor.get_components_by_class(unreal.StaticMeshComponent):
            part.modify()
            configure_service_collision(part)
    for a in actors.get_all_level_actors():
        assert unreal.MathLibrary.nearly_equal_transform_transform(before[a.get_path_name()], a.get_actor_transform(), .001, .001, .001), a.get_actor_label()
    assert unreal.EditorAssetLibrary.save_directory(BASE, True, True)
    assert level.save_current_level()
    report = dict(date='2026-10-16', service_actors=len(services), triangle_collision_meshes=len(_prepared),
                  profile='BlockAll', simulate_physics=False, original_actor_transforms_preserved=True, map_saved=True)
    path = Path(unreal.Paths.project_saved_dir()) / 'Testing/WarehouseServicesCollision_20261016.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('WAREHOUSE_SERVICES_COLLISION_SAVED', report)


if __name__ == '__main__':
    main()
    unreal.SystemLibrary.quit_editor()
