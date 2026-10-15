"""Run through Blender MCP on the original AGV scene; preserve it in the file.
Rigid part hierarchy, wheel revision, local-pivot FBX exports. Not a physics rig.
"""
import bpy
import math
import json
from pathlib import Path
from mathutils import Vector, Matrix

OUT=Path('F:/Works/Unreal/Projects/msc_vr/Art/Vehicles/OrangeAGV')
source=bpy.context.scene
assert source.name.startswith('Orange autonomous stacker'), 'Select the original AGV scene first'
assert not bpy.data.scenes.get('AGV - Unreal modular'), 'Revision already exists; do not duplicate it'
scene=bpy.data.scenes.new('AGV - Unreal modular')
scene.world=source.world.copy()
scene.unit_settings.system='METRIC'
scene.unit_settings.scale_length=1.0
parts=bpy.data.collections.new('AGV_MODULAR'); scene.collection.children.link(parts)
studio=bpy.data.collections.new('AGV_PRESENTATION'); scene.collection.children.link(studio)
original={}
for old in source.objects:
    obj=old.copy()
    if old.data: obj.data=old.data.copy()
    target=parts if any(c.name.startswith('AGV |') for c in old.users_collection) else studio
    target.objects.link(obj)
    obj['source_name']=old.name
    original[old.name]=obj
    if old==source.camera: scene.camera=obj
bpy.context.window.scene=scene

def material(name,color,metal=.2,rough=.4):
    m=bpy.data.materials.new(name); m.diffuse_color=(*color,1); m.use_nodes=True
    b=m.node_tree.nodes['Principled BSDF'];b.inputs['Base Color'].default_value=(*color,1)
    b.inputs['Metallic'].default_value=metal;b.inputs['Roughness'].default_value=rough
    return m

gold=material('AGV_Wheel_Polyurethane_Amber',(.72,.35,.025),.08,.46)
blue=material('AGV_Stabilizer_Polyurethane_Blue',(.025,.22,.58),.08,.46)
black=bpy.data.materials['Graphite powder-coated steel']
steel=bpy.data.materials['Machined steel']
rubber=bpy.data.materials['Black rubber']

def deselect():
    for o in bpy.context.view_layer.objects: o.select_set(False)

def finish(obj,name,mat):
    obj.name=name
    for c in list(obj.users_collection): c.objects.unlink(obj)
    parts.objects.link(obj)
    obj.data.materials.append(mat)
    return obj

def box(name,loc,dim,mat,bevel=.012):
    deselect(); bpy.ops.mesh.primitive_cube_add(size=1,location=loc)
    o=finish(bpy.context.object,name,mat);o.dimensions=dim
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bevel:
        m=o.modifiers.new('Edge radius','BEVEL');m.width=bevel;m.segments=3
        o.modifiers.new('Face normals','WEIGHTED_NORMAL')
    return o

def cyl(name,loc,radius,depth,mat,axis='X'):
    deselect(); bpy.ops.mesh.primitive_cylinder_add(vertices=48,radius=radius,depth=depth,location=loc)
    o=finish(bpy.context.object,name,mat)
    if axis=='X': o.rotation_euler.y=math.pi/2
    m=o.modifiers.new('Edge radius','BEVEL');m.width=.004;m.segments=3
    o.modifiers.new('Face normals','WEIGHTED_NORMAL')
    for p in o.data.polygons:p.use_smooth=True
    return o

def bake(obj):
    deselect();obj.select_set(True);bpy.context.view_layer.objects.active=obj
    bpy.ops.object.convert(target='MESH')
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    return bpy.context.object

def merge(objects,name,pivot):
    assert objects, name
    for key,value in list(original.items()):
        if value in objects: del original[key]
    converted=[bake(o) for o in objects]
    deselect()
    for o in converted:o.select_set(True)
    bpy.context.view_layer.objects.active=converted[0]
    bpy.ops.object.join()
    o=bpy.context.object;o.name=name
    scene.cursor.location=pivot
    bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    o['part_id']=name
    return o

def attach(child,parent):
    world=child.matrix_world.copy();child.parent=parent
    child.matrix_parent_inverse=Matrix.Identity(4)
    child.matrix_world=world
    bpy.context.view_layer.update()

def remove(obj): bpy.data.objects.remove(obj,do_unlink=True)

# Replace both former drive wheels and both single front rollers.
for name,obj in list(original.items()):
    if name.startswith(('Driven rubber wheel','Drive wheel hub','Front polyurethane load roller','Roller axle cap')):
        remove(obj); del original[name]

