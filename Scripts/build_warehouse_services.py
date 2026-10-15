"""Build warehouse services kit in Blender MCP; no Unreal connection or map edits."""
import bpy,math,json
from pathlib import Path
from mathutils import Vector,Matrix
OUT=Path('F:/Works/Unreal/Projects/msc_vr/Art/Environment/WarehouseServices')
OUT.mkdir(parents=True,exist_ok=True)
assert not bpy.data.scenes.get('Warehouse Services - Library')
library=bpy.data.scenes.new('Warehouse Services - Library');bpy.context.window.scene=library
library.unit_settings.system='METRIC';library.unit_settings.scale_length=1
collection=bpy.data.collections.new('WAREHOUSE_SERVICE_MODULES');library.collection.children.link(collection)
def mat(name,color,metal=0,rough=.4,emit=0):
    m=bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*color,1)
    n=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    n.inputs['Base Color'].default_value=(*color,1);n.inputs['Metallic'].default_value=metal;n.inputs['Roughness'].default_value=rough
    if emit:n.inputs['Emission'].default_value=(*color,1);n.inputs['Emission Strength'].default_value=emit
    return m
zinc=mat('WS Galvanized steel',(.38,.44,.46),.78,.34)
dark=mat('WS Structural graphite',(.055,.075,.082),.65,.39)
silver=mat('WS Fasteners',(.57,.62,.64),.88,.27)
black=mat('WS Cable black',(.012,.018,.022),.05,.54)
blue=mat('WS Data cable blue',(.025,.10,.27),.1,.48)
red=mat('WS Fire pipe red',(.50,.018,.012),.45,.31)
brass=mat('WS Sprinkler brass',(.58,.30,.065),.8,.28)
paint=mat('WS Electrical enclosure',(.23,.28,.30),.45,.4)
white=mat('WS LED diffuser',(.83,.90,1),.1,.3,2.0)
yellow=mat('WS Warning yellow',(.95,.59,.025),.1,.45)
concrete=mat('WS Preview concrete',(.22,.24,.245),0,.85)
pieces=[];modules=[];records=[]
def track(o,material):
    for c in list(o.users_collection):c.objects.unlink(o)
    collection.objects.link(o);o.data.materials.append(material);pieces.append(o);return o
def box(loc,dim,material,bevel=.002):
    bpy.ops.object.select_all(action='DESELECT');bpy.ops.mesh.primitive_cube_add(size=1,location=loc)
    o=track(bpy.context.object,material);o.dimensions=dim;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bevel:
        m=o.modifiers.new('Edge radius','BEVEL');m.width=bevel;m.segments=2;o.modifiers.new('Weighted normals','WEIGHTED_NORMAL')
    return o
def cylinder(loc,r,d,material,axis='Z',vertices=24):
    bpy.ops.object.select_all(action='DESELECT');bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=r,depth=d,location=loc)
    o=track(bpy.context.object,material)
    if axis=='X':o.rotation_euler.y=math.pi/2
    if axis=='Y':o.rotation_euler.x=math.pi/2
    m=o.modifiers.new('Rim radius','BEVEL');m.width=min(.0015,r*.1);m.segments=2;o.modifiers.new('Weighted normals','WEIGHTED_NORMAL')
    for f in o.data.polygons:f.use_smooth=True
    return o
def rod(a,b,r,material):
    a=Vector(a);b=Vector(b);o=cylinder((a+b)/2,r,(b-a).length,material);o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler();return o
def tube(path,r,thick,material,n=24):
    path=[Vector(p) for p in path];vs=[];fs=[]
    for i,p in enumerate(path):
        tangent=path[min(i+1,len(path)-1)]-path[max(i-1,0)];tangent.normalize()
        u=tangent.cross(Vector((0,0,1)))
        if u.length<.01:u=tangent.cross(Vector((0,1,0)))
        u.normalize();v=tangent.cross(u).normalized()
        for rr in [r,r-thick]:
            for j in range(n):vs.append(p+rr*(u*math.cos(j*math.tau/n)+v*math.sin(j*math.tau/n)))
    for i in range(len(path)-1):
        for k in [0,1]:
            for j in range(n):
                a=i*2*n+k*n+j;b=i*2*n+k*n+(j+1)%n;c=b+2*n;d=a+2*n
                fs.append((a,b,c,d) if k==0 else (d,c,b,a))
    for i in [0,len(path)-1]:
        for j in range(n):
            a=i*2*n+j;b=i*2*n+(j+1)%n;fs.append((a,b,b+n,a+n))
    mesh=bpy.data.meshes.new('Hollow tube');mesh.from_pydata(vs,[],fs);mesh.update()
    o=bpy.data.objects.new('Tube',mesh);collection.objects.link(o);mesh.materials.append(material);pieces.append(o)
    for f in mesh.polygons:f.use_smooth=True
    return o
