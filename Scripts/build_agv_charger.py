"""Run via Blender MCP. New scene; preserve all existing scenes."""
import bpy,math,json
from pathlib import Path
from mathutils import Vector,Matrix
OUT=Path('F:/Works/Unreal/Projects/msc_vr/Art/Equipment/AGVCharger')
OUT.mkdir(parents=True,exist_ok=True)
assert not bpy.data.scenes.get('AGV Charger - concept')
s=bpy.data.scenes.new('AGV Charger - concept');bpy.context.window.scene=s
s.unit_settings.system='METRIC';s.unit_settings.scale_length=1
asset=bpy.data.collections.new('CHARGER_ASSET');s.collection.children.link(asset)
studio=bpy.data.collections.new('CHARGER_STUDIO');s.collection.children.link(studio)
groups={}
def material(name,color,metal=0,rough=.4,emission=0,texture=False):
    m=bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*color,1)
    n=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    n.inputs['Base Color'].default_value=(*color,1);n.inputs['Metallic'].default_value=metal;n.inputs['Roughness'].default_value=rough
    if emission:n.inputs['Emission'].default_value=(*color,1);n.inputs['Emission Strength'].default_value=emission
    if texture:
        noise=m.node_tree.nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=190
        bump=m.node_tree.nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.12;bump.inputs['Distance'].default_value=.0004
        m.node_tree.links.new(noise.outputs['Fac'],bump.inputs['Height']);m.node_tree.links.new(bump.outputs['Normal'],n.inputs['Normal'])
    return m
graphite=material('Charger powder coat',(.042,.052,.049),.55,.36,texture=True)
edge=material('Charger gunmetal frame',(.075,.09,.087),.72,.30)
black=material('Charger recess rubber',(.008,.014,.013),.05,.55)
copper=material('Charger brass contact',(.60,.32,.105),.83,.27)
steel=material('Charger bolt steel',(.33,.38,.36),.90,.28)
white=material('Charger printed ivory',(.72,.77,.73),.0,.47)
green=material('Charger status mint',(.02,.8,.43),.1,.2,4)
yellow=material('Charger safety amber',(.95,.49,.025),.25,.33)
red=material('Charger emergency stop',(.68,.018,.009),.12,.29)
lens=material('Charger sensor glass',(.035,.13,.11),.4,.15)
floor=material('Charger studio floor',(.17,.19,.18),0,.8,texture=True)
wallmat=material('Charger concrete wall',(.12,.135,.13),0,.86,texture=True)
def finish(o,name,mat,group):
    o.name=name
    for c in list(o.users_collection):c.objects.unlink(o)
    (studio if group=='studio' else asset).objects.link(o)
    if mat:o.data.materials.append(mat)
    if group!='studio':groups.setdefault(group,[]).append(o)
    return o
def box(name,loc,size,mat,group='SM_Charger_Cabinet',bevel=.006):
    bpy.ops.object.select_all(action='DESELECT');bpy.ops.mesh.primitive_cube_add(size=1,location=loc)
    o=finish(bpy.context.object,name,mat,group);o.dimensions=size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bevel:
        b=o.modifiers.new('Machined edge radius','BEVEL');b.width=bevel;b.segments=4
        o.modifiers.new('Face normals','WEIGHTED_NORMAL')
    return o
def cyl(name,loc,r,d,mat,group='SM_Charger_Cabinet',axis='X'):
    bpy.ops.object.select_all(action='DESELECT');bpy.ops.mesh.primitive_cylinder_add(vertices=40,radius=r,depth=d,location=loc)
    o=finish(bpy.context.object,name,mat,group)
    if axis=='X':o.rotation_euler.y=math.pi/2
    b=o.modifiers.new('Metal rim radius','BEVEL');b.width=min(.002,r*.14);b.segments=3
    o.modifiers.new('Face normals','WEIGHTED_NORMAL')
    for p in o.data.polygons:p.use_smooth=True
    return o
def text(name,body,loc,size,mat,group='SM_Charger_Door'):
    c=bpy.data.curves.new(name,'FONT');c.body=body;c.size=size;c.align_x='CENTER';c.align_y='CENTER';c.extrude=0
    o=bpy.data.objects.new(name,c);asset.objects.link(o);o.location=loc;o.rotation_euler=(math.pi/2,0,math.pi/2);c.materials.append(mat);groups.setdefault(group,[]).append(o)
    return o
