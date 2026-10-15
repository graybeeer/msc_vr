"""Blender MCP: revise the saved modular model against user counterbalance references."""
import bpy,bmesh,math,json
from pathlib import Path
from mathutils import Vector,Matrix
OUT=Path('F:/Works/Unreal/Projects/msc_vr/Art/Vehicles/OrangeAGV/OriginalProportions')
OUT.mkdir(exist_ok=True)
s=bpy.context.scene
assert s.name=='AGV - Unreal modular'
s.name='AGV - OriginalProportions revision'
root=bpy.data.objects['AGV_ROOT']
for k in ['lift_m','travel_m','steer_deg','caster_L_deg','caster_R_deg','estop_press','hmi_1_press','hmi_2_press','hmi_3_press']:root[k]=0.0
root.update_tag();bpy.context.view_layer.update();s.frame_set(s.frame_current)
parts=[o for o in s.objects if o.name.startswith('SM_AGV_')]
worlds={o:o.matrix_world.copy() for o in parts}
for o in parts:o.animation_data_clear()
ch=bpy.data.objects['SM_AGV_Chassis']
bm=bmesh.new();bm.from_mesh(ch.data);seen=set();remove=[]
for v in list(bm.verts):
    if v in seen:continue
    stack=[v];seen.add(v);vs=[]
    while stack:
        a=stack.pop();vs.append(a)
        for e in a.link_edges:
            b=e.other_vert(a)
            if b not in seen:seen.add(b);stack.append(b)
    lo=[min(v.co[i] for v in vs) for i in range(3)];hi=[max(v.co[i] for v in vs) for i in range(3)]
    if hi[2]<.32 or (lo[0]<-.64 and hi[0]<-.63 and hi[2]<.36):
        remove.extend(vs);continue
    # Recessed side covers become compact projecting equipment boxes; labels move with them.
    if (lo[1]>.40 or hi[1]<-.40) and lo[0]>-.51 and hi[0]<.23 and lo[2]>.32 and hi[2]<1.24:
        sign=1 if lo[1]>0 else -1
        for a in vs:
            a.co.x=(a.co.x+.14)*.85-.08
            a.co.z=1.18+(a.co.z-1.18)*.78
            a.co.y=sign*(.42+(abs(a.co.y)-.407)*1.8)
bmesh.ops.delete(bm,geom=remove,context='VERTS');bm.to_mesh(ch.data);bm.free()
drop=[o for o in parts if any(t in o.name for t in ['Bogie','Wheel_Load','CasterSwivel'])]
for o in drop:bpy.data.objects.remove(o,do_unlink=True)
parts=[o for o in s.objects if o.name.startswith('SM_AGV_')]
# Resize the upper structure while preserving zero rotations and unit scales.
S=Matrix.Identity(4)
positions={}
for o in parts:
    w=worlds[o];p=(S@w).translation
    o.data.transform(Matrix.Translation(-p)@S@w);positions[o]=p
for o in parts:
    if o.parent and o.parent not in parts and o.parent!=root:o.parent=ch
    o.matrix_parent_inverse=Matrix.Identity(4);o.rotation_euler=(0,0,0);o.scale=(1,1,1)
    o.location=positions[o]-(positions.get(o.parent,Vector((0,0,0))))
bpy.context.view_layer.update()
# Use the existing detailed wheel meshes, restore round 210mm support tyres.
for suffix,sign in [('L',-1),('R',1)]:
    o=bpy.data.objects['SM_AGV_Wheel_Stabilizer_'+suffix]
    o.name='SM_AGV_Wheel_Support_'+suffix;o.parent=ch
    o.data.transform(S.inverted());o.location=(.27,sign*.45,.105)
    for i,m in enumerate(o.data.materials):
        if m.name.startswith('AGV_Stabilizer'):o.data.materials[i]=next(m for m in bpy.data.materials if m.name.startswith('AGV_Wheel_Polyurethane_Amber'))
    o['radius_m']=.105;o['joint_type']='revolute';o['motion_axis']='Y'