def channel(path,width=.40):
    # Three strips form each folded rail, plus separate ladder rungs.
    for sign in [-1,1]:
        for z,height,spread in [(.045,.09,.003),(.003,.006,.020),(.087,.006,.020)]:
            vs=[];fs=[]
            for i,p in enumerate(path):
                tangent=Vector(path[min(i+1,len(path)-1)])-Vector(path[max(i-1,0)]);tangent.normalize();normal=Vector((-tangent.y,tangent.x,0))
                center=Vector(p)+normal*sign*width/2+Vector((0,0,z))
                vs.extend([center+normal*q+Vector((0,0,h)) for q,h in [(-spread/2,-height/2),(spread/2,-height/2),(spread/2,height/2),(-spread/2,height/2)]])
            for i in range(len(path)-1):
                for k in range(4):fs.append((i*4+k,i*4+(k+1)%4,(i+1)*4+(k+1)%4,(i+1)*4+k))
            fs.extend([(3,2,1,0),tuple(range(len(vs)-4,len(vs)))])
            mesh=bpy.data.meshes.new('Folded tray rail');mesh.from_pydata(vs,[],fs);mesh.update();o=bpy.data.objects.new('Folded tray rail',mesh);collection.objects.link(o);mesh.materials.append(zinc);pieces.append(o)
    length=sum((Vector(b)-Vector(a)).length for a,b in zip(path,path[1:]));count=max(2,round(length/.24))
    for j in range(count+1):
        f=j/count*(len(path)-1);i=min(int(f),len(path)-2);p=Vector(path[i]).lerp(Vector(path[i+1]),f-i)
        tangent=(Vector(path[i+1])-Vector(path[i])).normalized();o=box(p+Vector((0,0,.012)),(.025,width+.004,.014),zinc,.001);o.rotation_euler.z=math.atan2(tangent.y,tangent.x)
def arc(radius,z=0):return [(radius*math.cos(a),radius+radius*math.sin(a),z) for a in [-math.pi/2+i*math.pi/32 for i in range(17)]]
def bolts_x(x,y,z):cylinder((x,y,z),.009,.008,silver,'X',6)
def finish(name,label,ports=None):
    bpy.ops.object.select_all(action='DESELECT')
    for o in pieces:
        o.select_set(True);bpy.context.view_layer.objects.active=o;bpy.ops.object.convert(target='MESH');o.select_set(False)
    for o in pieces:o.select_set(True)
    bpy.context.view_layer.objects.active=pieces[0]
    if len(pieces)>1:bpy.ops.object.join()
    o=bpy.context.object;o.name='SM_WS_'+name;library.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    o.data.transform(o.matrix_world.copy());o.matrix_world=Matrix.Identity(4)
    o['module_label']=label;o['snap_ports_m']=json.dumps(ports or []);modules.append(o)
    o.data.calc_loop_triangles();records.append({'name':o.name,'label':label,'dimensions_m':list(o.dimensions),'ports_cm':[[v*100 for v in p] for p in ports or []],'triangles':len(o.data.loop_triangles)})
    pieces.clear();return o
# Steel members and connection hardware.
for z in [-.19,.19]:box((2,0,z),(4,.20,.02),dark)
box((2,0,0),(4,.012,.36),dark)
finish('IBeam_4m','I-BEAM / 4 m',[(0,0,0),(4,0,0)])
box((0,0,0),(.025,.28,.46),dark)
for y in [-.095,.095]:
    for z in [-.16,-.06,.06,.16]:bolts_x(.018,y,z)
finish('BeamConnection','BOLTED CONNECTION')
rod((0,0,0),(2,0,0),.016,dark)
for x in [.04,1.96]:
    box((x,0,0),(.11,.085,.008),dark);cylinder((x,0,.009),.012,.015,silver,'Z',6)
finish('Brace_2m','BRACE / 2 m',[(0,0,0),(2,0,0)])
# Tray family, with consistent joining planes and routing origins.
channel([(0,0,0),(2,0,0)]);finish('TrayStraight_2m','LADDER TRAY / 2 m',[(0,0,0),(2,0,0)])
channel(arc(.60));finish('TrayElbow90','TRAY ELBOW / 90 deg',[(0,0,0),(.6,.6,0)])
# Open T junction: omit the top middle rail and put rungs around the side exit.
channel([(0,0,0),(.65,0,0)]);channel([(1.05,0,0),(1.70,0,0)])
channel([(.85,.20,0),(.85,.85,0)])
box((.85,-.20,.045),(.40,.003,.09),zinc,0)
box((.85,0,.01),(.40,.40,.012),zinc,.002)
finish('TrayTee','TRAY T-JUNCTION',[(0,0,0),(1.7,0,0),(.85,.85,0)])
for sign in [-1,1]:
    a=Vector((0,sign*.20,.045));b=Vector((.60,sign*.10,.045));o=box((a+b)/2,((b-a).length,.003,.09),zinc,0);o.rotation_euler.z=math.atan2((b-a).y,(b-a).x)
