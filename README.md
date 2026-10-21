# 무인 지게차 교육 시뮬레이션

Unreal Engine 5.8 기반 PC 프로젝트다. 물류창고에서 무인 지게차의 운반·충돌·센서 이상·적재 실패를 재현하고 운영 조건을 비교한다.

## 시작하기

1. 팀원은 [에셋 설치 안내](TEAM_ASSETS.md)에 따라 외부 에셋을 준비한다.
2. `msc_vr.uproject`를 열고 기본 맵 `Content/FirstPerson/Lvl_FirstPerson.umap`에서 플레이한다.
3. 현재 맵과 조작은 [창고 안내](WAREHOUSE.md), 수정할 파일은 [구조 요약](PROJECT_STRUCTURE.md)을 확인한다.

## 문서 안내

| 문서 | 담당 내용 |
| --- | --- |
| [WAREHOUSE.md](WAREHOUSE.md) | 현재 맵·플레이어/배치 편집·보류 물류·관제 화면·건축/작업자 |
| [PHYSICS.md](PHYSICS.md) | 강체·접촉·사람/차량 구동·손 운반·질량·마찰·손상 가정과 실행 검증 |
| [COLLISION_POLICY.md](COLLISION_POLICY.md) | 충돌/비충돌 부품 분류·공통 검사·선택 전용 채널·분류 검증 |
| [AUTONOMY.md](AUTONOMY.md) | 작업 큐·중앙 교통 관제·시설 누락·공유 충전/일반 대기·자율 운반·G 원격 조작·센서 선별 통합과 정보 범위 |
| [FLEET_ALGORITHM_COMPARISON.md](FLEET_ALGORITHM_COMPARISON.md) | 현재 차량·중앙 이동 알고리즘 도식과 실제 산업 시스템 비교 |
| [VNSL14_SPEC.md](VNSL14_SPEC.md) | 장비 공개 사양·게임 설정·배터리 모델의 가정 |
| [REFINED_AGV.md](REFINED_AGV.md) | 현재 지게차 모델·치수 보정·기계 리그 |
| [CHARGER_MODEL.md](CHARGER_MODEL.md) | 충전기 교체용 Blender 외형·FBX·접점 배치 기준 및 검증 범위 |
| [WAREHOUSE_SERVICES_MODEL.md](WAREHOUSE_SERVICES_MODEL.md) | 천장·벽면 설비/보호대 모델·FBX·현재 맵 배치 및 실제 검증 범위 |
| [MEZZANINE.md](MEZZANINE.md) | 기본3층·실험용 최대4층 복층, 범용/화물용 승강기·층간 이동 인터록 |
| [CARGO_RULES.md](CARGO_RULES.md) | 상품 특성·상자 크기·밀도·무게 생성 규칙 |
| [TEAM_ASSETS.md](TEAM_ASSETS.md) | 팀원용 외부 에셋 설치·로컬 재생성·Git 제외 |
| [MCP_SETUP.md](MCP_SETUP.md) | Unreal·Blender·Photoshop MCP 연결 설정과 실제 연결 검증 |
| [TRUCK_LOGISTICS_DESIGN.md](TRUCK_LOGISTICS_DESIGN.md) | 팔레트 배송편·트럭 양방향 상하차·출차 연출, 검증 상태와 남은 자동 교대 계획 |
| [FORKLIFT_NAVIGATION.md](Source/msc_vr/Agv/FORKLIFT_NAVIGATION.md) | 별도 개발 중인 동역학·LiDAR·내비게이션 모듈 |
| [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) | 요청별 첫 탐색 경로와 파일 구조 |
| [AGENTS.md](AGENTS.md) | AI 작업 범위와 구조 문서 갱신 지침 |

각 문서에는 담당 영역의 현재 기준을 기록한다. 설치 절차·사양·코드 설명을 다른 문서에 반복하지 않고 위 문서를 연결한다. 과거 검증 결과는 검증 날짜와 당시 범위를 구분한다. `Agv`의 센서만 기존 `WarehouseForklift`에 선별 통합했으며, 별도 타이어 구동·주행 알고리즘은 기존 차량에 연결하지 않는다. 활성화와 검증 상태는 `AUTONOMY.md`를 따른다.
