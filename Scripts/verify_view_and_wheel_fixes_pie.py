"""PIE regression: F1 debug binding, menu rendering, owner-only head hiding and native axles.

Uses an unsaved Entry fixture with the real player Blueprint and AGV. RenderOffscreen
captures the menu, carrying view and original wheel mounts; NullRHI checks state only.
Tests the loaded debug binding and menu handler, not OS keyboard injection.
"""
from pathlib import Path
import sys, time, traceback, unreal
sys.path.insert(0,str(Path(__file__).parent))
from apply_real_world_scale import fit

root=Path(unreal.Paths.project_dir())
render='-nullrhi' not in unreal.SystemLibrary.get_command_line().lower()
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
saved_vehicles=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseForklift)]
assert len(saved_vehicles)==2
for saved_rig in saved_vehicles:
    saved_parts={c.get_name():c for c in saved_rig.get_components_by_class(unreal.StaticMeshComponent)}
    assert len(saved_parts)==12
    for name,sign in (('LoadWheelL',1),('LoadWheelR',-1)):
        expected=unreal.Vector(-7*215/282.55,sign*45*215/282.55,10.5*215/282.55)
        assert (saved_parts[name].get_relative_transform().translation-expected).length()<.001
    points=[]
    for comp in saved_parts.values():
        if not comp.is_collision_enabled():continue
        bounds=comp.static_mesh.get_bounds()
        for x in (-1,1):
            for y in (-1,1):
                for z in (-1,1):
                    local=bounds.origin+unreal.Vector(x*bounds.box_extent.x,y*bounds.box_extent.y,z*bounds.box_extent.z)
                    world_point=unreal.MathLibrary.transform_location(comp.get_world_transform(),local)
                    points.append(unreal.MathLibrary.inverse_transform_location(saved_rig.get_actor_transform(),world_point))
    size=[max(getattr(p,k) for p in points)-min(getattr(p,k) for p in points) for k in ('x','y','z')]
    assert all(abs(a-b)<.1 for a,b in zip(size,(212.4,91.7,215))),size
    print('SAVED_ORIGINAL_FRONT_WHEEL_VERIFIED',saved_rig.get_actor_label(),'size_cm',size)
print('SAVED_FRONT_WHEEL_MOUNTS_PASSED',len(saved_vehicles),'vehicles; no map save')
assert level.load_level('/Engine/Maps/Entry')
for actor in list(actors.get_all_level_actors()):
    if not isinstance(actor,(unreal.WorldSettings,unreal.LevelScriptActor)): actors.destroy_actor(actor)
world=editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WorldSettings)[0].set_editor_property('default_game_mode',unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
floor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.set_actor_scale3d(unreal.Vector(40,40,.2))
if render:
    actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,400),unreal.Rotator(pitch=-40,yaw=20))
    actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,400))
actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(600,700,125))
vehicle=actors.spawn_actor_from_class(unreal.WarehouseForklift,unreal.Vector())
vehicle.set_actor_label('ViewFixAGV')
vehicle.set_editor_property('auto_start',False)
vehicle.set_editor_property('autonomous_mode',False)
dummy=actors.spawn_actor_from_class(unreal.TargetPoint,unreal.Vector(-200,-220,75))
dummy.set_actor_label('ViewFixOperator')
for comp in dummy.get_components_by_class(unreal.PrimitiveComponent): comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
box=actors.spawn_actor_from_class(unreal.WarehouseCargo,unreal.Vector(650,700,10.25))
box.set_actor_label('ViewFixBox')
box.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1'))
box.set_gross_mass_kg(12)
fit(box,(30,24,20),(650,700,10.25))
camera=actors.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(235,-255,105),unreal.MathLibrary.find_look_at_rotation(unreal.Vector(235,-255,105),unreal.Vector(-25,0,65)))
camera.set_actor_label('ViewFixCamera')
began=time.monotonic();phase='start';at=0;game=pc=person=rig=operator=cargo=review=None
parts={};wheels={};max_axle_error=0;max_front_spin=0;max_speed=0;start_yaw=0

