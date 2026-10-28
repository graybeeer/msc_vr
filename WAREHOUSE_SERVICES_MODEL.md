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
