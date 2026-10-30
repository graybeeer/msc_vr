"""Real project rack lower/upper loaded pallets: immediate pickup and return.

Reuses the shipping test's unsaved fixture construction only. No asset is saved,
no forklift is driven, and only fixture setup moves the player/initial freight.
"""
import ast
import json
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
source=Path(__file__).with_name('verify_shipping_workflow_pie.py')
tree=ast.parse(source.read_text(encoding='utf-8-sig'))
end=next(i for i,n in enumerate(tree.body) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='out' for t in n.targets))
fixture={'__file__':str(source),'__name__':'__instant_carry_fixture__'}
exec(compile(ast.Module(body=tree.body[:end],type_ignores=[]),str(source),'exec'),fixture)
level,actors,editor=(fixture[n] for n in ('level','actors','editor'))
named={a.get_actor_label():a for a in actors.get_all_level_actors()}
positions={'ExportA':(1200,900,20.2),'ExportB':(1200,1025,140.2)}
for prefix,(x,y,z) in positions.items():
    named[prefix+'Pallet'].set_actor_location(unreal.Vector(x,y,z),False,True)
    fixture['fit'](named[prefix+'Box'],(40,38,30),(x,y,z+30.1))
out=Path(unreal.Paths.project_saved_dir())/'Testing/InstantRackCarry'
out.mkdir(parents=True,exist_ok=True)
phase='start';at=0;started=time.monotonic();live=None;index=0;original=None
checks=[]
report=dict(passed=False,checks=checks,savedMapWritten=False,osInputTested=False,
            scope='Actual project rack meshes and loaded pallet bodies; native immediate player carry APIs')

def body(a):
    return a.get_component_by_class(unreal.StaticMeshComponent)

def finish(ok,detail):
    report.update(passed=ok,message=detail,wallSeconds=round(time.monotonic()-started,2))
    (out/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('INSTANT_RACK_CARRY_'+('PASSED' if ok else 'FAILED'),detail)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(dt):
    global phase,at,live,index,original
    try:
        assert time.monotonic()-started<180,('Timeout',phase)
        if phase=='start':
            level.editor_request_begin_play();phase='settle';return
        game=editor.get_game_world()
        if not game:return
        now=unreal.GameplayStatics.get_time_seconds(game)
        if live is None:live={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor)}
        pc=unreal.GameplayStatics.get_player_controller(game,0)
        pawn=unreal.GameplayStatics.get_player_character(game,0)
        prefix=list(positions)[index]
        pallet,box=live[prefix+'Pallet'],live[prefix+'Box']
        strength=live['ShippingStrength']
        if phase=='settle' and now>=1.5:
            assert box in strength.get_supported_actors(pallet,True),('Fixture load lost support',prefix)
            original=pallet.get_actor_transform()
            x,y,z=positions[prefix]
            pawn.set_actor_location(unreal.Vector(1020,y,100),False,True)
            pc.set_control_rotation(unreal.Rotator())
            at=now;phase='pickup';return
        if phase=='pickup' and now-at>=.15:
            relative=unreal.MathLibrary.make_relative_transform(box.get_actor_transform(),pallet.get_actor_transform())
            assert pawn.try_pickup_pallet(pallet),('Rack pickup rejected',prefix)
            assert pawn.get_held_cargo()==pallet and not pawn.is_placing_cargo()
            assert set(pawn.get_held_cargo_group())=={pallet,box}
            assert not body(pallet).is_simulating_physics() and not body(box).is_simulating_physics()
            carried=unreal.MathLibrary.make_relative_transform(box.get_actor_transform(),pallet.get_actor_transform())
            assert (carried.translation-relative.translation).length()<.1,'Load shifted during instant pickup'
            assert (pallet.get_actor_location()-original.translation).length()>15,'Pickup did not move in its native call'
            checks.append(prefix+' lower/upper loaded rack pickup is immediate and preserves the complete unit')
            # Return directly to the same real shelf. A visual path would sweep
            # through the front beam or upper deck; only the supported final pose matters.
            assert pawn.try_place_on_surface(original.translation),('Rack return rejected',prefix,str(original.translation))
            assert pawn.get_held_cargo() is None and not pawn.is_placing_cargo()
            assert body(pallet).is_simulating_physics() and body(box).is_simulating_physics()
            assert not pallet.actor_has_tag('PlayerHeldCargo') and not box.actor_has_tag('PlayerHeldCargo')
            assert abs(body(pallet).get_mass()-25)<.2
            at=now;phase='returned';return
        if phase=='returned' and now-at>=.8:
            assert (pallet.get_actor_location()-original.translation).length()<4,('Returned pallet did not settle on its shelf',prefix)
            assert box in strength.get_supported_actors(pallet,True),'Returned loose/wrapped load lost support'
            checks.append(prefix+' snaps onto the actual rack deck immediately and returns independent bodies to Chaos')
            if index==0:index=1;phase='settle';return
            finish(True,'Both actual rack shelf levels support immediate loaded pallet pickup/return without animation-path blockage')
    except Exception:
        finish(False,traceback.format_exc())

handle=unreal.register_slate_post_tick_callback(tick)
