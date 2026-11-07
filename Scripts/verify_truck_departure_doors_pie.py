"""Actual saved truck assemblies: close before departure, follow, reopen on return. Never saves."""
import json
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
for actor in actors.get_all_level_actors():
    if isinstance(actor,unreal.WarehouseForklift): actor.set_editor_property('auto_start',False)
started=time.monotonic();phase='start';trucks=[];opened={};moving=set();checks=[]
out=Path(unreal.Paths.project_saved_dir())/'Testing/TruckDepartureDoors_20261104/Report.json'

def finish(ok,detail):
    out.write_text(json.dumps(dict(passed=ok,message=detail,checks=checks,savedMapWritten=False,
        wallSeconds=round(time.monotonic()-started,2)),ensure_ascii=False,indent=2),encoding='utf-8')
    print('TRUCK_DOORS_RESULT',ok,detail)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def local_pose(part,truck):
    return unreal.MathLibrary.make_relative_transform(part.get_actor_transform(),truck.get_actor_transform())

def equal(a,b):
    assert (a.translation-b.translation).length()<.05,(a,b)
    q,r=a.rotation,b.rotation
    assert abs(q.x*r.x+q.y*r.y+q.z*r.z+q.w*r.w)>.99999,(a,b)

def tick(dt):
    global phase,trucks
    try:
        assert time.monotonic()-started<240,phase
        if phase=='start':level.editor_request_begin_play();phase='prepare';return
        world=editor.get_game_world()
        if not world:return
        now=unreal.GameplayStatics.get_time_seconds(world)
        if not trucks:
            trucks=list(unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WarehouseLogisticsTruck))
            assert len(trucks)==3
            unreal.GameplayStatics.get_player_controller(world,0).set_simulation_speed(4)
            for director in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WarehouseFocusDirector):director.set_actor_tick_enabled(False)
        if phase=='prepare':
            if now<5 or not all(t.is_manifest_ready() and t.is_ready() for t in trucks):
                assert now<40,[(t.get_actor_label(),t.get_editor_property('status')) for t in trucks];return
            for t in trucks:
                doors=t.get_editor_property('rear_doors');assert len(doors)==8
                assert not t.get_editor_property('rear_doors_closed')
                opened[t.get_name()]=[local_pose(d.part,t) for d in doors]
                assert t.prepare_departure(),t.get_editor_property('status')
            phase='clearance'
        elif phase=='clearance' and all(t.is_departure_prepared() for t in trucks):
            for t in trucks:
                before=t.get_actor_location()
                assert t.depart(),t.get_editor_property('status')
                assert t.get_editor_property('rear_doors_closed')
                assert (t.get_actor_location()-before).length()<.01,'Truck moved before its doors closed'
                for d in t.get_editor_property('rear_doors'):equal(local_pose(d.part,t),d.closed_pose)
            checks.append('All three actual trucks close panels and latch/trim before moving, including the initial loaded truck')
            phase='depart'
        elif phase=='depart':
            for t in trucks:
                assert t.get_editor_property('rear_doors_closed')
                for d in t.get_editor_property('rear_doors'):equal(local_pose(d.part,t),d.closed_pose)
                if t.get_editor_property('state')==unreal.WarehouseTruckState.DEPARTING:moving.add(t.get_name())
            if all(t.get_editor_property('state')==unreal.WarehouseTruckState.DEPARTED for t in trucks):
                assert len(moving)==3
                for t in trucks:assert t.return_for_next_visit(),t.get_editor_property('status')
                checks.append('Closed rear assemblies follow each truck through departure')
                phase='return'
        elif phase=='return' and all(t.get_editor_property('state')==unreal.WarehouseTruckState.AT_DOCK for t in trucks):
            for t in trucks:
                assert not t.get_editor_property('rear_doors_closed') and t.is_ready()
                for d,p in zip(t.get_editor_property('rear_doors'),opened[t.get_name()]):equal(local_pose(d.part,t),p)
            checks.append('All three next visits restore the original open door poses and ready state')
            finish(True,'Three saved trucks: close, depart with body, return and reopen passed')
    except Exception:finish(False,traceback.format_exc())

handle=unreal.register_slate_post_tick_callback(tick)
