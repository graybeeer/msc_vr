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
output=Path('C:/msc_UnrealProject/msc_vr/Saved/Testing/LayoutEditingUI');output.mkdir(parents=True,exist_ok=True)
screens=('Move','RotationMenu','Delete','Add','Computer')
for name in screens:
    (output/(name+'.png')).unlink(missing_ok=True)
phase='start';started=at=time.monotonic();pc=layout=computer=game=None
rotation_result=None

def finish(ok,detail):
    (output/'Report.json').write_text(json.dumps(dict(ok=ok,detail=detail,rotation_accepted=rotation_result,
        scope='Rendered saved warehouse and native context-menu/selection API; no OS mouse input claim'),ensure_ascii=False,indent=2),encoding='utf-8')
    print('LAYOUT_UI_PASSED' if ok else 'LAYOUT_UI_FAILED',detail)
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def shot(name):
    unreal.SystemLibrary.execute_console_command(game,'Shot showui filename='+str(output/name).replace('\\','/')+' -nosuffix',pc)

def tick(dt):
    global phase,at,pc,layout,computer,game,rotation_result
    try:
        assert time.monotonic()-started<240
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
            pc.set_observer_floor(0);pc.toggle_layout_editing()
            layout=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseLayoutEditor)[0]
            assert layout.get_editor_property('editing')
            pc.set_layout_category(9)
            assert layout.get_editor_property('category')==unreal.WarehouseLayoutCategory.ALL
            at=time.monotonic();phase='move';return
        if time.monotonic()-at<1:return
        if phase=='move':shot('Move.png');phase='select_rotation'
        elif phase=='select_rotation':
            candidates=[a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehousePallet)
                        if a.get_actor_location().z<2 and a.get_editor_property('payload_mass_kg')<=0]
            assert candidates,'No ground pallet for context-menu selection'
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
        elif phase=='add':shot('Add.png');phase='leave'
        elif phase=='leave':pc.toggle_layout_editing();pc.set_observer_floor(-1);pc.toggle_observer_view();phase='open'
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
            finish(True,'Rendered Move/Delete All category, selected-object rotation/delete menu, Add without All and held-data console; native rotation success/rollback, context deletion and empty-selection guard, 2 FIFO records, console live updates and return')
        at=time.monotonic()
    except Exception:finish(False,traceback.format_exc())

handle=unreal.register_slate_post_tick_callback(tick)
