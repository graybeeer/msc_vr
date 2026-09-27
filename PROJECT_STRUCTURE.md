# 프로젝트 구조 요약

## 목적과 현재 단계

Unreal Engine 5.8 기반 PC용 무인 지게차 교육 시뮬레이션이다. 센서 인식 이상, 충돌, 포크와 팔레트/적재물 결합 실패 같은 상황을 재현하고, 창고 운영 조건별 효율을 비교하는 것이 목표다. 향후 물리 거동은 질량·관성·마찰·접촉·하중을 실제 장비에 맞춰 검증해야 한다.

**현재 구현:** Fab 예제 구성을 참고한 창고, E키 화물 집기, E키로 시작·정지하는 지게차의 직선 팔레트 운반 과정이 있다. 포크 삽입 깊이·각도·높이 검사, 사람(Pawn) 안전 구역 감지와 부품별 충돌 스윕을 사용한다. 차량 마찰·전복·적재물 미끄러짐, 실제 LiDAR, 자유 경로 계획 및 운영 효율 평가는 아직 없다. 팔레트 지지는 기하 검사 후 부착하는 방식이며 실제 차량 물리가 완성된 것으로 해석하지 않는다.

## 작업별 첫 탐색 경로

| 요청 유형 | 먼저 확인할 곳 | 필요할 때만 넓힐 곳 |
| --- | --- | --- |
| 창고 배치·랙·화물·조명 | `WAREHOUSE.md`, `Scripts/build_first_person_warehouse.py`, `Content/FirstPerson/Lvl_FirstPerson.umap` | `Content/__ExternalActors__/FirstPerson/Lvl_FirstPerson/` (엔진이 생성한 액터 데이터) |
| 밝기·자동 노출 | `Config/DefaultEngine.ini`의 AutoExposure, 맵 생성 스크립트의 작업등 | 인게임 카메라·후처리 설정 |
| 배치·충돌 검증 | `Scripts/verify_first_person_warehouse.py`, `Scripts/verify_forklift_training.py` | 실제 에디터의 충돌 뷰/플레이 테스트 |
| 상자·크레이트 집기 | `Source/msc_vr/msc_vrCharacter.h/.cpp`, `Source/msc_vr/WarehouseCargo.h/.cpp` | `Scripts/configure_carryable_cargo.py`, `Scripts/build_first_person_warehouse.py`의 화물 배치 |
| 지게차 E 시작·운반·사람 감지·충돌 | `Source/msc_vr/WarehouseForklift.h/.cpp`, `msc_vrCharacter.cpp` | `Scripts/verify_forklift_training.py`, 맵 생성 스크립트의 지게차 배치 |
| 팔레트 구멍·포크 정렬·삽입 조건 | `Source/msc_vr/WarehousePallet.h/.cpp`, `WarehouseForklift.cpp` | `Scripts/verify_forklift_training.py` |
| 정적 삼각형·동적 볼록 충돌 에셋 | `Scripts/prepare_warehouse_assets.py`, `Source/msc_vr/WarehouseCargo.cpp` | `Content/Warehouse/Collision/`, `Content/Warehouse/Physics/` (로컬 재생성물) |
| 향후 차량 물리·LiDAR·경로 계획 | `WAREHOUSE.md`의 구현 한계, `WarehouseForklift.cpp`의 현재 절차 | `Config/DefaultEngine.ini`, 관련 레벨/블루프린트 |
| 1인칭 조작·카메라 | `Source/msc_vr/msc_vrCharacter.*`, `msc_vrPlayerController.*`, `msc_vrCameraManager.*` | `Content/FirstPerson/Blueprints/`, `Content/Input/`, `Config/DefaultInput.ini` |
| 시작 맵·게임 모드 | `Config/DefaultEngine.ini`, `Source/msc_vr/msc_vrGameMode.*`, `Content/FirstPerson/Blueprints/` | `msc_vr.uproject` |
| Fab 창고 에셋·팀원 설치 | `WAREHOUSE.md`, `Scripts/prepare_warehouse_assets.py`, `.gitignore` | `Content/Scene_Warehouse/` 원본 팩 |
| 구조 문서 갱신 | `AGENTS.md`, `Scripts/update_structure_summary.ps1`, 이 문서 | 없음 |

`Scripts/build_first_person_warehouse.py`를 다시 실행하면 `WH_` 액터를 재생성하고 기존 템플릿 장애물을 제거한다. 다른 작업자의 레벨 변경이 있으면 실행 전 확인한다. 바이너리 `.umap`/`.uasset`과 World Partition 외부 액터 파일은 텍스트처럼 직접 편집하지 않는다.

## 현재 경로 구조

아래 구역은 `AGENTS.md`의 PowerShell 명령으로 갱신한다. 대용량 에셋 파일과 엔진 생성 캐시는 개별 열거하지 않는다.

<!-- STRUCTURE:START -->
```text
msc_vr/
  AGENTS.md
  msc_vr.uproject
  PROJECT_STRUCTURE.md
  README.md
  WAREHOUSE.md
  Config/
    DefaultEditor.ini
    DefaultEditorPerProjectUserSettings.ini
    DefaultEngine.ini
    DefaultGame.ini
    DefaultInput.ini
  Scripts/
    build_first_person_warehouse.py
    configure_carryable_cargo.py
    prepare_warehouse_assets.py
    update_structure_summary.ps1
    verify_first_person_warehouse.py
    verify_forklift_training.py
  Source/
    msc_vr.Target.cs
    msc_vrEditor.Target.cs
    msc_vr/
      msc_vr.Build.cs
      msc_vr.cpp
      msc_vr.h
      msc_vrCameraManager.cpp
      msc_vrCameraManager.h
      msc_vrCharacter.cpp
      msc_vrCharacter.h
      msc_vrGameMode.cpp
      msc_vrGameMode.h
      msc_vrPlayerController.cpp
      msc_vrPlayerController.h
      WarehouseCargo.cpp
      WarehouseCargo.h
      WarehouseForklift.cpp
      WarehouseForklift.h
      WarehousePallet.cpp
      WarehousePallet.h
      Variant_Horror/
        UI/
      Variant_Shooter/
        AI/
        UI/
        Weapons/
  Content/
    __ExternalActors__/
    __ExternalObjects__/
    Characters/
      Mannequins/
    Collections/
    Developers/
    FirstPerson/
      Lvl_FirstPerson.umap
      Anims/
      Blueprints/
    Input/
      Actions/
      Touch/
    LevelPrototyping/
      Interactable/
      Materials/
      Meshes/
      Textures/
    Scene_Warehouse/
    Variant_Horror/
      Lvl_Horror.umap
      Blueprints/
      Input/
      UI/
    Variant_Shooter/
      Lvl_Shooter.umap
      Anims/
      Blueprints/
      Input/
      UI/
    Warehouse/
      Collision/
      Materials/
      Physics/
    Weapons/
      GrenadeLauncher/
      Pistol/
      Rifle/
```
<!-- STRUCTURE:END -->

`Content/Scene_Warehouse/`는 Fab 팩이며 Git에서 제외된다. 다른 팀원은 동일 경로에 설치해야 한다. `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `.vs/`는 생성물/캐시이므로 보통 탐색 대상이 아니다. `Variant_Horror`, `Variant_Shooter`, `Characters`, `Weapons`, `LevelPrototyping`은 현재 창고 기능의 핵심이 아닌 템플릿 콘텐츠다.
