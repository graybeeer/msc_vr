"""Native PIE: real load detection, reservations, rack delivery and saved-map layout. No saves."""
import unreal,time,traceback,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from configure_inbound_area import configure_inbound_area,RACK_CENTRES
from apply_real_world_scale import fit

SAVED='-inboundsavedmap' in unreal.SystemLibrary.get_command_line().lower()
CAPTURE='-inboundcapture' in unreal.SystemLibrary.get_command_line().lower()
PREVIEW_ONLY='-inboundpreviewonly' in unreal.SystemLibrary.get_command_line().lower()
REGION_ONLY='-inboundregiononly' in unreal.SystemLibrary.get_command_line().lower()
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
assert named['WH_Wall_E'].get_actor_bounds(False)[0].x==2780
assert abs(named['WH_InboundFloor'].get_actor_bounds(False)[1].x-400)<.1
zone=named['WH_InboundDispatch']
assert len(zone.get_editor_property('rack_slots'))==16
assert len(zone.get_editor_property('fleet'))==2
for n,a in named.items():
    if '_Paint_' in n and n.startswith('WH_InboundZone'):
        assert a.static_mesh_component.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION
configure_inbound_area()
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
count=len(named);profiles=len(named['WH_DamageSystem'].get_editor_property('objects'))
configure_inbound_area()
assert len(actors.get_all_level_actors())==count
assert len(named['WH_DamageSystem'].get_editor_property('objects'))==profiles
print('INBOUND_LAYOUT_IDEMPOTENCE_PASSED')
if SAVED and '-saveinboundbeforepie' in unreal.SystemLibrary.get_command_line().lower():
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('INBOUND_FINAL_LAYOUT_SAVED')
if SAVED and REGION_ONLY:
    # Test-only: retain exact clutter geometry as static colliders while omitting
    # unrelated cargo dynamics/damage. Do not save after this conversion.
    keep={'WH_TrainingPallet','WH_TrainingPallet_02','WH_InboundPallet_Empty','MZ_TransferCargo','WH_TransferCargo_02'}
    removed=set()
    for label,a in list(named.items()):
        if isinstance(a,(unreal.WarehousePallet,unreal.WarehouseCargo)) and label not in keep:
            part=a.get_component_by_class(unreal.StaticMeshComponent)
            replacement=actors.spawn_actor_from_class(unreal.StaticMeshActor,part.get_world_location(),part.get_world_rotation())
            replacement.set_actor_label('InboundStatic_'+label)
            replacement.static_mesh_component.set_static_mesh(part.static_mesh)
            replacement.static_mesh_component.set_editor_property('override_materials',part.get_editor_property('override_materials'))
            replacement.set_actor_transform(part.get_world_transform(),False,True)
            replacement.static_mesh_component.set_collision_profile_name('BlockAll')
            replacement.set_editor_property('is_spatially_loaded',False)
            removed.add(label);actors.destroy_actor(a)
    damage=named['WH_DamageSystem']
    damage.set_editor_property('objects',[s for s in damage.get_editor_property('objects') if any(unreal.SystemLibrary.is_valid(a) for a in s.members)])
    print('INBOUND_REGION_TEST_SCOPE',len(removed),'background cargo/pallets static; buildings, racks, people, fleet and inbound loads unchanged')
def spawn(cls,label,point,yaw=0):
    a=actors.spawn_actor_from_class(cls,unreal.Vector(*point),unreal.Rotator(yaw=yaw))
    a.set_actor_label(label);a.set_editor_property('is_spatially_loaded',False)
    return a

