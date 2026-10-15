"""Blender-only assembly and module catalog renders; no Unreal project edits."""
import bpy,math,json,bmesh
from pathlib import Path
from mathutils import Vector
OUT=Path('F:/Works/Unreal/Projects/msc_vr/Art/Environment/WarehouseServices')
library=bpy.data.scenes['Warehouse Services - Library']
source={o.name.removeprefix('SM_WS_'):o for o in library.objects if o.type=='MESH'}
for o in source.values():
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
def mat(name):return bpy.data.materials[name]
def new_scene(name):
    s=bpy.data.scenes.new(name);bpy.context.window.scene=s;s.unit_settings.system='METRIC';s.unit_settings.scale_length=1
    s.render.engine='BLENDER_EEVEE';s.eevee.use_gtao=True;s.eevee.gtao_distance=.22;s.eevee.gtao_factor=1.25;s.eevee.use_bloom=True;s.eevee.bloom_intensity=.035;s.eevee.taa_render_samples=128
    s.view_settings.view_transform='Filmic';s.render.image_settings.file_format='PNG';s.render.resolution_percentage=100
    w=bpy.data.worlds.new(name+' world');s.world=w;w.use_nodes=True
    n=next(n for n in w.node_tree.nodes if n.type=='BACKGROUND');n.inputs[0].default_value=(.20,.23,.27,1);n.inputs[1].default_value=.55
    return s
def instance(s,key,loc,rot=(0,0,0),scale=1):
    o=source[key].copy();o.data=source[key].data;s.collection.objects.link(o);o.name='PREVIEW_'+key
    o.location=loc;o.rotation_euler=rot;o.scale=(scale,scale,scale);return o
def box(s,name,loc,dim,material):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.name=name;o.dimensions=dim
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(mat(material))
    m=o.modifiers.new('Preview rounded edges','BEVEL');m.width=.008;m.segments=2;o.modifiers.new('Preview normals','WEIGHTED_NORMAL');return o
def label(s,body,loc,size=.10):
    c=bpy.data.curves.new(body,'FONT');c.body=body;c.align_x='CENTER';c.align_y='CENTER';c.size=size;c.extrude=0
    o=bpy.data.objects.new(body,c);s.collection.objects.link(o);o.location=loc;c.materials.append(mat('WS Structural graphite'));return o
def lighting(s,camera_pos,target,ortho,res):
    for loc,power,size in [((2,-4,7),1800,5),((-4,0,5),1200,4),((3,4,6),1400,4)]:
        bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=power;o.data.size=size;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
    bpy.ops.object.camera_add(location=camera_pos);c=bpy.context.object;c.rotation_euler=(Vector(target)-c.location).to_track_quat('-Z','Y').to_euler();c.data.type='ORTHO';c.data.ortho_scale=ortho;s.camera=c
    s.render.resolution_x=res[0];s.render.resolution_y=res[1]
assembly=new_scene('Warehouse Services - Assembly Preview')
box(assembly,'Preview floor',(0,.25,-.065),(6.3,6.4,.13),'WS Preview concrete')
box(assembly,'Preview wall',(0,2.75,1.85),(6.3,.12,3.7),'WS Preview concrete')
for x in [-2,2]:box(assembly,'Preview column',(x,2.42,1.86),(.16,.16,3.72),'WS Structural graphite')
for y in [-1.30,2]:
    instance(assembly,'IBeam_4m',(-2,y,3.8))
    for x in [-2,2]:instance(assembly,'BeamConnection',(x,y,3.8))
for x in [-2,2]:instance(assembly,'IBeam_4m',(x,-1.5,3.80),(0,0,math.pi/2))
for x in [-2,0]:
    instance(assembly,'TrayStraight_2m',(x,1.65,3.16));instance(assembly,'CableBundle_2m',(x,1.65,3.16))
instance(assembly,'TrayElbow90',(2,1.65,3.16))
for x in [-1.70,-.5,.7,1.7]:instance(assembly,'TrayHanger',(x,1.65,3.15))
for x in [-1.7,0,1.7]:
    o=instance(assembly,'WallBracket',(x,2.68,3.12),(0,0,-math.pi/2));o.scale.x=2
