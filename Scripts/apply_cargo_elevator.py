"""Called through the project's official MCP toolset; imports and applies the cargo lift."""
import json,re,hashlib
from pathlib import Path
import unreal


def fit_cargo_elevator_roof_clearance(lift, actor_subsystem):
    """Fit the demonstrated shaft/roof-beam conflict without changing either solid."""
    named = {a.get_actor_label(): a for a in actor_subsystem.get_all_level_actors()}
    beam = named.get('WH_RoofBeam_1500')
    roof = named.get('WH_Roof')
    assert isinstance(beam, unreal.StaticMeshActor) and isinstance(roof, unreal.StaticMeshActor)
    frame = next(c for c in lift.get_components_by_class(unreal.StaticMeshComponent)
                 if c.get_name() == 'Cargo_ShaftFrame')
    assert frame.static_mesh and frame.static_mesh.get_name() == 'SM_ELV_ShaftFrame'

    def values(vector):
        return [vector.x, vector.y, vector.z]

    def pose(actor):
        rotation = actor.get_actor_rotation()
        return dict(location_cm=values(actor.get_actor_location()), scale=values(actor.get_actor_scale3d()),
                    rotation_deg=[rotation.pitch, rotation.yaw, rotation.roll])

    frame_center, frame_half = unreal.SystemLibrary.get_component_bounds(frame)[:2]
    beam_center, beam_half = beam.get_actor_bounds(False)
    roof_center, roof_half = roof.get_actor_bounds(False)
    frame_top = frame_center.z+frame_half.z
    roof_bottom = roof_center.z-roof_half.z
    beam_bottom, beam_top = beam_center.z-beam_half.z, beam_center.z+beam_half.z
    before = pose(beam)
    result = dict(actor=beam.get_actor_label(), before=before, after=before, delta_z_cm=0.,
                  frame_top_cm=frame_top, roof_underside_cm=roof_bottom, beam_bottom_cm=beam_bottom,
                  beam_top_cm=beam_top, status='already_clear')
    # Only this beam has a demonstrated solid intersection. Do not move other
    # ceiling equipment from broad mesh bounds or from an unrelated overlap.
    same_xy = (abs(frame_center.x-beam_center.x) < frame_half.x+beam_half.x and
               abs(frame_center.y-beam_center.y) < frame_half.y+beam_half.y)
    if not same_xy or beam_bottom >= frame_top+.05:
        return [result]
    assert abs(frame.get_up_vector().z-1.) < .001 and abs(beam.static_mesh_component.get_up_vector().z-1.) < .001
    assert abs(roof.static_mesh_component.get_up_vector().z-1.) < .001
    delta = roof_bottom-beam_top
    # The saved 3-floor warehouse has 15cm above this 30cm beam. Raising it
    # that far yields 5cm above the model's top brace and touches the roof.
    assert .05 < delta <= 15.2, ('Unexpected roof allowance; no geometry changed', result, delta)
    assert beam_bottom+delta-frame_top >= 4.9, ('Insufficient shaft headroom; no geometry changed', result)
    original_location = beam.get_actor_location()
    beam.modify()
    beam.set_actor_location(original_location+unreal.Vector(0, 0, delta), False, False)
    after_center, after_half = beam.get_actor_bounds(False)
    assert (after_half-beam_half).length() < .001 and abs(after_center.x-beam_center.x) < .001 and abs(after_center.y-beam_center.y) < .001
    assert abs(after_center.z+after_half.z-roof_bottom) < .01
    result.update(after=pose(beam), delta_z_cm=delta, beam_bottom_cm=after_center.z-after_half.z,
                  beam_top_cm=after_center.z+after_half.z, status='raised_to_roof_underside')
    return [result]


