# 팀원 에셋 설치 안내

**팀원은 이 파일 순서대로 설치하면 됩니다.** 대상 프로젝트는 `msc_vr`, 개발 엔진은 **Unreal Engine 5.8.2**입니다. 이 문서는 다운로드·설치·로컬 에셋 재생성만 다룹니다. 게임 실행과 조작은 [README.md](README.md), 맵과 시뮬레이션 설정은 [WAREHOUSE.md](WAREHOUSE.md)를 확인합니다.

Git으로 프로젝트를 받아도 대용량 외부 에셋은 내려오지 않습니다. 아래 **필수 팩 6개**를 준비해야 창고·트럭·나무·상자와 플레이어 모션이 정상 표시됩니다.

## 1. 다운로드할 에셋 6개

에셋 이름을 누르면 Fab 페이지로 이동합니다. 기존5개는2026-10-06, Game Animation Sample은2026-10-16 무료 표시를 확인했습니다. 설치할 때 선택한 형식과 무료 표시를 확인합니다.

| 완료 | 에셋 / 다운로드 링크 | 게임에서 사용하는 곳 | 설치 방식 | 프로젝트 안의 위치 |
| --- | --- | --- | --- | --- |
| □ | [Warehouse — Quixel Megascans](https://www.fab.com/listings/a3149fab-3906-4043-b6ee-3937b752a06c) | 창고 랙·공통 팔레트·설비·바닥 재질 | **Unreal Engine 형식**을 프로젝트에 추가 | `Content/Scene_Warehouse/` |
| □ | [Vehicle Variety Pack — Switchboard Studios](https://www.fab.com/listings/dc1ada50-2523-44b1-b0e2-a72d14076fb4) | 외부 박스 트럭과 열린 적재함의 원본 | **프로젝트에 추가** | `Content/VehicleVarietyPack/` |
| □ | [European Beech — Quixel Megascans](https://www.fab.com/listings/d11cc01d-9422-41b7-950f-416c9ce79caf) | 창고 주변 나무 | **프로젝트에 추가** | `Content/EuropeanBeech/` + 함께 설치되는 `Content/MSPresets/` |
| □ | [Free Shrubs Pack (Ultra Realistic Wind) — Greenleaf Vision](https://www.fab.com/listings/7ca465ab-fb9c-4d6b-bddb-82c20f604657) | 울타리 주변 관목 | **프로젝트에 추가** | `Content/GV_FreeShrubsPack/` |
| □ | [Boxes & Pallets Pack — IINickE](https://www.fab.com/listings/a7c9776e-0a8e-424b-8f7c-f4d746097f0f) | 창고·트럭 안의 온전한 택배 상자 2종 | **FBX ZIP 다운로드 → 아래 3절의 임포트** | 원본 `SourceAssets/Cargo/BoxesPalletsPack/`, 결과 `Content/Warehouse/Cargo/BoxesPalletsPack/` |
| □ | [Game Animation Sample — Epic Games](https://www.fab.com/listings/880e319a-a59e-4ed2-b268-b32dac7fa016) | 플레이어의 기본 이동·앉기·점프·착지 모션 | **UE5.8 별도 프로젝트 생성 → 아래 모션 설치 절차** | 결과 `Content/Warehouse/AnimationSample/` |

**주의할 구분:** 상자 팩은 FBX 자료이므로 ZIP을 받기만 하면 설치가 끝난 것이 아닙니다. 상자 팩에 들어 있는 팔레트는 현재 사용하지 않으며, 공통 팔레트는 첫 번째 Warehouse 팩의 모델입니다.

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

## 3. 상자 팩은 FBX로 설치하기

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

## 4. C++ 빌드와 로컬 에셋 준비

팩 설치 뒤에는 Git에서 제외한 **충돌 복사본·온전한 상자·공통 팔레트·열린 트럭 메시·중량 시험 상자 색 재질**도 만들어야 합니다. 먼저 프로젝트의 C++를 `msc_vrEditor / Development Editor / Win64`로 빌드합니다. 팀원 PC에는 UE 5.8과 Visual Studio의 게임 C++ 개발 도구가 필요합니다.

| 순서 | 작업 | 저장하는 대상 |
| --- | --- | --- |
| ① | 상자 FBX 임포트 | 에셋만 생성 |
| ② | 충돌·실측 상자·공통 팔레트 준비 | 에셋만 생성 |
| ③ | 손상 프로필과 파손용 메시 준비 | 에셋 생성 + 맵 손상 프로필 저장 |
| ④ | 열린 트럭 적재함 준비 | 에셋 생성 + `TRK_` 내부와 상자 48개 재배치·저장 |
| ⑤ | 중량 시험 상자 색 재질 복원 | 로컬 재질 생성 + 대상 상자4개·표지·하중 프로필을 기본 배치로 갱신·저장 |

**①~④ 명령은 PowerShell에서 `msc_vr.uproject`가 있는 프로젝트 폴더로 이동한 뒤, Unreal Editor를 종료한 상태에서 순서대로 실행합니다.** 엔진 설치 위치가 다르면 실행 파일 경로를 수정합니다. 정상 완료 문구를 확인한 뒤 다음 단계로 넘어갑니다. ⑤는 아래 안내대로 일반 Unreal Editor에서 실행합니다.

### ① 상자 FBX 임포트 — 에셋만 생성

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\msc_vr.uproject" "-ExecutePythonScript=$PWD\Scripts\import_boxes_pallets_pack.py" -unattended -nop4 -nosplash -nullrhi
```

완료 문구: `FAB_BOXES_IMPORTED`

### ② 창고·팔레트·트럭·식물의 충돌 에셋 재생성 — 에셋만 생성

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\msc_vr.uproject" "-ExecutePythonScript=$PWD\Scripts\prepare_warehouse_assets.py" -unattended -nop4 -nosplash -nullrhi
```

완료 문구: `WAREHOUSE_COLLISION_ASSETS_READY`. 이 단계에서 `SM_Carton_Intact_1/2`와 110×110×15cm 공통 물리 팔레트 `Content/Warehouse/Physics/SM_Pallet_110.uasset`도 생성합니다. 원본 V3·V4는 임포트되지만 게임에 배치하지 않습니다. 파생 에셋은 Git 제외이므로 팀원마다 재생성해야 합니다.

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

### ⑤ 중량 시험 상자 색 재질 복원 — 첫 설치·재질 누락 때만

저장된 맵의 중량 상자는 `Content/Warehouse/Cargo/BoxesPalletsPack/HeavyTest/Materials/`의 로컬 파생 재질을 참조합니다. 이 폴더는 Git에 포함되지 않으므로 첫 설치 또는 해당 재질이 누락됐을 때 재생성해야 합니다. Fab 상자 팩과②의 `SM_Carton_Intact_2` 준비를 먼저 끝냅니다. 상자 사양·배치·검증 범위는 [WAREHOUSE.md의 중량 예외 시험 상자](WAREHOUSE.md#중량-예외-시험-상자)를 확인합니다.

1. 빌드를 마친 `msc_vr.uproject`를 일반 **Unreal Editor**로 열고 필요한 현재 작업을 저장합니다.
2. **Tools → Execute Python Script...**에서 프로젝트의 `Scripts/configure_heavy_test_cargo.py`를 선택합니다. 이 단계는 실제 PIE 검사를 사용하므로 앞 단계의 `UnrealEditor-Cmd.exe` 방식으로 실행하지 않습니다.
3. 스크립트가 지정 구석의 실제 충돌·바닥을 잠시 플레이로 확인한 뒤 재질을 생성하고 맵을 저장합니다. 출력 로그에서 `HEAVY_NATIVE_CORNER_PREFLIGHT_PASSED`와 마지막 `HEAVY_TEST_CARGO_SAVED`를 확인합니다. 정상 완료 후 에디터는 자동 종료됩니다. Python 오류가 있으면 완료로 처리하지 않습니다.

이 작업은 **대상 상자4개의 위치를 원래 두 열·두 단 배치로 되돌리고 관련 표지·하중 프로필도 갱신해 맵에 저장**합니다. 나머지 창고 물류를 재생성하지 않습니다. 이미 이 상자를 수동 배치한 팀원은 일상 작업마다 반복 실행하지 않습니다.

일반 설치에서는 맵을 다시 만드는 `build_first_person_warehouse.py`, `configure_mezzanine.py`, `apply_real_world_scale.py`, `configure_cargo_variety.py`, `configure_dual_forklifts.py`를 실행하지 않습니다. 최신 Git 맵에는 복층과 1층 지게차 2대의 배치가 이미 들어 있습니다. ③·④·⑤도 개인적으로 해당 영역을 편집한 뒤 반복 실행할 준비 작업은 아닙니다.

### ⑥ 관찰 카메라의 벽 페이드 재질 준비

C++ 빌드 후 일반 Unreal Editor의 **Tools → Execute Python Script...**에서 `Scripts/prepare_observer_wall_fade.py`를 실행합니다. 기존 벽 재질을 복제해 색·질감·노멀을 보존한 페이드 재질과 `DA_ObserverWallPalette`를 생성하고, 현재 창고의 벽에 관찰용 태그만 저장합니다. 위치·충돌과 원본 Fab 재질은 변경하지 않습니다. 완료 문구는 `OBSERVER_WALL_FADE_PREPARED`입니다.

`Content/Warehouse/Materials/ObserverWalls/`는 원본 팩에서 재생성하는 Git 제외 폴더입니다. 처음 설치하거나 이 폴더가 없을 때 준비하며, 새로운 벽 재질을 추가했을 때도 다시 실행합니다. 관찰 화면에서의 동작과 검증 범위는 [WAREHOUSE.md](WAREHOUSE.md#설정과-전지적-관찰)를 따릅니다.

## 5. 따로 다운로드하지 않아도 되는 것

| 항목 | 이미 받는 방법 |
| --- | --- |
| 지게차 원본 `orange_agv.fbx`, 수정 원본 `orange_agv_refined.fbx` | `SourceAssets/OrangeAGV/`에서 Git으로 받음 |
| 게임용 수정 지게차 메시·재질 | `Content/Warehouse/AGV/`의 최종 에셋을 Git으로 받음. 다른 지게차 팩 다운로드 불필요 |
| 플레이어·작업자 기본 캐릭터, 맵, 코드, 복층·승강기 구성 | 프로젝트 Git에 포함됨 |
| 자체 제작 팔레트 전용 승강기 | `Art/Equipment/PalletLift/`의 Blender 원본·부품 FBX와 `Content/Warehouse/PalletLift/`의 최종 메시·재질. 추가 Fab 다운로드 불필요 |
| 자체 제작 컨베이어·회전 이송대·4층 화물 승강기 확장 | `Art/Equipment/Conveyor/`, `Art/Equipment/PalletLiftExtensions/` 원본·FBX와 `Content/Warehouse/Conveyor/`, `Content/Warehouse/PalletLift/Meshes/`의 완성 메시. 추가 외부 다운로드 불필요 |
| 기존 Poly Haven 상자·크레이트 | `Content/Warehouse/Cargo/PolyHaven/`가 Git에 포함됨. 현재 배치 화물은 위의 Fab 상자 팩을 사용 |
| 충돌 복사본, 파손용 물리 메시, 열린 트럭 파생 메시 | 위 4절의 스크립트로 로컬 생성. 별도 구매할 에셋이 아님 |
| 기둥·보·난간 후보로 전에 검색했던 다른 팩 | 현재 맵의 필수 다운로드 목록에 포함되지 않음 |

## 6. 설치 완료 확인

- [ ] 필수 팩 6개의 다운로드·추가를 완료했다.
- [ ] Game Animation Sample의 모션 설치를 끝냈고 `BS_WarehouseStanding`, `BS_WarehouseCrouching`과 변환된32개 시퀀스가 있다.
- [ ] `Content/Scene_Warehouse`, `VehicleVarietyPack`, `EuropeanBeech`, `GV_FreeShrubsPack`, `MSPresets`가 있다.
- [ ] 상자 FBX 임포트와 충돌 준비를 끝냈고 `SM_Carton_Intact_1/2`, `SM_Pallet_110`이 있다.
- [ ] `Content/Warehouse/Collision/`와 `Content/Warehouse/Physics/`가 생성됐다.
- [ ] `Content/Warehouse/Exterior/TruckInterior/SM_Truck_Open.uasset`이 있다.
- [ ] `Content/Warehouse/Cargo/BoxesPalletsPack/HeavyTest/Materials/`를 재생성했고 중량 시험 상자가 텍스처와4색으로 보인다.
- [ ] C++ 빌드를 완료하고 `msc_vr.uproject`를 UE 5.8에서 열었다.
- [ ] 기본 맵 `Content/FirstPerson/Lvl_FirstPerson.umap`에서 랙·팔레트·상자·트럭·나무·관목이 보인다.

모델이 안 보이면 **프로젝트 안의 폴더 경로**부터 확인합니다. 팩을 다른 폴더로 옮기면 저장된 맵의 참조가 맞지 않을 수 있습니다. 상자 품목·질량 규칙은 [CARGO_RULES.md](CARGO_RULES.md), 지게차 모델·바퀴 충돌 재생성 순서는 [REFINED_AGV.md](REFINED_AGV.md), 실제 강체 구동과 물리 준비·검사 기준은 [PHYSICS.md](PHYSICS.md)를 확인합니다.

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

## Game Animation Sample 설치

런처에서 UE5.8용 샘플을 **별도 프로젝트로 생성**합니다. 현재 게임의 C++ 빌드를 먼저 완료하고 두 프로젝트의 에디터를 종료한 뒤, `msc_vr` 폴더에서 아래 순서로 실행합니다. 샘플 위치가 다르면 `$sampleProject`만 바꿉니다.

```powershell
$projectRoot=(Get-Location).Path
$sampleProject='C:/msc_UnrealProject/GameAnimationSample/GameAnimationSample.uproject'
$editorCmd='C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
& $editorCmd $sampleProject -run=pythonscript "-script=$projectRoot/Scripts/import_game_animation_sample.py" -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -NullRHI -nop4
& $editorCmd "$projectRoot/msc_vr.uproject" -run=pythonscript "-script=$projectRoot/Scripts/configure_game_animation_sample.py" -unattended -NullRHI -nop4
```

첫 명령은 샘플의32개 클립을 UE5 체형으로 변환하고 샘플 전용 이벤트를 정리해 가져옵니다. 샘플 안에는 파생 결과 `/Game/Warehouse/AnimationSample/Sequences`가 생성됩니다. 두 번째 명령은 현재 게임의 Manny 스켈레톤에 맞추고 방향·속도별 블렌드 스페이스 두 개와 플레이어의 기본 모션 연결을 저장합니다. 기존 플레이어 Blueprint는 `Saved/Testing/AnimationSampleBackup/`에 최초 한 번 백업하며 창고 맵은 재생성하지 않습니다.

`GAME_ANIMATION_SAMPLE_IMPORT_PASSED`와 `GAME_ANIMATION_SAMPLE_CONFIGURATION_PASSED`를 각각 확인합니다. 모션 파생 에셋은 다른 Fab 에셋처럼 Git에서 제외하므로 팀원도 이 절차를 수행합니다. 적용 범위는 [플레이어 조작](WAREHOUSE.md#플레이어-조작), 실행 검증과 실제 물리는 [PHYSICS.md](PHYSICS.md)를 확인합니다.

## 무료 운반 애니메이션 후보 — 미적용

추가 운반 애니메이션은 **무료 에셋만** 대상으로 합니다. 2026-10-12 [Mixamo 공식 검색](https://www.mixamo.com/#/?query=box&type=Motion%2CMotionPack)에서 `Box Idle`(들고 서기), `Box Turn`(좌·우 회전), `Box Walk Arc`(곡선 보행), `Jogging With Box`(들고 뛰기)를 확인했습니다. [Adobe 공식 안내](https://helpx.adobe.com/creative-cloud/faq/mixamo-faq.html)는 Adobe ID로 무료 이용 및 게임 사용이 가능하다고 명시합니다.

현재 다운로드·리타게팅·게임 적용은 하지 않았으며 필수 설치 목록에 포함하지 않습니다. 앞서 검토한 Fab `Pick Up & Carry Animation Pack`은 무료를 확인하지 못해 제외합니다. 적용할 때는 현재 캐릭터에 동작을 맞추고 기존 상자 크기별 양손 IK와 연결해야 합니다.
