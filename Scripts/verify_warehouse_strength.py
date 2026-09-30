"""Saved-map strength integration: contact loads, overload, impact, failure lockout and collapse bodies."""
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def load():
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    # Cold editor launches may still be cooking the collapse mesh collision.
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world,'Editor.AsyncAssetCompilationFinishAll')
    named = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    system = named['WH_DamageSystem']
    system.initialize_strength()
    return named, system


def specs(system):
    return {s.name: s for s in system.get_editor_property('objects')}


named, system = load()
initial = specs(system)
assert len(initial) > 400, len(initial)
assert all(s.mass_kg > 0 and s.rated_load_kg > 0 for s in initial.values())
assert all(not s.failed and s.damage == 0 for s in initial.values())
rack = initial['PalletRack_Left_00']
def mass(label):
    return named[label].get_editor_property('gross_mass_kg')
level_loads=[50+sum(mass(f'WH_Cargo_Left_00_{tier}{slot}{top}') for slot in ('','_B') for top in ('','_Top')) for tier in range(2)]
rack_load=sum(level_loads)
assert abs(rack.supported_kg-rack_load)<.5, (rack.supported_kg,rack_load)
assert abs(rack.peak_level_kg-max(level_loads))<.5
assert abs(initial['WH_Pallet_Left_00_0'].supported_kg-mass('WH_Cargo_Left_00_0')-mass('WH_Cargo_Left_00_0_Top'))<.5
assert abs(named['WH_Cargo_Left_00_0'].get_component_by_class(unreal.StaticMeshComponent).get_mass()-named['WH_Cargo_Left_00_0'].get_editor_property('gross_mass_kg')) < .1
for _ in range(100):
    system.advance_strength(.1)
assert all(s.damage == 0 for s in specs(system).values()), [(s.name,s.damage,s.supported_kg,s.rated_load_kg) for s in specs(system).values() if s.damage>0]

# Remove a box from its supporting stack: mass must follow placement, not the original actor label.
box = named['WH_Cargo_Left_00_0_Top']
box.set_actor_location(box.get_actor_location()+unreal.Vector(600,0,100),False,False)
system.advance_strength(.1)
assert abs(specs(system)['PalletRack_Left_00'].supported_kg-(rack_load-mass('WH_Cargo_Left_00_0_Top'))) < .5

# A per-level overload can fail a bay even when its total rated load has not been exceeded.
profiles = list(system.get_editor_property('objects'))
for profile in profiles:
    if profile.name == 'PalletRack_Left_00':
        profile.set_editor_property('level_capacity_kg', min(level_loads)*.4)
system.set_editor_property('objects', profiles)
for _ in range(300):
    system.advance_strength(.1)
    if specs(system)['PalletRack_Left_00'].failed:
        break
assert specs(system)['PalletRack_Left_00'].failed
for part in 'ABCDEFGH':
    body = named['WH_Rack_Left_00_'+part].static_mesh_component
    assert body.is_simulating_physics(), part
    assert unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_collision_complexity(body.static_mesh) != unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
assert named['WH_Pallet_Left_00_0'].static_mesh_component.is_simulating_physics(), 'Pallet remained suspended'
assert named['WH_Cargo_Left_00_0'].get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics(), 'Box remained suspended'

named, system = load()
vehicle = named['WH_AutonomousForklift']
system.apply_impact(vehicle, -1)
system.apply_impact(vehicle, 400)
assert not system.has_failed(vehicle) and specs(system)['WH_AutonomousForklift'].damage == 0
system.apply_impact(vehicle, 1400)
assert .49 < specs(system)['WH_AutonomousForklift'].damage < .51
system.apply_impact(vehicle, 1400)
assert system.has_failed(vehicle)
vehicle.toggle_power()
vehicle.request_charging()
vehicle.advance_simulation(.05)
assert vehicle.get_editor_property('mechanical_failure') and not vehicle.get_editor_property('powered')
assert 'MECHANICAL FAILURE' in vehicle.get_editor_property('status')

pallet = named['WH_TrainingPallet']
system.apply_impact(pallet, 1000)
assert system.has_failed(pallet)
assert abs(pallet.get_actor_scale3d().z-.6)<.001
assert pallet.get_component_by_class(unreal.StaticMeshComponent).is_simulating_physics()
print('WAREHOUSE_STRENGTH_VERIFIED contact loads, moved loads, level overload, physical rack collapse, falling stacks, impact accumulation, machine lockout, pallet crushing')
