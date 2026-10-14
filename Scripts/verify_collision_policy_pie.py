"""Common collision roles, actual floor/picking and layout regression. Never saves."""
import collections, json, time, traceback
from pathlib import Path
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.WarehouseForklift):
        actor.set_editor_property('auto_start', False)
        actor.set_editor_property('idle_parking_enabled', False)

cube = unreal.load_asset('/Engine/BasicShapes/Cube')
def block(label, location, scale, profile):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location))
    actor.set_actor_label(label)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_collision_profile_name(profile)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor
block('PolicyTestFloor', (12000,12000,-10), (12,12,.2), 'BlockAll')
visual = block('PolicyTestVisual', (12000,12000,100), (.2,.2,.2), 'BlockAll')
visual.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
block('PolicyTestTrigger', (12300,12300,100), (.2,.2,.2), 'Trigger')
bay = actors.spawn_actor_from_class(unreal.WarehouseWaitingBay, unreal.Vector(12000,12000,0))
bay.set_actor_label('PolicyTestWaitingBay')
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(12000,12000,2000), unreal.Rotator(pitch=-90))
camera.set_actor_label('PolicyTestCamera')

start = time.monotonic()
phase = 0
world = layout = pc = None
camera_at = None

def trace(x, y):
    result = unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,220),unreal.Vector(x,y,-20),
        unreal.TraceTypeQuery.ECC_VISIBILITY,False,[],unreal.DrawDebugTrace.NONE,True)
    hit = result if isinstance(result,unreal.HitResult) else next(v for v in result if isinstance(v,unreal.HitResult))
    values = hit.to_tuple()
    assert values[0], 'No actual floor found'
    return values[9]

