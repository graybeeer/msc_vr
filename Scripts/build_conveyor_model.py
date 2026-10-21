"""Original roller conveyor, exported from an isolated background Blender scene."""
import bpy
import json
import math
import sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Art/Equipment/Conveyor'
PARTS = OUT / 'UnrealParts'
PARTS.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1
assets = bpy.data.collections.new('CONVEYOR_ORIGINAL')
studio = bpy.data.collections.new('PREVIEW_ONLY')
scene.collection.children.link(assets)
scene.collection.children.link(studio)
groups, colliders, materials = {}, {}, {}

def material(name, color, metallic, roughness):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    mat.diffuse_color = (*color, 1)
    principled = mat.node_tree.nodes.get('Principled BSDF')
    principled.inputs['Base Color'].default_value = (*color, 1)
    principled.inputs['Metallic'].default_value = metallic
    principled.inputs['Roughness'].default_value = roughness
    materials[name] = dict(color=color, metallic=metallic, roughness=roughness)
    return mat

steel = material('Lift_GalvanizedSteel', (.48,.52,.56), .95,.36)
machined = material('Lift_MachinedSteel', (.64,.67,.70), .97,.24)
dark = material('Lift_Graphite', (.028,.034,.041), .5,.48)
yellow = material('Lift_SafetyYellow', (.95,.55,.012), .24,.39)
rubber = material('Lift_Rubber', (.014,.018,.021), .03,.72)
red = material('Lift_RedPowderCoat', (.58,.013,.018), .48,.34)

def place(obj, name, mat, group):
    obj.name = name
    for collection in list(obj.users_collection): collection.objects.unlink(obj)
    (studio if group == 'studio' else assets).objects.link(obj)
    if mat: obj.data.materials.append(mat)
    if group != 'studio': groups.setdefault(group, []).append(obj)
    return obj

def box(name, pos, size, mat, group, bevel=.002):
    bpy.ops.mesh.primitive_cube_add(size=1, location=pos)
    obj = place(bpy.context.object, name, mat, group)
    obj.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        modifier = obj.modifiers.new('Machined safe edge', 'BEVEL')
        modifier.width = bevel
        modifier.segments = 2
        obj.modifiers.new('Fabricated normal', 'WEIGHTED_NORMAL')
    return obj

def cylinder(name, pos, radius, length, mat, group, axis='Z', vertices=48, bevel=.001):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=length, location=pos)
    obj = place(bpy.context.object, name, mat, group)
    if axis == 'X': obj.rotation_euler.y = math.pi/2
    if axis == 'Y': obj.rotation_euler.x = math.pi/2
    if bevel:
        modifier = obj.modifiers.new('Turned rim', 'BEVEL')
        modifier.width = bevel
        modifier.segments = 2
    for face in obj.data.polygons: face.use_smooth = len(face.vertices) == 4
    return obj

def hull_box(group, pos, size):
    name = 'UCX_' + group + '_%02d' % len(colliders.setdefault(group, []))
    obj = box(name, pos, size, None, 'collision', 0)
    groups['collision'].remove(obj)
    colliders[group].append(obj)
    obj.hide_render = True
    obj.display_type = 'WIRE'

def join_asset(group):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in groups[group]:
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        for modifier in list(obj.modifiers):
            bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.context.view_layer.objects.active = groups[group][0]
    bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = group
    scene.cursor.location = (0,0,0)
    bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    return obj

def export_asset(group):
    obj = join_asset(group)
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    for collision in colliders[group]: collision.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=str(PARTS/(group+'.fbx')),use_selection=True,object_types={'MESH'},
        global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',
        mesh_smooth_type='FACE',use_mesh_modifiers=True,bake_anim=False)
    return obj

def hull_cylinder(group, pos, radius, height, vertices=32):
    name = 'UCX_' + group + '_%02d' % len(colliders.setdefault(group, []))
    obj = cylinder(name,pos,radius,height,None,'collision',vertices=vertices,bevel=0)
    groups['collision'].remove(obj)
    colliders[group].append(obj)
    obj.hide_render = True
    obj.display_type = 'WIRE'

