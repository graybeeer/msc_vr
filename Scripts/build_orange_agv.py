"""Photo-guided visual AGV model. Execute in Blender through its MCP server.
Dimensions are estimates in metres, not measured manufacturing specifications.
"""
import bpy
import math
from mathutils import Vector

OUT = 'F:/Works/Unreal/Projects/msc_vr/Art/Vehicles/OrangeAGV/'
scene = bpy.data.scenes.new('Orange autonomous stacker - reference study')
bpy.context.window.scene = scene
scene.unit_settings.system = 'METRIC'
vehicle = bpy.data.collections.new('AGV | vehicle parts')
scene.collection.children.link(vehicle)
studio = bpy.data.collections.new('STUDIO | cameras and lighting')
scene.collection.children.link(studio)

def mat(name, rgb, metal=0, rough=.4, emission=0):
    m=bpy.data.materials.new(name)
    m.diffuse_color=(*rgb,1)
    m.use_nodes=True
    n=m.node_tree.nodes.get('Principled BSDF')
    n.inputs['Base Color'].default_value=(*rgb,1)
    n.inputs['Metallic'].default_value=metal
    n.inputs['Roughness'].default_value=rough
    if emission:
        n.inputs['Emission'].default_value=(*rgb,1)
        n.inputs['Emission Strength'].default_value=emission
    return m

orange=mat('Safety orange - painted body',(.87,.075,.014),.2,.29)
black=mat('Graphite powder-coated steel',(.021,.027,.035),.25,.44)
panel=mat('Control enclosure - charcoal',(.035,.045,.058),.25,.4)
rubber=mat('Black rubber',(.009,.012,.016),0,.68)
steel=mat('Machined steel',(.36,.41,.45),.85,.25)
chrome=mat('Hydraulic piston chrome',(.65,.71,.76),.92,.16)
tyre=mat('Polyurethane roller',(.51,.39,.19),.1,.58)
yellow=mat('Safety warning yellow',(1,.62,.025),.05,.4)
white=mat('White print',(.9,.93,.95),0,.6)
green=mat('Sensor status green',(.05,1,.3),.1,.22,3)
blue=mat('Screen blue',(.025,.24,.55),.1,.25,.7)
lens=mat('Optical lens',(.015,.11,.15),.65,.1)
red=mat('Emergency stop red',(.75,.014,.018),.15,.28)
floor=mat('Studio concrete',(.20,.23,.27),0,.85)

def finish(obj,name,material,collection=vehicle):
    obj.name=name
    for c in list(obj.users_collection): c.objects.unlink(obj)
    collection.objects.link(obj)
    if material: obj.data.materials.append(material)
    return obj

def box(name,loc,size,material,bevel=.015,collection=vehicle):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc)
    o=finish(bpy.context.object,name,material,collection)
    o.dimensions=size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bevel:
        mod=o.modifiers.new('Manufactured edge radius','BEVEL')
        mod.width=bevel; mod.segments=4
        o.modifiers.new('Weighted face normals','WEIGHTED_NORMAL')
    return o

def cyl(name,loc,radius,depth,material,axis='Z',vertices=48):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=radius,depth=depth,location=loc)
    o=finish(bpy.context.object,name,material)
    if axis=='X': o.rotation_euler[1]=math.pi/2
    if axis=='Y': o.rotation_euler[0]=math.pi/2
    bevel=o.modifiers.new('Edge bevel','BEVEL'); bevel.width=.005; bevel.segments=3
    o.modifiers.new('Weighted normals','WEIGHTED_NORMAL')
    for p in o.data.polygons: p.use_smooth=True
    return o

def tube(name,points,radius,material):
    c=bpy.data.curves.new(name,'CURVE'); c.dimensions='3D'
    c.bevel_depth=radius; c.bevel_resolution=3
    s=c.splines.new('POLY'); s.points.add(len(points)-1)
    for p,co in zip(s.points,points): p.co=(*co,1)
    o=bpy.data.objects.new(name,c); vehicle.objects.link(o); o.data.materials.append(material)
    return o

