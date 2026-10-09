"""Production-map startup under native physics. Transient PIE; no save."""
import time, traceback, unreal

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
started=time.monotonic()
phase=0
initial={}
peak_speed=0

def finish():
    perf.set_editor_property('bThrottleCPUWhenNotForeground',old_throttle)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(_dt):
    global phase,initial,peak_speed
    try:
        assert time.monotonic()-started<240,'Dynamic startup timeout'
        if phase==0:
            if time.monotonic()-started<1:return
            level.editor_request_begin_play();phase=1;return
        game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        named={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
        vehicles=[named[n] for n in ('WH_AutonomousForklift','WH_AutonomousForklift_02')]
        cargo=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseCargo)
        if phase==1:
            if now<.15:return
            initial={v.get_actor_label():v.get_actor_location() for v in vehicles}
            for v in vehicles:
                assert abs(v.get_physical_mass_kg()-1000)<.1,(v.get_actor_label(),v.get_physical_mass_kg())
                assert v.root_component.is_simulating_physics()
            assert len(cargo)==360,len(cargo)
            for label,mass in (('WH_Utility_0',55),('WH_Utility_1',80),('WH_Utility_3',15)):
                part=named[label].get_component_by_class(unreal.StaticMeshComponent)
                assert part.is_simulating_physics(),label
                assert abs(part.get_mass()-mass)<.1,(label,part.get_mass())
            phase=2
        for c in cargo:
            part=c.root_component
            assert part.is_simulating_physics(),c.get_actor_label()
            peak_speed=max(peak_speed,part.get_physics_linear_velocity().length())
            assert part.get_physics_linear_velocity().length()<2500,('Runaway cargo',c.get_actor_label())
        if now<15:return
        workers=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseWorker)
        assert len(workers)==7,len(workers)
        assert not any(w.is_ragdoll() for w in workers),'Worker fell without a deliberate impact at startup'
        for w in workers:
            part=w.get_component_by_class(unreal.CapsuleComponent)
            assert part.is_simulating_physics() and abs(part.get_mass()-80)<.1,w.get_actor_label()
        for v in vehicles:
            print('DYNAMIC_STARTUP_VEHICLE',v.get_actor_label(),v.get_actor_location(),v.get_editor_property('ai_state'),v.get_editor_property('status'),v.get_editor_property('current_speed_cm'))
            assert v.get_actor_up_vector().z>.98,('Unstable empty vehicle',v.get_actor_label())
            assert (v.get_actor_location()-initial[v.get_actor_label()]).length()>10,('Autonomous motor did not depart',v.get_actor_label())
        print('DYNAMIC_WAREHOUSE_STARTUP_PASSED',len(cargo),'cartons',len(workers),'people, two physical AGVs, portable props; peak cargo speed',peak_speed)
        finish()
    except Exception:
        print('DYNAMIC_WAREHOUSE_STARTUP_FAILED',traceback.format_exc());finish()

handle=unreal.register_slate_post_tick_callback(tick)
