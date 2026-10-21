"""Original one-pallet tower model; run in an owned Blender background process."""
import bpy
import json
import math
import sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
POWERED_SHAFT = '--powered-shaft-only' in sys.argv
EXTENSIONS = '--transport-extensions' in sys.argv or POWERED_SHAFT
FLOORS = next((int(arg.split('=', 1)[1]) for arg in sys.argv if arg.startswith('--floors=')), 3) if POWERED_SHAFT else (4 if EXTENSIONS else 3)
if POWERED_SHAFT and FLOORS not in (3, 4):
    raise ValueError('Powered shaft variants support 3 or 4 floors')
EXTRA_HEIGHT = (FLOORS-3)*4
OUT = ROOT / ('Art/Equipment/PalletLiftExtensions' if EXTENSIONS else 'Art/Equipment/PalletLift')
PARTS = OUT / 'UnrealParts'
PARTS.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1
asset = bpy.data.collections.new('PALLET_LIFT_ORIGINAL')
studio = bpy.data.collections.new('PREVIEW_ONLY')
scene.collection.children.link(asset)
scene.collection.children.link(studio)
groups = {}
colliders = {}
values = {}

def material(name, color, metal, rough):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    mat.diffuse_color = (*color, 1)
    node = mat.node_tree.nodes.get('Principled BSDF')
    node.inputs['Base Color'].default_value = (*color, 1)
    node.inputs['Metallic'].default_value = metal
    node.inputs['Roughness'].default_value = rough
    values[name] = {'color': color, 'metallic': metal, 'roughness': rough}
    return mat

silver = material('Lift_GalvanizedSteel', (.48, .52, .56), .95, .36)
red = material('Lift_RedPowderCoat', (.58, .013, .018), .48, .34)
dark = material('Lift_Graphite', (.028, .034, .041), .5, .48)
bolt = material('Lift_MachinedSteel', (.64, .67, .70), .97, .24)
yellow = material('Lift_SafetyYellow', (.95, .55, .012), .24, .39)
rubber = material('Lift_Rubber', (.014, .018, .021), .03, .72)

def finish(obj, name, mat, group):
    obj.name = name
    for collection in list(obj.users_collection):
        collection.objects.unlink(obj)
    (studio if group == 'studio' else asset).objects.link(obj)
    if mat:
        obj.data.materials.append(mat)
    if group != 'studio':
        groups.setdefault(group, []).append(obj)
    return obj

def box(name, pos, size, mat, group, bevel=.002):
    bpy.ops.mesh.primitive_cube_add(size=1, location=pos)
    obj = finish(bpy.context.object, name, mat, group)
    obj.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        mod = obj.modifiers.new('Manufactured edge', 'BEVEL')
        mod.width = bevel
        mod.segments = 2
        obj.modifiers.new('Corner normals', 'WEIGHTED_NORMAL')
    return obj

def cyl(name, pos, radius, depth, mat, group, axis='Z', vertices=20):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=pos)
    obj = finish(bpy.context.object, name, mat, group)
    if axis == 'X':
        obj.rotation_euler.y = math.pi / 2
    elif axis == 'Y':
        obj.rotation_euler.x = math.pi / 2
    mod = obj.modifiers.new('Turned edge', 'BEVEL')
    mod.width = .0015
    mod.segments = 2
    for face in obj.data.polygons:
        face.use_smooth = True
    return obj

def beam(name, a, b, width, depth, mat, group):
    start, end = Vector(a), Vector(b)
    obj = box(name, (start + end) / 2, (width, depth, (end-start).length), mat, group)
    obj.rotation_euler = (end-start).to_track_quat('Z', 'Y').to_euler()
    return obj

def hull_box(group, pos, size):
    obj = box('UCX_' + group + '_%02d' % len(colliders.setdefault(group, [])), pos, size, None, 'collision', 0)
    groups['collision'].remove(obj)
    colliders[group].append(obj)
    obj.hide_render = True
    obj.display_type = 'WIRE'
    return obj

