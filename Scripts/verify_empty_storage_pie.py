"""Real PIE check: saved empty storage stacks and E-driven pallet placement. Saves nothing."""
import unreal,time,traceback,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from configure_empty_storage import RACKS,STACKS,STACK_HEIGHT,configure_empty_storage
from apply_real_world_scale import fit
PLACEMENT_ONLY='-placementonly' in unreal.SystemLibrary.get_command_line().lower()
CAPTURE='-captureemptystorage' in unreal.SystemLibrary.get_command_line().lower() and not PLACEMENT_ONLY

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Engine/Maps/Entry' if PLACEMENT_ONLY else '/Game/FirstPerson/Lvl_FirstPerson')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
# Use an engine-provided 0.1s test frame. On the large headless map, wall-clock
# frames can exceed MaxPhysicsDeltaTime and falsely make grip timeouts outrun physics.
unreal.SystemLibrary.execute_console_command(world,'t.OverrideFPS 10')
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
if PLACEMENT_ONLY:
    unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WorldSettings)[0].set_editor_property('default_game_mode',
        unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
else:
    assert sum(n.startswith('WH_EmptyRack_') for n in named)==len(RACKS)*8
    assert sum(n.startswith('WH_EmptyPallet_') for n in named)==len(STACKS)*STACK_HEIGHT
    original_profiles=len(named['WH_DamageSystem'].get_editor_property('objects'))
    configure_empty_storage()
    assert len(actors.get_all_level_actors())==len(named),'Duplicate storage actors on rerun'
    assert len(named['WH_DamageSystem'].get_editor_property('objects'))==original_profiles,'Duplicate strength profiles on rerun'
    print('EMPTY_STORAGE_IDEMPOTENCE_PASSED')
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)

def spawn(cls,label,location):
    a=actors.spawn_actor_from_class(cls,unreal.Vector(*location))
    a.set_actor_label(label)
    a.set_editor_property('is_spatially_loaded',False)
    return a

if CAPTURE:
    for label,origin,aim in [('EmptyStoragePreviewPallets',(1320,1200,240),(1760,1730,45)),
                             ('EmptyStoragePreviewRacks',(-850,-1820,235),(-1590,-1680,125))]:
        preview=spawn(unreal.SceneCapture2D,label,origin)
        preview.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(unreal.Vector(*origin),unreal.Vector(*aim)),False)
        cap=preview.get_component_by_class(unreal.SceneCaptureComponent2D)
        cap.texture_target=unreal.RenderingLibrary.create_render_target2d(world,960,640,unreal.TextureRenderTargetFormat.RTF_RGBA8)
        cap.capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
        cap.fov_angle=60