def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def enter(name,now):
    global phase,at
    phase,at=name,now

def shot(name):
    if render:
        (root/'Saved/Testing').mkdir(exist_ok=True)
        path=(root/'Saved/Testing'/name).as_posix()
        command='Shot showui -nosuffix' if name=='F1MenuFixed.png' else 'HighResShot 960x640'
        unreal.SystemLibrary.execute_console_command(game,command+' filename='+path,pc)

def check_head():
    carry=parts['TwoHandCarryMesh']
    assert carry.is_bone_hidden_by_name('neck_01'), 'Carry neck is still rendered'
    # Visibility is applied to GPU skinning matrices; socket poses remain animated.
    assert carry.is_bone_hidden_by_name('head'), 'Carry head did not inherit neck hiding'
    for name in ('CharacterMesh0','First Person Mesh'):
        assert not parts[name].is_bone_hidden_by_name('head'), ('Camera/world source head hidden',name)
        assert parts[name].get_socket_transform('head',unreal.RelativeTransformSpace.RTS_COMPONENT).scale3d.length()>1
    for bone in ('hand_l','hand_r'):
        assert not carry.is_bone_hidden_by_name(bone),('Carry hand hidden',bone)
        assert carry.get_socket_transform(bone,unreal.RelativeTransformSpace.RTS_COMPONENT).scale3d.length()>1

