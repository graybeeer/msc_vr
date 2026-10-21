"""Blender MCP: rubber column guards from user photo, model-only exports."""
import bpy,math,json,bmesh
from pathlib import Path
from mathutils import Matrix,Vector
OUT=Path('F:/Works/Unreal/Projects/msc_vr/Art/Environment/WarehouseServices/ColumnProtection')
OUT.mkdir(parents=True,exist_ok=True)
assert not bpy.data.scenes.get('Column protection - Library')
lib=bpy.data.scenes.new('Column protection - Library');bpy.context.window.scene=lib;lib.unit_settings.system='METRIC';lib.unit_settings.scale_length=1
def material(name,col,metal,rough):
    m=bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*col,1)
    n=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED');n.inputs['Base Color'].default_value=(*col,1);n.inputs['Metallic'].default_value=metal;n.inputs['Roughness'].default_value=rough
    return m
rubber=material('CP Matte molded rubber',(.016,.022,.025),0,.74)
yellow=material('CP Yellow warning strip',(.94,.56,.022),.03,.43)
fastener=material('CP Recessed screw steel',(.26,.30,.31),.82,.39)
concrete=material('CP Concrete',(.32,.34,.35),0,.87)
floor=material('CP Studio floor',(.16,.18,.19),0,.84)
pieces=[]
def box(loc,dim,mat,bevel=.003):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.dimensions=dim;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(mat)
    if bevel:
        m=o.modifiers.new('Rubber edge radius','BEVEL');m.width=bevel;m.segments=4;o.modifiers.new('Weighted normals','WEIGHTED_NORMAL')
    pieces.append(o);return o
def mesh_obj(name,vs,fs,mat):
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(vs,[],fs);mesh.update();o=bpy.data.objects.new(name,mesh);lib.collection.objects.link(o);mesh.materials.append(mat);pieces.append(o);return o
def screw(loc,axis):
    bpy.ops.mesh.primitive_cylinder_add(vertices=24,radius=.0045,depth=.002,location=loc);o=bpy.context.object
    if axis=='Y':o.rotation_euler.x=math.pi/2
    if axis=='X':o.rotation_euler.y=math.pi/2
    o.data.materials.append(fastener);pieces.append(o)
def clip(poly,func):
    out=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        fa,fb=func(a),func(b);ina,inb=fa<=0,fb<=0
        if ina:out.append(a)
        if ina!=inb:
            t=fa/(fa-fb);out.append((a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])))
    return out
def stripes(wing,width):
    # Clip diagonal yellow strips to the inset flat face, keeping rounded edges black.
    for k in range(-3,6):
        low=k*.235;high=low+.113;poly=[(.026,.082),(width-.027,.082),(width-.027,.858),(.026,.858)]
        poly=clip(poly,lambda p:low-(p[1]-1.9*p[0]));poly=clip(poly,lambda p:(p[1]-1.9*p[0])-high)
        if len(poly)<3:continue
        vs=[(u,-.0272,z) if wing=='front' else (-.0272,u,z) for u,z in poly]
        # Front-facing winding; printable film offset is 0.2mm, without raised text.
        mesh_obj('Diagonal warning band',vs,[tuple(range(len(vs))) if wing=='front' else tuple(reversed(range(len(vs))))],yellow)
def join(name):
    bpy.ops.object.select_all(action='DESELECT')
    for o in pieces:
        o.select_set(True);bpy.context.view_layer.objects.active=o;bpy.ops.object.convert(target='MESH');o.select_set(False)
    for o in pieces:o.select_set(True)
    bpy.context.view_layer.objects.active=pieces[0]
    if len(pieces)>1:bpy.ops.object.join()
    o=bpy.context.object;o.name=name;lib.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');o.data.transform(o.matrix_world.copy());o.matrix_world=Matrix.Identity(4)
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free();pieces.clear();return o
profile=[(-.027,-.027),(.22,-.027),(.22,-.004),(-.004,-.004),(-.004,.22),(-.027,.22)]
vs=[(x,y,z) for z in [.04,.90] for x,y in profile];n=len(profile)
fs=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
o=mesh_obj('Molded 90-degree rubber corner',vs,fs,rubber)
m=o.modifiers.new('Rounded protective edges','BEVEL');m.width=.006;m.segments=5;o.modifiers.new('Weighted normals','WEIGHTED_NORMAL')
for wing in ['front','side']:
    stripes('front' if wing=='front' else 'side',.22)
    for u in [.015,.205]:
        loc=(u,-.030,.47) if wing=='front' else (-.030,u,.47)
        box(loc,(.007,.010,.82) if wing=='front' else (.010,.007,.82),rubber,.002)
        for z in [.13,.43,.78]:
            screw((u,-.0355,z) if wing=='front' else (-.0355,u,z),'Y' if wing=='front' else 'X')
    for z in [.064,.878]:box((.11,-.029,z) if wing=='front' else (-.029,.11,z),(.219,.008,.028) if wing=='front' else (.008,.219,.028),rubber,.003)
