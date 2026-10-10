"""Live PIE check of one-second obstacle sampling. Never saves a map.

Query-only movable probes isolate sensor delay from contact forces. Real AGVs
and the autonomous test pallet keep native physics; only test probes are moved.
"""
import time, traceback, unreal

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
assert level.load_level('/Engine/Maps/Entry')
for a in list(actors.get_all_level_actors()):
    if not isinstance(a,(unreal.WorldSettings,unreal.LevelScriptActor)): actors.destroy_actor(a)
world=editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WorldSettings)[0].set_editor_property('default_game_mode',unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
floor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-10))
floor.static_mesh_component.set_static_mesh(cube)
floor.static_mesh_component.set_collision_profile_name('BlockAll')
floor.set_actor_scale3d(unreal.Vector(80,80,.2))
actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(3000,-3000,125))
remote=actors.spawn_actor_from_class(unreal.WarehouseForklift,unreal.Vector())
remote.set_actor_label('IntervalRemote')
remote.set_editor_property('auto_start',False)
remote.set_editor_property('autonomous_mode',False)
auto=actors.spawn_actor_from_class(unreal.WarehouseForklift,unreal.Vector(0,1600,0))
auto.set_actor_label('IntervalAuto')
auto.set_editor_property('auto_start',True)
pallet=actors.spawn_actor_from_class(unreal.WarehousePallet,unreal.Vector(1000,1600,.1))
pallet.set_actor_label('IntervalPallet')
job=unreal.WarehouseWorkOrder()
job.set_editor_property('job_id','INTERVAL-AUTO')
job.set_editor_property('pallet',pallet)
job.set_editor_property('destination',unreal.Transform(location=unreal.Vector(1700,1600,0)))
auto.set_editor_property('pending_jobs',[job])
dummy=actors.spawn_actor_from_class(unreal.TargetPoint,unreal.Vector(-200,-200,75))
dummy.set_actor_label('IntervalOperator')
for c in dummy.get_components_by_class(unreal.PrimitiveComponent): c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
for name,channel in (('IntervalPerson',unreal.CollisionChannel.ECC_PAWN),('IntervalBox',unreal.CollisionChannel.ECC_WORLD_DYNAMIC)):
    probe=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(3000,2500,85))
    probe.set_actor_label(name)
    body=probe.static_mesh_component
    body.set_mobility(unreal.ComponentMobility.MOVABLE)
    body.set_static_mesh(cube)
    body.set_collision_profile_name('BlockAll')
    body.set_collision_object_type(channel)
    body.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
    probe.set_actor_scale3d(unreal.Vector(.4,.5,1.7 if name=='IntervalPerson' else .8))

began=time.monotonic();phase='start';at=0;game=None;rig=autorig=operator=person=box=None
scan_times={'remote':[],'auto':[]};counts={'remote':0,'auto':0};baseline=0;inserted=0

def enter(name,now):
    global phase,at
    phase,at=name,now

def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def read_samples():
    for name,v in (('remote',rig),('auto',autorig)):
        n=v.get_editor_property('ObstacleCheckCount')
        if n==counts[name]:continue
        assert n==counts[name]+1,(name,'Multiple rounds in one frame',counts[name],n)
        stamp=v.get_editor_property('LastObstacleCheckSeconds')
        if scan_times[name]:
            interval=stamp-scan_times[name][-1]
            assert interval>=.999,(name,'Scanned faster than one second',interval)
        scan_times[name].append(stamp);counts[name]=n
        assert v.root_component.is_simulating_physics()
        assert abs(v.get_physical_mass_kg()-1000)<.1