def tick(dt):
    global game,pc,person,rig,operator,cargo,review,parts,wheels,max_axle_error,max_front_spin,max_speed,start_yaw
    try:
        assert time.monotonic()-began<180,('Presentation PIE timeout',phase)
        if phase=='start':
            level.editor_request_begin_play();enter('wait_world',0);return
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase=='wait_world':
            named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            rig,operator,cargo,review=(named[n] for n in ('ViewFixAGV','ViewFixOperator','ViewFixBox','ViewFixCamera'))
            pc=unreal.GameplayStatics.get_player_controller(game,0)
            person=pc.get_controlled_pawn()
            pc.set_control_rotation(unreal.Rotator())
            parts={c.get_name():c for c in person.get_components_by_class(unreal.SkeletalMeshComponent)}
            wheels={c.get_name():c for c in rig.get_components_by_class(unreal.StaticMeshComponent)}
            enter('settle',now);return
        age=now-at
        if phase=='settle':
            if age<1.5:return
            player_input=unreal.get_default_object(unreal.EnhancedPlayerInput)
            bindings=player_input.get_editor_property('debug_exec_bindings')
            assert not any(str(b.get_editor_property('Key').get_editor_property('KeyName'))=='F1' for b in bindings), 'Inherited F1 viewmode binding remains'
            # This follows the debug Exec path which previously selected wireframe.
            unreal.SystemLibrary.execute_console_command(game,'F1',pc)
            assert not pc.is_warehouse_menu_open()
            pc.set_view_target_with_blend(review,0)
            pc.toggle_warehouse_menu()
            assert pc.is_warehouse_menu_open()
            enter('menu',now);return
        if phase=='menu':
            if age<1:return
            shot('F1MenuFixed.png')
            enter('menu_capture',now);return
        if phase=='menu_capture':
            if age<1:return
            pc.toggle_warehouse_menu();assert not pc.is_warehouse_menu_open()
            pc.set_view_target_with_blend(person,0)
            print('F1_BINDING_MENU_PASSED','loaded EnhancedPlayerInput config has no F1 debug Exec; menu opens/closes')
            assert person.try_pickup_cargo(cargo)
            enter('carry',now);return
        if phase=='carry':
            if age<1.2:return
            assert person.has_held_cargo()
            check_head()
            assert parts['TwoHandCarryMesh'].is_visible() and not parts['First Person Mesh'].is_visible()
            shot('CarryViewHeadHidden.png')
            enter('carry_capture',now);return
        if phase=='carry_capture':
            if age<1:return
            pc.set_control_rotation(unreal.Rotator(pitch=-40))
            enter('carry_look_down',now);return
        if phase=='carry_look_down':
            if age<1:return
            check_head();shot('CarryViewLookingDown.png')
            enter('carry_down_capture',now);return
        if phase=='carry_down_capture':
            if age<1:return
            person.drop_cargo();enter('carry_release',now);return
        if phase=='carry_release':
            if age<.6:return
            assert not person.has_held_cargo()
            assert parts['First Person Mesh'].is_visible() and not parts['TwoHandCarryMesh'].is_visible()
            assert not parts['CharacterMesh0'].is_bone_hidden_by_name('head')
            print('CARRY_OWNER_HEAD_HIDDEN_PASSED','neck/head hidden only in carry render copy; hands/source camera/world mesh preserved; release restores view')
            pc.set_view_target_with_blend(review,0)
            shot('FrontWheelOriginalMounts.png')
            enter('wheel_capture',now);return
        if phase=='wheel_capture':
            if age<1:return
            assert abs(rig.get_physical_mass_kg()-1000)<.1
            assert rig.begin_remote_control(operator)
            enter('remote_check',now);return
        if phase=='remote_check':
            rig.set_remote_input(0,0,0)
            if age<1.3:return
            assert rig.empty_travel_speed_cm==180
            enter('drive',now);return
        if phase in ('drive','brake','turn'):
            s=215/282.55
            for name,sign in (('LoadWheelL',1),('LoadWheelR',-1)):
                actual=unreal.MathLibrary.inverse_transform_location(rig.get_actor_transform(),wheels[name].get_world_location())
                expected=unreal.Vector(-7*s,sign*45*s,10.5*s)
                error=(actual-expected).length()
                max_axle_error=max(max_axle_error,error)
                assert error<.5,('Front axle left original mount',name,actual,error)
                max_front_spin=max(max_front_spin,wheels[name].get_physics_angular_velocity_in_degrees().length())
            assert not rig.root_component.get_attach_parent()
            if phase=='drive':
                rig.set_remote_input(1,0,0)
                max_speed=max(max_speed,rig.root_component.get_physics_linear_velocity().length())
                if age<4:return
                print('ORIGINAL_WHEEL_DRIVE_DIAGNOSTIC','speed_cm_s',max_speed,'axle_error_cm',max_axle_error,'status',rig.status,'pose',rig.get_actor_transform())
                assert max_speed>165,('Restored wheel drive speed',max_speed,rig.status)
                assert max_front_spin>90, 'Front rollers do not rotate physically'
                enter('brake',now);return
            if phase=='brake':
                rig.set_remote_input(0,0,0)
                if age<2:return
                start_yaw=rig.get_actor_rotation().yaw
                enter('turn',now);return
            rig.set_remote_input(0,1,0)
            if age<4:return
            yaw=(rig.get_actor_rotation().yaw-start_yaw+180)%360-180
            assert yaw>40,('Original wheel mounts blocked steering',yaw)
            rig.end_remote_control(operator)
            if render:
                for name in ('F1MenuFixed.png','CarryViewHeadHidden.png','CarryViewLookingDown.png','FrontWheelOriginalMounts.png'):
                    assert (root/'Saved/Testing'/name).exists(),name
            print('VIEW_AND_WHEEL_FIXES_PIE_PASSED','axle_error_cm',max_axle_error,'front_spin_deg_s',max_front_spin,'speed_cm_s',max_speed,'turn_deg',yaw,'render_captures',render)
            finish()
    except Exception:
        print('VIEW_AND_WHEEL_FIXES_PIE_FAILED',phase,traceback.format_exc());finish()

handle=unreal.register_slate_post_tick_callback(tick)
