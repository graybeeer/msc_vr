"""Export and check the counterbalance revision via Blender MCP."""
import bpy,json,math
from pathlib import Path
from mathutils import Matrix
OUT=Path('F:/Works/Unreal/Projects/msc_vr/Art/Vehicles/OrangeAGV/OriginalProportions')
(OUT/'UnrealParts').mkdir(exist_ok=True)
scene=bpy.context.scene;assert scene.name=='AGV - OriginalProportions revision'
root=bpy.data.objects['AGV_ROOT'];objects=[o for o in scene.objects if o.name.startswith('SM_AGV_')]
assert len(objects)==16
assert len([o for o in objects if o.name.startswith('SM_AGV_Wheel_')])==3
def update():
    root.update_tag();bpy.context.view_layer.update();scene.frame_set(scene.frame_current)
fork=bpy.data.objects['SM_AGV_Fork_L'];update();rest=fork.matrix_world.translation.copy()
root['lift_m']=.65;root['steer_deg']=30;root['travel_m']=.2;update()
assert abs(fork.matrix_world.translation.z-rest.z-.65)<1e-5
assert abs(bpy.data.objects['SM_AGV_DriveSteer'].rotation_euler.z-math.pi/6)<1e-5
assert abs(bpy.data.objects['SM_AGV_Wheel_Support_L'].rotation_euler.y-.2/.105)<1e-5
for k in ['lift_m','steer_deg','travel_m']:root[k]=0.0
update()
manifest={'units':'centimeters','axes':'X forward, Y right, Z up','reference':'User supplied counterbalance example; dimensions approximate, not OEM validated','parts':[],'validation':{'mesh_count':16,'wheel_count':3,'lift_test_m':.65,'steer_test_deg':30,'passed':True,'unreal_import_tested':False}}
for o in objects:
    o.data.calc_loop_triangles()
    manifest['parts'].append({'name':o.name,'parent':o.parent.name,'relative_location_cm':[v*100 for v in o.location],'world_pivot_cm':[v*100 for v in o.matrix_world.translation],'triangles':len(o.data.loop_triangles),'joint_type':o.get('joint_type'),'motion_axis':o.get('motion_axis'),'fbx':'UnrealParts/'+o.name+'.fbx'})
def deselect():
    for o in bpy.context.view_layer.objects:o.select_set(False)
def fbx(path):
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH','EMPTY'},global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',use_mesh_modifiers=True,mesh_smooth_type='FACE',bake_anim=False,add_leaf_bones=False,use_custom_props=True)
for obj in objects:
    t=obj.copy();t.data=obj.data.copy();t.animation_data_clear();t.parent=None;scene.collection.objects.link(t);t.matrix_world=Matrix.Identity(4)
    deselect();t.select_set(True);bpy.context.view_layer.objects.active=t;fbx(OUT/'UnrealParts'/(obj.name+'.fbx'))
    data=t.data;bpy.data.objects.remove(t,do_unlink=True);bpy.data.meshes.remove(data)
deselect();root.select_set(True)
for o in objects:o.select_set(True)
bpy.context.view_layer.objects.active=root;fbx(OUT/'orange_agv_refined.fbx')
(OUT/'assembly_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
scene.camera=bpy.data.objects['Front three-quarter camera.001']
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.overlay.show_overlays=False
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'orange_agv_refined.blend'))
print(json.dumps(manifest['validation']))
