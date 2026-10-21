"""Reload saved services, verify authored persistence, capture without saving any scene edits."""
import unreal,time,json,traceback
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/Testing/WarehouseServicesDense'
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
allactors=actors.get_all_level_actors();services=[a for a in allactors if a.get_actor_label().startswith('WS_')]
report=json.loads((OUT/'Report.json').read_text(encoding='utf-8'));assert len(services)==report['placed_actors']
assert all(a.static_mesh_component.static_mesh for a in services)
assert all(unreal.WarehouseDamageSystem.is_physical_contact(a.static_mesh_component) for a in services)
mounts=json.loads((OUT/'Mounts.json').read_text(encoding='utf-8'))
named={a.get_actor_label():a for a in allactors}
for m in mounts:
    a=named[m['surface']];c,e=a.get_actor_bounds(False);x,y,z=m['point']
    if m['type']=='ceiling':
        assert abs(z-(c.z-e.z))<.01 and abs(x-c.x)<=e.x+.01 and abs(y-c.y)<=e.y+.01,m
    else:
        assert min(abs(x-(c.x-e.x)),abs(x-(c.x+e.x)),abs(y-(c.y-e.y)),abs(y-(c.y+e.y)))<.01,m
report['support_surface_points_verified_on_reload']=len(mounts)
report['saved_map_reload_verified']=True;report['reload_verified_date']='2026-10-14';(OUT/'Report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
camera=actors.spawn_actor_from_class(unreal.CameraActor,unreal.Vector());camera.set_actor_label('TEMP_ServicesCapture')
views=[('GroundServices',(1870,-1480,230),(850,800,360),92),('WallServices',(2250,-1400,240),(2720,-450,350),78),('UpperGallery',(-950,-1380,960),(900,900,1000),90),('Overview',(4200,-4500,3800),(400,200,500),75)]
start=time.monotonic();at=start;index=0;state='ready';task=None;hidden=[]
def tick(dt):
    global index,state,at,task
    try:
        if time.monotonic()-start<25:return
        if state=='pending':
            if not (OUT/(views[index][0]+'.png')).exists():return
            if time.monotonic()-at<8:return
            print('SERVICES_VIEW_SAVED',views[index][0]);index+=1;state='ready';at=time.monotonic();return
        if index>=len(views):
            for a in hidden:a.set_is_temporarily_hidden_in_editor(False)
            actors.destroy_actor(camera);unreal.unregister_slate_post_tick_callback(handle);print('SERVICES_RELOAD_AND_CAPTURE_PASSED',len(services));unreal.SystemLibrary.quit_editor();return
        if time.monotonic()-at<5:return
        name,eye,target,fov=views[index]
        if name=='Overview':
            for a in allactors:
                n=a.get_actor_label()
                if n=='WH_Roof' or n.startswith('WH_Wall_S_') or n=='WH_Wall_E':a.set_is_temporarily_hidden_in_editor(True);hidden.append(a)
        eye=unreal.Vector(*eye);rot=unreal.MathLibrary.find_look_at_rotation(eye,unreal.Vector(*target))
        camera.set_actor_location(eye,False,True);camera.set_actor_rotation(rot,False);camera.camera_component.set_field_of_view(fov);editor.set_level_viewport_camera_info(eye,rot)
        state='pending';at=time.monotonic()
        task=unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,str(OUT/(name+'.png')),camera=camera,delay=1)
    except Exception:
        (OUT/'CaptureError.txt').write_text(traceback.format_exc(),encoding='utf-8');unreal.unregister_slate_post_tick_callback(handle)
handle=unreal.register_slate_post_tick_callback(tick)
