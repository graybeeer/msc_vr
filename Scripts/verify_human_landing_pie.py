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

RENDER_CAPTURE='-nullrhi' not in unreal.SystemLibrary.get_command_line().lower()
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground', False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
original = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'Floor')
original_center,original_extent=original.get_actor_bounds(False)
floor_z=original_center.z+original_extent.z
# A shaft installation splits the old Floor actor into several real slabs.
# The label alone no longer guarantees that x +/- 600 cm has any floor.
slabs=[]
for candidate in actors.get_all_level_actors():
    if not isinstance(candidate,unreal.StaticMeshActor) or not candidate.get_actor_enable_collision():continue
    name=candidate.get_actor_label();tags={str(t) for t in candidate.tags}
    if not (name in ('Floor','WH_InboundFloor','MZ_GroundRearWest','MZ_GroundRearEast','MZ_GroundRear') or 'ElevatorLayoutFloor' in tags):continue
    c,e=candidate.get_actor_bounds(False)
    if abs(c.z+e.z-floor_z)>2 or e.z>50 or min(e.x,e.y)<160 or max(e.x,e.y)<1100:continue
    slabs.append((e.x*e.y,candidate))
assert slabs,'No saved first-floor slab has enough supported space for the gait/recovery fixture'
source=max(slabs,key=lambda pair:pair[0])[1]
source_label=source.get_actor_label()
floor_mesh = source.static_mesh_component.static_mesh
floor_pose = source.get_actor_transform()
floor_profile = source.static_mesh_component.get_collision_profile_name()
center, extent = source.get_actor_bounds(False)
floor_z = center.z + extent.z
travel=unreal.Vector(1,0,0) if extent.x>=extent.y else unreal.Vector(0,1,0)
lateral=unreal.Vector(-travel.y,travel.x,0)
# UE Python unary vector negation can return/self-copy the same wrapper.
# Keep this continuous command immutable instead of negating travel each frame.
recovery_direction=unreal.Vector(-travel.x,-travel.y,0)
spawn_center=unreal.Vector(center.x,center.y,floor_z+125)
human_starts=[spawn_center-travel*600+lateral*((i-1)*100) for i in range(3)]
operator_start=spawn_center+travel*850
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
world = editor.get_editor_world()
if RENDER_CAPTURE:
    actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(center.x,center.y,floor_z+600),unreal.Rotator(pitch=-45,yaw=30))
    actors.spawn_actor_from_class(unreal.SkyAtmosphere,unreal.Vector())
    fill=actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(center.x,center.y,floor_z+600))
    fill.get_component_by_class(unreal.SkyLightComponent).set_intensity(1.5)
unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)[0].set_editor_property('default_game_mode', unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, floor_pose.translation)
floor.set_actor_label('SavedFirstFloor')
floor.static_mesh_component.set_static_mesh(floor_mesh)
floor.static_mesh_component.set_collision_profile_name(floor_profile)
floor.set_actor_transform(floor_pose, False, True)
# Probe the actual copied collider at every spawn and both gait endpoints before PIE.
support_probes=[operator_start]+human_starts+[p+travel*1000 for p in human_starts]
for position in support_probes:
    hit=unreal.SystemLibrary.line_trace_single(world,position+unreal.Vector(0,0,50),position-unreal.Vector(0,0,160),unreal.TraceTypeQuery.ECC_VISIBILITY,False,[],unreal.DrawDebugTrace.NONE,True).to_tuple()
    assert hit[0] and hit[9]==floor,('Fixture has no saved floor support',position,source_label)
actors.spawn_actor_from_class(unreal.PlayerStart, operator_start)
character_class = unreal.load_class(None, '/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C')
recovery_roof=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(center.x+10000,center.y+10000,floor_z+150))
recovery_roof.set_actor_label('RecoveryLowCeiling')
recovery_roof.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
recovery_roof.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
recovery_roof.static_mesh_component.set_collision_profile_name('BlockAll');recovery_roof.set_actor_scale3d(unreal.Vector(6,6,.2))
for i in range(3):
    person = actors.spawn_actor_from_class(character_class, human_starts[i])
    person.set_actor_label('LandingHuman_%d' % i)
    person.set_editor_property('auto_possess_player', unreal.AutoReceiveInput.DISABLED)
    person.set_editor_property('auto_possess_ai', unreal.AutoPossessAI.DISABLED)
    person.set_editor_property('ai_controller_class', None)

