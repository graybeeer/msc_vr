# 팀원 에셋 받기와 선택적 재생성

대상 프로젝트는 `msc_vr`, 개발 엔진은 **Unreal Engine 5.8.2**입니다. 이 문서는 Git/LFS 에셋 수신과 빌드, 원본 팩이 필요한 선택적 재생성 절차를 담당합니다. 게임 실행과 조작은 [README.md](README.md), 맵과 시뮬레이션 설정은 [WAREHOUSE.md](WAREHOUSE.md)를 확인합니다.

**2026-10-28 사용 에셋 선별 기준:** 게임에서 사용하는 외부 모델·재질·텍스처·모션과 의존 에셋을 Git에 포함합니다. 일반 설치는 최신 Git 변경과 LFS 파일을 받은 뒤 빌드하는 방식입니다. 미사용 팩 전체, 전체 Game Animation Sample 프로젝트, 원본 FBX ZIP은 일반 실행에 필요하지 않으며 함께 추가하지 않습니다.

외부 에셋의 출처·라이선스 원문은 유지하며 Git 포함 자체가 별도의 재배포 권한을 부여하지 않습니다. 사용하지 않게 된 파일은 현재 Git 추적 대상에서 제외하되 작업 PC의 원본은 보존합니다. 기존 Git 기록을 다시 쓰지는 않습니다.

[게임 사용 에셋 목록](RUNTIME_ASSETS.md)과 `Config/RuntimeAssets.json`은 **Git에 포함할 런타임 파일·의존성·출처의 기준**입니다. 파생 재질·충돌 메시도 원본 출처를 따라 분류합니다. 에셋 경로는 그대로 유지하며, 목록 생성 자체가 Git 커밋이나 LFS 다운로드를 수행하지는 않습니다.

## 일반 설치와 실행

1. 에디터를 닫고 저장소의 최신 변경을 받습니다. 프로젝트 폴더에서 `git lfs install --local`, `git lfs pull`을 실행해 실제 Unreal 에셋을 수신합니다.
2. `git lfs status`로 LFS 상태를 확인합니다. 사용 모션과 파생 충돌·재질도 수신 대상이므로 일반 설치에서 원본 팩4개를 다시 임포트하거나 맵 생성 스크립트를 실행할 필요는 없습니다.
3. Unreal Engine 5.8.2와 Visual Studio의 게임 C++ 개발 도구로 `msc_vrEditor / Development Editor / Win64`를 빌드합니다.
4. 설치 완료 항목을 확인한 후 `msc_vr.uproject`를 열고 메인 메뉴에서 모드를 선택합니다. 조작은 [창고 안내](WAREHOUSE.md)를 따릅니다.

`.uasset`가 `version https://git-lfs.github.com/spec/v1`로 시작하는 작은 텍스트 파일로 남았다면 LFS 실제 파일을 받지 못한 상태입니다. `git lfs pull`의 다운로드 오류와 저장소 접근 상태를 먼저 확인합니다. 사용 목록의 파일 자체가 없으면 최신 에셋 커밋을 받았는지 확인합니다. 전체 팩 재임포트는 원본 변경이나 결과 재생성이 필요한 경우에만 선택합니다. 에셋 폴더를 임의로 옮기면 맵·재질 참조가 끊기므로 원래 경로를 유지합니다.

**2026-10-28 외부 식생 단순화 이전 검증:** 당시 새 외부 에셋226개 약3.31GiB를 포함한 전체 사용 목록은3,953개 약3.45GiB였으며, 그중1MiB 이상137개에 LFS를 사용했다. Git 인덱스에서만 복원한 별도 프로젝트에서 미사용 로컬 팩 없이 사용 모션32개·플레이어 Blueprint/메시·기본 맵5개의 로딩과 프로젝트 참조 누락 없음, LFS SHA-256 무결성을 확인했다. 같은 환경의 PIE에서 걷기·달리기·운반·점프·앉기·스마트폰 표시를 통과했고 애니메이션 누락 ensure는0건이었다. 검사는 당시 빌드한 DLL과 설치된 UE 플러그인을 재사용했으며 다른 PC의 전체 C++ 빌드나 패키지 실행 검사는 아니다. 기록은 `Saved/Testing/RuntimeAssets/GitIndexReport.json`·`GitRuntimeVerificationSummary.json`에 있다. 현재 파일 수·용량은 갱신된 [사용 에셋 목록](RUNTIME_ASSETS.md)을 확인한다. 처음 여는 PC에서는 셰이더·텍스처 준비 시간이 추가로 필요할 수 있다.

