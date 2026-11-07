"""Real impact API and simulation time: recover only down to 50%, retain failures."""
import ast,json,time,traceback
from pathlib import Path
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
source=Path(__file__).with_name('verify_shipping_workflow_pie.py')
tree=ast.parse(source.read_text(encoding='utf-8-sig'))
end=next(i for i,n in enumerate(tree.body) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='out' for t in n.targets))
env=dict(__file__=str(source),__name__='__recovery_fixture__')
exec(compile(ast.Module(body=tree.body[:end],type_ignores=[]),str(source),'exec'),env)
level,editor=env['level'],env['editor']
fleet=[env['vehicle']]
for i in (1,2):
    v=env['spawn'](unreal.WarehouseForklift,'RecoveryProbe'+str(i),(-1600,i*500,.1))
    v.set_editor_property('auto_start',False);v.set_editor_property('idle_parking_enabled',False);fleet.append(v)
specs=list(env['strength'].get_editor_property('objects'))
for i,v in enumerate(fleet):specs.append(unreal.WarehouseStrength(name='Recovery'+str(i),members=[v],failure=unreal.WarehouseFailure.MACHINE,
    mass_kg=1000,rated_load_kg=1400,impact_yield_j=400,impact_failure_j=2400))
env['strength'].set_editor_property('objects',specs)
out=Path(unreal.Paths.project_saved_dir())/'Testing/VehicleDamageRecovery_20261104'
out.mkdir(parents=True,exist_ok=True)
phase='start';at=0;live=None;started=time.monotonic();checks=[];restart_damage=0

def damage(name):return next(p.damage for p in live['ShippingStrength'].get_editor_property('objects') if p.name==name)
def enter(value,now):
    global phase,at
    phase,at=value,now
def finish(ok,detail):
    (out/'Report.json').write_text(json.dumps(dict(passed=ok,checks=checks,message=detail,wallSeconds=time.monotonic()-started,savedMapWritten=False),ensure_ascii=False,indent=2),encoding='utf-8')
    print('VEHICLE_RECOVERY_RESULT',ok,detail)
    unreal.unregister_slate_post_tick_callback(handle);level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def tick(dt):
    global live,restart_damage
    try:
        assert time.monotonic()-started<300,phase
        if phase=='start':level.editor_request_begin_play();enter('settle',0);return
        world=editor.get_game_world()
        if not world:return
        now=unreal.GameplayStatics.get_time_seconds(world)
        if live is None:
            live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor)}
            unreal.GameplayStatics.get_player_controller(world,0).set_simulation_speed(4)
        strength=live['ShippingStrength'];v=live['ShippingAGV']
        if phase=='settle' and now>3:
            for a,e in ((v,2000),(live['RecoveryProbe1'],1400),(live['RecoveryProbe2'],1000),(live['OtherShipmentBox'],840)):strength.apply_impact(a,e)
            assert abs(damage('Recovery0')-.8)<.0001
            enter('delay',now)
        elif phase=='delay' and now-at>=9:
            assert abs(damage('Recovery0')-.8)<.0001
            checks.append('No regeneration during the initial 10 simulation second delay')
            enter('rate',now)
        elif phase=='rate' and now-at>=11:
            assert abs(damage('Recovery0')-.775)<.002,damage('Recovery0')
            assert abs(damage('Recovery1')-.5)<.0001 and abs(damage('Recovery2')-.3)<.0001
            assert abs(damage('OtherShipmentBox')-.8)<.0001
            checks.append('Default rate 0.25 percentage points/s; 50%/30% vehicle damage and cargo damage are unchanged')
            strength.apply_impact(v,420);restart_damage=damage('Recovery0');enter('restart',now)
        elif phase=='restart' and now-at>=5:
            assert abs(damage('Recovery0')-restart_damage)<.0001
            checks.append('A new damaging impact restarts the quiet delay')
            enter('floor',now)
        elif phase=='floor' and now-at>=135:
            assert abs(damage('Recovery0')-.5)<.0001,damage('Recovery0')
            assert abs(damage('Recovery1')-.5)<.0001 and abs(damage('Recovery2')-.3)<.0001
            checks.append('Long recovery stops exactly at 50% without healing below the floor')
            strength.apply_impact(v,3000);assert strength.has_failed(v)
            enter('failed',now)
        elif phase=='failed' and now-at>=13:
            assert .5<damage('Recovery0')<1
            assert strength.has_failed(v) and v.get_editor_property('mechanical_failure')
            strength.apply_impact(v,800);restart_damage=damage('Recovery0');enter('failed_quiet',now)
        elif phase=='failed_quiet' and now-at>=5:
            assert abs(damage('Recovery0')-restart_damage)<.0001
            assert strength.has_failed(v) and v.get_editor_property('mechanical_failure')
            assert v.repair_and_reset(),v.status
            assert damage('Recovery0')==0 and not strength.has_failed(v)
            checks.append('Failure remains latched; another failed-body impact restarts delay; explicit E repair restores the vehicle')
            finish(True,'Recovery delay/rate/floor, renewed damage, cargo exclusion and manual repair passed')
    except Exception:finish(False,traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