# +X is the vehicle-facing direction; origin is ground below the contact pads.
box('Folded cabinet shell',(-.365,0,.815),(.37,.70,1.53),graphite,bevel=.018)
box('Cabinet base plinth',(-.36,0,.035),(.40,.74,.07),black,bevel=.012)
box('Chamfered top cap',(-.365,0,1.59),(.39,.735,.04),edge,bevel=.012)
box('Front door seam',(-.176,0,.855),(.006,.654,1.39),black)
box('Front service door',(-.169,0,.855),(.012,.635,1.37),graphite,'SM_Charger_Door',.007)
for y in [-.28,.28]:
    for z in [.17,1.46]:cyl('Captive door screw',(-.159,y,z),.003,.003,steel,'SM_Charger_Door')
box('Door recessed pull',(-.158,-.277,.85),(.012,.018,.095),black,'SM_Charger_Door',.006)
box('Door pull metal',(-.149,-.277,.85),(.013,.007,.071),steel,'SM_Charger_Door',.003)
for z in [.23,.75,1.26]:box('Door hinge',(-.16,.319,z),(.025,.012,.075),edge,'SM_Charger_Door',.003)
text('AGV cabinet title','AGV',(-.158,-.035,1.165),.075,white)
text('Charging cabinet subtitle','CHARGING STATION',(-.158,-.035,1.095),.028,white)
# Flush LED strips, inset black bezel.
for loc,dim in [((-.147,0,1.54),(.022,.47,.028)),((-.15,-.288,1.24),(.018,.014,.30))]:
    box('Status lamp bezel',loc,tuple(v+.009 for v in dim),black)
    box('Status lamp diffuser',(loc[0]+.005,loc[1],loc[2]),dim,green,'SM_Charger_StatusLight',.004)
# Side/back ventilation is geometry rather than a flat printed detail.
for side in [-1,1]:
    for z in [.18+i*.018 for i in range(12)]:
        box('Side cooling slot',(-.365,side*.351,z),(.225,.002,.006),black,bevel=.001)
for z in [.22+i*.018 for i in range(9)]:box('Rear cooling slot',(-.552,0,z),(.002,.35,.006),black,bevel=.001)
# Contact face at X=0: one of the five paired rows is exactly at Z=0.28m.
module='SM_Charger_ContactModule'
box('Connector stand-off',(-.138,0,.36),(.065,.36,.43),edge,module,.015)
box('Connector projecting casing',(-.083,0,.36),(.15,.405,.535),graphite,module,.03)
box('Connector black inset',(-.004,0,.36),(.005,.285,.415),black,module,.008)
for y in [-.181,.181]:box('Contact frame side',(-.012,y,.36),(.045,.031,.485),edge,module,.012)
for z in [.12,.60]:box('Contact frame crossbar',(-.012,0,z),(.045,.38,.03),edge,module,.012)
for y in [-.182,.182]:
    for z in [.145,.27,.45,.574]:cyl('Frame mounting screw',(.012,y,z),.005,.005,steel,module)
for row,z in enumerate([.20,.28,.36,.44,.52]):
    for side,y in enumerate([-.064,.064]):
        box('Contact ceramic backing',(-.011,y,z),(.022,.114,.065),black,module,.006)
        box('Brass contact pad',(-.003,y,z),(.006,.100,.053),copper,'SM_Charger_Contacts',.004)
        for yy in [y-.035,y+.035]:
            cyl('Contact pad fixing',(0.001,yy,z),.003,.002,steel,'SM_Charger_Contacts')
            box('Contact screw slot',(.002,yy,z),(.0006,.003,.0007),black,'SM_Charger_Contacts',.0001)
# Recessed docking guide bumper with amber upper strip.
box('Dock guide bumper',(-.103,0,.07),(.19,.70,.105),edge,'SM_Charger_DockBumper',.013)
box('Dock guide amber rail',(-.079,0,.13),(.095,.56,.012),yellow,'SM_Charger_DockBumper',.004)
for y in [-.31,.31]:box('Bumper protective end',(-.09,y,.08),(.23,.095,.14),graphite,'SM_Charger_DockBumper',.012)
box('Bumper status bezel',(-.005,0,.075),(.012,.21,.019),black,'SM_Charger_DockBumper',.003)
box('Bumper alignment light',(.002,0,.075),(.004,.15,.006),green,'SM_Charger_StatusLight',.002)
# Hooded sensors and a separate pushable emergency-stop cap.
for y,z in [(.248,1.33),(.255,.73)]:
    box('Sensor housing',(-.117,y,z),(.095,.105,.115),graphite,'SM_Charger_Sensors',.009)
    box('Sensor rain hood',(-.097,y,z+.068),(.14,.132,.017),edge,'SM_Charger_Sensors',.007)
    cyl('Position sensor lens',(-.065,y,z+.013),.016,.004,lens,'SM_Charger_Sensors')
    cyl('Sensor status dot',(-.062,y,z-.027),.005,.004,green,'SM_Charger_StatusLight')
