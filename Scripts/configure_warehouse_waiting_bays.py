"""Add two ordinary parking bays beside the shared charging area; preserve the existing fleet/jobs."""
import time,traceback,unreal

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not editor.get_game_world()
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
vehicles=sorted([a for a in named.values() if isinstance(a,unreal.WarehouseForklift)],key=lambda a:a.get_actor_label())
bays=[]
for index,y in enumerate((650,1150),1):
    label='WH_WaitingBay_%02d'%index
    bay=named.get(label) or actors.spawn_actor_from_class(unreal.WarehouseWaitingBay,unreal.Vector(2250,y,0))
    bay.modify();bay.set_actor_label(label);bay.set_editor_property('is_spatially_loaded',False)
    bay.set_editor_property('bay_name','일반 대기 자리 %02d'%index)
    bay.tags=list(dict.fromkeys([str(t) for t in bay.tags]+['WarehouseFloor0','LayoutType:WaitingBay']))
    bay.set_folder_path('Warehouse/Parking');bays.append(bay)
for index,vehicle in enumerate(vehicles):
    vehicle.modify();vehicle.set_editor_property('waiting_bay',bays[index%len(bays)])
    # The second charging cabinet blocks the straight approach from charger 1.
    # Author the free lane beside the cabinets as map data, keeping old lanes.
    anchors=list(vehicle.get_editor_property('navigation_anchors'))
    for y in (-640,-140,300,650,1150):
        pose=unreal.Transform(location=unreal.Vector(2050,y,0),rotation=unreal.Rotator(yaw=90))
        if not any((a.translation-pose.translation).length()<1 and abs(a.rotation.rotator().yaw-90)<.1 for a in anchors):anchors.append(pose)
    vehicle.set_editor_property('navigation_anchors',anchors)
layout=named['WH_LayoutEditor'];layout.modify()
zones=list(layout.get_editor_property('zones'))
for zone in zones:
    if zone.name=='Outbound':zone.destinations=list(named['WH_ManagementComputer'].get_editor_property('outbound_destinations'))
layout.set_editor_property('zones',zones)
auto_start={v:v.get_editor_property('auto_start') for v in vehicles}
for vehicle in vehicles:vehicle.set_editor_property('auto_start',False)
phase='start';started=time.monotonic()

def hits(value):
    if isinstance(value,unreal.HitResult):yield value.to_tuple()
    elif isinstance(value,(list,tuple,unreal.Array)):
        for item in value:yield from hits(item)

def preflight(dt):
    global phase
    try:
        assert time.monotonic()-started<240,'Parking preflight timeout'
        if phase=='start':level.editor_request_begin_play();phase='physics';return
        game=editor.get_game_world()
        if phase=='physics':
            if not game or unreal.GameplayStatics.get_time_seconds(game)<.5:return
            live_bays=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseWaitingBay)
            for bay in live_bays:
                p=bay.get_actor_location();size=bay.get_editor_property('size_cm')
                centre=p+unreal.Vector(0,0,110)
                result=unreal.SystemLibrary.box_trace_multi(game,centre,centre+unreal.Vector(0,0,.01),
                    unreal.Vector(size.x*.5,size.y*.5,100),bay.get_actor_rotation(),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                    False,live_bays,unreal.DrawDebugTrace.NONE,False)
                blocked=[h[9].get_actor_label() for h in hits(result) if h[0] and h[9]]
                assert not blocked,('Parking space obstructed; not saving',bay.get_actor_label(),blocked)
                for x in (-size.x*.45,size.x*.45):
                    for y in (-size.y*.45,size.y*.45):
                        probe=p+unreal.Vector(x,y,3)
                        result=unreal.SystemLibrary.line_trace_single(game,probe,probe-unreal.Vector(0,0,15),
                            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,live_bays,unreal.DrawDebugTrace.NONE,False)
                        assert any(h[0] and h[7].z>.95 for h in hits(result)),('Parking floor missing',bay.get_actor_label(),probe)
            print('WAITING_BAYS_NATIVE_CLEARANCE_PASSED',len(live_bays))
            level.editor_request_end_play();phase='save';return
        if phase=='save':
            if game:return
            # Saving may pump Slate; stop this callback before any save dialog.
            phase='saving';unreal.unregister_slate_post_tick_callback(handle)
            for vehicle,enabled in auto_start.items():vehicle.set_editor_property('auto_start',enabled)
            assert unreal.EditorLoadingAndSavingUtils.save_map(editor.get_editor_world(),'/Game/FirstPerson/Lvl_FirstPerson')
            assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False,True)
            print('WAREHOUSE_WAITING_BAYS_SAVED','2 ordinary waiting bays; existing fleet, chargers, jobs and geometry retained')
            unreal.SystemLibrary.quit_editor()
    except Exception:
        print('WAITING_BAYS_PREFLIGHT_FAILED',traceback.format_exc())
        if phase!='saving':unreal.unregister_slate_post_tick_callback(handle)
        phase='failed';level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

handle=unreal.register_slate_post_tick_callback(preflight)
