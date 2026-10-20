"""Import the original pallet tower and add it beside the unchanged AGV lift."""
import json
import re
import time
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/Testing/PalletLift'
OUT.mkdir(parents=True, exist_ok=True)
BASE = '/Game/Warehouse/PalletLift'
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert editor.get_game_world() is None
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
manifest = json.loads((ROOT/'Art/Equipment/PalletLift/manifest.json').read_text(encoding='utf-8'))
all_actors = list(actors.get_all_level_actors())
generic = [a for a in all_actors if isinstance(a, unreal.WarehouseElevator)]
assert generic, 'Existing general elevator must be present'

def pose_values(pose):
    return tuple(round(getattr(value, axis), 6) for value, axes in ((pose.translation,'xyz'),(pose.rotation,'xyzw'),(pose.scale3d,'xyz')) for axis in axes)

def signature(actor):
    return {'pose': pose_values(actor.get_actor_transform()), 'parts': [
        (c.get_name(), pose_values(c.get_relative_transform()), c.static_mesh.get_path_name() if c.static_mesh else None,
         [c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())], str(c.get_collision_enabled()))
        for c in actor.get_components_by_class(unreal.StaticMeshComponent)]}

before = {a.get_path_name(): signature(a) for a in generic}
materials = {}
for name, data in manifest['materials'].items():
    path = BASE+'/Materials/M_'+name
    mat = unreal.load_asset(path)
    if mat:
        materials[name.lower()] = mat
        continue
    mat = tools.create_asset('M_'+name, BASE+'/Materials', unreal.Material, unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
    for i, (prop, value) in enumerate(((unreal.MaterialProperty.MP_BASE_COLOR, data['color']),
                                     (unreal.MaterialProperty.MP_METALLIC, data['metallic']),
                                     (unreal.MaterialProperty.MP_ROUGHNESS, data['roughness']))):
        vector = isinstance(value, list)
        node = unreal.MaterialEditingLibrary.create_material_expression(mat,
            unreal.MaterialExpressionConstant3Vector if vector else unreal.MaterialExpressionConstant, -300, i*130)
        node.set_editor_property('constant' if vector else 'r', unreal.LinearColor(*value, 1) if vector else value)
        unreal.MaterialEditingLibrary.connect_material_property(node, '', prop)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    assets.save_loaded_asset(mat)
    materials[name.lower()] = mat
meshes = {}
request_path = ROOT/'Saved/Testing/InboundWrapRequest.json'
reimport_names = json.loads(request_path.read_text(encoding='utf-8-sig')).get('reimport_meshes', []) if request_path.exists() else []
for name, filename in manifest['files'].items():
    options = unreal.FbxImportUI()
    for key, value in dict(import_mesh=True, import_as_skeletal=False, import_materials=False, import_textures=False,
        import_animations=False, automated_import_should_detect_type=False, mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():
        options.set_editor_property(key, value)
    for key, value in dict(combine_meshes=True, auto_generate_collision=False, one_convex_hull_per_ucx=True,
        transform_vertex_to_absolute=True, convert_scene=True, convert_scene_unit=True, import_rotation=unreal.Rotator()).items():
        options.static_mesh_import_data.set_editor_property(key, value)
    task = unreal.AssetImportTask()
    for key, value in dict(filename=str(ROOT/'Art/Equipment/PalletLift'/filename), destination_path=BASE+'/Meshes',
        destination_name=name, automated=True, replace_existing=True, replace_existing_settings=True, save=True, options=options, factory=unreal.FbxFactory()).items():
        task.set_editor_property(key, value)
    # Reuse prepared assets unless the caller is intentionally reimporting an edited model.
    flags = unreal.SystemLibrary.get_command_line().lower().split()
    if not assets.does_asset_exist(BASE+'/Meshes/'+name) or name in reimport_names or '-palletliftreimport' in flags or ('-palletliftreimportcarrier' in flags and name.endswith('Carrier')):
        previous = unreal.load_asset(BASE+'/Meshes/'+name)
        if previous:
            # UE's automated FBX reimport substitutes the mesh's stored import
            # data for task.options. Apply unit settings there as well.
            previous_data = previous.get_editor_property('asset_import_data')
            if isinstance(previous_data, unreal.FbxStaticMeshImportData):
                for key, value in dict(convert_scene=True, convert_scene_unit=True, import_uniform_scale=1.0,
                    import_rotation=unreal.Rotator(), import_translation=unreal.Vector(), transform_vertex_to_absolute=True,
                    combine_meshes=True, auto_generate_collision=False, one_convex_hull_per_ucx=True).items():
                    previous_data.set_editor_property(key, value)
            else:
                # These are our regenerable, dedicated lift meshes. A legacy
                # factory cannot override an Interchange reimport pipeline;
                # rebuild this generated asset from the tracked original FBX.
                assert assets.delete_asset(BASE+'/Meshes/'+name), 'Could not rebuild generated Interchange lift mesh'
        # Match the explicit FbxImportUI/UCX settings; Interchange otherwise
        # substitutes its own pipeline and can discard centimetre conversion.
        old_fbx = unreal.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX')
        unreal.SystemLibrary.execute_console_command(editor.get_editor_world(), 'Interchange.FeatureFlags.Import.FBX 0')
        try:
            tools.import_asset_tasks([task])
        finally:
            unreal.SystemLibrary.execute_console_command(editor.get_editor_world(), f'Interchange.FeatureFlags.Import.FBX {old_fbx}')
    mesh = unreal.load_asset(BASE+'/Meshes/'+name)
    assert isinstance(mesh, unreal.StaticMesh), (name, task.imported_object_paths)
    normalize = lambda value: re.sub('[^a-z0-9]', '', value.lower())
    slots = list(mesh.get_editor_property('static_materials'))
    changed = False
    for slot in slots:
        key = normalize(str(slot.material_slot_name))
        material = next((v for n, v in materials.items() if normalize(n) == key), None)
        assert material, (name, key)
        if slot.material_interface != material:
            slot.set_editor_property('material_interface', material)
            changed = True
    # A single mesh edit avoids rebuilding distance fields/cards for every slot.
    if changed:
        mesh.set_editor_property('static_materials', slots)
    if name.endswith('Frame'):
        unreal.WarehouseCargo.configure_mesh_collision(mesh, True)
    else:
        setup = mesh.get_editor_property('body_setup')
        setup.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    assets.save_loaded_asset(mesh)
    meshes[name.rsplit('_', 1)[-1]] = mesh
    print('PALLET_LIFT_IMPORTED', name, mesh.get_bounds())
frame_bounds = meshes['Frame'].get_bounds()
carrier_bounds = meshes['Carrier'].get_bounds()
assert 1100 < frame_bounds.box_extent.z*2 < 1130, frame_bounds
assert abs(carrier_bounds.box_extent.x*2-125) < 1, carrier_bounds
assert abs(carrier_bounds.origin.z+carrier_bounds.box_extent.z) < .1, carrier_bounds

def is_floor(actor):
    name = actor.get_actor_label()
    return isinstance(actor, unreal.StaticMeshActor) and (name in ('Floor', 'WH_InboundFloor', 'MZ_GroundRearWest', 'MZ_GroundRearEast', 'MZ_GroundRear') or
        bool(re.fullmatch(r'MZ_L[23]_Deck_(Left|Right|RearWest|RearEast|North|South)', name)) or 'ElevatorLayoutFloor' in [str(t) for t in actor.tags])


def _hits(result):
    if isinstance(result, unreal.HitResult):
        return [result.to_tuple()]
    if isinstance(result, (tuple, list, unreal.Array)):
        return [hit for value in result for hit in _hits(value)]
    return []


floors = [a for a in all_actors if is_floor(a) and a.get_actor_enable_collision()]
existing = next((a for a in all_actors if isinstance(a, unreal.WarehousePalletLift)), None)
if existing:
    # Moving an installed shaft without restoring its old cutout leaves a hole.
    # Re-application is deliberately in place; actual relocation is a layout transaction.
    saved_position = existing.get_actor_location()
    initial_position = existing.get_editor_property('initial_shaft_position')
    assert (saved_position-initial_position).length() < .1, 'Existing lift and saved initial cutout differ; restore them before re-applying'
    orientation = existing.get_actor_rotation()
    assert abs(orientation.pitch) < .01 and abs(orientation.roll) < .01 and abs((orientation.yaw+45)%90-45) < .01
    candidates = [saved_position]
else:
    orientation = unreal.Rotator(yaw=90)
    candidates = [unreal.Vector(x, y, 0) for y in [1450, 1550, 1650] for x in [-650, -850, -1050, -1250]]
lift = existing or actors.spawn_actor_from_class(unreal.WarehousePalletLift, unreal.Vector(-600, 1600, 0), unreal.Rotator(yaw=90))
lift.modify()
lift.set_actor_label('MZ_PalletCargoLift')
lift.set_folder_path('Warehouse/Mezzanine/PalletLift')
lift.set_editor_property('is_spatially_loaded', False)
lift.tags = ['WarehousePhysicalRig', 'WarehousePalletLift', 'WarehouseMapStructure', 'LayoutType:Elevator', 'LayoutGroup:PalletCargoLift', 'WarehouseFloor0']
world = editor.get_editor_world()
ignore = floors + [lift]
# Check the complete shaft stroke and floor-supported entry on every floor.
chosen = None
for position in candidates:
    shaft = position+unreal.Vector(0, 0, 560)
    blocking = [h for h in _hits(unreal.SystemLibrary.box_trace_multi(world, shaft, shaft, unreal.Vector(80, 80, 559.5), orientation,
        unreal.TraceTypeQuery.ECC_VISIBILITY, False, ignore, unreal.DrawDebugTrace.NONE, True)) if h[0]]
    if blocking:
        print('PALLET_LIFT_SITE_BLOCKED', position, [h[9].get_actor_label() if h[9] else None for h in blocking])
        continue
    supported = True
    pose = unreal.Transform(location=position, rotation=orientation)
    for f in range(3):
        # Require the real floor at all four entry corners and its centre, not
        # a pallet/rack top or a single supported point beside an atrium.
        for x, y in [(-248,-88),(-248,88),(-92,-88),(-92,88),(-170,0)]:
            point = pose.transform_location(unreal.Vector(x, y, f*400))
            hit = next((h for h in _hits(unreal.SystemLibrary.line_trace_single(world, point+unreal.Vector(0,0,5), point-unreal.Vector(0,0,5),
                unreal.TraceTypeQuery.ECC_VISIBILITY, False, [lift], unreal.DrawDebugTrace.NONE, True)) if h[0]), None)
            if not hit or hit[9] not in floors or hit[7].z < .98 or abs(hit[5].z-point.z) > .1:
                print('PALLET_LIFT_ENTRY_UNSUPPORTED',position,f,point,'hit',hit)
                supported = False
    if supported:
        chosen = position
        break
assert chosen, 'No unblocked three-floor site. Saved map has not been changed.'
lift.set_actor_location(chosen, False, True)
lift.set_actor_rotation(orientation, False)
if not existing:
    lift.set_editor_property('initial_shaft_position', chosen)
parts = {c.get_name(): c for c in lift.get_components_by_class(unreal.StaticMeshComponent)}
for part in parts.values():
    part.modify()
    if part.get_name().startswith(('Column_', 'FrameTop', 'FrameBack', 'FrameSide', 'FrameRear')):
        part.set_visibility(False); part.set_hidden_in_game(True); part.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
for part_name, suffix in [('Frame', 'Frame'), ('CargoPlatform', 'Carrier')]:
    part = parts[part_name]
    part.set_static_mesh(meshes[suffix]); part.set_relative_scale3d(unreal.Vector(1,1,1))
    part.set_relative_location(unreal.Vector(0,0,40 if suffix=='Carrier' else 0), False, True)
    part.set_editor_property('override_materials', [])
    part.set_collision_profile_name('BlockAllDynamic')
for gate_part in lift.get_editor_property('gates'):
    gate_part.modify(); gate_part.set_static_mesh(meshes['Gate']); gate_part.set_relative_scale3d(unreal.Vector(1,1,1))
    gate_part.set_editor_property('override_materials', [])

# Persist real openings, leaving the original general elevator's geometry alone.
# Split only slabs intersecting this new 180cm shaft; preserve materials/profiles.
hole = (chosen.x-90, chosen.y-90, chosen.x+90, chosen.y+90)
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
strength = next((a for a in all_actors if isinstance(a, unreal.WarehouseDamageSystem)), None)
specs = list(strength.get_editor_property('objects')) if strength else []
cut_records = []
for floor_actor in floors:
    centre, extent = floor_actor.get_actor_bounds(False)
    lowx, lowy, highx, highy = centre.x-extent.x, centre.y-extent.y, centre.x+extent.x, centre.y+extent.y
    a,b,c,d = max(lowx,hole[0]), max(lowy,hole[1]), min(highx,hole[2]), min(highy,hole[3])
    if c<=a or d<=b:
        continue
    pieces = [(lowx,lowy,a,highy), (c,lowy,highx,highy), (a,lowy,c,b), (a,d,c,highy)]
    pieces = [p for p in pieces if p[2]-p[0]>.1 and p[3]-p[1]>.1]
    assert pieces
    original_name = floor_actor.get_actor_label()
    source = floor_actor.static_mesh_component
    # Copy the complete editable BodyInstance, including custom channel responses,
    # physical-material override and mass settings, before modifying the original piece.
    body = source.get_editor_property('body_instance')
    profile_name = source.get_collision_profile_name()
    mode = source.get_collision_enabled()
    mobility = source.get_editor_property('mobility')
    floor_materials = list(source.get_materials())
    source_properties = {key: source.get_editor_property(key) for key in (
        'visible','hidden_in_game','cast_shadow','render_custom_depth','custom_depth_stencil_value',
        'generate_overlap_events','can_character_step_up_on','component_tags')}
    actor_collision = floor_actor.get_actor_enable_collision()
    actor_hidden = floor_actor.get_editor_property('hidden')
    spatially_loaded = floor_actor.get_editor_property('is_spatially_loaded')
    floor_index = max(0,min(2,round((centre.z+extent.z)/400)))
    profile = next((s for s in specs if floor_actor in s.get_editor_property('members')), None)
    for i, (x0,y0,x1,y1) in enumerate(pieces):
        actor = floor_actor if i==0 else actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector())
        actor.modify()
        actor.set_actor_label(original_name if i==0 else original_name+'_PalletLiftCut_'+str(i))
        actor.set_editor_property('is_spatially_loaded', spatially_loaded)
        actor.tags = list(dict.fromkeys([str(t) for t in floor_actor.tags]+['ElevatorLayoutFloor','WarehouseMapStructure',f'WarehouseFloor{floor_index}']))
        component=actor.static_mesh_component
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        component.set_static_mesh(cube)
        component.set_editor_property('override_materials', floor_materials)
        component.set_collision_profile_name(profile_name)
        component.set_editor_property('body_instance', body)
        component.set_collision_enabled(mode)
        for key, value in source_properties.items():
            component.set_editor_property(key, value)
        actor.set_actor_enable_collision(actor_collision)
        actor.set_actor_hidden_in_game(actor_hidden)
        actor.set_actor_location(unreal.Vector((x0+x1)/2,(y0+y1)/2,centre.z),False,True)
        actor.set_actor_rotation(unreal.Rotator(),False)
        actor.set_actor_scale3d(unreal.Vector((x1-x0)/100,(y1-y0)/100,extent.z*2/100))
        component.set_mobility(mobility)
        if i and profile:
            members = list(profile.get_editor_property('members'))
            physics = list(profile.get_editor_property('physics_meshes'))
            physics.extend([None] * max(0, len(members)-len(physics)))
            profile.set_editor_property('members', members+[actor])
            profile.set_editor_property('physics_meshes', physics+[None])
    cut_records.append({'source':original_name,'floor':floor_index,'pieces':len(pieces)})
if strength:
    strength.modify(); strength.set_editor_property('objects', specs)
for f in range(3):
    label = next((a for a in all_actors if a.get_actor_label()==f'WH_AreaUI_Lift_L{f+1}'), None)
    if label:
        label.modify(); label.set_editor_property('area_name', unreal.Text(f'{f+1}층 · 범용 엘리베이터'))
    name=f'WH_AreaUI_PalletLift_L{f+1}'
    marker=next((a for a in all_actors if a.get_actor_label()==name),None) or actors.spawn_actor_from_class(unreal.WarehouseAreaLabel,unreal.Vector())
    marker.modify();marker.set_actor_label(name)
    marker.set_actor_location(unreal.Transform(location=chosen, rotation=orientation).transform_location(unreal.Vector(-170,0,f*400+310)),False,True)
    marker.set_folder_path('Warehouse/AreaUI/PalletLift')
    marker.set_editor_property('is_spatially_loaded',False)
    marker.set_editor_property('area_name',unreal.Text(f'{f+1}층 · 화물용 엘리베이터 · 팔레트 1개'))
    marker.set_editor_property('floor_index',f)
    marker.tags=['WarehouseAreaLabel',f'WarehouseFloor{f}','LayoutType:Elevator','LayoutGroup:PalletCargoLift']
    marker.set_actor_enable_collision(False)
assert before == {a.get_path_name():signature(a) for a in generic}, 'General elevator changed unexpectedly'
assert level.save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
report={'date':'2026-10-20','map_saved':True,'position_cm':[chosen.x,chosen.y,chosen.z], 'shaft_opening_cm':[180,180],
        'carrier_cm':[125,125],'general_lifts_preserved':len(generic),'floor_cuts':cut_records,'assets':manifest['files']}
(OUT/'Installation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('PALLET_LIFT_MAP_APPLIED',json.dumps(report,ensure_ascii=False))

# This camera is created only after the map save and is never persisted.
(OUT/'Installation.png').unlink(missing_ok=True)
(OUT/'CaptureError.txt').unlink(missing_ok=True)
_capture_pose=unreal.Transform(location=chosen,rotation=orientation)
_capture_eye=_capture_pose.transform_location(unreal.Vector(-420,-260,230))
_capture_target=chosen+unreal.Vector(0,0,150)
_capture_rotation=unreal.MathLibrary.find_look_at_rotation(_capture_eye,_capture_target)
_capture_camera=actors.spawn_actor_from_class(unreal.CameraActor,_capture_eye,_capture_rotation)
_capture_camera.set_actor_label('TEMP_PalletLiftCapture')
_capture_camera.camera_component.set_field_of_view(55)
editor.set_level_viewport_camera_info(_capture_eye,_capture_rotation)
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
_capture_started=time.monotonic()
_capture_requested=None


def _capture_tick(dt):
    global _capture_requested
    try:
        if time.monotonic()-_capture_started<25:
            return
        if _capture_requested is None:
            unreal.AutomationLibrary.take_high_res_screenshot(1600,1000,str(OUT/'Installation.png'),camera=_capture_camera,delay=1.0)
            _capture_requested=time.monotonic()
            return
        if time.monotonic()-_capture_requested<5:
            return
        assert (OUT/'Installation.png').is_file(), 'Installation capture missing'
        print('PALLET_LIFT_CAPTURE_COMPLETE',str(OUT/'Installation.png'))
        actors.destroy_actor(_capture_camera)
        unreal.unregister_slate_post_tick_callback(_capture_handle)
    except Exception:
        (OUT/'CaptureError.txt').write_text(traceback.format_exc(),encoding='utf-8')
        actors.destroy_actor(_capture_camera)
        unreal.unregister_slate_post_tick_callback(_capture_handle)


_capture_handle=unreal.register_slate_post_tick_callback(_capture_tick)
