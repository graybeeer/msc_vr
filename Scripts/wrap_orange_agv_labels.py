"""Run through Blender MCP in the modular scene; wrap rear prints to the shell."""
import bpy, bmesh, math, json
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree
from pathlib import Path

OUT=Path('F:/Works/Unreal/Projects/msc_vr/Art/Vehicles/OrangeAGV')
scene=bpy.context.scene
assert scene.name=='AGV - Unreal modular'
chassis=bpy.data.objects['SM_AGV_Chassis']
assert not chassis.get('rear_print_wrapped'), 'Rear prints already wrapped'
mesh=chassis.data
orange=[i for i,m in enumerate(mesh.materials) if m.name=='Safety orange - painted body']
white=[i for i,m in enumerate(mesh.materials) if m.name=='White print']
shell=BVHTree.FromPolygons([v.co.copy() for v in mesh.vertices],[list(p.vertices) for p in mesh.polygons if p.material_index in orange])

# The only white geometry on the rear negative-X face is the two rear labels.
bm=bmesh.new();bm.from_mesh(mesh)
old=[f for f in bm.faces if f.material_index in white and all(v.co.x<-.84 and abs(v.co.y)<.30 and .70<v.co.z<1.20 for v in f.verts)]
assert len(old)>50, len(old)
bmesh.ops.delete(bm,geom=list({v for f in old for v in f.verts}),context='VERTS')
bm.to_mesh(mesh);bm.free();mesh.update()
created=[];distances=[]
for name in ['Reference rear branding','Fleet number rear']:
    source=bpy.data.objects[name]
    curve=source.data.copy();curve.extrude=0;curve.bevel_depth=0
    temp=bpy.data.objects.new('Wrapped '+name,curve);scene.collection.objects.link(temp)
    temp.matrix_world=Matrix.Rotation(math.pi/2,4,'Z')@source.matrix_world
    for o in bpy.context.view_layer.objects:o.select_set(False)
    temp.select_set(True);bpy.context.view_layer.objects.active=temp
    bpy.ops.object.convert(target='MESH')
    temp=bpy.context.object
    temp.data.transform(temp.matrix_world);temp.matrix_world=Matrix.Identity(4)
    bm=bmesh.new();bm.from_mesh(temp.data)
    bmesh.ops.triangulate(bm,faces=list(bm.faces))
    for iteration in range(7):
        edges=[e for e in bm.edges if e.calc_length()>.006]
        if not edges:break
        bmesh.ops.subdivide_edges(bm,edges=edges,cuts=1,use_grid_fill=True)
        bmesh.ops.triangulate(bm,faces=[f for f in bm.faces if len(f.verts)>3])
    for v in bm.verts:
        hit,normal,index,distance=shell.ray_cast(Vector((-2,v.co.y,v.co.z)),Vector((1,0,0)),3)
        assert hit is not None, tuple(v.co)
        if normal.x>0:normal=-normal
        v.co=hit+normal*.00015
        distances.append(shell.find_nearest(v.co)[3])
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(temp.data);bm.free();temp.data.update()
    for p in temp.data.polygons:
        if p.normal.x>0:p.flip()
        p.use_smooth=True
    created.append(temp)
for o in bpy.context.view_layer.objects:o.select_set(False)
chassis.select_set(True)
for o in created:o.select_set(True)
bpy.context.view_layer.objects.active=chassis
bpy.ops.object.join()
chassis['rear_print_wrapped']=True
chassis['rear_print_surface_offset_m']=.00015
assert max(distances)<.000151
print(json.dumps({'wrapped_vertices':len(distances),'max_surface_gap_m':max(distances),'parts':len([o for o in scene.objects if o.name.startswith('SM_AGV_')])}))
