"""Unsaved default-map PIE: floor-level visual wiring and exterior role workers.

No forklift state/transport or OS input is forced. The one worker teleport is an
explicit setup probe for the production relocation function, then restored by it.
"""
import hashlib
import json
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/Testing/FloorWiringWorkers'
OUT.mkdir(parents=True, exist_ok=True)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
files = [ROOT / 'Content/FirstPerson/Lvl_FirstPerson.umap']
for folder in ('__ExternalActors__', '__ExternalObjects__'):
    files.extend((ROOT / 'Content' / folder / 'FirstPerson/Lvl_FirstPerson').rglob('*.uasset'))
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
assert level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
started = time.monotonic()
phase = 'start'
checks = []
report = dict(passed=False, checks=checks, sourceMapWritten=False, osInputTested=False)


def complete(ok, detail):
    report.update(passed=ok, message=detail, wallSeconds=round(time.monotonic() - started, 2),
                  savedMapUnchanged=hashes == {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files})
    report['passed'] &= report['savedMapUnchanged']
    (OUT / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('FLOOR_WIRING_WORKERS_' + ('PASSED' if report['passed'] else 'FAILED'), detail)
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    global phase
    try:
        assert time.monotonic() - started < 180, 'Environment test timed out'
        if phase == 'start':
            level.editor_request_begin_play()
            phase = 'running'
            return
        world = editor.get_game_world()
        if not world or unreal.GameplayStatics.get_time_seconds(world) < 2:
            return
        workers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WarehouseWorker)
        assert workers, 'No role presenters to verify'
        buildings = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WarehouseBuildingLayout)
        if buildings:
            box = buildings[0].get_footprint()
        else:
            walls = [a.get_actor_bounds(True) for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
                     if a.actor_has_tag('ObserverFadeWall')]
            assert walls, 'No building controller or authored perimeter walls'
            box = unreal.Box(min=unreal.Vector(*(min(getattr(c, axis)-getattr(e, axis) for c, e in walls) for axis in ('x','y','z'))),
                             max=unreal.Vector(*(max(getattr(c, axis)+getattr(e, axis) for c, e in walls) for axis in ('x','y','z'))))

        def outside(worker):
            p = worker.get_actor_location()
            return p.x < box.min.x - 45 or p.x > box.max.x + 45 or p.y < box.min.y - 45 or p.y > box.max.y + 45

        assert all(outside(worker) for worker in workers), [(w.get_actor_label(), str(w.get_actor_location())) for w in workers]
        assert all(w.get_component_by_class(unreal.CapsuleComponent).is_simulating_physics() for w in workers)
        assert all(unreal.WarehouseDamageSystem.is_physical_contact(w.get_component_by_class(unreal.CapsuleComponent)) for w in workers)
        checks.append('All role workers are outside the building with physical collision retained; player/truck drivers are separate classes')
        assert unreal.WarehouseWorker.relocate_indoor_workers(world) == 0, 'Outside placement must be idempotent'
        original_roles = [str(w.get_editor_property('role_name')) for w in workers]
        workers[0].set_actor_location(unreal.Vector((box.min.x + box.max.x) / 2, (box.min.y + box.max.y) / 2, 96.8), False, True)
        assert unreal.WarehouseWorker.relocate_indoor_workers(world) == 1
        assert all(outside(w) for w in workers) and original_roles == [str(w.get_editor_property('role_name')) for w in workers]
        checks.append('Relocation only moves indoor workers, preserves roles and does not accumulate changes')
        unreal.WarehouseChargerWiring.refresh_connections(workers[0])
        stations = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WarehouseChargingStation)
        assert stations, 'No chargers found'
        routes = []
        for station in stations:
            points = list(station.get_editor_property('supply_cable_route'))
            assert station.get_editor_property('supply_cabinet') and len(points) >= 3, station.get_editor_property('supply_cable_status')
            floor_z = int((station.get_actor_location().z + 20) // 400) * 400
            horizontal = [(a, b) for a, b in zip(points, points[1:]) if (unreal.Vector(a.x-b.x, a.y-b.y, 0)).length() > .2]
            assert horizontal and all(abs(a.z-floor_z-2.1) < .2 and abs(b.z-floor_z-2.1) < .2 for a, b in horizontal), [str(p) for p in points]
            routes.append(dict(charger=station.get_actor_label(), points=[[p.x,p.y,p.z] for p in points]))
        cables = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor) if a.actor_has_tag('ChargerSupplyCable')]
        assert len(cables) == len(stations)
        for cable in cables:
            assert not cable.actor_has_tag('WarehouseMapStructure')
            for component in cable.get_components_by_class(unreal.PrimitiveComponent):
                assert component.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
                assert not component.is_query_collision_enabled() if hasattr(component, 'is_query_collision_enabled') else True
                assert not unreal.WarehouseDamageSystem.is_physical_contact(component)
        checks.append('Every charger cable follows the floor, has no collision and is excluded from the fixed obstacle map')
        report.update(workers=len(workers), cables=len(cables), routes=routes)
        complete(True, 'Exterior role workers and floor-level non-colliding charger cables verified')
    except Exception:
        complete(False, traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