def ring(name, radius, width, height, z, mat, group, segments=48, collision=False):
    # Disjoint convex sectors retain the open centre; no full-diameter convex hull.
    angle = 2*math.pi/segments
    for i in range(segments):
        a = (i+.5)*angle
        centre = (radius*math.cos(a),radius*math.sin(a),z)
        size = (width,2*radius*math.sin(angle*.5)+.002,height)
        obj = box(name,centre,size,mat,group,.001)
        obj.rotation_euler.z = a
        if collision:
            hull_box(group,centre,size)
            colliders[group][-1].rotation_euler.z = a

def build_turntable_only():
    base, rotor = 'SM_Conveyor_TurntableBase', 'SM_Conveyor_TurntableRotor'
    # Base uses floor origin. The rotor's mesh coordinates use its mechanical
    # axis at world Z35cm, with its hub/disc wholly below the roller bottom34cm.
    cylinder('Rotary pedestal', (0,0,.115), .28,.21,dark,base,vertices=64)
    hull_cylinder(base,(0,0,.115),.28,.21)
    cylinder('Pedestal bearing seat', (0,0,.2075), .25,.025,machined,base)
    ring('Low folded circular chassis',1.10,.10,.045,.1925,steel,base,collision=True)
    ring('Safety yellow outside flange',1.135,.022,.010,.222,yellow,base)
    for i in range(8):
        a = (i+.5)*math.pi/4
        x,y = 1.08*math.cos(a),1.08*math.sin(a)
        cylinder('Adjustable chassis leg',(x,y,.092),.027,.15,steel,base)
        hull_box(base,(x,y,.092),(.054,.054,.15))
        cylinder('Levelling foot',(x,y,.009),.055,.018,rubber,base)
        hull_cylinder(base,(x,y,.009),.055,.018,16)
        cylinder('Leg locknut',(x,y,.037),.036,.016,machined,base,vertices=6)
    for a in [math.pi/4,3*math.pi/4,5*math.pi/4,7*math.pi/4]:
        # Motor/PLC covers are below22cm and away from the rotating hub.
        x,y = .73*math.cos(a),.73*math.sin(a)
        obj=box('Radial base brace',(x*.64,y*.64,.143),(.75,.06,.07),steel,base)
        obj.rotation_euler.z=a
        hull_box(base,(x*.64,y*.64,.143),(.75,.06,.07))
        colliders[base][-1].rotation_euler.z=a
        for offset in [-.026,.026]:
            cylinder('Chassis fixing bolt',(x+offset,y,.224),.009,.012,machined,base,vertices=6)
    box('Rotary drive geared housing',(.55,.55,.12),(.31,.23,.20),dark,base,.012)
    hull_box(base,(.55,.55,.12),(.31,.23,.20))
    for y in [.47,.51,.55,.59,.63]:
        box('Drive cooling fin',(.55,y,.226),(.27,.008,.008),steel,base,.001)
    box('Protected junction box',(-.67,-.67,.13),(.22,.18,.16),dark,base,.008)
    hull_box(base,(-.67,-.67,.13),(.22,.18,.16))
    box('Warning label',(-.67,-.765,.15),(.14,.003,.035),yellow,base,.001)
    # All rotor dimensions below are relative to the Z35cm mechanical axis.
    cylinder('Rotating bearing hub',(0,0,-.075),.22,.05,machined,rotor,vertices=64)
    hull_cylinder(rotor,(0,0,-.075),.22,.05)
    cylinder('Low rotating disc',(0,0,-.065),.97,.03,dark,rotor,vertices=96)
    hull_cylinder(rotor,(0,0,-.065),.97,.03,64)
    ring('Turned perimeter reinforcement',.955,.026,.026,-.071,steel,rotor)
    for side in [-1,1]:
        box('Rotor lower stiffener',(0,side*.45,-.085),(1.62,.045,.025),steel,rotor)
        hull_box(rotor,(0,side*.45,-.085),(1.62,.045,.025))
    for i in range(21):
        x = -.95+i*.095
        half = (2*math.sqrt(1-(abs(x)+.03)**2)-.04)*.5
        for side in [-1,1]:
            # Low saddles remain belowZ33.5; rollers occupyZ34..40.
            y=side*(half-.005)
            box('Roller bearing saddle',(x,y,-.035),(.045,.035,.040),steel,rotor,.001)
            hull_box(rotor,(x,y,-.035),(.045,.035,.040))
            cylinder('Saddle fastening',(x,y,-.013),.006,.004,machined,rotor,vertices=6)
    for i in range(12):
        a=i*math.pi/6
        cylinder('Hub flange bolt',(.17*math.cos(a),.17*math.sin(a),-.047),.009,.006,machined,rotor,vertices=6)
    bpy.context.view_layer.update()
    base_points=[obj.matrix_world @ vertex.co for obj in colliders[base] for vertex in obj.data.vertices]
    rotor_points=[obj.matrix_world @ vertex.co for obj in colliders[rotor] for vertex in obj.data.vertices]
    assert max(p.z for p in base_points)<=.22001, 'Static pedestal interferes with rotating body'
    assert min(p.z for p in rotor_points)+.35>=.24999, 'Rotor bottom must clear the22cm pedestal'
    assert max(p.z for p in rotor_points)+.35<.34, 'Rotor collider interferes with native roller bottoms'
    assert max(math.hypot(p.x,p.y) for p in rotor_points)<=1.00001, 'Rotor exceeds the200cm circular envelope'
    exports = {name:export_asset(name) for name in [base,rotor]}
    # Preview contains the21 separate roller bodies used by the native component.
    exports[rotor].location.z=.35
    for i in range(21):
        x=-.95+i*.095
        length=2*math.sqrt(1-(abs(x)+.03)**2)-.04
        cylinder('PREVIEW powered roller %02d'%i,(x,0,.37),.03,length,machined,'studio','Y',64)
    box('Preview studio floor',(0,0,-.035),(200,200,.05),rubber,'studio',0)
    def point(obj, target): obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
    data=bpy.data.cameras.new('Turntable preview camera')
    camera=bpy.data.objects.new('Turntable preview camera',data)
    studio.objects.link(camera)
    camera.location=(3.6,-4,3.1)
    point(camera,(0,0,.2))
    data.type='ORTHO'
    data.ortho_scale=3.65
    scene.camera=camera
    for name,pos,energy,size in [('Key',(1,-3,5),1600,4),('Fill',(-3,-1,2),1000,3),('Rim',(0,4,4),1800,3)]:
        data=bpy.data.lights.new(name,'AREA');data.energy=energy;data.shape='DISK';data.size=size
        obj=bpy.data.objects.new(name,data);studio.objects.link(obj);obj.location=pos;point(obj,(0,0,.3))
    if scene.world is None: scene.world=bpy.data.worlds.new('Turntable preview world')
    scene.world.color=(.15,.15,.15)
    scene.render.engine='CYCLES'
    scene.cycles.samples=24
    scene.cycles.use_denoising=True
    scene.render.threads_mode='FIXED';scene.render.threads=4
    scene.render.resolution_x=1600;scene.render.resolution_y=1200;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG';scene.render.filepath=str(OUT/'Conveyor_Turntable_Preview.png')
    scene.view_settings.view_transform='AgX'
    path=OUT/'manifest.json'
    manifest=json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
    manifest.setdefault('materials',{}).update(materials)
    manifest.setdefault('files',{}).update({name:'UnrealParts/'+name+'.fbx' for name in exports})
    manifest.setdefault('collision',{}).update({name:len(colliders[name]) for name in exports})
    manifest['turntable_reference_cm']=dict(date='2026-10-21',roller_diameter=200,base_outer_diameter=230,
        surface_height=40,base_origin='floor at rotary centre',rotor_origin='mechanical axis, component at Z35cm',
        base_pedestal_top=22,rotor_low_hub_bottom=25,rotor_disc_top=30,saddle_top=33.5,
        roller_count=21,roller_radius=3,roller_centre_z=37,roller_pitch=9.5,
        roller_width='2*sqrt(10000-(abs(x)+3)^2)-4 cm; x=-95..95 cm',
        rotor_mass_kg=80,roller_mass_kg=8,contact='Cargo contacts native rollers; low rotor and base do not cover the carrying plane')
    path.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'conveyor_turntable.blend'))
    if '--skip-render' not in sys.argv: bpy.ops.render.render(write_still=True)
    print('CONVEYOR_TURNTABLE_MODEL_EXPORTED',str(OUT))