def label(name,text,loc,size,material,side='X'):
    c=bpy.data.curves.new(name,'FONT'); c.body=text; c.size=size
    c.align_x='CENTER'; c.align_y='CENTER'; c.extrude=.0003
    o=bpy.data.objects.new(name,c); vehicle.objects.link(o); o.location=loc
    if side=='X': o.rotation_euler=(math.pi/2,0,math.pi/2)
    elif side=='BACK': o.rotation_euler=(math.pi/2,0,math.pi)
    else: o.rotation_euler=(math.pi/2,0,0)
    c.materials.append(material)
    return o

# Low compact power unit with a wraparound rounded orange cover.
box('Lower steel chassis',(0,.23,.17),(.91,1.20,.22),black,.065)
box('Rubber bumper skirt',(0,.31,.095),(.98,1.23,.12),rubber,.055)
box('Brushed perimeter bumper',(0,.31,.062),(1.02,1.26,.043),steel,.04)
profile=[(-.25,.30),(.78,.30),(.86,.43),(.86,.94),(.81,1.12),(.70,1.26),(.43,1.33),(-.16,1.33),(-.25,1.12)]
n=len(profile)
verts=[(x,y,z) for x in [-.43,.43] for y,z in profile]
faces=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
mesh=bpy.data.meshes.new('Rounded body shell');mesh.from_pydata(verts,[],faces);mesh.update()
o=bpy.data.objects.new('Orange molded rear cover',mesh);vehicle.objects.link(o);mesh.materials.append(orange)
bpy.context.view_layer.objects.active=o; o.select_set(True)
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.normals_make_consistent(inside=False);bpy.ops.object.mode_set(mode='OBJECT');o.select_set(False)
mod=o.modifiers.new('Large shell fillets','BEVEL');mod.width=.085;mod.segments=6
for p in mesh.polygons: p.use_smooth=True
mesh.use_auto_smooth=True
o.modifiers.new('Weighted shell normals','WEIGHTED_NORMAL')

for x in [-.452,.452]:
    box('Inset black side equipment cover',(x,.14,.77),(.07,.68,.87),panel,.045)
    box('Top enclosure lip',(x,.14,1.205),(.09,.70,.045),black,.015)
    for yy in [-.11,.39]:
        for zz in [.39,1.12]: cyl('Recessed panel fastener',(x+(.038 if x>0 else -.038),yy,zz),.012,.008,steel,'X',16)
    for j in range(7): box('Enclosure ventilation slit',(x+(.04 if x>0 else -.04),.12,.45+j*.025),(.012,.21,.008),rubber,.002)
    box('Service label plate',(x+(.04 if x>0 else -.04),.17,.36),(.015,.30,.052),steel,.005)
label('Fleet number side','19',(.495,.13,.94),.16,white)
label('Manufacturer side','VisionNav',(.496,.13,.79),.064,white)
label('Robotics side','Robotics',(.496,.13,.72),.057,white)
label('Fleet number rear','19',(0,.883,.82),.14,white,'BACK')
label('Reference rear branding','coupang',(0,.866,1.09),.102,white,'BACK')
box('Rear cover lower seam',(0,.882,.42),(.61,.008,.017),panel,.005)
for x in [-.34,.34]:
    cyl('Rear cover screw',(x,.876,.53),.012,.009,steel,'Y',16)
    cyl('Driven rubber wheel',(x,.58,.18),.17,.13,rubber,'X')
    cyl('Drive wheel hub',(x*1.18,.58,.18),.09,.02,steel,'X')

