"""Run through Blender MCP execute_blender_code; create a separate product scene."""
import bpy
import math
from mathutils import Vector

OUT = 'F:/Works/Unreal/Projects/msc_vr/Art/Accessories/CatCharm/'
scene = bpy.data.scenes.new('MIMI - Cat Charm')
bpy.context.window.scene = scene

def material(name, color, metallic=0, roughness=.32):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (*color, 1)
    bsdf.inputs['Metallic'].default_value = metallic
    bsdf.inputs['Roughness'].default_value = roughness
    return mat

cream = material('Warm ivory enamel', (.86, .67, .43), .12)
pink = material('Peach enamel', (.85, .26, .28), .05)
dark = material('Espresso details', (.055, .029, .026), .05)
gold = material('Champagne gold', (.83, .52, .20), .82, .21)
mint = material('Sage green enamel', (.26, .61, .47), .10)
white = material('Eye glints', (.98, .94, .85))
backdrop = material('Lavender studio', (.25, .21, .34), 0, .8)

def silhouette(name, points, bottom, top, mat, bevel):
    n = len(points)
    vertices = [(x,y,bottom) for x,y in points] + [(x,y,top) for x,y in points]
    faces = [tuple(range(n-1,-1,-1)), tuple(range(n,2*n))]
    faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    obj.data.materials.append(mat)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    obj.select_set(False)
    mod = obj.modifiers.new('Soft rounded rim', 'BEVEL')
    mod.width = bevel
    mod.segments = 5
    obj.modifiers.new('Weighted normals', 'WEIGHTED_NORMAL')
    return obj

def oval(name, pos, scale, mat):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=40, ring_count=24, location=pos)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    obj.data.materials.append(mat)
    for p in obj.data.polygons:
        p.use_smooth = True
    return obj

def line(name, points, thickness, mat):
    curve = bpy.data.curves.new(name, 'CURVE')
    curve.dimensions = '3D'
    curve.bevel_depth = thickness
    curve.bevel_resolution = 4
    spline = curve.splines.new('BEZIER')
    spline.bezier_points.add(len(points)-1)
    for p, co in zip(spline.bezier_points, points):
        p.co = co
        p.handle_left_type = p.handle_right_type = 'AUTO'
    obj = bpy.data.objects.new(name, curve)
    scene.collection.objects.link(obj)
    obj.data.materials.append(mat)
    return obj

outline = [(-.67,-.85),(0,-.96),(.67,-.85),(1.02,-.55),(1.15,-.10),
           (1.10,.45),(1.04,1.28),(.43,.85),(0,.94),(-.43,.85),
           (-1.04,1.28),(-1.10,.45),(-1.15,-.10),(-1.02,-.55)]
silhouette('Cat head - rounded ivory pendant', outline, -.14, .22, cream, .105)
for sign in [-1,1]:
    silhouette('Peach inner ear', [(sign*.60,.80),(sign*.94,1.08),(sign*.94,.55)], .22,.25,pink,.065)
    oval('Glossy espresso eye', (sign*.39,.18,.245), (.085,.115,.045), dark)
    oval('Eye highlight', (sign*.39-.023,.22,.285), (.020,.025,.012), white)
    oval('Peach cheek', (sign*.69,-.08,.239), (.17,.092,.025), pink)
    for j in range(2):
        line('Fine whisker', [(sign*.79,-.04-j*.15,.26),(sign*.98,.015-j*.20,.255)], .013,dark)
oval('Tiny pink nose', (0,-.075,.262), (.067,.047,.029), pink)
line('Smile left', [(0,-.11,.273),(-.01,-.22,.273),(-.13,-.27,.267),(-.21,-.20,.26)], .018,dark)
line('Smile right', [(0,-.11,.273),(.01,-.22,.273),(.13,-.27,.267),(.21,-.20,.26)], .018,dark)

for name, y, radius, tube in [('Pendant connector',1.06,.19,.043),('Gold key ring',1.72,.48,.060)]:
    bpy.ops.mesh.primitive_torus_add(major_segments=80, minor_segments=16, location=(0,y,.12), major_radius=radius,minor_radius=tube)
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(gold)
    for p in obj.data.polygons:
        p.use_smooth = True

star = []
for i in range(10):
    a=math.pi/2+i*math.pi/5
    radius=.24 if i%2==0 else .12
    star.append((.69+math.cos(a)*radius,1.30+math.sin(a)*radius))
silhouette('Little sage star charm',star,.025,.15,mint,.035)
line('Star gold connector',[(.35,1.41,.12),(.49,1.56,.12),(.63,1.51,.12)],.026,gold)

bpy.ops.mesh.primitive_plane_add(size=200, location=(0,0,-.20))
bpy.context.object.name='Studio backdrop'
bpy.context.object.data.materials.append(backdrop)

def aim(obj, target):
    obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()

bpy.ops.object.camera_add(location=(1.5,-3.0,8.7))
camera=bpy.context.object
camera.name='Product camera'
aim(camera,(0,.53,0))
camera.data.type='ORTHO'
camera.data.ortho_scale=4.9
camera.data.lens=52
scene.camera=camera
for name, pos, energy, size, color in [
    ('Key softbox',(-3,-4,7),650,4,(1,.86,.72)),
    ('Cool fill',(4,1,5),450,3,(.76,.83,1)),
    ('Top rim',(-1,4,5),750,3,(1,.91,.78)),
]:
    bpy.ops.object.light_add(type='AREA',location=pos)
    light=bpy.context.object
    light.name=name
    light.data.energy=energy
    light.data.shape='DISK'
    light.data.size=size
    light.data.color=color
    aim(light,(0,.5,0))
scene.world=bpy.data.worlds.new('Soft studio world')
scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.38,.32,.46,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.35
scene.render.engine='BLENDER_EEVEE'
scene.eevee.use_gtao=True
scene.eevee.gtao_distance=3
scene.eevee.gtao_factor=1.15
scene.eevee.use_soft_shadows=True
scene.eevee.taa_render_samples=128
scene.render.resolution_x=1200
scene.render.resolution_y=1200
scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
scene.render.filepath=OUT+'cat_charm.png'
scene.view_settings.view_transform='Filmic'
scene.view_settings.look='Medium High Contrast'
scene.view_settings.exposure=.25
bpy.ops.object.select_all(action='DESELECT')
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        area.spaces.active.region_3d.view_perspective='CAMERA'
        area.spaces.active.overlay.show_overlays=False
        area.spaces.active.shading.type='MATERIAL'
bpy.ops.wm.save_as_mainfile(filepath=OUT+'cat_charm.blend')
print('Created MIMI cat charm, saved blend:',OUT+'cat_charm.blend')
