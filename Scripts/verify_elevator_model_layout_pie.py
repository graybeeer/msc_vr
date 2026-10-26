"""Unsaved native model/floor-count/layout regression; no transport claim.

The saved model is inspected before an isolated Entry fixture is authored.
During PIE only public floor/layout APIs and one real door impulse are used;
no body pose, velocity, attachment or completed-state value is assigned.
"""
import json
from pathlib import Path
import time
import traceback

import unreal


level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
output = Path(unreal.Paths.project_saved_dir()) / 'Testing/TeamUIIntegration'
output.mkdir(parents=True, exist_ok=True)
keep_open = '-keeptesteditoropen' in unreal.SystemLibrary.get_command_line().lower()
started = time.monotonic()
report = dict(passed=False, checks=[], scope='Saved artwork plus unsaved native 4/3/4-floor, door interlock, relocation and delete/add regression',
              transportTested=False, osInputTested=False, savedMapWritten=False)
phase = 'start'
phase_at = 0
game = lift = layout = None
handle = None
observed_open = False
before_parts = {}
relocated = unreal.Vector(1000, 600, 0)
blocked_moves = [
    ('callstand', unreal.Vector(1000, 0, 0), unreal.Vector(-285, -205, 75), unreal.Vector(12, 12, 140)),
    ('shaft_post', unreal.Vector(0, 1000, 0), unreal.Vector(-187, 132, 200), unreal.Vector(10, 10, 120)),
    ('landing_jamb', unreal.Vector(-1000, 1000, 0), unreal.Vector(-218, 127, 135), unreal.Vector(8, 8, 150)),
]


def check(name, **details):
    report['checks'].append(dict(name=name, **details))
    print('ELEVATOR_MODEL_LAYOUT_CHECK', name, json.dumps(details, ensure_ascii=False))


def parts(actor):
    return {p.get_name(): p for p in actor.get_components_by_class(unreal.StaticMeshComponent)}


def inspect_art(actor, floor_count):
    result = parts(actor)
    assert actor.get_editor_property('use_cargo_model')
    assert result['Cargo_CabinShell'].static_mesh.get_name() == 'SM_ELV_CabinShell'
    assert result['Cargo_DeckVisual'].get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
    for floor in range(4):
        active = floor < floor_count
        for role in ('Landing', 'Bollards', 'CallStand'):
            name = f'Cargo_{role}_L{floor+1}'
            component = result[name]
            assert component.static_mesh and component.static_mesh.get_name() == f'SM_ELV_{role}_L{floor+1}', name
            assert (component.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS) == active, name
            if role == 'CallStand' and active:
                location = actor.get_actor_transform().inverse_transform_location(component.get_world_location())
                assert (location-unreal.Vector(-285, -205, floor*400)).length() < .1, (name, location)
        for side in (-1, 1):
            gate = result[f'Gate_{floor}_{side}']
            assert gate.static_mesh.get_name() == f'SM_ELV_Gate_{floor}_{side}'
            assert (gate.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS) == active
    return result


def native_rig(actor, floor_count, require_closed=True):
    result = inspect_art(actor, floor_count)
    platform = result['LiftPlatform']
    assert platform.is_simulating_physics()
    assert abs(platform.get_mass()-500) < .2, platform.get_mass()
    readouts = []
    for floor in range(4):
        for side in (-1, 1):
            gate = result[f'Gate_{floor}_{side}']
            local = actor.get_actor_transform().inverse_transform_location(gate.get_world_location())
            active = floor < floor_count
            assert gate.is_simulating_physics() == active, gate.get_name()
            assert abs(local.x+205) < .3 and abs(local.z-(floor*400+135)) < .3, (gate.get_name(), local)
            if active:
                assert abs(gate.get_mass()-80) < .2, (gate.get_name(), gate.get_mass())
                if require_closed:
                    assert abs(local.y-side*60) < .5 and gate.get_physics_linear_velocity().length() < 2, (gate.get_name(), local)
            readouts.append(dict(name=gate.get_name(), active=active, local=[local.x, local.y, local.z]))
    if require_closed:
        assert actor.are_landing_doors_closed(), 'All active landing doors should be closed and settled'
    return readouts


