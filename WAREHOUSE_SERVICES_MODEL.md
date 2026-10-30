# 창고 설비 모듈 모델

2026-10-14 Blender MCP로 만든 설비 26종과 추가 보호대 3종이다. [조사 구상](Art/Planning/WarehouseServices/CONCEPT.md)을 바탕으로 모델링한 뒤, 사용자 요청에 따라 Unreal 기본 맵에 배치했다. 현재 맵과 층별 관찰 기준은 [WAREHOUSE.md](WAREHOUSE.md)를 따른다.

## 파일

- Blender: `Art/Environment/WarehouseServices/warehouse_services.blend`.
- 사진: 같은 폴더의 `services_assembly.png`, `services_detail.png`, `services_catalog.png`.
- 부품별 FBX 26개: 같은 폴더의 `FBX/`.
- 치수·연결 지점·재질·검증: 같은 폴더의 `modules.json`.
- 전달용: 같은 폴더의 `Delivery/warehouse_services_fbx.zip`.
- Unreal 메쉬·재질: `Content/Warehouse/Services/`.
- 맵 배치/검증: `Scripts/apply_warehouse_services.py`, `Scripts/capture_warehouse_services.py`, `Saved/Testing/WarehouseServices/Report.json`.

건물 크기를 게임에서 확정할 때 사용하는 연결형 자동 배치는 `WarehouseBuildingLayout`이 이 모듈을 다시 배치한다. 사용 규칙·검증 결과는 [건물·복층 편집](WAREHOUSE.md#타일-방식-건물복층-편집), 실제 사용 에셋 목록은 [RUNTIME_ASSETS.md](RUNTIME_ASSETS.md)를 따른다. 원본 모델을 다시 생성하거나 저장된 기본 맵을 덮어쓰는 기능은 아니다.

`Warehouse Services - Library` 씬에 원점·단위 스케일을 정리한 원본 메쉬가 있다. 이 씬의 부품은 모두 로컬 원점에서 시작하므로 서로 겹쳐 보인다. 개별 부품 확인은 `Warehouse Services - Module Catalog`, 조립 형태는 `Warehouse Services - Assembly Preview` 씬에서 본다. 카탈로그는 모양을 보기 위해 부품별 크기를 달리 표시한 장면이며 같은 축척의 도면이 아니다.

## 모듈

| 종류 | 부품 |
| --- | --- |
| 철골 3종 | 4m I형 보, 볼트 접합판, 2m 가새 |
| 트레이/고정부 8종 | 2m 직선, 90도 엘보, T 분기, 폭400→200mm 리듀서, 끝막이, 2m 케이블 다발, 트래피즈 행거, 벽 브래킷 |
| 전기 6종 | 1m 전선관, 90도 엘보, 접속함, 클램프, 분전함, 힌지 기준 분전함 문 |
| 소방 5종 | 2m 직선 배관, 90도 엘보, T 분기, 하향 스프링클러 헤드, 링 행거 |
| 조명 1종 | 1.2m LED 선형 조명 |
| 환기 3종 | 1.5m 사각 덕트, 90도 엘보, 그릴 |

대부분의 직선 모듈은 로컬 +X 방향으로 이어지며 Z가 위쪽이다. 원점은 연결 입구 또는 부품 장착 기준이다. 분전함 전면은 -Y, 클램프의 관 축은 Y다. 분전함 문 원점은 왼쪽 힌지다. 직선/엘보/T 분기의 cm 연결 좌표는 manifest를 참고한다. 모든 FBX는 개별 부품의 원점에서 저장했다.

## 충전기 자동 배선 (2026-10-28)

`WarehouseChargerWiring`은 충전기 본체와 현재 존재하는 배전함 메시의 중심 거리로 가장 가까운 배전함을 선택한다. 기존 `SM_WS_DistributionCabinet`과 기존 충전기 모델을 사용하며, 전선관은 엔진 원통과 자체 흑연색 도장 재질로 생성한다. 외부 에셋을 추가하지 않는다.

**2026-10-29 사용자 변경:** 바닥 약2.1cm 높이를 따라 연결하며 충전기·배전함 끝부분만 수직으로 연결한다. 긴 구간은 바닥 지지를 확인하고 공중 가로선으로 대체하지 않는다. 파생 전선은 충돌·센서 장애물·고정 장애물 지도에서 제외한다. 전기 용량·차단기·케이블 처짐·열·구조 강도는 계산하지 않는다.

게임 시작·충전기 추가/삭제·재배치 확정/취소·건물 변경·배치 복원 때만 다시 계산한다. 관찰 화면에서 층이나 벽을 숨기는 표시는 실제 연결을 끊지 않는다. 전선은 충전기와 분리된 시각 전용 파생 액터다. 일반 배치 목록·초기 배치 저장에서 제외하고 최종 설비 위치로 재생성한다. 충돌 기준은 [COLLISION_POLICY.md](COLLISION_POLICY.md), 사용법은 [WAREHOUSE.md](WAREHOUSE.md)의 충전기 재배치 절을 따른다.

건물 확정으로 기존 벽을 교체할 때는 북쪽·양옆 벽의 실제 안쪽 면에 배전함과 문도 다시 배치한다. 약18m 간격 후보를 사용하며 해당 층에 실제 접근 바닥이 있고 기둥·승강기·물류와 겹치지 않는 곳만 채택한다. 연결 대상이 없거나 안전한 경로가 없으면 연결 상태에 이유를 남기고 기존 충전 전력 설정은 유지한다.

### 자동 배선 검증

**2026-10-29 현재 규칙 검증:** `verify_floor_wiring_workers_pie.py`가 실제 기본 맵의 충전기2대에 대해 바닥 높이 경로·끝단 수직 연결·전 구간 NoCollision·고정 지도 제외를 확인했다. 같은 검사에서 작업자7명 외부 배치·충돌 유지·멱등성과 실내로 옮긴 한 명의 외부 재배치를 확인했다. 결과는 `Saved/Testing/FloorWiringWorkers/Report.json`이다. `relocate_warehouse_workers_outside.py`로 저장 맵의 실내6명을 옮겼으며 이미 외부에 있던 한 명을 포함해 총7명을 유지했다. 아래2026-10-28 검증은 상부 물리 전선관을 사용하던 이전 구현의 기록이다.

2026-10-28 `ChargerWiringBuild5_20261028.log`의 Development Editor 빌드와 `ChargerWiringBuild5PIE_20261028.log`의 실제 PIE 검사를 통과했다. `Scripts/verify_charger_wiring_pie.py`는 미저장 시험 장면에서 8개 항목을 확인했다: 최근접 배전함, 벽면·상부 배선과 공통 물리 분류, 유휴 상태 재탐색 없음, 재배치/취소/회전, 추가/삭제/선택 제외, 화면 숨김과 물리 비활성 구분, 배전함 누락, 건물 재생성이다. 선호 높이가 막힌 한쪽 벽에서도 다른 높이의 벽 경로를 먼저 선택하는 회귀를 포함한다. 건물을 두 번 확정해도 배전함10개·충전기 전선1개가 누적되지 않았다.

같은 스크립트의 `-ChargerWiringSavedMap` 검사는 기본 창고의 배전함12개 중 기존 충전기2대가 각각 가장 가까운 배전함에 연결되는 것을 확인했다. 기존 설비 때문에 벽을 따라갈 수 없는 구간에서는 약3.3m의 충돌 없는 상부 연결을 사용했다. 지도에 전선이 없던 상태로 캐시를 만든 후 다시 전선을 추가해도, 즉시 추가 센서 탐색을 하지 않고 다음 정상1초 갱신에 새 고정 전선이 포함되는 것을 실제 Tick으로 확인했다. 원격 조작에 센서/AI를 추가하지 않으며 최대속도·차량 구동·물리 소스는 보존했다.

보고서는 `Saved/Testing/ChargerWiring/Report_FinalBuild5.json`, `Saved/Testing/ChargerWiringSavedMap/Report_FinalBuild5.json`이다. 기존 맵·외부 액터 파일과 주행 AI/물리 소스 해시는 검사 전후 같았다. 두 장면의 실제 PNG를 검토했다. 배치 검사는 UI와 같은 native 함수를 호출한 것으로 OS 마우스 조작·전체 운반 작업·전기 부하 시뮬레이션 또는 FPS 벤치마크 결과는 아니다. 배치 저장/불러오기는 기존 저장 구조에 파생 전선을 제외하고 완료 후 재생성하는 연결을 검토했으며, 이번 검사에서 새 저장 슬롯의 왕복 복원은 별도로 실행하지 않았다.

## 제작·검증 범위

### 현재 Unreal 배치

**충돌 기준: 2026-10-16 사용자 요청으로 설비 장식도 충돌한다.** `Scripts/configure_warehouse_services_collision.py`가 기존 배치를 재생성하지 않고 `WS_` 설비1083개의 메시를 고정 삼각형 충돌(ComplexAsSimple), 컴포넌트를 BlockAll로 설정한다. 매달린 설비와 벽면 설비는 고정 구조물로 취급한다. 단순 볼록 껍질로 트레이·보호대·지지대 사이의 빈 공간을 막지 않는다. 재배치 고정 태그와 관찰용 숨김 태그는 물리 분류와 별개다. 공통 검사 기준은 [충돌 분류](COLLISION_POLICY.md)를 따른다. apply/refine 스크립트도 같은 설정 함수를 사용한다. 아래2026-10-14의 NoCollision 검증 기록은 변경 전 상태다.

2026-10-16 저장 맵 재로드·실제 낙하 접촉·PIE 공통 충돌 분류와 기존 운반 표시 회귀를 통과했다. 실제 확인 범위와 결과 파일은 [충돌 검증 기록](COLLISION_POLICY.md#추가수정-시-지침과-검증)에 기록한다. 설비 위치·크기는 보존했으며 이 검사는 구조 강도·소방 성능이나 전체 창고 FPS 측정 결과가 아니다.

같은 날 라이브러리의 미사용 부품까지 포함한 메시34종(부품29종·지지대 묶음5종)에 충돌 섹션과 삼각형 충돌을 저장했다. `LhkMergeServiceCatalog_20261016.log`에서 이전에 배치하지 않은 I형 보도 실제 일반 충돌 질의와2kg 물체의 낙하·접촉 정지를 통과했다. `Saved/Testing/WarehouseServicesCollision_20261016.json`에는 적용 수와 기존 배치 변환 보존 여부를 기록한다.

**배치 기준: 2026-10-14, 밀도·장착 보정본.** 앞선421개 배치를 교체하여 장식 액터1083개로 정리했다. 실제 천장/복층 바닥 아래 경계와 벽 안쪽 경계를 기준으로 조명·트레이·배관·덕트 지지부, 분전함 장착 위치를 계산한다. 아래421개 기록은 첫 배치 당시 범위다.

트레이/케이블 다발 각212개, 소방 직선 배관101개·헤드202개, LED65개, 벽면 분전함12곳, 벽 브래킷144개와 보호대25개를 포함한다. 트레이·조명·덕트의 매달기 봉과 앵커, 배관 U형 받침, 벽면 전선관 고정부를 추가했다. 기존 조명의24개 장착 지점에도 매달기 부품을 보완했다. 지지 부품3603개를 층/지붕별5개 메쉬로 병합해 작은 액터 수를 줄였다.

최신 스크립트는 `Scripts/refine_warehouse_services_layout.py`이며 임포트만 이전 apply 스크립트에서 재사용하고 배치는 전부 새로 계산한다. 병합된 지지 메쉬는 `Content/Warehouse/Services/SupportBatches/`에 있다. 이 스크립트도 기존 WS 액터를 재생성하므로 수동 수정한 WS 배치가 있으면 실행 전 확인한다.

설비 밀도와 마운트 검증 결과는 `Saved/Testing/WarehouseServicesDense/Report.json`, 개별 장착 지점은 `Mounts.json`이다. 벽면 분전함은 실제 메쉬 경계의 장착 발을 기준으로 벽면에0.2cm 겹치게 놓고, 천장 앵커는0.4cm 접촉 겹침으로 떠 보이는 간격을 제거했다. 이 수치는 시각적 접촉 확인이며 실제 구조/전기/소방 설계 검증은 아니다. 맵 재로드·장착 지점 확인과 캡처는 `Scripts/capture_dense_warehouse_services.py`로 수행한다. 최종 사진은 같은 결과 폴더의 GroundServices/WallServices/UpperGallery/Overview PNG다.

기존 액터 변환은 유지하고 승강기 공간을 비웠다. 새 장식의 NoCollision·관찰용 태그 기준도 유지했다. 실행 중 충전/운반 및 FPS 비교는 이번 수정에서 수행하지 않았다.

2026-10-14 별도 에디터에서 수정 맵을 재로드해 설비1083개, 메쉬 참조·NoCollision 설정, 장착 지점1164개의 원래 벽/천장 경계 일치를 확인했다. 천장 앵커 상단과 분전함 장착 발의 실제 메쉬 경계에서 확인한 접촉 겹침은 최대0.4cm였다. GroundServices/WallServices/UpperGallery/Overview를 실제 Unreal 에디터에서 촬영했다. 열려 있던 다른 에디터의 맵은 다시 열어야 변경된 디스크 배치를 표시한다.

**첫 배치 기록(같은 날짜, 최신 보정 전):**

2026-10-14 `Lvl_FirstPerson`에 `WS_` 접두사의 장식 액터 421개를 배치했다. 천장 주 트레이·케이블·행거, 소방 배관/헤드, 외곽 환기 덕트, 추가 보·접합판, 복층 아래 분기와 LED 외형, 벽면 분전함·전선관, 기둥 보호대25개다. 메쉬·재질29종을 임포트해 실제 치수를 확인했다.

실제 승강기 경계와 여유 영역을 기준으로 겹치는 배치5개를 제외했다. 기존 액터의 위치·회전·스케일은 변경하지 않고 신규 장식 액터만 저장했다. 모든 새 설비는 NoCollision이며 고무 보호대의 충격 흡수 물리는 구현하지 않았다. 기존 기둥·벽·천장과 이동 경로의 충돌은 기존 구현을 따른다.

지붕 설비에는 `ObserverRoof`, 복층 설비에는 층별 `WarehouseFloor` 태그를 부여했다. `LayoutFixed`로 배치 편집 대상에서 고정되도록 표시하고 공통 메쉬·재질, 세부 부품 그림자 제한 및 거리 컬링을 사용한다. 관찰 모드 런타임 회귀·FPS 측정은 아직 수행하지 않았다. 기존 상층 조명은 유지하고 추가 LED 메쉬는 발광 외형으로 사용한다.

적용 스크립트는 기존 `WS_` 액터만 재생성하므로 재실행 시 사용자 수정한 WS 배치를 먼저 확인한다. 에디터 캡처는 `Saved/Testing/WarehouseServices/`의 `Atrium.png`, `UpperGallery.png`, `Overview.png`다. Overview는 배치를 보이기 위해 지붕·남쪽/동쪽 벽을 일시 숨긴 컷어웨이이며 실제 저장 맵에서 제거하지 않았다. 캡처용 카메라·숨김 변경은 저장하지 않는다.

2026-10-14 별도 에디터에서 저장 맵을 다시 로드해 설비421개와 메쉬 참조·NoCollision 설정이 유지되는지 확인했고, 위 세 화면을 실제 Unreal 렌더로 촬영했다. 결과는 `Saved/Testing/WarehouseServices/Report.json`의 `saved_map_reload_verified`에 기록한다. 기존 에디터가 이미 열려 있었다면 맵을 다시 열어 디스크의 배치를 확인해야 한다.

### 추가 기둥 보호대

2026-10-14 사용자 제공 고무 코너 보호대 사진을 참고해 3종을 추가했다. `Art/Environment/WarehouseServices/ColumnProtection/`에 `column_protectors.blend`, `column_guards.png`, `FBX/`와 `modules.json`이 있다. 높이 약 90cm의 ㄱ자 코너 보호대, 평판 보호대, 45cm 사각 기둥에 맞춘 4코너 조립형이다. 둥근 고무 몸체·검정/노랑 사선 표시·세로 보강선·체결 나사를 표현했다. 코너와 평판은 기둥 모서리·패널 왼쪽 아래를 기준으로, 조립형은 기둥 중심 바닥을 기준으로 원점을 정리했다. 치수는 참고 사진 기반 모델링 선택값이며 제품 실측값이 아니다.

제작 스크립트는 `Scripts/build_column_protectors.py`다. FBX에는 고무 보호대만 들어 있으며 촬영용 콘크리트 기둥·벽·바닥은 제외했다. 추가 전달 파일은 같은 폴더의 `column_protectors_fbx.zip`이다. 같은 날짜에 FBX 3개를 Blender에 재임포트해 메쉬 수·크기·원점을 확인했고 경계 오차는 0이었다. Unreal 적용·충격 흡수 물리 검사는 수행하지 않았다.

`Scripts/build_warehouse_services.py`, `Scripts/preview_warehouse_services.py`, `Scripts/export_warehouse_services.py` 순서로 Blender MCP에서 실행한다. 완료 파일에 제작 스크립트를 다시 실행하지 않는다. 원본 메쉬와 촬영용 복사본은 구분되며 벽·바닥·기둥·조명 매달기 예시 등 촬영 세트는 부품별 FBX에서 제외한다.

Blender 단위는 m이며 FBX에는 단위 정보를 기록한다. UV를 생성했고 재질 슬롯을 유지했다. 노이즈 텍스처 없이 단색 PBR 금속·도장·고무·발광 재질을 사용한다. 엔진의 LED 발광 값은 manifest를 참고해 설정할 수 있다. 게임용 충돌·추가 LOD·엔진 라이트맵과 실제 건축/전기/소방 성능은 아직 검증하지 않았다.

2026-10-14 Blender 조립·부품 목록·상세 렌더를 검토했다. 부품별 FBX 26개를 Blender에 독립 재임포트하여 부품 수, 원점과 경계 치수를 확인했다. 수치 결과는 `modules.json`의 validation에 기록한다. 이번 결과는 실제 맵 적용이나 설비 규정 적합성 확인을 의미하지 않는다.
