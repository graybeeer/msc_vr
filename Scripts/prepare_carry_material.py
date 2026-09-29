"""Create the held-cargo fade material without changing Fab sources or the map.

Run with -RenderOffscreen, not -nullrhi, so Unreal compiles the rendering shaders.
"""
import unreal

path = '/Game/Warehouse/Materials/M_CarryTransparent'
assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
if not assets.does_asset_exist(path):
    texture = unreal.load_asset('/Game/Scene_Warehouse/Assets/MS/3D/Ind_War_Storage_Box_Cardboard_Worn_02/T_Ind_War_Storage_Box_Cardboard_Worn_02_D')
    assert texture, 'Install Scene_Warehouse first'
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_CarryTransparent','/Game/Warehouse/Materials',unreal.Material,unreal.MaterialFactoryNew())
    material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    # Keep large cargo visible even if the camera enters its bounds.
    material.set_editor_property('two_sided',True)
    albedo = editing.create_material_expression(material,unreal.MaterialExpressionTextureSampleParameter2D)
    albedo.set_editor_property('parameter_name','Albedo')
    albedo.set_editor_property('texture',texture)
    albedo.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_VIRTUAL_COLOR if texture.get_editor_property('virtual_texture_streaming') else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    uv = editing.create_material_expression(material,unreal.MaterialExpressionTextureCoordinate)
    scale = editing.create_material_expression(material,unreal.MaterialExpressionScalarParameter)
    scale.set_editor_property('parameter_name','SamplingScale')
    scale.set_editor_property('default_value',1.0)
    multiply = editing.create_material_expression(material,unreal.MaterialExpressionMultiply)
    editing.connect_material_expressions(uv,'',multiply,'A')
    editing.connect_material_expressions(scale,'',multiply,'B')
    editing.connect_material_expressions(multiply,'',albedo,'Coordinates')
    editing.connect_material_property(albedo,'RGB',unreal.MaterialProperty.MP_BASE_COLOR)
    opacity = editing.create_material_expression(material,unreal.MaterialExpressionScalarParameter)
    opacity.set_editor_property('parameter_name','CarryOpacity')
    opacity.set_editor_property('default_value',.3)
    editing.connect_material_property(opacity,'',unreal.MaterialProperty.MP_OPACITY)
    roughness = editing.create_material_expression(material,unreal.MaterialExpressionConstant)
    roughness.set_editor_property('r',.75)
    editing.connect_material_property(roughness,'',unreal.MaterialProperty.MP_ROUGHNESS)
    editing.layout_material_expressions(material)
    editing.recompile_material(material)
material = unreal.load_asset(path)
# Changing blend mode creates a Substrate front graph before the legacy pins are wired.
# Connect the real surface inputs too; otherwise the generated empty surface stays invisible.
front = editing.get_material_property_input_node(material,unreal.MaterialProperty.MP_FRONT_MATERIAL)
if front:
    inputs = {name.replace(' ','').lower(): name for name in editing.get_material_expression_input_names(front)}
    for name, prop, output in [('basecolor',unreal.MaterialProperty.MP_BASE_COLOR,'RGB'),('roughness',unreal.MaterialProperty.MP_ROUGHNESS,''),('opacity',unreal.MaterialProperty.MP_OPACITY,'')]:
        source = editing.get_material_property_input_node(material,prop)
        assert source and name in inputs, (name,inputs)
        assert editing.connect_material_expressions(source,output,front,inputs[name])
material.set_editor_property('two_sided',True)
# Temporal coverage fade preserves opaque lighting and first-person rendering while revealing the view.
material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
mask = editing.get_material_property_input_node(material,unreal.MaterialProperty.MP_OPACITY_MASK)
if not mask:
    mask = editing.create_material_expression(material,unreal.MaterialExpressionMaterialFunctionCall)
    mask.set_material_function(unreal.load_asset('/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'))
    opacity = editing.get_material_property_input_node(material,unreal.MaterialProperty.MP_OPACITY)
    assert editing.connect_material_expressions(opacity,'',mask,'Alpha Threshold')
    assert editing.connect_material_property(mask,'',unreal.MaterialProperty.MP_OPACITY_MASK)
editing.recompile_material(material)
assert assets.save_loaded_asset(material)
print('CARRY_TRANSPARENT_MATERIAL_READY')
