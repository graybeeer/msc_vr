# 프로젝트 구조 요약

## 목적과 현재 단계

Unreal Engine 5.8 기반 PC용 무인 지게차 교육 시뮬레이션이다. 센서 인식 이상, 충돌, 포크와 팔레트/적재물 결합 실패 같은 상황을 재현하고, 창고 운영 조건별 효율을 비교하는 것이 목표다. 향후 물리 거동은 질량·관성·마찰·접촉·하중을 실제 장비에 맞춰 검증해야 한다.

**현재 맵:** 3층 복층·승강기, 트럭 적재함과 외부 환경, 양손 화물 운반, 1층 지게차 2대의 자율 작업·G 스마트폰 원격 조작, 센서/충돌/하중 검사와 자동 충전이 있다. 사람·차량·화물은 Chaos 강체와 유한 구동력을 사용한다. 실행 검증·계수·한계는 `PHYSICS.md`, 맵은 `WAREHOUSE.md`, 장비 사양은 `VNSL14_SPEC.md`를 확인한다. 별도 개발 중인 `Source/msc_vr/Agv/`의 타이어·LiDAR 모듈은 현재 `WarehouseForklift`에 통합되지 않았다.

## 작업별 첫 탐색 경로

| 요청 유형 | 먼저 확인할 곳 | 필요할 때만 넓힐 곳 |
| --- | --- | --- |
| 공통 충돌·마찰·관성·실제 강체 구동 | `PHYSICS.md`, `WarehousePhysics.h/.cpp`, `WarehouseForkliftPhysics.cpp`, `WarehouseWorker.cpp`, `msc_vrCharacter.cpp` | `WarehouseDamageSystem`, `WarehouseElevator`, `Config/DefaultEngine.ini`, `Scripts/prepare_warehouse_dynamics.py`, `Scripts/verify_warehouse_contact_physics.py` |
| 실제 Chaos 물리·주행·운반·손상 검증 | `PHYSICS.md`의 검증 표, `Scripts/verify_native_forklift_motion_pie.py`, `Scripts/verify_warehouse_contact_physics.py`, `Scripts/verify_native_damage_pie.py` | 승강기 호출은 `Scripts/verify_native_elevator_pie.py`, 저장된 창고 시작 상태는 `Scripts/verify_dynamic_warehouse_startup.py`; 과거 동기식 검사는 현재 강체 검증에 쓰지 않음 |
| 창고 작업자·역할 이름표·배치 | `Source/msc_vr/WarehouseWorker.h/.cpp`, `Scripts/configure_warehouse_workers.py`, `Scripts/verify_warehouse_workers.py` | `WAREHOUSE.md`의 역할별 작업자, 기본 플레이어 전신 메시 |
| 3층 복층·승강기·층간 운반 | `Scripts/configure_mezzanine.py`, `Source/msc_vr/WarehouseElevator.h/.cpp`, `WarehouseForkliftAI.cpp` | `MEZZANINE.md`, `Scripts/verify_mezzanine.py`, `Scripts/verify_mezzanine_pie.py`, `Scripts/verify_dynamic_forklift_transfer_pie.py`; 관찰 층 선택은 PlayerController |
| 창고 배치·랙·화물·조명 | `WAREHOUSE.md`, `Scripts/build_first_person_warehouse.py`, `Content/FirstPerson/Lvl_FirstPerson.umap` | `Content/__ExternalActors__/FirstPerson/Lvl_FirstPerson/` (엔진이 생성한 액터 데이터) |
| 지게차 기준 실측 크기·팔레트/중량/경량 랙·통로·팔레트·트럭 | `Scripts/apply_real_world_scale.py`, `Scripts/verify_real_world_scale.py`, `WAREHOUSE.md`의 실제 크기 기준 | `VNSL14_SPEC.md`, 창고/외부 에셋 배치 스크립트 |
| 트럭 후면 개방·적재함 내부·택배 적재 | `Scripts/configure_truck_interior.py`, `Scripts/verify_truck_interior_pie.py`, `WAREHOUSE.md`의 트럭 내부 절 | 맵의 `TRK_` 액터, `Content/Warehouse/Exterior/TruckInterior/` (Fab 파생 메시, Git 제외); 큰 지게차 안내는 `msc_vrPlayerController.cpp` |
| 입고·출고 트럭 분류·작업 완료·출발 허가·자동 교대 설계 | `TRUCK_LOGISTICS_DESIGN.md` (구현 전 설계) | `Scripts/configure_truck_interior.py`, `Scripts/build_warehouse_exterior.py`, `WarehouseCargo`, `WarehouseForkliftAI`의 작업 보고, `WarehouseDamageSystem` |
| 건물 외관·트럭 진입로·하역장 | `WAREHOUSE.md`의 외부 에셋 선정/설치 상태, `Scripts/build_warehouse_exterior.py`, `Scripts/verify_warehouse_exterior.py` | 맵의 `EXT_` 액터, `Content/Warehouse/Exterior/Materials/` |
| 밝기·자동 노출 | `Config/DefaultEngine.ini`의 AutoExposure, 맵 생성 스크립트의 작업등 | 인게임 카메라·후처리 설정 |
| 배치·충돌 검증 | `Scripts/verify_first_person_warehouse.py`, `Scripts/verify_forklift_training.py` | 실제 에디터의 충돌 뷰/플레이 테스트 |
| 화물 품목·밀도/부피별 무게·상자 크기·향후 트럭 입고 규칙 | `CARGO_RULES.md`, `Source/msc_vr/WarehouseCargo.h/.cpp`, `Scripts/configure_cargo_variety.py`, `Scripts/verify_cargo_rules.py` | `Scripts/import_boxes_pallets_pack.py`, `Content/Warehouse/Cargo/BoxesPalletsPack/` (Git 제외), `SourceAssets/Cargo/BoxesPalletsPack/` (Git 제외), `Scripts/download_cargo_assets.ps1`, `Scripts/import_cargo_assets.py`, `Content/Warehouse/Cargo/PolyHaven/`, `SourceAssets/Cargo/PolyHaven/`, `Scripts/verify_cargo_variety_pie.py`; 화면 표시는 PlayerController, 하중은 DamageSystem |
| 상자·크레이트·빈 팔레트 집기 | `Source/msc_vr/msc_vrCharacter.h/.cpp`, `Source/msc_vr/WarehouseCargo.h/.cpp`, `WarehousePallet.h/.cpp` | `Scripts/verify_cargo_pallet_physics_pie.py`, `Scripts/configure_carryable_cargo.py`, `Scripts/build_first_person_warehouse.py`의 화물 배치 |
| 온전한 상자·낙하 안정성·팔레트 동적 물리 | `Scripts/prepare_intact_cargo_physics.py`, `Scripts/apply_intact_cargo_physics.py`, `Scripts/verify_cargo_pallet_physics_pie.py` | `WarehouseCargo.cpp`의 `ConfigureCarryPhysics`, `msc_vrCharacter.cpp`의 `DropCargo`, `WarehousePallet.cpp`; 로컬 파생 메시 `SM_Carton_Intact_1/2`, `SM_Pallet_110` |
| 캐릭터 미끄러짐·보행 애니메이션·제동 | `msc_vrCharacter.cpp`의 `UpdatePhysicalMovement`, `WarehouseWorker.cpp`의 `DriveCapsule`, `Scripts/verify_character_drive_response_pie.py` | `PHYSICS.md`, `Content/Characters/Mannequins/Anims/Unarmed/`, `Content/FirstPerson/Anims/` |
| 양손 운반 자세·걷기 연결·뒤통수 가림 | `Source/msc_vr/WarehouseCarryAnimInstance.h/.cpp`, `msc_vrCharacter.cpp`의 `InitializeCarryMesh`/`UpdateCarryPose` | `Scripts/verify_view_and_wheel_fixes_pie.py`, `Scripts/verify_character_drive_response_pie.py`, `Scripts/verify_two_hand_carry.py`, `Content/FirstPerson/Anims/` |
| 지게차 가속·제동·자리 회전·1.8m/s 적용 | `WarehouseForkliftPhysics.cpp`, `WarehouseForkliftRemote.cpp`, `Scripts/apply_forklift_drive_tuning.py`, `Scripts/verify_character_drive_response_pie.py` | `VNSL14_SPEC.md`, `PHYSICS.md`, 자율 경로는 `WarehouseForkliftAI.cpp` |
| 운반 물체 반투명·시야 확보 | `msc_vrCharacter.cpp`의 `TryPickupCargo`/`DropCargo`, `msc_vrCharacter.h`의 `CarryOpacity` | `Scripts/prepare_carry_material.py`, `Content/Warehouse/Materials/M_CarryTransparent.uasset`, `Scripts/verify_two_hand_carry.py` |
| 자율 작업·자체 점검·경로·팔레트 인식·작업 큐/보고 | `AUTONOMY.md`, `Source/msc_vr/WarehouseForkliftAI.cpp`, `WarehouseAutonomyTypes.h`, `Scripts/configure_forklift_autonomy.py` | `Scripts/verify_forklift_autonomy.py`, `Scripts/verify_forklift_autonomy_pie.py`, `WarehouseForklift.h/.cpp` |
| 1초 장애물 확인·결과 유지·다음 검사에서 해제 | `WarehouseForklift.cpp`의 `RefreshObstacleChecks`/`ClearToMove`, `WarehouseForkliftAI.cpp`의 `FollowRoute`, `WarehouseForkliftRemote.cpp`의 `RemotePoseClear` | `AUTONOMY.md`, `Scripts/verify_obstacle_interval_pie.py`; 실제 물리 충돌은 `WarehousePhysics`/`WarehouseForkliftPhysics` |
| 지게차 E 시작·운반·사람 감지·충돌 | `Source/msc_vr/WarehouseForklift.h/.cpp`, `msc_vrCharacter.cpp` | `Scripts/verify_forklift_training.py`, 맵 생성 스크립트의 지게차 배치 |
| 1층 지게차 2대·개별 작업·G 스마트폰 원격 조작 | `Source/msc_vr/WarehouseForkliftRemote.cpp`, `msc_vrCharacter.h/.cpp`, `Scripts/configure_dual_forklifts.py` | `AUTONOMY.md`, `Scripts/verify_dual_forklifts_remote_pie.py`; 양손 자세는 `WarehouseCarryAnimInstance`, 안내창은 `msc_vrPlayerController`, 중복 배정은 `WarehouseForkliftAI` |
| 원격 조작 캐릭터·스마트폰 30% 반투명·종료 시 비활성화 | `msc_vrCharacter.cpp`의 `SetRemoteCharacterFade`/`BeginRemoteControl`/`EndRemoteControl`, `Scripts/prepare_remote_character_fade.py` | `Content/Warehouse/Materials/RemoteCharacter/`, `Scripts/verify_character_drive_response_pie.py`; 조작 규칙은 `AUTONOMY.md` |
| 질량·과적·충격 손상·랙 붕괴 | `Source/msc_vr/WarehouseDamageSystem.h/.cpp`, `Scripts/configure_warehouse_strength.py`, `Scripts/verify_warehouse_strength.py`, `Scripts/verify_warehouse_strength_pie.py`, `WAREHOUSE.md`의 하중·손상 절 | `WarehouseForklift.cpp`의 접촉/고장 처리, `prepare_warehouse_assets.py`의 볼록 충돌 복사본 |
| VNSL14 사양·하중·속도·배터리·충전 | `VNSL14_SPEC.md`, `Source/msc_vr/WarehouseForklift.h/.cpp`, `WarehouseChargingStation.h/.cpp` | `WarehousePallet.h/.cpp`, `Scripts/apply_orange_agv.py`, `Scripts/verify_forklift_training.py` |
| 주황 AGV 모델·바퀴·기계 리그·PBR 재질 | `REFINED_AGV.md`, `Scripts/prepare_refined_agv.py`, `Scripts/apply_refined_agv.py`, `Source/msc_vr/WarehouseForklift.cpp` | 바퀴 위치만 복원은 `Scripts/restore_front_wheel_mounts.py`; `SourceAssets/OrangeAGV/orange_agv_refined.fbx`, `Content/Warehouse/AGV/Meshes/`, `Content/Warehouse/AGV/Materials/`, `Scripts/verify_refined_agv.py`, `Scripts/verify_view_and_wheel_fixes_pie.py`, `Scripts/verify_refined_agv_pie.py`; 기존 prepare/apply Orange AGV 스크립트는 과거 모델 복원용 |
| 팔레트 구멍·포크 정렬·삽입 조건 | `Source/msc_vr/WarehousePallet.h/.cpp`, `WarehouseForklift.cpp` | `Scripts/verify_forklift_training.py` |
| 공통 목재 팔레트 모델·재질 | `WarehousePallet.cpp`, `Scripts/apply_real_world_scale.py`, `Scripts/prepare_warehouse_assets.py` | 원본 `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Storage_Pallet_Wood_Worn_01/`; 공통 실측 물리 복사본 `Content/Warehouse/Physics/SM_Pallet_110.uasset` (준비 스크립트로 재생성) |
| 정적 삼각형·동적 볼록 충돌 에셋 | `Scripts/prepare_warehouse_assets.py`, `Source/msc_vr/WarehouseCargo.cpp` | `Content/Warehouse/Collision/`, `Content/Warehouse/Physics/` (로컬 재생성물) |
| 물리 실측·LiDAR·경로 계획 확장 | `PHYSICS.md`의 실측 전 가정, `AUTONOMY.md`의 경로·인식 범위 | `WarehouseForkliftPhysics.cpp`, `Config/DefaultEngine.ini`, 관련 레벨/블루프린트 |
| 별도 Agv 동역학·LiDAR·내비게이션 모듈 | `Source/msc_vr/Agv/FORKLIFT_NAVIGATION.md`, 같은 폴더의 관련 컴포넌트 | `Scripts/verify_agv_*.py`; 현재 맵 지게차의 자율 작업은 `WarehouseForkliftAI.cpp` |
| 달리기·앉기·팔레트에 내려놓기·쌓인 상자 중력 | `Source/msc_vr/msc_vrCharacter.h/.cpp`, `WarehouseCargo.h/.cpp`, `Scripts/verify_warehouse_interaction_pie.py` | `WAREHOUSE.md`의 플레이어 조작, 기존 운반/파손 검사 |
| 설정 메뉴·전지적 관찰 카메라·지붕 숨김·F1 와이어프레임 충돌 | `Source/msc_vr/msc_vrPlayerController.h/.cpp`, `Config/DefaultInput.ini`의 `DebugExecBindings`, `Scripts/configure_observer_view.py` | `Scripts/verify_view_and_wheel_fixes_pie.py`, `Scripts/verify_observer_view_pie.py`, `Scripts/verify_warehouse_interaction_pie.py`, `Saved/Config/WindowsEditor/Game.ini` (로컬 설정) |
| 1인칭 조작·카메라 | `Source/msc_vr/msc_vrCharacter.*`, `msc_vrPlayerController.*`, `msc_vrCameraManager.*` | `Content/FirstPerson/Blueprints/`, `Content/Input/`, `Config/DefaultInput.ini` |
| 시작 맵·게임 모드 | `Config/DefaultEngine.ini`, `Source/msc_vr/msc_vrGameMode.*`, `Content/FirstPerson/Blueprints/` | `msc_vr.uproject` |
| Fab 창고 에셋·팀원 설치 | `TEAM_ASSETS.md` (필수 5개·설치·목록 누락 해결·로컬 재생성), `WAREHOUSE.md`, `Scripts/prepare_warehouse_assets.py`, `.gitignore` | `Content/Scene_Warehouse/` 원본 팩 |
| Fab 트럭·나무·관목 설치·배치 | `WAREHOUSE.md`의 외부 에셋 절, `Scripts/apply_exterior_fab_assets.py`, `Scripts/prepare_warehouse_assets.py` | `Content/VehicleVarietyPack/`, `Content/EuropeanBeech/`, `Content/MSPresets/`, `Content/GV_FreeShrubsPack/` (Git 제외 원본 팩) |
| 구조 문서 갱신 | `AGENTS.md`, `Scripts/update_structure_summary.ps1`, 이 문서 | 없음 |

