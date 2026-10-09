"""Prepare native rolling tyre collision without changing the supplied AGV geometry."""
import unreal
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_warehouse_assets import collision_mesh
from apply_real_world_scale import fit


def prepare_lift_pit():
    """Keep existing slabs and their identities, with clearance around the moving deck."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named = {actor.get_actor_label(): actor for actor in actors.get_all_level_actors()}
    if 'MZ_AGVElevator' not in named:
        return
    labels = ('Floor', 'MZ_GroundRearWest', 'MZ_GroundRearEast', 'MZ_GroundRear', 'MZ_LiftPitBase')
    assert all(label in named for label in labels), ('Existing lift pit slabs missing', labels)
    elevator = named['MZ_AGVElevator']
    assert (elevator.get_actor_location() - unreal.Vector(1000, 1600, 0)).length() < .01
    assert abs(elevator.get_actor_rotation().yaw - 90) < .01
    # Native deck local 350 x 240 cm at yaw 90: world 240 x 350 cm.
    # Retain the existing 20 cm side clearance, and add 1 cm at each longitudinal end.
    half_z = named['Floor'].get_actor_bounds(False)[1].z
    assert half_z > 0
    slabs = [
        ('Floor', (0, -288, -half_z), (4000, 3424, half_z * 2)),
        ('MZ_GroundRearWest', (-570.5, 1712, -half_z), (2859, 576, half_z * 2)),
        ('MZ_GroundRearEast', (1570.5, 1712, -half_z), (859, 576, half_z * 2)),
        ('MZ_GroundRear', (1000, 1888, -half_z), (282, 224, half_z * 2)),
        ('MZ_LiftPitBase', (1000, 1600, -30), (282, 352, 20)),
    ]
    for floor in (1, 2):
        z = floor * 400 - 10
        slabs.extend((
            (f'MZ_L{floor + 1}_Deck_Right', (1150, -188, z), (1300, 3224, 20)),
            (f'MZ_L{floor + 1}_Deck_RearWest', (679.5, 1612, z), (359, 376, 20)),
            (f'MZ_L{floor + 1}_Deck_RearEast', (1470.5, 1612, z), (659, 376, 20)),
        ))
    assert all(label in named for label, _, _ in slabs), 'Existing upper lift opening slabs missing'
    for label, center, size in slabs:
        actor = named[label]
        original_tags = list(actor.tags)
        fit(actor, size, center)
        actor.static_mesh_component.set_collision_profile_name('BlockAll')
        assert list(actor.tags) == original_tags, ('Lift pit tags changed', label)
        print('PHYSICAL_LIFT_PIT_SLAB', label, actor.get_actor_bounds(False))
    # Existing actor identities stay in WarehouseBuilding; no membership rebuild is needed.
    print('PHYSICAL_LIFT_PIT_CLEARANCE', 'world opening X859..1141 Y1424..1776; deck X880..1120 Y1425..1775')

assets = unreal.EditorAssetLibrary
for name in ('DriveWheel', 'LoadWheelL', 'LoadWheelR'):
    mesh = unreal.load_asset('/Game/Warehouse/AGV/Meshes/SM_Refined_AGV_' + name)
    assert mesh, name
    unreal.WarehouseForklift.configure_wheel_collision(mesh)
    assets.save_loaded_asset(mesh)
    count = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_convex_collision_count(mesh)
    assert count == 1, (name, count)
    print('PHYSICAL_TYRE_PREPARED', name, mesh.get_bounds())
print('WAREHOUSE_DYNAMICS_PREPARED')

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
prepare_lift_pit()
portable = {'WH_Utility_0', 'WH_Utility_1', 'WH_Utility_3'}
for actor in actors.get_all_level_actors():
    tag = None
    if actor.get_actor_label() in portable:
        tag = 'WarehousePortable'
        mesh = actor.get_component_by_class(unreal.StaticMeshComponent).static_mesh
        assert collision_mesh(mesh, True), actor.get_actor_label()
    elif isinstance(actor, (unreal.WarehouseForklift, unreal.AgvTestVehicle)):
        tag = 'WarehousePhysicalVehicle'
    elif isinstance(actor, unreal.WarehouseWorker):
        tag = 'WarehousePhysicalHuman'
    elif isinstance(actor, unreal.WarehouseElevator):
        tag = 'WarehousePhysicalRig'
    if tag:
        actor.modify()
        tags = list(actor.tags)
        if unreal.Name(tag) not in tags: tags.append(unreal.Name(tag))
        actor.tags = tags
        print('PHYSICAL_ACTOR_CLASSIFIED', actor.get_actor_label(), tag)
assert level.save_current_level()
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
print('WAREHOUSE_DYNAMICS_APPLIED')