def fit_cargo_elevator_north_guard_clearance(lift, actor_subsystem):
    """Keep the complete north guardrails outside the authored shaft, on supported floor."""
    named = {a.get_actor_label(): a for a in actor_subsystem.get_all_level_actors()}
    frame = next(c for c in lift.get_components_by_class(unreal.StaticMeshComponent)
                 if c.get_name() == 'Cargo_ShaftFrame')
    assert frame.static_mesh and frame.static_mesh.get_name() == 'SM_ELV_ShaftFrame'
    centre, half = unreal.SystemLibrary.get_component_bounds(frame)[:2]
    frame_north = centre.y+half.y
    assert abs(frame_north-1804.) < .1, ('Unexpected shaft placement; no rails changed', frame_north)
    rail_y = frame_north+7.5
    strip_half_y = (rail_y+2.5-1800.)*.5
    strip_y = 1800.+strip_half_y

    def values(vector):
        return [vector.x, vector.y, vector.z]

    def pose(actor):
        rotation = actor.get_actor_rotation()
        return dict(location_cm=values(actor.get_actor_location()), scale=values(actor.get_actor_scale3d()),
                    rotation_deg=[rotation.pitch, rotation.yaw, rotation.roll])

    plans = []
    for floor in (1, 2):
        prefix = f'MZ_L{floor+1}_OuterNorth_'
        expected = {prefix+f'Bar{height}' for height in (8, 55, 110)}
        expected.update(prefix+f'Post{i}' for i in range(21))
        found = {label for label in named if label.startswith(prefix)}
        assert found == expected, ('Incomplete north guardrail; no rails changed', prefix, sorted(found ^ expected))
        source = named.get(f'MZ_L{floor+1}_Deck_North')
        assert isinstance(source, unreal.StaticMeshActor)
        source_component = source.static_mesh_component
        source_centre, source_half = source.get_actor_bounds(False)
        assert abs(source_centre.z+source_half.z-floor*400.) < .1 and abs(source_half.z-10.) < .1
        assert abs(source_centre.y+source_half.y-1800.) < .1
        source_mesh = source_component.static_mesh
        assert source_mesh and source_mesh.get_bounds().origin.length() < .001
        mesh_half = source_mesh.get_bounds().box_extent
        assert min(mesh_half.x, mesh_half.y, mesh_half.z) > 0
        assert source_component.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
        rails = []
        for label in sorted(expected):
            rail = named[label]
            assert isinstance(rail, unreal.StaticMeshActor)
            location = rail.get_actor_location()
            rail_centre, rail_half = rail.get_actor_bounds(False)
            assert min(abs(location.y-1800.), abs(location.y-rail_y)) < .01
            assert abs(rail_centre.y-location.y) < .01 and rail_half.y <= 2.51
            assert rail_y-rail_half.y-frame_north >= 4.9
            rails.append((rail, pose(rail)))
        label = f'MZ_L{floor+1}_Deck_NorthClearance'
        strip = named.get(label)
        if strip:
            assert isinstance(strip, unreal.StaticMeshActor) and strip.static_mesh_component.static_mesh == source_mesh
            strip_centre, strip_half = strip.get_actor_bounds(False)
            assert (strip_centre-unreal.Vector(0, strip_y, floor*400-10)).length() < .01
            assert (strip_half-unreal.Vector(1800, strip_half_y, 10)).length() < .01
        tags = {str(tag) for tag in source.tags if not str(tag).startswith('ElevatorFloorSource:')}
        tags.update((f'WarehouseFloor{floor}', 'ElevatorLayoutFloor', f'ElevatorFloorSource:{label}', 'WarehouseMapStructure', 'LayoutFixed'))
        plans.append((floor, rails, source, strip, label, sorted(tags)))

    # Validate both floors before changing any existing actor. Deterministic
    # targets make a second application a no-op, rather than another offset.
    result = dict(frame_north_cm=frame_north, rail_y_cm=rail_y, rail_changes=[], floor_strips=[])
    for floor, rails, source, strip, label, tags in plans:
        for rail, before in rails:
            location = rail.get_actor_location()
            changed = abs(location.y-rail_y) > .001
            if changed:
                rail.modify()
                rail.set_actor_location(unreal.Vector(location.x, rail_y, location.z), False, False)
            after = pose(rail)
            assert before['scale'] == after['scale'] and before['rotation_deg'] == after['rotation_deg']
            assert before['location_cm'][0] == after['location_cm'][0] and before['location_cm'][2] == after['location_cm'][2]
            result['rail_changes'].append(dict(actor=rail.get_actor_label(), before=before, after=after, changed=changed))
        created = strip is None
        if created:
            source_component = source.static_mesh_component
            strip = actor_subsystem.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, strip_y, floor*400-10))
            assert strip
            strip.set_actor_label(label)
            strip.set_folder_path(source.get_folder_path())
            strip.set_editor_property('is_spatially_loaded', False)
            component = strip.static_mesh_component
            component.set_static_mesh(source_component.static_mesh)
            for slot in range(source_component.get_num_materials()):
                component.set_material(slot, source_component.get_material(slot))
            component.set_mobility(unreal.ComponentMobility.STATIC)
            component.set_collision_profile_name(source_component.get_collision_profile_name())
            component.set_collision_enabled(source_component.get_collision_enabled())
            component.set_phys_material_override(source_component.get_editor_property('body_instance').get_editor_property('phys_material_override'))
            mesh_half = source_component.static_mesh.get_bounds().box_extent
            strip.set_actor_scale3d(unreal.Vector(1800/mesh_half.x, strip_half_y/mesh_half.y, 10/mesh_half.z))
        if sorted(str(tag) for tag in strip.tags) != tags:
            strip.modify()
            strip.tags = [unreal.Name(tag) for tag in tags]
        strip_centre, strip_half = strip.get_actor_bounds(False)
        assert (strip_centre-unreal.Vector(0, strip_y, floor*400-10)).length() < .01
        assert (strip_half-unreal.Vector(1800, strip_half_y, 10)).length() < .01
        result['floor_strips'].append(dict(actor=label, created=created, centre_cm=values(strip_centre),
                                          dimensions_cm=values(strip_half*2), tags=tags))
    return result


ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'Saved/Testing/CargoElevator';OUT.mkdir(parents=True,exist_ok=True)
BASE='/Game/Warehouse/Elevator'
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets=unreal.EditorAssetLibrary
assert not editor.get_game_world()
assert editor.get_editor_world().get_path_name().startswith('/Game/FirstPerson/Lvl_FirstPerson')
manifest=json.loads((ROOT/'Art/Equipment/CargoElevator/export_manifest.json').read_text(encoding='utf-8'))
tools=unreal.AssetToolsHelpers.get_asset_tools()
materials={}
for name,values in manifest['materials'].items():
    safe='M_'+re.sub('[^A-Za-z0-9_]','_',name)
    path=BASE+'/Materials/'+safe
    mat=unreal.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(safe,BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
    for i,(key,prop,vector) in enumerate([('base_color',unreal.MaterialProperty.MP_BASE_COLOR,True),('metallic',unreal.MaterialProperty.MP_METALLIC,False),('roughness',unreal.MaterialProperty.MP_ROUGHNESS,False)]):
        node=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector if vector else unreal.MaterialExpressionConstant,-250,i*130)
        value=values[key]
        node.set_editor_property('constant' if vector else 'r',unreal.LinearColor(*value) if vector else value)
        unreal.MaterialEditingLibrary.connect_material_property(node,'',prop)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    assets.save_loaded_asset(mat)
    materials[re.sub('[^a-z0-9]','',name.lower())]=mat
