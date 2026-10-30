"""Legacy exterior rebuild: recreate the Fab truck and remove old decorative planting.

This also reapplies the original warehouse scale/strength setup. It is not the
narrow vegetation-removal tool for an existing, hand-edited operating map.
"""
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(__file__))
from prepare_warehouse_assets import collision_mesh, EXTERIOR_MESHES


def apply_assets():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
    existing = actors.get_all_level_actors()
    assert any(a.get_actor_label() == 'EXT_DockPlatform' for a in existing)
    # Only the truck pack remains required; the yard uses its existing green strips.
    meshes = {}
    for name, path in EXTERIOR_MESHES.items():
        assert unreal.EditorAssetLibrary.does_asset_exist(path), 'Install Fab pack: ' + path
        meshes[name] = collision_mesh(unreal.load_asset(path))
    for actor in existing:
        if actor.get_actor_label().startswith(('EXT_TreeTrunk_', 'EXT_TreeCrown_',
                                              'EXT_Beech_', 'EXT_Shrub_', 'EXT_ParkedTruck_')):
            actors.destroy_actor(actor)

    def place(label, mesh, x, y, ground, scale=1.0, yaw=0.0):
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x,y,0), unreal.Rotator(yaw=yaw))
        actor.set_actor_label(label)
        actor.set_folder_path('Warehouse/Exterior/Fab')
        actor.set_editor_property('is_spatially_loaded', False)
        comp = actor.static_mesh_component
        comp.set_static_mesh(mesh)
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        comp.set_collision_profile_name('BlockAll')
        actor.set_actor_scale3d(unreal.Vector(scale,scale,scale))
        center, extent = actor.get_actor_bounds(False)
        actor.set_actor_location(unreal.Vector(x,y,ground-center.z+extent.z), False, False)
        return actor

    truck = place('EXT_ParkedTruck_02', meshes['truck'], 350, -3000, -120, yaw=-90)
    center, extent = truck.get_actor_bounds(False)
    pos = truck.get_actor_location()
    # Rear is 17 cm from the bumper face; leave dock 01 and the approach open.
    truck.set_actor_location(unreal.Vector(pos.x,pos.y-2535-center.y-extent.y,pos.z), False, False)
    from apply_real_world_scale import apply_scale
    apply_scale()
    from configure_warehouse_strength import configure_strength
    configure_strength()
    assert level.save_current_level()
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('EXTERIOR_FAB_APPLIED', '1 truck; decorative trees/shrubs omitted, existing green strips retained')


if __name__ == '__main__':
    apply_assets()