for x,w in [(0,.4),(.2,.333),(.4,.267),(.6,.2)]:box((x,0,.012),(.025,w,.014),zinc,.001)
finish('TrayReducer','TRAY REDUCER / 400-200',[(0,0,0),(.6,0,0)])
box((0,0,.045),(.004,.40,.09),zinc,.001);finish('TrayEndCap','TRAY END CAP')
for j in range(7):tube([(0,-.14+j*.045,.033),(1,-.14+j*.045,.033),(2,-.14+j*.045,.033)],.014,.003,blue if j>4 else black,12)
for x in [.15,.70,1.30,1.85]:box((x,0,.033),(.012,.32,.031),black,.002)
finish('CableBundle_2m','CABLE BUNDLE / 2 m',[(0,0,0),(2,0,0)])
box((0,0,-.025),(.045,.56,.05),zinc)
for y in [-.245,.245]:
    rod((0,y,0),(0,y,.55),.006,silver)
    for z in [-.01,.01,.54]:cylinder((0,y,z),.012,.009,silver,'Z',6)
    box((0,y,.56),(.08,.08,.008),zinc)
finish('TrayHanger','TRAPEZE HANGER / 550 mm')
box((0,0,.13),(.04,.08,.26),zinc);box((.235,0,0),(.47,.055,.045),zinc)
rod((.03,0,.12),(.38,0,0),.009,zinc)
for z in [.045,.20]:bolts_x(.025,0,z)
finish('WallBracket','WALL SUPPORT / 470 mm')
# Electrical conduit and enclosure modules.
tube([(0,0,0),(1,0,0)],.013,.002,zinc)
for x in [.03,.97]:tube([(x-.02,0,0),(x+.02,0,0)],.016,.002,zinc)
finish('ConduitStraight_1m','CONDUIT / 1 m',[(0,0,0),(1,0,0)])
tube(arc(.15),.013,.002,zinc);finish('ConduitElbow90','CONDUIT ELBOW / 90 deg',[(0,0,0),(.15,.15,0)])
box((0,0,0),(.17,.075,.17),paint,.008);box((0,-.043,0),(.16,.008,.16),zinc,.004)
for x in [-.065,.065]:
    for z in [-.065,.065]:cylinder((x,-.05,z),.004,.005,silver,'Y',6)
for x in [-.085,.085]:cylinder((x,0,0),.018,.007,zinc,'X')
finish('JunctionBox','JUNCTION BOX / 170 mm')
box((0,0,-.02),(.07,.035,.008),zinc,.002)
path=[(.016*math.cos(a),0,.016*math.sin(a)) for a in [i*math.pi/12 for i in range(13)]]
for a,b in zip(path,path[1:]):rod(a,b,.002,zinc)
for x in [-.026,.026]:cylinder((x,0,-.011),.004,.008,silver,'Z',6)
finish('ConduitClamp','CONDUIT SADDLE')
box((0,0,0),(.60,.18,.90),paint,.012)
for x in [-.245,.245]:
    for z in [-.38,.38]:box((x,.105,z),(.045,.035,.08),zinc)
for x in [-.18,-.06,.06,.18]:cylinder((x,0,-.46),.018,.02,black)
finish('DistributionCabinet','DISTRIBUTION CABINET / 900 mm')
box((.28,-.10,0),(.56,.012,.86),paint,.006)
box((.50,-.113,0),(.018,.016,.105),dark,.004)
for z in [-.30,.30]:box((.014,-.098,z),(.028,.025,.085),zinc)
# Geometric warning triangle and lightning mark on door.
mesh=bpy.data.meshes.new('Warning triangle');mesh.from_pydata([(.225,-.108,-.01),(.335,-.108,-.01),(.28,-.108,.09)],[],[(0,1,2)]);mesh.update()
o=bpy.data.objects.new('Warning triangle',mesh);collection.objects.link(o);mesh.materials.append(yellow);pieces.append(o)
box((.282,-.110,.035),(.006,.002,.035),black,.0001)
finish('DistributionDoor','CABINET DOOR / HINGE PIVOT')
# Fire pipe family with hollow ends, couplings, flanged branch and sprinkler.
tube([(0,0,0),(2,0,0)],.042,.003,red)
for x in [.04,1.96]:tube([(x-.024,0,0),(x+.024,0,0)],.053,.005,red)
finish('FirePipeStraight_2m','FIRE PIPE / 2 m',[(0,0,0),(2,0,0)])
tube(arc(.20),.042,.003,red);finish('FirePipeElbow90','FIRE PIPE ELBOW / 90 deg',[(0,0,0),(.2,.2,0)])
tube([(0,0,0),(1,0,0)],.042,.003,red);tube([(.5,0,0),(.5,.35,0)],.027,.003,red)
for x in [.04,.96]:tube([(x-.025,0,0),(x+.025,0,0)],.053,.005,red)
finish('FirePipeTee','FIRE PIPE TEE',[(0,0,0),(1,0,0),(.5,.35,0)])
tube([(0,0,0),(0,0,-.07)],.013,.002,brass)
cylinder((0,0,-.078),.019,.016,brass,'Z',6)
for y in [-.015,.015]:rod((0,y,-.086),(0,y*.45,-.126),.0025,brass)
cylinder((0,0,-.108),.002,.03,red)
cylinder((0,0,-.139),.030,.003,brass)
for j in range(12):
    a=j*math.tau/12;rod((.024*math.cos(a),.024*math.sin(a),-.139),(.032*math.cos(a),.032*math.sin(a),-.139),.0015,brass)