def finish(ok, detail):
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    if layout and layout.get_editor_property('editing'):
        layout.set_editing(False, 0)
    report.update(passed=ok, detail=detail, wall_seconds=round(time.monotonic()-started, 3))
    (output/'ElevatorLayoutReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('ELEVATOR_MODEL_LAYOUT_PIE_PASSED' if ok else 'ELEVATOR_MODEL_LAYOUT_PIE_FAILED', detail)
    if editor.get_game_world():
        level.editor_request_end_play()
    if not keep_open:
        unreal.SystemLibrary.quit_editor()


def fixture():
    assert not editor.get_game_world()
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    saved = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.WarehouseElevator)]
    assert len(saved) == 1
    saved_parts = inspect_art(saved[0], saved[0].get_editor_property('floor_count'))
    mesh_tools = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    collision_counts = {}
    for name, part in saved_parts.items():
        if name.startswith('Cargo_') and part.static_mesh and name != 'Cargo_DeckVisual':
            count = mesh_tools.get_simple_collision_count(part.static_mesh) + mesh_tools.get_convex_collision_count(part.static_mesh)
            assert count > 0, (name, 'Triangle-only artwork cannot validate native layout overlap')
            collision_counts[name] = count
    check('fixed_art_has_queryable_compound_collision', simple_shapes=collision_counts)
    assert not any(a.get_actor_label().startswith('MZ_L') and a.get_actor_label().endswith('_LiftCallStand')
                   for a in actors.get_all_level_actors()), 'Duplicate standalone call stands remain'
    properties = {key: saved[0].get_editor_property(key) for key in ('ground_opening_size_cm', 'upper_opening_size_cm', 'upper_opening_offset_cm')}
    check('saved_model_and_native_per_floor_callstands', floor_count=saved[0].get_editor_property('floor_count'))
    assert level.load_level('/Engine/Maps/Entry')
    for actor in list(actors.get_all_level_actors()):
        if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
            actors.destroy_actor(actor)
    world = editor.get_editor_world()
    unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property('default_game_mode', unreal.GameModeBase)
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-2000, -2000, 120))
    native = actors.spawn_actor_from_class(unreal.WarehouseElevator, unreal.Vector())
    native.set_actor_label('ModelLayoutLift')
    native.tags = ['LayoutType:Elevator', 'LayoutGroup:ModelLayoutLift', 'WarehouseFloor0']
    for key, value in properties.items():
        native.set_editor_property(key, value)
    assert native.set_floor_count(4)
    native.set_editor_property('use_cargo_model', True)
    native.apply_cargo_model()
    inspect_art(native, 4)
    native.set_editor_property('use_cargo_model', False)
    native.apply_cargo_model()
    fallback = parts(native)
    assert fallback['Gate_3_1'].static_mesh.get_name() == 'Cube'
    assert fallback['Column_1_1'].get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
    assert fallback['Cargo_CabinShell'].get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
    native.set_editor_property('use_cargo_model', True)
    native.apply_cargo_model()
    inspect_art(native, 4)
    check('editor_model_toggle_restores_complete_cube_and_art')
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    # The exact yaw-zero equivalent of production's shaft opening, with broad
    # continuous approach space for a real 10 m relocation on each floor.
    for floor in range(4):
        size = properties['ground_opening_size_cm'] if floor == 0 else properties['upper_opening_size_cm']
        offset = 0 if floor == 0 else properties['upper_opening_offset_cm'].y
        x0, x1, y0, y1 = offset-size.y/2, offset+size.y/2, -size.x/2, size.x/2
        rectangles = [(-2500, x0, -2500, 2500), (x1, 2500, -2500, 2500),
                      (x0, x1, -2500, y0), (x0, x1, y1, 2500)]
        for index, (left, right, bottom, top) in enumerate(rectangles):
            slab = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector((left+right)/2, (bottom+top)/2, floor*400-10))
            slab.set_actor_label(f'ModelLayoutFloor_{floor}_{index}')
            slab.static_mesh_component.set_static_mesh(cube)
            slab.static_mesh_component.set_collision_profile_name('BlockAll')
            slab.set_actor_scale3d(unreal.Vector((right-left)/100, (top-bottom)/100, .2))
            slab.tags = ['WarehouseMapStructure', 'ElevatorLayoutFloor', 'LayoutFixed']
    # Independent physical blockers touch only the fixed artwork named here,
    # outside the platform's travel prism and the convex cabin/door shapes.
    # They remain where authored; no test-time position or collision changes.
    for name, destination, offset, size in blocked_moves:
        blocker = actors.spawn_actor_from_class(unreal.StaticMeshActor, destination+offset)
        blocker.set_actor_label('ModelLayoutBlocker_'+name)
        blocker.static_mesh_component.set_static_mesh(cube)
        blocker.static_mesh_component.set_collision_profile_name('BlockAll')
        blocker.set_actor_scale3d(size/100)
        blocker.tags = ['WarehouseMapStructure', 'LayoutFixed']
    actors.spawn_actor_from_class(unreal.WarehouseLayoutEditor, unreal.Vector(-2400, -2400, 0))


