# 프로젝트 구조 요약

## 목적과 현재 단계

Unreal Engine 5.8 기반 PC용 무인 지게차 교육 시뮬레이션이다. 센서 인식 이상, 충돌, 포크와 팔레트/적재물 결합 실패 같은 상황을 재현하고, 창고 운영 조건별 효율을 비교하는 것이 목표다. 향후 물리 거동은 질량·관성·마찰·접촉·하중을 실제 장비에 맞춰 검증해야 한다.

**현재 맵:** 3층 복층·승강기, 트럭 적재함과 외부 환경, 양손 화물 운반, 1층 지게차 2대의 자율 작업·G 스마트폰 원격 조작, 센서/충돌/하중 검사와 자동 충전이 있다. 사람·차량·화물은 Chaos 강체와 유한 구동력을 사용한다. 실행 검증·계수·한계는 `PHYSICS.md`, 맵은 `WAREHOUSE.md`, 장비 사양은 `VNSL14_SPEC.md`를 확인한다. 기존 `WarehouseForklift`에 `Agv`의 센서만 연결하며, 별도 타이어·주행 알고리즘은 연결하지 않는다.

## 실행 구조 한눈에 보기

2026-10-25 로컬 `main`의 `391c5f8b`에서 구조 정리를 시작하고, 초기 배치 저장·반복 실험·운영 성과 기능을 반영했다. 아래는 현재 실행 경계이며, 추가 제안·우선순위는 [시뮬레이션 개선안](SIMULATION_ROADMAP.md)에서 별도로 관리한다. 기능별 실제 검증 범위는 담당 문서에서 확인한다.

| 담당 | 현재 진입점 | 연결되는 영역 |
| --- | --- | --- |
| 조작·관찰·명령 화면 | `msc_vrCharacter`, `msc_vrPlayerController`, `WarehouseComputerDashboard`, `WarehouseComputerUILegacy` | 플레이어 상호작용, 배치 도구, 관제의 명령 API |
| 재고·작업·배송편 | `WarehouseComputer`, `WarehouseInboundZone`, `WarehouseShippingSystem`, `WarehouseLayoutInventory` | 팔레트 작업 제출·완료 보고, 입고 확정, 트럭 인계, 보류 물류 |
| 차량별 자율 운반 | `WarehouseForkliftAI`, `WarehouseForkliftRecovery`, `WarehouseForkliftParking`, `WarehouseForkliftPerception` | 작업 상태·경로·복구·충전, 고정 지도와 센서 관측 |
| 다중 차량 교통 | `WarehouseFleetCoordinator` | 차량 경로의 공간 예약·양보·우회·폐쇄, 공유 충전 배정 |
| 접촉·설비 | `WarehousePhysics`, `WarehouseDamageSystem`, `WarehouseForkliftPhysics`, `WarehouseElevator`, `WarehousePalletLift`, `WarehouseConveyor` | Chaos 구동·받침·손상, 범용/화물용 승강기와 컨베이어 인계 |
| 공간 편집·시간 | `WarehouseLayoutEditor`, `WarehouseLayoutSnap`, `WarehouseBuildingLayout`, `WarehouseConstructionEditor`, `WarehouseSimulationClock` | 세션 내 재배치·건물 편집, 지도 갱신, 1/2/4배 시계 |
| 배치 보관·반복 실험·성과 | `WarehouseExperimentStorage`, `WarehouseExperimentSubsystem`, `WarehouseExperimentMetrics`, `WarehouseExperimentUI` | 초기 배치 JSON 복원, 공통 주문·새 레벨 반복, 완료·대기 시간과 CSV/JSON 비교 |
| 검증·모델·맵 | `Scripts/verify_*`, `Art/`, `Content/`, `Config/` | 회귀 검사·적용 도구, 자체 모델 원본, Unreal 에셋·맵, 실행 설정 |

관제·차량·설비는 현재 같은 게임 프로세스 안에서 연결된다. `Source/msc_vr/Agv/` 중 센서만 운용 차량에 연결하며, 별도 주행·위치 추정은 독립 개발 영역이다. 자세한 상태 흐름과 정보 경계는 [알고리즘 도식](FLEET_ALGORITHM_COMPARISON.md)과 [자율 운반 기준](AUTONOMY.md)을 따른다.

## 작업별 첫 탐색 경로


