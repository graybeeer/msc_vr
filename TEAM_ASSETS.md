# 팀원 에셋 설치와 로컬 재생성

대상 프로젝트는 `msc_vr`, 개발 엔진은 **Unreal Engine 5.8.2**입니다. 이 문서는 공개 저장소 수신, 별도로 받아야 하는 외부 팩, 누락된 파생 에셋·모션의 로컬 준비를 담당합니다. 게임 실행과 조작은 [README.md](README.md), 맵과 시뮬레이션 설정은 [WAREHOUSE.md](WAREHOUSE.md)를 확인합니다.

**2026-10-28 공개 저장소 기준:** 코드·자체 제작물·문서를 공유하며 외부 에셋 원본, 이를 복제·가공한 모델/재질, Game Animation Sample에서 가져온 모션은 공개 Git에 포함하지 않습니다. 외부 팩6개는 각 팀원이 본인 계정으로 확보하고 필요한 결과를 로컬에서 만듭니다. Git LFS는 공개 대상으로 남긴 대용량 파일을 받는 수단이며, 제외된 외부 자료를 제공하지 않습니다.

이번 정책은 외부 원본·파생 파일을 새로 업로드하지 않는 기준입니다. 기존 커밋의 Unreal 기본 템플릿·시험 에셋과 기록은 유지하며, 이를 정리하려고 과거 Git 기록을 다시 쓰지는 않습니다. 추가·변경된 외부 템플릿 원본 역시 이번 업로드 대상에서 제외합니다.

[게임 사용 에셋 목록](RUNTIME_ASSETS.md)과 `Config/RuntimeAssets.json`은 **실제로 필요한 파일과 출처를 찾는 목록**입니다. 목록에 있다는 이유만으로 공개 Git에 포함된 파일은 아닙니다. 파생 재질·충돌 메시도 원본 출처를 따라 분류하며 정확한 공개 포함/제외 판정은 해당 목록을 확인합니다.

## 일반 설치와 실행

1. 에디터를 닫고 공개 저장소의 최신 변경을 받습니다. Git LFS를 사용하는 파일이 있으면 프로젝트 폴더에서 `git lfs install --local`, `git lfs pull`로 수신합니다.
2. 아래 **외부 에셋6개** 표를 확인합니다. 현재 전체 게임을 플레이하거나 PIE로 검사하는 팀원은6개 모두 필요합니다. 소스 검토·C++ 빌드만 맡는 팀원은 다운로드를 미룰 수 있지만, 에셋 없는 게임 실행까지 검증된 것은 아닙니다.
3. Unreal Engine 5.8.2와 Visual Studio의 게임 C++ 개발 도구로 `msc_vrEditor / Development Editor / Win64`를 빌드합니다.
4. 앞의4개 팩을 프로젝트에 추가하고 상자 FBX를 준비한 뒤, 아래4절에서 **누락된 결과에 해당하는 준비 작업만** 실행합니다. Game Animation Sample은 뒤의 전용 절차로 사용 모션을 가져옵니다. 이미 올바른 로컬 결과가 있으면 매 업데이트마다 재생성하지 않습니다.
5. 설치 완료 항목을 확인한 후 `msc_vr.uproject`를 열고 메인 메뉴에서 모드를 선택합니다. 조작은 [창고 안내](WAREHOUSE.md)를 따릅니다.

공개 대상 `.uasset`가 작은 LFS 텍스트 포인터로 남았다면 `git lfs pull`을 확인합니다. **외부 에셋이나 사용 모션 자체가 없을 때는 LFS 재시도로 해결되지 않으며 아래 설치·재생성이 필요합니다.** 에셋 폴더를 임의로 옮기면 맵·재질 참조가 끊기므로 원래 경로를 유지합니다.

### 설치 완료 확인

- [ ] 최신 공개 Git 변경과 해당 LFS 파일을 받았고, 외부 팩6개를 별도로 준비했다.
- [ ] `BS_WarehouseStanding`, `BS_WarehouseCrouching`과 사용 모션30개를 로컬에 생성했다.
- [ ] `SM_Carton_Intact_1/2`·`SM_Pallet_110`·열린 트럭·중량 상자 색·관찰용 페이드 등 맵 참조 파생 파일이 존재한다. 전체 경로와 출처는 사용 에셋 목록으로 확인한다.
- [ ] C++ 빌드를 완료하고 UE 5.8.2로 메인 메뉴와 관리자 창고를 열 수 있다.
- [ ] 관리자 창고에서 랙·팔레트·상자·트럭·나무·관목과 중량 시험 상자의 재질이 표시된다.