def finish(ok, detail):
    print('COLLISION_POLICY_PIE_PASSED' if ok else 'COLLISION_POLICY_PIE_FAILED',detail)
    if layout and layout.get_editor_property('editing'): layout.set_editing(False,0)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(dt):
    global phase, world, layout, pc, camera_at
    try:
        assert time.monotonic()-start < 180
        world = editor.get_game_world()
        if not world: return
        live = list(unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor))
        named = {a.get_actor_label():a for a in live}
        if phase == 0:
            if unreal.GameplayStatics.get_time_seconds(world)<3: return
            for actor in live:
                if isinstance(actor,unreal.WarehouseInboundZone):actor.set_actor_tick_enabled(False)
            layout = next(a for a in live if isinstance(a,unreal.WarehouseLayoutEditor))
            layout.set_editing(True,0)
            rows=[]
            channels={'visibility':unreal.CollisionChannel.ECC_VISIBILITY,'pawn':unreal.CollisionChannel.ECC_PAWN,
                'vehicle':unreal.CollisionChannel.ECC_VEHICLE,'body':unreal.CollisionChannel.ECC_PHYSICS_BODY,
                'static':unreal.CollisionChannel.ECC_WORLD_STATIC,'dynamic':unreal.CollisionChannel.ECC_WORLD_DYNAMIC}
            for actor in live:
                for part in actor.get_components_by_class(unreal.PrimitiveComponent):
                    physical=unreal.WarehouseDamageSystem.is_physical_contact(part)
                    mode=part.get_collision_enabled()
                    # Hidden custom channel enums are not exported by Unreal Python.
                    # Test the actual dedicated pick via PointerDown below instead.
                    selection=isinstance(actor,unreal.WarehouseWaitingBay) and part.get_name()=='ParkingSelection'
                    role='physical' if physical else 'selection' if selection else 'nonphysical'
                    responses={k:str(part.get_collision_response_to_channel(v)) for k,v in channels.items()}
                    if physical:
                        assert part.is_physics_collision_enabled(),(actor.get_actor_label(),part.get_name())
                        assert all('BLOCK' in v for v in responses.values()),(actor.get_actor_label(),part.get_name(),responses)
                    if mode in (unreal.CollisionEnabled.NO_COLLISION,unreal.CollisionEnabled.QUERY_ONLY):
                        assert not physical,(actor.get_actor_label(),part.get_name(),'nonphysical mode promoted')
                    rows.append(dict(actor=actor.get_actor_label(),component=part.get_name(),role=role,
                        enabled=str(mode),profile=str(part.get_collision_profile_name()),responses=responses))
            report=Path(unreal.Paths.project_saved_dir())/'Testing'/'CollisionPolicyManifest_20261014.json'
            report.write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
            print('COLLISION_POLICY_ALL_COMPONENTS_PASSED',len(rows),dict(collections.Counter(r['role'] for r in rows)))
            for bay in [a for a in live if isinstance(a,unreal.WarehouseWaitingBay)]:
                for part in bay.get_components_by_class(unreal.StaticMeshComponent):
                    assert not unreal.WarehouseDamageSystem.is_physical_contact(part)
                    if part.get_name()=='ParkingSelection':
                        assert part.get_collision_enabled()==unreal.CollisionEnabled.QUERY_ONLY
                        assert all(part.get_collision_response_to_channel(v)==unreal.CollisionResponseType.ECR_IGNORE for v in channels.values())
                    else: assert part.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION
            assert trace(12000,12000)==named['PolicyTestFloor'],'Selection or visual mesh became a fake floor'
            assert trace(12300,12300)==named['PolicyTestFloor'],'Trigger became a fake floor'
            print('COLLISION_POLICY_REAL_FLOOR_MARKER_TRIGGER_PASSED')
            pallet=named['WH_TrainingPallet']
            frame=unreal.MathLibrary.compose_transforms(unreal.Transform(location=unreal.Vector(-60,0,0)),pallet.get_actor_transform())
            assert pallet.can_engage(frame), 'Native pallet fixture cannot support correctly inserted forks'
            body=pallet.get_component_by_class(unreal.StaticMeshComponent)
            mode=body.get_collision_enabled()
            body.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            try:
                assert not unreal.WarehouseDamageSystem.is_physical_contact(body)
                assert not pallet.can_engage(frame), 'Direct mesh query treated a disabled pallet as a physical support'
            finally: body.set_collision_enabled(mode)
            print('COLLISION_POLICY_DISABLED_PALLET_DIRECT_QUERY_PASSED')
            vehicle=next(a for a in live if isinstance(a,unreal.WarehouseForklift))
            point=vehicle.get_actor_location()
            layout.set_editor_property('edge_snap_enabled',False)
            layout.set_category(unreal.WarehouseLayoutCategory.FORKLIFT)
            assert layout.begin_drag(vehicle,point)
            layout.update_drag(unreal.Vector(12000,12000,point.z))
            assert layout.end_drag(),layout.get_editor_property('status')
            moved=vehicle.get_actor_location()
            assert layout.begin_drag(vehicle,moved)
            layout.update_drag(point)
            assert layout.end_drag(),layout.get_editor_property('status')
            assert (vehicle.get_actor_location()-point).length()<.1
            assert vehicle.root_component.is_simulating_physics()
            print('COLLISION_POLICY_VEHICLE_LAYOUT_ON_MARKER_PASSED')
            layout.set_category(unreal.WarehouseLayoutCategory.WAITING_BAY)
            pc=unreal.GameplayStatics.get_player_controller(world,0)
            pc.set_view_target_with_blend(named['PolicyTestCamera'],0.)
            camera_at=time.monotonic();phase=1
            return
        if time.monotonic()-camera_at<.3: return
        result=pc.project_world_location_to_screen(unreal.Vector(12000,12000,.2),False)
        pixel=result if isinstance(result,unreal.Vector2D) else next(v for v in result if isinstance(v,unreal.Vector2D))
        layout.pointer_down(pc,pixel,False)
        assert layout.get_selection()==[named['PolicyTestWaitingBay']], 'Dedicated pick channel could not select a nonphysical waiting bay'
        layout.pointer_up(False)
        print('COLLISION_POLICY_NONPHYSICAL_NATIVE_PICK_PASSED')
        finish(True,'Saved native component roles; real floor, paint/selection/trigger exclusions; forklift relocation; dedicated camera-projected pick; no map saved')
    except Exception: finish(False,traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
