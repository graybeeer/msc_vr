"""Add four reinforced 1,000 kg exception cartons without rebuilding other stock.

The 3,000 kg stacking capacity is an explicit reinforced test-cargo assumption,
not a certified cardboard rating. Fab texture derivatives remain in its ignored
local asset folder. The caller owns loading/saving the level.
"""
import json
import sys
import time
import traceback
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from apply_real_world_scale import fit

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
PREFIX = 'WH_HeavyTestCargo_'
MESH = '/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2'
MATERIAL_DIR = '/Game/Warehouse/Cargo/BoxesPalletsPack/HeavyTest/Materials'
SIZE = (100., 100., 90.)
COLUMNS = ((2460., 1820.), (2590., 1820.))
KINDS = ('보강 시험화물 / 금속 베어링', '보강 시험화물 / 강철 기어',
         '보강 시험화물 / 금속 금형', '보강 시험화물 / 구리 케이블')
COLORS = ((.22, .50, .95, 1.), (.20, .78, .40, 1.),
          (.95, .28, .20, 1.), (.68, .25, .88, 1.))
TINT_PARAMETER = 'HeavyTestTint'
_NATIVE_CORNER_PROOF = None


def _floor_snapshot(named):
    return {name: (named[name].get_actor_location(), *named[name].get_actor_bounds(False))
            for name in ('Floor', 'WH_InboundFloor') if name in named}


def _assert_same_floors(before, named):
    after = _floor_snapshot(named)
    assert before.keys() == after.keys(), 'Warehouse floors changed during native corner preflight'
    for name in before:
        assert all((old - new).length() < .01 for old, new in zip(before[name], after[name])), (
            'Warehouse floor transform/bounds changed during preflight', name)


def _hits(value):
    if isinstance(value, unreal.HitResult):
        # HasNativeBreak is exposed through the struct wrapper in UE Python.
        yield value.to_tuple()
    elif isinstance(value, (list, tuple, unreal.Array)):
        for part in value:
            yield from _hits(part)


def assert_clear_corner(world, named):
    """Native collision queries reject occupied footprints or missing floor support."""
    ignored = [a for name, a in named.items() if name.startswith(PREFIX)]
    floors = [named[n] for n in ('Floor', 'WH_InboundFloor') if n in named]
    assert floors, 'Warehouse floor is missing'
    for floor in floors:
        part = floor.get_component_by_class(unreal.StaticMeshComponent)
        print('HEAVY_NATIVE_FLOOR_DIAGNOSTIC', floor.get_actor_label(),
            'location', floor.get_actor_location(), 'render bounds', floor.get_actor_bounds(False),
            'actor collision', floor.get_actor_enable_collision(), 'profile', part.get_collision_profile_name(),
            'enabled', part.get_collision_enabled(),
            'visibility', part.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY),
            'native body bounds', unreal.WarehouseDamageSystem.get_physical_bounds(floor))
    for x, y in COLUMNS:
        center = unreal.Vector(x, y, 90.25)
        result = unreal.SystemLibrary.box_trace_multi(world, center, center + unreal.Vector(0, 0, .01),
            unreal.Vector(51, 51, 90), unreal.Rotator(), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
            False, ignored + floors, unreal.DrawDebugTrace.NONE, False)
        blocking = [hit[9].get_actor_label() for hit in _hits(result) if hit[0] and hit[9]]
        assert not blocking, ('Heavy test corner is occupied; refusing to save', (x, y), blocking)
        for dx in (-40., 40.):
            for dy in (-40., 40.):
                top = unreal.Vector(x + dx, y + dy, 3)
                result = unreal.SystemLibrary.line_trace_single(world, top, top - unreal.Vector(0, 0, 23),
                    unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ignored, unreal.DrawDebugTrace.NONE, False)
                support = list(_hits(result))
                if not any(hit[0] and hit[9] in floors and abs(hit[5].z) < .5 and hit[7].z > .98 for hit in support):
                    print('HEAVY_NATIVE_SUPPORT_QUERY_DIAGNOSTIC', 'point', top,
                        'result type', type(result), 'raw', result,
                        'broken hits', [(hit[0], hit[1], hit[4], hit[5], hit[6], hit[7],
                                         hit[9].get_actor_label() if hit[9] else None) for hit in support])
                assert any(hit[0] and hit[9] in floors and abs(hit[5].z) < .5 and hit[7].z > .98 for hit in support), (
                    'No flat native floor under heavy test cargo; refusing to save', x + dx, y + dy)


