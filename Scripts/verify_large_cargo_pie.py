"""Reload expanded cartons; verify unchanged minima, recipe replay and real Chaos settling."""
import json,math,time,traceback
from pathlib import Path
import unreal

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report=json.loads((Path(unreal.Paths.project_saved_dir())/'Testing'/'LargeCargoExpansion.json').read_text(encoding='utf-8'))
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
loads={a.get_actor_label():a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseCargo)}
assert set(loads)==set(report['before'])
changed=set(report['changed']);minima=[math.inf]*3;maxima=[0]*3;initial_centers={}
for name,a in loads.items():
    old=report['before'][name];expected=report['after'][name];c,e=a.get_actor_bounds(False)
    size=(e.x*2,e.y*2,e.z*2);center=(c.x,c.y,c.z)
    assert all(abs(size[k]-expected['size'][k])<.01 for k in range(3)),name+' size not saved'
    # Later layout changes may move entire stacks without changing parcel sizes.
    initial_centers[name]=center
    assert str(a.get_editor_property('cargo_id'))==old['id'] and str(a.get_editor_property('cargo_kind'))==old['kind']
    body=a.get_component_by_class(unreal.StaticMeshComponent)
    assert body.static_mesh.get_path_name()==old['mesh']
    r=a.get_editor_property('packing')
    if name not in changed:
        assert all(abs(size[k]-old['size'][k])<.01 for k in range(3)),name+' small carton resized'
        assert abs(a.get_editor_property('gross_mass_kg')-old['mass'])<.001
    else:
        assert r.rule_version==2 and r.amount_scale>1
        assert all(old['size'][k]-.01<=size[k]<=old['size'][k]*1.25+.01 for k in range(3))
        replay=unreal.WarehouseCargo.generate_cargo_recipe(r.delivery_seed,r.item_index,r.product_index,r.size_limit_cm,r.amount_scale)
        assert (replay.size_cm-r.size_cm).length()<.001 and abs(replay.gross_mass_kg-r.gross_mass_kg)<.001
        assert math.isclose(r.net_mass_kg,math.prod(size)/1e6*r.packed_density_kg_m3,rel_tol=1e-5)
    for k in range(3):minima[k]=min(minima[k],size[k]);maxima[k]=max(maxima[k],size[k])
assert all(abs(minima[k]-report['minimum_world_size_cm'][k])<.001 for k in range(3))
assert all(maxima[k]>max(v['size'][k] for v in report['before'].values())*1.2 for k in range(3))
print('LARGE_CARGO_DISK_PASSED',len(loads),len(changed),'larger,',len(loads)-len(changed),'unchanged, minimum',minima,'maximum',maxima)
# Prevent task dispatch during the settling check. Every vehicle and parcel retains physics.
for a in actors.get_all_level_actors():
    if isinstance(a,unreal.WarehouseForklift):a.set_editor_property('auto_start',False)
started=time.monotonic();phase=0

def finish(ok):
    print('LARGE_CARGO_PIE_PASSED' if ok else 'LARGE_CARGO_PIE_FAILED')
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play();unreal.SystemLibrary.quit_editor()

def tick(dt):
    global phase
    try:
        assert time.monotonic()-started<240,'Large cargo PIE timeout'
        if phase==0:
            if time.monotonic()-started<3:return
            level.editor_request_begin_play();phase=1;return
        game=editor.get_game_world()
        if not game or unreal.GameplayStatics.get_time_seconds(game)<6:return
        live=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseCargo)
        system=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.WarehouseDamageSystem))
        specs={s.name:s for s in system.get_editor_property('objects')}
        displaced=[]
        for a in live:
            name=a.get_actor_label();body=a.get_component_by_class(unreal.StaticMeshComponent)
            assert body.is_simulating_physics(),name+' lost physics'
            assert abs(body.get_mass()-a.get_editor_property('gross_mass_kg'))<.02,name+' wrong physical mass'
            if name not in changed:continue
            assert abs(specs[name].mass_kg-body.get_mass())<.02,name+' wrong damage-system mass'
            center=a.get_actor_bounds(False)[0]
            if (center-unreal.Vector(*initial_centers[name])).length()>8:displaced.append(name)
            assert not system.has_failed(a),name+' damaged at startup'
        assert not displaced,'Expanded cartons moved unexpectedly: '+str(displaced)
        print('LARGE_CARGO_PHYSICS_PASSED',len(live),'dynamic cartons, mass bookkeeping, six-second enlarged-stack settling')
        finish(True)
    except Exception:
        print(traceback.format_exc());finish(False)
handle=unreal.register_slate_post_tick_callback(tick)
