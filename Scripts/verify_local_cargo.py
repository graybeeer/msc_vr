"""Check local 4K cargo materials, collision, recipe sizing and Chaos mass; saves nothing."""
import json
from pathlib import Path
import unreal


def verify_local_cargo():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level('/Engine/Maps/Entry')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    result = {}
    for index, asset in enumerate(('cardboard_box_01', 'plastic_crate_01')):
        base = '/Game/Warehouse/Cargo/PolyHaven/' + asset + '/Local4K/'
        mesh = unreal.load_asset(base + asset + '_4k')
        assert mesh, asset
        extent = mesh.get_bounds().box_extent
        assert min(extent.x, extent.y, extent.z) > 0, asset
        for slot in mesh.static_materials:
            assert slot.material_interface, asset
        normal = unreal.load_asset(base + asset + '_nor_gl_4k')
        albedo = unreal.load_asset(base + asset + '_diff_4k')
        roughness = unreal.load_asset(base + asset + '_rough_4k')
        assert normal.get_editor_property('flip_green_channel'), asset
        assert not normal.get_editor_property('srgb'), asset
        assert albedo.get_editor_property('srgb'), asset
        assert not roughness.get_editor_property('srgb'), asset
        assert albedo.blueprint_get_size_x() == albedo.blueprint_get_size_y() == 4096, asset
        material = unreal.load_asset(base + 'M_' + asset)
        assert unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
        assert unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_NORMAL)
        if asset.startswith('plastic'):
            assert unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_METALLIC)
            assert editor.get_convex_collision_count(mesh) > 1, asset
        else:
            assert editor.get_simple_collision_count(mesh) == 1, asset
        cargo = actors.spawn_actor_from_class(unreal.WarehouseCargo, unreal.Vector(index * 150, 0, 100))
        try:
            cargo.set_cargo_mesh(mesh)
            recipe = unreal.WarehouseCargo.generate_cargo_recipe(20261006, index, 0, unreal.Vector(50, 40, 35), 1)
            assert cargo.apply_cargo_recipe(recipe), asset
            extent = cargo.get_actor_bounds(False)[1]
            expected = (recipe.size_cm.x, recipe.size_cm.y, recipe.size_cm.z)
            assert all(abs(actual-wanted) < .05 for actual, wanted in zip((extent.x*2, extent.y*2, extent.z*2), expected)), asset
            body = cargo.get_component_by_class(unreal.StaticMeshComponent)
            body.set_simulate_physics(True)
            assert body.is_simulating_physics(), asset
            assert abs(body.get_mass() - recipe.gross_mass_kg) < .02, asset
            result[asset] = {'mesh': mesh.get_path_name(), 'size_cm': list(expected),
                             'mass_kg': body.get_mass(), 'simple_collisions': editor.get_simple_collision_count(mesh),
                             'convex_collisions': editor.get_convex_collision_count(mesh), 'texture_resolution': 4096}
        finally:
            actors.destroy_actor(cargo)
    Path(unreal.Paths.project_saved_dir(), 'LocalCargoValidation.json').write_text(json.dumps(result, indent=2))
    print('LOCAL_4K_CARGO_VERIFIED', result)
    return result


if __name__ == '__main__':
    verify_local_cargo()
