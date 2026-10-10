"""Unsaved Entry PIE: partial insertion retry, real wall blocking, local detour.

Transient perception bias causes incomplete insertion in the normal work order.
Native vehicle/pallet transforms, velocities and contacts remain untouched during
play. A kinematic test wall is placed behind one vehicle after its failed attempt;
a movable QueryOnly Pawn isolates person-scanner timing.
"""
import time, traceback, unreal
CAPTURE='-recoverycapture' in unreal.SystemLibrary.get_command_line().lower()

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
assert level.load_level('/Engine/Maps/Entry')
for a in list(actors.get_all_level_actors()):
    if not isinstance(a,(unreal.WorldSettings,unreal.LevelScriptActor)):actors.destroy_actor(a)
world=editor.get_editor_world()
unreal.GameplayStatics.get_all_actors_of_class(world,unreal.WorldSettings)[0].set_editor_property('default_game_mode',unreal.load_class(None,'/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'))
cube=unreal.load_asset('/Engine/BasicShapes/Cube')

def obstacle(name,location,scale,person=False):
    a=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*location))
    a.set_actor_label(name)
    a.static_mesh_component.set_static_mesh(cube)
    a.static_mesh_component.set_collision_profile_name('BlockAll')
    a.set_actor_scale3d(unreal.Vector(*scale))
    if person:
        a.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        a.static_mesh_component.set_collision_object_type(unreal.CollisionChannel.ECC_PAWN)
        a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
    return a

obstacle('RecoveryFloor',(0,0,-10),(80,80,.2))
actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(3000,-3000,125))
for name,y,px in (('Retry',0,500),('Blocked',-1500,500),('Detour',1500,1200),('Stall',3000,1000),('Limit',-3000,500)):
    rig=actors.spawn_actor_from_class(unreal.WarehouseForklift,unreal.Vector(0,y,0))
    rig.set_actor_label(name)
    rig.set_editor_property('auto_start',True)
    if name in ('Retry','Blocked','Limit'):rig.set_editor_property('pallet_position_bias_cm',unreal.Vector(-20,0,0))
    if name=='Stall':rig.set_editor_property('empty_travel_speed_cm',30)
    deck=actors.spawn_actor_from_class(unreal.WarehousePallet,unreal.Vector(px,y,.1))
    deck.set_actor_label(name+'Pallet')
    rig.set_editor_property('target_pallet',deck)
    job=unreal.WarehouseWorkOrder(job_id=name,pallet=deck,destination=unreal.Transform(location=unreal.Vector(1700,y,0)))
    rig.set_editor_property('pending_jobs',[job])
rear=obstacle('RearWall',(-170,-1500,115),(.5,4,2.3))
rear.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
obstacle('RouteWall',(450,1500,115),(.5,1,2.3))
obstacle('PersonProbe',(3000,2500,85),(.4,.5,1.7),True)
stall_wall=obstacle('UnseenContact',(145,3000,115),(.1,4,2.3))
stall_wall.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
stall_wall.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.PHYSICS_ONLY)
if CAPTURE:actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,500),unreal.Rotator(pitch=-45,yaw=30))

began=time.monotonic();phase='start';at=0;game=None;named=None
person_stage=0;person_at=0;hold_pose=None;held=False
retry_ok=blocked_ok=detour_ok=stall_ok=limit_ok=False;max_side=0;stall_pose=None
states={};scans={};withdraw_origin=None;withdraw_distance=0
diag_at=0;rear_placed=False;blocked_start=None
alert_stage=0;alert_at=0;audio_played=False

