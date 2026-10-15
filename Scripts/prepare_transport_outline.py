"""Build the owned transport-target postprocess material (run with -RenderOffscreen).

Stencils 192..254 (outlines) and 128..190 (arrows) belong to the local visualizer.
Visible silhouettes only: objects behind walls do not reveal warehouse contents.
"""
import unreal

PATH = '/Game/Warehouse/Materials/M_TransportOutline'
edit = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
material = unreal.load_asset(PATH) if assets.does_asset_exist(PATH) else None
if not material:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_TransportOutline', '/Game/Warehouse/Materials', unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
material.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
edit.delete_all_material_expressions(material)

# Connected SceneTexture nodes declare the shader's scene texture dependencies.
custom = edit.create_material_expression(material, unreal.MaterialExpressionCustom)
names = ('SceneColor', 'StencilDependency', 'DepthDependency', 'SceneDepthDependency')
custom_inputs = []
for name in names:
    item = unreal.CustomInput()
    item.set_editor_property('input_name', name)
    custom_inputs.append(item)
custom.set_editor_property('inputs', custom_inputs)
custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
for name, texture_id in zip(names, (
        unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, unreal.SceneTextureId.PPI_CUSTOM_STENCIL,
        unreal.SceneTextureId.PPI_CUSTOM_DEPTH, unreal.SceneTextureId.PPI_SCENE_DEPTH)):
    node = edit.create_material_expression(material, unreal.MaterialExpressionSceneTexture)
    node.set_editor_property('scene_texture_id', texture_id)
    assert edit.connect_material_expressions(node, 'Color', custom, name)

# Engine MaterialSceneTextureId.h: SceneDepth=1, CustomDepth=13, CustomStencil=25.
# Buffer UVs (not raw viewport UVs) keep edges aligned under screen percentage / TSR.
custom.set_editor_property('code', r'''
float2 uv = GetDefaultSceneTextureUV(Parameters, 25);
float2 pixel = GetSceneTextureBufferSize(25).zw;
float stencil = SceneTextureLookup(uv, 25, false).r;
float center = stencil >= 192.0 && stencil <= 254.0;
float depthHere = SceneTextureLookup(GetDefaultSceneTextureUV(Parameters, 1), 1, false).r;
float arrow = stencil >= 128.0 && stencil <= 190.0 && DepthDependency.r <= depthHere + 1.0;
float edge = 0.0, index = stencil - 128.0, closest = 1.e20;
for (int y = -1; y <= 1; ++y)
for (int x = -1; x <= 1; ++x)
{
    float2 at = uv + float2(x, y) * pixel * 2.0;
    float neighbor = SceneTextureLookup(at, 25, false).r;
    float selected = neighbor >= 192.0 && neighbor <= 254.0;
    float targetDepth = SceneTextureLookup(at, 13, false).r;
    float sceneDepth = SceneTextureLookup(at, 1, false).r;
    float visible = targetDepth <= sceneDepth + 1.0 && targetDepth <= depthHere + 5.0;
    if (!arrow && selected * visible > 0.5 && targetDepth < closest)
    { edge = 1.0; index = neighbor - 192.0; closest = targetDepth; }
}
// Shared palette: first green, second sky blue, then distinct golden-angle hues.
float hue = frac((index - 2.0) * 0.61803398875 + 0.08);
float3 color = lerp(float3(0.12,0.12,0.12), saturate(abs(frac(hue + float3(0,2.0/3.0,1.0/3.0))*6.0-3.0)-1.0), 0.88);
if (index < 0.5) color = float3(0.08, 1.0, 0.18);
else if (index < 1.5) color = float3(0.12, 0.68, 1.0);
return lerp(SceneColor.rgb, color, max(arrow, edge * (1.0 - center)));
''')
assert edit.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
edit.layout_material_expressions(material)
edit.recompile_material(material)
assert assets.save_loaded_asset(material)

arrow_path = '/Game/Warehouse/Materials/M_RouteArrow'
arrow = unreal.load_asset(arrow_path) if assets.does_asset_exist(arrow_path) else None
if not arrow:
    arrow = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_RouteArrow', '/Game/Warehouse/Materials', unreal.Material, unreal.MaterialFactoryNew())
edit.set_material_usage(arrow, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
edit.delete_all_material_expressions(arrow)
white = edit.create_material_expression(arrow, unreal.MaterialExpressionConstant3Vector)
white.set_editor_property('constant', unreal.LinearColor(1, 1, 1, 1))
surface = edit.create_material_expression(arrow, unreal.MaterialExpressionSubstrateUnlitBSDF)
emissive_pin = next(name for name in edit.get_material_expression_input_names(surface)
                    if name.replace(' ', '').lower() == 'emissivecolor')
assert edit.connect_material_expressions(white, '', surface, emissive_pin)
assert edit.connect_material_property(surface, '', unreal.MaterialProperty.MP_FRONT_MATERIAL)
edit.layout_material_expressions(arrow)
edit.recompile_material(arrow)
assert assets.save_loaded_asset(arrow)
print('TRANSPORT_OUTLINE_MATERIAL_READY', PATH)
if '-unattended' in unreal.SystemLibrary.get_command_line().lower():
    unreal.SystemLibrary.quit_editor()
