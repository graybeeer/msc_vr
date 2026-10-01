"""Check the native delivery generator and saved cargo. Never saves test mutations."""
import math, unreal

def values(r):
    return (r.product_index,str(r.product_kind),r.size_cm.x,r.size_cm.y,r.size_cm.z,
            r.net_mass_kg,r.packaging_mass_kg,r.gross_mass_kg,r.packed_density_kg_m3,r.fragile)

def generate(seed,index,product=-1,limit=(60,40,38),amount=1):
    return unreal.WarehouseCargo.generate_cargo_recipe(seed,index,product,unreal.Vector(*limit),amount)

count=unreal.WarehouseCargo.get_cargo_profile_count()
assert count==16
batch={i:values(generate(7351,i)) for i in range(1024)}
assert {i:values(generate(7351,i)) for i in reversed(range(1024))}==batch,'Arrival order changed recipes'
assert len({v[0] for v in batch.values()})==count
assert sum(values(generate(7352,i))!=batch[i] for i in batch)>1000,'Different deliveries repeated the same cargo'
for product in range(count):
    for i in range(64):
        small=generate(501,i,product,amount=.4)
        large=generate(501,i,product,amount=1)
        assert small.valid and large.valid
        assert small.gross_mass_kg<large.gross_mass_kg
        assert small.size_cm.x<large.size_cm.x and small.size_cm.y<large.size_cm.y and small.size_cm.z<large.size_cm.z
        limited=generate(501,i,product,(20,16,18))
        assert limited.gross_mass_kg<large.gross_mass_kg
        assert limited.size_cm.x<=20.001 and limited.size_cm.y<=16.001 and limited.size_cm.z<=18.001
        for r in (small,large,limited):
            volume=r.size_cm.x*r.size_cm.y*r.size_cm.z/1e6
            assert math.isclose(r.net_mass_kg/volume,r.packed_density_kg_m3,rel_tol=1e-5)
            assert math.isclose(r.gross_mass_kg,r.net_mass_kg+r.packaging_mass_kg,rel_tol=1e-5)
            assert r.packaging_mass_kg>0
            assert values(generate(r.delivery_seed,r.item_index,r.product_index,
                                   (r.size_limit_cm.x,r.size_limit_cm.y,r.size_limit_cm.z),r.amount_scale))==values(r)
assert generate(9,0,7).packed_density_kg_m3>generate(9,0,13).packed_density_kg_m3*10,'Metal and towels lost density distinction'
assert generate(9,0,5).fragile and not generate(9,0,7).fragile
for args in ((1,-1),(1,0,-2),(1,0,16),(1,0,-1,(0,30,20)),(1,0,-1,(math.nan,30,20)),
             (1,0,-1,(math.inf,30,20)),(1,0,-1,(60,40,38),0),(1,0,-1,(60,40,38),math.nan)):
    assert not generate(*args).valid,args
assert generate(-2147483648,2147483647).valid
print('CARGO_RULES_GENERATOR_PASSED deterministic deliveries, all 16 profiles, size/mass correlation, packing, constraints, invalid input')

level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
loads=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.WarehouseCargo)]
assert len(loads)==311
for actor in loads:
    r=actor.get_editor_property('packing');assert r.valid,actor.get_actor_label()
    assert values(generate(r.delivery_seed,r.item_index,r.product_index,
                           (r.size_limit_cm.x,r.size_limit_cm.y,r.size_limit_cm.z),r.amount_scale))==values(r)
    assert abs(actor.get_editor_property('gross_mass_kg')-r.gross_mass_kg)<.001
    extent=actor.get_actor_bounds(False)[1]
    expected=[r.size_cm.x,r.size_cm.y,r.size_cm.z]
    if round(actor.get_actor_rotation().yaw/90)%2:expected[0],expected[1]=expected[1],expected[0]
    assert all(abs(actual-wanted)<.1 for actual,wanted in zip((extent.x*2,extent.y*2,extent.z*2),expected)),actor.get_actor_label()
    assert 'cm' in str(actor.get_cargo_description())
# Runtime API geometry behavior also checked on a non-centred imported mesh; no test assets saved.
sample=loads[0];before=sample.get_actor_bounds(False);bottom=before[0].z-before[1].z
assert sample.apply_cargo_recipe(generate(77,15,3,(55,36,33)))
after=sample.get_actor_bounds(False)
assert abs(after[0].z-after[1].z-bottom)<.01,'Applying recipe moved the bottom'
assert not sample.apply_cargo_recipe(generate(1,-1))
print('CARGO_RULES_SAVED_MAP_PASSED',len(loads),'recipes, geometry, identity, readout, bottom-preserving runtime apply')
