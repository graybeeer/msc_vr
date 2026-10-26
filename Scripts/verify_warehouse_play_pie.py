"""Isolated prototype campaign: real layout transactions, budget guards and native inbound AGV delivery."""
import json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/Testing/WarehousePlay';OUT.mkdir(parents=True,exist_ok=True)
assert '-warehouseplaytest' in unreal.SystemLibrary.get_command_line().lower(),'Use the isolated campaign save slot'
start_stage=int(globals().get('WAREHOUSE_PLAY_START_STAGE',1))
assert 1<=start_stage<=3
if start_stage==1:unreal.GameplayStatics.delete_game_in_slot('WarehousePlayProgress_Test',0)
result_file=OUT/('Runtime.json' if start_stage==1 else f'Runtime_From{start_stage}.json')
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert level.load_level('/Game/Warehouse/Play/Maps/MainMenu')
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
started=time.monotonic();phase='menu';checks=[];runs=[];target=start_stage;next_poll=0;last_log=0;stage_started=0
game=pc=stage=None

def allof(cls):return unreal.GameplayStatics.get_all_actors_of_class(game,cls)
def note(name,**values):checks.append(dict(name=name,**values));print('PLAY_CHECK',name,json.dumps(values,ensure_ascii=False))
def finish(ok,error=''):
    result_file.write_text(json.dumps(dict(passed=ok,error=error,start_stage=start_stage,checks=checks,runs=runs,wall_seconds=time.monotonic()-started),ensure_ascii=False,indent=2),encoding='utf-8')
    print('WAREHOUSE_PLAY_'+('PASSED' if ok else 'FAILED'),error)
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play()
    if '-keeptesteditoropen' not in unreal.SystemLibrary.get_command_line().lower():unreal.SystemLibrary.quit_editor()

def layout_checks(index):
    global pc,stage
    layout=allof(unreal.WarehouseLayoutEditor)[0]
    assert pc.is_layout_editing() and unreal.GameplayStatics.is_game_paused(game)
    assert stage.get_editor_property('budget_won')==stage.get_editor_property('starting_budget_won')
    station=allof(unreal.WarehouseChargingStation)[0]
    assert abs(station.get_editor_property('dock_position_tolerance_cm')-2)<.01
    fixed=next(a for a in allof(unreal.Actor) if a.get_actor_label()=='Building_Wall_N')
    layout.set_category(unreal.WarehouseLayoutCategory.ALL);layout.set_selection([fixed])
    assert len(layout.get_selection())==0
    pc.set_construction_editing(True)
    assert not allof(unreal.WarehouseBuildingLayout),'Play mode opened building editor'
    store=allof(unreal.WarehouseLayoutInventory)[0]
    assert not store.create_held_cargo('PLAY-INVALID','Forbidden stock',10,unreal.Vector(30,30,30))
    sub=next(s for s in unreal.ObjectIterator(unreal.WarehouseExperimentSubsystem) if s.get_outer()==unreal.GameplayStatics.get_game_instance(game))
    assert not sub.save_layout('Play campaign must not become administrator layout')
    layout.set_category(unreal.WarehouseLayoutCategory.CARGO);layout.set_action(unreal.WarehouseLayoutAction.ADD)
    assert not layout.begin_add(unreal.Vector(0,0,0))
    budget=stage.get_editor_property('budget_won')
    point=unreal.Vector(650,-750 if index==1 else -1250,0)
    layout.set_category(unreal.WarehouseLayoutCategory.RACK)
    assert layout.begin_add(point),layout.get_editor_property('status')
    layout.end_drag(False)
    assert stage.get_editor_property('budget_won')==budget
    assert layout.begin_add(unreal.Vector(9000,0,0))
    assert not layout.end_drag(True)
    assert stage.get_editor_property('budget_won')==budget
    assert layout.begin_add(point),layout.get_editor_property('status')
    assert layout.end_drag(True),layout.get_editor_property('status')
    assert stage.get_editor_property('budget_won')==budget-1000000
    layout.set_action(unreal.WarehouseLayoutAction.DELETE)
    assert layout.delete_selection(),layout.get_editor_property('status')
    assert stage.get_editor_property('budget_won')==budget-1000000,'Unexpected demolition refund'
    layout.set_action(unreal.WarehouseLayoutAction.ADD)
    layout.set_category(unreal.WarehouseLayoutCategory.CONVEYOR)
    if index==1:assert not layout.begin_add(point)
    else:
        assert layout.begin_add(point),layout.get_editor_property('status')
        assert layout.end_drag(True),layout.get_editor_property('status')
        assert stage.get_editor_property('budget_won')==budget-11000000
        layout.set_action(unreal.WarehouseLayoutAction.DELETE);assert layout.delete_selection()
    layout.set_action(unreal.WarehouseLayoutAction.ADD);layout.set_category(unreal.WarehouseLayoutCategory.FORKLIFT)
    if index==1:assert not layout.begin_add(point),'Insufficient-budget purchase accepted'
    else:
        assert layout.begin_add(point),layout.get_editor_property('status')
        layout.end_drag(False)
    layout.set_category(unreal.WarehouseLayoutCategory.WAITING_BAY)
    if index<3:assert not layout.begin_add(point)
    else:
        before=stage.get_editor_property('budget_won')
        assert layout.begin_add(point),layout.get_editor_property('status')
        assert layout.end_drag(True),layout.get_editor_property('status')
        assert stage.get_editor_property('budget_won')==before-500000
        layout.set_action(unreal.WarehouseLayoutAction.DELETE);assert layout.delete_selection()
    layout.set_action(unreal.WarehouseLayoutAction.MOVE)
    services=[a for a in allof(unreal.Actor) if a.actor_has_tag('WarehouseServices')]
    assert len(services)>50
    assert all(a.actor_has_tag('WarehouseMapStructure') and a.actor_has_tag('PlayFixed') for a in services)
    assert all(unreal.WarehouseDamageSystem.is_physical_contact(a.get_component_by_class(unreal.StaticMeshComponent)) for a in services)
    note('fixed_layout_budget_unlock_collision_guards',stage=index,service_actors=len(services),budget_remaining=stage.get_editor_property('budget_won'))

