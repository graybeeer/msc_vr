"""Capture saved stage services and actual game Slate. Does not save maps or simulate OS input."""
import json,time,traceback,struct
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/Testing/WarehousePlay/Views';OUT.mkdir(parents=True,exist_ok=True)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem);actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert editor.get_game_world() is None
assert '-nullrhi' not in unreal.SystemLibrary.get_command_line().lower()
unreal.load_module('AutomationUtils')
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
index=0;phase='load';at=time.monotonic();start=at;task=None;camera=None;shots=[];progress_reload_verified=False

def capture_ui(game,label):
    name=label+'_'+str(time.time_ns())
    unreal.AutomationUtilsBlueprintLibrary.take_gameplay_automation_screenshot(name,map_name_override='WarehousePlay')
    options=unreal.AutomationScreenshotOptions();options.set_editor_property('disable_noisy_rendering_features',False);options.set_editor_property('disable_tonemapping',False)
    unreal.AutomationLibrary.take_automation_screenshot_of_ui(game,unreal.LatentActionInfo(),name,options)
    images=list((ROOT/'Saved/Automation/Incoming/WarehousePlay'/name).rglob('*.png'))
    assert len(images)==1,('No actual Slate capture',label)
    data=images[0].read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n'
    (OUT/(label+'.png')).write_bytes(data);shots.append(dict(name=label,actual_slate=True,resolution=struct.unpack('>II',data[16:24])))

def finish(ok,error=''):
    (OUT/'Report.json').write_text(json.dumps(dict(passed=ok,error=error,screenshots=shots,progress_reload_verified=progress_reload_verified,os_input_tested=False),ensure_ascii=False,indent=2),encoding='utf-8')
    print('WAREHOUSE_PLAY_VIEWS_'+('PASSED' if ok else 'FAILED'),error)
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play()

def run_tick(dt):
    global index,phase,at,task,camera,progress_reload_verified
    try:
        assert time.monotonic()-start<600,'Capture timeout'
        if phase=='load':
            if editor.get_game_world():return
            if index>3:finish(True);return
            path='/Game/Warehouse/Play/Maps/'+('MainMenu' if index==0 else f'Stage{index:02d}')
            assert level.load_level(path)
            if index==0:level.editor_request_begin_play();phase='ui';at=time.monotonic();return
            stage=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehousePlayStage))
            half=stage.get_editor_property('half_size_cm');z=stage.get_editor_property('ceiling_cm')
            eye=unreal.Vector(-half.x+300,-half.y+250,200);target=unreal.Vector(half.x*.3,half.y*.25,z*.52)
            rot=unreal.MathLibrary.find_look_at_rotation(eye,target)
            camera=actors.spawn_actor_from_class(unreal.CameraActor,eye,rot);camera.camera_component.set_field_of_view(85)
            editor.set_level_viewport_camera_info(eye,rot);phase='decor';at=time.monotonic();return
        if phase=='decor':
            if time.monotonic()-at<8:return
            task=unreal.AutomationLibrary.take_high_res_screenshot(1600,1000,str(OUT/f'Stage{index}_Decor.png'),camera=camera,delay=1)
            at=time.monotonic();phase='decor_done';return
        if phase=='decor_done':
            if time.monotonic()-at<4 or not (OUT/f'Stage{index}_Decor.png').exists():return
            shots.append(dict(name=f'Stage{index}_Decor',actual_scene=True))
            actors.destroy_actor(camera);camera=None;level.editor_request_begin_play();at=time.monotonic();phase='ui';return
        if phase=='ui':
            game=editor.get_game_world()
            if not game or time.monotonic()-at<6:return
            pc=unreal.GameplayStatics.get_player_controller(game,0)
            if index>0:
                assert pc.is_layout_editing()
                vehicle=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseForklift)[0]
                station=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseChargingStation)[0]
                assert abs(station.get_editor_property('dock_position_tolerance_cm')-2)<.01 and station.is_docked(vehicle)
                pc.set_layout_action(2);pc.set_layout_category(0 if index==1 else 10 if index==2 else 5)
                at=time.monotonic();phase='ui_ready';return
            reports=list((ROOT/'Saved/Testing/WarehousePlay').glob('Runtime*.json'))
            if '-warehouseplaytest' in unreal.SystemLibrary.get_command_line().lower() and any(json.loads(p.read_text(encoding='utf-8')).get('passed') for p in reports):
                progress=next(s for s in unreal.ObjectIterator(unreal.WarehousePlayProgress) if s.get_outer()==unreal.GameplayStatics.get_game_instance(game))
                assert progress.get_editor_property('unlocked_stage')==3
                assert sorted(progress.get_editor_property('completed_stages'))==[1,2,3]
                progress_reload_verified=True
            capture_ui(game,'MainMenu');level.editor_request_end_play();index+=1;phase='load';return
        if phase=='ui_ready':
            if time.monotonic()-at<3:return
            capture_ui(editor.get_game_world(),f'Stage{index}_UI');level.editor_request_end_play();index+=1;phase='load'
    except Exception:finish(False,traceback.format_exc())

busy=False
def tick(dt):
    global busy
    if busy:return
    busy=True
    try:run_tick(dt)
    finally:busy=False
handle=unreal.register_slate_post_tick_callback(tick)
