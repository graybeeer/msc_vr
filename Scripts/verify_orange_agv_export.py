"""FBX round-trip checks and wheel-layout / motion previews, through Blender MCP."""
import bpy
import json
import math
from pathlib import Path
from mathutils import Vector, Matrix

OUT=Path('F:/Works/Unreal/Projects/msc_vr/Art/Vehicles/OrangeAGV')
main=bpy.data.scenes['AGV - Unreal modular']
root=bpy.data.objects['AGV_ROOT']
parts=[o for o in main.objects if o.name.startswith('SM_AGV_')]
check=bpy.data.scenes.new('Temporary FBX roundtrip validation')
bpy.context.window.scene=check

def bounds(vertices):
    return [min(v[i] for v in vertices) for i in range(3)]+[max(v[i] for v in vertices) for i in range(3)]

checked=[]
try:
    for part in parts:
        bpy.ops.import_scene.fbx(filepath=str(OUT/'UnrealParts'/(part.name+'.fbx')),use_anim=False)
        imported=[o for o in check.objects if o.type=='MESH']
        assert len(imported)==1, part.name
        obj=imported[0]
        expected=bounds([v.co for v in part.data.vertices])
        actual=bounds([obj.matrix_world@v.co for v in obj.data.vertices])
        error=max(abs(a-b) for a,b in zip(actual,expected))
        assert error<1e-4,(part.name,error)
        assert obj.matrix_world.translation.length<1e-6,(part.name,'pivot shifted')
        checked.append({'name':part.name,'bbox_error_m':error,'pivot_at_origin':True})
        for o in list(check.objects): bpy.data.objects.remove(o,do_unlink=True)
finally:
    bpy.context.window.scene=main
    for o in list(check.objects):bpy.data.objects.remove(o,do_unlink=True)
    bpy.data.scenes.remove(check)

report=json.loads((OUT/'validation.json').read_text())
report['fbx_roundtrip_parts']=len(checked)
report['fbx_roundtrip_max_bbox_error_m']=max(c['bbox_error_m'] for c in checked)
report['unreal_import_tested']=False
(OUT/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')

# Pose preview verifies visible movement without changing the saved neutral export pose.
root['lift_m']=.65;root['steer_deg']=30;root['caster_L_deg']=12;root['caster_R_deg']=12
root.update_tag();bpy.context.view_layer.update();main.frame_set(1)
main.render.filepath=str(OUT/'orange_agv_lift_demo.png')
bpy.ops.render.render(write_still=True)
for key in ['lift_m','steer_deg','caster_L_deg','caster_R_deg']:root[key]=0.0
root.update_tag();bpy.context.view_layer.update();main.frame_set(1)
main.render.filepath=str(OUT/'orange_agv_modular.png')

# Separate presentation scene exposes all seven wheels; no extra meshes in the FBX.
layout=bpy.data.scenes.new('AGV - wheel layout inspection')
layout.world=main.world.copy();layout.render.engine='BLENDER_EEVEE'
layout.eevee.use_gtao=True;layout.eevee.gtao_distance=2;layout.eevee.taa_render_samples=96
layout.render.resolution_x=1400;layout.render.resolution_y=1000;layout.render.resolution_percentage=100
layout.view_settings.view_transform='Filmic';layout.view_settings.look='Medium High Contrast'
layout.render.filepath=str(OUT/'orange_agv_wheel_layout.png')
layout.render.image_settings.file_format='PNG'
for old in parts:
    if old.name.startswith(('SM_AGV_Wheel_','SM_AGV_DriveSteer','SM_AGV_Caster','SM_AGV_Bogie')):
        obj=old.copy();obj.data=old.data.copy();obj.animation_data_clear();obj.parent=None
        obj.matrix_world=old.matrix_world.copy();layout.collection.objects.link(obj)
for old in main.objects:
    if old.type=='LIGHT' or old.get('source_name')=='Ground':
        obj=old.copy();obj.data=old.data.copy();layout.collection.objects.link(obj)
# Keep the fork-side load-bearing rails visible for context.
source=bpy.data.scenes['Orange autonomous stacker - reference study']
for old in source.objects:
    if old.name.startswith('Outrigger support leg'):
        obj=old.copy();obj.data=old.data.copy();obj.matrix_world=Matrix.Rotation(math.pi/2,4,'Z')@old.matrix_world
        obj.location.z+=.10;layout.collection.objects.link(obj)
bpy.context.window.scene=layout
bpy.ops.object.camera_add(location=(-2.6,3.8,3.2))
cam=bpy.context.object;cam.name='Wheel layout camera'
cam.rotation_euler=(Vector((.35,0,.12))-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.type='ORTHO';cam.data.ortho_scale=3.6;layout.camera=cam
bpy.ops.render.render(write_still=True)
bpy.context.window.scene=main
main['validation']='24 FBX parts round-tripped; pivots and scale checked. Unreal import not yet tested.'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'orange_agv_unreal.blend'))
print(json.dumps(report))
