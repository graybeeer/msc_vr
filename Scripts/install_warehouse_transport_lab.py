"""Import our transport extensions; save only a separate optional experiment map.

Run in Unreal Python, outside PIE. Never loads or saves the default warehouse.
"""
import json
import re
from pathlib import Path
import unreal

def install_transport():
    root = Path(unreal.Paths.project_dir()).resolve()
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert editor.get_game_world() is None
    assets = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    normalize = lambda text: re.sub('[^a-z0-9]', '', text.lower())
    report = {'default_map_saved': False, 'meshes': []}
    request_file = root/'Saved/Testing/InboundWrapRequest.json'
    request = json.loads(request_file.read_text(encoding='utf-8-sig')) if request_file.exists() else {}
    requested_reimports = request.get('reimport_models', [])

    for directory, destination in [('PalletLiftExtensions', '/Game/Warehouse/PalletLift'),
                                   ('Conveyor', '/Game/Warehouse/Conveyor')]:
        folder = root/'Art/Equipment'/directory
        manifest = json.loads((folder/'manifest.json').read_text(encoding='utf-8'))
        materials = {normalize(name): unreal.load_asset('/Game/Warehouse/PalletLift/Materials/M_'+name)
                     for name in manifest['materials']}
        assert all(materials.values()), 'Install existing pallet lift materials first'
        for name, filename in manifest['files'].items():
            options = unreal.FbxImportUI()
            for key, value in dict(import_mesh=True, import_as_skeletal=False, import_materials=False,
                    import_textures=False, import_animations=False, automated_import_should_detect_type=False,
                    mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():
                options.set_editor_property(key, value)
            for key, value in dict(combine_meshes=True, auto_generate_collision=False, one_convex_hull_per_ucx=True,
                    transform_vertex_to_absolute=True, convert_scene=True, convert_scene_unit=True,
                    import_uniform_scale=1.0, import_rotation=unreal.Rotator()).items():
                options.static_mesh_import_data.set_editor_property(key, value)
            task = unreal.AssetImportTask()
            for key, value in dict(filename=str(folder/filename), destination_path=destination+'/Meshes',
                    destination_name=name, automated=True, replace_existing=True, replace_existing_settings=True,
                    save=True, options=options, factory=unreal.FbxFactory()).items():
                task.set_editor_property(key, value)
            path = destination+'/Meshes/'+name
            reimport = '-transportmodelreimport' in unreal.SystemLibrary.get_command_line().lower() or name in requested_reimports
            if not assets.does_asset_exist(path) or reimport:
                old_mesh = unreal.load_asset(path)
                if old_mesh:
                    stored = old_mesh.get_editor_property('asset_import_data')
                    assert isinstance(stored, unreal.FbxStaticMeshImportData), 'Use the tracked legacy FBX asset for reimport'
                    for key, value in dict(convert_scene=True, convert_scene_unit=True, import_uniform_scale=1.0,
                            import_rotation=unreal.Rotator(), import_translation=unreal.Vector(),
                            transform_vertex_to_absolute=True, combine_meshes=True, auto_generate_collision=False,
                            one_convex_hull_per_ucx=True).items():
                        stored.set_editor_property(key, value)
                previous = unreal.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX')
                unreal.SystemLibrary.execute_console_command(editor.get_editor_world(), 'Interchange.FeatureFlags.Import.FBX 0')
                try:
                    tools.import_asset_tasks([task])
                finally:
                    unreal.SystemLibrary.execute_console_command(editor.get_editor_world(),
                        f'Interchange.FeatureFlags.Import.FBX {previous}')
            mesh = unreal.load_asset(path)
            assert isinstance(mesh, unreal.StaticMesh), (name, task.imported_object_paths)
            slots = list(mesh.get_editor_property('static_materials'))
            changed_materials = False
            for slot in slots:
                material = materials.get(normalize(str(slot.material_slot_name)))
                assert material, (name, slot.material_slot_name)
                changed_materials |= slot.material_interface != material
                slot.set_editor_property('material_interface', material)
            if changed_materials:
                mesh.set_editor_property('static_materials', slots)
            is_lift_frame = name.startswith('SM_PalletLift_Frame')
            if is_lift_frame:
                unreal.WarehouseCargo.configure_mesh_collision(mesh, True)
            else:
                mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag',
                    unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
            assets.save_loaded_asset(mesh)
            bounds = mesh.get_bounds()
            hulls = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_convex_collision_count(mesh)
            assert is_lift_frame or hulls > 0, (name, 'Missing UCX')
            report['meshes'].append(dict(path=path, size_cm=[bounds.box_extent.x*2, bounds.box_extent.y*2,
                bounds.box_extent.z*2], hulls=hulls))
            print('TRANSPORT_MODEL_IMPORTED', name, bounds, hulls)

    if request.get('models_only'):
        output = root/'Saved/Testing/Construction'
        output.mkdir(parents=True, exist_ok=True)
        (output/'ModelImportReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        print('TRANSPORT_MODELS_INSTALLED', json.dumps(report, ensure_ascii=False))
        return

    # The lab is optional. It is deliberately independent of the World Partition
    # warehouse and cannot change its external actors, level or startup settings.
    lab_path = '/Game/Warehouse/Maps/Warehouse_TransportLab'
    if assets.does_asset_exist(lab_path):
        assert level.load_level(lab_path)
        for actor in list(actors.get_all_level_actors()):
            if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
                actors.destroy_actor(actor)
    else:
        assert level.new_level(lab_path)
    world = editor.get_editor_world()
    settings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0]
    settings.set_editor_property('default_game_mode', unreal.load_class(None,
        '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    steel = unreal.load_asset('/Game/Warehouse/PalletLift/Materials/M_Lift_GalvanizedSteel')

    def spawn(cls, name, point, yaw=0):
        obj = actors.spawn_actor_from_class(cls, unreal.Vector(*point), unreal.Rotator(yaw=yaw))
        obj.set_actor_label(name)
        return obj

    def block(name, point, size, floor=0):
        obj = spawn(unreal.StaticMeshActor, name, point)
        obj.static_mesh_component.set_static_mesh(cube)
        obj.static_mesh_component.set_collision_profile_name('BlockAll')
        obj.static_mesh_component.set_material(0, steel)
        obj.set_actor_scale3d(unreal.Vector(*[v/100 for v in size]))
        obj.tags = ['LayoutFixed', 'WarehouseMapStructure', f'WarehouseFloor{floor}']
        return obj

    block('LabGround', (0, 0, -10), (5000, 4000, 20))
    spawn(unreal.PlayerStart, 'LabStart', (1100, -900, 100), 150)
    spawn(unreal.load_class(None, '/Script/msc_vr.WarehouseSimulationClock'), 'LabClock', (0, 0, 0))
    lift = spawn(unreal.WarehousePalletLift, 'LabFreightLift', (700, 700, 0))
    lift.set_editor_property('floor_count', 4)
    lift.set_editor_property('powered_conveyor', True)
    lift.set_editor_property('travel_speed_cm', 50)
    for floor in (0, 3):
        conveyor = spawn(unreal.WarehouseConveyor, 'LabLiftPort'+str(floor+1), (484.5, 700, floor*400))
        conveyor.set_editor_property('lift', lift)
        conveyor.set_editor_property('lift_floor', floor)
        conveyor.set_editor_property('destination_floor', 3 if floor == 0 else 0)
        conveyor.set_editor_property('lift_infeed', floor == 0)
        conveyor.rebuild_conveyor()
        if floor:
            block('LabReceivingLanding', (200, 700, floor*400-10), (400, 400, 20), floor)
    spawn(unreal.WarehousePallet, 'LabLiftPallet', (390, 700, 40.2))
    pickup = spawn(unreal.WarehouseConveyor, 'LabPickupLine', (-400, -500, 0))
    pickup.rebuild_conveyor()
    spawn(unreal.WarehousePallet, 'LabPickupPallet', (-490, -500, 40.2), 180)
    cartons = spawn(unreal.WarehouseConveyor, 'LabCartonLine', (-400, -1100, 0))
    cartons.set_editor_property('kind', unreal.WarehouseConveyorKind.CARTON_BELT)
    cartons.set_editor_property('width_cm', 70)
    cartons.rebuild_conveyor()
    parcel = spawn(unreal.WarehouseCargo, 'LabCarton', (-490, -1100, 60.2))
    parcel.set_cargo_mesh(cube)
    parcel.set_actor_scale3d(unreal.Vector(.4, .35, .4))
    parcel.set_gross_mass_kg(12)
    corner_source = spawn(unreal.WarehouseConveyor, 'LabCornerInfeed', (-1600, 700, 0))
    corner_source.rebuild_conveyor()
    corner = spawn(unreal.WarehouseConveyor, 'Lab90DegreeTransfer', (-1100, 700, 0))
    corner.set_editor_property('kind', unreal.WarehouseConveyorKind.ROTARY_TRANSFER)
    assert corner.configure_geometry(200, 200, 40, 20), corner.get_editor_property('status')
    assert corner.snap_entrance_to(corner_source), corner.get_editor_property('status')
    corner_output = spawn(unreal.WarehouseConveyor, 'LabCornerOutfeed', (-1200, 200, 0), -90)
    corner_output.rebuild_conveyor()
    assert corner_output.snap_entrance_to(corner), corner_output.get_editor_property('status')
    spawn(unreal.WarehousePallet, 'LabCornerPallet', (-1690, 700, 40.2))
    vehicle = spawn(unreal.WarehouseForklift, 'LabForklift', (450, -500, 0), 180)
    vehicle.set_editor_property('auto_start', False)
    vehicle.set_editor_property('pending_jobs', [])
    light = spawn(unreal.DirectionalLight, 'LabSun', (0, 0, 1800))
    light.set_actor_rotation(unreal.Rotator(pitch=-55, yaw=-30), False)
    sky = spawn(unreal.SkyLight, 'LabSky', (0, 0, 1400))
    sky.light_component.set_editor_property('intensity', 2)
    spawn(unreal.SkyAtmosphere, 'LabAtmosphere', (0, 0, 0))
    assert level.save_current_level()
    report['lab_map'] = '/Game/Warehouse/Maps/Warehouse_TransportLab'
    output = root/'Saved/Testing/Construction'
    output.mkdir(parents=True, exist_ok=True)
    (output/'InstallReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('TRANSPORT_LAB_INSTALLED', json.dumps(report, ensure_ascii=False))


install_transport()