def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def tick(dt):
    global game,named,phase,at,person_stage,person_at,hold_pose,held
    global retry_ok,blocked_ok,detour_ok,stall_ok,limit_ok,stall_pose,max_side,withdraw_origin,withdraw_distance,diag_at
    global rear_placed,blocked_start
    global alert_stage,alert_at,audio_played
    try:
        assert time.monotonic()-began<260,('RECOVERY timeout',phase)
        if phase=='start':level.editor_request_begin_play();phase='wait';return
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if phase=='wait':
            named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
            phase='settle';at=now;return
        if phase=='settle':phase='exercise';at=now;return
        if phase!='exercise':return
        for name in ('Retry','Blocked','Detour','Stall','Limit'):
            rig=named[name];deck=named[name+'Pallet']
            assert rig.root_component.is_simulating_physics()
            assert abs(rig.get_physical_mass_kg()-1000)<.1
            assert deck.root_component.is_simulating_physics() and not deck.get_attach_parent_actor()
            assert abs(deck.root_component.get_mass()-25)<.1
            stamp=rig.get_editor_property('LastObstacleCheckSeconds')
            if stamp>=0 and scans.get(name)!=stamp:
                if name in scans:assert stamp-scans[name]>=.999,(name,'scan too frequent',stamp-scans[name])
                scans[name]=stamp
            if states.get(name)!=rig.ai_state:
                states[name]=rig.ai_state
                print('RECOVERY_STATE',name,now,rig.ai_state,rig.status,rig.get_actor_location())
        rig=named['Retry'];deck=named['RetryPallet'];probe=named['PersonProbe']
        if rig.get_editor_property('ForkInsertionRetries')>=1:
            rig.set_editor_property('pallet_position_bias_cm',unreal.Vector())
        if rig.ai_state==unreal.WarehouseAIState.RECOVER_WITHDRAW and rig.get_editor_property('ForkInsertionRetries')>=1:
            if withdraw_origin is None:withdraw_origin=rig.get_actor_location()
            withdraw_distance=max(withdraw_distance,(rig.get_actor_location()-withdraw_origin).length())
            if person_stage==0:
                probe.set_actor_location(rig.get_actor_location()+unreal.Vector(-150,0,85),False,True)
                person_stage=1;person_at=now
            elif person_stage==1 and 'PERSON' in rig.status and abs(rig.current_speed_cm)<2:
                hold_pose=rig.get_actor_location();person_stage=2;person_at=now
            elif person_stage==2 and now-person_at>=1.5:
                assert (rig.get_actor_location()-hold_pose).length()<5,('Moved while person blocked retreat',rig.status)
                probe.set_actor_location(unreal.Vector(3000,2500,85),False,True)
                held=True;person_stage=3
                print('RECOVERY_PERSON_STOP_PASSED',now-person_at)
        if not retry_ok and deck.get_actor_location().z>7:
            assert rig.get_editor_property('ForkInsertionRetries')>=1
            assert withdraw_distance>75,('No physical withdrawal',withdraw_distance)
            assert held,'Person safety was bypassed'
            retry_ok=True
            rig.toggle_power()
            print('PHYSICAL_REINSERT_LIFT_PASSED','withdraw_cm',withdraw_distance,'pallet_height',deck.get_actor_location().z)
        if not retry_ok:assert rig.ai_state!=unreal.WarehouseAIState.FAULT,('Retry failed',rig.status)
        blocked=named['Blocked']
        pc=unreal.GameplayStatics.get_player_controller(game,0)
        if not rear_placed and blocked.ai_state==unreal.WarehouseAIState.RECOVER_WITHDRAW and blocked.get_editor_property('ForkInsertionRetries')>=1:
            blocked_start=blocked.get_actor_location()
            named['RearWall'].set_actor_location(blocked_start+unreal.Vector(-150,0,115),False,True)
            rear_placed=True
        if blocked.ai_state==unreal.WarehouseAIState.FAULT:
            assert 'WITHDRAW BLOCKED' in blocked.status,blocked.status
            assert 1<=blocked.get_editor_property('RecoveryCount')<=3
            assert rear_placed and blocked_start.x-50<blocked.get_actor_location().x<blocked_start.x+10,('Passed through wall',blocked.get_actor_location())
            assert blocked.get_editor_property('bEmergencyBlocked'),blocked.status
            if alert_stage==0 and 'Blocked' in pc.get_editor_property('EmergencyAlertText'):
                assert pc.get_editor_property('EmergencyAlarmCount')>=1
                pc.toggle_observer_view();pc.toggle_warehouse_menu()
                alert_stage=1;alert_at=now
                if CAPTURE:
                    unreal.SystemLibrary.execute_console_command(game,'Shot showui -nosuffix filename=C:/msc_UnrealProject/msc_vr/Saved/Testing/ForkliftEmergencyAlert.png')
            if alert_stage==1:
                if pc.is_emergency_alarm_playing():audio_played=True
                if now-alert_at>6.3:
                    assert 'Blocked' in pc.get_editor_property('EmergencyAlertText')
                    assert pc.get_editor_property('EmergencyAlarmCount')>=2
                    assert pc.is_observer_view() and pc.is_warehouse_menu_open()
                    if CAPTURE:assert audio_played,'Audio component did not play the emergency tone'
                    pc.toggle_warehouse_menu();pc.toggle_observer_view()
                    named['RearWall'].set_actor_location(unreal.Vector(3000,2500,115),False,True)
                    blocked.toggle_power();alert_stage=2;alert_at=now
        if alert_stage==2 and now-alert_at>.35:
            assert not blocked.get_editor_property('bEmergencyBlocked')
            assert 'Blocked' not in pc.get_editor_property('EmergencyAlertText'),pc.get_editor_property('EmergencyAlertText')
            blocked.toggle_power();blocked_ok=True;alert_stage=3
            print('EMERGENCY_UI_ALARM_PASSED','repeated_alarm_count',pc.get_editor_property('EmergencyAlarmCount'),'audio_component_played',audio_played,'capture',CAPTURE)
        detour=named['Detour']
        max_side=max(max_side,abs(detour.get_actor_location().y-1500))
        if not detour_ok and detour.get_actor_location().x>650:
            assert detour.get_editor_property('DetourCount')>=1,('No replanned route',detour.status)
            assert max_side>150,('Did not drive around wall',max_side)
            detour_ok=True;detour.toggle_power()
            print('PHYSICAL_DETOUR_PASSED','max_side_cm',max_side,'location',detour.get_actor_location())
        if not detour_ok:assert detour.ai_state!=unreal.WarehouseAIState.FAULT,('Detour failed',detour.status)
        stalled=named['Stall']
        if not stall_ok and stalled.get_editor_property('RecoveryCount')>=1:
            if stall_pose is None:
                assert any('NO PHYSICAL PROGRESS' in r.detail for r in stalled.job_reports),stalled.status
                stall_pose=stalled.get_actor_location()
                named['UnseenContact'].set_actor_location(unreal.Vector(3000,2500,115),False,True)
            elif (stalled.get_actor_location()-stall_pose).length()>70:
                stall_ok=True;stalled.toggle_power()
                print('PHYSICAL_STALL_RECOVERY_PASSED','travel_cm',(stalled.get_actor_location()-stall_pose).length())
        if not stall_ok:assert stalled.ai_state!=unreal.WarehouseAIState.FAULT,stalled.status
        limit=named['Limit']
        if not limit_ok and limit.ai_state==unreal.WarehouseAIState.FAULT:
            assert 'RECOVERY LIMIT' in limit.status,limit.status
            assert limit.get_editor_property('RecoveryCount')==3 and 1<=limit.get_editor_property('ForkInsertionRetries')<=3
            assert any('FORK INSERTION FAILED' in r.detail for r in limit.job_reports)
            assert limit.get_editor_property('bEmergencyBlocked')
            limit_ok=True
            print('THREE_RETRY_LIMIT_PASSED','insertion_retries',limit.get_editor_property('ForkInsertionRetries'),limit.status)
        if now-diag_at>=2:
            diag_at=now
            print('RECOVERY_DIAG',now,'retry',rig.status,rig.get_actor_location(),rig.current_speed_cm,'pallet',deck.get_actor_transform(),'blocked',blocked.status,'detour',detour.status,detour.get_actor_location())
        if retry_ok and blocked_ok and detour_ok and stall_ok and limit_ok:
            print('FORKLIFT_RECOVERY_PIE_PASSED physical withdrawal/reinsert/lift, person stop, bounded blocked escape and emergency UI/alarm/clearance, real wall detour, query-invisible physical contact stall recovery, one-second sensing, dynamic loads and mass preserved')
            finish()
    except Exception:
        print('FORKLIFT_RECOVERY_PIE_FAILED',traceback.format_exc());finish()

handle=unreal.register_slate_post_tick_callback(tick)