다음 절차는 처음 환경을 준비하거나 외부 에셋의 누락·변경이 있을 때 사용합니다. **파일이 이미 있으면 일상 업데이트마다 재생성할 필요가 없습니다.** 맵을 저장하는 스크립트와 에셋만 만드는 스크립트를 아래 표에서 구분합니다.

## 1. 전체 게임 실행에 필요한 외부 에셋 6개

에셋 이름을 누르면 Fab 페이지로 이동합니다. 기존5개는2026-10-06, Game Animation Sample은2026-10-16 무료 표시를 확인했습니다. 설치할 때 선택한 형식과 무료 표시를 확인합니다.

| 완료 | 에셋 / 다운로드 링크 | 게임에서 사용하는 곳 | 설치 방식 | 프로젝트 안의 위치 |
| --- | --- | --- | --- | --- |
| □ | [Warehouse — Quixel Megascans](https://www.fab.com/listings/a3149fab-3906-4043-b6ee-3937b752a06c) | 창고 랙·공통 팔레트·설비·바닥 재질 | **Unreal Engine 형식**을 프로젝트에 추가 | `Content/Scene_Warehouse/` |
| □ | [Vehicle Variety Pack — Switchboard Studios](https://www.fab.com/listings/dc1ada50-2523-44b1-b0e2-a72d14076fb4) | 외부 박스 트럭과 열린 적재함의 원본 | **프로젝트에 추가** | `Content/VehicleVarietyPack/` |
| □ | [European Beech — Quixel Megascans](https://www.fab.com/listings/d11cc01d-9422-41b7-950f-416c9ce79caf) | 창고 주변 나무 | **프로젝트에 추가** | `Content/EuropeanBeech/` + 함께 설치되는 `Content/MSPresets/` |
| □ | [Free Shrubs Pack (Ultra Realistic Wind) — Greenleaf Vision](https://www.fab.com/listings/7ca465ab-fb9c-4d6b-bddb-82c20f604657) | 울타리 주변 관목 | **프로젝트에 추가** | `Content/GV_FreeShrubsPack/` |
| □ | [Boxes & Pallets Pack — IINickE](https://www.fab.com/listings/a7c9776e-0a8e-424b-8f7c-f4d746097f0f) | 창고·트럭 안의 온전한 택배 상자 2종 | **FBX ZIP 다운로드 → 아래 3절의 임포트** | 원본 `SourceAssets/Cargo/BoxesPalletsPack/`, 결과 `Content/Warehouse/Cargo/BoxesPalletsPack/` |
| □ | [Game Animation Sample — Epic Games](https://www.fab.com/listings/880e319a-a59e-4ed2-b268-b32dac7fa016) | 플레이어의 기본 이동·앉기·점프·착지 모션 | **UE5.8 별도 프로젝트 생성 → 뒤의 전용 절차로 사용30개 클립과 블렌드2개 준비**. 원본/결과 모두 공개 Git 제외 | 결과 `Content/Warehouse/AnimationSample/` |

상자 팩은 FBX 자료이므로 ZIP 수신 뒤 아래 임포트 절차가 필요합니다. 상자 팩의 팔레트는 사용하지 않으며 공통 팔레트는 Warehouse 팩의 모델입니다. 추가 운반 모션 후보인 Mixamo는 아직 사용하지 않으므로 필수6개에 포함하지 않습니다.

## 2. 앞의 4개 팩을 프로젝트에 추가하기

1. Git에서 최신 프로젝트를 받고, 폴더 안에 `msc_vr.uproject`가 있는지 확인합니다.
2. 본인 Epic 계정으로 Fab에 로그인하고 위 팩들을 **내 라이브러리에 추가**합니다.
3. Epic Games Launcher의 Fab 라이브러리에서 **프로젝트에 추가 → msc_vr**를 선택합니다. 프로젝트를 에디터로 열지 않고도 팩 파일을 추가할 수 있습니다.
4. 설치가 끝나면 위 표의 `Content` 폴더가 생성됐는지 확인합니다. 폴더 이름은 바꾸지 않습니다.

버전 선택에서 5.8 프로젝트가 보이지 않으면 **모든 프로젝트 표시**를 확인합니다. 이 프로젝트에서는 European Beech를 **5.6 패키지**로 설치하여 5.8.2에서 사용했습니다. Warehouse의 Fab 설명도 씬 호환 버전을 5.4–5.6으로 표기합니다. 선택 가능한 5.6 자료를 사용하되 프로젝트 자체의 엔진 연결은 5.8로 유지합니다. 설치 화면이 **프로젝트 생성**만 제공하면 별도 임시 프로젝트에 받은 뒤 그 패키지의 Content를 기존 프로젝트에 옮겨 위 폴더 구조를 맞춥니다.

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

## 3. 상자 팩 FBX 준비

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

## 4. 누락된 파생 에셋 준비와 선택적 맵 재구성

외부 팩을 받은 뒤 아래 명령으로 누락된 **충돌 복사본·온전한 상자·공통 팔레트·열린 트럭 메시·중량 시험 상자 색 재질**을 로컬에서 만듭니다. 외부 원본을 사용한 파생 결과도 공개 Git에는 포함하지 않습니다. C++ 빌드를 마치고 현재 변경을 저장해 복구 지점을 마련한 뒤 필요한 단계만 선택합니다. **③·④·⑤는 에셋뿐 아니라 기존 맵을 저장하거나 초기 배치로 갱신하므로 개인 편집 맵에서 일괄 실행하지 않습니다.**

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

### ② 창고·팔레트·트럭·식물의 충돌 에셋 재생성 — 에셋만 생성

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\msc_vr.uproject" "-ExecutePythonScript=$PWD\Scripts\prepare_warehouse_assets.py" -unattended -nop4 -nosplash -nullrhi
```

완료 문구: `WAREHOUSE_COLLISION_ASSETS_READY`. 이 단계에서 `SM_Carton_Intact_1/2`와 110×110×15cm 공통 물리 팔레트 `Content/Warehouse/Physics/SM_Pallet_110.uasset`도 생성합니다. 원본 V3·V4는 임포트되지만 게임에 배치하지 않습니다. 이 결과는 로컬 설치 대상이며 공개 Git에 추가하지 않습니다.

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

`Content/Warehouse/Materials/ObserverWalls/`에는 외부 벽 재질에서 파생한 파일도 있으므로 공개 Git만으로 모두 준비되지 않습니다. 누락되거나 새 벽 재질에 대응해야 할 때 위 스크립트를 실행합니다. 관찰 화면에서의 동작과 검증 범위는 [WAREHOUSE.md](WAREHOUSE.md#설정과-전지적-관찰)를 따릅니다.

### ⑦ 원격 조작 캐릭터 페이드 재질 준비

현재 기본 캐릭터·프로젝트 재질이 있는 상태에서 일반 Unreal Editor의 **Tools → Execute Python Script...**로 `Scripts/prepare_remote_character_fade.py`를 실행합니다. 원본을 유지하면서 `Content/Warehouse/Materials/RemoteCharacter/`에 원격 조작용 파생 재질을 만들며 맵을 재배치하지 않습니다. 완료 문구는 `REMOTE_CHARACTER_FADE_PREPARED`입니다. 캐릭터 원본에서 파생한 파일은 공개 Git 제외 대상입니다.

## 완성 에셋과 제작 자료의 기준

게임 실행에 필요한 에셋과 공개 포함/제외 기준은 [게임 사용 에셋 목록](RUNTIME_ASSETS.md)에서 관리합니다. 이 파일에서는 설치·재생성 방법만 설명하며 전체 파일 목록은 중복 관리하지 않습니다. 자체 제작 설비의 공개 대상 파일은 Git/LFS로 받고, 외부 원본·Fab 파생 파일·Game Animation Sample 모션은 별도로 로컬에 준비합니다. 기존 맵에 외부 파일의 참조가 남아 있다는 사실은 해당 파일을 공개 배포한다는 뜻이 아닙니다.

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

## Game Animation Sample 사용 모션 준비

전체 게임 실행에 필요한 사용 모션32개는 공개 Git에 포함하지 않습니다. 로컬에 결과가 없다면 런처에서 UE5.8용 샘플을 **별도 프로젝트로 생성**합니다. 현재 게임의 C++ 빌드를 먼저 완료하고 두 프로젝트의 에디터를 종료한 뒤, `msc_vr` 폴더에서 아래 순서로 실행합니다. 샘플 위치가 다르면 `$sampleProject`만 바꿉니다. 이미 올바른 사용 모션이 있으면 반복하지 않습니다.

```powershell
$projectRoot=(Get-Location).Path
$sampleProject='C:/msc_UnrealProject/GameAnimationSample/GameAnimationSample.uproject'
$editorCmd='C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
& $editorCmd $sampleProject -run=pythonscript "-script=$projectRoot/Scripts/import_game_animation_sample.py" -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -NullRHI -nop4
& $editorCmd "$projectRoot/msc_vr.uproject" -run=pythonscript "-script=$projectRoot/Scripts/configure_game_animation_sample.py" -unattended -NullRHI -nop4
```

첫 명령은 실제 사용하는30개 클립을 UE5 체형으로 변환하고 샘플 전용 이벤트를 정리해 가져옵니다. 샘플 안에는 파생 결과 `/Game/Warehouse/AnimationSample/Sequences`가 생성됩니다. 두 번째 명령은 현재 게임의 Manny 스켈레톤에 맞추고 방향·속도별 블렌드 스페이스 두 개와 플레이어의 기본 모션 연결을 저장합니다. 원본 샘플 전용 미리보기 참조는 프로젝트 모델로 바꾸고, 변환된 기준 자세 데이터는 유지합니다. 기존 플레이어 Blueprint는 `Saved/Testing/AnimationSampleBackup/`에 최초 한 번 백업하며 창고 맵은 재생성하지 않습니다.

`GAME_ANIMATION_SAMPLE_IMPORT_PASSED`와 `GAME_ANIMATION_SAMPLE_CONFIGURATION_PASSED`를 각각 확인합니다. 결과32개는 실제 사용 범위이지만 **공개 Git 제외 대상**입니다. 미사용 자세 전환 클립2개는 만들지 않으며 샘플 전체를 현재 프로젝트에 복사하지 않습니다.

팀원 에디터의 `StandingMotion && CrouchingMotion && JumpStartMotion && FallMotion && LandingMotion` ensure는 필요한 모션 또는 블렌드 에셋을 로드하지 못해 발생합니다. 공개 저장소에서 해당 파일이 없는 것은 LFS 수신 실패와 구분해야 합니다. 위의 **임포트 → 구성 두 단계**를 완료한 뒤 `Content/Warehouse/AnimationSample/`의 블렌드2개와 `Sequences/`의 클립30개, 두 성공 문구를 확인하고 에디터를 다시 엽니다. 이 문제를 C++ 빌드만 반복하거나 ensure를 지워서 해결하지 않습니다. 적용 범위는 [플레이어 조작](WAREHOUSE.md#플레이어-조작), 실행 검증과 실제 물리는 [PHYSICS.md](PHYSICS.md)를 확인합니다.

## 무료 운반 애니메이션 후보 — 미적용

추가 운반 애니메이션은 **무료 에셋만** 대상으로 합니다. 2026-10-12 [Mixamo 공식 검색](https://www.mixamo.com/#/?query=box&type=Motion%2CMotionPack)에서 `Box Idle`(들고 서기), `Box Turn`(좌·우 회전), `Box Walk Arc`(곡선 보행), `Jogging With Box`(들고 뛰기)를 확인했습니다. [Adobe 공식 안내](https://helpx.adobe.com/creative-cloud/faq/mixamo-faq.html)는 Adobe ID로 무료 이용 및 게임 사용이 가능하다고 명시합니다.

현재 다운로드·리타게팅·게임 적용은 하지 않았으며 필수 설치 목록에 포함하지 않습니다. 앞서 검토한 Fab `Pick Up & Carry Animation Pack`은 무료를 확인하지 못해 제외합니다. 적용할 때는 현재 캐릭터에 동작을 맞추고 기존 상자 크기별 양손 IK와 연결해야 합니다.