### 설치 완료 확인

- [ ] 최신 Git 변경과 해당 LFS 파일을 받았다.
- [ ] `BS_WarehouseStanding`, `BS_WarehouseCrouching`과 사용 모션30개를 Git/LFS로 받았다.
- [ ] `SM_Carton_Intact_1/2`·`SM_Pallet_110`·열린 트럭·중량 상자 색·관찰용 페이드 등 맵 참조 파생 파일이 존재한다. 전체 경로와 출처는 사용 에셋 목록으로 확인한다.
- [ ] C++ 빌드를 완료하고 UE 5.8.2로 메인 메뉴와 관리자 창고를 열 수 있다.
- [ ] 관리자 창고에서 랙·팔레트·상자·트럭·중량 시험 상자와 외부 잔디·도로·울타리·도크가 표시된다.

아래 절차는 **원본 팩 변경·재생성 또는 사용 에셋 추가를 맡은 팀원**을 위한 것입니다. 일반 설치에서는 실행하지 않습니다. 맵을 저장하는 스크립트와 에셋만 만드는 스크립트를 아래 표에서 구분합니다.

## 로컬 용량 정리

`Scripts/optimize_project_storage.py`는 최신 사용 목록을 기준으로 내려받은 팩의 미사용 파일과 재생성 가능한 프로젝트 캐시를 정리합니다. 에디터·컴파일러를 닫고 Unreal Python에서 `Scripts/audit_runtime_assets.py`를 먼저 실행합니다. 소스·설정·필수 에셋이 검사 후 변경됐거나 목록이 24시간보다 오래되면 정리를 거부합니다.

```powershell
python Scripts/optimize_project_storage.py --caches
python Scripts/optimize_project_storage.py --archive-only --caches
```

첫 명령은 미리보기이고 두 번째는 삭제 없이 미사용 패키지를 `Saved/StorageArchives/`의 ZIP으로 보관합니다. 보관본과 원본을 SHA-256으로 비교한 뒤 보고서의 `verified_archive.path`를 다음 명령에 전달합니다.

```powershell
python Scripts/optimize_project_storage.py --apply --caches --archive "Saved/StorageArchives/UnusedPackages_검사에서생성된이름.zip"
```

적용 직전에도 보관본을 다시 검증합니다. 정리 대상은 지정된 외부 팩의 **미사용·Git 미추적·무시 파일**과 `Intermediate`, `.vs`, 프로젝트 `DerivedDataCache`, `Saved/Cooked`, `Saved/Shaders`, `Saved/ShaderDebugInfo`입니다. 필수 에셋, Git 추적 파일, 자체 제작 원본, 사용자 배치·성과 기록, 검사 결과·GIF·캡처는 보존합니다. 충돌용·동적 물리용 메시가 모두 사용 중이면 양쪽을 유지합니다.

보관본도 로컬 용량에 포함되며 Git에는 올리지 않습니다. 원본 팩 재생성이 필요하면 프로젝트 루트에서 `python -m zipfile -e "보관본의 실제 경로.zip" .`으로 원래 `Content/` 경로에 복원하거나 위 원본 팩을 다시 받습니다. 캐시는 다음 빌드·실행에서 다시 생성되므로 첫 준비 시간이 늘 수 있습니다. 공용 Unreal/Zen 캐시와 Git 이력은 이 도구가 삭제하지 않습니다. 과거 LFS 캐시는 원격 보관 확인 후 `git lfs prune --verify-remote`로 별도 정리합니다.

검사·정리의 실제 파일 목록과 삭제한 바이트 수는 `Saved/Testing/StorageOptimization_20261029/` 보고서에 남깁니다. 게임 사용 목록과 Git 선별 기준은 [RUNTIME_ASSETS.md](RUNTIME_ASSETS.md)를 따릅니다.

**2026-10-29 적용·검증:** 지정된 미사용 패키지1,202개16.13GiB를 원본과 SHA-256이 일치하는15.75GiB ZIP으로 보관하고 생성 캐시를 포함해2,137개22.56GiB를 정리했습니다. 원격 보관을 확인한 과거 LFS 캐시23개1.13GiB도 제거했습니다. 로컬 파일 길이 합계는34.96→27.06GiB, `Content`는18.78→2.58GiB입니다. 전체 합계에는 복구 ZIP·저장 기록·캡처를 포함하며 마지막 측정은 운반 검사 중 수행한 근삿값입니다. Git 이력을 다시 쓰지 않았으며 현재 사용 목록은 구형 팔레트73.94MiB를 제외한3,853개2.24GiB, LFS113개입니다.

