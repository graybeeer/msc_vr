"""PIE regression: locomotion, live stack physics, pallet placement, menu and observer. Never saves the map."""
import unreal, time, traceback
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
unreal.SystemLibrary.execute_console_command(actors.get_editor_world() if hasattr(actors,'get_editor_world') else unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),'Editor.AsyncAssetCompilationFinishAll')
def spawn(cls,name,pos):
    a=actors.spawn_actor_from_class(cls,unreal.Vector(*pos))
    a.set_actor_label(name)
    a.set_editor_property('is_spatially_loaded',False)
    return a
floor=spawn(unreal.StaticMeshActor,'InteractionTestFloor',(8000,8000,-10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(12,12,.2))
floor.static_mesh_component.set_collision_profile_name('BlockAll')
pallet=spawn(unreal.WarehousePallet,'InteractionTestPallet',(8000,8000,0))
mesh=unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/Cargo_Box_V1_001')
assert mesh
for name,bottom in [('InteractionLower',16),('InteractionUpper',57)]:
    box=spawn(unreal.WarehouseCargo,name,(8000,8000,bottom))
    box.set_cargo_mesh(mesh)
    _,e=box.get_actor_bounds(False)
    box.set_actor_scale3d(unreal.Vector(60/(e.x*2),40/(e.y*2),40/(e.z*2)))
    c,e=box.get_actor_bounds(False)
    box.set_actor_location(box.get_actor_location()+unreal.Vector(8000-c.x,8000-c.y,bottom-(c.z-e.z)),False,True)
player=spawn(unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'),'InteractionTestPlayer',(7860,8000,100))
started=time.monotonic();phase=0;phase_time=0;game=None;pc=None;char=None;lower=None;upper=None;test_pallet=None;upper_z=0;eye_z=0

def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(dt):
    global phase,phase_time,game,pc,char,lower,upper,test_pallet,upper_z,eye_z
    try:
        assert time.monotonic()-started<180,'PIE timeout'
        if phase==0:
            if time.monotonic()-started<5:return
            level.editor_request_begin_play();phase=1;return
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase==1:
            if now<3:return
            named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            char=named['InteractionTestPlayer'];lower=named['InteractionLower'];upper=named['InteractionUpper'];test_pallet=named['InteractionTestPallet']
            cargo=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseCargo)
            assert len(cargo)>=264 and all(a.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics() for a in cargo),'Some placed cargo is still kinematic'
            print('INTERACTION_STARTUP_PHYSICS',len(cargo))
            pc=unreal.GameplayStatics.get_player_controller(game,0)
            assert pc,'No local player controller'
            unreal.SystemLibrary.execute_console_command(game,'Log Logmsc_vr Verbose',pc)
            pc.possess(char)
            movement=char.get_component_by_class(unreal.CharacterMovementComponent)
            base=movement.max_walk_speed
            char.set_locomotion_input(True,False)
            assert movement.max_walk_speed>base*1.5,'Sprint did not increase speed'
            char.set_locomotion_input(False,False)
            assert abs(movement.max_walk_speed-base)<.1,'Walk speed did not restore'
            assert movement.get_editor_property('nav_agent_props').get_editor_property('can_crouch'),'Crouch disabled'
            eye_z=char.get_component_by_class(unreal.CameraComponent).get_world_location().z
            pc.un_possess()
            movement.set_editor_property('run_physics_with_no_controller',True)
            char.set_locomotion_input(False,True)
            phase_time=now;phase=10;return
        if phase==10:
            if now-phase_time<.5:return
            capsule=char.get_component_by_class(unreal.CapsuleComponent)
            assert abs(capsule.get_unscaled_capsule_half_height()-58)<1,'Crouch capsule did not shrink'
            assert char.get_component_by_class(unreal.CameraComponent).get_world_location().z<eye_z-25,'Crouch camera did not lower'
            char.set_locomotion_input(False,False)
            phase_time=now;phase=11;return
        if phase==11:
            if now-phase_time<.5:return
            assert abs(char.get_component_by_class(unreal.CapsuleComponent).get_unscaled_capsule_half_height()-96)<1,'Standing height not restored'
            pc.possess(char)
            print('INTERACTION_LOCOMOTION_VERIFIED sprint, walk restore, crouch capsule and camera')
            body=upper.get_component_by_class(unreal.StaticMeshComponent)
            assert body.is_simulating_physics(),'Initial stack is kinematic'
            upper_z=upper.get_actor_bounds(False)[0].z
            print('STACK_BEFORE',lower.get_actor_location(),lower.get_actor_bounds(False),upper.get_actor_location(),upper.get_actor_bounds(False),body.get_physics_linear_velocity())
            assert char.try_pickup_cargo(lower),'Pickup failed'
            phase_time=now;phase=2;return
        if phase==2:
            if now-phase_time<2:return
            print('STACK_AFTER',upper_z,upper.get_actor_location(),upper.get_actor_bounds(False),upper.get_component_by_class(unreal.StaticMeshComponent).get_physics_linear_velocity())
            assert upper.get_actor_bounds(False)[0].z<upper_z-25,'Unsupported box did not fall'
            # Gravity may tumble the asymmetric worn carton. Set a level support for
            # the separate placement check; tilted/uneven supports are intentionally rejected.
            support=upper.get_component_by_class(unreal.StaticMeshComponent)
            support.set_simulate_physics(False)
            upper.set_actor_rotation(unreal.Rotator(),True)
            c,e=upper.get_actor_bounds(False)
            upper.set_actor_location(upper.get_actor_location()+unreal.Vector(8000-c.x,8000-c.y,16-(c.z-e.z)),False,True)
            support.set_simulate_physics(True)
            support.set_physics_linear_velocity(unreal.Vector())
            support.set_physics_angular_velocity_in_degrees(unreal.Vector())
            c,e=upper.get_actor_bounds(False)
            assert char.try_place_on_pallet(test_pallet,unreal.Vector(c.x,c.y,c.z+e.z)),'Valid stack placement rejected'
            phase_time=now;phase=3;return
        if phase==3:
            if now-phase_time<2:return
            assert not lower.get_attach_parent_actor(),'Placement animation never released cargo'
            assert lower.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics(),'Placed cargo is kinematic'
            lc,le=lower.get_actor_bounds(False);uc,ue=upper.get_actor_bounds(False)
            assert abs((lc.z-uc.z)-40)<6 and abs(lc.x-uc.x)<15 and abs(lc.y-uc.y)<15,('Not resting on stack',lc,uc)
            print('INTERACTION_STACK_VERIFIED startup physics, support removal, animated placement')
            assert char.try_pickup_cargo(lower)
            assert not char.try_place_on_pallet(test_pallet,unreal.Vector(9000,9000,55)),'Out-of-reach placement accepted'
            support=upper.get_component_by_class(unreal.StaticMeshComponent)
            support.set_simulate_physics(False)
            upper.set_actor_location(upper.get_actor_location()+unreal.Vector(300,0,0),False,True)
            support.set_simulate_physics(True)
            phase_time=now;phase=13;return
        if phase==13:
            if now-phase_time<.5:return  # Let Chaos update the moved support's broadphase.
            assert char.try_place_on_pallet(test_pallet,unreal.Vector(8000,8000,15)),'Empty pallet placement rejected'
            phase_time=now;phase=12;return
        if phase==12:
            if now-phase_time<2:return
            lc,le=lower.get_actor_bounds(False)
            assert not lower.get_attach_parent_actor() and abs(lc.z-35)<5,'Empty pallet placement did not finish'
            print('INTERACTION_EMPTY_PALLET_VERIFIED')
            original=char.get_actor_location()
            pc.toggle_warehouse_menu();assert pc.is_warehouse_menu_open() and pc.is_move_input_ignored()
            pc.toggle_warehouse_menu();assert not pc.is_warehouse_menu_open() and not pc.is_move_input_ignored()
            pc.toggle_observer_view();assert pc.is_observer_view() and pc.is_move_input_ignored()
            assert pc.get_view_target()!=char and pc.get_controlled_pawn()==char,'Observer took over character'
            assert (char.get_actor_location()-original).length()<.1,'Observer moved character'
            pc.toggle_observer_view();assert pc.get_view_target()==char and not pc.is_move_input_ignored()
            print('INTERACTION_MENU_VERIFIED settings overlay, observer camera, return to possessed character')
            pc.toggle_warehouse_menu()
            phase_time=now;phase=4;return
        if phase==4:
            if now-phase_time<1:return
            unreal.SystemLibrary.execute_console_command(game,'Shot showui filename=C:/msc_UnrealProject/msc_vr/Saved/InteractionMenu.png',pc)
            phase_time=now;phase=5;return
        if phase==5:
            if now-phase_time<2:return
            pc.toggle_warehouse_menu()
            print('WAREHOUSE_INTERACTION_PIE_VERIFIED')
            finish()
    except Exception:
        print('WAREHOUSE_INTERACTION_PIE_FAILED',traceback.format_exc());finish()
handle=unreal.register_slate_post_tick_callback(tick)