# Clearance pockets: central wheel can yaw without clipping the solid old base.
for name in ['Lower steel chassis','Rubber bumper skirt','Brushed perimeter bumper','Orange molded rear cover']:
    obj=bake(original[name]);original[name]=obj
    for x,y,z,dx,dy,dz in [(0,.40,.16,.58,.66,.49),(-.46,.72,.13,.25,.36,.30),(.46,.72,.13,.25,.36,.30)]:
        cutter=box('Temporary wheel well',(x,y,z),(dx,dy,dz),black,.025)
        bake(cutter)
        deselect();obj.select_set(True);bpy.context.view_layer.objects.active=obj
        mod=obj.modifiers.new('Wheel clearance','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
        bpy.ops.object.modifier_apply(modifier=mod.name)
        remove(cutter)

# Load rollers sit in the outrigger apertures, rather than intersecting the rails.
for name in ['Outrigger support leg','Outrigger support leg.001']:
    obj=original[name]
    cutter=box('Temporary tandem pocket',(obj.location.x,-1.40,.05),(.23,.57,.19),black,0)
    deselect();obj.select_set(True);bpy.context.view_layer.objects.active=obj
    mod=obj.modifiers.new('Tandem clearance','BOOLEAN');mod.operation='DIFFERENCE';mod.object=cutter
    bpy.ops.object.modifier_apply(modifier=mod.name);remove(cutter)

# Select parts by their original semantic names before joining the static chassis.
used=set()
def pick(prefixes):
    found=[o for n,o in original.items() if n.startswith(tuple(prefixes))]
    used.update(o.as_pointer() for o in found)
    return found

mast=merge(pick(['Inner sliding mast rail']), 'SM_AGV_MastInner',(0,-.27,.32))
ram=merge(pick(['Chrome lift ram']),'SM_AGV_LiftRam',(0,-.19,1.43))
pulley=merge(pick(['Lift chain pulley']),'SM_AGV_LiftPulley',(0,-.19,2.20))
chains=merge(pick(['Lift chain link']),'SM_AGV_LiftChains',(0,-.35,.4))
carriage=merge(pick(['Fork carriage','Load backrest guard']),'SM_AGV_LiftCarriage',(0,-.40,.40))
forks=[]
for sign,suffix in [(-1,'L'),(1,'R')]:
    members=[o for n,o in original.items() if n.startswith(('Forged fork blade','Tapered fork nose','Vertical fork heel','Carriage locking block')) and o.location.x*sign>0]
    used.update(o.as_pointer() for o in members)
    forks.append(merge(members,'SM_AGV_Fork_'+suffix,(sign*.31,-.325,.40)))
buttons=[]
for i,(n,o) in enumerate([(n,o) for n,o in original.items() if n.startswith('HMI pushbutton')]):
    used.add(o.as_pointer());buttons.append(merge([o],'SM_AGV_Button_HMI_'+str(i+1),o.location.copy()))
o=original['Emergency stop button'];used.add(o.as_pointer())
stop=merge([o],'SM_AGV_Button_EStop',o.location.copy())

# Central drive-and-steer unit: separate steering carrier and spinning wheel.
drive_y=.40;drive_z=.205
carrier=[cyl('Steering bearing',(0,drive_y,.435),.17,.06,steel,'Z'),
         cyl('Steering motor',(0,drive_y,.50),.095,.08,black,'Z')]
for x in [-.105,.105]: carrier.append(box('Drive fork plate',(x,drive_y,.305),(.035,.22,.25),black,.018))
carrier.append(cyl('Drive gear case',(.144,drive_y,drive_z),.12,.075,steel))
steer=merge(carrier,'SM_AGV_DriveSteer',(0,drive_y,.435))

def wheel(name,center,radius,width,mat):
    x,y,z=center
    members=[cyl('PU tread',center,radius,width,mat)]
    for sign in [-1,1]:
        xx=x+sign*(width/2+.004)
        members.append(cyl('Recessed wheel hub',(xx,y,z),radius*.67,.012,black))
        members.append(cyl('Axle cap',(xx+sign*.009,y,z),radius*.25,.012,steel))
        for j in range(6):
            a=j*math.tau/6
            members.append(cyl('Wheel hub bolt',(xx+sign*.010,y+radius*.45*math.sin(a),z+radius*.45*math.cos(a)),radius*.047,.008,steel))
    return merge(members,name,center)

drive=wheel('SM_AGV_Wheel_Drive',(0,drive_y,drive_z),.205,.17,gold)
supports=[];support_yokes=[]
for sign,suffix in [(-1,'L'),(1,'R')]:
    x=sign*.45;y=.72;z=.105
    members=[cyl('Caster swivel',(x,y,.255),.075,.035,steel,'Z')]
    for xx in [x-.054,x+.054]: members.append(box('Caster fork',(xx,y+.018,.175),(.018,.13,.16),black,.012))
    yoke=merge(members,'SM_AGV_CasterSwivel_'+suffix,(x,y,.255));support_yokes.append(yoke)
    supports.append(wheel('SM_AGV_Wheel_Stabilizer_'+suffix,(x,y+.028,z),.105,.085,blue))

bogies=[];rollers=[]
for sign,suffix in [(-1,'L'),(1,'R')]:
    x=sign*.31
    members=[box('Tandem rocker spine',(x,-1.40,.156),(.035,.46,.04),black,.006)]
    for xx in [x-.075,x+.075]: members.append(box('Tandem cheek plate',(xx,-1.40,.125),(.014,.44,.065),black,.01))
    bogie=merge(members,'SM_AGV_Bogie_'+suffix,(x,-1.40,.14));bogies.append(bogie)
    for j,y in enumerate([-1.55,-1.26]):
        rollers.append(wheel('SM_AGV_Wheel_Load_'+suffix+str(j+1),(x,y,.077),.077,.125,gold))

# All remaining stationary decoration, chassis, outer mast and sensors become one mesh.
# Merged source objects have been removed from bpy; use source membership names safely.
moving=[mast,ram,pulley,chains,carriage,*forks,*buttons,stop,steer,drive,*supports,*support_yokes,*bogies,*rollers]
static=[o for o in list(parts.objects) if o not in moving]
chassis=merge(static,'SM_AGV_Chassis',(0,0,0))
root=bpy.data.objects.new('AGV_ROOT',None);parts.objects.link(root);root.empty_display_type='ARROWS';root.empty_display_size=.35
attach(chassis,root)
for o in [mast,steer,*support_yokes,*bogies,*buttons,stop]:attach(o,chassis)
attach(ram,mast);attach(pulley,mast);attach(chains,mast);attach(carriage,mast)
for o in forks:attach(o,carriage)
attach(drive,steer)
for o,p in zip(supports,support_yokes):attach(o,p)
for i,o in enumerate(rollers):attach(o,bogies[i//2])

# Normalize the entire vehicle to X forward, Y right, Z up, in metres.
rot=Matrix.Rotation(math.pi/2,4,'Z')
worlds={o:rot@o.matrix_world.copy() for o in parts.objects}
for o in parts.objects:
    if o.type=='MESH':
        p=worlds[o].translation.copy()
        o.data.transform(Matrix.Translation(-p)@worlds[o])
        worlds[o]=Matrix.Translation(p)
for o in parts.objects:o.matrix_world=worlds[o]
bpy.context.view_layer.update()
# Canonical parent transforms with identity parent-inverse matrices.
positions={o:worlds[o].translation.copy() for o in parts.objects}
for o in parts.objects:
    o.matrix_parent_inverse=Matrix.Identity(4)
    o.rotation_euler=(0,0,0);o.scale=(1,1,1)
    o.location=positions[o]-(positions[o.parent] if o.parent else Vector((0,0,0)))
for o in studio.objects:o.matrix_world=rot@o.matrix_world
bpy.context.view_layer.update()

for o in parts.objects:
    o['joint_type']='fixed';o['motion_axis']='none'
for o in [drive,*supports,*rollers,pulley,*bogies]:o['joint_type']='revolute';o['motion_axis']='Y'
for o in [steer,*support_yokes]:o['joint_type']='revolute';o['motion_axis']='Z'
for o in [mast,carriage]:o['joint_type']='prismatic';o['motion_axis']='Z';o['preview_travel_m']=.45
for o in forks:o['joint_type']='prismatic_optional';o['motion_axis']='Y'
for o in buttons:o['joint_type']='prismatic';o['motion_axis']='X';o['press_travel_m']=.003
stop['joint_type']='prismatic';stop['motion_axis']='-Y';stop['press_travel_m']=.008
chains['note']='Separate visual chain. No chain circulation/deformation simulation.'
ram['note']='Follows inner mast; cylinder barrel is fixed in chassis.'
root['note']='Rigid component hierarchy, not a skeletal or physics rig. Estimated reference geometry.'
scene['wheel_layout']='1 central steer-drive + 2 stabilizer casters + 4 tandem load rollers'
scene['forward_axis']='+X';scene['right_axis']='+Y';scene['up_axis']='+Z'
scene.camera=next(o for o in studio.objects if o.get('source_name')=='Front three-quarter camera')
scene.render.engine='BLENDER_EEVEE';scene.eevee.use_gtao=True;scene.eevee.gtao_distance=3;scene.eevee.taa_render_samples=96
scene.eevee.use_soft_shadows=True;scene.render.resolution_x=1400;scene.render.resolution_y=1400;scene.render.resolution_percentage=100
scene.view_settings.view_transform='Filmic';scene.view_settings.look='Medium High Contrast'
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(OUT/'orange_agv_modular.png')
deselect();root.select_set(True);bpy.context.view_layer.objects.active=root
print(json.dumps({'scene':scene.name,'meshes':len([o for o in parts.objects if o.type=='MESH']),'wheels':[o.name for o in parts.objects if o.name.startswith('SM_AGV_Wheel')]}))