meshes={}
for part in manifest['parts']:
    name=part['name'];path=BASE+'/Meshes/'+name
    source_file=ROOT/'Art/Equipment/CargoElevator'/part['file']
    source_hash=hashlib.sha256(source_file.read_bytes()).hexdigest()
    mesh=unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if not mesh or assets.get_metadata_tag(mesh,'ElevatorSourceHash')!=source_hash:
        options=unreal.FbxImportUI()
        for k,v in dict(import_mesh=True,import_as_skeletal=False,import_materials=False,import_textures=False,import_animations=False,automated_import_should_detect_type=False,mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():options.set_editor_property(k,v)
        for k,v in dict(combine_meshes=True,auto_generate_collision=False,transform_vertex_to_absolute=True,convert_scene=True,convert_scene_unit=True,import_rotation=unreal.Rotator()).items():options.static_mesh_import_data.set_editor_property(k,v)
        task=unreal.AssetImportTask()
        for k,v in dict(filename=str(source_file),destination_path=BASE+'/Meshes',destination_name=name,automated=True,replace_existing=True,save=True,options=options,factory=unreal.FbxFactory()).items():task.set_editor_property(k,v)
        tools.import_asset_tasks([task]);mesh=unreal.load_asset(path)
    assert isinstance(mesh,unreal.StaticMesh),name
    extent=mesh.get_bounds().box_extent
    dimensions=[extent.x*2,extent.y*2,extent.z*2]
    assert max(abs(a-b) for a,b in zip(dimensions,part['dimensions_cm']))<.2,(name,dimensions,part['dimensions_cm'])
    for i,slot in enumerate(mesh.static_materials):
        normalized=re.sub('[^a-z0-9]','',str(slot.material_slot_name).lower())
        assert normalized in materials,(name,normalized)
        mesh.set_material(i,materials[normalized])
    unreal.WarehouseElevator.configure_model_collision(mesh,name.removeprefix('SM_ELV_'))
    assets.set_metadata_tag(mesh,'ElevatorSourceHash',source_hash)
    assets.save_loaded_asset(mesh)
    meshes[name]=mesh
lifts=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseElevator)]
assert len(lifts)==1
lift=lifts[0];before=lift.get_actor_transform()
lift.modify();lift.set_editor_property('use_cargo_model',True);lift.apply_cargo_model()
roof_clearance=fit_cargo_elevator_roof_clearance(lift,actors)
north_rail_clearance=fit_cargo_elevator_north_guard_clearance(lift,actors)
# Call stands are native per-floor components so resize/relocation/delete share
# the complete lift group. Retire only the previous standalone implementation.
stands=[]
for actor in list(actors.get_all_level_actors()):
    if re.fullmatch(r'MZ_L[1-4]_LiftCallStand',actor.get_actor_label()):
        actors.destroy_actor(actor)