if '--turntable-only' in sys.argv:
    build_turntable_only()
    raise SystemExit(0)

frame, roller = 'SM_Conveyor_Frame', 'SM_Conveyor_Roller'
# Reference dimensions: X3m line, 1.25m roller width, roller crown at Z.4m.
# No plate or collider occupies the carrying plane; each roller is a real separate body.
for side in [-1,1]:
    y = side*.655
    box('Folded longitudinal rail', (0,y,.31), (3,.06,.12), steel, frame)
    hull_box(frame, (0,y,.31), (3,.06,.12))
    box('Rail lower flange', (0,y-side*.025,.252), (3,.085,.012), steel, frame)
    box('Red drive chain guard', (0,y+side*.038,.315), (2.88,.018,.074), dark, frame)
    hull_box(frame, (0,y+side*.038,.315), (2.88,.018,.074))
    # Warning strips are behind the roller ends and cannot touch a passing pallet.
    box('Safety yellow rail lip', (0,y,.373), (3,.064,.006), yellow, frame, .001)
    for x in [-1.32,0,1.32]:
        box('Adjustable leg', (x,y,.137), (.06,.06,.238), steel, frame)
        hull_box(frame, (x,y,.137), (.06,.06,.238))
        box('Leg telescopic insert', (x,y,.061), (.042,.042,.112), dark, frame)
        cylinder('Levelling stem', (x,y,.022), .009,.035, machined, frame)
        cylinder('Neoprene floor foot', (x,y,.008), .047,.016, rubber, frame)
        hull_box(frame, (x,y,.008), (.094,.094,.016))
        for dz in [.09,.14,.19]:
            cylinder('Leg adjustment bolt', (x,y-side*.033,dz), .006,.014,machined,frame,'Y',6)
    for x in [-1.0,1.0]:
        # Photoeyes stand back from the lift gate and end picking area.
        box('Photoeye angle bracket', (x,y+side*.035,.449), (.028,.025,.145),steel,frame)
        box('Photoelectric sensor', (x,y+side*.015,.469), (.036,.036,.036),dark,frame)
        cylinder('Photoeye lens', (x,y-side*.005,.469), .011,.007,red,frame,'Y')
        hull_box(frame, (x,y+side*.03,.449), (.04,.07,.145))
    for x in [-1.44,-.95,-.46,.03,.52,1.01,1.44]:
        cylinder('M10 frame fastening', (x,y+side*.037,.324), .009,.009,machined,frame,'Y',6)