Editor 빌드 성공, 정리 보호 조건7개, Git 인덱스의 모든 필수 에셋과 LFS SHA-256, 새 프로세스의 맵5개·사용 모션32개·공통 팔레트 로딩 및 패키지 비변경, 실제 지게차 포크 삽입·승강·하역과 양방향 트럭 인계17개 검사를 통과했습니다. 결과는 같은 폴더의 `CleanupGuards.json`, `GitIndexAssets.json`, `AssetLoadReport.json`, `ShippingReport.json`에 있습니다. 저장 맵을 수정하지 않았고, OS 키 입력·다른 PC의 신규 빌드·패키징 실행 검사는 아닙니다. 지게차 주행·센서·충돌 계수와 사용 중인 동적/고정 충돌 형상은 변경하지 않았습니다.

## 1. 현재 사용 에셋의 원본 팩 4개 — 재생성 시 확보

현재 게임에서 사용하는 창고·트럭·상자·애니메이션의 출처입니다. 결과 파일은 Git/LFS로 받고, 원본이 필요한 재생성 작업을 맡았을 때 본인 계정으로 해당 팩을 확보합니다. 에셋 이름을 누르면 Fab 페이지로 이동합니다. 창고·트럭·상자 팩은2026-10-06, Game Animation Sample은2026-10-16 무료 표시를 확인했습니다. 원본을 새로 받을 때 선택한 형식과 표시 가격을 확인합니다.

