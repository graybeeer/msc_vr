"""Read-only runtime asset dependency inventory. Run in the project editor.

Seeds: shipped maps, production C++ asset paths (including runtime name prefixes)
and Config references. Recurses hard/soft package dependencies without saving maps.
The report separates required project packages from engine/plugin prerequisites.
"""
import json
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
packages = {str(a.package_name): a for a in registry.get_all_assets(False)
            if str(a.package_name).startswith('/Game/')}
references = {}
engine_references = set()
unresolved = {}
sources = list((ROOT / 'Source/msc_vr').rglob('*.cpp')) + list((ROOT / 'Source/msc_vr').rglob('*.h'))
sources += [p for p in (ROOT / 'Config').glob('*.ini') if not p.name.startswith('DefaultEditor')]
for source in sources:
    if 'Tests' in source.parts:
        continue
    text = source.read_text(encoding='utf-8-sig')
    # Preserve quoted paths while removing comments (including URLs in strings).
    text = re.sub(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*',
                  lambda m: m[0] if m[0].startswith('"') else '', text, flags=re.S)
    if source.suffix == '.ini':
        text = '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith((';', '#')))
    for value in re.findall(r'/Engine/[A-Za-z0-9_./]+', text):
        package = value.split('.')[0]
        if registry.get_assets_by_package_name(package):
            engine_references.add(package)
    for value in re.findall(r'/Game/[A-Za-z0-9_./-]+', text):
        path = value.split('.')[0]
        matched = [path] if path in packages else []
        # Literal prefixes used to assemble runtime model/portrait/map names.
        if not matched and (path.endswith('_') or path.endswith('/Stage')):
            matched = [p for p in packages if p.startswith(path)]
        # Directory settings/base paths are not standalone packages. Do not
        # silently discard a missing direct LoadObject path, however.
        folder = any(p.startswith(path.rstrip('/') + '/') for p in packages)
        if not matched and not folder:
            unresolved.setdefault(path, []).append(str(source.relative_to(ROOT)).replace('\\', '/'))
        for package in matched:
            references.setdefault(package, []).append(str(source.relative_to(ROOT)).replace('\\', '/'))
for path in ('/Game/FirstPerson/Lvl_FirstPerson', '/Game/Warehouse/Play/Maps/MainMenu',
             '/Game/Warehouse/Play/Maps/Stage01', '/Game/Warehouse/Play/Maps/Stage02', '/Game/Warehouse/Play/Maps/Stage03'):
    assert path in packages, path
    references.setdefault(path, []).append('shipped map')
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
    include_hard_package_references=True, include_searchable_names=False,
    include_soft_management_references=False, include_hard_management_references=False)
seen, external, missing, records = set(), engine_references, set(unresolved), []
pending = list(sorted(references))
while pending:
    package = pending.pop()
    if package in seen:
        continue
    seen.add(package)
    asset = packages.get(package)
    candidates = [ROOT / 'Content' / (package[6:] + extension) for extension in ('.uasset', '.umap')]
    file = next((p for p in candidates if p.is_file()), None)
    if not file:
        missing.add(package)
        continue
    dependencies = sorted(str(p) for p in registry.get_dependencies(package, options))
    records.append(dict(package=package, path=str(file.relative_to(ROOT)).replace('\\', '/'),
        asset_class=str(asset.asset_class_path.asset_name) if asset else 'Unknown', bytes=file.stat().st_size,
        roots=sorted(set(references.get(package, []))), dependencies=dependencies))
    for dependency in dependencies:
        if dependency.startswith('/Game/'):
            pending.append(dependency)
        else:
            external.add(dependency)
report = dict(schema=1, generated_utc=datetime.now(timezone.utc).isoformat(),
    seeds={p: sorted(set(v)) for p,v in sorted(references.items())},
    unresolved_direct_references={p: sorted(set(v)) for p,v in sorted(unresolved.items())},
    assets=sorted(records, key=lambda x: x['path']), missing_project_packages=sorted(missing),
    engine_plugin_dependencies=sorted(external), total_bytes=sum(r['bytes'] for r in records))
