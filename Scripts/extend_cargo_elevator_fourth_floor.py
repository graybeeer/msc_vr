"""Blender: derive fourth-floor modules from the teammate's editable third floor.

Run in a background Blender loaded with Art/Equipment/CargoElevator/cargo_elevator.blend.
The original blend and first three floors are never overwritten. Only five new FBX
parts and their manifest entries are written; the runtime stretches the fixed shaft.
"""
import json
from pathlib import Path
import bpy
from mathutils import Matrix, Vector

out = Path(__file__).resolve().parents[1] / 'Art/Equipment/CargoElevator'
manifest_path = out / 'export_manifest.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
source = bpy.data.scenes['Cargo Elevator - model']
bpy.context.window.scene = source
source.frame_set(1)
source_groups = {'Landing_L3':'Landing_L4', 'Bollards_L3':'Bollards_L4',
                 'CallStand_L3':'CallStand_L4', 'Gate_2_-1':'Gate_3_-1', 'Gate_2_1':'Gate_3_1'}
temporary = bpy.data.scenes.new('Fourth floor export temporary')
temporary.unit_settings.system = 'METRIC'
temporary.unit_settings.scale_length = 1
entries = []
try:
    for old_group, new_group in source_groups.items():
        originals = [o for o in bpy.data.collections['ELEVATOR_ASSET'].objects
                     if o.get('elevator_export_group') == old_group]
        assert originals, old_group
        clones = []
        for original in originals:
            clone = original.copy()
            clone.data = original.data.copy()
            clone.animation_data_clear()
            clone.parent = None
            clone.matrix_world = Matrix.Translation(Vector((0,0,4))) @ original.matrix_world
            if clone.type == 'FONT' and ('floor number' in original.name.lower() or 'header number' in original.name.lower()):
                assert clone.data.body == '3', (original.name, clone.data.body)
                clone.data.body = '4'
            temporary.collection.objects.link(clone)
            clones.append(clone)
        bpy.context.window.scene = temporary
        bpy.context.view_layer.update()
        graph = bpy.context.evaluated_depsgraph_get()
        pivot = Vector((-2.85,-2.05,12)) if new_group=='CallStand_L4' else Vector((0,0,0))
        if new_group.startswith('Gate_'):
            pivot = Vector((-2.05,int(new_group.split('_')[-1])*.6,13.35))
        vertices, faces, indices, materials = [], [], [], []
        for clone in clones:
            mesh = bpy.data.meshes.new_from_object(clone.evaluated_get(graph), depsgraph=graph)
            start = len(vertices)
            vertices.extend(tuple(clone.matrix_world @ v.co-pivot) for v in mesh.vertices)
            for polygon in mesh.polygons:
                faces.append(tuple(start+i for i in polygon.vertices))
                material = mesh.materials[polygon.material_index]
                if material not in materials: materials.append(material)
                indices.append(materials.index(material))
            bpy.data.meshes.remove(mesh)
        mesh = bpy.data.meshes.new('SM_ELV_'+new_group)
        mesh.from_pydata(vertices, [], faces)
        mesh.update()
        for material in materials: mesh.materials.append(material)
        for polygon, index in zip(mesh.polygons, indices): polygon.material_index=index
        merged = bpy.data.objects.new(mesh.name, mesh)
        temporary.collection.objects.link(merged)
        for clone in clones: bpy.data.objects.remove(clone,do_unlink=True)
        for obj in temporary.objects: obj.select_set(False)
        merged.select_set(True)
        bpy.context.view_layer.update()
        target = out/'UnrealParts'/('SM_ELV_'+new_group+'.fbx')
        bpy.ops.export_scene.fbx(filepath=str(target),use_selection=True,object_types={'MESH'},
            apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='-Y',axis_up='Z',
            bake_anim=False,use_mesh_modifiers=True,mesh_smooth_type='FACE',path_mode='STRIP')
        entries.append({'name':'SM_ELV_'+new_group,'pivot_cm':list(pivot*100),
            'dimensions_cm':[float(v*100) for v in merged.dimensions],
            'vertices':len(vertices),'polygons':len(faces),'file':'UnrealParts/'+target.name})
        bpy.data.objects.remove(merged,do_unlink=True)
        bpy.context.window.scene = source
    names = {e['name'] for e in entries}
    manifest['parts'] = [e for e in manifest['parts'] if e['name'] not in names]+entries
    manifest['fourth_floor_extension'] = {'date':'2026-10-22','parts':sorted(names),
        'source':'Editable L3 groups, translated +400cm; display digits changed to 4; original blend preserved.'}
    manifest_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
finally:
    bpy.context.window.scene = source
    bpy.data.scenes.remove(temporary)
print('ELEVATOR_FOURTH_FLOOR_EXPORTED',len(entries))