frame = f'SM_PalletLift_FramePowered{FLOORS}' if POWERED_SHAFT else ('SM_PalletLift_Frame4' if EXTENSIONS else 'SM_PalletLift_Frame')
carrier = 'SM_PalletLift_CarrierPoweredFrame' if EXTENSIONS else 'SM_PalletLift_Carrier'
gate = 'SM_PalletLift_Gate'
# Local -X is the open loading side. Floor clearances are kept free of braces.
for x in [-.83, .83]:
    for y in [-.83, .83]:
        box('Column base plate', (x, y, .018), (.14, .14, .036), silver, frame)
        box('Steel lift column', (x, y, 5.38+EXTRA_HEIGHT/2), (.12, .12, 10.72+EXTRA_HEIGHT), silver, frame, .004)
        box('Column inner rail', (x-.064 if x>0 else x+.064, y, 5.38+EXTRA_HEIGHT/2), (.018, .055, 10.64+EXTRA_HEIGHT), dark, frame)
        for dx in [-.047, .047]:
            for dy in [-.047, .047]:
                cyl('Base anchoring bolt', (x+dx, y+dy, .044), .008, .018, bolt, frame, vertices=6)
for y in [-.899, .899]:
    box('Red chain cover', (.76, y, 5.45+EXTRA_HEIGHT/2), (.22, .002, 10.70+EXTRA_HEIGHT), red, frame, .0005)
    box('Cover folded outer edge', (.872, y*.99, 5.45+EXTRA_HEIGHT/2), (.025, .025, 10.70+EXTRA_HEIGHT), red, frame)
    # Individual dark chain links are visible inside the guards.
    for index in range(130+round(EXTRA_HEIGHT/.081)):
        z = .18+index*.081
        box('Roller chain link', (.688, y*.89, z), (.012, .026, .048), dark, frame, .001)
        cyl('Roller chain pin', (.68, y*.89, z), .008, .018, bolt, frame, 'X', 10)
for z in ([.20, 3.4, 4.2, 7.4, 8.2, 11.4, 12.2, 14.72] if FLOORS == 4 else [.20, 3.4, 4.2, 7.4, 8.2, 10.72]):
    for y in [-.83, .83]:
        box('Side horizontal tie', (0, y, z), (1.66, .07, .09), silver, frame)
    box('Rear horizontal tie', (.83, 0, z), (.07, 1.66, .09), silver, frame)
for lo, hi in ([(.3,3.3), (4.3,7.3), (8.3,11.3), (12.3,14.6)] if FLOORS == 4 else [(.3,3.3), (4.3,7.3), (8.3,10.6)]):
    for y in [-.83, .83]:
        beam('Side diagonal brace', (-.76,y,lo), (.76,y,hi), .045,.045,silver,frame)
        beam('Side diagonal brace', (.76,y,lo), (-.76,y,hi), .045,.045,silver,frame)
    beam('Rear X brace', (.83,-.76,lo), (.83,.76,hi), .045,.045,silver,frame)
    beam('Rear X brace', (.83,.76,lo), (.83,-.76,hi), .045,.045,silver,frame)
for y in [-.83, .83]:
    box('Top motor beam', (0,y,10.78+EXTRA_HEIGHT), (1.78,.12,.12), silver,frame)
box('Top drive cross tie', (.40,0,10.78+EXTRA_HEIGHT), (.12,1.76,.12),silver,frame)
for y in [-.63,.63]:
    box('Gear reducer housing', (.30,y,10.96+EXTRA_HEIGHT), (.25,.21,.25),dark,frame,.018)
    cyl('Geared motor housing', (.01,y,10.97+EXTRA_HEIGHT),.12,.35,silver,frame,'X',32)
    for i in range(9):
        cyl('Motor cooling rib', (-.14+i*.032,y,10.97+EXTRA_HEIGHT),.126,.008,dark,frame,'X',32)
    cyl('Motor end cap',(-.20,y,10.97+EXTRA_HEIGHT),.122,.045,dark,frame,'X',32)
    cyl('Drive sprocket',(.64,y,10.96+EXTRA_HEIGHT),.14,.055,bolt,frame,'Y',32)
cyl('Linked drive shaft',(.64,0,10.96+EXTRA_HEIGHT),.026,1.5,bolt,frame,'Y')
for level in range(FLOORS):
    z=.40+4*level
    # Powered-only sills top out at floor +24cm, below the actual adjoining
    # conveyor rails (25cm) and roller envelopes (34cm). Legacy shafts retain
    # their original sill and are never overwritten by this variant export.
    sill_offset = .215 if POWERED_SHAFT else .075
    box('Landing sill',(-.73,0,z-sill_offset),(.12,1.54,.11),silver,frame)
    for y in [-.76,.76]:
        box('Landing gate post',(-.77,y,z+1.15),(.06,.06,2.30),silver,frame)
        box('Yellow gate marker',(-.808,y,z+1.6),(.012,.065,.24),yellow,frame)
        box('Gate sensor housing',(-.80,y,z+.34),(.045,.07,.09),dark,frame)
    box('Gate top guide',(-.77,0,z+2.35),(.07,1.62,.08),silver,frame)
    box('Level identification plate',(-.816,-.76,z+1.93),(.01,.15,.16),dark,frame)
