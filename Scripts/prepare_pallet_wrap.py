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
color.set_editor_property('default_value', unreal.LinearColor(.96, .965, .97))
assert edit.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
rough = constant(.42, unreal.MaterialProperty.MP_ROUGHNESS)
spec = constant(.4, unreal.MaterialProperty.MP_SPECULAR)
# A small diffuse-looking fill keeps shaded translucent faces milky, instead
# of reading as smoked glass. It is below 1 and does not replace scene lighting.
scatter_strength = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
scatter_strength.set_editor_property('parameter_name', 'FilmScatterFill')
scatter_strength.set_editor_property('group', 'Film')
scatter_strength.set_editor_property('default_value', .18)
scatter = edit.create_material_expression(material, unreal.MaterialExpressionMultiply)
assert edit.connect_material_expressions(color, '', scatter, 'A')
assert edit.connect_material_expressions(scatter_strength, '', scatter, 'B')
assert edit.connect_material_property(scatter, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
# UV0 travels with the sheet, including its spline deformation. World-position
# noise would slide across the plastic whenever the wrapped pallet is moved.
uv = edit.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate)
uv.set_editor_property('coordinate_index', 0)
film = edit.create_material_expression(material, unreal.MaterialExpressionCustom)
film.set_editor_property('code', '''
// Broad overlapping turns and fine creases stay milky white, while still
// admitting the contents' silhouette. Each sheet has opacity 0.78--0.88.
float overlapPhase = P.y * 56.54867 + sin(P.x * 12.0) * 0.22;
float overlap = pow(0.5 + 0.5 * cos(overlapPhase), 20.0);
float creasePhase = P.x * 125.0 + P.y * 15.0 + sin(P.y * 11.0) * 2.0;
float crease = pow(0.5 + 0.5 * sin(creasePhase), 18.0);
return 0.78 + 0.07 * overlap + 0.03 * crease;
''')
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
# A shallow tangent-space normal catches light on stretched folds without
# deforming the sheet, changing its outline, or adding a collision surface.
normal = edit.create_material_expression(material, unreal.MaterialExpressionCustom)
normal.set_editor_property('code', '''
float creasePhase = P.x * 125.0 + P.y * 15.0 + sin(P.y * 11.0) * 2.0;
float overlapPhase = P.y * 56.54867 + sin(P.x * 12.0) * 0.22;
float finePhase = P.x * 57.0 - P.y * 83.0;
float2 slope = float2(
    0.075 * cos(creasePhase) + 0.025 * sin(finePhase),
    0.055 * sin(overlapPhase) + 0.018 * cos(finePhase));
return normalize(float3(-slope, 1.0));
''')
normal.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
normal_input = unreal.CustomInput()
normal_input.set_editor_property('input_name', 'P')
normal.set_editor_property('inputs', [normal_input])
assert edit.connect_material_expressions(uv, '', normal, 'P')
assert edit.connect_material_property(normal, '', unreal.MaterialProperty.MP_NORMAL)
# Deleting expressions also removes Substrate's real surface. Setting the same
# blend mode again does not recreate it: legacy pins alone render nothing in5.8.
front = edit.create_material_expression(material, unreal.MaterialExpressionSubstrateShadingModels)
pins = {n.replace(' ', '').lower(): n for n in edit.get_material_expression_input_names(front)}
for name, node in [('basecolor', color), ('roughness', rough), ('specular', spec),
                   ('opacity', opacity), ('normal', normal), ('emissivecolor', scatter)]:
    assert name in pins, (name, pins)
    assert edit.connect_material_expressions(node, '', front, pins[name])
assert edit.connect_material_property(front, '', unreal.MaterialProperty.MP_FRONT_MATERIAL)
edit.layout_material_expressions(material)
edit.recompile_material(material)
assert assets.save_loaded_asset(material)
print('PALLET_WRAP_MATERIAL_READY')

# Native mesh overlay: a one-second white wash without replacing base/physics materials.
white_path = '/Game/Warehouse/Materials/M_PalletWrappingWhite'
white = unreal.load_asset(white_path) if assets.does_asset_exist(white_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    'M_PalletWrappingWhite', '/Game/Warehouse/Materials', unreal.Material, unreal.MaterialFactoryNew())
edit.delete_all_material_expressions(white)
white.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
white.set_editor_property('two_sided', True)
white.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
white_color = edit.create_material_expression(white, unreal.MaterialExpressionConstant3Vector)
white_color.set_editor_property('constant', unreal.LinearColor(1, 1, 1))
white_opacity = edit.create_material_expression(white, unreal.MaterialExpressionConstant)
white_opacity.set_editor_property('r', .88)
white_roughness = edit.create_material_expression(white, unreal.MaterialExpressionConstant)
white_roughness.set_editor_property('r', 1)
for node, prop in [(white_color, unreal.MaterialProperty.MP_BASE_COLOR),
                   (white_color, unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                   (white_opacity, unreal.MaterialProperty.MP_OPACITY),
                   (white_roughness, unreal.MaterialProperty.MP_ROUGHNESS)]:
    assert edit.connect_material_property(node, '', prop)
front = edit.create_material_expression(white, unreal.MaterialExpressionSubstrateShadingModels)
pins = {name.replace(' ', '').lower(): name for name in edit.get_material_expression_input_names(front)}
for name, node in [('basecolor', white_color), ('emissivecolor', white_color),
                   ('opacity', white_opacity), ('roughness', white_roughness)]:
    assert name in pins, (name, pins)
    assert edit.connect_material_expressions(node, '', front, pins[name])
assert edit.connect_material_property(front, '', unreal.MaterialProperty.MP_FRONT_MATERIAL)
edit.layout_material_expressions(white)
edit.recompile_material(white)
assert assets.save_loaded_asset(white)
print('PALLET_WRAPPING_WHITE_MATERIAL_READY')
