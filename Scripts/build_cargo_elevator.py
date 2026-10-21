"""Build the reference-inspired cargo lift in a new Blender scene via Blender MCP.

Keeps other scenes. Geometry is in metres; export manifests use centimetres.
This creates an art asset and a mechanical preview, not a replacement runtime PLC.
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

OUT = Path(__file__).resolve().parents[1] / 'Art/Equipment/CargoElevator'
OUT.mkdir(parents=True, exist_ok=True)
assert not bpy.data.scenes.get('Cargo Elevator - model'), 'Use the saved model for revisions.'
scene = bpy.data.scenes.new('Cargo Elevator - model')
bpy.context.window.scene = scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1
asset = bpy.data.collections.new('ELEVATOR_ASSET')
studio = bpy.data.collections.new('ELEVATOR_STUDIO')
scene.collection.children.link(asset)
scene.collection.children.link(studio)
groups, roots, materials = {}, {}, {}


def empty(name, point=(0, 0, 0)):
    obj = bpy.data.objects.new(name, None)
    asset.objects.link(obj)
    obj.empty_display_type = 'PLAIN_AXES'
    obj.empty_display_size = .18
    obj.matrix_world = Matrix.Translation(Vector(point))
    roots[name] = obj
    return obj


car = empty('ELV_CarRoot')
doors = {}
for floor in range(3):
    for side in (-1, 1):
        doors[floor, side] = empty(f'ELV_Gate_{floor}_{side}', (-2.05, side * .6, floor * 4 + 1.35))


def material(name, rgb, metal=0, rough=.4, glow=0, brushed=False):
    m = bpy.data.materials.new('ELV ' + name)
    m.use_nodes = True
    m.diffuse_color = (*rgb, 1)
    node = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    for key, value in [('Base Color', (*rgb, 1)), ('Metallic', metal), ('Roughness', rough)]:
        node.inputs[key].default_value = value
    if glow:
        node.inputs['Emission'].default_value = (*rgb, 1)
        node.inputs['Emission Strength'].default_value = glow
    if brushed:
        node.inputs['Anisotropic'].default_value = .45
        tex = m.node_tree.nodes.new('ShaderNodeTexNoise')
        mapping = m.node_tree.nodes.new('ShaderNodeVectorMath')
        mapping.operation = 'MULTIPLY'
        mapping.inputs[1].default_value = (140, 140, 1.5)
        coords = m.node_tree.nodes.new('ShaderNodeTexCoord')
        m.node_tree.links.new(coords.outputs['Generated'], mapping.inputs[0])
        m.node_tree.links.new(mapping.outputs['Vector'], tex.inputs['Vector'])
        tex.inputs['Scale'].default_value = 4
        bump = m.node_tree.nodes.new('ShaderNodeBump')
        bump.inputs['Strength'].default_value = .13
        bump.inputs['Distance'].default_value = .00012
        m.node_tree.links.new(tex.outputs['Fac'], bump.inputs['Height'])
        m.node_tree.links.new(bump.outputs['Normal'], node.inputs['Normal'])
    materials[name] = m
    return m


steel = material('Brushed stainless', (.46, .51, .54), .88, .30, brushed=True)
steel2 = material('Satin door steel', (.36, .40, .42), .84, .35, brushed=True)
dark = material('Graphite frame', (.038, .05, .061), .65, .36)
blue = material('Warehouse blue cladding', (.018, .085, .16), .42, .36)
rubber = material('Black rubber', (.012, .016, .019), .05, .64)
yellow = material('Safety yellow', (.96, .54, .025), .18, .36)
white = material('White lettering', (.79, .83, .83), .05, .43)
black = material('Display glass', (.008, .019, .023), .25, .16)
green = material('Indicator mint', (.035, .65, .33), .15, .26, 2)
red = material('Emergency red', (.65, .018, .012), .15, .33)
led = material('Warm cabin LED', (1, .87, .66), .05, .3, 3)
floor_mat = material('Loading deck steel', (.18, .21, .23), .68, .48)
concrete = material('Preview concrete', (.20, .23, .25), 0, .85)


def finish(obj, name, mat, group, parent=None, bevel=0):
    obj.name = name
    for collection in list(obj.users_collection):
        collection.objects.unlink(obj)
    (studio if group == 'studio' else asset).objects.link(obj)
    if mat:
        obj.data.materials.append(mat)
    if parent:
        obj.parent = parent
        obj.matrix_parent_inverse = parent.matrix_world.inverted()
    if bevel:
        mod = obj.modifiers.new('Manufactured edge', 'BEVEL')
        mod.width = bevel
        mod.segments = 3
        mod.limit_method = 'ANGLE'
        obj.modifiers.new('Face normals', 'WEIGHTED_NORMAL')
    if group != 'studio':
        groups.setdefault(group, []).append(obj)
        obj['elevator_export_group'] = group
    return obj


def box(name, point, size, mat, group='ShaftFrame', parent=None, bevel=.003):
    bpy.ops.mesh.primitive_cube_add(size=1, location=point)
    obj = bpy.context.object
    obj.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return finish(obj, name, mat, group, parent, min(bevel, min(size) * .18))


def cylinder(name, point, radius, depth, mat, group, parent=None, axis='Z'):
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=radius, depth=depth, location=point)
    obj = bpy.context.object
    if axis == 'X':
        obj.rotation_euler.y = math.pi / 2
    for polygon in obj.data.polygons:
        polygon.use_smooth = len(polygon.vertices) == 4
    return finish(obj, name, mat, group, parent, .002)


font = bpy.data.fonts.load('C:/Windows/Fonts/malgunbd.ttf')


def text(name, value, point, size, mat, group, parent=None):
    curve = bpy.data.curves.new(name, 'FONT')
    curve.body = value
    curve.font = font
    curve.size = size
    curve.align_x = 'CENTER'
    curve.extrude = .0002
    obj = bpy.data.objects.new(name, curve)
    scene.collection.objects.link(obj)
    obj.location = point
    obj.rotation_euler = (math.pi / 2, 0, -math.pi / 2)
    return finish(obj, name, mat, group, parent)


# Native lift: longitudinal X, 240cm opening across Y, entrance at X=-205cm.
for x in (-1.87, 1.92):
    for y in (-1.32, 1.32):
        box(f'Shaft post {x} {y}', (x, y, 5.8), (.16, .16, 11.6), dark)
        # Keep foundation plates outside the moving deck's four corners.
        box(f'Base foot {x} {y}', (x, y, -.05), (.24, .22, .10), steel)
for z in (0, 4, 8, 11.55):
    for y in (-1.37, 1.37):
        box(f'Side cross member {y} {z}', (0, y, z), (3.8, .12, .20), dark)
    box(f'Rear cross member {z}', (1.92, 0, z), (.16, 2.80, .20), dark)
for y in (-1.08, 1.08):
    box('Cabin guide rail', (1.885, y, 5.6), (.07, .08, 11.3), steel, 'GuideRails')
    for z in (.8, 3.8, 6.8, 9.8):
        box('Guide bracket', (1.935, y, z), (.10, .19, .065), dark, 'GuideRails')
for floor in range(3):
    low = floor * 4
    height = 3.95 if floor < 2 else 3.6
    for side in (-1, 1):
        box(f'Shaft side liner L{floor+1}', (0, side*1.36, low+height/2), (3.72, .035, height), dark, 'ShaftEnclosure')
    box(f'Shaft rear L{floor+1}', (1.96, 0, low+height/2), (.035, 2.8, height), dark, 'ShaftEnclosure')

# Traveling cabin. The floor and cabin shell remain independent export meshes.
box('Lift platform 350 x 240 cm', (0, 0, -.05), (3.5, 2.4, .10), floor_mat, 'LiftPlatform', car)
for x in (-1.1, 0, 1.1):
    box('Platform underfloor stiffener', (x, 0, -.12), (.13, 2.34, .06), dark, 'LiftPlatform', car)
for side in (-1, 1):
    y = side*1.225
    box('Cabin stainless side', (.05, y, 1.42), (3.4, .045, 2.84), steel2, 'CabinShell', car)
    for x in (-1.1, -.2, .7, 1.55):
        box('Interior panel seam', (x, side*1.199, 1.42), (.008, .004, 2.7), dark, 'CabinShell', car, .0005)
    for z in (.48, 1.03):
        box('Load protection rubber rail', (.05, side*1.176, z), (3.28, .065, .10), rubber, 'CabinShell', car, .01)
    box('Interior LED channel', (0, side*1.12, 2.79), (3.15, .10, .045), dark, 'CabinShell', car)
    box('Interior LED diffuser', (0, side*1.12, 2.762), (3.03, .055, .014), led, 'CabinShell', car, .001)
box('Cabin rear panel', (1.765, 0, 1.42), (.04, 2.49, 2.84), steel2, 'CabinShell', car)
for z in (.48, 1.03):
    box('Rear rubber rail', (1.72, 0, z), (.07, 2.32, .10), rubber, 'CabinShell', car, .01)
box('Cabin ceiling', (.05, 0, 2.865), (3.4, 2.49, .05), steel, 'CabinShell', car)
for x in (-1.3, 1.3):
    for y in (-.75, .75):
        cylinder('Flush platform fastener', (x, y, .001), .012, .003, steel, 'LiftPlatform', car)
# One mesh for the raised tread, so the deck is patterned without hundreds of actors.
verts, faces = [], []
for i in range(27):
    for j in range(16):
        x, y = -1.66+i*.125, -1.10+j*.145
        for direction in (-1, 1):
            theta = direction*math.pi/4
            dx, dy = math.cos(theta)*.022, math.sin(theta)*.022
            nx, ny = -math.sin(theta)*.003, math.cos(theta)*.003
            n = len(verts)
            verts.extend([(x-dx-nx,y-dy-ny,.002),(x+dx-nx,y+dy-ny,.002),(x+dx+nx,y+dy+ny,.002),(x-dx+nx,y-dy+ny,.002)])
            faces.append((n,n+1,n+2,n+3))
mesh = bpy.data.meshes.new('Raised diamond tread')
mesh.from_pydata(verts, [], faces)
mesh.update()
obj = bpy.data.objects.new('Diamond tread plate', mesh)
scene.collection.objects.link(obj)
finish(obj, obj.name, steel, 'LiftPlatform', car)

def build_call_stand(floor):
    """An independent, floor-anchored pedestal outside the vehicle doorway."""
    z, x, y = floor*4, -2.85, -2.05
    group = f'CallStand_L{floor+1}'
    box('Call stand anchor base', (x,y,z+.018), (.30,.28,.036), dark, group, bevel=.009)
    box('Call stand stainless plinth', (x,y,z+.05), (.13,.15,.06), steel2, group)
    box('Call stand slim post', (x+.012,y,z+.49), (.075,.085,.84), dark, group, bevel=.008)
    box('Call stand mounting neck', (x,y,z+.925), (.08,.10,.06), steel2, group)
    box('Call stand control head', (x,y,z+1.19), (.14,.23,.52), steel2, group, bevel=.016)
    box('Call stand face inset', (x-.073,y,z+1.19), (.008,.19,.47), dark, group)
    box('Call stand display', (x-.079,y,z+1.32), (.006,.15,.105), black, group)
    text('Call stand floor number', str(floor+1), (x-.084,y,z+1.285), .075, green, group)
    for zz, symbol in ((1.20,'▲'),(1.09,'▼')):
        cylinder('Call stand button bezel', (x-.084,y,z+zz), .029,.015, steel,group,axis='X')
        cylinder('Call stand button cap', (x-.094,y,z+zz), .022,.009,green,group,axis='X')
        text('Call stand button legend',symbol,(x-.101,y,z+zz-.013),.031,white,group)
    box('Call stand emergency surround',(x-.08,y,z+.985),(.012,.09,.07),yellow,group)
    cylinder('Call stand emergency mushroom',(x-.103,y,z+.985),.023,.03,red,group,axis='X')
    for xx in (-.105,.105):
        for yy in (-.095,.095):
            cylinder('Call stand anchor bolt',(x+xx,y+yy,z+.044),.011,.014,steel,group)


# Three landing modules with exposed horizontal door travel, no broad wings.
for floor in range(3):
    z = floor*4
    group = f'Landing_L{floor+1}'
    for side in (-1, 1):
        for offset in (-.083, .083):
            box('Stainless jamb trim', (-2.19, side*1.27+offset*.25, z+1.35), (.17, .045, 2.7), steel, group)
        box('Jamb recess', (-1.94, side*1.265, z+1.35), (.06, .13, 2.7), rubber, group)
        box('Jamb face', (-2.16, side*1.27, z+1.35), (.04, .14, 2.7), steel, group)
        # Door panels use exactly the existing Gate component centres.
        root = doors[floor, side]
        door_group = f'Gate_{floor}_{side}'
        box('Door steel leaf', (-2.05, side*.602, z+1.35), (.06, 1.196, 2.68), steel2, door_group, root, .004)
        for segment in range(3):
            box('Door vertical skin', (-2.083, side*.602+(.4*(segment-1)), z+1.35), (.003, .391, 2.668), steel, door_group, root, .001)
        box('Door horizontal reinforced joint', (-2.087, side*.602, z+1.20), (.006, 1.184, .012), dark, door_group, root, .001)
        box('Door bottom shoe', (-2.055, side*.602, z+.04), (.075, 1.18, .055), dark, door_group, root)
        box('Closing edge gasket', (-2.052, side*.004, z+1.35), (.055, .009, 2.68), rubber, door_group, root, .001)
        for yy in (-.48, .48):
            for zz in (.12, 2.58):
                cylinder('Door recessed fastener', (-2.088, side*.602+yy, z+zz), .007, .002, steel2, door_group, root, 'X')
        box('Door caution patch', (-2.089, side*.14, z+1.42), (.001, .07, .10), white, door_group, root, .0001)
        text('Door caution symbol', '!', (-2.091, side*.14, z+1.425), .065, red, door_group, root)
        # Bollards are separate static parts, outside the clear opening.
        guard_group = f'Bollards_L{floor+1}'
        cylinder('Safety bollard', (-2.49, side*1.52, z+.48), .09, .96, yellow, guard_group)
        for zz in (.38, .70):
            cylinder('Black reflective bollard band', (-2.49, side*1.52, z+zz), .092, .12, rubber, guard_group)
        cylinder('Bollard top cap', (-2.49, side*1.52, z+.965), .092, .035, rubber, guard_group)
        box('Bollard floor plate', (-2.49, side*1.52, z+.015), (.25, .25, .03), dark, guard_group)
        for xx in (-.08, .08):
            for yy in (-.08, .08):
                cylinder('Bollard anchor bolt', (-2.49+xx, side*1.52+yy, z+.033), .012, .014, steel, guard_group)
    box('Door header face', (-2.13, 0, z+2.86), (.18, 2.76, .32), steel, group)
    box('Header top folded lip', (-2.23, 0, z+3.025), (.05, 2.78, .018), steel2, group)
    box('Exposed door travel rail',(-2.04,0,z+2.82),(.10,5.12,.085),dark,group)
    # Ends on the fixed floor, clear of the moving deck. The native slab
    # supplies the landing approach; the applied map ends it at X=-181 cm
    # to retain 6 cm of physical clearance from the moving deck.
    box('Threshold plate', (-2.03, 0, z-.025), (.33, 2.48, .05), steel, group)
    for k in range(6):
        box('Threshold guide groove', (-2.15+k*.025, 0, z-.001), (.007, 2.39, .002), rubber, group, bevel=.0002)
    box('Header indicator glass', (-2.232, -.79, z+2.88), (.008, .34, .13), black, group)
    text('Floor header number', f'{floor+1}', (-2.239, -.79, z+2.846), .10, green, group)
    text('Cargo lift designation', '화물용 승강기', (-2.232, .23, z+2.89), .075, dark, group)
    text('Load rating plate', '2000 kg', (-2.234, .23, z+2.765), .052, dark, group)
    build_call_stand(floor)

# Demonstration keyframes: open L1, close, lift to L3, open L3, then return.
scene.frame_start, scene.frame_end = 1, 300
scene.render.fps = 24
for floor in range(3):
    timeline = [(1,0),(24,0),(56,1),(85,1),(112,0),(150,0),(184,0),(216,0),(246,0),(300,0)] if floor == 0 else [(1,0),(184,0),(216,1),(246,1),(274,0),(300,0)] if floor == 2 else [(1,0),(300,0)]
    for side in (-1, 1):
        root = doors[floor, side]
        for frame, opening in timeline:
            root.location.y = side*(.6+opening*1.25)
            root.keyframe_insert(data_path='location', frame=frame)
for frame, height in [(1,0),(112,0),(184,8),(274,8),(300,0)]:
    car.location.z = height
    car.keyframe_insert(data_path='location', frame=frame)
for root in roots.values():
    if root.animation_data and root.animation_data.action:
        for curve in root.animation_data.action.fcurves:
            for key in curve.keyframe_points:
                key.interpolation = 'LINEAR'
scene.frame_set(1)

# Presentation lighting and camera are excluded from every export.
box('Studio ground', (-.7, 0, -.20), (16, 16, .25), concrete, 'studio', bevel=.01)


def aim(obj, target):
    obj.rotation_euler = (Vector(target)-obj.location).to_track_quat('-Z', 'Y').to_euler()


def light(name, location, power, size, target):
    data = bpy.data.lights.new(name, 'AREA')
    data.energy = power
    data.size = size
    obj = bpy.data.objects.new(name, data)
    studio.objects.link(obj)
    obj.location = location
    aim(obj, target)
    return obj


light('Large soft key', (-5, -4, 6), 1800, 5, (-1, 0, 1.6))
light('Silver reflection strip', (-3, 4, 4), 1400, 4, (-1, 0, 1.5))
light('Cool overhead rim', (2, -3, 5), 1700, 4, (-1, 0, 1.6))
for z in (2.55, 6.55, 10.55):
    light('Interior softbox', (.5, 0, z), 75, 1.3, (.2, 0, z-1.8))
data = bpy.data.cameras.new('Elevator hero lens')
camera = bpy.data.objects.new('Elevator hero camera', data)
studio.objects.link(camera)
camera.location = (-8.4, -6.8, 3.9)
aim(camera, (-1.10, 0, 1.60))
data.type = 'ORTHO'
data.ortho_scale = 7.1
scene.camera = camera
scene.world = bpy.data.worlds.new('Elevator studio world')
scene.world.use_nodes = True
background = next(n for n in scene.world.node_tree.nodes if n.type == 'BACKGROUND')
background.inputs['Color'].default_value = (.16, .19, .23, 1)
background.inputs['Strength'].default_value = .55
scene.render.engine = 'BLENDER_EEVEE'
scene.eevee.use_gtao = True
scene.eevee.gtao_distance = .20
scene.eevee.gtao_factor = 1.20
scene.eevee.use_bloom = True
scene.eevee.bloom_intensity = .025
scene.eevee.taa_render_samples = 96
scene.view_settings.exposure = -.25
scene.render.resolution_x = 1500
scene.render.resolution_y = 1250
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.image_settings.color_mode = 'RGB'
for area in bpy.context.screen.areas:
    if area.type == 'VIEW_3D':
        area.spaces.active.region_3d.view_perspective = 'CAMERA'
        area.spaces.active.overlay.show_overlays = False
scene['asset_scope'] = 'Art model and visual preview only; existing Unreal map and controls unchanged.'
scene['floor_heights_cm'] = '[0, 400, 800]'
scene['platform_dimensions_cm'] = '[350, 240, 10]'
scene['door_clear_opening_cm'] = '[240, 270]'
scene['render_views'] = 'Use render_cargo_elevator.py for closed/open/tower views.'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'cargo_elevator.blend'))
print('CARGO_ELEVATOR_BUILT', len(asset.objects), 'objects', len(groups), 'export groups')
