"""Inspect logged pickup goal poses against the saved map and real scan snapshot.

Recreates the reported stopped vehicle position in editor setup only. Original
map is never saved. Mirrored headings are geometry diagnostics, not proof of
opposite-face pallet engagement. -PickupDiagnosticRun=name keeps each result.
"""
from pathlib import Path
import json,re,time,traceback,unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
root=Path(unreal.Paths.project_dir()).resolve()
match=re.search(r'-PickupDiagnosticRun=([A-Za-z0-9_-]+)',unreal.SystemLibrary.get_command_line(),re.I)
out=root/'Saved/Testing/PickupApproach_20261103'/(match.group(1) if match else 'Parts')
out.mkdir(parents=True,exist_ok=True)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem);actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
for a in actors.get_all_level_actors():
 if isinstance(a,unreal.WarehouseForklift):
  a.set_editor_property('auto_start',False);a.set_editor_property('idle_parking_enabled',False)
  if a.get_actor_label()=='WH_AutonomousForklift':a.set_actor_location(unreal.Vector(2400.976,-1533.034,0),False,True);a.set_actor_rotation(unreal.Rotator(yaw=180),True)
 if isinstance(a,unreal.WarehouseInboundZone):a.set_actor_tick_enabled(False)
started=time.monotonic();powered=False;finished=False;result={}
def finish():
 global finished
 if finished:return
 finished=True
 (out/'Baseline.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
 unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()
def tick(dt):
 global powered,result
 try:
  assert time.monotonic()-started<600,'Baseline timeout'
  game=editor.get_game_world()
  if not game:return
  now=unreal.GameplayStatics.get_time_seconds(game)
  fleet=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseForklift)
  v=next((a for a in fleet if a.get_actor_label()=='WH_AutonomousForklift'),None)
  if not v:return
  if not powered:
   for director in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseFocusDirector):director.set_manual_operations(True,False)
   if not v.powered:v.toggle_power()
   powered=True
  kit=v.get_component_by_class(unreal.AgvShadowSensorsComponent)
  if now<4 or not kit or not kit.snapshot_valid:return
  result=dict(passed=True,saved_map_written=False,simulation_seconds=now,current_position=str(v.get_actor_location()),goals=[],actors=[])
  for label,x,y,yaw in [('console',-215,-860.5,0),('inbound_right',1300,-1790,0),('inbound_mid',785,-1865,180),('inbound_left',925,-1865,180)]:
   for flip in [0,180]:
    rot=unreal.Rotator(yaw=yaw+flip);forward=unreal.MathLibrary.get_forward_vector(rot)
    for distance in [120,140,160,185,220,280,320,400,500]:
     point=unreal.Vector(x,y,0)-forward*distance
     pose=unreal.Transform(location=point,rotation=rot)
     result['goals'].append(dict(label=label,flip=flip,distance=distance,position=[point.x,point.y,point.z],clear=v.navigation_clear_for_diagnostics(pose)))
  for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor):
   result['actors'].append(dict(name=a.get_name(),label=a.get_actor_label(),position=str(a.get_actor_location())))
  result['snapshot']=kit.snapshot_count;result['map_revision']=kit.map_revision
  finish()
 except Exception:result=dict(passed=False,error=traceback.format_exc());finish()
handle=unreal.register_slate_post_tick_callback(tick);level.editor_request_begin_play()
