"""Give each existing cargo a stable SKU, individual mass and varied real-world packaging.
Mass ranges are training assumptions, not manufacturer specifications. Existing pallets stay unchanged.
"""
import json,random,re,zlib,unreal
from pathlib import Path
import os,sys
sys.path.insert(0,os.path.dirname(__file__))
from apply_real_world_scale import fit
from prepare_warehouse_assets import collision_mesh

# Contents, package centimetres, plausible gross kg range.
PROFILES=(
 ('의류 / 면 티셔츠',(48,34,32),(3,7)),('서적 / 단행본',(36,28,22),(8,17)),
 ('식품 / 건면',(40,30,28),(4,10)),('음료 / 생수',(40,30,27),(10,20)),
 ('생활용품 / 세제',(36,26,30),(6,13)),('주방용품 / 식기',(50,36,32),(5,12)),
 ('전자제품 / 공유기',(46,32,26),(4,10)),('기계부품 / 베어링',(34,26,20),(12,24)),
 ('전기자재 / 케이블',(38,28,23),(6,14)),('화장품 / 스킨케어',(30,22,18),(2,6)),
 ('문구 / 복사용지',(34,25,24),(5,11)),('신발 / 운동화',(52,34,28),(3,8)),
 ('식품 / 농산물',(40,30,25),(5,12)),('생활용품 / 수건',(44,32,30),(2,5)),
 ('전자부품 / 센서',(32,24,20),(2,8)),('식품 / 통조림',(36,28,24),(9,18)),
)

def configure_cargo_variety():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cargo=sorted((a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseCargo)),key=lambda a:a.get_actor_label())
    assert cargo,'No cargo in level'
    meshes=[unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/Cargo_Box_V'+str(i)+'_001') for i in range(1,5)]
    assert all(meshes),'Install the local Fab Boxes & Pallets Pack with Scripts/import_boxes_pallets_pack.py; see WAREHOUSE.md'
    named={a.get_actor_label():a for a in actors.get_all_level_actors()}
    groups={}
    for a in cargo:
        label=a.get_actor_label();match=re.fullmatch(r'(WH_Stock_\d+_\d_\d)_(\d)',label)
        group,layer=(match[1],int(match[2])) if match else (label.removesuffix('_Top'),int(label.endswith('_Top')))
        groups.setdefault(group,[]).append((layer,a))
    rows=[];used_masses=set()
    for group,stack in sorted(groups.items()):
        stack.sort(key=lambda item:item[0])
        center,extent=stack[0][1].get_actor_bounds(False)
        # Anchor to the actual supporting surface, so repeated mesh/scale edits cannot drift.
        support_label=None
        if group.startswith('WH_Cargo_'):support_label=group.replace('WH_Cargo_','WH_Pallet_',1)
        elif group.startswith('WH_Stock_'):support_label='WH_DispatchPallet_'+group.split('_')[2]
        elif group.startswith(('WH_MediumStock_','WH_LightStock_')):support_label=group.replace('Stock_','Shelf_')
        elif group in ('WH_Pickup_Box','WH_Drop_Box'):support_label=group.replace('_Box','_Pallet')
        bottom=center.z-extent.z
        if support_label in named:
            support_center,support_extent=named[support_label].get_actor_bounds(False)
            bottom=support_center.z+support_extent.z
            if group.startswith('WH_Stock_'):
                _,_,_,row,col=group.split('_')
                center=unreal.Vector(support_center.x+(int(row)-.5)*43,support_center.y+(int(col)-.5)*34,center.z)
            else:
                center=unreal.Vector(support_center.x,support_center.y,center.z)
        maximum=(60,40,38) if group.startswith('WH_Cargo_') else (30,20,25) if group.startswith('WH_LightStock_') else (40,30,28)
        footprint=maximum[:2]
        for layer,a in stack:
            label=a.get_actor_label();seed=zlib.crc32(label.encode())
            rng=random.Random(seed);index=seed%len(PROFILES)
            kind,size,mass_range=PROFILES[index]
            # Scale package proportions uniformly; upper packages cannot overhang their support.
            limit=min(maximum[0]/size[0],maximum[1]/size[1],maximum[2]/size[2],footprint[0]/size[0],footprint[1]/size[1])
            factor=min(1.1,limit)*rng.uniform(.90,1.0)
            dimensions=tuple(round(s*factor,2) for s in size)
            # Different contents need different mass even when cartons share a mesh.
            cents=round(rng.uniform(*mass_range)*min(1.0,factor**3)*100)
            while cents in used_masses:cents+=1
            used_masses.add(cents);mass=cents/100
            mesh=meshes[seed%len(meshes)]
            a.modify();a.set_cargo_mesh(mesh)
            a.get_component_by_class(unreal.StaticMeshComponent).set_editor_property('override_materials',[])
            fit(a,dimensions,(center.x,center.y,bottom+dimensions[2]/2))
            a.set_editor_property('cargo_id',unreal.Name(label.removeprefix('WH_')))
            a.set_editor_property('cargo_kind',unreal.Text(kind))
            a.set_gross_mass_kg(mass)
            rows.append({'id':str(a.get_editor_property('cargo_id')),'actor':label,'kind':kind,'gross_kg':mass,'size_cm':dimensions,'mesh':mesh.get_path_name()})
            bottom+=dimensions[2]+.1
            footprint=dimensions[:2]
    (Path(unreal.Paths.project_saved_dir())/'CargoManifest.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    print('CARGO_VARIETY_CONFIGURED',len(rows),'cargo',len({r['kind'] for r in rows}),'kinds',len({r['mesh'] for r in rows}),'meshes',min(r['gross_kg'] for r in rows),max(r['gross_kg'] for r in rows),'kg')
    return rows

if __name__=='__main__':
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    configure_cargo_variety()
    from configure_warehouse_strength import configure_strength
    configure_strength()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('CARGO_VARIETY_SAVED')