| 요청 유형 | 먼저 확인할 곳 | 필요할 때만 넓힐 곳 |
| --- | --- | --- |
| 현재 구조 평가·추가 개발 우선순위 | `SIMULATION_ROADMAP.md`, 이 문서의 실행 구조 표 | 구현된 1·2번 범위는 `WAREHOUSE.md`, 운용 기준은 `AUTONOMY.md`, 물리 가정은 `PHYSICS.md`, 입출고 계획은 `TRUCK_LOGISTICS_DESIGN.md` |
| 초기 배치 저장·불러오기·같은 주문 반복·성과 대시보드 | `WarehouseExperimentSubsystem.h/.cpp`, `WarehouseExperimentStorage.h/.cpp`, `WarehouseExperimentMetrics.h/.cpp`, `WarehouseExperimentUI.cpp`, `WAREHOUSE.md`의 실험 절 | 저장은 새 레벨 BeginPlay 전 복원, 계측은 기존 보고 관측; `Scripts/verify_warehouse_experiments_pie.py`, `verify_warehouse_experiment_transport.py`, `verify_warehouse_experiment_ui.py`; 목적지 예약·주문 순서·재시작 기록은 `verify_warehouse_experiment_guards.py`, native `Warehouse.Experiments.MetricsAccounting`; 결과는 Git 제외 `Saved/WarehouseExperiments/` |
| 충돌/비충돌 공통 기준·선택 전용 판·모든 판정의 대상 분류 | `COLLISION_POLICY.md`, `WarehousePhysics.h/.cpp`, `Scripts/verify_collision_policy_pie.py` | `WarehouseWaitingBay`, `WarehouseDamageSystem`, `WarehouseLayoutEditor`, `WarehouseForklift*`, `WarehouseElevator`, `Config/DefaultEngine.ini` |
| 배치 모드 재배치·추가·삭제·다중 선택·스냅 적재·시설 누락 | `WarehouseLayoutEditor.h/.cpp`, `WarehouseLayoutInventory.h/.cpp`, `WarehouseLayoutSnap.h/.cpp`, `Scripts/verify_layout_inventory_pie.py`, `Scripts/verify_layout_editor_pie.py` | 하단 Slate 도구·렌더링 검사는 `msc_vrPlayerController`, `Scripts/verify_layout_editing_ui_pie.py`, 승강기 개구부는 `WarehouseElevatorLayout`, 슬롯은 `WarehouseInboundZone`, 사용 기준은 `WAREHOUSE.md` |
| 배치 모두 범주·혼합 선택·우클릭 좌우90도·공통 축 회전·삭제 | `WarehouseLayoutEditor::RotateSelection`/`CanRotateSelection`, `msc_vrPlayerController::OpenLayoutRotationMenu`/`RotateLayoutSelection`/`DeleteLayoutSelection` | 실제 부품·참조·물리 복원은 `WarehouseLayoutEditor`, 층별 개구부는 `WarehouseElevatorLayout`, 검사는 위 배치 native/UI 스크립트 |
| 전지적 진입 시 기본 재배치·깨끗한 관찰·하단 층 선택·컨베이어 도구 | `msc_vrPlayerController::ToggleObserverView`/`SetLayoutEditing`/`SetObserverFloor`, `Scripts/verify_layout_editing_ui_pie.py` | 관찰 진입은 이미 편집을 켜므로 검사·외부 호출은 `SetLayoutEditing(bool)`로 의도를 명시한다. 도구 표시와 입력은 같은 컨트롤러, 일시정지·취소 복원은 `WarehouseLayoutEditor::SetEditing`, 사용법은 `WAREHOUSE.md` |
| 전지적1m 타일 건물·복층·계단 편집·최대4층·위험한 축소 차단 | `WarehouseBuildingLayout.h/.cpp`, `WarehouseConstructionEditor.h/.cpp`, `Scripts/verify_construction_editor_pie.py` | 하단 도구와 격자 표시 `msc_vrPlayerController`, 기존 구조 캡처·실패 복원은 `WarehouseBuildingLayout`, 현재 세션 사용법은 `WAREHOUSE.md`, 승강기 층수·개구부는 `WarehouseElevator`·`WarehousePalletLift`·`WarehouseElevatorLayout`, 실제4층 회귀는 `verify_building_lift_four_floors_pie.py` |
| 자동 팔레트/상자 컨베이어·90도 회전·끝단 픽업·화물용 승강기 인계 | `WarehouseConveyor.h/.cpp`, `WarehousePalletLift.h/.cpp`, `Scripts/verify_conveyor_transport_pie.py`, `Scripts/verify_conveyor_turntable_pie.py` | 장비별 고정 지도 태그·충돌은 `WarehousePhysics`·`AgvShadowSensors`, 자유 각도·직결·승강기 공급/수신 포트 UI·물리 복원은 `WarehouseLayoutEditor`·`msc_vrPlayerController`와 `verify_conveyor_layout_pie.py`, 원본·FBX·미리보기는 `Art/Equipment/Conveyor/`와 `build_conveyor_model.py`, 전용 샤프트는 `build_pallet_lift.py`의 powered 변형. 설치는 `install_warehouse_transport_lab.py`, 사용·설치·물리 계수는 각각 `WAREHOUSE.md`·`TEAM_ASSETS.md`·`PHYSICS.md` |
| 건물 변경 후 승강기 재배치·삭제·개구부 복원과 기본 맵 보존 | `Scripts/verify_building_shaft_layout_pie.py`, `Scripts/verify_building_saved_map_pie.py`, `WarehouseBuildingLayout`·`WarehouseElevatorLayout` | 최초 구조와 기존 화물 유지 검사는 `verify_construction_editor_pie.py`, 실제4층 왕복과 문 가이드 검사는 `verify_building_lift_four_floors_pie.py`; 검사는 원본 맵을 저장하지 않음 |
| 보류된 물류 FIFO·품목/질량/크기/재질 보존·컴퓨터 데이터 입력/삭제 | `WarehouseLayoutInventory.h/.cpp`, `WarehouseComputer.h/.cpp`, `WarehouseComputerUI.cpp`, `Scripts/verify_layout_inventory_pie.py` | 접촉/손상 참조는 `WarehouseDamageSystem`, 상품 생성 규칙은 `CARGO_RULES.md`, 사용자 조작은 `WAREHOUSE.md` |
| 공유 충전소·일반 대기 자리·낮은 배터리 우선·기회 충전·작업 선점 | `WarehouseForkliftParking.cpp`, `WarehouseWaitingBay.h/.cpp`, `WarehouseChargingStation.h/.cpp`, `WarehouseFleetCoordinator`, `Scripts/verify_shared_charging_pie.py`, `Scripts/configure_warehouse_waiting_bays.py` | 작업 상태 전환은 `WarehouseForkliftAI`, 운영 정책은 `AUTONOMY.md`, 배터리 수치는 `VNSL14_SPEC.md` |
| 충전기 상상 이미지 외형·Blender/Unreal 캡처·맵 적용·FBX 전달 | `CHARGER_MODEL.md`, `Art/Equipment/AGVCharger/agv_charger.blend`, 같은 폴더의 `Delivery/AGV_ChargingStation.fbx`, `Content/Warehouse/ChargingStation/` | `Scripts/build_agv_charger.py`, `Scripts/export_agv_charger.py`, `Scripts/apply_agv_charger.py`, 접점 기준은 `WarehouseChargingStation.cpp` |
| 창고 천장·벽면 설비 조사·모델링 전 구상도 | `Art/Planning/WarehouseServices/CONCEPT.md`, 같은 폴더의 `warehouse_services_concept.png` | 현재 공간은 `WAREHOUSE.md`, 복층은 `MEZZANINE.md` |
| 창고 설비 모듈·밀도/벽·천장 장착·장식 충돌·Blender/Unreal 렌더 | `WAREHOUSE_SERVICES_MODEL.md`, `Art/Environment/WarehouseServices/`, `Content/Warehouse/Services/`, `Scripts/configure_warehouse_services_collision.py` | 모델은 `build_warehouse_services.py`·`preview_warehouse_services.py`·`export_warehouse_services.py`, 최신 배치는 `refine_warehouse_services_layout.py`, 검증/캡처는 `capture_dense_warehouse_services.py` |
| 고무 기둥 코너·평판·전체 보호대 모델·FBX | `WAREHOUSE_SERVICES_MODEL.md`의 추가 기둥 보호대, `Art/Environment/WarehouseServices/ColumnProtection/` | `Scripts/build_column_protectors.py` |
| 기존 지게차 2대의 초기 위치를 연결 충전기로 통합 | `Scripts/configure_charger_start_positions.py`, `WarehouseChargingStation.h/.cpp` | 에디터에서 기존 차량과 분리된 부품까지 actor 기준 상대 자세·크기를 보존해 시작 위치를 저장. 실행 중 재배치는 `WarehouseLayoutEditor`, 사용·검증 범위는 `WAREHOUSE.md` |
| 옮긴 충전기로 실제 주행·도킹·배터리 증가·출차 검사 | `Scripts/verify_native_charger_layout_pie.py` | 저장 원본 리그·충전기를 복사한 미저장 Entry PIE. 실제 저장 맵의 선택·그룹/물리 복원은 `verify_layout_editor_pie.py`, 충전 정책은 `VNSL14_SPEC.md` |
| 1/2/4배 시뮬레이션·같은 물리 간격·실효 배속 | `WarehouseSimulationClock.h/.cpp`, `msc_vrPlayerController`, `Scripts/verify_simulation_speed_pie.py` | 물리/시간 기준은 `PHYSICS.md`, 작업 시간 UI는 `WarehouseComputerUI.cpp` |
| 창고 CPU/GPU 병목·native CSV 성능 측정·받침 스냅샷·경로 질의 최적화 | `WarehousePerformance.h`, `Scripts/profile_warehouse_pie.py`, `Scripts/analyze_warehouse_profile.py`, `Scripts/verify_support_snapshot_pie.py` | 하중/재고는 `WarehouseDamageSystem`·`WarehouseComputer`·`WarehouseInboundZone`, 계획은 `WarehouseForkliftAI`, 측정값과 실행 기준은 `PHYSICS.md` |
| 트럭 3대 동시 접안·도크 확대 | `Scripts/configure_multi_truck_docks.py`, `Scripts/verify_multi_truck_docks.py` | 기존 트럭 내부·외부 설치는 `WAREHOUSE.md`, 자동 트럭 교대는 미구현 계획 |
| 배송편 명세·출고 실제 인계·한 묶음씩 트럭 상차/입고 하역·출발 | `WarehouseShippingSystem.h/.cpp`, `WarehouseLogisticsTruck.h/.cpp`, `Scripts/configure_shipping_system.py`, `Scripts/verify_shipping_workflow_pie.py`, `Scripts/verify_shipping_saved_map_pie.py` | 관제 UI는 `WarehouseComputerUI.cpp`, 기존 운반 검증·지시는 `WarehouseComputer`; 운영 기준과 남은 계획은 `TRUCK_LOGISTICS_DESIGN.md`, 화면 사용은 `WAREHOUSE.md` |
| 현재 이동 알고리즘 도식·실제 산업 AGV/FMS 비교 | `FLEET_ALGORITHM_COMPARISON.md` | 구현은 `WarehouseForkliftAI`, `WarehouseFleetCoordinator`, 실행 검증은 `AUTONOMY.md` |
| 중앙 관제 통로 예약·우회·후퇴·양보·구간 폐쇄·운반 시간 측정 | `WarehouseFleetCoordinator.h/.cpp`, `WarehouseForkliftAI.cpp`, `WarehouseForkliftRecovery.cpp`, `Scripts/configure_fleet_corridors.py`, `Scripts/verify_fleet_workflow_pie.py`, `Scripts/verify_fleet_traffic_pie.py` | 물리 주행은 `WarehouseForkliftPhysics.cpp`, 중앙 관제 설계·검증은 `AUTONOMY.md` |
| 입구 관제 컴퓨터·새 대시보드/레거시·E 접속/복귀·실시간 재고·작업 지시 | `WarehouseComputer.h/.cpp`, `WarehouseComputerUI.cpp`, `WarehouseComputerDashboard.cpp`, `WarehouseComputerUILegacy.cpp`, `Scripts/verify_computer_dashboard_pie.py`, `Scripts/configure_warehouse_computer.py`, `Scripts/verify_warehouse_computer_pie.py` | 화면/입력 연결은 `msc_vrPlayerController`·`msc_vrCharacter`, 실제 지시는 `WarehouseForkliftAI.cpp`의 `SubmitJob`, 접촉 재고는 `WarehouseDamageSystem::GetSupportedActors`, 사용·한계는 `WAREHOUSE.md` |
| 작은 박스·최소 크기 유지, 큰 박스 상한 확대 | `Scripts/expand_large_cargo.py`, `Scripts/verify_large_cargo_pie.py`, `WarehouseCargo.cpp`의 수량별 상한 | `CARGO_RULES.md`, 적용 전후 기록 `Saved/Testing/LargeCargoExpansion.json`; 전체 물품 재생성은 `configure_cargo_variety.py` |
| 구석의 색상별1000kg 중량 시험 상자4개·2단 적재 | `Scripts/configure_heavy_test_cargo.py`, `Scripts/verify_heavy_test_cargo_pie.py` | 맵·재생성·실제 검증 범위는 `WAREHOUSE.md`, `ExceptionCargo` 보존은 `configure_cargo_variety.py`, 파생 Fab 재질은 `Content/Warehouse/Cargo/BoxesPalletsPack/HeavyTest/Materials/` (Git 제외) |
| 구역 위치 한글 UI·층별 표시·메뉴/이름표/안내의 공통 스타일 | `WarehouseAreaLabel.h/.cpp`, `WarehouseUIStyle.h`, `Scripts/configure_area_labels.py`, `Scripts/verify_area_labels_pie.py` | 작업자 이름표는 `WarehouseWorker.h/.cpp`, HUD·설정·비상 안내는 `msc_vrPlayerController.cpp`, 배치와 사용 규칙은 `WAREHOUSE.md` |
| 원격 지게차 후방 TPS·마우스 둘러보기·카메라 벽 충돌·1인칭 복귀·하단 조작 안내 | `msc_vrCameraManager.h/.cpp`의 `UpdateViewTarget`, `msc_vrCharacter.cpp`의 원격 연결/해제·`DoAim`, `msc_vrPlayerController.cpp`의 `GetCargoReadout`/`BeginPlay`, `verify_interaction_movement_pie.py`의 `-RemoteTPSChecks` | 원격 조작은 `AUTONOMY.md`, UI 표시는 `WAREHOUSE.md`, 화면 캡처는 `-RemoteTPSCapture` |
| 지게차 E/G·안내문 인식, 저장된 입고 배치, 보행 정지·방향 전환 | `msc_vrCharacter.cpp`의 `GetInteractionTarget`, `WarehouseWorker.cpp`의 `DriveCapsule`, `Scripts/verify_interaction_movement_pie.py` | `msc_vrPlayerController.cpp`의 `GetCargoReadout`, `configure_inbound_area.py`의 액터 `modify`와 저장, `AUTONOMY.md`, `PHYSICS.md` |
| 입구 입고/출고 대기 구역·노란 선·팔레트/충전소 재배치·적재 자동 배정 | `Scripts/warehouse_staging_layout.py`, `Scripts/configure_inbound_area.py`, `Scripts/verify_shipping_staging_pie.py`, `WarehouseInboundZone.h/.cpp` | 한글 표지는 `configure_area_labels.py`, 맵은 `WAREHOUSE.md`, 예약은 `AUTONOMY.md`, 전체 운반 검사는 `verify_inbound_area_pie.py`, 경로·지지는 `WarehouseForkliftAI.cpp` |
| NPC 입고 확정·파손 가능한 랩 포장·팔레트 묶음 보관 | `WarehouseInboundZone.h/.cpp`, `WarehousePalletWrapping.cpp`, `WarehousePallet.h`, `Scripts/verify_inbound_wrap_pie.py` | 재질 생성은 `Scripts/prepare_pallet_wrap.py`, 재고 UI는 `WarehouseComputer`, 하중 경로는 `WarehouseDamageSystem`, 배치 그룹은 `WarehouseLayoutEditor`; 사용은 `WAREHOUSE.md`, 물리 가정은 `PHYSICS.md` |
| 공통 충돌·마찰·관성·실제 강체 구동 | `PHYSICS.md`, `WarehousePhysics.h/.cpp`, `WarehouseForkliftPhysics.cpp`, `WarehouseWorker.cpp`, `msc_vrCharacter.cpp` | `WarehouseDamageSystem`, `WarehouseElevator`, `Config/DefaultEngine.ini`, `Scripts/prepare_warehouse_dynamics.py`, `Scripts/verify_warehouse_contact_physics.py` |
| 실제 Chaos 물리·주행·운반·손상 검증 | `PHYSICS.md`의 검증 표, `Scripts/verify_native_forklift_motion_pie.py`, `Scripts/verify_warehouse_contact_physics.py`, `Scripts/verify_native_damage_pie.py` | 승강기 호출은 `Scripts/verify_native_elevator_pie.py`, 저장된 창고 시작 상태는 `Scripts/verify_dynamic_warehouse_startup.py`; 과거 동기식 검사는 현재 강체 검증에 쓰지 않음 |
| 점프 착지·1층 바닥 관통·앉기/일어서기 충돌체 | `msc_vrCharacter.cpp`의 `UpdatePhysicalMovement`/`DoJumpStart`, `WarehouseWorker.cpp`의 `DriveCapsule`, `Scripts/verify_human_landing_pie.py` | 실제 저장 바닥을 복사한 미저장 PIE에서 반복 점프·보행·달리기·공중 앉기 검사, 결과와 한계는 `PHYSICS.md` |
| 랙·화물 재배치와 지게차 2/3/4대 스트레스 검사 | `Scripts/verify_fleet_layout_stress_pie.py` | 실제 저장 창고의 재배치·운반 및 별도 교차·후퇴·우회·안전 정지 사례; 결과는 `Saved/Testing/FleetLayoutStress/Report.json` |
| 창고 작업자·역할 이름표·배치 | `Source/msc_vr/WarehouseWorker.h/.cpp`, `Scripts/configure_warehouse_workers.py`, `Scripts/verify_warehouse_workers.py` | `WAREHOUSE.md`의 역할별 작업자, 기본 플레이어 전신 메시 |
| 3층 복층·승강기·층간 운반 | `Scripts/configure_mezzanine.py`, `Source/msc_vr/WarehouseElevator.h/.cpp`, `WarehouseForkliftAI.cpp` | `MEZZANINE.md`, `Scripts/verify_mezzanine.py`, `Scripts/verify_mezzanine_pie.py`, `Scripts/verify_dynamic_forklift_transfer_pie.py`; 관찰 층 선택은 PlayerController |
| 팀원 범용 승강기 모델·4층 호출대·문닫힘·물리 문턱 | `ELEVATOR_MODEL.md`, `WarehouseElevator.h/.cpp`, `Scripts/apply_cargo_elevator.py`, `Scripts/extend_cargo_elevator_fourth_floor.py` | 모델은 `Art/Equipment/CargoElevator`·`Content/Warehouse/Elevator`; native 운행은 `verify_cargo_elevator_pie.py`, 문틀/콜스탠드 추가·삭제·층수 검사는 `verify_elevator_model_layout_pie.py` |
| 범용/화물용 승강기 구분·팔레트 한 개 전용 타워 모델·E 호출/발송 | `MEZZANINE.md`, `WarehousePalletLift.h/.cpp`, `Scripts/build_pallet_lift.py`, `Scripts/apply_pallet_lift.py`, `Art/Equipment/PalletLift/` | `Content/Warehouse/PalletLift/`, `Scripts/verify_native_pallet_lift_pie.py`; 배치 공용 개구부는 `WarehouseElevatorLayout`, 기존 AGV 운반은 `WarehouseElevator`만 사용 |
| 창고 배치·랙·화물·조명 | `WAREHOUSE.md`, `Scripts/build_first_person_warehouse.py`, `Content/FirstPerson/Lvl_FirstPerson.umap` | `Content/__ExternalActors__/FirstPerson/Lvl_FirstPerson/` (엔진이 생성한 액터 데이터) |
| 빈 랙·구석 빈 팔레트 더미·손으로 팔레트 쌓기 | `Scripts/configure_empty_storage.py`, `msc_vrCharacter.cpp`의 `FindPlacement`/`TryPlaceOnPallet`, `Scripts/verify_empty_storage_pie.py` | `Scripts/configure_warehouse_strength.py`, `WAREHOUSE.md`의 양손 운반과 적재; 짐 운반의 균형은 `WarehouseWorker.cpp`의 `DriveCapsule`과 `PHYSICS.md` |
| 전 층 공통 팔레트 보관 랙·단일 추가 원형·피킹 추후 계획 | `Scripts/configure_unified_racks.py`, `WarehouseLayoutInventory::CapturePrototypes`, `Scripts/verify_layout_inventory_pie.py` | 주랙 프레임8부품·안전 데크2개와 하중은 `configure_warehouse_strength.py`; 기존 주소·그룹과 화물 보존, 재생성은 `apply_real_world_scale.py`·`configure_empty_storage.py`; 사용·검증은 `WAREHOUSE.md` |
| 지게차 기준 실측 크기·공통 보관 랙·통로·팔레트·트럭 | `Scripts/apply_real_world_scale.py`, `Scripts/verify_real_world_scale.py`, `WAREHOUSE.md`의 실제 크기 기준 | `VNSL14_SPEC.md`, 창고/외부 에셋 배치 스크립트 |
| 트럭 후면 개방·적재함 내부·택배 적재 | `Scripts/configure_truck_interior.py`, `Scripts/verify_truck_interior_pie.py`, `WAREHOUSE.md`의 트럭 내부 절 | 맵의 `TRK_` 액터, `Content/Warehouse/Exterior/TruckInterior/` (Fab 파생 메시, Git 제외); 큰 지게차 안내는 `msc_vrPlayerController.cpp` |
| 건물 외관·트럭 진입로·하역장 | `WAREHOUSE.md`의 외부 에셋 선정/설치 상태, `Scripts/build_warehouse_exterior.py`, `Scripts/verify_warehouse_exterior.py` | 맵의 `EXT_` 액터, `Content/Warehouse/Exterior/Materials/` |
| 밝기·자동 노출 | `Config/DefaultEngine.ini`의 AutoExposure, 맵 생성 스크립트의 작업등 | 인게임 카메라·후처리 설정 |
| 배치·충돌 검증 | `Scripts/verify_first_person_warehouse.py`, `Scripts/verify_forklift_training.py` | 실제 에디터의 충돌 뷰/플레이 테스트 |
| 화물 품목·밀도/부피별 무게·상자 크기·향후 트럭 입고 규칙 | `CARGO_RULES.md`, `Source/msc_vr/WarehouseCargo.h/.cpp`, `Scripts/configure_cargo_variety.py`, `Scripts/verify_cargo_rules.py` | `Scripts/import_boxes_pallets_pack.py`, `Content/Warehouse/Cargo/BoxesPalletsPack/` (Git 제외), `SourceAssets/Cargo/BoxesPalletsPack/` (Git 제외), `Scripts/download_cargo_assets.ps1`, `Scripts/import_cargo_assets.py`, `Content/Warehouse/Cargo/PolyHaven/`, `SourceAssets/Cargo/PolyHaven/`, `Scripts/verify_cargo_variety_pie.py`; 화면 표시는 PlayerController, 하중은 DamageSystem |
| 상자·크레이트·빈 팔레트 집기·단단한 손 제어·막힘/지연 취소 | `Source/msc_vr/msc_vrCharacter.h/.cpp`, `Scripts/verify_carry_response_pie.py`, `Source/msc_vr/WarehouseCargo.h/.cpp`, `WarehousePallet.h/.cpp` | `WAREHOUSE.md`의 양손 운반, 계수는 `PHYSICS.md`; `Scripts/verify_carry_regression_suite.py`는 상자·팔레트·보행/손 모션 검사를 한 에디터에서 연속 실행 |
| 온전한 상자·낙하 안정성·팔레트 동적 물리 | `Scripts/prepare_intact_cargo_physics.py`, `Scripts/apply_intact_cargo_physics.py`, `Scripts/verify_cargo_pallet_physics_pie.py` | `WarehouseCargo.cpp`의 `ConfigureCarryPhysics`, `msc_vrCharacter.cpp`의 `DropCargo`, `WarehousePallet.cpp`; 로컬 파생 메시 `SM_Carton_Intact_1/2`, `SM_Pallet_110` |
| 캐릭터 미끄러짐·보행 애니메이션·제동 | `msc_vrCharacter.cpp`의 `UpdatePhysicalMovement`, `WarehouseWorker.cpp`의 `DriveCapsule`, `WarehouseLocomotionAnimInstance.h/.cpp`, `Scripts/verify_character_drive_response_pie.py` | `PHYSICS.md`, 모션 설치는 `TEAM_ASSETS.md`, 1인칭 복사는 `Content/FirstPerson/Anims/` |
| Game Animation Sample 모션 교체·리타게팅·방향/속도별 보행·앉기·점프/착지 | `Scripts/import_game_animation_sample.py`, `Scripts/configure_game_animation_sample.py`, `WarehouseLocomotionAnimInstance.h/.cpp`, `Content/Warehouse/AnimationSample/` (로컬 재생성) | `TEAM_ASSETS.md`의 설치 순서, `WAREHOUSE.md`의 적용 범위, 기존 물리 구동은 `msc_vrCharacter`/`WarehouseWorker`, 운반·스마트폰은 `WarehouseCarryAnimInstance`, `verify_character_drive_response_pie.py -SampleAnimationChecksOnly` |
| 양손 운반 자세·걷기 연결·뒤통수 가림·외부 시점 손 접촉 | `Source/msc_vr/WarehouseCarryAnimInstance.h/.cpp`, `msc_vrCharacter.cpp`의 `InitializeCarryPresentation`/`InitializeCarryMesh`/`UpdateCarryPose` | `Scripts/verify_view_and_wheel_fixes_pie.py`, `Scripts/verify_character_drive_response_pie.py`, `Scripts/verify_two_hand_carry.py`, `Content/FirstPerson/Anims/`; 외부 운반/스마트폰은 `RemoteOperatorWorldMesh`를 공유 |
| 지게차 가속·제동·자리 회전·1.8m/s·수동 입력 지연/주기적 정지 | `WarehouseForkliftPhysics.cpp`, `WarehouseForkliftRemote.cpp`, `Scripts/verify_remote_drive_continuity_pie.py` | `msc_vrCharacter::UpdateRemoteControl`, `apply_forklift_drive_tuning.py`, 기존 보행/운전자 외형은 `verify_character_drive_response_pie.py`, `VNSL14_SPEC.md`, `PHYSICS.md` |
| 운반 물체 반투명·시야 확보 | `msc_vrCharacter.cpp`의 `TryPickupCargo`/`DropCargo`, `msc_vrCharacter.h`의 `CarryOpacity` | `Scripts/prepare_carry_material.py`, `Content/Warehouse/Materials/M_CarryTransparent.uasset`, `Scripts/verify_two_hand_carry.py` |
| 차량별 운반 대상 외곽선·남은 경로/승강기 진입·하차 바닥 화살표 | `WarehouseTransportVisualizer.h/.cpp`, `WarehouseForklift::GetDisplayRoute`, `msc_vrPlayerController::PlayerTick` | `Scripts/prepare_transport_outline.py`, `Scripts/verify_transport_visuals_pie.py`, `Scripts/verify_elevator_route_visuals_pie.py`, `Content/Warehouse/Materials/M_TransportOutline.uasset`, `M_RouteArrow.uasset`; 표시 규칙은 `WAREHOUSE.md` |
| 자율 작업·자체 점검·경로·팔레트 인식·작업 큐/보고 | `AUTONOMY.md`, `Source/msc_vr/WarehouseForkliftAI.cpp`, `WarehouseAutonomyTypes.h`, `Scripts/configure_forklift_autonomy.py` | `Scripts/verify_forklift_autonomy.py`, `Scripts/verify_forklift_autonomy_pie.py`, `WarehouseForklift.h/.cpp` |
| 자율 운행 1초 장애물 확인·수동에서는 탐색/예측 정지 제외 | `WarehouseForklift.cpp`의 `RefreshObstacleChecks`/`ClearToMove`, `WarehouseForkliftAI.cpp`의 `FollowRoute`, `WarehouseForkliftRemote.cpp`의 `RemotePoseClear` | `AUTONOMY.md`, `Scripts/verify_obstacle_interval_pie.py`는 `verify_remote_drive_continuity_pie.py`의 호환 진입점; 실제 물리 충돌은 `WarehousePhysics`/`WarehouseForkliftPhysics` |
| 포크 끼임·후진 재삽입·접촉 탈출·우회 | `WarehouseForkliftRecovery.cpp`, `WarehouseForkliftAI.cpp`, `WarehouseForklift.cpp`의 `StopFor`/`ClearToMove`, `Scripts/verify_forklift_recovery_pie.py` | `AUTONOMY.md`, 구동력은 `WarehouseForkliftPhysics.cpp`와 `PHYSICS.md` |
| 지게차 끼임 비상 UI·경고음 | `WarehouseForkliftRecovery.cpp`의 `RecoveryFault`, `msc_vrPlayerController.cpp`의 `UpdateEmergencyAlerts`/`PlayEmergencyAlarm` | `AUTONOMY.md`, `Scripts/verify_forklift_recovery_pie.py`의 `-RecoveryCapture` |
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
| 센서·고정 건물 지도·관측 정보 경계·1초 검사·원격 분리 | `AUTONOMY.md`, `WarehouseForkliftPerception.cpp`, `Agv/AgvShadowSensors.h/.cpp`, `Scripts/verify_forklift_sensors_pie.py` | 지도 태그·독립 시험 차량의 운용 맵 제외는 `Scripts/configure_sensor_structure_tags.py`, 경로 연결은 `WarehouseForkliftAI`, 원격은 `WarehouseForkliftRemote`. 별도 타이어/주행 AI는 연결하지 않음 |
| 달리기·앉기·팔레트에 내려놓기·쌓인 상자 중력 | `Source/msc_vr/msc_vrCharacter.h/.cpp`, `WarehouseCargo.h/.cpp`, `Scripts/verify_warehouse_interaction_pie.py` | `WAREHOUSE.md`의 플레이어 조작, 기존 운반/파손 검사 |
| 설정 메뉴·전지적 관찰 카메라·지붕 숨김·F1 와이어프레임 충돌 | `Source/msc_vr/msc_vrPlayerController.h/.cpp`, `Config/DefaultInput.ini`의 `DebugExecBindings`, `Scripts/configure_observer_view.py` | `Scripts/verify_view_and_wheel_fixes_pie.py`, `Scripts/verify_observer_view_pie.py`, `Scripts/verify_warehouse_interaction_pie.py`, `Saved/Config/WindowsEditor/Game.ini` (로컬 설정) |
| 전지적 카메라 앞 가까운 벽 페이드·원본 재질 복원 | `WarehouseObserverWallFade.h/.cpp`, `msc_vrPlayerController::PlayerTick`, `Scripts/prepare_observer_wall_fade.py`, `Scripts/verify_observer_wall_fade_pie.py` | 대상 `ObserverFadeWall` 태그도 준비 스크립트가 저장. 로컬 팔레트·파생 재질은 `Content/Warehouse/Materials/ObserverWalls/`, 설치는 `TEAM_ASSETS.md`, 동작은 `WAREHOUSE.md` |
| 1인칭 조작·카메라 | `Source/msc_vr/msc_vrCharacter.*`, `msc_vrPlayerController.*`, `msc_vrCameraManager.*` | `Content/FirstPerson/Blueprints/`, `Content/Input/`, `Config/DefaultInput.ini` |
| 시작 맵·게임 모드 | `Config/DefaultEngine.ini`, `Source/msc_vr/msc_vrGameMode.*`, `Content/FirstPerson/Blueprints/` | `msc_vr.uproject` |
| Fab 창고 에셋·팀원 설치 | `TEAM_ASSETS.md` (필수 5개·설치·목록 누락 해결·로컬 재생성), `WAREHOUSE.md`, `Scripts/prepare_warehouse_assets.py`, `.gitignore` | `Content/Scene_Warehouse/` 원본 팩 |
| Fab 트럭·나무·관목 설치·배치 | `WAREHOUSE.md`의 외부 에셋 절, `Scripts/apply_exterior_fab_assets.py`, `Scripts/prepare_warehouse_assets.py` | `Content/VehicleVarietyPack/`, `Content/EuropeanBeech/`, `Content/MSPresets/`, `Content/GV_FreeShrubsPack/` (Git 제외 원본 팩) |
| 구조 문서 갱신 | `AGENTS.md`, `Scripts/update_structure_summary.ps1`, 이 문서 | 없음 |
| Unreal MCP 연결·공식 도구 조회·자동 시작 복구 | `MCP_SETUP.md`, `.codex/config.toml`, `msc_vr.uproject`, `Config/DefaultEditorPerProjectUserSettings.ini` | 연결 검사는 `Scripts/verify_mcp.py`; 에셋 설치는 `TEAM_ASSETS.md` |