for floor in range(lift.get_editor_property('floor_count')):
    part=next(c for c in lift.get_components_by_class(unreal.StaticMeshComponent)
              if c.get_name()==f'Cargo_CallStand_L{floor+1}')
    assert part.static_mesh and part.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION
    stands.append({'component':part.get_name(),'position':str(part.get_world_location()),'collision':'QueryAndPhysics'})
# A 1 cm seam can generate speculative corner contacts on descent, then release
# stored guide force. Trim only the three approach slabs and ground rear edge;
# retain their material, thickness and all surrounding supported floor.
trimmed=[]
for actor in actors.get_all_level_actors():
    label=actor.get_actor_label()
    if label not in ('Floor','MZ_L2_Deck_Right','MZ_L3_Deck_Right','MZ_GroundRear'):continue
    centre,extent=actor.get_actor_bounds(False)
    low,high=centre.y-extent.y,centre.y+extent.y
    if label=='MZ_GroundRear':
        assert abs(low-1776)<.2 or abs(low-1781)<.2,(label,low)
        newlow,newhigh=1781,high
    else:
        assert abs(high-1424)<.2 or abs(high-1419)<.2,(label,high)
        newlow,newhigh=low,1419
    actor.modify()
    scale=actor.get_actor_scale3d();scale.y*=(newhigh-newlow)/(high-low)
    actor.set_actor_scale3d(scale)
    position=actor.get_actor_location();position.y+=(newlow+newhigh)/2-centre.y
    actor.set_actor_location(position,False,False)
    trimmed.append(label)
assert len(trimmed)==4,trimmed
lift.set_editor_property('ground_opening_size_cm',unreal.Vector2D(282,362))
lift.set_editor_property('upper_opening_size_cm',unreal.Vector2D(282,381))
lift.set_editor_property('upper_opening_offset_cm',unreal.Vector2D(0,9.5))
# The new face already contains floor/load labels; retire only the old lift signs.
retired=[]
for actor in actors.get_all_level_actors():
    if actor.get_actor_label() in ('MZ_L1_LiftSign','MZ_L2_LiftSign','MZ_L3_LiftSign'):
        actor.modify();actor.set_actor_hidden_in_game(True)
        for c in actor.get_components_by_class(unreal.TextRenderComponent):
            c.modify();c.set_visibility(False);c.set_hidden_in_game(True)
        retired.append(actor.get_actor_label())
assert unreal.MathLibrary.nearly_equal_transform_transform(before,lift.get_actor_transform(),.001,.001,.001)
assert level.save_current_level()
assets.save_directory(BASE,False,True)
report={'date':'2026-10-22','map_saved':True,'lift':lift.get_actor_label(),'meshes':list(meshes),'retired_signs':retired,'call_stands':stands,
    'trimmed_slabs':trimmed,'ground_opening_cm':[282,362],'upper_opening_cm':[282,381],
    'upper_opening_offset_cm':[0,9.5],'roof_clearance':roof_clearance,'north_rail_clearance':north_rail_clearance,'physics_test_complete':False}
(OUT/'ApplyReport.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
eye=lift.get_actor_transform().transform_location(unreal.Vector(-720,-420,230))
target=lift.get_actor_transform().transform_location(unreal.Vector(-120,0,150))
editor.set_level_viewport_camera_info(eye,unreal.MathLibrary.find_look_at_rotation(eye,target))
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
print('CARGO_ELEVATOR_APPLIED',json.dumps(report))
# Runtime inspection and captures are performed separately; no temporary camera is left in the saved map.