def run_tick(dt):
    global game,pc,stage,phase,target,next_poll,last_log,stage_started
    if time.monotonic()<next_poll:return
    next_poll=time.monotonic()+.5
    try:
        assert time.monotonic()-started<2400,'Campaign wall timeout'
        game=editor.get_game_world()
        if not game:return
        pc=unreal.GameplayStatics.get_player_controller(game,0)
        stages=allof(unreal.WarehousePlayStage)
        if phase=='admin':
            if stages:return
            assert not pc.is_main_menu()
            assert len(allof(unreal.WarehouseForklift))>=2
            assert all(abs(s.get_editor_property('dock_position_tolerance_cm')-1)<.01 for s in allof(unreal.WarehouseChargingStation))
            pc.set_layout_editing(True);layout=allof(unreal.WarehouseLayoutEditor)[0]
            layout.set_category(unreal.WarehouseLayoutCategory.CARGO)
            assert '플레이' not in layout.get_add_choice_label()
            note('administrator_original_map_and_tools_available',vehicles=len(allof(unreal.WarehouseForklift)))
            finish(True);return
        if not stages:return
        stage=stages[0];index=stage.get_editor_property('stage_index')
        if phase=='menu':
            if index!=0:return
            assert pc.is_main_menu() and unreal.GameplayStatics.is_game_paused(game)
            if start_stage==1:assert not pc.open_play_mode(2),'Locked stage opened'
            assert pc.open_play_mode(target),'Requested stage is not unlocked in the isolated save'
            note('main_menu_initial_stage_lock' if start_stage==1 else 'main_menu_saved_stage_unlock',stage=target);phase='load';return
        if phase=='load':
            if index!=target or not pc.is_layout_editing():return
            layout_checks(index)
            assert stage.start_operation(),stage.get_editor_property('status')
            assert not pc.is_layout_editing() and not unreal.GameplayStatics.is_game_paused(game)
            assert pc.set_simulation_speed(4)
            stage_started=unreal.GameplayStatics.get_time_seconds(game);phase='running';return
        if phase=='running':
            if index!=target:return
            now=unreal.GameplayStatics.get_time_seconds(game)
            vehicles=allof(unreal.WarehouseForklift)
            for v in vehicles:
                assert abs(v.get_editor_property('empty_travel_speed_cm')-180)<.01
                assert not v.get_editor_property('mechanical_failure'),v.get_editor_property('status')
                assert v.get_editor_property('ai_state')!=unreal.WarehouseAIState.FAULT,v.get_editor_property('status')
            if time.monotonic()-last_log>15:
                last_log=time.monotonic()
                status=dict(stage=index,seconds=now-stage_started,arrived=stage.get_editor_property('arrived'),delivered=stage.get_editor_property('delivered'),
                            stage_status=stage.get_editor_property('status'),vehicles=[dict(state=str(v.get_editor_property('ai_state')),status=v.get_editor_property('status'),position=str(v.get_actor_location())) for v in vehicles])
                (OUT/'Progress.json').write_text(json.dumps(status,ensure_ascii=False,indent=2),encoding='utf-8');print('PLAY_PROGRESS',json.dumps(status,ensure_ascii=False))
            assert now-stage_started<900,('Stage transport timeout',index,stage.get_editor_property('status'),[v.get_editor_property('status') for v in vehicles])
            if not stage.get_editor_property('completed'):return
            goal=stage.get_editor_property('delivery_goal')
            assert stage.get_editor_property('arrived')==goal and stage.get_editor_property('delivered')==goal
            assert sum(len(v.get_editor_property('completed_job_ids')) for v in vehicles)==goal
            assert all(c.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics() for c in allof(unreal.WarehouseCargo))
            saved=unreal.GameplayStatics.load_game_from_slot('WarehousePlayProgress_Test',0)
            assert saved and saved.get_editor_property('unlocked_stage')==min(3,index+1)
            assert index in saved.get_editor_property('completed_stages')
            runs.append(dict(stage=index,completed=goal,simulation_seconds=now-stage_started,unlocked_stage=saved.get_editor_property('unlocked_stage')))
            note('actual_native_inbound_delivery_and_progress_save',**runs[-1])
            (OUT/('Partial.json' if start_stage==1 else f'Partial_From{start_stage}.json')).write_text(json.dumps(dict(checks=checks,runs=runs),ensure_ascii=False,indent=2),encoding='utf-8')
            if index<3:target=index+1;assert pc.open_play_mode(target);phase='load'
            else:assert pc.open_play_mode(-1);phase='admin'
    except Exception:finish(False,traceback.format_exc())

busy=False
def tick(dt):
    global busy
    if busy:return
    busy=True
    try:run_tick(dt)
    finally:busy=False
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