# Each exported moving part is centred at its mechanical reference point.
for y in [-.595,.595]:
    box('Carrier side channel',(0,y,-.08),(1.25,.06,.12),silver,carrier)
    hull_box(carrier,(0,y,-.08),(1.25,.06,.12))
for x in [-.595,.595]:
    box('Carrier cross channel',(x,0,-.08),(.06,1.13,.12),dark,carrier)
    hull_box(carrier,(x,0,-.08),(.06,1.13,.12))
for i in range(0 if EXTENSIONS else 13):
    x=-.57+i*.095
    cyl('Conveyor steel roller',(x,0,-.03),.03,1.13,bolt,carrier,'Y',24)
    # Fine facets prevent coarse polygon crowns rocking a supported wooden pallet.
    obj=cyl('UCX_'+carrier+'_%02d'%len(colliders[carrier]),(x,0,-.03),.03,1.13,None,'collision','Y',128)
    for mod in list(obj.modifiers):obj.modifiers.remove(mod)
    groups['collision'].remove(obj);colliders[carrier].append(obj)
    obj.hide_render=True;obj.display_type='WIRE'
for y in [-.625,.625]:
    box('Carrier painted side edge',(0,y,-.035),(1.25,.012,.028),yellow,carrier,.001)
for y in [-.295,.295]:
    box('Gate border upright',(0,y,0),(.03,.035,2.3),silver,gate)
    hull_box(gate,(0,y,0),(.03,.035,2.3))
for z in [-1.1325,1.1325]:
    box('Gate border crossbar',(0,0,z),(.03,.625,.035),silver,gate)
    hull_box(gate,(0,0,z),(.03,.625,.035))
for y in [-.20,-.10,0,.10,.20]:
    box('Gate safety grille',(.002,y,0),(.018,.009,2.23),dark,gate,.001)
    hull_box(gate,(.002,y,0),(.018,.009,2.23))
for z in [-.75,-.25,.25,.75]:
    box('Gate horizontal grille',(.002,0,z),(.018,.58,.009),dark,gate,.001)
    hull_box(gate,(.002,0,z),(.018,.58,.009))
box('Gate red leading edge',(-.020,-.295,0),(.012,.04,2.29),red,gate)

def join_meshes(name, objects):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.hide_set(False);obj.select_set(True)
    bpy.context.view_layer.objects.active=objects[0]
    for obj in objects:
        bpy.context.view_layer.objects.active=obj
        for mod in list(obj.modifiers):
            bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.context.view_layer.objects.active=objects[0]
    bpy.ops.object.join()
    obj=bpy.context.object;obj.name=name
    scene.cursor.location=(0,0,0)
    bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    return obj

