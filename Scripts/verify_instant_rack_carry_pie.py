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
named['OtherShipmentPallet'].set_actor_location(unreal.Vector(1200,760,.2),False,True)
fixture['fit'](named['OtherShipmentBox'],(40,38,30),(1200,760,30.3))
fixture['in_zone'].set_editor_property('rack_slots',[
    unreal.WarehouseRackSlot(name='CarryLower',rack=fixture['rack'][0],pose=unreal.Transform(location=unreal.Vector(1200,900,20))),
    unreal.WarehouseRackSlot(name='CarryUpper',rack=fixture['rack'][0],pose=unreal.Transform(location=unreal.Vector(1200,1025,140)))])
geometry_only='-RackGeometryOnly' in unreal.SystemLibrary.get_command_line()
if geometry_only:
    fixture['in_zone'].set_editor_property('rack_slots',[])
    fixture['spawn'](unreal.WarehousePallet,'GeometryLowerOccupied',(1200,1025,20.2))
    fixture['spawn'](unreal.WarehousePallet,'GeometryUpperOccupied',(1200,900,140.2))
out=Path(unreal.Paths.project_saved_dir())/('Testing/InstantRackCarryGeometry' if geometry_only else 'Testing/InstantRackCarry')
out.mkdir(parents=True,exist_ok=True)
phase='start';at=0;started=time.monotonic();live=None;index=0;original=None
checks=[]
report=dict(passed=False,checks=checks,savedMapWritten=False,osInputTested=False,
            geometryOnly=geometry_only,
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
            half=pawn.get_component_by_class(unreal.CapsuleComponent).get_scaled_capsule_half_height()
            pawn.set_actor_location(unreal.Vector(1020,y,half+.2),False,True)
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
            # Aim at the vertical front beam, rather than its thin horizontal
            # deck. E must choose the rack slot and snap the complete unit there.
            aim=unreal.Vector(1145,positions[prefix][1],positions[prefix][2]-6)
            eye=pawn.get_component_by_class(unreal.CameraComponent).get_world_transform().translation
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye,aim))
            at=now;phase='rack_interact';return
        if phase=='rack_interact' and now-at>=.15:
            pawn.toggle_carry()
            e_placed=pawn.get_held_cargo() is None and not pawn.is_placing_cargo()
            if not e_placed:
                camera=pawn.get_component_by_class(unreal.CameraComponent)
                eye=camera.get_world_transform().translation
                trace=unreal.SystemLibrary.line_trace_single(game,eye,eye+camera.get_forward_vector()*250,
                    unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[pawn]+list(pawn.get_held_cargo_group()),unreal.DrawDebugTrace.NONE,False)
                def detail(value):
                    if isinstance(value,unreal.HitResult):return str(value.to_tuple())
                    if isinstance(value,(list,tuple,unreal.Array)):return [detail(item) for item in value]
                    return str(value)
                report['failureDiagnostic']=dict(sight=detail(trace),eye=str(eye),cameraRotation=str(camera.get_world_rotation()),
                    originalPallet=str(original),directRack=pawn.try_place_on_rack(live['ShippingRack0'],original.translation))
            assert e_placed,('E did not snap',prefix,pawn.get_actor_location(),pc.get_control_rotation())
            assert body(pallet).is_simulating_physics() and body(box).is_simulating_physics()
            assert not pallet.actor_has_tag('PlayerHeldCargo') and not box.actor_has_tag('PlayerHeldCargo')
            assert abs(body(pallet).get_mass()-25)<.2
            at=now;phase='returned';return
        if phase=='returned' and now-at>=.8:
            assert (pallet.get_actor_location()-original.translation).length()<4,('Returned pallet did not settle on its shelf',prefix)
            assert box in strength.get_supported_actors(pallet,True),'Returned loose/wrapped load lost support'
            checks.append(prefix+' E on the rack front beam snaps the whole unit onto the registered shelf and restores Chaos')
            if index==0:index=1;phase='settle';return
            half=pawn.get_component_by_class(unreal.CapsuleComponent).get_scaled_capsule_half_height()
            pawn.set_actor_location(unreal.Vector(1020,900,half+.2),False,True)
            pc.set_control_rotation(unreal.Rotator())
            phase='full_rack';at=now;return
        if phase=='full_rack' and now-at>=.15:
            other=live['OtherShipmentPallet']
            assert pawn.try_pickup_pallet(other)
            held=other.get_actor_transform()
            assert not pawn.try_place_on_rack(live['ShippingRack0'],unreal.Vector(1200,900,20)),'Full rack accepted an overlapping pallet'
            assert pawn.get_held_cargo()==other and not pawn.is_placing_cargo()
            assert (other.get_actor_location()-held.translation).length()<.01
            checks.append('occupied rack refuses the snap and keeps the complete held unit without dropping or waiting for animation')
            pawn.drop_cargo()
            finish(True,'E on both shelf beams snaps loaded pallets immediately; a full rack retains the held unit')
    except Exception:
        finish(False,traceback.format_exc())

handle=unreal.register_slate_post_tick_callback(tick)
