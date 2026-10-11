"""Grow the upper 30% of saved cartons up to 25%; preserve small cartons and actual packing space."""
import json,math
from pathlib import Path
import unreal

LEVEL='/Game/FirstPerson/Lvl_FirstPerson'
REPORT=Path(unreal.Paths.project_saved_dir())/'Testing'/'LargeCargoExpansion.json'

def dimensions(a):
    c,e=a.get_actor_bounds(False)
    return [c.x,c.y,c.z],[e.x*2,e.y*2,e.z*2]

def intersects(c,size,other):
    return all(abs(c[k]-other['center'][k])<(size[k]+other['size'][k])*.5-.15 for k in range(3))

def expand_large_cargo():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    all_actors=list(actors.get_all_level_actors())
    named={a.get_actor_label():a for a in all_actors}
    cargo=sorted((a for a in all_actors if isinstance(a,unreal.WarehouseCargo)),key=lambda a:a.get_actor_label())
    assert cargo
    applied=['LargeCargoExpanded' in [str(t) for t in a.tags] for a in cargo]
    # The map carries the guard even on a teammate's checkout without the local report.
    if all(applied):
        print('LARGE_CARGO_ALREADY_APPLIED',len(cargo));return
    if any(applied):raise AssertionError('Partial expansion detected; inspect the backup before retrying')
    # The report is the immutable before-state. Reapplying must never grow a parcel again.
    if REPORT.exists():
        report=json.loads(REPORT.read_text(encoding='utf-8'))
        assert set(report['before'])=={a.get_actor_label() for a in cargo},'Cargo inventory changed; inspect the expansion report first'
        raise AssertionError('Partial expansion detected; restore the backup before retrying')
    before={}
    groups={}
    for a in cargo:
        name=a.get_actor_label();center,size=dimensions(a);r=a.get_editor_property('packing')
        assert r.valid,name+' has no packing recipe'
        before[name]={'center':center,'size':size,'mass':a.get_editor_property('gross_mass_kg'),
                      'kind':str(a.get_editor_property('cargo_kind')),'id':str(a.get_editor_property('cargo_id')),
                      'mesh':a.get_component_by_class(unreal.StaticMeshComponent).static_mesh.get_path_name()}
        groups.setdefault((round(center[0],1),round(center[1],1)),[]).append(a)
    volumes=sorted(math.prod(v['size']) for v in before.values())
    threshold=volumes[int((len(volumes)-1)*.7)]
    minima=[min(v['size'][k] for v in before.values()) for k in range(3)]
    protected={n for n,v in before.items() if any(abs(v['size'][k]-minima[k])<.001 for k in range(3))}
    # Use each collider-bearing mesh's bounds as a conservative packing limit.
    # Boxes and every physical pallet/rack/vehicle remain collidable; no new invisible support.
    blockers=[]
    for a in all_actors:
        if isinstance(a,unreal.WarehouseCargo) or not a.get_actor_enable_collision():continue
        for component in a.get_components_by_class(unreal.StaticMeshComponent):
            if component.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION:continue
            origin,extent,_=unreal.SystemLibrary.get_component_bounds(component)
            blockers.append({'center':[origin.x,origin.y,origin.z],'size':[extent.x*2,extent.y*2,extent.z*2]})
    plans={n:{'center':list(v['center']),'size':list(v['size'])} for n,v in before.items()}
    changed=[]
    for stack in groups.values():
        stack.sort(key=lambda a:before[a.get_actor_label()]['center'][2])
        stack_names={a.get_actor_label() for a in stack}
        lower=None;lower_old=None
        for a in stack:
            name=a.get_actor_label();old=before[name];old_bottom=old['center'][2]-old['size'][2]*.5
            # Equal XY on different floors/shelves does not mean one continuous stack.
            connected=lower_old is not None and abs(old_bottom-(lower_old['center'][2]+lower_old['size'][2]*.5))<1.
            bottom=lower['center'][2]+lower['size'][2]*.5+.1 if connected else old_bottom
            base=list(old['center']);base[2]=bottom+old['size'][2]*.5
            plans[name]={'center':base,'size':list(old['size'])}
            r=a.get_editor_property('packing');recipe=None
            if math.prod(old['size'])>=threshold and name not in protected:
                quarter=round(a.get_actor_rotation().yaw/90)%2
                local=[r.size_cm.x,r.size_cm.y,r.size_cm.z]
                def candidate(growth):
                    limits=[v*growth for v in local]
                    if connected:
                        limit_x,limit_y=lower['size'][:2]
                        if quarter:limit_x,limit_y=limit_y,limit_x
                        limits[0]=min(limits[0],limit_x);limits[1]=min(limits[1],limit_y)
                    value=unreal.WarehouseCargo.generate_cargo_recipe(r.delivery_seed,r.item_index,r.product_index,
                        unreal.Vector(*limits),min(10.,r.amount_scale*1.25**3))
                    assert value.valid,name
                    size=[value.size_cm.x,value.size_cm.y,value.size_cm.z]
                    if quarter:size[0],size[1]=size[1],size[0]
                    center=list(base);center[2]=bottom+size[2]*.5
                    if any(size[k]<old['size'][k]-.001 for k in range(3)):return None
                    if any(intersects(center,size,v) for n,v in plans.items() if n not in stack_names):return None
                    if any(intersects(center,size,v) for v in blockers):return None
                    return value,center,size
                result=candidate(1.25)
                if result is None:
                    lo,hi=1.,1.25
                    for _ in range(8):
                        mid=(lo+hi)*.5;trial=candidate(mid)
                        if trial is None:hi=mid
                        else:lo=mid;result=trial
                if result and max(result[2][k]/old['size'][k] for k in range(3))>1.01:
                    recipe,center,size=result
                    plans[name]={'center':center,'size':size};changed.append(name)
            a.modify()
            if recipe:assert a.apply_cargo_recipe(recipe),name
            center,_=dimensions(a)
            a.set_actor_location(a.get_actor_location()+unreal.Vector(*(plans[name]['center'][k]-center[k] for k in range(3))),False,True)
            a.tags=list(a.tags)+[unreal.Name('LargeCargoExpanded')]
            lower=plans[name];lower_old=old
    assert changed,'No large carton could grow in the available space'
    system=named['WH_DamageSystem'];specs=list(system.get_editor_property('objects'))
    for spec in specs:
        if spec.name in changed:spec.mass_kg=named[spec.name].get_editor_property('gross_mass_kg')
    system.modify();system.set_editor_property('objects',specs)
    after={a.get_actor_label():dict(zip(('center','size'),dimensions(a))) for a in cargo}
    assert all(abs(min(v['size'][k] for v in after.values())-minima[k])<.001 for k in range(3)),'Minimum carton dimensions changed'
    report={'before':before,'after':after,'changed':changed,'threshold_volume_cm3':threshold,
            'minimum_world_size_cm':minima,'maximum_growth':1.25}
    REPORT.parent.mkdir(exist_ok=True);REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('LARGE_CARGO_EXPANDED',len(changed),'of',len(cargo),'minimum',minima,
          'max before',[max(v['size'][k] for v in before.values()) for k in range(3)],
          'max after',[max(v['size'][k] for v in after.values()) for k in range(3)])

if __name__=='__main__':
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    expand_large_cargo()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('LARGE_CARGO_SAVED')
    unreal.SystemLibrary.quit_editor()
