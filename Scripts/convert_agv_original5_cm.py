"""Blender: rigid part groups of the original orange AGV (SourceAssets/OrangeAGV/orange_agv.fbx, not modified) for the
five-wheel vehicle, in cm and in the vehicle frame (forks along +X), each with its own pivot. The original has no
central drive wheel; the drive unit comes from the refined model. Floor marks and the ground plane are dropped.
Run by import_agv_original5.py: blender -b --factory-startup --python this.py -- <src.fbx> <dst.fbx>"""
import sys
import bpy
from mathutils import Matrix, Vector

src, dst = sys.argv[sys.argv.index('--') + 1:][:2]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)

# Original: forks along -Y in metres. Vehicle frame: forks along +X, cm (UE flips Y on import, so +Y here is UE -Y).
TO_VEHICLE = Matrix.Scale(100, 4) @ Matrix.Rotation(1.5707963267948966, 4, 'Z')

# Group by part name; side by the part's position across the vehicle. Pivots of the wheel groups are wheel centres.
GROUPS = [
    ('LiftStage', ('Inner sliding mast', 'Chrome lift ram', 'Lift chain')),
    ('Carriage', ('Fork carriage', 'Load backrest', 'Carriage locking', 'Vertical fork heel')),
    ('Forks', ('Forged fork blade', 'Tapered fork nose')),
    ('RearWheel', ('Driven rubber wheel', 'Drive wheel hub')),
    ('LoadRoller', ('Front polyurethane load roller', 'Roller axle cap')),
]
SIDED = {'RearWheel', 'LoadRoller'}
WHEEL_CENTRES = {'RearWheel': 'Driven rubber wheel', 'LoadRoller': 'Front polyurethane load roller'}

parts = {}
for obj in list(bpy.context.scene.objects):
    if obj.type != 'MESH' or obj.name.startswith(('Ground', 'Blue floor')):
        bpy.data.objects.remove(obj, do_unlink=True)
        continue
    world = TO_VEHICLE @ obj.matrix_world
    obj.data = obj.data.copy()
    obj.data.transform(world)
    obj.matrix_world = Matrix.Identity(4)
    base = obj.name.split('.')[0]
    group = next((name for name, prefixes in GROUPS if base.startswith(prefixes)), 'Body')
    parts.setdefault(group, []).append(obj)

# Wheel sides: UE +Y (= -Y here) is L. Axle caps go with the nearest wheel.
def centre(obj):
    pts = [obj.matrix_world @ Vector(v.co) for v in obj.data.vertices]
    return sum(pts, Vector()) / len(pts)

result = {}
for group, objs in parts.items():
    if group not in SIDED:
        result[group] = (objs, Vector((0, 0, 0)))
        continue
    wheels = [o for o in objs if o.name.startswith(WHEEL_CENTRES[group])]
    for wheel in wheels:
        side = 'L' if centre(wheel).y < 0 else 'R'
        members = [o for o in objs if min(wheels, key=lambda w: (centre(w) - centre(o)).length) is wheel]
        pts = [v.co for v in wheel.data.vertices]
        mid = Vector([(min(p[i] for p in pts) + max(p[i] for p in pts)) / 2 for i in range(3)])
        result[group + side] = (members, mid)

bpy.ops.object.select_all(action='DESELECT')
for name, (objs, pivot) in result.items():
    for obj in objs:
        obj.data.transform(Matrix.Translation(-pivot))
    target = objs[0]
    if len(objs) > 1:
        with bpy.context.temp_override(active_object=target, selected_editable_objects=objs, selected_objects=objs):
            bpy.ops.object.join()
    target.name = 'SM_AGV5_' + name
    target.data.name = target.name
    # Pivot printed in UE coordinates (Y flipped) for attaching the component.
    print('AGV5_PART %s pivot_ue=(%.2f, %.2f, %.2f)' % (target.name, pivot.x, -pivot.y, pivot.z))

bpy.context.scene.unit_settings.scale_length = 0.01
bpy.ops.export_scene.fbx(filepath=dst, apply_unit_scale=True, global_scale=1.0, apply_scale_options='FBX_SCALE_NONE',
                         object_types={'MESH'}, use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False,
                         path_mode='STRIP')
print('AGV5_CM_READY')
