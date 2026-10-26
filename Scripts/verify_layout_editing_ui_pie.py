"""Render real editing/held inventory screens; native PIE APIs, no map save."""
import json,time,traceback,unreal
from pathlib import Path

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
for actor in actors.get_all_level_actors():
    if isinstance(actor,unreal.WarehouseForklift):actor.set_editor_property('auto_start',False)
# An isolated authored pad gives the Add/Move/Delete check a known free bearing;
# neither the saved warehouse nor runtime cargo is moved to make the check pass.
pad=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(20000,0,-10))
pad.set_actor_label('ObserverToolbarConveyorTestPad')
pad.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube.Cube'))
pad.static_mesh_component.set_collision_profile_name('BlockAll')
pad.set_actor_scale3d(unreal.Vector(20,20,.2))
pad.tags=['WarehouseMapStructure','LayoutFixed']
test_camera=actors.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(19400,-700,650))
test_camera.tags=['ObserverToolbarTestCamera']
test_camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(test_camera.get_actor_location(),unreal.Vector(20100,0,40)),False)
output=Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/LayoutEditingUI');output.mkdir(parents=True,exist_ok=True)
screens=('Move','Clean','Floors','RotationMenu','Delete','Add','Conveyor','Computer')
for name in screens:
    (output/(name+'.png')).unlink(missing_ok=True)
phase='start';started=at=time.monotonic();pc=layout=computer=game=None
rotation_result=None
checks=[]
unpaused_at=0
conveyor=None

def finish(ok,detail):
    (output/'Report.json').write_text(json.dumps(dict(ok=ok,detail=detail,rotation_accepted=rotation_result,
        checks=checks, screenshots=list(screens), osInputTested=False,
        scope='Actual game SViewport Slate and public native editing API; unsaved warehouse plus isolated test pad; no OS mouse input claim'),ensure_ascii=False,indent=2),encoding='utf-8')
    print('LAYOUT_UI_PASSED' if ok else 'LAYOUT_UI_FAILED',detail)
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play()
    if '-KeepTestEditorOpen' not in unreal.SystemLibrary.get_command_line():unreal.SystemLibrary.quit_editor()

def shot(name):
    # Capture the game's own Slate viewport, not the surrounding editor window.
    unreal.load_module('AutomationUtils')
    capture_name=Path(name).stem+'_'+str(time.time_ns())
    incoming=Path(unreal.Paths.project_saved_dir())/'Automation/Incoming/LayoutEditingUI'/capture_name
    options=unreal.AutomationScreenshotOptions()
    options.set_editor_property('disable_noisy_rendering_features',False)
    options.set_editor_property('disable_tonemapping',False)
    unreal.AutomationUtilsBlueprintLibrary.take_gameplay_automation_screenshot(capture_name,map_name_override='LayoutEditingUI')
    unreal.AutomationLibrary.take_automation_screenshot_of_ui(game,unreal.LatentActionInfo(),capture_name,options)
    images=list(incoming.rglob('*.png'))
    assert len(images)==1,('No unique actual UI screenshot',name)
    data=images[0].read_bytes()
    assert len(data)>1000 and data[:8]==b'\x89PNG\r\n\x1a\n' and data[-8:-4]==b'IEND',name
    (output/name).write_bytes(data)