for x in [-1.32,0,1.32]:
    box('Lower transverse brace', (x,0,.205), (.055,1.29,.055), steel, frame)
    hull_box(frame, (x,0,.205), (.055,1.29,.055))
box('Geared drive motor housing', (-1.12,.775,.255), (.28,.20,.17),dark,frame,.015)
hull_box(frame, (-1.12,.775,.255), (.28,.20,.17))
cylinder('Drive motor gearbox', (-1.12,.694,.255), .063,.055,steel,frame,'Y')
for dx in [-.09,-.045,0,.045,.09]:
    box('Motor cooling fin', (-1.12+dx,.775,.345), (.008,.19,.018),steel,frame,.001)
box('Cable gland', (-.97,.774,.246), (.028,.04,.045),rubber,frame)
box('Compact local junction box', (.55,.724,.292), (.22,.10,.11),dark,frame,.008)
hull_box(frame, (.55,.724,.292), (.22,.10,.11))
cylinder('Local emergency stop mushroom', (.55,.792,.322), .025,.03,red,frame,'Y')
box('Warning plate', (.55,.78,.277), (.16,.003,.032),yellow,frame,.001)

# Native roller is exactly 100cm diameter x100cm along local Z. Runtime nonuniform
# scale maps this turned steel cylinder to radius3cm and 125cm bearing width.
cylinder('Turned steel roller shell', (0,0,0), .5,.97,machined,roller,vertices=128,bevel=.001)
for side in [-1,1]:
    cylinder('Press fit end cap', (0,0,side*.488), .48,.016,steel,roller,vertices=96,bevel=.001)
    cylinder('Flush bearing centre', (0,0,side*.498), .13,.004,dark,roller,vertices=48,bevel=.0005)
