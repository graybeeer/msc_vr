"""Read-only cold-process asset/map load checks after local storage cleanup.

Run in Unreal Python with PIE stopped. Actual transport is checked separately
by verify_shipping_workflow_pie.py; this test never saves a package.
"""
import json
from pathlib import Path
import time
import traceback
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
output = root/'Saved/Testing/StorageOptimization_20261029'
output.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
report = dict(passed=False, maps=[], motions=[], scope='Cold editor asset/map loads; no save or OS input; not a packaged game')
try:
    manifest = json.loads((root/'Config/RuntimeAssets.json').read_text(encoding='utf-8-sig'))
    assert not manifest['missing_project_packages'] and not manifest['unresolved_direct_references']
    rows = manifest['assets']
    before = {}
    for row in rows:
        path = root/row['path']
        assert path.is_file() and path.stat().st_size == row['bytes'], row['path']
        with path.open('rb') as stream:
            assert not stream.read(48).startswith(b'version https://git-lfs.github.com/spec/v1'), row['path']
        before[row['path']] = (path.stat().st_size, path.stat().st_mtime_ns)
    report['required_files'] = len(rows)
    report['required_bytes'] = manifest['total_bytes']
    default = unreal.get_default_object(unreal.WarehousePallet)
    body = default.get_editor_property('body')
    assert body.static_mesh.get_path_name() == '/Game/Warehouse/Physics/SM_Pallet_110.SM_Pallet_110'
    assert (body.get_relative_transform().scale3d-unreal.Vector(1, 1, 1)).length() < 0.001
    assert not (root/'Content/Warehouse/Physics/SM_Ind_War_Storage_Pallet_Wood_Worn_01.uasset').exists()
    report['pallet_mesh'] = body.static_mesh.get_path_name()
    for row in rows:
        if row['path'].startswith('Content/Warehouse/AnimationSample/'):
            motion = unreal.load_asset(row['package'])
            assert motion and motion.get_editor_property('skeleton'), row['package']
            report['motions'].append(row['package'])
    assert len(report['motions']) == 32
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert not editor.get_game_world()
    for name in ('/Game/FirstPerson/Lvl_FirstPerson', '/Game/Warehouse/Play/Maps/MainMenu',
                 '/Game/Warehouse/Play/Maps/Stage01', '/Game/Warehouse/Play/Maps/Stage02', '/Game/Warehouse/Play/Maps/Stage03'):
        assert level.load_level(name), name
        assert editor.get_editor_world().get_path_name().startswith(name+'.'), name
        report['maps'].append(name)
    changed = [relative for relative, state in before.items()
               if ((root/relative).stat().st_size, (root/relative).stat().st_mtime_ns) != state]
    assert not changed, changed
    report['packages_unchanged'] = True
    report['passed'] = True
except Exception:
    report['error'] = traceback.format_exc()
finally:
    report['wall_seconds'] = round(time.monotonic()-started, 2)
    (output/'AssetLoadReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('RUNTIME_STORAGE_'+('PASSED' if report['passed'] else 'FAILED'), report.get('error', ''), str(output))
