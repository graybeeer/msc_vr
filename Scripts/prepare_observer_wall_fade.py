"""Preserve wall shading in masked temporal-fade variants and tag the warehouse walls.

Import and call prepare() in the loaded warehouse. Direct execution loads that map.
Use -RenderOffscreen for shader compilation; -QuitAfterObserverWallFade is optional.
"""
import hashlib
import unreal

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
DIRECTORY = '/Game/Warehouse/Materials/ObserverWalls'
PALETTE = DIRECTORY + '/DA_ObserverWallPalette'
PARAMETER = 'ObserverWallOpacity'
PREFIXES = ('WH_Wall_', 'EXT_FrontCladding_', 'EXT_SideCladding_')
TAG = 'ObserverFadeWall'
assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary


def _variant(source, cache):
    source_path = source.get_path_name()
    if source_path in cache:
        return cache[source_path]
    digest = hashlib.sha1(source_path.encode('utf-8')).hexdigest()[:10]
    instance = isinstance(source, unreal.MaterialInstanceConstant)
    assert instance or isinstance(source, unreal.Material), source_path
    name = ('MI_' if instance else 'M_') + 'ObserverWall_' + source.get_name() + '_' + digest
    path = DIRECTORY + '/' + name
    if not assets.does_asset_exist(path):
        assert assets.duplicate_asset(source_path, path), source_path
    material = unreal.load_asset(path)
    cache[source_path] = material
    if instance:
        # Rebuild the entire parent chain so intermediate instance overrides survive.
        editing.set_material_instance_parent(material, _variant(source.get_editor_property('parent'), cache))
        overrides = material.get_editor_property('base_property_overrides')
        overrides.set_editor_property('override_blend_mode', True)
        overrides.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        material.set_editor_property('base_property_overrides', overrides)
    else:
        assert source.get_editor_property('blend_mode') in (
            unreal.BlendMode.BLEND_OPAQUE, unreal.BlendMode.BLEND_MASKED), source_path
        opacity = next((node for node in editing.get_material_expressions(material)
                        if isinstance(node, unreal.MaterialExpressionScalarParameter)
                        and str(node.get_editor_property('parameter_name')) == PARAMETER), None)
        if assets.get_metadata_tag(material, 'ObserverWallFadeVersion') != '2':
            # Keep the full attributes wire (including UVs hidden from Python) intact.
            # The engine's layer function replaces only its opacity-mask channel.
            attributes = None
            override = None
            attribute_output = ''
            original_mask = editing.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY_MASK)
            original_output = editing.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_OPACITY_MASK)
            if material.get_editor_property('use_material_attributes'):
                attributes = editing.get_material_property_input_node(material, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
                attribute_output = editing.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
                assert attributes, 'No material attributes input: ' + source_path
                split = editing.create_material_expression(material, unreal.MaterialExpressionBreakMaterialAttributes)
                split_inputs = list(editing.get_material_expression_input_names(split))
                assert len(split_inputs) == 1, split_inputs
                assert editing.connect_material_expressions(attributes, attribute_output, split, split_inputs[0])
                split_outputs = list(editing.get_material_expression_output_names(split))
                assert len(split_outputs) >= 8, split_outputs
                original_mask, original_output = split, split_outputs[7]
                function_path = '/Engine/Functions/MaterialLayerFunctions/MatLayerBlend_OverrideOpacityMask'
                function = unreal.load_asset(function_path)
                assert function, function_path
                override = editing.create_material_expression(material, unreal.MaterialExpressionMaterialFunctionCall)
                override.set_material_function(function)
                override_inputs = list(editing.get_material_expression_input_names(override))
                override_outputs = list(editing.get_material_expression_output_names(override))
                print('OBSERVER_WALL_MASK_OVERRIDE_DIAGNOSTIC', source_path, function_path,
                      'inputs', override_inputs, 'outputs', override_outputs,
                      'source', attributes.get_path_name(), repr(attribute_output))
                pins = {name.replace(' ', '').lower(): name for name in override_inputs}
                assert 'material' in pins and 'newopacitymask' in pins and 'mask' in pins, pins
                assert len(override_outputs) == 1, override_outputs
            if opacity is None:
                opacity = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
                opacity.set_editor_property('parameter_name', PARAMETER)
                opacity.set_editor_property('default_value', 1.0)
            coverage = editing.create_material_expression(material, unreal.MaterialExpressionMaterialFunctionCall)
            coverage.set_material_function(unreal.load_asset('/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'))
            assert editing.connect_material_expressions(opacity, '', coverage, 'Alpha Threshold')
            if original_mask:
                multiply = editing.create_material_expression(material, unreal.MaterialExpressionMultiply)
                assert editing.connect_material_expressions(original_mask, original_output, multiply, 'A')
                assert editing.connect_material_expressions(coverage, '', multiply, 'B')
                coverage = multiply
            if override:
                one = editing.create_material_expression(material, unreal.MaterialExpressionConstant)
                one.set_editor_property('r', 1.0)
                assert editing.connect_material_expressions(attributes, attribute_output, override, pins['material'])
                assert editing.connect_material_expressions(coverage, '', override, pins['newopacitymask'])
                assert editing.connect_material_expressions(one, '', override, pins['mask'])
                assert editing.connect_material_property(override, override_outputs[0], unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
            else:
                assert editing.connect_material_property(coverage, '', unreal.MaterialProperty.MP_OPACITY_MASK)
            material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
            editing.recompile_material(material)
            assets.set_metadata_tag(material, 'ObserverWallFadeVersion', '2')
    assert assets.save_loaded_asset(material)
    return material


def prepare():
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    assert any(actor.get_actor_label() == 'WH_Roof' for actor in actors), 'Load the warehouse map first'
    walls = [actor for actor in actors if isinstance(actor, unreal.StaticMeshActor)
             and actor.get_actor_label().startswith(PREFIXES)
             and 'ObserverRoof' not in [str(tag) for tag in actor.tags]]
    assert walls, 'No warehouse walls found'
    cache, materials = {}, {}
    for actor in walls:
        for mesh in actor.get_components_by_class(unreal.MeshComponent):
            for slot in range(mesh.get_num_materials()):
                source = mesh.get_material(slot)
                if source:
                    materials[source] = _variant(source, cache)
    assert materials
    palette = unreal.load_asset(PALETTE) if assets.does_asset_exist(PALETTE) else None
    if palette is None:
        palette_class = unreal.load_class(None, '/Script/msc_vr.WarehouseObserverWallPalette')
        assert palette_class, 'Compile WarehouseObserverWallPalette first'
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', palette_class)
        palette = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'DA_ObserverWallPalette', DIRECTORY, palette_class, factory)
    assert palette, 'Compile WarehouseObserverWallPalette before preparing wall materials'
    palette.set_editor_property('materials', materials)
    assert assets.save_loaded_asset(palette)
    for actor in walls:
        tags = list(actor.tags)
        if TAG not in [str(tag) for tag in tags]:
            actor.modify()
            actor.tags = tags + [unreal.Name(TAG)]
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    print('OBSERVER_WALL_FADE_PREPARED', len(walls), 'walls', len(materials), 'source materials', PALETTE)
    return palette


if __name__ == '__main__':
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    prepare()
    if '-QuitAfterObserverWallFade' in unreal.SystemLibrary.get_command_line():
        unreal.SystemLibrary.quit_editor()