for x in [-1.18,-1,-.82]:
    instance(assembly,'ConduitStraight_1m',(x,2.43,1.85),(0,-math.pi/2,0))
    # Upward leg ends in a 90deg turn towards the tray; horizontal segment joins that tray.
    instance(assembly,'ConduitElbow90',(x,2.43,2.85),(math.pi/2,-math.pi/2,0))
    instance(assembly,'ConduitStraight_1m',(x,2.28,3.00),(0,0,-math.pi/2),.63)
    for z in [2.04,2.52]:instance(assembly,'ConduitClamp',(x,2.43,z),(math.pi/2,0,0))
instance(assembly,'DistributionCabinet',(-1,2.50,1.4))
instance(assembly,'DistributionDoor',(-1.28,2.50,1.4))
instance(assembly,'JunctionBox',(.23,2.52,1.75))
for z in [.65,1.75]:instance(assembly,'ConduitStraight_1m',(.23,2.5,z),(0,-math.pi/2,0))
for z in [1.03,2.05]:instance(assembly,'ConduitClamp',(.23,2.5,z),(math.pi/2,0,0))
for x,length in [(-2,.5),(-.5,1),(1.5,.5)]:
    o=instance(assembly,'FirePipeStraight_2m',(x,-.9,3.23));o.scale.x=length/2
for x in [-1.8,-.2,1.7]:instance(assembly,'PipeHanger',(x,-.9,3.23))
for x in [-1.5,.5]:
    # Tee's short branch points down and receives the sprinkler thread origin.
    instance(assembly,'FirePipeTee',(x,-.90,3.23),(-math.pi/2,0,0))
    instance(assembly,'SprinklerHead',(x+.5,-.9,2.88))
for x in [-1.5,.3]:
    for y in [-.2,.8]:instance(assembly,'LEDLinear_1200',(x,y,2.96))
for x in [-1.44,-.36,.36,1.44]:
    for y in [-.2,.8]:
        bpy.ops.mesh.primitive_cylinder_add(vertices=12,radius=.003,depth=.80,location=(x,y,3.4))
        o=bpy.context.object;o.name='PREVIEW_LED_Suspension';o.data.materials.append(mat('WS Fasteners'))
for x in [-2,-.5,1]:instance(assembly,'DuctStraight_1500',(x,2.29,3.43))
instance(assembly,'DuctGrille',(2.51,2.29,3.43))
lighting(assembly,(7,-9,4),(.05,.8,2.05),7.5,(1600,1300));assembly.view_settings.exposure=-.55
assembly.render.filepath=str(OUT/'services_assembly.png');bpy.ops.render.render(write_still=True)
# Separate catalog scene; uniformly scaled display copies do not affect exported modules.
catalog=new_scene('Warehouse Services - Module Catalog')
box(catalog,'Catalog surface',(0,0,-.12),(18,19,.15),'WS Preview concrete')
for i,(key,src) in enumerate(source.items()):
    col=i%5;row=i//5;center=Vector(((col-2)*3.05,(2.5-row)*2.55,0))
    o=instance(catalog,key,(0,0,0));o.scale=(1,1,1)
    pts=[v.co for v in o.data.vertices];lo=Vector([min(p[k] for p in pts) for k in range(3)]);hi=Vector([max(p[k] for p in pts) for k in range(3)])
    factor=1.85/max(hi-lo);o.scale=(factor,)*3;o.location=center-(lo+hi)*.5*factor;o.location.z-=lo.z*factor-(lo.z+hi.z)*.5*factor;o.location.z+=.05
    # Cabinet door display makes its front face readable from the front catalog camera.
    label(catalog,f'{i+1:02d}  '+src['module_label'],center+Vector((0,-.98,.011)),.14)
lighting(catalog,(0,-9,23),(0,0,.2),17.2,(2100,1800))
for o in catalog.objects:
    if o.type=='LIGHT':o.data.energy*=.52;o.data.size*=2
catalog.view_settings.exposure=-.35
catalog.render.filepath=str(OUT/'services_catalog.png');bpy.ops.render.render(write_still=True)
bpy.context.window.scene=assembly
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.overlay.show_overlays=False
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'warehouse_services.blend'))
print('Assembly and catalog rendered; original 26 modules kept at unit scale in Library scene')