floor=spawn(unreal.StaticMeshActor,'EmptyStackTestFloor',(8000,8000,-10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.set_actor_scale3d(unreal.Vector(12,12,.2))
support=spawn(unreal.WarehousePallet,'EmptyStackTestSupport',(8000,8000,.1))
held=spawn(unreal.WarehousePallet,'EmptyStackTestHeld',(7960,7865,.1))
carton_support=spawn(unreal.WarehousePallet,'EmptyStackCartonSupport',(8000,8350,.1))
carton=spawn(unreal.WarehouseCargo,'EmptyStackCarton',(7970,8240,20))
carton.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1'))
fit(carton,(40,30,25),(7970,8240,12.7));carton.set_gross_mass_kg(12.)
player=spawn(unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C'),
             'EmptyStackTestPlayer',(7860,7930,96))
started=time.monotonic();phase=0;at=0;game=None;char=None;pc=None;support=None;held=None
stack_initial={};max_speed=0;live_cache={};pose_trace_at=0

def body(a): return a.get_component_by_class(unreal.StaticMeshComponent)
def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(dt):
    global phase,at,game,char,pc,support,held,max_speed,live_cache,pose_trace_at
    try:
        assert time.monotonic()-started<360,'PIE timeout'
        if phase==0:
            if time.monotonic()-started<3:return
            level.editor_request_begin_play();phase=1;return
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if not live_cache:
            live_cache={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
        live=live_cache
        pallets=[a for n,a in live.items() if n.startswith('WH_EmptyPallet_')]
        for a in pallets:
            assert body(a).is_simulating_physics() and not a.get_attach_parent_actor(),a.get_actor_label()
            assert abs(body(a).get_mass()-25)<.01
            if stack_initial:
                assert (a.get_actor_bounds(False)[0]-stack_initial[a.get_actor_label()]).length()<1,'Map stack drifted during placement checks'
        if phase==1:
            if now<16:return
            for stack,(x,y) in enumerate([] if PLACEMENT_ONLY else STACKS):
                for tier in range(STACK_HEIGHT):
                    a=live[f'WH_EmptyPallet_{stack:02}_{tier}'];c,e=a.get_actor_bounds(False)
                    assert abs(c.x-x)<3 and abs(c.y-y)<3 and abs(c.z-(7.5+tier*15))<3,('Unstable map stack',a.get_actor_label(),c)
                    assert abs(a.get_actor_rotation().pitch)<2 and abs(a.get_actor_rotation().roll)<2
                    # Multi-convex worn wood can have small contact velocities without
                    # drifting. Track the settled pose throughout the remaining checks.
                    assert body(a).get_physics_linear_velocity().length()<20,(a.get_actor_label(),c,body(a).get_physics_linear_velocity())
                    stack_initial[a.get_actor_label()]=c
            if not PLACEMENT_ONLY:
                print('SAVED_EMPTY_STACKS_PASSED',len(pallets),'dynamic 25kg pallets; 16 seconds settled')
                damage=live['WH_DamageSystem']
                assert not any(damage.has_failed(a) for a in pallets),'Map stacks damaged at startup'
            for floor,index,x,y in ([] if PLACEMENT_ONLY else RACKS):
                prefix=f'WH_EmptyRack_L{floor+1}_{index}'
                profile=next(s for s in damage.get_editor_property('objects') if s.name==prefix)
                assert len(profile.members)==8 and not profile.get_editor_property('bFailed')
                assert profile.failure==unreal.WarehouseFailure.RACK and profile.rated_load_kg==4000
                assert all(body(a).get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION for a in profile.members)
                assert all(not damage.get_supported_actors(a) for a in profile.members),'Empty rack is occupied'
            if not PLACEMENT_ONLY: print('EMPTY_RACK_PROFILES_PASSED',len(RACKS),'bays; 4000kg per bay, 2000kg per level')
            char=live['EmptyStackTestPlayer'];support=live['EmptyStackTestSupport'];held=live['EmptyStackTestHeld']
            pc=unreal.GameplayStatics.get_player_controller(game,0);pc.possess(char)
            unreal.SystemLibrary.execute_console_command(game,'Log Logmsc_vr Verbose',pc)
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(char.get_component_by_class(unreal.CameraComponent).get_world_location(),held.get_actor_bounds(False)[0]))
            assert char.try_pickup_pallet(held),'Empty pallet pickup failed'
            at=now;phase=2;return
        if phase==2:
            if now-at<2:return
            assert char.get_held_cargo()==held,'Pallet grip lost before placement'
            print('PICKUP_POSE',held.get_actor_bounds(False)[0],held.get_actor_rotation())
            mb=body(held).static_mesh.get_bounds()
            print('PALLET_CENTRES',mb.origin,body(held).get_world_transform().transform_location(mb.origin),body(held).get_center_of_mass(),held.get_actor_location(),held.get_actor_scale3d())
            assert not char.try_place_on_pallet(held,held.get_actor_location()),'Self-placement accepted'
            assert not char.try_place_on_pallet(support,unreal.Vector(9000,9000,15)),'Out of reach accepted'
            support.set_editor_property('payload_mass_kg',1.)
            assert not char.try_place_on_pallet(support,unreal.Vector(8000,8000,15)),'Loaded pallet accepted'
            support.set_editor_property('payload_mass_kg',0.)
            eye=char.get_component_by_class(unreal.CameraComponent).get_world_location()
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye,unreal.Vector(8000,8000,15)))
            # E calls this same placement method after selecting the aimed support.
            print('PLACEMENT_INPUT',char.get_actor_location(),eye,support.get_actor_location(),support.get_actor_rotation(),body(support).get_physics_linear_velocity())
            assert char.try_place_on_pallet(support,unreal.Vector(8030,8025,15)),'Equal-size pallet placement rejected'
            at=now;phase=3;return
        if phase==3:
            max_speed=max(max_speed,body(held).get_physics_linear_velocity().length())
            if now-pose_trace_at>.5:
                print('PLACEMENT_STEP',round(now-at,2),held.get_actor_bounds(False)[0],held.get_actor_rotation(),char.get_held_cargo(),char.is_physical_ragdoll())
                pose_trace_at=now
            assert body(held).is_simulating_physics() and not held.get_attach_parent_actor()
            if now-at<7:return
            hc,he=held.get_actor_bounds(False);sc,se=support.get_actor_bounds(False)
            print('PALLET_PLACEMENT_POSES',hc,sc,'held',char.get_held_cargo(),'max speed',max_speed)
            assert char.get_held_cargo() is None,'Placement did not release'
            assert abs(hc.x-sc.x)<3 and abs(hc.y-sc.y)<3 and abs((hc.z-he.z)-(sc.z+se.z))<2,'Pallet did not stack'
            assert abs(held.get_actor_rotation().pitch)<2 and abs(held.get_actor_rotation().roll)<2
            assert max_speed<500,'Placement launched the pallet'
            print('E_PALLET_STACK_PLACEMENT_PASSED')
            assert char.try_pickup_pallet(held),'Top pallet cannot be picked up again'
            at=now;phase=4;return
        if phase==4:
            if now-at<2:return
            assert char.get_held_cargo()==held
            assert body(support).is_simulating_physics() and body(held).is_simulating_physics()
            assert abs(support.get_actor_bounds(False)[0].z-7.5)<2,'Base pallet moved vertically'
            char.drop_cargo()
            char.set_actor_location(unreal.Vector(7860,8350,96),False,True)
            pc.set_control_rotation(unreal.Rotator(yaw=0))
            assert char.try_pickup_cargo(live['EmptyStackCarton']),'Carton pickup regression'
            at=now;phase=5;return
        if phase==5:
            if now-at<2:return
            assert char.try_place_on_pallet(live['EmptyStackCartonSupport'],unreal.Vector(8000,8350,15)),'Carton placement regression'
            at=now;phase=6;return
        if phase==6:
            if now-at<5:return
            carton=live['EmptyStackCarton'];cc,ce=carton.get_actor_bounds(False)
            assert char.get_held_cargo() is None and body(carton).is_simulating_physics()
            assert abs(cc.x-8000)<5 and abs(cc.y-8350)<5 and abs(cc.z-ce.z-15)<2,('Carton did not settle',cc)
            print('CARTON_PLACEMENT_REGRESSION_PASSED')
            if CAPTURE:
                for label in ('EmptyStoragePreviewPallets','EmptyStoragePreviewRacks'):
                    cap=live[label].get_component_by_class(unreal.SceneCaptureComponent2D)
                    unreal.RenderingLibrary.export_render_target(game,cap.texture_target,'C:/msc_UnrealProject/msc_vr/Saved/Testing',label+'.png')
            print('EMPTY_STORAGE_PIE_PASSED');finish()
    except Exception:
        print('EMPTY_STORAGE_PIE_FAILED',traceback.format_exc());finish()

handle=unreal.register_slate_post_tick_callback(tick)
