"""Repeat walk/sprint, crouch/stand and jumps on the saved first-floor collider.

Copies only the floor to an unsaved Entry PIE. No runtime pose/velocity resets,
no floor/asset saves. Checks the physical capsule and its standing dimensions;
this is native physics/input-function testing, not an OS keyboard test.
"""
import json
import time
import traceback
from pathlib import Path
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
source = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'Floor')
floor_mesh = source.static_mesh_component.static_mesh
floor_pose = source.get_actor_transform()
floor_profile = source.static_mesh_component.get_collision_profile_name()
center, extent = source.get_actor_bounds(False)
floor_z = center.z + extent.z
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property('default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, floor_pose.translation)
floor.set_actor_label('SavedFirstFloor')
floor.static_mesh_component.set_static_mesh(floor_mesh)
floor.static_mesh_component.set_collision_profile_name(floor_profile)
floor.set_actor_transform(floor_pose, False, True)
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(center.x - 800, center.y - 800, floor_z + 125))
character_class = unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
for i in range(3):
    person = actors.spawn_actor_from_class(character_class, unreal.Vector(center.x - 600 + i * 600, center.y + 400, floor_z + 125))
    person.set_actor_label('LandingHuman_%d' % i)
    person.set_editor_property('auto_possess_player', unreal.AutoReceiveInput.DISABLED)
    person.set_editor_property('auto_possess_ai', unreal.AutoPossessAI.DISABLED)
    person.set_editor_property('ai_controller_class', None)

label = 'Baseline' if '-landingbaseline' in unreal.SystemLibrary.get_command_line().lower() else 'Fixed'
path = Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/HumanLanding_20261014')
path.mkdir(parents=True, exist_ok=True)
report = dict(label=label, floor_mesh=floor_mesh.get_path_name(), floor_profile=str(floor_profile), floor_z=floor_z, samples=[], jumps=[], anomalies=[])
started = time.monotonic()
phase = 0
game = None
people = []
previous = {}
jumped = set()


def finish(ok, detail):
    report.update(ok=ok, detail=detail, wall_seconds=time.monotonic() - started)
    (path / (label + '.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('HUMAN_LANDING_PIE_PASSED' if ok else 'HUMAN_LANDING_PIE_FAILED', detail)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    global phase, game, people
    try:
        assert time.monotonic() - started < 240, 'Human landing timeout'
        if phase == 0:
            level.editor_request_begin_play()
            phase = 1
            return
        game = editor.get_game_world()
        if not game:
            return
        now = unreal.GameplayStatics.get_time_seconds(game)
        if not people:
            people = sorted([a for a in unreal.GameplayStatics.get_all_actors_of_class(game, character_class) if a.get_actor_label().startswith('LandingHuman_')], key=lambda a: a.get_actor_label())
            assert len(people) == 3
            clocks = unreal.GameplayStatics.get_all_actors_of_class(game, unreal.WarehouseSimulationClock)
            assert clocks and clocks[0].set_speed(4)
        if now < 2:
            return
        elapsed = now - 2
        cycle = int(elapsed / 3.5)
        within = elapsed % 3.5
        if cycle >= 16:
            assert len(report['jumps']) == 48
            assert all(j['rise_cm'] > 25 for j in report['jumps']), ('Jump rejected', report['jumps'])
            assert not report['anomalies'], report['anomalies'][:8]
            finish(True, '48 native jumps; stationary/walk/sprint after crouch, including mid-air crouch/stand; saved first-floor collider; no penetration or unintended standing shrink')
            return
        for i, person in enumerate(people):
            capsule = person.get_component_by_class(unreal.CapsuleComponent)
            assert capsule.is_simulating_physics(), 'Capsule became ragdoll'
            name = person.get_actor_label()
            crouching = cycle % 2 == 0 and within < .6
            if i == 2 and cycle >= 8 and 2.0 <= within < 2.15:
                crouching = True
            person.set_locomotion_input(i == 2, crouching)
            # Return along the same corridor each cycle; no teleporting fixtures.
            if i and within > 1.3:
                person.add_movement_input(unreal.Vector(0, 1 if cycle % 2 == 0 else -1, 0), 1, True)
            half = capsule.get_unscaled_capsule_half_height()
            height = capsule.get_world_location().z
            bottom = height - ((half - capsule.get_unscaled_capsule_radius()) * abs(person.get_actor_up_vector().z) + capsule.get_unscaled_capsule_radius())
            state = dict(time=now, human=name, cycle=cycle, within=within, crouching=crouching, half_cm=half, center_z=height, bottom_z=bottom, velocity_z=person.get_velocity().z, up_z=person.get_actor_up_vector().z)
            report['samples'].append(state)
            prev = previous.get(name)
            if bottom < floor_z - 3:
                report['anomalies'].append(dict(kind='floor_penetration', **state))
            if prev and not crouching and not prev['crouching'] and within > 1.2 and half < prev['half_cm'] - .15:
                report['anomalies'].append(dict(kind='unintended_standing_shrink', previous_half=prev['half_cm'], **state))
            key = (name, cycle)
            if within >= 1.8 and key not in jumped:
                jumped.add(key)
                person.do_jump_start()
                report['jumps'].append(dict(human=name, cycle=cycle, start_z=height, half_cm=half, rise_cm=0))
            if key in jumped:
                entry = next(j for j in reversed(report['jumps']) if j['human'] == name and j['cycle'] == cycle)
                entry['rise_cm'] = max(entry['rise_cm'], height - entry['start_z'])
            previous[name] = state
    except Exception:
        finish(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