def tick(dt):
    global game,rig,autorig,operator,person,box,baseline,inserted
    try:
        assert time.monotonic()-began<160,('Obstacle interval timeout',phase)
        if phase=='start':
            level.editor_request_begin_play();enter('wait',0);return
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase=='wait':
            named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            rig,autorig,operator,person,box=(named[n] for n in ('IntervalRemote','IntervalAuto','IntervalOperator','IntervalPerson','IntervalBox'))
            enter('settle',now);return
        if phase=='settle':
            if now-at<1.5:return
            assert rig.begin_remote_control(operator),(rig.status,operator.get_actor_location(),rig.get_actor_location())
            enter('remote_check',now);return
        read_samples()
        age=now-at
        if phase=='remote_check':
            rig.set_remote_input(0,0,0)
            if age<1.3:return
            enter('clear',now);return
        if phase=='clear':
            rig.set_remote_input(1,0,0)
            if not scan_times['remote'] or now-scan_times['remote'][-1]<.2:return
            baseline=counts['remote'];inserted=now
            person.set_actor_location(rig.get_actor_location()+rig.get_actor_forward_vector()*180+unreal.Vector(0,0,85),False,True)
            enter('person_pending',now);return
        if phase=='person_pending':
            rig.set_remote_input(1,0,0)
            if counts['remote']==baseline:
                assert 'PERSON' not in rig.status,('Detected between sampling rounds',rig.status)
                return
            assert 'PERSON' in rig.status,('Next scan did not detect person',rig.status)
            assert now-inserted>.6,('Person detection skipped configured delay',now-inserted)
            print('ONE_SECOND_PERSON_DETECTION_PASSED','delay_seconds',now-inserted)
            baseline=counts['remote']
            person.set_actor_location(unreal.Vector(3000,2500,85),False,True)
            enter('person_removed',now);return
        if phase=='person_removed':
            rig.set_remote_input(1,0,0)
            if counts['remote']==baseline:
                assert 'PERSON' in rig.status,('Blocked result did not hold until next scan',rig.status)
                return
            assert 'PERSON' not in rig.status
            print('ONE_SECOND_PERSON_CLEARANCE_PASSED','delay_seconds',age)
            enter('box_setup',now);return
        if phase=='box_setup':
            rig.set_remote_input(1,0,0)
            if now-scan_times['remote'][-1]<.2:return
            baseline=counts['remote'];inserted=now
            box.set_actor_location(rig.get_actor_location()+rig.get_actor_forward_vector()*70+unreal.Vector(0,0,60),False,True)
            enter('box_pending',now);return
        if phase=='box_pending':
            rig.set_remote_input(1,0,0)
            if counts['remote']==baseline:
                assert 'OBSTACLE' not in rig.status,('Detected box between rounds',rig.status)
                return
            assert 'OBSTACLE' in rig.status,('Next scan did not detect box',rig.status)
            print('ONE_SECOND_BOX_DETECTION_PASSED','delay_seconds',now-inserted)
            baseline=counts['remote']
            box.set_actor_location(unreal.Vector(3000,2500,85),False,True)
            enter('box_removed',now);return
        if phase=='box_removed':
            rig.set_remote_input(1,0,0)
            if counts['remote']==baseline:
                assert 'OBSTACLE' in rig.status
                return
            assert 'OBSTACLE' not in rig.status
            enter('turn',now);return
        if phase=='turn':
            rig.set_remote_input(0,1,0)
            if age<2.2:return
            enter('lift',now);return
        if phase=='lift':
            rig.set_remote_input(0,0,1)
            if age<2.2:return
            assert rig.get_fork_height_cm()>15,('Cached lift check stopped normal actuation',rig.status)
            for name,times in scan_times.items():
                assert len(times)>=3,(name,'Sampling did not repeat',times)
                gaps=[b-a for a,b in zip(times,times[1:])]
                assert min(gaps)>=.999,(name,gaps)
                print('OBSTACLE_SCAN_CADENCE',name,'samples',times,'intervals',gaps)
            assert autorig.ai_state not in (unreal.WarehouseAIState.OFF,unreal.WarehouseAIState.FAULT),autorig.status
            rig.end_remote_control(operator)
            print('OBSTACLE_INTERVAL_PIE_PASSED one-second remote/autonomous sampling, delayed person/box detection and clearance, shared turn/lift rounds, native mass and physics maintained')
            finish()
    except Exception:
        print('OBSTACLE_INTERVAL_PIE_FAILED',phase,traceback.format_exc());finish()

handle=unreal.register_slate_post_tick_callback(tick)
