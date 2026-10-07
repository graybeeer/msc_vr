"""Give each existing cargo a stable SKU, individual mass and varied real-world packaging.
Mass ranges are training assumptions, not manufacturer specifications. Existing pallets stay unchanged.
"""
import json,re,zlib,unreal
from pathlib import Path
import os,sys
sys.path.insert(0,os.path.dirname(__file__))
from apply_real_world_scale import fit

# The native runtime generator is the single source for editor cargo and future truck deliveries.
# Keep this seed stable: arrival time, iteration order, and actor count must not affect cargo recipes.
DELIVERY_SEED = 20261001

def configure_cargo_variety():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cargo=sorted((a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseCargo)),key=lambda a:a.get_actor_label())
    assert cargo,'No cargo in level'
    meshes=[unreal.load_asset('/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_'+str(i)) for i in (1,2)]
    assert all(meshes),'Install the local Fab Boxes & Pallets Pack with Scripts/import_boxes_pallets_pack.py; see WAREHOUSE.md'
    named={a.get_actor_label():a for a in actors.get_all_level_actors()}
    groups={}
    for a in cargo:
        label=a.get_actor_label();match=re.fullmatch(r'(WH_Stock_\d+_\d_\d)_(\d)',label)
        group,layer=(match[1],int(match[2])) if match else (label.removesuffix('_Top'),int(label.endswith('_Top')))
        groups.setdefault(group,[]).append((layer,a))
    rows=[]
    for group,stack in sorted(groups.items()):
        stack.sort(key=lambda item:item[0])
        center,extent=stack[0][1].get_actor_bounds(False)
        # Anchor to the actual supporting surface, so repeated mesh/scale edits cannot drift.
        support_label=None
        if group.startswith('WH_Cargo_'):support_label=group.replace('WH_Cargo_','WH_Pallet_',1)
        elif group.startswith('WH_Stock_'):support_label='WH_DispatchPallet_'+group.split('_')[2]
        elif group.startswith(('WH_MediumStock_','WH_LightStock_')):support_label=group.replace('Stock_','Shelf_')
        elif group in ('WH_Pickup_Box','WH_Drop_Box'):support_label=group.replace('_Box','_Pallet')
        elif group.startswith('MZ_Cargo_'):support_label=group.replace('MZ_Cargo_','MZ_Pallet_',1)
        elif group=='MZ_TransferCargo':support_label='WH_TrainingPallet'
        bottom=center.z-extent.z
        if support_label in named:
            support_center,support_extent=named[support_label].get_actor_bounds(False)
            bottom=support_center.z+support_extent.z
            if group.startswith('WH_Stock_'):
                _,_,_,row,col=group.split('_')
                center=unreal.Vector(support_center.x+(int(row)-.5)*43,support_center.y+(int(col)-.5)*34,center.z)
                if group=='WH_Stock_5_0_0':
                    # This small footprint needs the actual frame/board intersection.
                    center=unreal.Vector(support_center.x,support_center.y-50,center.z)
            else:
                center=unreal.Vector(support_center.x,support_center.y,center.z)
        maximum=(60,40,38) if group.startswith('WH_Cargo_') else (30,20,25) if group.startswith('WH_LightStock_') else (40,30,28)
        footprint=maximum[:2]
        for layer,a in stack:
            label=a.get_actor_label();seed=zlib.crc32(label.encode())
            quarter_turns=round(a.get_actor_rotation().yaw/90)
            limits=[min(maximum[0],footprint[0]),min(maximum[1],footprint[1]),maximum[2]]
            if quarter_turns%2:limits[0],limits[1]=limits[1],limits[0]
            # CRC item keys stay stable when boxes are added, removed, or reordered.
            recipe=unreal.WarehouseCargo.generate_cargo_recipe(DELIVERY_SEED,seed & 0x7fffffff,-1,unreal.Vector(*limits),1.0)
            assert recipe.valid,label
            mesh=meshes[seed%len(meshes)]
            a.modify();a.set_cargo_mesh(mesh)
            a.get_component_by_class(unreal.StaticMeshComponent).set_editor_property('override_materials',[])
            assert a.apply_cargo_recipe(recipe),label
            dimensions=[recipe.size_cm.x,recipe.size_cm.y,recipe.size_cm.z]
            if quarter_turns%2:dimensions[0],dimensions[1]=dimensions[1],dimensions[0]
            fit(a,dimensions,(center.x,center.y,bottom+dimensions[2]/2))
            a.set_editor_property('cargo_id',unreal.Name(label.removeprefix('WH_')))
            rows.append({'id':str(a.get_editor_property('cargo_id')),'actor':label,'kind':str(recipe.product_kind),
                         'gross_kg':recipe.gross_mass_kg,'net_kg':recipe.net_mass_kg,'packaging_kg':recipe.packaging_mass_kg,
                         'packed_density_kg_m3':recipe.packed_density_kg_m3,'fragile':recipe.fragile,
                         'size_cm':dimensions,'local_size_cm':[recipe.size_cm.x,recipe.size_cm.y,recipe.size_cm.z],
                         'delivery_seed':recipe.delivery_seed,'item_index':recipe.item_index,'product_index':recipe.product_index,
                         'rule_version':recipe.rule_version,'size_limit_cm':limits,'amount_scale':recipe.amount_scale,'mesh':mesh.get_path_name()})
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
