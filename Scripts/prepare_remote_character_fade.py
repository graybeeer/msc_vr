"""Clone character/phone shading; add temporal 30% coverage for remote control.

Original character materials stay intact. Run with -RenderOffscreen to compile
shaders. The native character restores its prior material slots on disconnect.
"""
import unreal

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
directory = '/Game/Warehouse/Materials/RemoteCharacter'
def prepare_base(source):
    base_path = directory + '/M_RemoteFade_' + source.get_name()
    if not assets.does_asset_exist(base_path):
        assert assets.duplicate_asset(source.get_path_name(), base_path)
    material = unreal.load_asset(base_path)
    opacity = next((expression for expression in editing.get_material_expressions(material)
                    if isinstance(expression, unreal.MaterialExpressionScalarParameter)
                    and str(expression.get_editor_property('parameter_name')) == 'RemoteCharacterOpacity'), None)
    if opacity is not None:
        return material
    # Preserve a material-attributes source if this template uses one.
    if material.get_editor_property('use_material_attributes'):
        attributes = editing.get_material_property_input_node(material, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
        output = editing.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
        split = editing.create_material_expression(material, unreal.MaterialExpressionBreakMaterialAttributes)
        assert editing.connect_material_expressions(attributes, output, split, 'Attributes')
        material.set_editor_property('use_material_attributes', False)
        for name, prop in [('BaseColor', 'MP_BASE_COLOR'), ('Metallic', 'MP_METALLIC'), ('Specular', 'MP_SPECULAR'),
                           ('Roughness', 'MP_ROUGHNESS'), ('Normal', 'MP_NORMAL'), ('EmissiveColor', 'MP_EMISSIVE_COLOR'),
                           ('AmbientOcclusion', 'MP_AMBIENT_OCCLUSION')]:
            outputs = editing.get_material_expression_output_names(split)
            match = next((output for output in outputs if output.replace(' ', '') == name), None)
            assert match, (name, outputs)
            assert editing.connect_material_property(split, match, getattr(unreal.MaterialProperty, prop))
    original_mask = editing.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY_MASK)
    original_output = editing.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_OPACITY_MASK)
    opacity = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
    opacity.set_editor_property('parameter_name', 'RemoteCharacterOpacity')
    opacity.set_editor_property('default_value', 1.0)
    coverage = editing.create_material_expression(material, unreal.MaterialExpressionMaterialFunctionCall)
    coverage.set_material_function(unreal.load_asset('/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'))
    assert editing.connect_material_expressions(opacity, '', coverage, 'Alpha Threshold')
    if original_mask:
        multiply = editing.create_material_expression(material, unreal.MaterialExpressionMultiply)
        assert editing.connect_material_expressions(original_mask, original_output, multiply, 'A')
        assert editing.connect_material_expressions(coverage, '', multiply, 'B')
        coverage = multiply
    assert editing.connect_material_property(coverage, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property('used_with_skeletal_mesh', True)
    editing.recompile_material(material)
    assert assets.save_loaded_asset(material)
    return material

sources = []
for group, names in [('Manny', ('MI_Manny_01_New', 'MI_Manny_02_New')), ('Quinn', ('MI_Quinn_01', 'MI_Quinn_02'))]:
    for name in names:
        sources.append('/Game/Characters/Mannequins/Materials/' + group + '/' + name)
sources.extend(['/Game/Warehouse/Materials/MI_Rubber', '/Game/Warehouse/AGV/Materials/M_Screen_blue'])
for original in sources:
    source = unreal.load_asset(original)
    assert source, original
    parent = source
    while isinstance(parent, unreal.MaterialInstanceConstant):
        parent = parent.get_editor_property('parent')
    assert isinstance(parent, unreal.Material), original
    material = prepare_base(parent)
    target = directory + '/MI_RemoteFade_' + source.get_name()
    if not assets.does_asset_exist(target):
        if isinstance(source, unreal.MaterialInstanceConstant):
            assert assets.duplicate_asset(original, target)
        else:
            assert unreal.AssetToolsHelpers.get_asset_tools().create_asset('MI_RemoteFade_' + source.get_name(), directory, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    instance = unreal.load_asset(target)
    assert isinstance(instance, unreal.MaterialInstanceConstant)
    editing.set_material_instance_parent(instance, material)
    assert assets.save_loaded_asset(instance)
    print('REMOTE_CHARACTER_FADE_MATERIAL_READY', target)
print('REMOTE_CHARACTER_FADE_PREPARED')