output = ROOT / 'Saved/Testing/RuntimeAssets/Inventory.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('RUNTIME_ASSET_INVENTORY', len(records), report['total_bytes'], 'missing', sorted(missing), str(output))

# Publish the exact versioned manifest only after every direct/package reference
# resolves. Never add files to Git or save an Unreal package here.
if not missing:
    groups = {
        'warehouse': ('Warehouse / Quixel Megascans', '창고 랙·팔레트·바닥·설비와 파생 충돌', 'https://www.fab.com/listings/a3149fab-3906-4043-b6ee-3937b752a06c'),
        'truck': ('Vehicle Variety Pack / Switchboard Studios', '물류 트럭과 파생 적재함', 'https://www.fab.com/listings/dc1ada50-2523-44b1-b0e2-a72d14076fb4'),
        'beech': ('European Beech / Quixel Megascans', '외부 나무·파생 충돌 메시', 'https://www.fab.com/listings/d11cc01d-9422-41b7-950f-416c9ce79caf'),
        'shrubs': ('Free Shrubs Pack / Greenleaf Vision', '외부 관목·파생 충돌 메시', 'https://www.fab.com/listings/7ca465ab-fb9c-4d6b-bddb-82c20f604657'),
        'boxes': ('Boxes & Pallets Pack / IINickE', '온전한 상자와 파생 재질', 'https://www.fab.com/listings/a7c9776e-0a8e-424b-8f7c-f4d746097f0f'),
        'animation': ('Game Animation Sample / Epic Games', '이동·앉기·점프·착지의 사용 클립과 블렌드', 'https://www.fab.com/listings/880e319a-a59e-4ed2-b268-b32dac7fa016'),
        'template': ('Unreal 기본 캐릭터·입력·1인칭 템플릿 / Epic Games', '플레이어·NPC·입력·파생 캐릭터 페이드 재질', ''),
        'shared': ('Megascans 공통 재질', '사용 중인 외부 에셋이 참조하는 MSPresets', ''),
        'project': ('프로젝트 구성/팀원 제작·사용자 제공 모델', 'AGV·승강기·충전기·컨베이어·장식·UI·공통 표시 재질', ''),
        'map_objects': ('프로젝트 맵 배치 데이터', '맵이 참조하는 외부 액터/오브젝트 패키지', ''),
    }
    def group(path):
        tail=path.removeprefix('Content/')
        for prefixes,key in (
            (('__ExternalActors__/','__ExternalObjects__/'),'map_objects'),
            (('Warehouse/AnimationSample/',),'animation'),
            # Collision copies retain the model pack's actual provenance.
            (('Warehouse/Collision/SM_EuropeanBeech_',),'beech'),
            (('Warehouse/Collision/GV_Vol7_Shrub_',),'shrubs'),
            (('Warehouse/Collision/SM_Truck_',),'truck'),
            (('Warehouse/Materials/ObserverWalls/MI_ObserverWall_MI_Ind_War_',
              'Warehouse/Materials/ObserverWalls/M_ObserverWall_M_MS_Surface_'),'warehouse'),
            (('Warehouse/Materials/RemoteCharacter/MI_RemoteFade_MI_Manny_',
              'Warehouse/Materials/RemoteCharacter/MI_RemoteFade_MI_Quinn_',
              'Warehouse/Materials/RemoteCharacter/M_RemoteFade_M_Mannequin'),'template'),
            (('Scene_Warehouse/','Warehouse/Collision/','Warehouse/Physics/'),'warehouse'),
            (('Warehouse/Cargo/BoxesPalletsPack/',),'boxes'),
            (('VehicleVarietyPack/','Warehouse/Exterior/TruckInterior/'),'truck'),
            (('EuropeanBeech/',),'beech'),(('GV_FreeShrubsPack/',),'shrubs'),
            (('MSPresets/',),'shared'),(('Characters/','FirstPerson/','Input/'),'template')):
            if tail.startswith(prefixes):return key
        return 'project'
    for record in report['assets']:
        record['source_group']=group(record['path'])
        record['public_git_policy']='project_or_scene_reference' if record['source_group'] in ('project','map_objects') else 'external_source_or_derivative_no_new_upload'
    report['source_groups']={key:dict(name=value[0],use=value[1],source=value[2]) for key,value in groups.items()}
    destination=ROOT/'Config/RuntimeAssets.json'
    destination.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    lines=['# 게임 사용 에셋 목록','',
        '관리자 창고·메인 메뉴·플레이 스테이지 3개, C++/설정의 직접 로딩과 이름 조합 로딩을 시작점으로 실제 하드/소프트 참조를 수집한다. 이것은 로컬 실행에 필요한 전체 목록이며, 공개 Git 저장소에 모든 파일을 올린다는 뜻은 아니다. 파일은 원래 경로에 두어 맵·재질 참조가 끊기지 않게 한다.', '',
        '**2026-10-28 공개 저장소 기준:** 코드·자체 제작물·맵 참조·문서만 추가한다. 외부 팩과 모션, 원본을 복사·변형한 충돌/재질/트럭 파일은 새로 업로드하지 않고 팀원이 별도 설치·재생성한다. 기존부터 추적되던 기본 템플릿과 과거 기록은 유지한다. JSON의 `public_git_policy`는 이번 추가 정책이며 실제 Git 추적 상태와 구분한다.', '',
        '모든 파일·종류·원본 그룹·직접 참조·코드 진입점은 [정확한 목록](Config/RuntimeAssets.json)에 기록한다. 아래에는 이름이 있는 모델·재질·텍스처·모션·맵을 나열한다. GUID 이름의 외부 액터/오브젝트는 같은 JSON의 `map_objects` 그룹에 전부 포함한다.', '',
        '팀원 설치·Git LFS 받기·원본 재생성은 [TEAM_ASSETS.md](TEAM_ASSETS.md), 실제 기능은 [WAREHOUSE.md](WAREHOUSE.md)를 따른다. 에셋의 기존 라이선스와 출처는 유지된다. 엔진/플러그인 자체 파일은 저장소에 복사하지 않으며 목록의 `engine_plugin_dependencies`에 기록한다.', '',
        '충돌 복사본·캐릭터 페이드·창고 벽 페이드는 원본 팩으로 분류한다. 여러 출처를 조합한 맵·공통 표시·모델 캡처 아이콘은 프로젝트 구성으로 분류하되 원본의 이용 조건을 승계한다. 단순히 프로젝트 폴더로 복사하거나 재질을 수정했다고 자체 원본이 되는 것은 아니다.', '',
        '목록은 에셋 파일의 하드/소프트 참조를 보존하는 보수적인 의존성 폐쇄이며, 파일 내부의 미리보기·기본 재질 참조도 포함될 수 있다. 실제 Cook 결과의 바이트별 최소 목록을 뜻하지 않는다. 에디터 전용 설정의 예제 맵·콘텐츠 브라우저 폴더는 시작점에서 제외한다. 직접 로딩 경로가 없으면 보고서의 `unresolved_direct_references`에 기록하고 최신 목록 발행을 중단한다.', '',
        f'총 {len(records):,}개 파일 · {report["total_bytes"]/1024**3:.2f} GiB. C++ 동적 경로·맵 참조가 달라지면 `Scripts/audit_runtime_assets.py`를 Unreal Python에서 다시 실행한다. 새 목록만으로 Git에 자동 추가하지 않는다.', '',
        '| 분류 / 출처 | 용도 | 파일 수 | MiB |','| --- | --- | ---: | ---: |']
    for key,(name,use,url) in groups.items():
        selected=[r for r in records if r['source_group']==key]
        title=f'[{name}]({url})' if url else name
        lines.append(f'| {title} | {use} | {len(selected)} | {sum(r["bytes"] for r in selected)/1024**2:.2f} |')
    for key,(name,_,_) in groups.items():
        if key=='map_objects':continue
        lines+=['',f'## {name}','','| 에셋 경로 | 종류 | MiB |','| --- | --- | ---: |']
        for record in report['assets']:
            if record['source_group']==key:
                lines.append(f'| `{record["path"]}` | {record["asset_class"]} | {record["bytes"]/1024**2:.3f} |')
    (ROOT/'RUNTIME_ASSETS.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'Scripts/update_structure_summary.ps1')],cwd=str(ROOT),check=True)
