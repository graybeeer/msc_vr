"""Generate the project's reusable stretch-film material; no external assets."""
import unreal

path = '/Game/Warehouse/Materials/M_PalletWrap'
assets, edit = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
material = unreal.load_asset(path) if assets.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    'M_PalletWrap', '/Game/Warehouse/Materials', unreal.Material, unreal.MaterialFactoryNew())
edit.delete_all_material_expressions(material)
material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property('two_sided', True)
material.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
material.set_editor_property('used_with_spline_meshes', True)


def constant(value, prop):
    node = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
    node.set_editor_property('parameter_name', 'FilmRoughness' if prop == unreal.MaterialProperty.MP_ROUGHNESS else 'FilmSpecular')
    node.set_editor_property('group', 'Film')
    node.set_editor_property('default_value', value)
    assert edit.connect_material_property(node, '', prop)
    return node


color = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter)
color.set_editor_property('parameter_name', 'FilmTint')
color.set_editor_property('group', 'Film')
color.set_editor_property('default_value', unreal.LinearColor(.72, .83, .88))
assert edit.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
rough = constant(.18, unreal.MaterialProperty.MP_ROUGHNESS)
spec = constant(.75, unreal.MaterialProperty.MP_SPECULAR)
uv = edit.create_material_expression(material, unreal.MaterialExpressionWorldPosition)
film = edit.create_material_expression(material, unreal.MaterialExpressionCustom)
film.set_editor_property('code', 'return 0.12 + 0.07 * pow(abs(sin(P.z * 0.42 + sin(P.x * 0.08 + P.y * 0.09) * 0.35)), 14);')
film.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
position_input = unreal.CustomInput()
position_input.set_editor_property('input_name', 'P')
film.set_editor_property('inputs', [position_input])
assert edit.connect_material_expressions(uv, '', film, 'P')
opacity_scale = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
opacity_scale.set_editor_property('parameter_name', 'FilmOpacity')
opacity_scale.set_editor_property('group', 'Film')
opacity_scale.set_editor_property('default_value', 1)
opacity = edit.create_material_expression(material, unreal.MaterialExpressionMultiply)
assert edit.connect_material_expressions(film, '', opacity, 'A')
assert edit.connect_material_expressions(opacity_scale, '', opacity, 'B')
assert edit.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
# Substrate creates its surface node while changing blend mode. Wire the real
# surface too, as in prepare_carry_material.py, so the film remains visible.
front = edit.get_material_property_input_node(material, unreal.MaterialProperty.MP_FRONT_MATERIAL)
if front:
    pins = {n.replace(' ', '').lower(): n for n in edit.get_material_expression_input_names(front)}
    for name, node in [('basecolor', color), ('roughness', rough), ('specular', spec), ('opacity', opacity)]:
        if name in pins:
            assert edit.connect_material_expressions(node, '', front, pins[name])
edit.layout_material_expressions(material)
edit.recompile_material(material)
assert assets.save_loaded_asset(material)
print('PALLET_WRAP_MATERIAL_READY')