drive=bpy.data.objects['SM_AGV_Wheel_Drive'];steer=bpy.data.objects['SM_AGV_DriveSteer']
drive['radius_m']=.205
steer.location=(-.40,0,.435)
drive.location=(0,0,-.230)
black=bpy.data.materials['Graphite powder-coated steel'];steel=bpy.data.materials['Machined steel']
fixed=[]
def box(name,loc,dim,mat,r=.012):
    bpy.ops.object.select_all(action='DESELECT');bpy.ops.mesh.primitive_cube_add(size=1,location=loc)
    o=bpy.context.object;o.name=name;o.dimensions=dim;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    o.data.materials.append(mat)
    if r:
        m=o.modifiers.new('Rounded fabricated edges','BEVEL');m.width=r;m.segments=4
        o.modifiers.new('Weighted normals','WEIGHTED_NORMAL')
    bpy.ops.object.convert(target='MESH');fixed.append(o);return o
# Restore original layered base, rubber skirt and thin perimeter trim.
for name in ['Lower steel chassis','Rubber bumper skirt','Brushed perimeter bumper']:
    src=bpy.data.objects[name];base=src.copy();base.data=src.data.copy();s.collection.objects.link(base)
    base.matrix_world=Matrix.Rotation(math.pi/2,4,'Z')@src.matrix_world
    bpy.ops.object.select_all(action='DESELECT');base.select_set(True);bpy.context.view_layer.objects.active=base
    bpy.ops.object.convert(target='MESH');fixed.append(base)
    for loc,dim in [((-.40,0,.16),(.66,.58,.49)),((.27,-.45,.105),(.25,.21,.23)),((.27,.45,.105),(.25,.21,.23))]:
        cutter=box('Wheel clearance',loc,dim,black,.018);fixed.remove(cutter)
        bpy.context.view_layer.objects.active=base
        m=base.modifiers.new('Wheel opening','BOOLEAN');m.operation='DIFFERENCE';m.solver='EXACT';m.object=cutter
        bpy.ops.object.modifier_apply(modifier=m.name);bpy.data.objects.remove(cutter,do_unlink=True)
for sign in [-1,1]:
    box('Equipment module rear housing',(-.08,sign*.475,.85),(.58,.19,.59),black,.033)
    box('Lower module plinth',(-.08,sign*.445,.435),(.43,.16,.20),black,.018)
    box('Module latch',(-.29,sign*.582,.85),(.026,.018,.085),steel,.006)
    for x in [-.29,.13]:box('Handle mount',(x,sign*.57,1.185),(.05,.045,.035),black,.01)
    box('Raised carry handle',(-.08,sign*.59,1.205),(.44,.025,.025),black,.009)
    box('Side service plate',(-.08,sign*.531,.43),(.23,.007,.04),steel,.003)
bpy.ops.object.select_all(action='DESELECT');ch.select_set(True)
for o in fixed:o.select_set(True)
bpy.context.view_layer.objects.active=ch;bpy.ops.object.join()
# Preserve original fork length and upper proportions.
def driver(o,path,index,prop,expr):
    d=o.driver_add(path,index).driver;d.expression=expr
    v=d.variables.new();v.name='v';v.type='SINGLE_PROP';v.targets[0].id=root;v.targets[0].data_path='["'+prop+'"]'
driver(steer,'rotation_euler',2,'steer_deg','v*pi/180')
for o in parts:
    if o.name.startswith('SM_AGV_Wheel_'):driver(o,'rotation_euler',1,'travel_m',f'v/{o["radius_m"]}')
for name in ['SM_AGV_MastInner','SM_AGV_LiftCarriage']:
    o=bpy.data.objects[name];driver(o,'location',2,'lift_m',f'{o.location.z}+v*0.5')
o=bpy.data.objects['SM_AGV_Button_EStop'];driver(o,'location',1,'estop_press',f'{o.location.y}-v*.008')
for i in range(1,4):
    o=bpy.data.objects['SM_AGV_Button_HMI_'+str(i)];driver(o,'location',0,'hmi_'+str(i)+'_press',f'{o.location.x}+v*.003')
root['reference_note']='User supplied VNP15 example; visual approximation, not verified OEM specifications'
for key in ['caster_L_deg','caster_R_deg']:
    if key in root:del root[key]
bpy.context.view_layer.update()
c=bpy.data.objects['Front three-quarter camera.001'];s.camera=c
c.location=(5,4,3.05);c.rotation_euler=(Vector((.35,0,1.30))-c.location).to_track_quat('-Z','Y').to_euler();c.data.ortho_scale=4.30
s.render.resolution_x=1400;s.render.resolution_y=1400;s.render.resolution_percentage=100
s.render.filepath=str(OUT/'refined_front.png')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'orange_agv_refined.blend'))
bpy.ops.render.render(write_still=True)
print('OriginalProportions revision saved; mesh count',len(parts))