`Scripts/build_first_person_warehouse.py`를 다시 실행하면 `WH_` 액터를 재생성하고 기존 템플릿 장애물을 제거하며 `EXT_` 외부 환경은 유지한다. `Scripts/build_warehouse_exterior.py`는 `EXT_` 액터만 재생성한다. 다른 작업자의 레벨 변경이 있으면 실행 전 확인한다. 바이너리 `.umap`/`.uasset`과 World Partition 외부 액터 파일은 텍스트처럼 직접 편집하지 않는다.

## 현재 경로 구조

아래 구역은 `AGENTS.md`의 PowerShell 명령으로 갱신한다. 대용량 에셋 파일과 엔진 생성 캐시는 개별 열거하지 않는다.

<!-- STRUCTURE:START -->
```text
msc_vr/
  AGENTS.md
  AUTONOMY.md
  CARGO_RULES.md
  MEZZANINE.md
  msc_vr.uproject
  PHYSICS.md
  PROJECT_STRUCTURE.md
  README.md
  REFINED_AGV.md
  TEAM_ASSETS.md
  TRUCK_LOGISTICS_DESIGN.md
  VNSL14_SPEC.md
  WAREHOUSE.md
  Config/
    DefaultEditor.ini
    DefaultEditorPerProjectUserSettings.ini
    DefaultEngine.ini
    DefaultGame.ini
    DefaultInput.ini
  Scripts/
    apply_exterior_fab_assets.py
    apply_forklift_drive_tuning.py
    apply_intact_cargo_physics.py
    apply_orange_agv.py
    apply_real_world_scale.py
    apply_refined_agv.py
    apply_visual_fixes.py
    build_agv_motion_test.py
    build_first_person_warehouse.py
    build_warehouse_exterior.py
    configure_cargo_variety.py
    configure_carryable_cargo.py
    configure_dual_forklifts.py
    configure_forklift_autonomy.py
    configure_mezzanine.py
    configure_observer_view.py
    configure_truck_interior.py
    configure_warehouse_strength.py
    configure_warehouse_workers.py
    convert_agv_original5_cm.py
    convert_agv_refined_cm.py
    download_cargo_assets.ps1
    import_agv_original5.py
    import_agv_refined.py
    import_boxes_pallets_pack.py
    import_cargo_assets.py
    place_agv_in_warehouse.py
    prepare_carry_material.py
    prepare_intact_cargo_physics.py
    prepare_orange_agv.py
    prepare_original_body.py
    prepare_refined_agv.py
    prepare_remote_character_fade.py
    prepare_warehouse_assets.py
    prepare_warehouse_dynamics.py
    restore_front_wheel_mounts.py
    update_structure_summary.ps1
    verify_agv_dynamics.py
    verify_agv_hazards.py
    verify_agv_localization.py
    verify_agv_navigation.py
    verify_agv_safety.py
    verify_agv_warehouse.py
    verify_cargo_pallet_physics_pie.py
    verify_cargo_rules.py
    verify_cargo_variety_pie.py
    verify_character_drive_response_pie.py
    verify_dual_forklifts_remote_pie.py
    verify_dynamic_forklift_transfer_pie.py
    verify_dynamic_warehouse_startup.py
    verify_first_person_warehouse.py
    verify_forklift_autonomy.py
    verify_forklift_autonomy_pie.py
    verify_forklift_training.py
    verify_mezzanine.py
    verify_mezzanine_pie.py
    verify_native_damage_pie.py
    verify_native_elevator_pie.py
    verify_native_forklift_motion_pie.py
    verify_observer_view_pie.py
    verify_obstacle_interval_pie.py
    verify_real_world_scale.py
    verify_refined_agv.py
    verify_refined_agv_pie.py
    verify_truck_interior_pie.py
    verify_two_hand_carry.py
    verify_view_and_wheel_fixes_pie.py
    verify_warehouse_contact_physics.py
    verify_warehouse_exterior.py
    verify_warehouse_interaction_pie.py
    verify_warehouse_strength.py
    verify_warehouse_strength_pie.py
    verify_warehouse_workers.py
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
      WarehouseAutonomyTypes.h
      WarehouseCargo.cpp
      WarehouseCargo.h
      WarehouseCarryAnimInstance.cpp
      WarehouseCarryAnimInstance.h
      WarehouseChargingStation.cpp
      WarehouseChargingStation.h
      WarehouseDamageSystem.cpp
      WarehouseDamageSystem.h
      WarehouseElevator.cpp
      WarehouseElevator.h
      WarehouseForklift.cpp
      WarehouseForklift.h
      WarehouseForkliftAI.cpp
      WarehouseForkliftPhysics.cpp
      WarehouseForkliftRemote.cpp
      WarehousePallet.cpp
      WarehousePallet.h
      WarehousePhysics.cpp
      WarehousePhysics.h
      WarehouseWorker.cpp
      WarehouseWorker.h
      Agv/
      Variant_Horror/
        UI/
      Variant_Shooter/
        AI/
        UI/
        Weapons/
  Content/
    __ExternalActors__/
    __ExternalObjects__/
    AgvTest/
    Characters/
      Mannequins/
    Collections/
    Developers/
    EuropeanBeech/
      Foliage/
      Geometry/
      Maps/
      Materials/
      Textures/
    FirstPerson/
      Lvl_FirstPerson.umap
      Anims/
      Blueprints/
    GV_FreeShrubsPack/
      Demo/
      GlobalFoliageActor/
      Maps/
      Materials/
      Meshes/
      Textures/
    Input/
      Actions/
      Touch/
    LevelPrototyping/
      Interactable/
      Materials/
      Meshes/
      Textures/
    MSPresets/
      MS_Foliage_Material/
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
    VehicleVarietyPack/
      Blueprints/
      Maps/
      Materials/
      Meshes/
      Skeletons/
      Textures/
    Warehouse/
      AGV/
      Cargo/
      Collision/
      Exterior/
      Materials/
      Physics/
    Weapons/
      GrenadeLauncher/
      Pistol/
      Rifle/
  SourceAssets/
```
<!-- STRUCTURE:END -->

`Content/Scene_Warehouse/`는 Fab 팩이며 Git에서 제외된다. 다른 팀원은 동일 경로에 설치해야 한다. `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `.vs/`는 생성물/캐시이므로 보통 탐색 대상이 아니다. `Variant_Horror`, `Variant_Shooter`, `Characters`, `Weapons`, `LevelPrototyping`은 현재 창고 기능의 핵심이 아닌 템플릿 콘텐츠다.
