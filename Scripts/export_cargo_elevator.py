"""Bake editable detail into pivoted meshes without modifying the source model."""
import json
from pathlib import Path
import bpy
from mathutils import Vector

out = Path(__file__).resolve().parents[1] / 'Art/Equipment/CargoElevator'
parts_dir = out / 'UnrealParts'
parts_dir.mkdir(exist_ok=True)
source = bpy.data.scenes['Cargo Elevator - model']
bpy.context.window.scene = source
source.frame_set(1)
depsgraph = bpy.context.evaluated_depsgraph_get()
groups = {}
for obj in bpy.data.collections['ELEVATOR_ASSET'].objects:
    group = obj.get('elevator_export_group')
    if group:
        groups.setdefault(group, []).append(obj)
export_scene = bpy.data.scenes.new('Elevator export temporary')
manifest = {'date': '2026-10-21', 'units': 'metres in Blender; FBX unit metadata converts to centimetres',
            'axis_forward': '-Y', 'axis_up': 'Z', 'runtime_application_report': '../../../Saved/Testing/CargoElevator/ApplyReport.json',
            'materials': {}, 'parts': [],
            'collision_note': 'Use separate physical boxes for deck, walls and doors. Never one convex hull around the cabin void.',
            'material_note': 'FBX carries base PBR values. Procedural brushing is Blender-only; rebuild it in Unreal.'}
try:
    for group, objects in sorted(groups.items()):
        pivot = Vector((0, 0, -.05)) if group == 'LiftPlatform' else Vector((0, 0, 0))
        if group.startswith('CallStand_L'):
            pivot = Vector((-2.85,-2.05,(int(group[-1])-1)*4))
        if group.startswith('Gate_'):
            _, floor, side = group.split('_')
            pivot = Vector((-2.05, int(side)*.6, int(floor)*4+1.35))
        vertices, faces, indices, palette = [], [], [], []
        for obj in objects:
            mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph), depsgraph=depsgraph)
            start = len(vertices)
            vertices.extend(tuple(obj.matrix_world @ v.co - pivot) for v in mesh.vertices)
            for polygon in mesh.polygons:
                faces.append(tuple(start+i for i in polygon.vertices))
                mat = mesh.materials[polygon.material_index] if mesh.materials else None
                if mat not in palette:
                    palette.append(mat)
                indices.append(palette.index(mat))
            bpy.data.meshes.remove(mesh)
        mesh = bpy.data.meshes.new('SM_ELV_' + group)
        mesh.from_pydata(vertices, [], faces)
        mesh.update()
        for mat in palette:
            mesh.materials.append(mat)
            if mat:
                p = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
                manifest['materials'][mat.name] = {'base_color': list(mat.diffuse_color),
                    'metallic': p.inputs['Metallic'].default_value,
                    'roughness': p.inputs['Roughness'].default_value}
        for polygon, index in zip(mesh.polygons, indices):
            polygon.material_index = index
        obj = bpy.data.objects.new(mesh.name, mesh)
        export_scene.collection.objects.link(obj)
        obj.location = pivot
        obj['source_group'] = group
        manifest['parts'].append({'name': obj.name, 'pivot_cm': list(pivot*100),
            'dimensions_cm': [float(x*100) for x in obj.dimensions], 'vertices': len(vertices),
            'polygons': len(faces), 'file': 'UnrealParts/'+obj.name+'.fbx'})
    bpy.context.window.scene = export_scene
    bpy.context.view_layer.update()
    for obj in export_scene.objects:
        obj.select_set(True)
    kwargs = dict(use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_NONE', axis_forward='-Y', axis_up='Z',
        bake_anim=False, use_mesh_modifiers=True, mesh_smooth_type='FACE', path_mode='STRIP')
    assembly=out/'CargoElevator_Assembly.fbx'
    temporary=out/'CargoElevator_Assembly_export.fbx'
    bpy.ops.export_scene.fbx(filepath=str(temporary), **kwargs)
    temporary.replace(assembly)
    for obj, entry in zip(list(export_scene.objects), manifest['parts']):
        for other in export_scene.objects:
            other.select_set(False)
        obj.select_set(True)
        position = obj.location.copy()
        obj.location = (0, 0, 0)
        bpy.context.view_layer.update()
        # Resolve by name instead of relying on collection iteration order.
        target=parts_dir/(obj.name+'.fbx')
        temporary=parts_dir/(obj.name+'_export.fbx')
        bpy.ops.export_scene.fbx(filepath=str(temporary), **kwargs)
        temporary.replace(target)
        obj.location = position
    bpy.context.view_layer.update()
    # Dimensions are measured after dependency updates, not lazy object creation.
    for entry in manifest['parts']:
        entry['dimensions_cm'] = [float(v*100) for v in export_scene.objects[entry['name']].dimensions]
    (out/'export_manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding='utf-8')
finally:
    bpy.context.window.scene = source
    for obj in list(export_scene.objects):
        mesh = obj.data
        bpy.data.objects.remove(obj, do_unlink=True)
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    bpy.data.scenes.remove(export_scene)
print('ELEVATOR_EXPORTED', len(manifest['parts']), 'parts')
