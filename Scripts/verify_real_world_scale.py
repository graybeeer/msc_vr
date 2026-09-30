"""Check saved dimensions, rack support/clearance and repeatable scale application."""
import os
import re
import sys
import unreal

sys.path.insert(0, os.path.dirname(__file__))
from apply_real_world_scale import apply_scale, PALLET_MESH, SHELF_TOPS

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
named = {a.get_actor_label(): a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}


def bounds(actor):
    c, e = actor.get_actor_bounds(False)
    return (c.x-e.x, c.y-e.y, c.z-e.z), (c.x+e.x, c.y+e.y, c.z+e.z)


def dimensions(actor, expected):
    low, high = bounds(actor)
    size = [high[i]-low[i] for i in range(3)]
    assert all(abs(size[i]-expected[i]) < .15 for i in range(3)), (actor.get_actor_label(), size, expected)


def cargo_fits(actor, maximum):
    low, high=bounds(actor)
    assert all(5 < high[i]-low[i] <= maximum[i]+.15 for i in range(3)), actor.get_actor_label()


ground = bounds(named['Floor'])[1][2]
dimensions(named['WH_AutonomousForklift'], (99.4, 164.2, 215))  # yaw 90
dimensions(named['WH_TrainingPallet'], (110, 110, 15))
pallet_actors = [a for label, a in named.items() if label.startswith(('WH_Pallet_', 'WH_DispatchPallet_')) or label in ('WH_Pickup_Pallet', 'WH_Drop_Pallet', 'WH_TrainingPallet')]
assert len(pallet_actors) == 97
pallet_mesh = unreal.load_asset(PALLET_MESH)
assert unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_convex_collision_count(pallet_mesh) > 0
for actor in pallet_actors:
    parts = actor.get_components_by_class(unreal.StaticMeshComponent)
    assert len(parts) == 1 and parts[0].static_mesh == pallet_mesh, ('Mixed pallet model', actor.get_actor_label())
    assert all(parts[0].get_material(i) == pallet_mesh.get_material(i) for i in range(parts[0].get_num_materials())), 'Pallet must keep the authored wood textures'
scale = named['WH_AutonomousForklift'].get_actor_scale3d()
assert all(abs(value-1) < .001 for value in (scale.x, scale.y, scale.z))
for side in ('Left', 'Right'):
    for row in range(11):
        rack = [named[f'WH_Rack_{side}_{row:02}_{part}'] for part in 'ABCDEFGH']
        low = [min(bounds(a)[0][i] for a in rack) for i in range(3)]
        high = [max(bounds(a)[1][i] for a in rack) for i in range(3)]
        assert all(abs(a-b) < .15 for a, b in zip([high[i]-low[i] for i in range(3)], (100, 266, 250))), (low, high)
        assert abs(low[2]-ground) < .1
        for tier, top in enumerate(SHELF_TOPS):
            pallet = named[f'WH_Pallet_{side}_{row:02}_{tier}']
            cargo = named[f'WH_Cargo_{side}_{row:02}_{tier}']
            dimensions(pallet, (110, 110, 15))
            cargo_fits(cargo, (60, 40, 38))
            assert abs(bounds(pallet)[0][2]-ground-top) < .1
            assert abs(bounds(cargo)[0][2]-bounds(pallet)[1][2]) < .1, 'Floating cargo'
            assert top + 12 + 5 <= 160, 'Fork top must reach pallet deck with lifting clearance'
            for beam in rack:
                a, b = bounds(beam)
                c, d = bounds(cargo)
                assert not all(a[i] < d[i]-.1 and b[i] > c[i]+.1 for i in range(3)), ('Cargo clips rack', cargo.get_actor_label(), beam.get_actor_label())
        left = bounds(named[f'WH_Rack_Left_{row:02}_G'])[1][0]
        right = bounds(named[f'WH_Rack_Right_{row:02}_G'])[0][0]
        assert abs(right-left-330) < .1, 'Frame clearance must leave 320 cm between overhanging pallets'