def _tinted_material(source, index):
    assets = unreal.EditorAssetLibrary
    editing = unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    existing = next((name for name in editing.get_vector_parameter_names(source)
                     if str(name).lower() in ('heavytint', 'heavytesttint', 'tint', 'basecolortint', 'colortint')), None)
    parent = source
    if not existing:
        base = source
        while isinstance(base, unreal.MaterialInstanceConstant):
            base = base.get_editor_property('parent')
        assert isinstance(base, unreal.Material), source.get_path_name()
        path = MATERIAL_DIR + '/M_HeavyTint_' + base.get_name()
        if not assets.does_asset_exist(path):
            assert assets.duplicate_asset(base.get_path_name(), path)
        parent = unreal.load_asset(path)
        parameter = next((node for node in editing.get_material_expressions(parent)
                          if isinstance(node, unreal.MaterialExpressionVectorParameter)
                          and str(node.get_editor_property('parameter_name')) == TINT_PARAMETER), None)
        if not parameter:
            assert not parent.get_editor_property('use_material_attributes'), 'Intact carton must expose its textured base color'
            albedo = editing.get_material_property_input_node(parent, unreal.MaterialProperty.MP_BASE_COLOR)
            output = editing.get_material_property_input_node_output_name(parent, unreal.MaterialProperty.MP_BASE_COLOR)
            assert albedo, 'Cannot preserve the carton albedo texture'
            parameter = editing.create_material_expression(parent, unreal.MaterialExpressionVectorParameter)
            parameter.set_editor_property('parameter_name', TINT_PARAMETER)
            parameter.set_editor_property('default_value', unreal.LinearColor(1, 1, 1, 1))
            multiply = editing.create_material_expression(parent, unreal.MaterialExpressionMultiply)
            assert editing.connect_material_expressions(albedo, output, multiply, 'A')
            assert editing.connect_material_expressions(parameter, '', multiply, 'B')
            assert editing.connect_material_property(multiply, '', unreal.MaterialProperty.MP_BASE_COLOR)
            # Preserve the current opaque surface bridge when Substrate has one.
            front = editing.get_material_property_input_node(parent, unreal.MaterialProperty.MP_FRONT_MATERIAL)
            if front:
                inputs = {name.replace(' ', '').lower(): name for name in editing.get_material_expression_input_names(front)}
                if 'basecolor' in inputs:
                    assert editing.connect_material_expressions(multiply, '', front, inputs['basecolor'])
            editing.recompile_material(parent)
            assert assets.save_loaded_asset(parent)
        existing = unreal.Name(TINT_PARAMETER)
    target = MATERIAL_DIR + '/MI_HeavyTest_%02d_%s' % (index + 1, source.get_name())
    if not assets.does_asset_exist(target):
        if isinstance(source, unreal.MaterialInstanceConstant):
            assert assets.duplicate_asset(source.get_path_name(), target)
        else:
            assert tools.create_asset(target.rsplit('/', 1)[1], MATERIAL_DIR,
                unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    instance = unreal.load_asset(target)
    editing.set_material_instance_parent(instance, parent)
    # UE 5.8's setter returns false even after applying the override; verify the value.
    editing.set_material_instance_vector_parameter_value(instance, existing, unreal.LinearColor(*COLORS[index]))
    actual = editing.get_material_instance_vector_parameter_value(instance, existing)
    assert max(abs(a - b) for a, b in zip((actual.r, actual.g, actual.b, actual.a), COLORS[index])) < .001, 'Heavy cargo tint override did not apply'
    assert assets.save_loaded_asset(instance)
    return instance, str(existing)


def configure_heavy_test_cargo(_native_corner_proof=None):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    # Editor map loads have no live Chaos bodies in this project. Only accept a
    # proof produced by the actual PIE preflight below, with unchanged source floors.
    assert _native_corner_proof is not None and _native_corner_proof is _NATIVE_CORNER_PROOF, 'Run the native PIE corner preflight before configuring heavy cargo'
    assert _native_corner_proof[0] == world, 'Native corner proof belongs to another editor world'
    _assert_same_floors(_native_corner_proof[1], named)
    system = named['WH_DamageSystem']
    original = list(system.get_editor_property('objects'))
    preserved = [spec for spec in original if not spec.name.startswith(PREFIX)]
    mesh = unreal.load_asset(MESH)
    assert mesh, 'Install the intact Boxes & Pallets Pack before adding heavy test cargo'
    specs = []
    manifest = []
    for index in range(4):
        column, tier = divmod(index, 2)
        x, y = COLUMNS[column]
        bottom = .15 + tier * 90.15
        label = PREFIX + '%02d' % (index + 1)
        actor = named.get(label) or actors.spawn_actor_from_class(unreal.WarehouseCargo, unreal.Vector(x, y, bottom))
        assert isinstance(actor, unreal.WarehouseCargo), label
        actor.modify()
        actor.set_actor_label(label)
        actor.set_folder_path('Warehouse/ExceptionCargo')
        actor.set_editor_property('is_spatially_loaded', False)
        actor.tags = list(dict.fromkeys([str(tag) for tag in actor.tags] + ['ExceptionCargo', 'WarehouseFloor0', 'LayoutType:Other']))
        actor.set_actor_rotation(unreal.Rotator(), False)
        actor.set_cargo_mesh(mesh)
        actor.set_editor_property('cargo_id', unreal.Name('HEAVY-TEST-%02d' % (index + 1)))
        recipe = unreal.WarehouseCargoRecipe(valid=True, rule_version=2, delivery_seed=20261013,
            item_index=index, product_index=8 if index == 3 else 7,
            size_limit_cm=unreal.Vector(*SIZE), amount_scale=1, product_kind=KINDS[index],
            size_cm=unreal.Vector(*SIZE), packed_density_kg_m3=980 / .9,
            net_mass_kg=980, packaging_mass_kg=20, gross_mass_kg=1000, fragile=False)
        assert actor.apply_cargo_recipe(recipe), label
        fit(actor, SIZE, (x, y, bottom + SIZE[2] / 2))
        actor.set_gross_mass_kg(1000)
        part = actor.get_component_by_class(unreal.StaticMeshComponent)
        part.set_editor_property('override_materials', [])
        params = []
        for slot in range(part.get_num_materials()):
            material, parameter = _tinted_material(mesh.get_material(slot), index)
            part.set_material(slot, material)
            params.append(parameter)
        sign_label = label + '_Label'
        sign = named.get(sign_label) or actors.spawn_actor_from_class(unreal.TextRenderActor,
            unreal.Vector(x, y - 50.8, bottom + 45), unreal.Rotator(yaw=-90))
        sign.modify()
        sign.set_actor_label(sign_label)
        sign.set_folder_path('Warehouse/ExceptionCargo')
        sign.set_editor_property('is_spatially_loaded', False)
        sign.set_actor_enable_collision(False)
        text = sign.get_component_by_class(unreal.TextRenderComponent)
        text.set_mobility(unreal.ComponentMobility.MOVABLE)
        text.set_text('1000 kg')
        text.set_world_size(13)
        text.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
        text.set_text_render_color(unreal.Color(255, 245, 220, 255))
        sign.set_actor_location(unreal.Vector(x, y - 50.8, bottom + 45), False, True)
        sign.set_actor_rotation(unreal.Rotator(yaw=-90), False)
        sign.attach_to_actor(actor, unreal.Name('None'), unreal.AttachmentRule.KEEP_WORLD,
            unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
        specs.append(unreal.WarehouseStrength(name=label, members=[actor], physics_meshes=[mesh],
            failure=unreal.WarehouseFailure.CRUSH, mass_kg=1000, rated_load_kg=3000,
            impact_yield_j=5000, impact_failure_j=20000, overload_seconds=10))
        manifest.append({'actor': label, 'id': str(actor.get_editor_property('cargo_id')), 'kind': KINDS[index],
            'gross_kg': 1000, 'net_kg': 980, 'packaging_kg': 20, 'size_cm': list(SIZE),
            'column': column, 'tier': tier, 'tint': COLORS[index], 'tint_parameters': params,
            'materials': [part.get_material(slot).get_path_name() for slot in range(part.get_num_materials())],
            'stacking_capacity_kg': 3000})
    system.modify()
    system.set_editor_property('objects', preserved + specs)
    assert len(system.get_editor_property('objects')) == len(preserved) + 4
    output = Path(unreal.Paths.project_saved_dir()) / 'Testing' / 'HeavyTestCargoManifest.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print('HEAVY_TEST_CARGO_CONFIGURED', 'four textured 1000 kg reinforced cartons; two columns/two tiers; unrelated profiles preserved')
    return manifest


if __name__ == '__main__':
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    source_world = editor.get_editor_world()
    source_floors = _floor_snapshot({a.get_actor_label(): a for a in actors.get_all_level_actors()})
    unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
    started = time.monotonic()
    phase = 0

    def preflight(dt):
        global phase, _NATIVE_CORNER_PROOF
        if phase == 3:
            return
        try:
            assert time.monotonic() - started < 240, 'Heavy cargo native preflight timeout'
            if phase == 0:
                if time.monotonic() - started < .5:
                    return
                level.editor_request_begin_play()
                phase = 1
                return
            game = editor.get_game_world()
            if phase == 1:
                if not game or unreal.GameplayStatics.get_time_seconds(game) < .25:
                    return
                live = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(game, unreal.Actor)}
                _assert_same_floors(source_floors, live)
                assert all(unreal.WarehouseDamageSystem.get_physical_bounds(live[name]).is_valid for name in source_floors), 'Native warehouse floor bodies were not installed'
                assert_clear_corner(game, live)
                _NATIVE_CORNER_PROOF = (source_world, source_floors)
                print('HEAVY_NATIVE_CORNER_PREFLIGHT_PASSED', 'actual PIE collision scene; unchanged source floor transforms/bounds')
                level.editor_request_end_play()
                phase = 2
                return
            if game:
                return
            # Material recompilation refreshes Slate. Author once after removing
            # this callback so a reentrant refresh cannot repeat material/map writes.
            phase = 3
            unreal.unregister_slate_post_tick_callback(preflight_handle)
            configure_heavy_test_cargo(_NATIVE_CORNER_PROOF)
            assert level.save_current_level()
            assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
            print('HEAVY_TEST_CARGO_SAVED')
            unreal.SystemLibrary.quit_editor()
        except Exception:
            unreal.log_error(traceback.format_exc())
            if phase != 3:
                unreal.unregister_slate_post_tick_callback(preflight_handle)
            level.editor_request_end_play()
            unreal.SystemLibrary.quit_editor()

    preflight_handle = unreal.register_slate_post_tick_callback(preflight)