exported={}
for name in [frame,carrier,gate]:
    obj=join_meshes(name,groups[name]);exported[name]=obj
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True)
    for collider in colliders.get(name,[]):collider.select_set(True)
    bpy.context.view_layer.objects.active=obj
    if (POWERED_SHAFT and name == frame) or (not POWERED_SHAFT and (not EXTENSIONS or name!=gate)):
        bpy.ops.export_scene.fbx(filepath=str(PARTS/(name+'.fbx')),use_selection=True,object_types={'MESH'},global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',use_mesh_modifiers=True,bake_anim=False)
manifest={'date':'2026-10-20','authoring':'Original procedural model inspired by four-column pallet lifts; no OEM CAD or logo','frame_cm':[180,180,1120],'carrier_cm':[125,125],'carrier_origin':'Roller top Z=0; runtime component top Z=40cm at floor 0','gate_cm':[3,62.5,230],'materials':values,'files':{name:'UnrealParts/'+name+'.fbx' for name in exported},'collision':{'frame':'static complex mesh','carrier':'17 separate convex hulls','gate':'13 separate convex hulls'}}
if EXTENSIONS:
    manifest['frame_cm']=[180,180,1120+EXTRA_HEIGHT*100]
    manifest['files'].pop(gate)
    manifest['collision']['carrier']='4 separate convex channels; dynamic rollers are separate runtime bodies'
    previous = json.loads((OUT/'manifest.json').read_text(encoding='utf-8')) if (OUT/'manifest.json').exists() else {}
    if POWERED_SHAFT:
        manifest = previous or manifest
        manifest.setdefault('files', {})[frame]='UnrealParts/'+frame+'.fbx'
        manifest.setdefault('powered_shafts', {})[frame] = {
            'floors': FLOORS, 'size_cm': [180, 180, 1120+EXTRA_HEIGHT*100],
            'landing_sill_bounds_cm': {'x': [-79, -67], 'y': [-77, 77], 'z_above_floor': [13, 24]},
            'rail_clearance_cm': 1, 'legacy_assets_changed': False}
        # A shaft-only run exports no carrier or gate file.
        if not previous:
            manifest['files'] = {frame: 'UnrealParts/'+frame+'.fbx'}
    else:
        manifest['files'].update({name: filename for name, filename in previous.get('files', {}).items()
                                  if name.startswith('SM_PalletLift_FramePowered')})
        if previous.get('powered_shafts'):
            manifest['powered_shafts'] = previous['powered_shafts']
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
# Preview assembly only. Exported moving meshes stay at their own origins in FBX.
exported[carrier].location.z=.40
exported[gate].hide_render=True
for floor in range(FLOORS):
    for side in [-1,1]:
        obj=bpy.data.objects.new('PREVIEW gate',exported[gate].data)
        studio.objects.link(obj);obj.location=(-.77,side*.3125,.40+floor*4+1.15)
wood=material('Preview_Wood',(.35,.22,.10),0,.7)
carton=material('Preview_Carton',(.50,.37,.22),0,.8)
for y in [-.45,0,.45]:box('Preview pallet stringer',(0,y,.475),(1.10,.10,.15),wood,'studio')
for x in [-.46,-.23,0,.23,.46]:box('Preview pallet deck',(x,0,.565),(.16,1.10,.03),wood,'studio')
for x in [-.25,.25]:
    for y in [-.25,.25]:
        for z in [.83,1.32]:box('Preview boxed goods',(x,y,z),(.48,.48,.48),carton,'studio',.004)
floor_mat=material('Preview_Floor',(.13,.15,.18),.05,.76)
box('Studio ground',(0,0,-.07),(200,200,.1),floor_mat,'studio')
scene.world=bpy.data.worlds.new('Pallet Lift Studio')
scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.21,.24,.29,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.5
for pos,energy,size in [((-4,-6,9),2600,6),((3,4,8),3300,5),((-3,5,3),1800,4)]:
    data=bpy.data.lights.new('Softbox','AREA');data.energy=energy;data.shape='DISK';data.size=size
    obj=bpy.data.objects.new('Softbox',data);studio.objects.link(obj);obj.location=pos
    obj.rotation_euler=(Vector((0,0,5))-obj.location).to_track_quat('-Z','Y').to_euler()
camera_data=bpy.data.cameras.new('Preview Camera');camera=bpy.data.objects.new('Preview Camera',camera_data)
studio.objects.link(camera);scene.camera=camera;camera_data.type='ORTHO'
scene.render.engine='CYCLES';scene.cycles.samples=32
scene.render.image_settings.file_format='PNG'
scene.render.resolution_percentage=100
scene.view_settings.view_transform='AgX'
for filename,eye,target,scale,res in [('PalletLift_Tower.png',(-12,-17,11),(0,0,5.5),13.4,(1000,1400)),('PalletLift_Loading.png',(-4,-5,2.9),(0,0,1.4),3.7,(1400,1100))]:
    camera.location=eye;camera.rotation_euler=(Vector(target)-camera.location).to_track_quat('-Z','Y').to_euler();camera_data.ortho_scale=scale
    scene.render.resolution_x,scene.render.resolution_y=res
    scene.render.filepath=str(OUT/filename)
    if '--skip-render' not in sys.argv:
        bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(f'pallet_lift_powered_frame{FLOORS}.blend' if POWERED_SHAFT else ('pallet_lift_extensions.blend' if EXTENSIONS else 'pallet_lift.blend'))))
print('PALLET_LIFT_MODEL_EXPORTED',str(OUT))
