"""Blender: copy of the refined AGV FBX with mesh data scaled from metres to cm.
UE drops the unit conversion when meshes keep their own pivots, so the scale must live in the vertices.
Run by import_agv_refined.py: blender -b --factory-startup --python this.py -- <src.fbx> <dst.fbx>"""
import sys
import bpy
from mathutils import Matrix

src, dst = sys.argv[sys.argv.index('--') + 1:][:2]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)
scale = Matrix.Scale(100, 4)
done = set()
for obj in bpy.context.scene.objects:
    obj.location *= 100
    if obj.type == 'MESH' and obj.data.name not in done:
        obj.data.transform(scale)
        done.add(obj.data.name)
bpy.context.scene.unit_settings.scale_length = 0.01
bpy.ops.export_scene.fbx(filepath=dst, apply_unit_scale=True, global_scale=1.0, apply_scale_options='FBX_SCALE_NONE',
                         object_types={'EMPTY', 'MESH'}, use_mesh_modifiers=False, add_leaf_bones=False,
                         bake_anim=False, path_mode='STRIP')
print('AGV_REFINED_CM_READY')