| 완료 | 에셋 / 다운로드 링크 | 게임에서 사용하는 곳 | 원본 재생성 시 확보 방식 | 프로젝트 안의 위치 |
| --- | --- | --- | --- | --- |
| □ | [Warehouse — Quixel Megascans](https://www.fab.com/listings/a3149fab-3906-4043-b6ee-3937b752a06c) | 창고 랙·공통 팔레트·설비·바닥 재질 | **Unreal Engine 형식**을 프로젝트에 추가 | `Content/Scene_Warehouse/` |
| □ | [Vehicle Variety Pack — Switchboard Studios](https://www.fab.com/listings/dc1ada50-2523-44b1-b0e2-a72d14076fb4) | 외부 박스 트럭과 열린 적재함의 원본 | **프로젝트에 추가** | `Content/VehicleVarietyPack/` |
| □ | [Boxes & Pallets Pack — IINickE](https://www.fab.com/listings/a7c9776e-0a8e-424b-8f7c-f4d746097f0f) | 창고·트럭 안의 온전한 택배 상자 2종 | **FBX ZIP 다운로드 → 아래 3절의 임포트** | 원본 `SourceAssets/Cargo/BoxesPalletsPack/`, 결과 `Content/Warehouse/Cargo/BoxesPalletsPack/` |
| □ | [Game Animation Sample — Epic Games](https://www.fab.com/listings/880e319a-a59e-4ed2-b268-b32dac7fa016) | 플레이어의 기본 이동·앉기·점프·착지 모션 | **재생성 때만 UE5.8 별도 프로젝트 생성 → 전용 절차 실행**. 게임 사용 클립30개·블렌드2개는 Git/LFS로 수신 | 결과 `Content/Warehouse/AnimationSample/` |

상자 팩을 재생성할 때는 FBX ZIP 수신 뒤 아래 임포트 절차가 필요합니다. 상자 팩의 팔레트는 사용하지 않으며 공통 팔레트는 Warehouse 팩의 모델입니다. 추가 운반 모션 후보인 Mixamo는 아직 사용하지 않으므로 사용 원본4개에 포함하지 않습니다.

### 과거 식생 출처 — 현재 미사용

2026-10-28 외부 환경 단순화로 장식 나무5그루와 관목15개를 제거하고 잔디·도로·울타리·도크는 유지합니다. 아래 두 팩과 식생 전용 의존 파일은 현재 실행에 필요하지 않습니다. 원본은 로컬에 보존하며 현재 사용 목록과 Git 추적 대상에서 제외합니다.

| 과거 에셋 / 출처 | 이전 사용 위치 | 보존되는 로컬 원본 |
| --- | --- | --- |
| [European Beech — Quixel Megascans](https://www.fab.com/listings/d11cc01d-9422-41b7-950f-416c9ce79caf) | 장식 나무5그루 | `Content/EuropeanBeech/`, 식생 전용 `Content/MSPresets/` |
| [Free Shrubs Pack — Greenleaf Vision](https://www.fab.com/listings/7ca465ab-fb9c-4d6b-bddb-82c20f604657) | 장식 관목15개 | `Content/GV_FreeShrubsPack/` |

기존 설치에서는 European Beech의5.6 패키지를 UE5.8.2에서 사용했습니다. 이는 과거 환경 재현용 기록이며 현재 설치 단계가 아닙니다. 창고 팩 내부의 `Content/Scene_Warehouse/Materials/MSPresets/`는 별도의 사용 재질이므로 식생 전용 폴더와 혼동하지 않습니다.

## 2. 창고·트럭 원본 팩을 프로젝트에 추가하기 — 선택 작업

1. Git에서 최신 프로젝트를 받고, 폴더 안에 `msc_vr.uproject`가 있는지 확인합니다.
2. 본인 Epic 계정으로 Fab에 로그인하고 필요한 창고·트럭 팩을 **내 라이브러리에 추가**합니다.
3. Epic Games Launcher의 Fab 라이브러리에서 **프로젝트에 추가 → msc_vr**를 선택합니다. 프로젝트를 에디터로 열지 않고도 팩 파일을 추가할 수 있습니다.
4. 설치가 끝나면 위 표의 `Content` 폴더가 생성됐는지 확인합니다. 폴더 이름은 바꾸지 않습니다.

버전 선택에서 5.8 프로젝트가 보이지 않으면 **모든 프로젝트 표시**를 확인합니다. Warehouse의 기존 Fab 설명은 씬 호환 버전을5.4–5.6으로 표기했습니다. 선택 가능한5.6 자료를 사용하되 프로젝트 자체의 엔진 연결은5.8로 유지합니다. 설치 화면이 **프로젝트 생성**만 제공하면 별도 임시 프로젝트에 받은 뒤 그 패키지의 Content를 기존 프로젝트에 옮겨 위 폴더 구조를 맞춥니다.

### msc_vr가 프로젝트 목록에 없을 때

Windows 기준으로 아래 순서로 런처의 검색 경로를 등록합니다. 이 방법은 [Epic 개발자 커뮤니티의 프로젝트 검색 경로 안내](https://forums.unrealengine.com/t/how-do-i-get-a-project-to-show-up-in-the-project-browser/515095)를 참고했습니다.

1. Epic Games Launcher를 트레이 아이콘에서도 **완전히 종료**합니다.
2. `Win + R` → 아래 경로를 입력합니다.

   ```text
   %LOCALAPPDATA%\EpicGamesLauncher\Saved\Config\Windows
   ```

3. `GameUserSettings.ini`를 메모장으로 열고 기존 `[Launcher]` 항목 아래에 프로젝트의 **부모 폴더**를 추가합니다. 기존 경로는 유지합니다.

   프로젝트 위치가 `C:\msc_UnrealProject\msc_vr\msc_vr.uproject`인 경우:

   ```ini
   [Launcher]
   CreatedProjectPaths=C:/msc_UnrealProject
   ```

   팀원의 프로젝트 위치가 `D:\Git\msc_vr\msc_vr.uproject`라면 `CreatedProjectPaths=D:/Git`로 적습니다. `.uproject` 파일 경로나 `msc_vr` 폴더 자체를 적는 것이 아닙니다.

4. 저장하고 런처를 다시 실행해 프로젝트 목록을 확인합니다.

## 3. 상자 팩 FBX 준비 — 선택적 재생성

1. [Boxes & Pallets Pack](https://www.fab.com/listings/a7c9776e-0a8e-424b-8f7c-f4d746097f0f)에서 **FBX 형식**을 다운로드합니다. 현재 사용한 ZIP 이름은 `box_cargo_collection_fbx.zip`입니다.
2. 프로젝트에 `SourceAssets/Cargo/BoxesPalletsPack/` 폴더를 만들고 ZIP을 압축 해제합니다.
3. 아래 두 파일이 정확한 위치에 있어야 합니다. 함께 제공된 텍스처도 원래 하위 폴더 구조 그대로 둡니다.

   ```text
   msc_vr/
   └─ SourceAssets/
      └─ Cargo/
         └─ BoxesPalletsPack/
            └─ Box Cargo Collection FBX/
               ├─ Cargo.fbx
               ├─ Cargo.mtl
               └─ (함께 제공된 텍스처 파일·폴더)
   ```

4. 다음 절의 **상자 임포트 명령**을 실행합니다. 임포트 후 `Content/Warehouse/Cargo/BoxesPalletsPack/Meshes/`에 `Cargo_Box_V1_001`~`Cargo_Box_V4_001`이 있어야 합니다.

## 4. 파생 에셋 재생성과 선택적 맵 재구성

원본 팩을 변경하거나 새 사용 결과를 만들 때 아래 명령으로 **충돌 복사본·온전한 상자·공통 팔레트·열린 트럭 메시·중량 시험 상자 색 재질**을 재생성합니다. 사용 중인 결과는 Git/LFS에 포함하므로 일반 설치에서는 실행하지 않습니다. C++ 빌드를 마치고 현재 변경을 저장해 복구 지점을 마련한 뒤 필요한 단계만 선택합니다. **③·④·⑤는 에셋뿐 아니라 기존 맵을 저장하거나 초기 배치로 갱신하므로 개인 편집 맵에서 일괄 실행하지 않습니다.**

| 순서 | 작업 | 저장하는 대상 |
| --- | --- | --- |
| ① | 상자 FBX 임포트 | 에셋만 생성 |
| ② | 충돌·실측 상자·공통 팔레트 준비 | 에셋만 생성 |
| ③ | 손상 프로필과 파손용 메시 준비 | 에셋 생성 + 맵 손상 프로필 저장 |
| ④ | 열린 트럭 적재함 준비 | 에셋 생성 + `TRK_` 내부와 상자 48개 재배치·저장 |
| ⑤ | 중량 시험 상자 색 재질 복원 | 로컬 재질 생성 + 대상 상자4개·표지·하중 프로필을 기본 배치로 갱신·저장 |
| ⑥ | 관찰 화면의 벽 페이드 복원 | 파생 재질 + 현재 창고의 벽 태그 저장 |
| ⑦ | 원격 조작 캐릭터 페이드 복원 | 파생 재질만 생성 |

**선택한①~④ 명령은 PowerShell에서 `msc_vr.uproject`가 있는 프로젝트 폴더로 이동한 뒤, Unreal Editor를 종료한 상태에서 실행합니다.** 의존 순서는①→②→③이며,④는 트럭 팩과 기존 프로젝트 재질이 필요합니다. 엔진 설치 위치가 다르면 실행 파일 경로를 수정합니다. 정상 완료 문구를 확인한 뒤 필요한 다음 단계로 넘어갑니다. ⑤~⑦은 아래 안내대로 일반 Unreal Editor에서 실행합니다.

개인 맵을 보존하면서③·④·⑤의 **에셋만 복구**하려면 현재 프로젝트의 작업용 사본에서 준비한 뒤, 생성된 에셋만 같은 `Content` 경로로 가져옵니다. 이때 사본의 `.umap`·`__ExternalActors__`·`__ExternalObjects__`를 개인 맵 위에 덮어쓰지 않습니다. 이 문서의 기존 스크립트는 최신 맵을 무변경 복원하는 일괄 설치 도구가 아닙니다.

### ① 상자 FBX 임포트 — 에셋만 생성

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\msc_vr.uproject" "-ExecutePythonScript=$PWD\Scripts\import_boxes_pallets_pack.py" -unattended -nop4 -nosplash -nullrhi
```

완료 문구: `FAB_BOXES_IMPORTED`

### ② 창고·팔레트·트럭의 충돌 에셋 재생성 — 에셋만 생성

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\msc_vr.uproject" "-ExecutePythonScript=$PWD\Scripts\prepare_warehouse_assets.py" -unattended -nop4 -nosplash -nullrhi
```

완료 문구: `WAREHOUSE_COLLISION_ASSETS_READY`. 이 단계에서 `SM_Carton_Intact_1/2`와 110×110×15cm 공통 물리 팔레트 `Content/Warehouse/Physics/SM_Pallet_110.uasset`도 생성합니다. 원본 V3·V4는 임포트되지만 게임에 배치하지 않습니다. 재생성 후 사용 목록을 갱신하고 실제 사용 결과·의존 에셋만 Git에 추가합니다.

### ③ 하중·파손용 물리 복사본 준비 — 맵의 손상 프로필도 저장

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\msc_vr.uproject" "-ExecutePythonScript=$PWD\Scripts\configure_warehouse_strength.py" -unattended -nop4 -nosplash -nullrhi
```

완료 문구: `WAREHOUSE_STRENGTH_CONFIGURED`. 같은 이름의 기존 하중 설정을 보존하면서 필요한 물리 메시를 생성합니다.

### ④ 열린 트럭 적재함 준비 — TRK_ 내부 구성과 상자 48개를 재배치·저장

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\msc_vr.uproject" "-ExecutePythonScript=$PWD\Scripts\configure_truck_interior.py" -unattended -nop4 -nosplash -nullrhi
```

완료 문구: `TRUCK_INTERIOR_APPLIED`. 원본 트럭 팩은 그대로 두고 로컬 파생 메시를 만듭니다. 이 단계는 단순 다운로드가 아니라 **맵의 TRK_ 배치를 초기 구성으로 다시 만드는 작업**입니다. 개인적으로 트럭 내부를 편집한 뒤에는 자동으로 반복 실행하지 않습니다.

### ⑤ 중량 시험 상자 색 재질 복원 — 맵 재배치 포함

저장된 맵의 중량 상자는 `Content/Warehouse/Cargo/BoxesPalletsPack/HeavyTest/Materials/`의 로컬 파생 재질을 참조합니다. 재질이 없으면 Fab 상자 팩과 ②의 `SM_Carton_Intact_2` 준비를 먼저 끝냅니다. 상자 사양·배치·검증 범위는 [WAREHOUSE.md의 중량 예외 시험 상자](WAREHOUSE.md#중량-예외-시험-상자)를 확인합니다.

1. 빌드를 마친 `msc_vr.uproject`를 일반 **Unreal Editor**로 열고 필요한 현재 작업을 저장합니다.
2. **Tools → Execute Python Script...**에서 프로젝트의 `Scripts/configure_heavy_test_cargo.py`를 선택합니다. 이 단계는 실제 PIE 검사를 사용하므로 앞 단계의 `UnrealEditor-Cmd.exe` 방식으로 실행하지 않습니다.
3. 스크립트가 지정 구석의 실제 충돌·바닥을 잠시 플레이로 확인한 뒤 재질을 생성하고 맵을 저장합니다. 출력 로그에서 `HEAVY_NATIVE_CORNER_PREFLIGHT_PASSED`와 마지막 `HEAVY_TEST_CARGO_SAVED`를 확인합니다. 정상 완료 후 에디터는 자동 종료됩니다. Python 오류가 있으면 완료로 처리하지 않습니다.

이 작업은 **대상 상자4개의 위치를 원래 두 열·두 단 배치로 되돌리고 관련 표지·하중 프로필도 갱신해 맵에 저장**합니다. 나머지 창고 물류를 재생성하지 않습니다. 이미 이 상자를 수동 배치한 팀원은 일상 작업마다 반복 실행하지 않습니다.

일반 설치에서는 맵을 다시 만드는 `build_first_person_warehouse.py`, `configure_mezzanine.py`, `apply_real_world_scale.py`, `configure_cargo_variety.py`, `configure_dual_forklifts.py`를 실행하지 않습니다. 최신 Git 맵에는 복층과 1층 지게차 2대의 배치가 이미 들어 있습니다. ③·④·⑤도 개인적으로 해당 영역을 편집한 뒤 반복 실행할 준비 작업은 아닙니다.

### ⑥ 관찰 카메라의 벽 페이드 재질 준비

C++ 빌드 후 일반 Unreal Editor의 **Tools → Execute Python Script...**에서 `Scripts/prepare_observer_wall_fade.py`를 실행합니다. 기존 벽 재질을 복제해 색·질감·노멀을 보존한 페이드 재질과 `DA_ObserverWallPalette`를 생성하고, 현재 창고의 벽에 관찰용 태그만 저장합니다. 위치·충돌과 원본 Fab 재질은 변경하지 않습니다. 완료 문구는 `OBSERVER_WALL_FADE_PREPARED`입니다.

`Content/Warehouse/Materials/ObserverWalls/`의 사용 중인 파생 재질도 Git/LFS로 받습니다. 새 벽 재질에 대응하거나 결과를 재생성해야 할 때 위 스크립트를 실행합니다. 관찰 화면에서의 동작과 검증 범위는 [WAREHOUSE.md](WAREHOUSE.md#설정과-전지적-관찰)를 따릅니다.

### ⑦ 원격 조작 캐릭터 페이드 재질 준비

현재 기본 캐릭터·프로젝트 재질이 있는 상태에서 일반 Unreal Editor의 **Tools → Execute Python Script...**로 `Scripts/prepare_remote_character_fade.py`를 실행합니다. 원본을 유지하면서 `Content/Warehouse/Materials/RemoteCharacter/`에 원격 조작용 파생 재질을 만들며 맵을 재배치하지 않습니다. 완료 문구는 `REMOTE_CHARACTER_FADE_PREPARED`입니다. 사용 중인 결과는 Git/LFS로 받으므로 일반 설치에서 다시 만들지 않습니다.

## 완성 에셋과 제작 자료의 기준

게임 실행에 필요한 에셋과 Git 선별 기준은 [게임 사용 에셋 목록](RUNTIME_ASSETS.md)에서 관리합니다. 이 파일에서는 수신·재생성 방법만 설명하며 전체 파일 목록은 중복 관리하지 않습니다. 자체 제작 설비와 사용 중인 외부·파생 에셋, Game Animation Sample 모션은 Git/LFS로 받습니다. 원본 팩 전체·FBX ZIP·샘플 프로젝트는 필요한 재생성 담당자만 별도로 준비합니다.

상자 품목·질량 규칙은 [CARGO_RULES.md](CARGO_RULES.md), 지게차 모델·바퀴 충돌을 다시 만드는 순서는 [REFINED_AGV.md](REFINED_AGV.md), 물리 준비·검사는 [PHYSICS.md](PHYSICS.md)를 확인합니다.

## 자체 제작 화물용 승강기 재생성

일반 설치는 Git의 완성 에셋을 사용한다. 모델 수정 시 Blender5.2의 전용 백그라운드 프로세스에서 `Scripts/build_pallet_lift.py`를 실행하면 `Art/Equipment/PalletLift/`에 원본·FBX·재질 명세·미리보기가 생성된다. 이 스크립트는 해당 프로세스의 장면을 초기화하므로 사용자가 편집 중인 Blender 장면에서 실행하지 않는다.

그다음 C++ 빌드를 마친 프로젝트의 Unreal Python에서 `Scripts/apply_pallet_lift.py`를 실행한다. 자체 PBR 재질과 세 부품을 임포트하고 기존 범용 승강기의 자세·부품을 비교해 보존하면서 별도 화물용을 배치한다. 준비된 메시를 재사용하므로 FBX 자체를 수정했으면 에디터 실행 인수 `-PalletLiftReimport`를 지정한다. 새 샤프트에 걸치는 바닥만 실제로 분할 저장하며 재실행은 설치된 위치를 유지한다. 새 위치에서의 실험은 게임의 배치 모드를 사용한다. 동적 적재대·문은 FBX에 포함된 개별 볼록 충돌체, 정적 프레임은 삼각형 충돌을 사용한다. 장비 사용·규격·검증 범위는 [MEZZANINE.md](MEZZANINE.md)가 담당한다.

## 컨베이어·4층 화물 승강기 확장 재생성

일반 설치는 Git의 완성 메시를 사용한다. 원본을 수정할 때는 전용 백그라운드 Blender5.2에서 `Scripts/build_conveyor_model.py`를 실행한다. `Scripts/build_pallet_lift.py`의 `--transport-extensions --skip-render` 인수는 기존 3층 모델을 덮어쓰지 않고 별도 `PalletLiftExtensions` 폴더에 4층 프레임과 구동 롤러용 캐리어를 만든다.

컨베이어 제작 스크립트의 `--turntable-only`는 `SM_Conveyor_TurntableBase.fbx`·`SM_Conveyor_TurntableRotor.fbx`, 별도 `conveyor_turntable.blend`와 `Conveyor_Turntable_Preview.png`만 생성하고 기존 직선 모델을 유지한다. `--frame-only --skip-render`는 직선 프레임만 다시 내보내며 롤러 FBX를 유지한다. 두 경로 모두 `manifest.json`의 다른 모델 항목을 보존한다. 일반 직선 원본은 `conveyor.blend`, 미리보기는 `Conveyor_Preview.png`다. 이 스크립트도 사용자가 편집 중인 Blender 장면에서 실행하지 않는다.

컨베이어 포트와 문턱의 실제 간섭을 해소한 전용 샤프트는 `SM_PalletLift_FramePowered3.fbx`·`SM_PalletLift_FramePowered4.fbx`다. `PalletLiftExtensions` 폴더에 별도로 생성하며 기존 프레임·캐리어·문 출력은 유지한다. 프로젝트 루트에서 다음 두 명령을 각각 실행한다. 물리적 간격의 기준은 [PHYSICS.md](PHYSICS.md#자동-컨베이어와-승강기-롤러의-가정)에 둔다.

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --factory-startup --python .\Scripts\build_pallet_lift.py -- --powered-shaft-only --floors=3 --skip-render
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --factory-startup --python .\Scripts\build_pallet_lift.py -- --powered-shaft-only --floors=4 --skip-render
```

프로젝트 C++ 빌드 후 PIE가 꺼진 Unreal Python에서 `Scripts/install_warehouse_transport_lab.py`를 실행하면 확장 메시를 임포트하고 별도 실험 맵만 저장한다. FBX 수정 후 재임포트하려면 에디터 시작 인수 `-TransportModelReimport`를 추가한다. 기본 맵·시작 설정·기존 3층 메시를 저장하거나 재생성하지 않는다. 조작·연결 한계·실험 맵 위치는 [창고 안내](WAREHOUSE.md#자동-컨베이어)를 따른다.

## Game Animation Sample 사용 모션 재생성 — 선택 작업

게임에서 사용하는 클립30개와 블렌드2개는 Git/LFS로 받습니다. 새 동작을 선택하거나 리타게팅 결과를 다시 만들 때만 런처에서 UE5.8용 샘플을 **별도 프로젝트로 생성**합니다. 현재 게임의 C++ 빌드를 먼저 완료하고 두 프로젝트의 에디터를 종료한 뒤, `msc_vr` 폴더에서 아래 순서로 실행합니다. 샘플 위치가 다르면 `$sampleProject`만 바꿉니다. 일반 설치에서는 두 명령을 실행하지 않습니다.

```powershell
$projectRoot=(Get-Location).Path
$sampleProject='C:/msc_UnrealProject/GameAnimationSample/GameAnimationSample.uproject'
$editorCmd='C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
& $editorCmd $sampleProject -run=pythonscript "-script=$projectRoot/Scripts/import_game_animation_sample.py" -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -NullRHI -nop4
& $editorCmd "$projectRoot/msc_vr.uproject" -run=pythonscript "-script=$projectRoot/Scripts/configure_game_animation_sample.py" -unattended -NullRHI -nop4
```

첫 명령은 실제 사용하는30개 클립을 UE5 체형으로 변환하고 샘플 전용 이벤트를 정리해 가져옵니다. 샘플 안에는 파생 결과 `/Game/Warehouse/AnimationSample/Sequences`가 생성됩니다. 두 번째 명령은 현재 게임의 Manny 스켈레톤에 맞추고 방향·속도별 블렌드 스페이스 두 개와 플레이어의 기본 모션 연결을 저장합니다. 원본 샘플 전용 미리보기 참조는 프로젝트 모델로 바꾸고, 변환된 기준 자세 데이터는 유지합니다. 기존 플레이어 Blueprint는 `Saved/Testing/AnimationSampleBackup/`에 최초 한 번 백업하며 창고 맵은 재생성하지 않습니다.

`GAME_ANIMATION_SAMPLE_IMPORT_PASSED`와 `GAME_ANIMATION_SAMPLE_CONFIGURATION_PASSED`를 각각 확인합니다. 결과32개와 의존 에셋은 사용 목록으로 관리합니다. 미사용 자세 전환 클립2개는 만들지 않으며 샘플 전체를 현재 프로젝트에 복사하지 않습니다.

팀원 에디터의 `StandingMotion && CrouchingMotion && JumpStartMotion && FallMotion && LandingMotion` ensure는 필요한 모션 또는 블렌드 에셋을 로드하지 못해 발생합니다. 먼저 최신 에셋 커밋과 `git lfs pull` 수신을 확인하고, `Content/Warehouse/AnimationSample/`의 블렌드2개와 `Sequences/`의 클립30개가 실제 `.uasset`인지 확인합니다. 직접 모션을 재생성한 경우에는 위 **임포트 → 구성 두 단계**의 성공 문구도 확인합니다. 이 문제를 C++ 빌드만 반복하거나 ensure를 지워서 해결하지 않습니다. 적용 범위는 [플레이어 조작](WAREHOUSE.md#플레이어-조작), 실행 검증과 실제 물리는 [PHYSICS.md](PHYSICS.md)를 확인합니다.

## 무료 운반 애니메이션 후보 — 미적용

추가 운반 애니메이션은 **무료 에셋만** 대상으로 합니다. 2026-10-12 [Mixamo 공식 검색](https://www.mixamo.com/#/?query=box&type=Motion%2CMotionPack)에서 `Box Idle`(들고 서기), `Box Turn`(좌·우 회전), `Box Walk Arc`(곡선 보행), `Jogging With Box`(들고 뛰기)를 확인했습니다. [Adobe 공식 안내](https://helpx.adobe.com/creative-cloud/faq/mixamo-faq.html)는 Adobe ID로 무료 이용 및 게임 사용이 가능하다고 명시합니다.

현재 다운로드·리타게팅·게임 적용은 하지 않았으며 사용 에셋 목록에 포함하지 않습니다. 앞서 검토한 Fab `Pick Up & Carry Animation Pack`은 무료를 확인하지 못해 제외합니다. 적용할 때는 현재 캐릭터에 동작을 맞추고 기존 상자 크기별 양손 IK와 연결해야 합니다.
