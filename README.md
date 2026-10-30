# 무인 지게차 교육 시뮬레이션

Unreal Engine 5.8 기반 PC 프로젝트다. 물류창고에서 무인 지게차의 운반·충돌·센서 이상·적재 실패를 재현하고 운영 조건을 비교한다.

## 시작하기

1. 코드와 게임에서 사용하는 에셋·필수 의존 파일을 Git으로 관리한다. [팀원 에셋 받기 안내](TEAM_ASSETS.md)에 따라 Git LFS 파일을 받은 뒤 프로젝트를 빌드한다. 전체 외부 샘플 팩은 필요하지 않으며, 사용 에셋의 출처와 범위는 [에셋 목록](RUNTIME_ASSETS.md)을 따른다.
2. `msc_vr.uproject`를 열고 메인 메뉴에서 **창고 시뮬레이션 시작** 또는 플레이 스테이지를 선택한다. 양쪽 모두 기본은 지게차 명령·플레이어 조작·배치 편집에 집중하고 다른 공정은 자동 처리한다. 기존 전체 관제는 설정의 **모든 작업 수동으로 처리**로 연다. 3개 플레이 스테이지의 예산·해금·고정 건물은 유지한다. [모드·스테이지 안내](PLAY_MODE.md)를 참고한다.
3. 현재 맵과 조작은 [창고 안내](WAREHOUSE.md), 수정할 파일은 [구조 요약](PROJECT_STRUCTURE.md)을 확인한다.

## 문서 안내

| 문서 | 담당 내용 |
| --- | --- |
| [PLAY_MODE.md](PLAY_MODE.md) | 메인 메뉴·관리자/플레이 분리·3개 고정 스테이지·예산/해금·가격 가정·스테이지 장식·모델 아이콘·검증 |
| [WAREHOUSE.md](WAREHOUSE.md) | 현재 맵·플레이어/배치 편집·초기 배치 저장·반복 실험/성과 비교·보류 물류·관제 화면·건축/작업자 |
| [PHYSICS.md](PHYSICS.md) | 강체·접촉·사람/차량 구동·손 운반·질량·마찰·손상 가정과 실행 검증 |
| [COLLISION_POLICY.md](COLLISION_POLICY.md) | 충돌/비충돌 부품 분류·공통 검사·선택 전용 채널·분류 검증 |
| [AUTONOMY.md](AUTONOMY.md) | 작업 큐·중앙 교통 관제·시설 누락·공유 충전/일반 대기·자율 운반·G 원격 조작·센서 선별 통합과 정보 범위 |
| [FLEET_ALGORITHM_COMPARISON.md](FLEET_ALGORITHM_COMPARISON.md) | 현재 차량·중앙 이동 알고리즘 도식과 실제 산업 시스템 비교 |
| [VNSL14_SPEC.md](VNSL14_SPEC.md) | 장비 공개 사양·게임 설정·배터리 모델의 가정 |
| [REFINED_AGV.md](REFINED_AGV.md) | 현재 지게차 모델·치수 보정·기계 리그 |
| [CHARGER_MODEL.md](CHARGER_MODEL.md) | 충전기 교체용 Blender 외형·FBX·접점 배치 기준 및 검증 범위 |
| [WAREHOUSE_SERVICES_MODEL.md](WAREHOUSE_SERVICES_MODEL.md) | 천장·벽면 설비/보호대 모델·FBX·현재 맵 배치 및 실제 검증 범위 |
| [MEZZANINE.md](MEZZANINE.md) | 기본3층·실험용 최대4층 복층, 범용/화물용 승강기·층간 이동 인터록 |
| [ELEVATOR_MODEL.md](ELEVATOR_MODEL.md) | 범용 승강기 외형·층별 호출대·팀원 모델 통합·파생4층 부품과 검증 |
| [CARGO_RULES.md](CARGO_RULES.md) | 상품 특성·상자 크기·밀도·무게 생성 규칙 |
| [TEAM_ASSETS.md](TEAM_ASSETS.md) | Git/LFS 받기·누락 에셋 진단·선택적인 원본 팩 설치와 파생 에셋 재생성 |
| [RUNTIME_ASSETS.md](RUNTIME_ASSETS.md) | Git에 선별한 게임 사용 에셋·필수 의존 파일의 이름·출처·용도·경로와 이용 조건 |
| [MCP_SETUP.md](MCP_SETUP.md) | Unreal·Blender·Photoshop MCP 연결 설정과 실제 연결 검증 |
| [TRUCK_LOGISTICS_DESIGN.md](TRUCK_LOGISTICS_DESIGN.md) | 팔레트 배송편·트럭 양방향 상하차·출차 연출, 검증 상태와 남은 자동 교대 계획 |
| [FORKLIFT_NAVIGATION.md](Source/msc_vr/Agv/FORKLIFT_NAVIGATION.md) | 별도 개발 중인 동역학·LiDAR·내비게이션 모듈 |
| [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) | 요청별 첫 탐색 경로와 파일 구조 |
| [SIMULATION_ROADMAP.md](SIMULATION_ROADMAP.md) | 현재 구조의 평가·미구현 개선안·배치 비교 실험과 운영 지표의 개발 우선순위 |
| [AGENTS.md](AGENTS.md) | AI 작업 범위와 구조 문서 갱신 지침 |

각 문서에는 담당 영역의 현재 기준을 기록한다. 설치 절차·사양·코드 설명을 다른 문서에 반복하지 않고 위 문서를 연결한다. 과거 검증 결과는 검증 날짜와 당시 범위를 구분한다. `Agv`의 센서만 기존 `WarehouseForklift`에 선별 통합했으며, 별도 타이어 구동·주행 알고리즘은 기존 차량에 연결하지 않는다. 활성화와 검증 상태는 `AUTONOMY.md`를 따른다.
