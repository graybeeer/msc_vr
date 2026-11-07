import bpy
from mathutils import Vector
from pathlib import Path

def repair_cut_uv(mesh):
    """Planar wood UVs on new pocket faces; preserve the worn original surfaces."""
    uv=mesh.data.uv_layers.active
    if uv is None:uv=mesh.data.uv_layers.new(name='UVMap')
    mesh.data.update()
    count=0
    for face in mesh.data.polygons:
        normal=face.normal
        axis=max(range(3),key=lambda i:abs(normal[i]))
        if abs(normal[axis])<.99999:continue
        coordinate=mesh.data.vertices[face.vertices[0]].co[axis]
        planes=(.0325,.1025) if axis==2 else (-.4275,-.0625,.0625,.4275)
        if not any(abs(coordinate-p)<.00001 for p in planes):continue
        for index in face.loop_indices:
            p=mesh.data.vertices[mesh.data.loops[index].vertex_index].co
            uv.data[index].uv=(p.y/1.1+.5,p.z/.15) if axis==0 else (p.x/1.1+.5,p.z/.15) if axis==1 else (p.x/1.1+.5,p.y/1.1+.5)
        count+=1
    print('PALLET_CUT_UV_REPAIRED',count)
folder=Path(__file__).resolve().parents[1]/'Saved/Testing/PalletFourWay_20261104'
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(folder/'Original.fbx'))
meshes=[o for o in bpy.context.scene.objects if o.type=='MESH' and not o.name.startswith(('UCX_','UBX_','USP_','UCP_'))]
for obj in list(bpy.context.scene.objects):
    if obj not in meshes:bpy.data.objects.remove(obj,do_unlink=True)
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:o.select_set(True)
bpy.context.view_layer.objects.active=meshes[0];bpy.ops.object.join()
mesh=bpy.context.object
bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
points=[v.co.copy() for v in mesh.data.vertices]
low=Vector(tuple(min(v[i] for v in points) for i in range(3)))
high=Vector(tuple(max(v[i] for v in points) for i in range(3)))
for v in mesh.data.vertices:
    v.co=Vector(((v.co.x-(low.x+high.x)*.5)*1.1/(high.x-low.x),
                 (v.co.y-(low.y+high.y)*.5)*1.1/(high.y-low.y),
                 (v.co.z-low.z)*.15/(high.z-low.z)))
for axis in (0,1):
    for side in (-1,1):
        point=[0,0,.0675];point[axis]=side*.245
        bpy.ops.mesh.primitive_cube_add(size=1,location=point)
        cutter=bpy.context.object
        cutter.dimensions=Vector((.365,1.2,.07) if axis==0 else (1.2,.365,.07))
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        bpy.context.view_layer.objects.active=mesh
        modifier=mesh.modifiers.new('ForkPocket','BOOLEAN');modifier.operation='DIFFERENCE';modifier.solver='EXACT';modifier.object=cutter
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        bpy.data.objects.remove(cutter,do_unlink=True)
repair_cut_uv(mesh)
for vertex in mesh.data.vertices:vertex.co*=100
bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=.01
bpy.ops.object.select_all(action='DESELECT');mesh.select_set(True);bpy.context.view_layer.objects.active=mesh
mesh.name='SM_Pallet_110'
bpy.ops.export_scene.fbx(filepath=str(folder/'FourWay.fbx'),use_selection=True,object_types={'MESH'},
    global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',
    mesh_smooth_type='FACE',use_mesh_modifiers=True,bake_anim=False)
print('FOUR_WAY_PALLET_BUILT',len(mesh.data.vertices),len(mesh.data.polygons),tuple(mesh.dimensions))
