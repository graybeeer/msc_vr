"""Export a provided Poly Haven Blender model to FBX and a local 4K import manifest.

Run with Blender --background --factory-startup --disable-autoexec --python this_file
-- ASSET_ID INPUT_BLEND_OR_FBX TEXTURE_DIRECTORY. The original input is never saved.
"""
import hashlib
import json
import shutil
import sys
from pathlib import Path

import bpy


def export_local_cargo(asset, source, textures):
    assert asset in ('cardboard_box_01', 'plastic_crate_01'), asset
    root = Path(__file__).resolve().parents[1]
    folder = root / 'SourceAssets/Cargo/PolyHaven' / asset
    folder.mkdir(parents=True, exist_ok=True)
    scene = bpy.context.scene
    mesh_file = folder / (asset + '_4k.fbx')
    if source.suffix.lower() == '.fbx':
        # Texture resolution does not change the mesh; reuse an existing FBX of the same model.
        shutil.copy2(source, mesh_file)
        objects = []
    else:
        bpy.ops.wm.open_mainfile(filepath=str(source), load_ui=False, use_scripts=False)
        scene = bpy.context.scene
        objects = [o for o in scene.objects if o.type == 'MESH']
        assert len(objects) == 1, [(o.name, o.type) for o in scene.objects]
        bpy.ops.object.select_all(action='DESELECT')
        objects[0].select_set(True)
        bpy.context.view_layer.objects.active = objects[0]
        bpy.ops.export_scene.fbx(filepath=str(mesh_file), use_selection=True,
                             object_types={'MESH'}, use_mesh_modifiers=True,
                             apply_unit_scale=True, axis_forward='-Y', axis_up='Z',
                             bake_anim=False, add_leaf_bones=False, path_mode='STRIP')
    manifest = {'asset': asset, 'source_mesh': str(source),
                'texture_directory': str(textures), 'normal_format': 'OpenGL', 'files': {}}

    def record(kind, path):
        manifest['files'][kind] = {'filename': path.name,
                                  'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}

    record('mesh', mesh_file)
    albedo = folder / (asset + '_diff_4k.jpg')
    shutil.copy2(textures / albedo.name, albedo)
    record('albedo', albedo)
    # Raw view transform keeps normal/roughness/metallic data free of display transforms.
    scene.view_settings.view_transform = 'Raw'
    scene.view_settings.look = 'None'
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.color_depth = '16'
    for kind, suffix in [('normal', 'nor_gl'), ('roughness', 'rough'), ('metallic', 'metal')]:
        source_image = textures / f'{asset}_{suffix}_4k.exr'
        if not source_image.exists():
            assert kind == 'metallic', source_image
            continue
        image = bpy.data.images.load(str(source_image), check_existing=False)
        image.colorspace_settings.name = 'Non-Color'
        output = folder / f'{asset}_{suffix}_4k.png'
        image.save_render(str(output), scene=scene)
        record(kind, output)
        bpy.data.images.remove(image)
    if objects:
        manifest['dimensions_m'] = list(objects[0].dimensions)
    (folder / 'source_4k.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print('LOCAL_CARGO_EXPORTED', asset, mesh_file)


if __name__ == '__main__':
    args = sys.argv[sys.argv.index('--') + 1:]
    assert len(args) == 3, 'Expected ASSET_ID INPUT_BLEND TEXTURE_DIRECTORY'
    export_local_cargo(args[0], Path(args[1]).resolve(), Path(args[2]).resolve())