corner=join('SM_WS_RubberCornerGuard_900')
# Flat face panel shares height and warning-band dimensions.
box((.125,-.0155,.47),(.25,.023,.86),rubber,.006);stripes('front',.25)
for x in [.014,.236]:
    box((x,-.029,.47),(.007,.010,.82),rubber,.002)
    for z in [.13,.43,.78]:screw((x,-.0355,z),'Y')
for z in [.064,.878]:box((.125,-.029,z),(.25,.008,.028),rubber,.003)
flat=join('SM_WS_RubberFlatGuard_900')
# 450mm square column wrapper, consisting only of four rubber corner guards.
for i,(x,y) in enumerate([(-.225,-.225),(.225,-.225),(.225,.225),(-.225,.225)]):
    o=corner.copy();o.data=corner.data.copy();lib.collection.objects.link(o);o.matrix_world=Matrix.Translation((x,y,0))@Matrix.Rotation(i*math.pi/2,4,'Z');pieces.append(o)
wrap=join('SM_WS_RubberColumnWrap_450')
modules=[corner,flat,wrap]
preview=bpy.data.scenes.new('Column protection - Preview');bpy.context.window.scene=preview;preview.unit_settings.system='METRIC';preview.unit_settings.scale_length=1
def copy(src,loc,rot=0):
    o=src.copy();o.data=src.data;preview.collection.objects.link(o);o.name='PREVIEW_'+src.name;o.location=loc;o.rotation_euler.z=rot;return o
# Display raw corner at left, four-corner installed form on a taller concrete post.
copy(corner,(-.65,-.12,0));copy(flat,(-1.12,-.10,0));copy(wrap,(.45,.08,0))
def scenery(loc,dim,mat):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.dimensions=dim;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(mat)
    m=o.modifiers.new('Scenery edge bevel','BEVEL');m.width=.008;m.segments=3;o.modifiers.new('Scenery normals','WEIGHTED_NORMAL');return o
scenery((.45,.08,1.0),(.45,.45,2.0),concrete)
scenery((0,0,-.03),(5,4,.06),floor)
scenery((0,.70,1.4),(5,.10,2.8),concrete)
for x in [-1.5,0,1.5]:scenery((x,-.3,.0008),(.002,2,.0016),rubber)
w=bpy.data.worlds.new('Column protection studio');preview.world=w;w.use_nodes=True
next(n for n in w.node_tree.nodes if n.type=='BACKGROUND').inputs[0].default_value=(.16,.19,.23,1)
next(n for n in w.node_tree.nodes if n.type=='BACKGROUND').inputs[1].default_value=.45
for loc,power,size in [((-2,-3,3),400,2),((2,-1,3),550,2),((0,1,3),300,2)]:
    bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=power;o.data.size=size;o.rotation_euler=(Vector((0,0,.6))-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(-2.7,-4.3,2.3));camera=bpy.context.object;camera.rotation_euler=(Vector((-.2,.05,.72))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=2.65;preview.camera=camera
preview.render.engine='BLENDER_EEVEE';preview.eevee.use_gtao=True;preview.eevee.gtao_distance=.12;preview.eevee.gtao_factor=1.4;preview.eevee.taa_render_samples=128
preview.view_settings.view_transform='Filmic';preview.render.resolution_x=1500;preview.render.resolution_y=1300;preview.render.resolution_percentage=100;preview.render.image_settings.file_format='PNG';preview.render.filepath=str(OUT/'column_guards.png')
bpy.ops.render.render(write_still=True)
# Individual FBX files contain protective rubber parts only, never the concrete post.
bpy.context.window.scene=lib;(OUT/'FBX').mkdir(exist_ok=True)
manifest={'date':'2026-10-14','blender_units':'meters','unreal_applied':False,'reference':'user supplied rubber corner protector photo','columns_excluded':True,'modules':[]}
for o in modules:
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(island_margin=.02);bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.export_scene.fbx(filepath=str(OUT/'FBX'/(o.name+'.fbx')),use_selection=True,object_types={'MESH'},global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',bake_anim=False)
    manifest['modules'].append({'name':o.name,'dimensions_cm':[v*100 for v in o.dimensions],'origin':'floor at inner corner; flat panel at left edge; wrap at column center','fbx':'FBX/'+o.name+'.fbx'})
(OUT/'modules.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.context.window.scene=preview
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.overlay.show_overlays=False
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'column_protectors.blend'))
print('Three column guard variants modeled and exported; Unreal not modified')
