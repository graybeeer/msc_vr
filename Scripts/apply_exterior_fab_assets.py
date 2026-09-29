"""Replace only exterior truck/planting actors, preserving hand-edited buildings."""
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
    # Validate and prepare all packs before removing any existing planting.
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
        if label.startswith(('EXT_Beech_', 'EXT_Shrub_')):
            # UE 5.8 Nanite capture path loses fine foliage; use authored fallback.
            comp.set_editor_property('disallow_nanite', True)
            comp.set_editor_property('can_ever_affect_navigation', False)
        actor.set_actor_scale3d(unreal.Vector(scale,scale,scale))
        center, extent = actor.get_actor_bounds(False)
        actor.set_actor_location(unreal.Vector(x,y,ground-center.z+extent.z), False, False)
        return actor

    truck = place('EXT_ParkedTruck_02', meshes['truck'], 350, -3000, -120, yaw=-90)
    center, extent = truck.get_actor_bounds(False)
    pos = truck.get_actor_location()
    # Rear is 17 cm from the bumper face; leave dock 01 and the approach open.
    truck.set_actor_location(unreal.Vector(pos.x,pos.y-2535-center.y-extent.y,pos.z), False, False)
    for index,(x,y) in enumerate(((-3400,-5600),(-3400,-3400),(-3400,-1200),(3400,-4600),(3400,-2200))):
        place(f'EXT_Beech_{index}', meshes['tree'], x,y,-120,1.35+index*.04,index*67)
        for offset in (-1,0,1):
            place(f'EXT_Shrub_{index}_{offset}', meshes['shrub'], x+offset*125,y+180,-130,
                  .11+(index%3)*.01,index*41+offset*70)
    from apply_real_world_scale import apply_scale
    apply_scale()
    assert level.save_current_level()
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('EXTERIOR_FAB_APPLIED', '1 truck, 5 beech trees, 15 shrubs')


if __name__ == '__main__':
    apply_assets()
