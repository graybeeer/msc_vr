# 무인 지게차 교육 시뮬레이션

Unreal Engine 5.8 기반 PC 프로젝트다. 물류창고에서 무인 지게차의 운반·충돌·센서 이상·적재 실패를 재현하고 운영 조건을 비교한다.

## 시작하기

1. 팀원은 [에셋 설치 안내](TEAM_ASSETS.md)에 따라 외부 에셋을 준비한다.
2. `msc_vr.uproject`를 열고 기본 맵 `Content/FirstPerson/Lvl_FirstPerson.umap`에서 플레이한다.
3. 현재 맵과 조작은 [창고 안내](WAREHOUSE.md), 수정할 파일은 [구조 요약](PROJECT_STRUCTURE.md)을 확인한다.

## 문서 안내

| 문서 | 담당 내용 |
| --- | --- |
| [WAREHOUSE.md](WAREHOUSE.md) | 현재 맵·플레이어 조작·건축/작업자·물리 범위 |
| [PHYSICS.md](PHYSICS.md) | 강체·접촉·사람/차량 구동·손 운반·질량·마찰·손상 가정과 실행 검증 |
| [AUTONOMY.md](AUTONOMY.md) | 지게차 2대의 작업 큐·점검·자율 운반·G 원격 조작 |
| [VNSL14_SPEC.md](VNSL14_SPEC.md) | 장비 공개 사양·게임 설정·배터리 모델의 가정 |
| [REFINED_AGV.md](REFINED_AGV.md) | 현재 지게차 모델·치수 보정·기계 리그 |
| [MEZZANINE.md](MEZZANINE.md) | 3층 복층·승강기 규격·층간 이동 인터록 |
| [CARGO_RULES.md](CARGO_RULES.md) | 상품 특성·상자 크기·밀도·무게 생성 규칙 |
| [TEAM_ASSETS.md](TEAM_ASSETS.md) | 팀원용 외부 에셋 설치·로컬 재생성·Git 제외 |
| [TRUCK_LOGISTICS_DESIGN.md](TRUCK_LOGISTICS_DESIGN.md) | 입고/출고 트럭의 자동 교대 계획 **(미구현)** |
| [FORKLIFT_NAVIGATION.md](Source/msc_vr/Agv/FORKLIFT_NAVIGATION.md) | 별도 개발 중인 동역학·LiDAR·내비게이션 모듈 |
| [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) | 요청별 첫 탐색 경로와 파일 구조 |
| [AGENTS.md](AGENTS.md) | AI 작업 범위와 구조 문서 갱신 지침 |

각 문서에는 담당 영역의 현재 기준을 기록한다. 설치 절차·사양·코드 설명을 다른 문서에 반복하지 않고 위 문서를 연결한다. 과거 검증 결과는 검증 날짜와 당시 범위를 구분한다. 별도 `Agv` 모듈과 현재 맵의 `WarehouseForklift`는 아직 통합되지 않았다.
