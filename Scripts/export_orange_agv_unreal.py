"""Run after prepare_orange_agv_unreal.py through Blender MCP."""
import bpy
import json
import math
from pathlib import Path
from mathutils import Matrix, Vector

OUT=Path('F:/Works/Unreal/Projects/msc_vr/Art/Vehicles/OrangeAGV')
scene=bpy.context.scene
assert scene.name=='AGV - Unreal modular'
root=bpy.data.objects['AGV_ROOT']
objects=[o for o in scene.objects if o.name.startswith('SM_AGV_')]
assert len(objects)==24
assert len([o for o in objects if o.name.startswith('SM_AGV_Wheel_')])==7

def update():
    root.update_tag();bpy.context.view_layer.update();scene.frame_set(scene.frame_current)

for key,value,lo,hi in [('steer_deg',0.0,-90,90),('travel_m',0.0,-10,10),('lift_m',0.0,0,1),('caster_L_deg',0.0,-180,180),('caster_R_deg',0.0,-180,180),('estop_press',0.0,0,1),('hmi_1_press',0.0,0,1),('hmi_2_press',0.0,0,1),('hmi_3_press',0.0,0,1)]:
    root[key]=value;root.id_properties_ui(key).update(min=lo,max=hi)

def driver(o,path,index,prop,expression):
    d=o.driver_add(path,index).driver;d.type='SCRIPTED'
    v=d.variables.new();v.name='v';v.type='SINGLE_PROP';v.targets[0].id=root;v.targets[0].data_path='["'+prop+'"]'
    d.expression=expression

driver(bpy.data.objects['SM_AGV_DriveSteer'],'rotation_euler',2,'steer_deg','v*pi/180')
for suffix in ['L','R']:
    driver(bpy.data.objects['SM_AGV_CasterSwivel_'+suffix],'rotation_euler',2,'caster_'+suffix+'_deg','v*pi/180')
for o in objects:
    if o.name.startswith('SM_AGV_Wheel_'):
        r=.205 if o.name.endswith('Drive') else (.105 if 'Stabilizer' in o.name else .077)
        o['radius_m']=r;driver(o,'rotation_euler',1,'travel_m',f'v/{r}')
for name in ['SM_AGV_MastInner','SM_AGV_LiftCarriage']:
    o=bpy.data.objects[name];driver(o,'location',2,'lift_m',f'{o.location.z!r}+v*0.5')
o=bpy.data.objects['SM_AGV_Button_EStop'];driver(o,'location',1,'estop_press',f'{o.location.y!r}-v*0.008')
for i in range(1,4):
    o=bpy.data.objects['SM_AGV_Button_HMI_'+str(i)];driver(o,'location',0,'hmi_'+str(i)+'_press',f'{o.location.x!r}+v*0.003')
update()

rest={o.name:o.matrix_world.copy() for o in objects}
for name,pivot in {
    'SM_AGV_Wheel_Drive':(-.40,0,.205),
    'SM_AGV_Wheel_Stabilizer_L':(-.748,-.45,.105),
    'SM_AGV_Wheel_Stabilizer_R':(-.748,.45,.105),
    'SM_AGV_Wheel_Load_L1':(1.55,-.31,.077),
    'SM_AGV_Wheel_Load_L2':(1.26,-.31,.077),
    'SM_AGV_Wheel_Load_R1':(1.55,.31,.077),
    'SM_AGV_Wheel_Load_R2':(1.26,.31,.077),
    'SM_AGV_Fork_L':(.325,-.31,.40),
    'SM_AGV_Fork_R':(.325,.31,.40),
}.items():
    assert (rest[name].translation-Vector(pivot)).length<1e-5,(name,rest[name].translation)
root['lift_m']=.8;root['steer_deg']=35;root['travel_m']=.25;root['estop_press']=1;update()
fork=bpy.data.objects['SM_AGV_Fork_L']
assert abs(fork.matrix_world.translation.z-rest[fork.name].translation.z-.8)<1e-5
assert abs(bpy.data.objects['SM_AGV_DriveSteer'].rotation_euler.z-math.radians(35))<1e-5
assert abs(bpy.data.objects['SM_AGV_Wheel_Load_L1'].rotation_euler.y-.25/.077)<1e-5
assert abs(bpy.data.objects['SM_AGV_Button_EStop'].matrix_world.translation.y-rest['SM_AGV_Button_EStop'].translation.y+.008)<1e-5
assert (bpy.data.objects['SM_AGV_Chassis'].matrix_world.translation-rest['SM_AGV_Chassis'].translation).length<1e-8
for key in ['lift_m','steer_deg','travel_m','estop_press']:root[key]=0.0
update()

manifest={'units':'centimeters','axes':'X forward, Y right, Z up','rig':'rigid static-mesh hierarchy; Blender drivers are preview controls, not FBX physics constraints','parts':[]}
materials={}
for o in objects:
    assert o.type=='MESH' and len(o.data.vertices)>0
    assert max(abs(s-1) for s in o.scale)<1e-6
    assert o.matrix_parent_inverse==Matrix.Identity(4)
    o.data.calc_loop_triangles()
    manifest['parts'].append({'name':o.name,'parent':o.parent.name if o.parent else None,'relative_location_cm':[round(v*100,6) for v in o.location],'world_pivot_cm':[round(v*100,6) for v in o.matrix_world.translation],'joint_type':o.get('joint_type'),'motion_axis':o.get('motion_axis'),'triangles':len(o.data.loop_triangles),'radius_cm':o.get('radius_m',0)*100,'fbx':'UnrealParts/'+o.name+'.fbx'})
    for m in o.data.materials:
        if m and m.use_nodes:
            n=m.node_tree.nodes.get('Principled BSDF')
            if n:materials[m.name]={'base_color':list(n.inputs['Base Color'].default_value),'metallic':n.inputs['Metallic'].default_value,'roughness':n.inputs['Roughness'].default_value,'emission_color':list(n.inputs['Emission'].default_value),'emission_strength':n.inputs['Emission Strength'].default_value}
manifest['materials']=materials
manifest['validation']={'wheel_count':7,'mesh_count':24,'lift_test_m':.8,'steer_test_degrees':35,'roll_test_distance_m':.25,'button_test_m':.008,'passed':True}

def deselect():
    for o in bpy.context.view_layer.objects:o.select_set(False)

def fbx(path):
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH','EMPTY'},global_scale=1.0,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',use_mesh_modifiers=True,mesh_smooth_type='FACE',bake_anim=False,add_leaf_bones=False,use_custom_props=True)

# Each single-part file is baked at zero translation, so import cannot swallow its pivot.
for obj in objects:
    temp=obj.copy();temp.data=obj.data.copy();temp.animation_data_clear();temp.parent=None
    scene.collection.objects.link(temp);temp.matrix_world=Matrix.Identity(4)
    deselect();temp.select_set(True);bpy.context.view_layer.objects.active=temp
    fbx(OUT/'UnrealParts'/(obj.name+'.fbx'))
    data=temp.data;bpy.data.objects.remove(temp,do_unlink=True);bpy.data.meshes.remove(data)
deselect();root.select_set(True)
for o in objects:o.select_set(True)
bpy.context.view_layer.objects.active=root
fbx(OUT/'orange_agv_assembly.fbx')
(OUT/'assembly_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'validation.json').write_text(json.dumps(manifest['validation'],indent=2),encoding='utf-8')

# Original source scene and its current unsaved state are retained in this new file.
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'orange_agv_unreal.blend'))
scene.render.filepath=str(OUT/'orange_agv_modular.png')
bpy.ops.render.render(write_still=True)
print(json.dumps(manifest['validation']))
