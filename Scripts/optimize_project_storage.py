"""Preview/apply local storage cleanup after a fresh Unreal runtime asset audit.

Only ignored, unreferenced packages in downloaded packs and generated caches
are eligible. Tracked files, source art, user saves and test captures are kept.
Run with the editor closed. Git asset removal is deliberately a separate review.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parents[1]
PACKS = ('Content/EuropeanBeech', 'Content/GV_FreeShrubsPack',
         'Content/Scene_Warehouse', 'Content/VehicleVarietyPack', 'Content/MSPresets',
         'Content/Warehouse/Collision', 'Content/Warehouse/Physics')
CACHES = ('Intermediate', '.vs', 'DerivedDataCache', 'Saved/Cooked',
          'Saved/Shaders', 'Saved/ShaderDebugInfo')
PACKAGE_EXTENSIONS = {'.uasset', '.umap', '.ubulk', '.uexp', '.uptnl'}


def inside(relative):
    path = ROOT / relative
    if not path.resolve().is_relative_to(ROOT) or path.is_symlink():
        raise RuntimeError(f'Unsafe cleanup path: {relative}')
    # Reject reparse directories, including junctions within the workspace.
    for parent in (path, *path.parents):
        if parent == ROOT:
            break
        if parent.exists() and parent.lstat().st_file_attributes & 0x400:
            raise RuntimeError(f'Reparse point in cleanup path: {relative}')
    return path


def git_paths(*args):
    data = subprocess.check_output(['git', *args, '-z', '--', *PACKS, *CACHES], cwd=ROOT)
    return {p.decode('utf-8') for p in data.split(b'\0') if p}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true', help='Delete eligible files after preflight')
    parser.add_argument('--caches', action='store_true', help='Also remove generated local project caches')
    parser.add_argument('--archive-only', action='store_true', help='Back up unused packages; delete nothing')
    parser.add_argument('--archive', help='Verified local ZIP backup required before removing downloaded packages')
    args = parser.parse_args()
    if args.archive_only and args.apply:
        parser.error('--archive-only and --apply are mutually exclusive')
    active = subprocess.check_output(['powershell.exe', '-NoProfile', '-Command',
        "Get-Process UnrealEditor,UnrealEditor-Cmd,UnrealPak,UnrealBuildTool,MSBuild,cl,link,ShaderCompileWorker -ErrorAction SilentlyContinue | Select-Object -ExpandProperty ProcessName; exit 0"],
        text=True, cwd=ROOT).strip()
    if active:
        raise RuntimeError(f'Close editors/build tools before cleanup: {active}')
    manifest = json.loads((ROOT/'Config/RuntimeAssets.json').read_text(encoding='utf-8-sig'))
    if manifest.get('missing_project_packages') or manifest.get('unresolved_direct_references'):
        raise RuntimeError('Runtime dependency audit has unresolved references')
    generated = datetime.fromisoformat(manifest['generated_utc']).timestamp()
    if datetime.now(timezone.utc).timestamp()-generated > 86400:
        raise RuntimeError('Run audit_runtime_assets.py in Unreal again; manifest is over 24 hours old')
    inputs = [*ROOT.joinpath('Source/msc_vr').rglob('*.cpp'), *ROOT.joinpath('Source/msc_vr').rglob('*.h')]
    inputs += [p for p in (ROOT/'Config').glob('*.ini') if not p.name.startswith('DefaultEditor')]
    stale = [str(p.relative_to(ROOT)) for p in inputs if p.stat().st_mtime > generated]
    if stale:
        raise RuntimeError(f'Sources changed after runtime audit: {stale}')
    required = set()
    for row in manifest['assets']:
        path = inside(row['path'])
        if not path.is_file() or path.stat().st_size != row['bytes']:
            raise RuntimeError(f'Missing or changed required asset: {row["path"]}')
        if path.stat().st_mtime > generated:
            raise RuntimeError(f'Asset changed after runtime audit: {row["path"]}')
        with path.open('rb') as stream:
            if stream.read(48).startswith(b'version https://git-lfs.github.com/spec/v1'):
                raise RuntimeError(f'Run git lfs pull first: {row["path"]}')
        required.add(row['path'])
        required.update(str(Path(row['path']).with_suffix(ext)).replace('\\', '/')
                        for ext in ('.ubulk', '.uexp', '.uptnl'))
    tracked = git_paths('ls-files')
    ignored = git_paths('ls-files', '--others', '--ignored', '--exclude-standard')
    candidates = []
    for prefix in (*PACKS, *(CACHES if args.caches else ())):
        folder = inside(prefix)
        if not folder.exists():
            continue
        for path in folder.rglob('*'):
            relative = path.relative_to(ROOT).as_posix()
            if not path.is_file() or relative in required or relative in tracked or relative not in ignored:
                continue
            if prefix in PACKS and path.suffix.lower() not in PACKAGE_EXTENSIONS:
                continue
            inside(relative)
            candidates.append(dict(path=relative, bytes=path.stat().st_size,
                                   category='downloaded_unused_package' if prefix in PACKS else 'generated_cache'))
    report = dict(generated_utc=datetime.now(timezone.utc).isoformat(), applied=False,
                  required_files=len(manifest['assets']), required_bytes=manifest['total_bytes'],
                  eligible_bytes=sum(row['bytes'] for row in candidates), files=candidates)
    output = inside('Saved/Testing/StorageOptimization_20261029')
    output.mkdir(parents=True, exist_ok=True)
    destination = inside((output/('CleanupApplied.json' if args.apply else 'CleanupPreview.json')).relative_to(ROOT))
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    packages = [row for row in candidates if row['category'] == 'downloaded_unused_package']
    if args.archive_only:
        archive = inside('Saved/StorageArchives/UnusedPackages_'+datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S')+'.zip')
        archive.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=1) as backup:
            for row in packages:
                with inside(row['path']).open('rb') as source, backup.open(row['path'], 'w', force_zip64=True) as target:
                    while block := source.read(1024*1024):
                        target.write(block)
        args.archive = archive.relative_to(ROOT).as_posix()
    if args.archive:
        archive = inside(args.archive)
        # Verify every original against the recoverable copy before deleting anything.
        with zipfile.ZipFile(archive) as backup:
            for row in packages:
                hashes = []
                for stream in (inside(row['path']).open('rb'), backup.open(row['path'])):
                    with stream:
                        digest = hashlib.sha256()
                        while block := stream.read(1024*1024):
                            digest.update(block)
                        hashes.append(digest.hexdigest())
                if hashes[0] != hashes[1]:
                    raise RuntimeError(f'Archive does not match original: {row["path"]}')
                row['sha256'] = hashes[0]
        report['verified_archive'] = dict(path=archive.relative_to(ROOT).as_posix(), bytes=archive.stat().st_size,
                                        original_bytes=sum(row['bytes'] for row in packages), files=len(packages))
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    if args.apply:
        if packages and not args.archive:
            raise RuntimeError('Create a recovery copy with --archive-only, then supply --archive before --apply')
        # Complete preflight and persist the exact plan before the first unlink.
        report['deleted_files'] = 0
        report['deleted_bytes'] = 0
        try:
            for row in candidates:
                path = inside(row['path'])
                if path.stat().st_size != row['bytes']:
                    raise RuntimeError(f'Cleanup candidate changed: {row["path"]}')
                path.unlink()
                report['deleted_files'] += 1
                report['deleted_bytes'] += row['bytes']
            report['applied'] = True
        finally:
            # On failure the first deleted_files entries identify completed work.
            destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(dict(applied=report['applied'], files=len(candidates),
                         freed_or_eligible_gib=round(report['eligible_bytes']/1024**3, 3),
                         report=str(destination)), ensure_ascii=False))


if __name__ == '__main__':
    main()