label = 'Baseline' if '-landingbaseline' in unreal.SystemLibrary.get_command_line().lower() else 'Fixed'
path = Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/HumanLanding_20261027')
path.mkdir(parents=True, exist_ok=True)
report = dict(label=label,source_floor_label=source_label,source_floor_center_cm=[center.x,center.y,center.z],source_floor_half_extent_cm=[extent.x,extent.y,extent.z],fixture_support_probes=len(support_probes),floor_mesh=floor_mesh.get_path_name(), floor_profile=str(floor_profile), floor_z=floor_z, samples=[], jumps=[], anomalies=[])
started = time.monotonic()
phase = 0
game = None
people = []
previous = {}
jumped = set()
recovery_started=0
operator=roof=None
recovery_origin=None
recovery_rise=0
captured=set()


def capture_view(name):
    if not RENDER_CAPTURE or name in captured:return
    captured.add(name)
    target=path / (label+'_'+name+'.png')
    unreal.SystemLibrary.execute_console_command(game,'HighResShot 1600x900 filename='+target.as_posix())
    report.setdefault('rendered_views',{})[name]=str(target)


def finish(ok, detail):
    report.update(ok=ok, detail=detail, wall_seconds=time.monotonic() - started)
    (path / (label + '.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('HUMAN_LANDING_PIE_PASSED' if ok else 'HUMAN_LANDING_PIE_FAILED', detail)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():unreal.SystemLibrary.quit_editor()


def run_tick(dt):
    global phase, game, people,recovery_started,operator,roof,recovery_origin,recovery_rise
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
        if phase>=2:
            body=operator.get_component_by_class(unreal.CapsuleComponent)
            mesh=next(c for c in operator.get_components_by_class(unreal.SkeletalMeshComponent) if c.get_name()=='CharacterMesh0')
            age=now-recovery_started
            assert age<15,('Ragdoll recovery phase timeout',phase)
            if phase==2:
                report.setdefault('before_topple_movement',dict(tick_enabled=operator.character_movement.is_component_tick_enabled(),auto_tick=operator.character_movement.get_editor_property('auto_update_tick_registration')))
                if not operator.is_physical_ragdoll():
                    body.add_torque_in_radians(unreal.Vector(12000000,0,0),unreal.Name('None'),False)
                    assert age<3,'Native tipping impulse did not produce a ragdoll'
                    return
                phase=3;recovery_started=now
                report['native_topple']=True
                p=mesh.get_socket_location('pelvis')
                roof.set_actor_location(unreal.Vector(p.x,p.y,floor_z+150),False,True)
                return
            if phase==3:
                assert operator.is_physical_ragdoll(),'Recovered through the low ceiling'
                camera=unreal.GameplayStatics.get_player_camera_manager(game,0)
                if age>.5:
                    pelvis=mesh.get_socket_location('pelvis');view=camera.get_camera_location()
                    aim=pelvis
                    eye=aim-unreal.MathLibrary.get_forward_vector(unreal.Rotator(pitch=-20,yaw=operator.get_control_rotation().yaw))*260
                    hit=unreal.SystemLibrary.sphere_trace_single(game,aim,eye,12,unreal.TraceTypeQuery.ECC_VISIBILITY,False,[operator],unreal.DrawDebugTrace.NONE,True).to_tuple()
                    sample=dict(age=age,pelvis=[pelvis.x,pelvis.y,pelvis.z],view=[view.x,view.y,view.z],distance=(view-pelvis).length(),
                                sweep_blocking=bool(hit[0]),sweep_start_penetrating=bool(hit[1]),sweep_distance=hit[3],
                                sweep_actor=hit[9].get_actor_label() if hit[9] else None)
                    report.setdefault('fallen_camera_samples',[]).append(sample)
                    assert sample['distance']>45,('Fallen camera is inside the body',sample)
                    first=next(c for c in operator.get_components_by_class(unreal.SkeletalMeshComponent) if c.get_name()=='First Person Mesh')
                    assert first.get_editor_property('hidden_in_game'),'Fallen first-person body remained visible'
                if age>2:capture_view('fallen_external')
                if age<4:return
                roof.set_actor_location(unreal.Vector(center.x+10000,center.y+10000,floor_z+150),False,True)
                operator.do_jump_start();phase=4;recovery_started=now;return
            if phase==4:
                if operator.is_physical_ragdoll():return
                assert operator.root_component==body and body.is_simulating_physics() and abs(body.get_mass()-80)<.1
                phones=[c for c in operator.get_components_by_class(unreal.StaticMeshComponent) if 'Phone' in c.get_name()]
                assert len(phones)==4 and all(not c.is_visible() and c.get_editor_property('hidden_in_game') for c in phones),'Recovery revealed the unused phone'
                first=next(c for c in operator.get_components_by_class(unreal.SkeletalMeshComponent) if c.get_name()=='First Person Mesh')
                assert not first.get_editor_property('hidden_in_game'),'First-person view was not restored'
                phase=5;recovery_started=now;return
            if phase==5:
                if age>.3:capture_view('recovered_first_person')
                assert not operator.is_physical_ragdoll(),'Walking retriggered a fall'
                pc=unreal.GameplayStatics.get_player_controller(game,0)
                velocity=operator.get_velocity();position=body.get_world_location()
                pending=operator.get_pending_movement_input_vector();last_input=operator.get_last_movement_input_vector()
                floor_hit=unreal.SystemLibrary.line_trace_single(game,position,position-unreal.Vector(0,0,body.get_scaled_capsule_half_height()+8),unreal.TraceTypeQuery.ECC_VISIBILITY,False,[operator],unreal.DrawDebugTrace.NONE,True).to_tuple()
                sample=dict(age=age,position=[position.x,position.y,position.z],velocity=[velocity.x,velocity.y,velocity.z],
                            physical_velocity=list(body.get_physics_linear_velocity().to_tuple()),mass=body.get_mass(),simulating=body.is_simulating_physics(),
                            root_name=operator.root_component.get_name(),half=body.get_unscaled_capsule_half_height(),up_z=body.get_up_vector().z,
                            movement_tick_enabled=operator.character_movement.is_component_tick_enabled(),
                            pending_before=[pending.x,pending.y,pending.z],last_input=[last_input.x,last_input.y,last_input.z],
                            move_ignored=pc.is_move_input_ignored(),observer=pc.is_observer_view(),menu_open=pc.is_warehouse_menu_open(),
                            movement_falling=operator.character_movement.is_falling(),crouched=bool(operator.get_editor_property('is_crouched')),
                            floor_blocking=bool(floor_hit[0]),floor_actor=floor_hit[9].get_actor_label() if floor_hit[9] else None,
                            floor_distance=floor_hit[3],mesh_simulating=mesh.is_simulating_physics())
                report.setdefault('recovery_walk_samples',[]).append(sample)
                operator.add_movement_input(recovery_direction,1,True)
                pending_after=operator.get_pending_movement_input_vector();sample['pending_after']=[pending_after.x,pending_after.y,pending_after.z]
                if age<1.5:return
                assert velocity.length()>300,('Recovered character cannot walk',sample)
                phase=6;recovery_started=now;return
            if phase==6:
                if age<1:return
                recovery_origin=operator.get_actor_location().z;operator.do_jump_start()
                phase=7;recovery_started=now;return
            if phase==7:
                assert not operator.is_physical_ragdoll(),'Normal jump retriggered a fall'
                recovery_rise=max(recovery_rise,operator.get_actor_location().z-recovery_origin)
                if age<1.5:return
                assert recovery_rise>30,'Recovered character cannot jump'
                if RENDER_CAPTURE:
                    for name in ('fallen_external','recovered_first_person'):
                        target=path / (label+'_'+name+'.png')
                        assert target.is_file() and target.stat().st_size>10000,('Missing rendered recovery view',str(target))
                report['recovery']=dict(blocked_headroom=True,camera_outside_body=True,restored_physics_mass_kg=body.get_mass(),walk=True,jump_rise_cm=recovery_rise,unused_phone_hidden=True)
                finish(True,'48 native jumps and crouch/stand cycles; impulse-induced topple, blocked-headroom recovery wait, safe get-up, walk/jump and hidden unused phone')
                return
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
            for person in people:person.set_locomotion_input(False,False)
            operator=unreal.GameplayStatics.get_player_character(game,0)
            roof=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.StaticMeshActor) if a.get_actor_label()=='RecoveryLowCeiling')
            phase=2;recovery_started=now
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
                person.add_movement_input(travel*(1 if cycle % 2 == 0 else -1), 1, True)
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


busy=False
def tick(dt):
    global busy
    if busy:return
    busy=True
    try:run_tick(dt)
    finally:busy=False
handle = unreal.register_slate_post_tick_callback(tick)