# Long low outriggers and separate raised L-shaped fork blades.
for x in [-.31,.31]:
    box('Outrigger support leg',(x,-.90,.115),(.20,1.46,.14),black,.028)
    cyl('Front polyurethane load roller',(x,-1.53,.105),.098,.18,tyre,'X')
    cyl('Roller axle cap',(x+.098,-1.53,.105),.053,.012,steel,'X')
    box('Forged fork blade',(x,-1.02,.27),(.14,1.48,.065),steel,.012)
    box('Tapered fork nose',(x,-1.76,.252),(.14,.12,.034),steel,.01)
    box('Vertical fork heel',(x,-.325,.57),(.14,.07,.63),black,.015)
    box('Carriage locking block',(x,-.33,.9),(.18,.12,.06),steel,.01)

# Paired telescopic mast rails, polished running surfaces and hydraulic lift.
for x in [-.335,.335]:
    box('Outer mast column',(x,-.23,1.33),(.105,.19,2.23),black,.012)
    box('Polished mast track',(x,-.336,1.38),(.035,.013,2.05),steel,.004)
    box('Inner sliding mast rail',(x*.76,-.27,1.31),(.05,.115,2.08),black,.007)
for z in [.32,1.13,2.37]: box('Mast cross member',(0,-.23,z),(.77,.17,.09),black,.012)
cyl('Hydraulic cylinder',(0,-.19,.91),.054,1.12,black)
cyl('Chrome lift ram',(0,-.19,1.79),.027,.72,chrome)
cyl('Lift chain pulley',(0,-.19,2.20),.092,.09,steel,'Y')
for x in [-.14,.14]:
    for i in range(42):
        box('Lift chain link',(x,-.35,.40+i*.044),(.027,.025,.028),steel,.004)
    tube('Hydraulic hose',[(x,-.14,.38),(x,-.08,1.1),(x,-.08,2.17),(x*.7,-.16,2.23)],.011,rubber)
box('Fork carriage bottom',(0,-.40,.40),(.77,.10,.10),black,.015)
box('Fork carriage top',(0,-.40,1.03),(.77,.10,.08),black,.012)
for x in [-.37,-.19,0,.19,.37]: box('Load backrest guard',(x,-.405,.72),(.025,.045,.59),black,.004)

# Tall autonomous navigation tower distinct from the mast.
box('Navigation tower foot',(.10,.36,1.35),(.28,.29,.09),black,.025)
box('Navigation computer tower',(.10,.36,1.87),(.20,.18,1.02),panel,.018)
box('Sensor crossbar',(.10,.36,2.35),(.72,.19,.10),black,.018)
for x in [-.26,.46]:
    box('Stereo sensor housing',(x,.35,2.34),(.13,.24,.12),panel,.021)
    cyl('Navigation camera lens',(x,.22,2.35),.039,.022,lens,'Y')
    cyl('Green status indicator',(x,.488,2.35),.022,.015,green,'Y')
    cyl('Side status indicator',(x+(.072 if x>0 else -.072),.34,2.35),.021,.012,green,'X')
cyl('Lidar riser',(.10,.36,2.57),.035,.40,black)
cyl('Lidar black housing',(.10,.36,2.77),.102,.07,panel)
cyl('Lidar silver cap',(.10,.36,2.814),.11,.023,steel)
cyl('Lidar status band',(.10,.36,2.796),.104,.008,green)
box('Front perception riser',(-.335,-.23,2.53),(.055,.06,.32),black,.006)
box('Upper mast optical unit',(-.335,-.23,2.70),(.22,.16,.13),panel,.016)
cyl('Upper optical lens',(-.335,-.32,2.70),.04,.018,lens,'Y')

# Human interface, warning labels, emergency stop and cable conduit.
box('HMI screen backing',(.11,.481,1.68),(.33,.052,.25),black,.025)
box('HMI illuminated display',(.11,.51,1.69),(.27,.006,.17),blue,.006)
for j in range(3): box('Display status line',(.10,.515,1.73-j*.037),(.17-j*.035,.005,.009),white,.001)
for x in [.015,.11,.205]: cyl('HMI pushbutton',(x,.517,1.585),.009,.008,green if x==.11 else steel,'Y',24)
label('Tower fleet number','19',(.208,.36,2.12),.12,white)
box('Emergency stop bracket',(.235,.38,1.97),(.085,.13,.18),yellow,.008)
cyl('Emergency stop button',(.294,.38,1.99),.036,.043,red,'X')
for z in [1.78,2.18]:
    box('Warning decal',(.208,.37,z),(.009,.10,.075),yellow,.004)
    label('Warning symbol','!',(.216,.37,z),.055,black)