def _tick_impl(dt):
    global phase,at,pc,layout,computer,game,rotation_result,unpaused_at,conveyor
    try:
        assert time.monotonic()-started<420
        if phase=='start':level.editor_request_begin_play();phase='settle';return
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase=='settle':
            if now<3:return
            pc=unreal.GameplayStatics.get_player_controller(game,0)
            computer=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseComputer)[0]
            assert computer.create_held_cargo('UI-HELD-001','시각 검사 상품',12,unreal.Vector(40,30,25))
            assert computer.create_held_cargo('UI-HELD-002','두 번째 상품',20,unreal.Vector(50,40,30))
            unreal.SystemLibrary.execute_console_command(game,'r.SetRes 1280x720w',pc)
            unreal.SystemLibrary.execute_console_command(game,'DisableAllScreenMessages',pc)
            assert not pc.is_observer_view()
            pc.toggle_observer_view()
            layout=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseLayoutEditor)[0]
            assert pc.is_observer_view() and pc.is_layout_editing() and layout.get_editor_property('editing')
            assert pc.get_editor_property('observer_floor')==0 and unreal.GameplayStatics.is_game_paused(game)
            checks.append('Observer entry defaults to paused first-floor editing')
            pc.set_layout_category(9)
            assert layout.get_editor_property('category')==unreal.WarehouseLayoutCategory.ALL
            at=time.monotonic();phase='move';return
        if time.monotonic()-at<1:return
        if phase=='move':
            shot('Move.png')
            pc.set_layout_editing(False)
            assert pc.is_observer_view() and not pc.is_layout_editing() and not unreal.GameplayStatics.is_game_paused(game)
            unpaused_at=now;phase='clean'
        elif phase=='clean':
            assert now>unpaused_at+.001,'Clean observation did not resume simulation'
            shot('Clean.png')
            pc.set_layout_editing(True)
            assert pc.is_layout_editing() and unreal.GameplayStatics.is_game_paused(game)
            for floor in [1,2,3,0]:
                pc.set_observer_floor(floor)
                assert pc.get_editor_property('observer_floor')==floor and pc.is_layout_editing() and unreal.GameplayStatics.is_game_paused(game)
            pc.set_observer_floor(2);pc.set_observer_floor(-1)
            assert not pc.is_layout_editing() and not unreal.GameplayStatics.is_game_paused(game)
            pc.set_layout_editing(True)
            assert pc.get_editor_property('observer_floor')==2 and pc.is_layout_editing()
            pc.set_observer_floor(1)
            checks.append('Clean view resumes, floors 1-4 preserve editing, All resumes, re-enable restores last floor')
            phase='floors'
        elif phase=='floors':
            shot('Floors.png');pc.set_observer_floor(0);phase='select_rotation'
        elif phase=='select_rotation':
            candidates=[a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehousePallet)
                        if a.get_actor_location().z<2 and a.get_editor_property('payload_mass_kg')<=0]
            assert candidates,'No ground pallet for context-menu selection'
            item=candidates[0];before=item.get_actor_transform()
            layout.set_selection([item])
            assert layout.begin_drag(item,item.get_actor_location()),layout.get_editor_property('status')
            layout.update_drag(item.get_actor_location()+unreal.Vector(30,20,0))
            pc.set_layout_editing(False)
            assert not layout.get_editor_property('dragging') and not layout.get_selection()
            assert (item.get_actor_location()-before.translation).length()<.1
            pc.set_layout_editing(True)
            pc.set_construction_editing(True)
            assert pc.get_construction_editor().get_editor_property('active')
            pc.set_layout_editing(False)
            assert not pc.get_construction_editor().get_editor_property('active')
            pc.set_layout_editing(True)
            layout.set_selection([item]);pc.set_observer_floor(1)
            assert not layout.get_selection() and not layout.get_editor_property('dragging')
            pc.set_observer_floor(0)
            checks.append('Turning editing off cancels native drag and building preview; floor changes clear selection')
            layout.set_selection(candidates[:2]);assert layout.get_editor_property('selection_count')>0
            assert layout.can_rotate_selection()
            assert pc.open_layout_rotation_menu(unreal.Vector2D(640,220))
            phase='rotation_menu'
        elif phase=='rotation_menu':shot('RotationMenu.png');phase='apply_rotation'
        elif phase=='apply_rotation':
            selected=layout.get_selection();poses=[a.get_actor_transform() for a in selected]
            rotation_result=pc.rotate_layout_selection(-90)
            if rotation_result:
                assert pc.rotate_layout_selection(90),'Inverse rotation failed in the unchanged scene'
            for a,before in zip(selected,poses):
                assert (a.get_actor_location()-before.translation).length()<.1
                assert abs((a.get_actor_rotation().yaw-before.rotation.rotator().yaw+180)%360-180)<.1
            paths={a.get_path_name() for a in selected}
            assert pc.open_layout_rotation_menu(unreal.Vector2D(640,220))
            assert pc.delete_layout_selection(),layout.get_editor_property('status')
            assert not paths & {a.get_path_name() for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            assert not layout.get_selection() and not pc.open_layout_rotation_menu(unreal.Vector2D(640,220))
            phase='set_delete'
        elif phase=='set_delete':
            pc.set_layout_action(1);pc.set_layout_category(9)
            assert layout.get_editor_property('category')==unreal.WarehouseLayoutCategory.ALL
            phase='delete'
        elif phase=='delete':shot('Delete.png');phase='set_add'
        elif phase=='set_add':
            pc.set_layout_action(2)
            assert layout.get_editor_property('category')!=unreal.WarehouseLayoutCategory.ALL
            assert not pc.open_layout_rotation_menu(unreal.Vector2D(640,220))
            layout.set_category(unreal.WarehouseLayoutCategory.CARGO);phase='add'
        elif phase=='add':
            shot('Add.png')
            pc.set_layout_category(10);pc.set_layout_action(2)
            assert layout.begin_add(unreal.Vector(20000,0,0)),layout.get_editor_property('status')
            assert layout.end_drag(),layout.get_editor_property('status')
            conveyor=layout.get_selected_conveyor();assert conveyor
            pc.set_layout_action(0);layout.set_selection([conveyor])
            assert pc.configure_layout_conveyor(0,30),layout.get_editor_property('status')
            assert pc.configure_layout_conveyor(2,100),layout.get_editor_property('status')
            assert pc.configure_layout_conveyor(1,5),layout.get_editor_property('status')
            assert pc.configure_layout_conveyor(1,0),layout.get_editor_property('status')
            assert pc.configure_layout_conveyor(2,0),layout.get_editor_property('status')
            start=conveyor.get_actor_location()
            assert layout.begin_drag(conveyor,start)
            layout.update_drag(start+unreal.Vector(100,0,0))
            assert layout.end_drag(),layout.get_editor_property('status')
            assert (conveyor.get_actor_location()-start-unreal.Vector(100,0,0)).length()<.1
            assert pc.rotate_layout_selection(90),layout.get_editor_property('status')
            camera=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.CameraActor) if a.actor_has_tag('ObserverToolbarTestCamera'))
            pc.set_view_target_with_blend(camera,0.0)
            phase='conveyor'
        elif phase=='conveyor':
            shot('Conveyor.png')
            assert pc.delete_layout_selection(),layout.get_editor_property('status')
            checks.append('Bottom conveyor category adds, edits yaw/incline, moves, rotates and deletes a physical conveyor')
            phase='leave'
        elif phase=='leave':
            pc.set_layout_editing(False);pc.set_observer_floor(-1);pc.toggle_observer_view()
            assert not pc.is_observer_view() and not pc.is_layout_editing() and not unreal.GameplayStatics.is_game_paused(game)
            assert not pc.get_editor_property('show_mouse_cursor')
            checks.append('Returning to first person hides observer controls and restores cursor/input state')
            phase='open'
        elif phase=='open':
            pawn=unreal.GameplayStatics.get_player_pawn(game,0)
            pawn.set_actor_location(computer.get_actor_location()-computer.get_actor_forward_vector()*210+unreal.Vector(0,0,96.5),False,True)
            camera=pawn.get_components_by_class(unreal.CameraComponent)[0]
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_world_location(),computer.get_actor_location()+unreal.Vector(0,0,111)))
            phase='aim'
        elif phase=='aim':
            assert pc.open_warehouse_computer(computer)
            assert pc.is_warehouse_computer_open() and not unreal.GameplayStatics.is_game_paused(game)
            phase='computer'
        elif phase=='computer':shot('Computer.png');phase='finish'
        elif phase=='finish':
            assert all((output/(name+'.png')).exists() for name in screens)
            pc.close_warehouse_computer();assert not pc.is_warehouse_computer_open()
            finish(True,'Observer default editing, clean view, floor toolbar, drag/preview cancellation, conveyor Add/Move/Rotate/Edit/Delete and existing context/FIFO/console workflow; actual Slate images, no OS-input claim')
        at=time.monotonic()
    except Exception:finish(False,traceback.format_exc())

_callback_busy=False


def tick(dt):
    # Native loads, geometry edits and synchronous Slate screenshots can pump
    # Slate again. Each transaction must still run only once.
    global _callback_busy
    if _callback_busy:return
    _callback_busy=True
    try:return _tick_impl(dt)
    finally:_callback_busy=False


handle=unreal.register_slate_post_tick_callback(tick)