`Scripts/build_first_person_warehouse.py`를 다시 실행하면 `WH_` 액터를 재생성하고 기존 템플릿 장애물을 제거하며 `EXT_` 외부 환경은 유지한다. `Scripts/build_warehouse_exterior.py`는 `EXT_` 액터만 재생성한다. 다른 작업자의 레벨 변경이 있으면 실행 전 확인한다. 바이너리 `.umap`/`.uasset`과 World Partition 외부 액터 파일은 텍스트처럼 직접 편집하지 않는다.

## 현재 경로 구조

아래 구역은 `AGENTS.md`의 PowerShell 명령으로 갱신한다. 대용량 에셋 파일과 엔진 생성 캐시는 개별 열거하지 않는다.

<!-- STRUCTURE:START -->
```text
msc_vr/
  AGENTS.md
  AUTONOMY.md
  CARGO_RULES.md
  CHARGER_MODEL.md
  COLLISION_POLICY.md
  ELEVATOR_MODEL.md
  FLEET_ALGORITHM_COMPARISON.md
  MCP_SETUP.md
  MEZZANINE.md
  msc_vr.uproject
  PHYSICS.md
  PROJECT_STRUCTURE.md
  README.md
  REFINED_AGV.md
  SIMULATION_ROADMAP.md
  TEAM_ASSETS.md
  TRUCK_LOGISTICS_DESIGN.md
  VNSL14_SPEC.md
  WAREHOUSE.md
  WAREHOUSE_SERVICES_MODEL.md
  Config/
    DefaultEditor.ini
    DefaultEditorPerProjectUserSettings.ini
    DefaultEngine.ini
    DefaultGame.ini
    DefaultInput.ini
  Scripts/
    analyze_warehouse_profile.py
    apply_agv_charger.py
    apply_cargo_elevator.py
    apply_exterior_fab_assets.py
    apply_forklift_drive_tuning.py
    apply_intact_cargo_physics.py
    apply_orange_agv.py
    apply_pallet_lift.py
    apply_real_world_scale.py
    apply_refined_agv.py
    apply_visual_fixes.py
    apply_warehouse_services.py
    build_agv_charger.py
    build_agv_motion_test.py
    build_cargo_elevator.py
    build_cat_accessory.py
    build_column_protectors.py
    build_conveyor_model.py
    build_first_person_warehouse.py
    build_orange_agv.py
    build_pallet_lift.py
    build_warehouse_exterior.py
    build_warehouse_services.py
    capture_dense_warehouse_services.py
    capture_warehouse_services.py
    configure_area_labels.py
    configure_cargo_variety.py
    configure_carryable_cargo.py
    configure_charger_start_positions.py
    configure_dual_forklifts.py
    configure_empty_storage.py
    configure_fleet_corridors.py
    configure_forklift_autonomy.py
    configure_game_animation_sample.py
    configure_heavy_test_cargo.py
    configure_inbound_area.py
    configure_mezzanine.py
    configure_multi_truck_docks.py
    configure_observer_view.py
    configure_sensor_structure_tags.py
    configure_shipping_system.py
    configure_truck_interior.py
    configure_unified_racks.py
    configure_warehouse_computer.py
    configure_warehouse_layout.py
    configure_warehouse_services_collision.py
    configure_warehouse_strength.py
    configure_warehouse_waiting_bays.py
    configure_warehouse_workers.py
    convert_agv_original5_cm.py
    convert_agv_refined_cm.py
    download_cargo_assets.ps1
    expand_large_cargo.py
    export_agv_charger.py
    export_agv_counterbalance.py
    export_agv_refined.py
    export_cargo_elevator.py
    export_orange_agv_unreal.py
    export_warehouse_services.py
    extend_cargo_elevator_fourth_floor.py
    import_agv_original5.py
    import_agv_refined.py
    import_boxes_pallets_pack.py
    import_cargo_assets.py
    import_game_animation_sample.py
    install_warehouse_transport_lab.py
    place_agv_in_warehouse.py
    prepare_carry_material.py
    prepare_intact_cargo_physics.py
    prepare_observer_wall_fade.py
    prepare_orange_agv.py
    prepare_orange_agv_unreal.py
    prepare_original_body.py
    prepare_pallet_wrap.py
    prepare_refined_agv.py
    prepare_remote_character_fade.py
    prepare_transport_outline.py
    prepare_warehouse_assets.py
    prepare_warehouse_dynamics.py
    preview_warehouse_services.py
    profile_warehouse_pie.py
    refine_agv_original_proportions.py
    refine_cargo_elevator_call_stands.py
    refine_warehouse_services_layout.py
    render_cargo_elevator.py
    restore_front_wheel_mounts.py
    revise_agv_counterbalance.py
    update_structure_summary.ps1
    verify_agv_dynamics.py
    verify_agv_hazards.py
    verify_agv_localization.py
    verify_agv_navigation.py
    verify_agv_safety.py
    verify_agv_safety_pie.py
    verify_agv_warehouse.py
    verify_agv_warehouse_pie.py
    verify_area_labels_pie.py
    verify_building_lift_four_floors_pie.py
    verify_building_saved_map_pie.py
    verify_building_shaft_layout_pie.py
    verify_cargo_elevator_pie.py
    verify_cargo_pallet_physics_pie.py
    verify_cargo_rules.py
    verify_cargo_variety_pie.py
    verify_carry_regression_suite.py
    verify_carry_response_pie.py
    verify_character_drive_response_pie.py
    verify_collision_policy_pie.py
    verify_computer_dashboard_pie.py
    verify_construction_editor_pie.py
    verify_conveyor_layout_pie.py
    verify_conveyor_transport_pie.py
    verify_conveyor_turntable_pie.py
    verify_dual_forklifts_remote_pie.py
    verify_dynamic_forklift_transfer_pie.py
    verify_dynamic_warehouse_startup.py
    verify_elevator_model_layout_pie.py
    verify_elevator_route_visuals_pie.py
    verify_empty_storage_pie.py
    verify_first_person_warehouse.py
    verify_fleet_layout_stress_pie.py
    verify_fleet_traffic_pie.py
    verify_fleet_workflow_pie.py
    verify_forklift_autonomy.py
    verify_forklift_autonomy_pie.py
    verify_forklift_recovery_pie.py
    verify_forklift_sensors_pie.py
    verify_forklift_training.py
    verify_heavy_test_cargo_pie.py
    verify_human_landing_pie.py
    verify_inbound_area_pie.py
    verify_inbound_wrap_pie.py
    verify_interaction_movement_pie.py
    verify_large_cargo_pie.py
    verify_layout_editing_ui_pie.py
    verify_layout_editor_pie.py
    verify_layout_inventory_pie.py
    verify_mcp.py
    verify_mezzanine.py
    verify_mezzanine_pie.py
    verify_multi_truck_docks.py
    verify_native_charger_layout_pie.py
    verify_native_damage_pie.py
    verify_native_elevator_pie.py
    verify_native_forklift_motion_pie.py
    verify_native_pallet_lift_pie.py
    verify_observer_view_pie.py
    verify_observer_wall_fade_pie.py
    verify_obstacle_interval_pie.py
    verify_orange_agv_export.py
    verify_real_world_scale.py
    verify_refined_agv.py
    verify_refined_agv_pie.py
    verify_remote_drive_continuity_pie.py
    verify_shared_charging_pie.py
    verify_shipping_saved_map_pie.py
    verify_shipping_staging_pie.py
    verify_shipping_workflow_pie.py
    verify_simulation_speed_pie.py
    verify_support_snapshot_pie.py
    verify_transport_visuals_pie.py
    verify_truck_interior_pie.py
    verify_two_hand_carry.py
    verify_view_and_wheel_fixes_pie.py
    verify_warehouse_computer_pie.py
    verify_warehouse_contact_physics.py
    verify_warehouse_experiment_building.py
    verify_warehouse_experiment_guards.py
    verify_warehouse_experiment_transport.py
    verify_warehouse_experiment_ui.py
    verify_warehouse_experiments_pie.py
    verify_warehouse_exterior.py
    verify_warehouse_interaction_pie.py
    verify_warehouse_strength.py
    verify_warehouse_strength_pie.py
    verify_warehouse_workers.py
    warehouse_staging_layout.py
    wrap_orange_agv_labels.py
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
      WarehouseAreaLabel.cpp
      WarehouseAreaLabel.h
      WarehouseAutonomyTypes.h
      WarehouseBuildingLayout.cpp
      WarehouseBuildingLayout.h
      WarehouseCargo.cpp
      WarehouseCargo.h
      WarehouseCarryAnimInstance.cpp
      WarehouseCarryAnimInstance.h
      WarehouseChargingStation.cpp
      WarehouseChargingStation.h
      WarehouseComputer.cpp
      WarehouseComputer.h
      WarehouseComputerDashboard.cpp
      WarehouseComputerDashboard.h
      WarehouseComputerUI.cpp
      WarehouseComputerUILegacy.cpp
      WarehouseConstructionEditor.cpp
      WarehouseConstructionEditor.h
      WarehouseConveyor.cpp
      WarehouseConveyor.h
      WarehouseDamageSystem.cpp
      WarehouseDamageSystem.h
      WarehouseElevator.cpp
      WarehouseElevator.h
      WarehouseElevatorLayout.cpp
      WarehouseElevatorLayout.h
      WarehouseExperimentMetrics.cpp
      WarehouseExperimentMetrics.h
      WarehouseExperimentStorage.cpp
      WarehouseExperimentStorage.h
      WarehouseExperimentSubsystem.cpp
      WarehouseExperimentSubsystem.h
      WarehouseExperimentUI.cpp
      WarehouseExperimentUI.h
      WarehouseFleetCoordinator.cpp
      WarehouseFleetCoordinator.h
      WarehouseForklift.cpp
      WarehouseForklift.h
      WarehouseForkliftAI.cpp
      WarehouseForkliftParking.cpp
      WarehouseForkliftPerception.cpp
      WarehouseForkliftPhysics.cpp
      WarehouseForkliftRecovery.cpp
      WarehouseForkliftRemote.cpp
      WarehouseInboundZone.cpp
      WarehouseInboundZone.h
      WarehouseLayoutEditor.cpp
      WarehouseLayoutEditor.h
      WarehouseLayoutInventory.cpp
      WarehouseLayoutInventory.h
      WarehouseLayoutSnap.cpp
      WarehouseLayoutSnap.h
      WarehouseLocomotionAnimInstance.cpp
      WarehouseLocomotionAnimInstance.h
      WarehouseLogisticsTruck.cpp
      WarehouseLogisticsTruck.h
      WarehouseObserverWallFade.cpp
      WarehouseObserverWallFade.h
      WarehousePallet.cpp
      WarehousePallet.h
      WarehousePalletLift.cpp
      WarehousePalletLift.h
      WarehousePalletWrapping.cpp
      WarehousePerformance.h
      WarehousePhysics.cpp
      WarehousePhysics.h
      WarehouseShippingSystem.cpp
      WarehouseShippingSystem.h
      WarehouseSimulationClock.cpp
      WarehouseSimulationClock.h
      WarehouseTransportVisualizer.cpp
      WarehouseTransportVisualizer.h
      WarehouseUIStyle.h
      WarehouseWaitingBay.cpp
      WarehouseWaitingBay.h
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
    __ExperimentTests/
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
    Python/
      __pycache__/
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
      AnimationSample/
      Cargo/
      ChargingStation/
      Collision/
      Conveyor/
      Elevator/
      Exterior/
      Maps/
      Materials/
      PalletLift/
      Physics/
      Services/
    Weapons/
      GrenadeLauncher/
      Pistol/
      Rifle/
  Art/
  Build/
  SourceAssets/
```
<!-- STRUCTURE:END -->

`Content/Scene_Warehouse/`는 Fab 팩이며 Git에서 제외된다. 다른 팀원은 동일 경로에 설치해야 한다. `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `.vs/`는 생성물/캐시이므로 보통 탐색 대상이 아니다. `Variant_Horror`, `Variant_Shooter`, `Characters`, `Weapons`, `LevelPrototyping`은 현재 창고 기능의 핵심이 아닌 템플릿 콘텐츠다.