# Both pallets and both box layers fit each commercial bay.
for side in ('Left', 'Right'):
    for row in range(11):
        for tier, top in enumerate(SHELF_TOPS):
            base=f'{side}_{row:02}_{tier}'
            for slot in ('', '_B'):
                pallet=named['WH_Pallet_'+base+slot]
                dimensions(pallet,(110,110,15))
                for stacked in ('', '_Top'):
                    cargo=named['WH_Cargo_'+base+slot+stacked]
                    cargo_fits(cargo,(60,40,38))
                    support=named['WH_Cargo_'+base+slot] if stacked else pallet
                    assert abs(bounds(cargo)[0][2]-bounds(support)[1][2]-(.1 if stacked else 0)) < .15
            first=bounds(named['WH_Pallet_'+base])
            second=bounds(named['WH_Pallet_'+base+'_B'])
            assert abs(second[0][1]-first[1][1]-15) < .1
        left=bounds(named[f'WH_Pallet_Left_{row:02}_0'])[1][0]
        right=bounds(named[f'WH_Pallet_Right_{row:02}_0'])[0][0]
        assert abs(right-left-320)<.1
for kind, size, levels in [('Medium',(60,180,210),(20,95,170)),('Light',(45,120,180),(20,85,150))]:
    for index in range(2):
        parts=[named[f'WH_{kind}Rack_{index}_{part}'] for part in 'ABCDEFGH']
        low=[min(bounds(a)[0][i] for a in parts) for i in range(3)]
        high=[max(bounds(a)[1][i] for a in parts) for i in range(3)]
        assert all(abs(high[i]-low[i]-size[i])<.15 for i in range(3))
        for tier, top in enumerate(levels):
            cargo=named[f'WH_{kind}Stock_{index}_{tier}']
            assert abs(bounds(cargo)[0][2]-ground-top)<.1

for label, actor in named.items():
    if label.startswith('WH_DispatchPallet_') or label in ('WH_Pickup_Pallet', 'WH_Drop_Pallet'):
        dimensions(actor, (110, 110, 15))
        assert abs(bounds(actor)[0][2]-ground) < .1
    stock = re.fullmatch(r'WH_Stock_(\d+)_(\d)_(\d)_(\d)', label)
    if stock or label in ('WH_Pickup_Box', 'WH_Drop_Box'):
        cargo_fits(actor, (40, 30, 28))
    if stock:
        tier = int(stock[4])
        support=named[label.rsplit('_',1)[0]+'_'+str(tier-1)] if tier else named['WH_DispatchPallet_'+stock[1]]
        assert abs(bounds(actor)[0][2]-bounds(support)[1][2]-(.1 if tier else 0)) < .15

truck = named['EXT_ParkedTruck_02']
dimensions(truck, (306.289, 732.138, 318.925))
assert abs(bounds(truck)[0][2]-(ground-120)) < .1
assert abs(bounds(truck)[1][1]+2535) < .1
# The wider bounding box includes mirrors. Measure the box body separately.
source = unreal.load_asset('/Game/VehicleVarietyPack/Meshes/SM_Truck_Box')
vertices = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(source, 0, 4)[0]
assert abs(max(v.y for v in vertices)-min(v.y for v in vertices)-232.463) < .1
start = next(a for a in named.values() if isinstance(a, unreal.PlayerStart))
assert abs(start.get_actor_location().x-780) < .1
assert abs(start.get_actor_location().y+1120) < .1
assert 90 < start.get_actor_location().z-ground < 105
assert len([a for a in named.values() if isinstance(a, unreal.WarehouseCargo)]) == 262

before = {label: bounds(a) for label, a in named.items()}
apply_scale()
for label, actor in named.items():
    after = bounds(actor)
    assert all(abs(before[label][j][i]-after[j][i]) < .15 for j in range(2) for i in range(3)), ('Repeated application drifts', label, before[label], after)
# Never save test mutations.
print('REAL_WORLD_SCALE_VERIFIED', '88 pallet slots, 320cm clear loaded aisle, supplier rack dimensions, 262 cargo, repeatable application')