finish('SprinklerHead','SPRINKLER / PENDENT')
tube([(0,0,0),(.014,0,0)],.048,.004,zinc)
rod((.007,0,.048),(.007,0,.55),.006,silver);box((.007,0,.55),(.07,.07,.008),zinc)
finish('PipeHanger','PIPE RING HANGER')
# LED linear fixture with hanging lugs, rear driver and end sockets.
box((.60,0,0),(1.20,.10,.047),zinc,.008)
box((.60,0,-.027),(1.14,.079,.01),white,.003)
for x in [.06,1.14]:box((x,0,.034),(.05,.035,.018),zinc)
box((.60,0,.037),(.30,.047,.03),dark)
finish('LEDLinear_1200','LED LUMINAIRE / 1.2 m',[(0,0,0),(1.2,0,0)])
# Rectangular hollow ducts, visible folded flanges and bolt rows.
def duct(path,width=.35,height=.25):
    vs=[];fs=[]
    for i,p in enumerate(path):
        tangent=Vector(path[min(i+1,len(path)-1)])-Vector(path[max(0,i-1)]);tangent.normalize();normal=Vector((-tangent.y,tangent.x,0))
        for w,h in [(width,height),(width-.004,height-.004)]:
            vs.extend([Vector(p)+normal*y+Vector((0,0,z)) for y,z in [(-w/2,-h/2),(w/2,-h/2),(w/2,h/2),(-w/2,h/2)]])
    for i in range(len(path)-1):
        for ring in [0,1]:
            for j in range(4):
                a=i*8+ring*4+j;b=i*8+ring*4+(j+1)%4;fs.append((a,b,b+8,a+8))
    for i in [0,len(path)-1]:
        for j in range(4):fs.append((i*8+j,i*8+(j+1)%4,i*8+4+(j+1)%4,i*8+4+j))
    mesh=bpy.data.meshes.new('Sheetmetal duct');mesh.from_pydata(vs,[],fs);mesh.update();o=bpy.data.objects.new('Duct',mesh);collection.objects.link(o);mesh.materials.append(zinc);pieces.append(o)
def flange(x):
    for y in [-.186,.186]:box((x,y,0),(.016,.016,.278),zinc,.001)
    for z in [-.136,.136]:box((x,0,z),(.016,.388,.016),zinc,.001)
    for y in [-.185,.185]:
        for z in [-.11,.11]:bolts_x(x+.012,y,z)
duct([(0,0,0),(1.5,0,0)]);flange(.008);flange(1.492)
for x in [.5,1]:
    for y in [-.176,.176]:box((x,y,0),(.012,.004,.25),zinc,.001)
finish('DuctStraight_1500','VENT DUCT / 1.5 m',[(0,0,0),(1.5,0,0)])
duct(arc(.45));flange(.008)
for y in [.4,.45,.5]:box((.45,y,.128),(.35,.007,.006),zinc,.001)
finish('DuctElbow90','DUCT ELBOW / 90 deg',[(0,0,0),(.45,.45,0)])
for y in [-.186,.186]:box((0,y,0),(.025,.02,.28),zinc)
for z in [-.136,.136]:box((0,0,z),(.025,.39,.02),zinc)
for z in [-.105+i*.03 for i in range(8)]:
    o=box((0,0,z),(.035,.348,.007),zinc,.001);o.rotation_euler.y=math.radians(25)
finish('DuctGrille','VENT GRILLE / 350 x 250')
manifest={'date':'2026-10-14','units':'meters in Blender; centimeters for Unreal','axes':'X along straight routing, Z up','unreal_applied':False,'modules':records}
(OUT/'modules.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'warehouse_services.blend'))
print('LIBRARY_READY',len(modules),'modules')
