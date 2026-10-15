"""Blender MCP: module-only FBX export and independent geometry roundtrip verification."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector,Matrix
OUT=Path('F:/Works/Unreal/Projects/msc_vr/Art/Environment/WarehouseServices')
(OUT/'FBX').mkdir(exist_ok=True)
original=bpy.context.scene;lib=bpy.data.scenes['Warehouse Services - Library'];bpy.context.window.scene=lib
parts=[o for o in lib.objects if o.type=='MESH'];assert len(parts)==26
manifest={'date':'2026-10-14','axes':'X along straight routing / Z up; cabinet front -Y; conduit saddle axial Y','blender_units':'meters','unreal_units':'centimeters','unreal_applied':False,'preview_walls_floors_excluded':True,'modules':[],'materials':{}}
def bounds(mesh):
    return [min(v.co[k] for v in mesh.vertices) for k in range(3)]+[max(v.co[k] for v in mesh.vertices) for k in range(3)]
for o in parts:
    assert o.matrix_world==Matrix.Identity(4),(o.name,o.matrix_world)
    for a in lib.objects:a.select_set(False)
    o.select_set(True);bpy.context.view_layer.objects.active=o
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.02);bpy.ops.object.mode_set(mode='OBJECT')
    o.data.calc_loop_triangles()
    row={'name':o.name,'label':o['module_label'],'dimensions_cm':[v*100 for v in o.dimensions],'snap_ports_cm':[[v*100 for v in p] for p in json.loads(o['snap_ports_m'])],'pivot':'local origin at routing inlet / connection; cabinet door at left hinge','triangles':len(o.data.loop_triangles),'fbx':'FBX/'+o.name+'.fbx'}
    manifest['modules'].append(row)
    bpy.ops.export_scene.fbx(filepath=str(OUT/'FBX'/(o.name+'.fbx')),use_selection=True,object_types={'MESH'},global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',bake_anim=False)
    for m in o.data.materials:
        if m and m.use_nodes:
            n=next((n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
            if n:manifest['materials'][m.name]={k:list(n.inputs[k].default_value) if k in ['Base Color','Emission'] else n.inputs[k].default_value for k in ['Base Color','Metallic','Roughness','Emission','Emission Strength']}
# Validation imports every exported file, compares bounding extents and routing pivot.
temp=bpy.data.scenes.new('Warehouse service FBX check');bpy.context.window.scene=temp
maximum_error=0
for source in parts:
    bpy.ops.import_scene.fbx(filepath=str(OUT/'FBX'/(source.name+'.fbx')))
    imported=[o for o in temp.objects if o.type=='MESH'];assert len(imported)==1
    o=imported[0];assert o.matrix_world.translation.length<1e-6
    expected=bounds(source.data);actual=bounds(o.data)
    error=max(abs(a-b) for a,b in zip(expected,actual));assert error<1e-4,(source.name,error)
    maximum_error=max(maximum_error,error)
    for obj in list(temp.objects):bpy.data.objects.remove(obj,do_unlink=True)
bpy.context.window.scene=original;bpy.data.scenes.remove(temp)
manifest['validation']={'date':'2026-10-14','fbx_roundtrip_files':26,'maximum_bounds_error_m':maximum_error,'origin_and_unit_scale_checked':True,'uv_created':True,'unreal_import_tested':False,'passed':True}
(OUT/'modules.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'warehouse_services.blend'))
print(json.dumps(manifest['validation']))