tube('Navigation power cable',[(.20,.41,1.38),(.27,.56,1.35),(.31,.55,1.19)],.015,rubber)
for x in [-.41,.41]:
    cyl('Low safety scanner',(x,.73,.255),.087,.105,panel)
    cyl('Scanner optical belt',(x,.73,.27),.089,.031,lens)
    box('Amber side reflector',(x,.73,.34),(.07,.10,.02),yellow,.006)

# Studio presentation, keeping the reference machine unobscured by pallet cargo.
box('Ground',(0,0,-.055),(200,200,.08),floor,.0,studio)
lineblue=mat('Projected blue safety boundary',(.006,.055,.8),0,.4,2)
for x in [-.83,.83]:
    o=box('Blue floor boundary',(x,-.4,-.008),(.018,3.05,.004),lineblue,.001,studio)
box('Blue floor boundary',(0,-1.92,-.008),(1.68,.018,.004),lineblue,.001,studio)
box('Blue floor boundary',(0,1.12,-.008),(1.68,.018,.004),lineblue,.001,studio)

def aim(o,target): o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(4,-5,3.05))
camera=finish(bpy.context.object,'Front three-quarter camera',None,studio)
aim(camera,(0,-.35,1.30));camera.data.type='ORTHO';camera.data.ortho_scale=4.30;scene.camera=camera
bpy.ops.object.camera_add(location=(4,5,3.1))
rear=finish(bpy.context.object,'Rear three-quarter camera',None,studio)
aim(rear,(0,-.15,1.31));rear.data.type='ORTHO';rear.data.ortho_scale=4.25
for name,loc,energy,size,color in [('Large key',(1,-4,6),950,4,(.86,.92,1)),('Orange rim',(-3,2,5),1300,3,(1,.84,.68)),('Rear fill',(4,4,4),950,3,(1,1,1))]:
    bpy.ops.object.light_add(type='AREA',location=loc)
    l=finish(bpy.context.object,name,None,studio);l.data.energy=energy;l.data.shape='DISK';l.data.size=size;l.data.color=color;aim(l,(0,0,1))
scene.world=bpy.data.worlds.new('Neutral studio environment');scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.27,.32,.42,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.45
scene.render.engine='BLENDER_EEVEE';scene.eevee.use_gtao=True;scene.eevee.gtao_distance=3;scene.eevee.gtao_factor=1.25
scene.eevee.use_soft_shadows=True;scene.eevee.taa_render_samples=128;scene.eevee.use_bloom=True;scene.eevee.bloom_intensity=.12
scene.render.resolution_x=1500;scene.render.resolution_y=1500;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=OUT+'orange_agv_front.png'
scene.eevee.use_bloom=False
for light in studio.objects:
    if light.type=='LIGHT': light.data.energy*=.65
orange.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=.36
orange.node_tree.nodes['Principled BSDF'].inputs['Specular'].default_value=.32
scene.view_settings.view_transform='Filmic';scene.view_settings.look='Medium High Contrast'
scene['reference_note']='Visual approximation from five supplied warehouse photographs. No measured engineering dimensions.'
scene['estimated_dimensions']='Body width 0.95 m; overall fork-to-rear length 2.70 m; sensor height 2.83 m.'
bpy.ops.object.select_all(action='DESELECT')
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        area.spaces.active.region_3d.view_perspective='CAMERA'
        area.spaces.active.overlay.show_overlays=False
        area.spaces.active.shading.type='MATERIAL'
bpy.ops.wm.save_as_mainfile(filepath=OUT+'orange_agv.blend')
print('AGV saved:',OUT+'orange_agv.blend','vehicle parts:',len(vehicle.objects))
