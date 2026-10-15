"""Blender MCP: export charger asset only; studio and bollards excluded."""
import bpy,json
from pathlib import Path
from mathutils import Matrix
OUT=Path('F:/Works/Unreal/Projects/msc_vr/Art/Equipment/AGVCharger')
(OUT/'UnrealParts').mkdir(exist_ok=True)
s=bpy.context.scene;assert s.name=='AGV Charger - concept'
asset=bpy.data.collections['CHARGER_ASSET'];parts=[o for o in asset.objects if o.type=='MESH'];assert len(parts)==8
button=bpy.data.objects['SM_Charger_EStop']
for o in bpy.context.view_layer.objects:o.select_set(False)
button.select_set(True);bpy.context.view_layer.objects.active=button;s.cursor.location=(-.086,.25,.90)
bpy.ops.object.origin_set(type='ORIGIN_CURSOR');s.cursor.location=(0,0,0)
rear_extension=128*(215/282.55)-51
manifest={'date':'2026-10-14','axes':'X front / Y right / Z up','units':'centimeters','root_definition':'ground beneath contact pad surface X=0','suggested_location_relative_to_existing_DockPose_cm':[-52-rear_extension,0,0],'contact_row_heights_cm':[20,28,36,44,52],'parts':[],'materials':{},'unreal_import_tested':False}
for o in parts:
    o.data.calc_loop_triangles()
    manifest['parts'].append({'name':o.name,'location_cm':[v*100 for v in o.location],'triangles':len(o.data.loop_triangles),'fbx':'UnrealParts/'+o.name+'.fbx'})
    for mat in o.data.materials:
        if mat and mat.use_nodes:
            n=next((n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
            if n:manifest['materials'][mat.name]={k:list(n.inputs[k].default_value) if k in ['Base Color','Emission'] else n.inputs[k].default_value for k in ['Base Color','Metallic','Roughness','Emission','Emission Strength']}
def deselect():
    for o in bpy.context.view_layer.objects:o.select_set(False)
def export(path):
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH'},global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',use_mesh_modifiers=True,bake_anim=False)
for o in parts:
    t=o.copy();t.data=o.data.copy();t.parent=None;s.collection.objects.link(t);t.matrix_world=Matrix.Identity(4)
    deselect();t.select_set(True);bpy.context.view_layer.objects.active=t;export(OUT/'UnrealParts'/(o.name+'.fbx'))
    mesh=t.data;bpy.data.objects.remove(t,do_unlink=True);bpy.data.meshes.remove(mesh)
deselect()
for o in parts:o.select_set(True)
bpy.context.view_layer.objects.active=parts[0];export(OUT/'agv_charger.fbx')
# Independently reimport assembled FBX to a temporary scene and compare dimensions.
expected=sorted([tuple(round(v,5) for v in o.dimensions) for o in parts])
temp=bpy.data.scenes.new('Charger FBX check');bpy.context.window.scene=temp
bpy.ops.import_scene.fbx(filepath=str(OUT/'agv_charger.fbx'))
imported=[o for o in temp.objects if o.type=='MESH'];assert len(imported)==8
actual=sorted([tuple(round(v,5) for v in o.dimensions) for o in imported]);assert expected==actual,(expected,actual)
for o in list(temp.objects):bpy.data.objects.remove(o,do_unlink=True)
bpy.context.window.scene=s;bpy.data.scenes.remove(temp)
manifest['validation']={'date':'2026-10-14','scope':'Blender renders and FBX reimport: eight meshes and matching dimensions; Unreal source contact height comparison only','fbx_roundtrip_mesh_count':8,'passed':True}
(OUT/'assembly_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
s.render.filepath=str(OUT/'charger_hero.png')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'agv_charger.blend'))
print(json.dumps(manifest['validation']))
