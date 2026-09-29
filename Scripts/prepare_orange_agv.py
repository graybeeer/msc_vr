"""Build the tracked rigid mechanical rig assets from SourceAssets/OrangeAGV.

Run with UnrealEditor-Cmd -ExecutePythonScript=... -unattended -RenderOffscreen.
Uses native FBX import/mesh merge; never saves or rebuilds the warehouse map.
"""
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
BASE = '/Game/Warehouse/AGV'
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
meshes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def material(name, color, metallic, roughness, grain=0.04, emission=0):
    path = BASE + '/Materials/M_' + name
    mat = unreal.load_asset(path) if assets.does_asset_exist(path) else asset_tools.create_asset('M_'+name, BASE+'/Materials', unreal.Material, unreal.MaterialFactoryNew())
    if editing.get_num_material_expressions(mat):
        # The native actor may already hold this material; update its graph in place.
        glow = editing.get_material_property_input_node(mat,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        for expression in editing.get_material_expressions(mat):
            if isinstance(expression,unreal.MaterialExpressionConstant3Vector):
                expression.set_editor_property('constant',unreal.LinearColor(*color,1))
            elif isinstance(expression,unreal.MaterialExpressionConstant):
                expression.set_editor_property('r',metallic)
            elif isinstance(expression,unreal.MaterialExpressionAdd):
                expression.set_editor_property('const_b',roughness)
            elif isinstance(expression,unreal.MaterialExpressionMultiply):
                expression.set_editor_property('const_b',emission if expression==glow else grain)
        editing.recompile_material(mat)
        assets.save_loaded_asset(mat)
        return mat
    def node(cls):
        return editing.create_material_expression(mat, cls)
    def scalar(value):
        n = node(unreal.MaterialExpressionConstant)
        n.set_editor_property('r', value)
        return n
    def output(n, prop):
        editing.connect_material_property(n, '', prop)
    def link(a, b, slot):
        editing.connect_material_expressions(a, '', b, slot)
    tint = node(unreal.MaterialExpressionConstant3Vector)
    tint.set_editor_property('constant', unreal.LinearColor(*color, 1))
    output(tint, unreal.MaterialProperty.MP_BASE_COLOR)
    output(scalar(metallic), unreal.MaterialProperty.MP_METALLIC)
    # Object-local 3D grain stays attached during wheel and carriage movement.
    position = node(unreal.MaterialExpressionWorldPosition)
    local = node(unreal.MaterialExpressionTransformPosition)
    local.set_editor_property('transform_source_type', unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local.set_editor_property('transform_type', unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    link(position, local, 'Input')
    noise = node(unreal.MaterialExpressionNoise)
    noise.set_editor_property('scale', 4.0)
    noise.set_editor_property('levels', 2)
    noise.set_editor_property('quality', 1)
    link(local, noise, 'Position')
    amplitude = node(unreal.MaterialExpressionMultiply)
    amplitude.set_editor_property('const_b', grain)
    link(noise, amplitude, 'A')
    rough = node(unreal.MaterialExpressionAdd)
    rough.set_editor_property('const_b', roughness)
    link(amplitude, rough, 'A')
    output(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    if emission:
        glow = node(unreal.MaterialExpressionMultiply)
        glow.set_editor_property('const_b', emission)
        link(tint, glow, 'A')
        output(glow, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    editing.layout_material_expressions(mat)
    editing.recompile_material(mat)
    assets.save_loaded_asset(mat)
    return mat


SURFACES = {
    'Graphite_powder-coated_steel': ((.065,.075,.085), 0,.46,.04,0),
    'Black_rubber': ((.022,.026,.029), 0,.74,.06,0),
    'Machined_steel': ((.35,.38,.41), 1,.25,.07,0),
    'Safety_orange_-_painted_body': ((.72,.12,.016), 0,.36,.035,0),
    'Control_enclosure_-_charcoal': ((.038,.045,.052), 0,.48,.04,0),
    'White_print': ((.79,.81,.78), 0,.48,.02,0),
    'Polyurethane_roller': ((.22,.19,.13), 0,.55,.05,0),
    'Hydraulic_piston_chrome': ((.62,.65,.68), 1,.13,.02,0),
    'Optical_lens': ((.005,.018,.026), .45,.08,.005,0),
    'Sensor_status_green': ((.02,.8,.19), .1,.22,.01,3),
    'Screen_blue': ((.018,.18,.36), .05,.24,.01,1.5),
    'Safety_warning_yellow': ((.95,.64,.015), .1,.4,.03,0),
    'Emergency_stop_red': ((.65,.012,.009), 0,.38,.04,0),
}


def import_source():
    if assets.does_directory_exist(BASE+'/SourceMeshes'):
        return
    options = unreal.FbxImportUI()
    for key, value in dict(import_mesh=True, import_as_skeletal=False, import_materials=True,
                           import_textures=False, import_animations=False,
                           automated_import_should_detect_type=False,
                           mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():
        options.set_editor_property(key,value)
    data = options.static_mesh_import_data
    for key,value in dict(combine_meshes=False, auto_generate_collision=False,
                          transform_vertex_to_absolute=True, convert_scene=True,
                          convert_scene_unit=True, import_rotation=unreal.Rotator(yaw=90)).items():
        data.set_editor_property(key,value)
    task = unreal.AssetImportTask()
    for key,value in dict(filename=str(ROOT/'SourceAssets/OrangeAGV/orange_agv.fbx'),
                          destination_path=BASE+'/SourceMeshes', automated=True,
                          replace_existing=True, save=True, options=options,
                          factory=unreal.FbxFactory()).items():
        task.set_editor_property(key,value)
    asset_tools.import_asset_tasks([task])


def build():
    import_source()
    palette = {name: material(name,*values) for name,values in SURFACES.items()}
    groups = {}
    for path in assets.list_assets(BASE+'/SourceMeshes'):
        mesh = unreal.load_asset(path)
        if not isinstance(mesh,unreal.StaticMesh):
            continue
        name = mesh.get_name().removeprefix('orange_agv_')
        if name == 'Ground' or name.startswith('Blue_floor_boundary'):
            continue
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector())
        comp = actor.static_mesh_component
        comp.set_static_mesh(mesh)
        center, extent = actor.get_actor_bounds(False)
        side = 1 if center.y < 0 else -1
        suffix = 'L' if side < 0 else 'R'
        target = unreal.Vector(-center.x+11.5,-center.y,center.z)
        scale = unreal.Vector(1,1,1)
        pivot = unreal.Vector()
        group = 'Body'
        if name.startswith(('Driven_rubber_wheel','Drive_wheel_hub')):
            group = 'DriveWheel'+suffix
            pivot = unreal.Vector(-46.5,side*56,17)
            target.y += side*22
            target.z -= 1
        elif name.startswith(('Front_polyurethane_load_roller','Roller_axle_cap')):
            group = 'LoadWheel'+suffix
            pivot = unreal.Vector(164.5,side*62,9.8)
            target.y += side*31
            target.z -= .7
        elif name.startswith('Outrigger_support_leg'):
            group = 'Outrigger'+suffix
            target.y = side*62
        elif name.startswith(('Forged_fork_blade','Tapered_fork_nose')):
            group = 'Fork'+suffix
            # Both bevelled pieces together span 50..170 X, +/-22 Y, 5.5..9.5 Z.
            scale = unreal.Vector(120/154,8/14,4/6.75)
            target.x = 50+(-center.x-28)*scale.x
            target.y = side*22
            target.z = 5.5+(center.z-23.5)*scale.z
        elif name.startswith('Vertical_fork_heel'):
            group = 'Carriage'
            scale.y = 8/14
            scale.x = 8/7
            target.x, target.y = 49,side*22
            target.z -= 19.5
        elif name.startswith(('Fork_carriage','Load_backrest','Carriage_locking')):
            group = 'Carriage'
            target.z -= 19.5
        elif name.startswith(('Inner_sliding_mast','Chrome_lift_ram','Lift_chain')):
            group = 'LiftStage'
        elif name.startswith(('Outer_mast','Polished_mast','Mast_cross','Hydraulic_cylinder','Hydraulic_hose')):
            group = 'MastFrame'
        elif any(key in name for key in ('Navigation','Sensor_crossbar','Stereo_sensor','Lidar','HMI','Display_status','Upper_','Front_perception','Tower_fleet')):
            group = 'SensorTower'
        actor.set_actor_rotation(unreal.Rotator(yaw=180),False)
        actor.set_actor_scale3d(scale)
        actor.set_actor_location(target-unreal.Vector(-center.x*scale.x,-center.y*scale.y,center.z*scale.z)-pivot,False,False)
        for index in range(comp.get_num_materials()):
            original = str(mesh.static_materials[index].material_slot_name).replace(' ','_')
            assert original in palette, original
            mesh.set_material(index,palette[original])
            comp.set_material(index,palette[original])
        # Convex collision per mechanical piece preserves the gaps between rails.
        meshes.remove_collisions(mesh)
        if name.startswith(('Forged_fork_blade','Tapered_fork_nose')):
            # Exact bounds prevent voxel hull inflation closing the pallet's 0.5 mm contact clearance.
            assert meshes.add_simple_collisions(mesh,unreal.ScriptCollisionShapeType.BOX)>=0
        else:
            assert meshes.set_convex_decomposition_collisions(mesh, 1, 32, 10000), name
        unreal.WarehouseCargo.configure_mesh_collision(mesh,False)
        groups.setdefault(group,[]).append(actor)
    # Solid crossmembers connect the wider straddle legs and exposed wheel axles to the chassis.
    for center,size in [((28.5,0,11.5),(25,144,12)),((-46.5,0,17),(12,112,12))]:
        brace = actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*center))
        brace.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
        brace.static_mesh_component.set_material(0,palette['Graphite_powder-coated_steel'])
        brace.set_actor_scale3d(unreal.Vector(*(v/100 for v in size)))
        groups['Body'].append(brace)
    for group, sources in groups.items():
        options = unreal.MergeStaticMeshActorsOptions()
        options.base_package_name = BASE+'/Meshes/AGV_'+group
        options.destroy_source_actors = True
        options.spawn_merged_actor = True
        settings = options.mesh_merging_settings
        settings.pivot_type = unreal.MeshMergePivotType.WORLD_ORIGIN
        settings.merge_materials = False
        settings.merge_physics_data = True
        settings.generate_light_map_uv = False
        options.mesh_merging_settings = settings
        merged = meshes.merge_static_mesh_actors(sources,options)
        assert merged, group
        result = merged.static_mesh_component.static_mesh
        assert all(slot.material_interface and slot.material_interface.get_path_name().startswith(BASE+'/Materials/') for slot in result.static_materials), ('Lost merge materials',group)
        unreal.WarehouseCargo.configure_mesh_collision(result,False)
        assert meshes.get_simple_collision_count(result)+meshes.get_convex_collision_count(result)>0, group
        assets.save_loaded_asset(result)
        actors.destroy_actor(merged)
        print('AGV_RIG_PART',group,result.get_path_name())
    print('ORANGE_AGV_ASSETS_READY',len(groups),'mechanical groups')


if __name__ == '__main__':
    build()