def tick(dt):
    global phase, phase_at, game, lift, layout, observed_open, before_parts
    try:
        assert time.monotonic()-started < 240, ('Timeout', phase)
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'settle'
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if lift is None:
            lift = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseElevator)[0]
            layout = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseLayoutEditor)[0]
        if phase == 'settle':
            if now < 3:
                return
            check('four_floor_native_500kg_and_eight_closed_gates', doors=native_rig(lift, 4))
            parts(lift)['Gate_3_1'].add_impulse(unreal.Vector(0, 4000, 0), '', False)
            phase_at, phase = now, 'door_impulse'
            return
        if phase == 'door_impulse':
            if not lift.are_landing_doors_closed():
                observed_open = True
            if now-phase_at < 3:
                return
            assert observed_open, 'Fourth-floor physical opening was ignored by the closed-door interlock'
            native_rig(lift, 4)
            check('non_current_fourth_floor_door_interlock_tracks_actual_physics')
            before_parts = {name: p.get_world_location() for name, p in parts(lift).items() if name.startswith('Gate_')}
            assert lift.set_floor_count(3)
            assert all((parts(lift)[name].get_world_location()-point).length() < .01 for name, point in before_parts.items())
            phase_at, phase = now, 'shrink'
            return
        if phase == 'shrink':
            if now-phase_at < 2:
                return
            check('native_floor_shrink_keeps_guides_and_disables_fourth_floor', doors=native_rig(lift, 3))
            assert lift.set_floor_count(4)
            phase_at, phase = now, 'expand'
            return
        if phase == 'expand':
            if now-phase_at < 2:
                return
            check('native_floor_restore_keeps_500kg_and_eight_guides', doors=native_rig(lift, 4))
            layout.set_editing(True, 0)
            assert layout.get_editor_property('editing')
            layout.set_category(unreal.WarehouseLayoutCategory.ELEVATOR)
            layout.set_editor_property('edge_snap_enabled', False)
            layout.set_selection([lift])
            rejection_checks = []
            for name, destination, _, _ in blocked_moves:
                origin = lift.get_actor_location()
                assert layout.begin_drag(lift, origin), layout.get_editor_property('status')
                layout.update_drag(destination)
                accepted = layout.end_drag()
                status = str(layout.get_editor_property('status'))
                intended_blocker = 'ModelLayoutBlocker_'+name in status
                rejection_checks.append(dict(part=name, rejected=not accepted, intended_blocker=intended_blocker, status=status))
                if accepted:
                    # Recover through the same public layout transaction so
                    # all independent negative cases can be observed at once.
                    assert layout.begin_drag(lift, lift.get_actor_location())
                    layout.update_drag(origin)
                    assert layout.end_drag(), layout.get_editor_property('status')
                assert (lift.get_actor_location()-origin).length() < .1, name
            check('fixed_artwork_rejects_independent_physical_blockers', cases=rejection_checks)
            assert all(case['rejected'] and case['intended_blocker'] for case in rejection_checks), rejection_checks
            before_parts = {name: p.get_world_location() for name, p in parts(lift).items()}
            assert layout.begin_drag(lift, lift.get_actor_location()), layout.get_editor_property('status')
            layout.update_drag(relocated)
            assert layout.end_drag(), layout.get_editor_property('status')
            assert (lift.get_actor_location()-relocated).length() < .1
            for name, point in before_parts.items():
                assert (parts(lift)[name].get_world_location()-point-relocated).length() < .2, name
            layout.set_editing(False, 0)
            phase_at, phase = now, 'relocation_settle'
            return
        if phase == 'relocation_settle':
            if now-phase_at < 2:
                return
            native_rig(lift, 4)
            check('public_layout_relocation_moves_all_floors_and_preserves_native_rig', offset_cm=[relocated.x, relocated.y, relocated.z])
            layout.set_editing(True, 0)
            layout.set_selection([lift])
            assert layout.delete_selection(), layout.get_editor_property('status')
            assert not unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseElevator)
            layout.set_action(unreal.WarehouseLayoutAction.ADD)
            assert layout.get_add_choice_label() == '범용 엘리베이터'
            place = unreal.Vector(-1000, 0, 0)
            assert layout.begin_add(place), layout.get_editor_property('status')
            layout.update_drag(place)
            assert layout.end_drag(), layout.get_editor_property('status')
            restored = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseElevator)
            assert len(restored) == 1
            lift = restored[0]
            assert (lift.get_actor_location()-place).length() < .1
            layout.set_editing(False, 0)
            phase_at, phase = now, 'clone_settle'
            return
        if phase == 'clone_settle':
            if now-phase_at < 3:
                return
            native_rig(lift, 4)
            check('delete_add_clone_preserves_art_four_floor_callstands_and_physics')
            finish(True, 'Native model, floor resize, physical-door interlock and layout lifecycle passed; no transport or OS-input claim.')
    except Exception:
        finish(False, traceback.format_exc())


try:
    fixture()
    handle = unreal.register_slate_post_tick_callback(tick)
except Exception:
    finish(False, traceback.format_exc())