if not SAVED:
    rig_data={c.get_name():(c.get_relative_transform(),c.static_mesh,list(c.get_editor_property('override_materials')))
        for c in named['WH_AutonomousForklift'].get_components_by_class(unreal.StaticMeshComponent)}
    rack_data=[(a.get_actor_location(),a.get_actor_rotation(),a.get_actor_scale3d(),a.static_mesh_component.static_mesh,
                list(a.static_mesh_component.get_editor_property('override_materials'))) for a in [named['WH_InboundRack_00_'+p] for p in list('ABCDEFGH')+['Deck_20','Deck_140']]]
    assert level.load_level('/Engine/Maps/Entry')
    for a in list(actors.get_all_level_actors()):
        if not isinstance(a,(unreal.WorldSettings,unreal.LevelScriptActor)):actors.destroy_actor(a)
    world=editor.get_editor_world()
    unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WorldSettings)[0].set_editor_property('default_game_mode',
        unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
    floor=spawn(unreal.StaticMeshActor,'InboundTestFloor',(0,0,-10))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.set_actor_scale3d(unreal.Vector(60,40,.2));floor.static_mesh_component.set_collision_profile_name('BlockAll')
    spawn(unreal.PlayerStart,'InboundTestStart',(-2000,-1500,125))
    vehicle=spawn(unreal.WarehouseForklift,'InboundTestAGV',(0,0,0))
    for c in vehicle.get_components_by_class(unreal.StaticMeshComponent):
        transform,mesh,materials=rig_data[c.get_name()]
        c.set_relative_transform(transform,False,True);c.set_static_mesh(mesh);c.set_editor_property('override_materials',materials)
    vehicle.set_editor_property('auto_start',True)
    pallet=spawn(unreal.WarehousePallet,'InboundTestLoaded',(500,0,.1))
    empty=spawn(unreal.WarehousePallet,'InboundTestEmpty',(500,220,.1))
    cargo=spawn(unreal.WarehouseCargo,'InboundTestCarton',(500,0,31))
    cargo.set_cargo_mesh(unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2'))
    recipe=unreal.WarehouseCargo.generate_cargo_recipe(20261011,0,-1,unreal.Vector(40,30,30),1)
    assert recipe.valid and cargo.apply_cargo_recipe(recipe)
    fit(cargo,(40,30,30),(500,0,30.2))
    rack=[]
    for index,(p,r,s,mesh,materials) in enumerate(rack_data):
        a=spawn(unreal.StaticMeshActor,f'InboundTestRack{index}',(p.x-RACK_CENTRES[0][0]+1200,p.y-RACK_CENTRES[0][1]+62.5,p.z))
        a.set_actor_rotation(r,False);a.set_actor_scale3d(s);a.static_mesh_component.set_static_mesh(mesh)
        a.static_mesh_component.set_editor_property('override_materials',materials);a.static_mesh_component.set_collision_profile_name('BlockAll');rack.append(a)
    print('INBOUND_RACK_GEOMETRY',[(a.get_actor_label(),a.get_actor_bounds(False)) for a in rack])
    zone=spawn(unreal.WarehouseInboundZone,'InboundTestZone',(500,110,0))
    zone.set_editor_property('half_size_cm',unreal.Vector(180,260,80));zone.set_editor_property('fleet',[vehicle])
    zone.set_editor_property('rack_slots',[unreal.WarehouseRackSlot(name='FixtureRackLower',rack=rack[0],
        pose=unreal.Transform(location=unreal.Vector(1200,0,20)),clear_height_cm=110,capacity_kg=10)])
    damage=spawn(unreal.WarehouseDamageSystem,'InboundTestStrength',(0,-1800,0))
    damage.set_editor_property('objects',[
        unreal.WarehouseStrength(name=a.get_actor_label(),members=[a],mass_kg=mass,rated_load_kg=rated,impact_yield_j=200,impact_failure_j=1000)
        for a,mass,rated in [(pallet,25,1500),(empty,25,1500),(cargo,cargo.get_editor_property('gross_mass_kg'),120)]
    ]+[unreal.WarehouseStrength(name='FixtureRack',members=rack,mass_kg=250,rated_load_kg=4000,level_capacity_kg=2000,shelf_heights_cm=[20,140])])
world=editor.get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'t.OverrideFPS 30')
if CAPTURE:
    preview=spawn(unreal.SceneCapture2D,'InboundPreview',(2050,-1500,1000))
    preview.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(preview.get_actor_location(),unreal.Vector(2380,-1300,0)),False)
    cap=preview.get_component_by_class(unreal.SceneCaptureComponent2D)
    cap.texture_target=unreal.RenderingLibrary.create_render_target2d(world,1100,760,unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cap.capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR;cap.fov_angle=72
    cap.capture_every_frame=False;cap.capture_on_movement=False
started=time.monotonic();last_log=-10;live=None;checked=False;capacity_checked=False
def finish(ok,message):
    if CAPTURE and live and 'InboundPreview' in live:
        game=editor.get_game_world()
        capture=live['InboundPreview'].get_component_by_class(unreal.SceneCaptureComponent2D)
        capture.capture_scene()
        unreal.RenderingLibrary.export_render_target(game,capture.texture_target,'C:/msc_UnrealProject/msc_vr/Saved/Testing','InboundAreaPreview.png')
    print('INBOUND_NATIVE_PIE_'+('PASSED' if ok else 'FAILED'),message)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    global live,last_log,checked,capacity_checked
    try:
        if time.monotonic()-started>900:raise AssertionError('Wall-clock timeout')
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if not live:live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
        dispatch=live['WH_InboundDispatch' if SAVED else 'InboundTestZone']
        vehicles=[live[n] for n in (['WH_AutonomousForklift','WH_AutonomousForklift_02'] if SAVED else ['InboundTestAGV'])]
        if PREVIEW_ONLY and now>2:
            for label in ('MZ_TransferCargo','WH_TransferCargo_02'):
                item=live[label];part=item.get_component_by_class(unreal.StaticMeshComponent)
                print('INBOUND_CARGO_RENDER',label,item.get_actor_bounds(False),part.static_mesh,
                      part.get_editor_property('visible'),part.get_editor_property('hidden_in_game'),
                      part.get_editor_property('override_materials'))
            finish(True,'Saved warehouse rendering and yellow staging zones only');return
        if not SAVED and now>6 and not capacity_checked:
            assert dispatch.get_editor_property('dispatched_count')==0,'Over-capacity rack dispatched'
            slots=list(dispatch.get_editor_property('rack_slots'));slots[0].capacity_kg=2000;dispatch.set_editor_property('rack_slots',slots)
            capacity_checked=True;print('INBOUND_CAPACITY_GUARD_PASSED')
        if now-last_log>10:
            last_log=now
            print('INBOUND_PROGRESS',round(now,1),dispatch.get_editor_property('status'),[(v.get_editor_property('ai_state'),v.get_actor_location(),v.get_editor_property('status'),
                v.get_editor_property('active_job').pallet.get_actor_location() if v.get_editor_property('active_job').pallet else None,v.get_fork_height_cm(),v.get_actor_rotation(),
                v.get_editor_property('planned_route')[-1] if v.get_editor_property('planned_route') else None) for v in vehicles])
        if now>12 and not checked:
            assert dispatch.get_editor_property('dispatched_count')==1,'First loaded pallet was not scheduled'
            ids=[v.get_editor_property('active_job').job_id for v in vehicles if v.get_editor_property('active_job').job_id]
            assert len(set(ids))==1,'Shared inbound lane received simultaneous jobs'
            for _ in range(3):dispatch.scan_inbound()
            assert dispatch.get_editor_property('dispatched_count')==1,'Duplicate dispatch'
            empty=live['WH_InboundPallet_Empty' if SAVED else 'InboundTestEmpty']
            assert all(v.get_editor_property('active_job').pallet!=empty for v in vehicles),'Empty pallet dispatched'
            print('INBOUND_SCHEDULING_PASSED');checked=True
        for v in vehicles:
            if v.get_editor_property('ai_state')==unreal.WarehouseAIState.FAULT:
                job=v.get_editor_property('active_job');p=job.pallet
                print('INBOUND_FAULT_POSE',p.get_actor_location() if p else None,p.get_actor_rotation() if p else None,job.destination,
                      v.get_actor_location(),v.get_actor_rotation())
            assert not v.get_editor_property('mechanical_failure'),v.get_editor_property('status')
            assert v.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,v.get_editor_property('status')
        if dispatch.get_editor_property('completed_count')==(2 if SAVED else 1):
            for v in vehicles:assert v.get_editor_property('completed_job_ids')
            finish(True,'Real cargo detection -> independent reservations -> physical pickup / beam-supported rack delivery')
        elif now>360:raise AssertionError('Delivery timeout')
    except Exception:
        traceback.print_exc();finish(False,'See traceback')
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
