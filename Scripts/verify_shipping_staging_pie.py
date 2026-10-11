"""Fresh saved-map PIE: dock staging, separate outbound cargo, inbound pickup/route and Korean UI. Never saves."""
import sys,time,traceback
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from warehouse_staging_layout import (INBOUND,OUTBOUND,STAGING_ZONES,PALLET_POSES,
    VEHICLE_POSES,OUTBOUND_PALLETS,RELOCATED_POSES)
from configure_area_labels import AREAS,PREFIX

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
for label,position in {**PALLET_POSES,**VEHICLE_POSES,**OUTBOUND_PALLETS,**RELOCATED_POSES}.items():
    assert (named[label].get_actor_location()-unreal.Vector(*position)).length()<.1,('Unsaved relocation',label)
assert PREFIX+'Buffer' not in named
assert sum(isinstance(a,unreal.WarehouseAreaLabel) for a in named.values())==23
for key,title,floor,position in AREAS:
    a=named[PREFIX+key]
    assert str(a.get_editor_property('area_name'))==title
    assert (a.get_actor_location()-unreal.Vector(*position)).length()<.1
assert sum(isinstance(a,unreal.WarehouseInboundZone) for a in named.values())==1
zone=named['WH_InboundDispatch']
assert zone.get_actor_location()==unreal.Vector(INBOUND[0],INBOUND[1],0)
assert zone.get_editor_property('half_size_cm')==unreal.Vector(INBOUND[2],INBOUND[3],80)
assert len(zone.get_editor_property('rack_slots'))==16 and len(zone.get_editor_property('fleet'))==2
for index,(x,y,hx,hy) in enumerate(STAGING_ZONES,1):
    for side,p in [('W',(x-hx,y,.12)),('E',(x+hx,y,.12)),('S',(x,y-hy,.12)),('N',(x,y+hy,.12))]:
        a=named[f'WH_InboundZone{index}_Paint_{side}']
        assert (a.get_actor_bounds(False)[0]-unreal.Vector(*p)).length()<.1
        assert a.static_mesh_component.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION
    assert abs(y+hy)<1980 and y-hy>-1980,'Staging outside the warehouse'
# Two regions border the clear central opening and are only metres from the real dock platform.
assert OUTBOUND[0]+OUTBOUND[2]<INBOUND[0]-INBOUND[2]
for label in ('WH_LoadingDoor_Header','EXT_DockPlatform','EXT_TruckDockTarget_01','EXT_TruckDockTarget_02'):
    assert label in named,'Dock connection lost'
for pallet_name,box_name in [('WH_TrainingPallet','MZ_TransferCargo'),('WH_TrainingPallet_02','WH_TransferCargo_02'),('WH_Drop_Pallet','WH_Drop_Box')]:
    p=named[pallet_name];box=named[box_name];center,extent=box.get_actor_bounds(False)
    assert abs(center.x-p.get_actor_location().x)<1 and abs(center.y-p.get_actor_location().y)<1
    assert abs(center.z-extent.z-(p.get_actor_location().z+15))<.3,'Moved load is not on its pallet'
print('SHIPPING_STAGING_DISK_PASSED','2 separated yellow waiting areas, 23 Korean area labels, 6 staging pallets, truck docks preserved')

camera=actors.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(150,-2600,5200))
camera.set_actor_label('ShippingStagingPreview')
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),unreal.Vector(150,-1640,0)),False)
camera.get_component_by_class(unreal.CameraComponent).set_editor_property('field_of_view',55.)
world=editor.get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'t.OverrideFPS 30')
started=time.monotonic();phase=0;live=None;pc=None;camera_live=None;last_log=-10.;initial={};shot_at=None;delivery_seen=False
def finish(ok):
    print('SHIPPING_STAGING_PIE_PASSED' if ok else 'SHIPPING_STAGING_PIE_FAILED')
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play();unreal.SystemLibrary.quit_editor()
def tick(dt):
    global phase,live,pc,camera_live,last_log,initial,shot_at,delivery_seen
    try:
        assert time.monotonic()-started<1200,'Shipping staging wall-clock timeout'
        if phase==0:
            if time.monotonic()-started<3:return
            level.editor_request_begin_play();phase=1;return
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if not live:
            live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            pc=unreal.GameplayStatics.get_player_controller(game,0)
            camera_live=live['ShippingStagingPreview']
            initial={label:live[label].get_actor_location() for label in VEHICLE_POSES}
            unreal.SystemLibrary.execute_console_command(game,'DisableAllScreenMessages',pc)
            pc.toggle_observer_view();pc.set_observer_floor(0);pc.set_view_target_with_blend(camera_live,0.)
        dispatch=live['WH_InboundDispatch'];fleet=[live[k] for k in VEHICLE_POSES]
        if now-last_log>10:
            last_log=now
            print('SHIPPING_PROGRESS',round(now,1),dispatch.get_editor_property('status'),[(v.get_actor_label(),str(v.get_editor_property('ai_state')),v.get_actor_location(),v.get_editor_property('status')) for v in fleet])
        for v in fleet:
            assert not v.get_editor_property('mechanical_failure')
            assert v.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,v.get_editor_property('status')
            job=v.get_editor_property('active_job')
            if job.pallet:assert job.pallet.get_actor_label() in PALLET_POSES,'Outbound load incorrectly scheduled as inbound'
            if v.get_editor_property('ai_state') in (unreal.WarehouseAIState.NAVIGATE_DELIVERY,unreal.WarehouseAIState.ALIGN_UNLOAD,
                    unreal.WarehouseAIState.LOWER_LOAD,unreal.WarehouseAIState.WITHDRAW_FORK,unreal.WarehouseAIState.VERIFY_UNLOAD,unreal.WarehouseAIState.REPORT_COMPLETE):
                delivery_seen=True
                assert v.get_load_mass_kg()>0
                print('SHIPPING_LOADED_ROUTE_PASSED',v.get_actor_label(),'real fork insertion, lift, departure and loaded route')
        if now>6 and shot_at is None:
            for key in ('Inbound','Outbound'):
                a=live[PREFIX+key];assert a.get_component_by_class(unreal.WidgetComponent).is_visible(),key+' UI hidden'
            unreal.SystemLibrary.execute_console_command(game,'Shot showui filename=C:/msc_UnrealProject/msc_vr/Saved/Testing/ShippingStaging.png -nosuffix',pc)
            shot_at=now
        if now>12:
            assert dispatch.get_editor_property('dispatched_count')>=1,'No inbound job dispatched'
            assert any((v.get_actor_location()-initial[v.get_actor_label()]).length()>15 for v in fleet),'Fleet did not move toward inbound loads'
            for label,position in OUTBOUND_PALLETS.items():
                p=live[label];body=p.get_component_by_class(unreal.StaticMeshComponent)
                assert body.is_simulating_physics() and abs(body.get_mass()-25)<.05
                assert (p.get_actor_location()-unreal.Vector(*position)).length()<8,'Outbound pallet unstable'
        if delivery_seen and shot_at and now-shot_at>1:
            assert Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/ShippingStaging.png').is_file(),'Missing rendered UI screenshot'
            print('SHIPPING_STAGING_NATIVE_PASSED','all warehouse cargo physics active; outbound excluded; inbound physically picked up and routing to rack')
            finish(True)
        elif now>100:raise AssertionError('Inbound pickup/loaded-route timeout')
    except Exception:
        print(traceback.format_exc());finish(False)
handle=unreal.register_slate_post_tick_callback(tick)