collision = cylinder('UCX_'+roller+'_00',(0,0,0),.5,1,None,'collision',vertices=128,bevel=0)
groups['collision'].remove(collision)
colliders[roller] = [collision]
collision.hide_render = True
collision.display_type = 'WIRE'

exports = {}
for name in [frame,roller]:
    obj = join_asset(name)
    exports[name] = obj
    if '--frame-only' in sys.argv and name!=frame: continue
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    for collision in colliders[name]: collision.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=str(PARTS/(name+'.fbx')),use_selection=True,object_types={'MESH'},
        global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',
        mesh_smooth_type='FACE',use_mesh_modifiers=True,bake_anim=False)

# Linked preview rollers use exactly the runtime geometry and spacing.
exports[roller].hide_render = True
for i in range(34):
    obj = bpy.data.objects.new('PREVIEW actual powered roller %02d'%i,exports[roller].data)
    studio.objects.link(obj)
    obj.location = (-1.47+i*(2.94/33),0,.37)
    obj.rotation_euler.x = math.pi/2
    obj.scale = (.06,.06,1.25)
floor = box('Preview studio floor',(0,0,-.035),(200,200,.05),rubber,'studio',0)

def point(obj, target): obj.rotation_euler = (Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
camera_data = bpy.data.cameras.new('Asset preview camera')
camera = bpy.data.objects.new('Asset preview camera',camera_data)
studio.objects.link(camera)
camera.location = (4.2,-4.8,3.25)
point(camera,(0,0,.23))
camera_data.type='ORTHO'
camera_data.ortho_scale = 4.5
scene.camera = camera
for name, pos, energy, size in [('Key',(1,-3,5),1800,4),('Fill',(-3,-1,2),1200,3),('Rim',(0,4,4),2200,3)]:
    data=bpy.data.lights.new(name,'AREA')
    data.energy=energy
    data.shape='DISK'
    data.size=size
    obj=bpy.data.objects.new(name,data)
    studio.objects.link(obj)
    obj.location=pos
    point(obj,(0,0,.3))
if scene.world is None: scene.world=bpy.data.worlds.new('Conveyor preview world')
scene.world.color=(.15,.15,.15)
scene.render.engine='CYCLES'
scene.cycles.samples=32
scene.cycles.use_denoising=True
scene.render.resolution_x=1600
scene.render.resolution_y=1000
scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
scene.render.filepath=str(OUT/'Conveyor_Preview.png')
scene.view_settings.view_transform='AgX'
manifest_path=OUT/'manifest.json'
previous_manifest=json.loads(manifest_path.read_text(encoding='utf-8')) if manifest_path.exists() else {}
manifest=dict(date='2026-10-20',authoring='Original Blender roller conveyor; no proprietary CAD',
    frame_reference_cm=dict(length=300,roller_width=125,surface_height=40,origin='floor at machine centre'),
    roller_reference_cm=dict(diameter=100,length=100,axis='local Z',runtime_scale='X/Y=diameter/100, Z=width/100, Roll=90deg'),
    materials=materials,files={name:'UnrealParts/'+name+'.fbx' for name in exports},
    collision={frame:len(colliders[frame]),roller:1},physics='No deck carrying-plane collider. Separate finite-torque driven roller bodies supply contact.')
for key in ['materials','files','collision']:
    previous_manifest.setdefault(key,{}).update(manifest.pop(key))
previous_manifest.update(manifest)
previous_manifest['frame_photoeye_x_cm']=[-100,100]
manifest_path.write_text(json.dumps(previous_manifest,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'conveyor.blend'))
if '--skip-render' not in sys.argv: bpy.ops.render.render(write_still=True)
print('CONVEYOR_MODEL_EXPORTED', str(OUT))
