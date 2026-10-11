"""Reload saved area UI, then check floor/menu visibility and capture real PIE with Slate UI."""
import sys,time,traceback
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from configure_area_labels import AREAS,LEVEL,PREFIX
from warehouse_staging_layout import INBOUND

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level(LEVEL)
saved=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseAreaLabel)]
labels={a.get_actor_label():a for a in saved}
assert len(saved)==len(labels)==len(AREAS)==23,'Missing or duplicate saved area labels'
for key,title,floor,position in AREAS:
    a=labels[PREFIX+key]
    assert str(a.get_editor_property('area_name'))==title
    assert a.get_editor_property('floor_index')==floor
    assert a.actor_has_tag(f'WarehouseFloor{floor}')
    assert (a.get_actor_location()-unreal.Vector(*position)).length()<.1
    assert not a.get_actor_enable_collision()
    widget=a.get_component_by_class(unreal.WidgetComponent)
    assert widget.get_editor_property('space')==unreal.WidgetSpace.SCREEN
    assert widget.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION
print('AREA_UI_DISK_PASSED',len(labels),'saved Korean labels / floor tags / positions / no collision')

# Preview helpers exist only in this unsaved editor session.
camera=actors.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(400,-4700,7000))
camera.set_actor_label('AreaUICaptureCamera')
camera.get_component_by_class(unreal.CameraComponent).set_editor_property('field_of_view',55.)
wall=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-10000))
wall.set_actor_label('AreaUIOcclusionProbe')
wall.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
wall.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
wall.static_mesh_component.set_collision_profile_name('BlockAll')
Path('C:/msc_UnrealProject/msc_vr/Saved/Testing').mkdir(exist_ok=True)
started=time.monotonic();phase=0;phase_at=0.;captured=False;pc=None;game=None;live=[]

def finish(ok):
    print('AREA_UI_PIE_PASSED' if ok else 'AREA_UI_PIE_FAILED')
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def shot(name):
    unreal.SystemLibrary.execute_console_command(game,
        f'Shot showui filename=C:/msc_UnrealProject/msc_vr/Saved/Testing/{name}.png -nosuffix',pc)

def visible():
    return [a for a in live if a.get_component_by_class(unreal.WidgetComponent).is_visible()]

def frame(floor):
    pc.set_observer_floor(floor)
    z=floor*400
    eye=unreal.Vector(400,-2900,8500+z)
    camera.set_actor_location(eye,False,True)
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye,unreal.Vector(400,0,z)),True)
    pc.set_view_target_with_blend(camera,0.)

def tick(dt):
    global phase,phase_at,captured,pc,game,camera,wall,live
    try:
        assert time.monotonic()-started<240,'Area UI PIE timeout'
        if phase==0:
            if time.monotonic()-started<3:return
            level.editor_request_begin_play();phase=1;return
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase==1:
            if now<2:return
            pc=unreal.GameplayStatics.get_player_controller(game,0)
            unreal.SystemLibrary.execute_console_command(game,'DisableAllScreenMessages',pc)
            live=list(unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseAreaLabel))
            assert len(live)==23
            camera=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.CameraActor) if a.get_actor_label()=='AreaUICaptureCamera')
            wall=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.StaticMeshActor) if a.get_actor_label()=='AreaUIOcclusionProbe')
            frame(0);phase_at=now;phase=2;return
        if now-phase_at<1.5:return
        if phase in (2,3,4):
            if captured:
                captured=False;phase+=1;phase_at=now
                if phase<=4:frame(phase-2)
                return
            floor=phase-2
            shown=visible()
            assert shown and all(a.get_editor_property('floor_index')==floor for a in shown),'Mixed floor labels'
            assert len(shown)>=(13 if floor==0 else 4),f'Too few visible labels on floor {floor+1}: {len(shown)}'
            workers=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseWorker)
            assert not any(a.get_component_by_class(unreal.WidgetComponent).is_visible() for a in workers),'Duplicate role labels clutter the overview'
            print('AREA_UI_FLOOR_PASSED',floor+1,len(shown),[str(a.get_editor_property('area_name')) for a in shown])
            shot(f'AreaUI_Floor{floor+1}')
            phase_at=now;captured=True
            return
        if phase==5:
            pc.toggle_warehouse_menu();phase_at=now;phase=6;return
        if phase==6:
            assert pc.is_warehouse_menu_open() and not visible(),'Area UI drawn over settings menu'
            workers=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseWorker)
            assert not any(a.get_component_by_class(unreal.WidgetComponent).is_visible() for a in workers),'Worker UI drawn over settings menu'
            shot('AreaUI_Menu')
            phase_at=now;phase=7;return
        if phase==7:
            pc.toggle_warehouse_menu();pc.toggle_observer_view()
            eye=unreal.Vector(INBOUND[0]-260,INBOUND[1]-180,170)
            target=next(a for a in live if a.get_actor_label()==PREFIX+'Inbound')
            camera.set_actor_location(eye,False,True)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye,target.get_actor_location()),True)
            pc.set_view_target_with_blend(camera,0.)
            phase_at=now;phase=8;return
        if phase==8:
            target=next(a for a in live if a.get_actor_label()==PREFIX+'Inbound')
            assert target in visible(),'Nearby first-person area label hidden'
            workers=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseWorker)
            assert any(a.get_component_by_class(unreal.WidgetComponent).is_visible() for a in workers),'Role labels did not return outside observer mode'
            shot('AreaUI_Inbound')
            phase_at=now;phase=9;return
        if phase==9:
            target=next(a for a in live if a.get_actor_label()==PREFIX+'Inbound')
            eye=unreal.GameplayStatics.get_player_camera_manager(game,0).get_camera_location()
            assert wall.set_actor_location((eye+target.get_actor_location())*.5,False,True),'Probe wall could not move'
            phase_at=now;phase=10;return
        if phase==10:
            target=next(a for a in live if a.get_actor_label()==PREFIX+'Inbound')
            assert target not in visible(),'Label shown through blocking wall'
            wall.set_actor_location(unreal.Vector(0,0,-10000),False,True)
            phase_at=now;phase=11;return
        if phase==11:
            target=next(a for a in live if a.get_actor_label()==PREFIX+'Inbound')
            assert target in visible(),'Label did not return after wall cleared'
            pc.set_view_target_with_blend(pc.get_controlled_pawn(),0.)
            print('AREA_UI_MENU_AND_OCCLUSION_PASSED')
            finish(True)
    except Exception:
        print(traceback.format_exc());finish(False)

handle=unreal.register_slate_post_tick_callback(tick)
