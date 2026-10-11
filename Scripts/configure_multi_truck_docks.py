"""Expand the existing apron to three occupied docks; preserve warehouse and Truck02 cargo.

Run in Unreal Python with the saved warehouse loaded (main() loads/saves it).
All units are cm. This creates static docked trucks, not a truck driving scheduler.
"""
from pathlib import Path
import math
import sys
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from apply_real_world_scale import fit

LEVEL = '/Game/FirstPerson/Lvl_FirstPerson'
BAYS = ((1, -100.0, '출고 도크 01'),
        (2, 350.0, '입고 도크 02'),
        (3, 800.0, '공용 도크 03'))
PLATFORM_CENTER = (350, -2465, -65)
PLATFORM_SIZE = (2100, 970, 130)
REAR_Y = -2935.0
GROUPS = tuple('LayoutGroup:TruckDock%02d' % index for index, _, _ in BAYS)


def configure_multi_truck_docks():
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    named = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    original = named['EXT_ParkedTruck_02']
    interior = [a for name, a in named.items()
                if name.startswith('TRK_') and not name.startswith('TRK_Dock0')]
    assert all(name in named for name in ('TRK_Floor', 'TRK_Roof', 'TRK_DockLeveller'))
    assert len([a for a in interior if isinstance(a, unreal.WarehouseCargo)]) == 48
    assert abs(original.get_actor_scale3d().x - 1) < .001
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    concrete = named['EXT_DockPlatform'].static_mesh_component.get_material(0)
    white = unreal.load_asset('/Game/Warehouse/Exterior/Materials/MI_Cladding')
    yellow = unreal.load_asset('/Game/Warehouse/Materials/MI_SafetyYellow')
    rubber = unreal.load_asset('/Game/Warehouse/Materials/MI_Rubber')
    assert all((cube, concrete, white, yellow, rubber))

    def tags(actor, values):
        current = [str(t) for t in actor.tags]
        actor.modify()
        actor.tags = [unreal.Name(t) for t in dict.fromkeys(current + values)]

    def group(actor, index):
        actor.modify()
        current = [str(t) for t in actor.tags if not str(t).startswith('LayoutGroup:')]
        actor.tags = [unreal.Name(t) for t in dict.fromkeys(current + [
            GROUPS[index - 1], 'LayoutType:Other', 'WarehouseFloor0'])]

    def ensure(label, cls=unreal.StaticMeshActor):
        actor = named.get(label)
        if actor is None:
            actor = actors.spawn_actor_from_class(cls, unreal.Vector())
            actor.set_actor_label(label)
            actor.set_editor_property('is_spatially_loaded', False)
            named[label] = actor
        assert isinstance(actor, cls), label
        actor.modify()
        actor.set_folder_path('Warehouse/Exterior/MultiTruckDocks')
        return actor

    def block(label, center, size, material, collision=True):
        actor = ensure(label)
        actor.set_actor_rotation(unreal.Rotator(), False)
        component = actor.static_mesh_component
        component.set_static_mesh(cube)
        component.set_editor_property('override_materials', [material])
        component.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
        actor.set_actor_scale3d(unreal.Vector(*(v / 100 for v in size)))
        actor.set_actor_location(unreal.Vector(*center), False, False)
        return actor

    # Move the complete original assembly, including its existing leveller, by
    # one measured delta. Absolute rear alignment makes repeated runs drift-free.
    center, extent = original.get_actor_bounds(False)
    delta = unreal.Vector(350 - center.x, REAR_Y - center.y - extent.y, 0)
    for actor in [original] + interior:
        actor.modify()
        actor.set_actor_location(actor.get_actor_location() + delta, False, False)
        group(actor, 2)
    tags(named['TRK_Roof'], ['ObserverRoof'])

    # The former long plate sank into the newly extended apron. Fit its actual
    # endpoints to the cargo floor and the new dock edge, keeping its underside
    # above the dock instead of accepting an intentional fixed-body overlap.
    floor_center, floor_extent = named['TRK_Floor'].get_actor_bounds(False)
    inside = unreal.Vector(floor_center.x, floor_center.y + floor_extent.y - 147,
                           floor_center.z + floor_extent.z + 2)
    end = unreal.Vector(floor_center.x, PLATFORM_CENTER[1] - PLATFORM_SIZE[1] / 2 + 2, 3)
    span = end - inside
    bridge = named['TRK_DockLeveller']
    bridge.modify()
    bridge.set_actor_location_and_rotation((inside + end) * .5,
        unreal.Rotator(pitch=math.degrees(math.atan2(span.z, span.y)), yaw=90), False, False)
    bridge.set_actor_scale3d(unreal.Vector(span.length() / 100, 2, .04))

    def copy_part(source, label, offset, index):
        cls = unreal.StaticMeshActor if isinstance(source, unreal.StaticMeshActor) else unreal.RectLight
        actor = ensure(label, cls)
        actor.set_actor_location(source.get_actor_location() + offset, False, False)
        actor.set_actor_rotation(source.get_actor_rotation(), False)
        actor.set_actor_scale3d(source.get_actor_scale3d())
        if isinstance(source, unreal.StaticMeshActor):
            source_part = source.static_mesh_component
            part = actor.static_mesh_component
            part.set_static_mesh(source_part.static_mesh)
            part.set_editor_property('override_materials', source_part.get_editor_property('override_materials'))
            part.set_mobility(source_part.get_editor_property('mobility'))
            part.set_collision_enabled(source_part.get_collision_enabled())
            part.set_collision_profile_name(source_part.get_collision_profile_name())
        else:
            assert isinstance(source, unreal.RectLight), source.get_actor_label()
            source_part = source.get_component_by_class(unreal.RectLightComponent)
            part = actor.get_component_by_class(unreal.RectLightComponent)
            for key in ('intensity', 'attenuation_radius', 'source_width',
                        'source_height', 'use_temperature', 'temperature'):
                part.set_editor_property(key, source_part.get_editor_property(key))
        group(actor, index)
        if source.get_actor_label() == 'TRK_Roof':
            tags(actor, ['ObserverRoof'])
        return actor

    additions = []
    shell = [a for a in interior if not isinstance(a, unreal.WarehouseCargo)]
    for index, x, _ in BAYS:
        if index == 2:
            continue
        offset = unreal.Vector(x - 350, 0, 0)
        additions.append(copy_part(original, 'EXT_ParkedTruck_%02d' % index, offset, index))
        for source in shell:
            additions.append(copy_part(source, 'TRK_Dock%02d_' % index + source.get_actor_label()[4:], offset, index))

    # 21 x 9.7 m platform, 4.5 m centre pitch. The original pedestrian ramp
    # and landing still meet its west edge, and the warehouse doorway is retained.
    fit(named['EXT_DockPlatform'], PLATFORM_SIZE, PLATFORM_CENTER)
    fit(named['EXT_Canopy'], (2300, 1240, 28), (350, -2580, 445))
    fit(named['EXT_CanopyFascia'], (2300, 25, 80), (350, -3200, 410))
    for old, x in ((-665, -675), (665, 1375)):
        column = named[f'EXT_CanopyColumn_{old}']
        fit(column, (16, 16, 434), (x, -2690, 217))
        guard = named[f'EXT_DockGuard_{old}']
        guard.modify()
        guard.set_actor_location(unreal.Vector(x, -2940, -70), False, False)

    # Existing bay actors are reused instead of accumulating duplicate paint.
    for index, x, title in BAYS:
        for side in ('Left', 'Right'):
            side_x = x + (-215 if side == 'Left' else 215)
            block(f'EXT_Bay{index}_{side}', (side_x, -3630, -119.85), (8, 1400, .2), white, False)
        block(f'EXT_Bay{index}_End', (x, -4330, -119.85), (430, 8, .2), white, False)
        for old_y in range(-3800, -2600, 200):
            block(f'EXT_Bay{index}_Center_{old_y}', (x, old_y - 400, -119.85), (5, 85, .2), yellow, False)
        for side in (-1, 1):
            block(f'EXT_Dock{index}_Bumper_{side}', (x + side * 140, -2958, -43), (25, 20, 82), rubber)
        target = ensure(f'EXT_TruckDockTarget_{index:02}', unreal.TargetPoint)
        target.set_actor_location(unreal.Vector(x, -3520, -120), False, False)
        target.set_actor_rotation(unreal.Rotator(yaw=-90), False)
        handoff = ensure(f'EXT_DockHandoffTarget_{index:02}', unreal.TargetPoint)
        handoff.set_actor_location(unreal.Vector(x, -2680, 0), False, False)
        handoff.set_actor_rotation(unreal.Rotator(yaw=-90), False)
        sign = ensure(f'EXT_Dock{index}_Sign', unreal.TextRenderActor)
        sign.set_actor_location(unreal.Vector(x, -2976, 385), False, False)
        sign.set_actor_rotation(unreal.Rotator(yaw=-90), False)
        text = sign.get_component_by_class(unreal.TextRenderComponent)
        text.set_text('DOCK %02d' % index)
        text.set_world_size(50)
        text.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
        ui = ensure(f'EXT_DockAreaUI_{index:02}', unreal.WarehouseAreaLabel)
        ui.set_actor_location(unreal.Vector(x, -2710, 275), False, False)
        ui.set_editor_property('area_name', unreal.Text(title))
        ui.set_editor_property('floor_index', 0)
        ui.set_editor_property('view_distance_cm', 4500)
        ui.set_actor_enable_collision(False)
        tags(ui, ['WarehouseAreaLabel', 'WarehouseFloor0'])
    if 'WH_AreaUI_Loading' in named:
        named['WH_AreaUI_Loading'].modify()
        named['WH_AreaUI_Loading'].set_actor_location(unreal.Vector(350, -2250, 310), False, False)

    # Preserve all calibrated profiles, adding fixed-truck/shell profiles only.
    # No reset of warehouse strength, cargo recipes or fleet navigation anchors.
    system = named['WH_DamageSystem']
    specs = list(system.get_editor_property('objects'))
    known = {str(spec.name) for spec in specs}
    for actor in additions:
        if not isinstance(actor, unreal.StaticMeshActor) or actor.get_actor_label() in known:
            continue
        spec = unreal.WarehouseStrength()
        spec.set_editor_property('name', actor.get_actor_label())
        spec.set_editor_property('members', [actor])
        spec.set_editor_property('physics_meshes', [None])
        spec.set_editor_property('failure', unreal.WarehouseFailure.STRUCTURE)
        cab = actor.get_actor_label().startswith('EXT_ParkedTruck_')
        spec.set_editor_property('mass_kg', 6000 if cab else 80)
        spec.set_editor_property('rated_load_kg', 4000 if cab else 3000)
        spec.set_editor_property('impact_yield_j', 1500 if cab else 2000)
        spec.set_editor_property('impact_failure_j', 15000 if cab else 20000)
        specs.append(spec)
    system.modify()
    system.set_editor_property('objects', specs)
    print('MULTI_TRUCK_DOCKS_CONFIGURED', '3 static open trucks, 450cm centre pitch, platform 2100x970cm, original 48 cartons retained')
    return named


if __name__ == '__main__':
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level(LEVEL)
    configure_multi_truck_docks()
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    print('MULTI_TRUCK_DOCKS_SAVED')
    unreal.SystemLibrary.quit_editor()