box('Emergency button base',(-.135,.25,.90),(.045,.083,.086),yellow,'SM_Charger_Sensors',.006)
cyl('Emergency button collar',(-.106,.25,.90),.025,.018,black,'SM_Charger_Sensors')
cyl('Emergency stop mushroom',(-.086,.25,.90),.026,.025,red,'SM_Charger_EStop')
text('Voltage warning triangle','!',(-.158,-.252,.535),.037,yellow)
# Join fixed subassemblies; retain lamps, contacts and button as selectable parts.
root=bpy.data.objects.new('CHARGER_ROOT',None);asset.objects.link(root)
for name,items in groups.items():
    for o in bpy.context.view_layer.objects:o.select_set(False)
    for o in items:
        o.select_set(True);bpy.context.view_layer.objects.active=o;bpy.ops.object.convert(target='MESH');o.select_set(False)
    for o in items:o.select_set(True)
    bpy.context.view_layer.objects.active=items[0];bpy.ops.object.join();o=bpy.context.object;o.name=name
    s.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');o.parent=root;o.matrix_parent_inverse=Matrix.Identity(4)
# Presentation only: warehouse floor, wall, striped safety bollards.
box('Studio ground',(-.1,0,-.025),(7,7,.05),floor,'studio',0)
box('Concrete wall',(-.68,0,1.55),(.12,5,3.1),wallmat,'studio',.002)
for z in [.75,1.5,2.25]:box('Wall seam',(-.618,0,z),(.002,5,.007),black,'studio',0)
for side in [-1,1]:
    y=side*.64
    box('Bollard anchoring plate',(-.02,y,.012),(.17,.17,.024),yellow,'studio',.006)
    for i in range(6):cyl('Safety bollard band',(-.02,y,.07+i*.11),.048,.11,yellow if i%2==0 else black,'studio','Z')
    cyl('Safety bollard cap',(-.02,y,.6875),.048,.025,black,'studio','Z')
    for x in [-.08,.04]:
        for yy in [y-.06,y+.06]:cyl('Bollard anchor bolt',(x,yy,.028),.006,.012,steel,'studio','Z')
for y in [-.67,.67]:box('Floor parking side',(.28,y,.002),(1.42,.026,.004),yellow,'studio',0)
box('Floor parking end',(.96,0,.002),(.026,1.35,.004),yellow,'studio',0)
w=bpy.data.worlds.new('Charger studio world');s.world=w;w.use_nodes=True
next(n for n in w.node_tree.nodes if n.type=='BACKGROUND').inputs[0].default_value=(.12,.15,.17,1)
next(n for n in w.node_tree.nodes if n.type=='BACKGROUND').inputs[1].default_value=.45
def aim(o,p):o.rotation_euler=(Vector(p)-o.location).to_track_quat('-Z','Y').to_euler()
for name,loc,power,size,col in [('Large softbox',(2,-3,3.6),650,3,(.86,.93,1)),('Warm rim',(-.1,2,3),750,2,(1,.84,.64)),('Front fill',(3,1,1.8),240,2,(.92,1,.96))]:
    bpy.ops.object.light_add(type='AREA',location=loc);o=finish(bpy.context.object,name,None,'studio');o.data.energy=power;o.data.size=size;o.data.color=col;aim(o,(-.2,0,.8))
bpy.ops.object.camera_add(location=(2.7,-3.6,2.15));c=finish(bpy.context.object,'Charger hero camera',None,'studio');aim(c,(-.12,0,.79));c.data.type='ORTHO';c.data.ortho_scale=2.25;s.camera=c
s.render.engine='BLENDER_EEVEE';s.eevee.use_gtao=True;s.eevee.gtao_distance=.16;s.eevee.gtao_factor=1.3;s.eevee.use_bloom=True;s.eevee.bloom_intensity=.045;s.eevee.taa_render_samples=128
s.view_settings.view_transform='Filmic';s.view_settings.exposure=0;s.view_settings.gamma=1
s.render.resolution_x=1300;s.render.resolution_y=1500;s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG';s.render.filepath=str(OUT/'charger_hero.png')
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.overlay.show_overlays=False
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'agv_charger.blend'))
bpy.ops.render.render(write_still=True)
print('Charger modeled',[(o.name,len(o.data.vertices)) for o in asset.objects if o.type=='MESH'])
