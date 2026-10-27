# LiDAR 위치 추정 작업 인계 (2026-10-27)

새 대화(또는 새 작업자)가 지게차 **LiDAR 위치 추정**을 이어받을 때 읽는 문서다. 구현된 기능이 아니라 인계 메모이며, "확인"이라고 적은 것은 2026-10-27에 코드·맵을 직접 읽어 확인한 내용이다.

## 1. 목표와 합의
- 현재 차량 위치는 엔진이 주는 정확한 값을 쓴다. 사용자가 이 방식을 받아들였다. LiDAR로 위치를 추정하는 것은 **현실감을 위한 선택 기능**이다.
- 선택지는 3단계다. (1) 엔진 위치 그대로 (2) 엔진 위치에 노이즈·지연·가끔 분실을 섞는 모델 (3) 실제 스캔 매칭. **(2)부터 시작해 (3)으로 가는 순서를 제안**했고, 최종 범위는 팀원과 정한다.
- 교수님 예시처럼 AGV가 이해한 월드(지도, 스캔 점, 경로)를 3D 월드에 선·점으로 보여 주는 시각화도 함께 논의했다. 엔진 안에서 그리는 방식을 권했다(5절).
- 사용자는 팀 프로젝트에서 "움직임 전반"을 맡을 예정이다(팀원과 2026-10-28 협의). 팀원 파일은 허락 없이 고치지 않는다.

## 2. 지금 코드 상태 (확인)
- **센서**: 지게차마다 라이다 5개(상단 1, 측면 스캐너 2, 포크 센서 2). 구현은 `Agv/AgvShadowSensors.cpp`, 호출은 `WarehouseForkliftPerception.cpp`. 1시뮬레이션 초에 한 번 스캔한다.
- **알려진 지도**: 건물 구조(`WarehouseMapStructure` 태그)와 고정 랙(`LayoutType:Rack`)의 충돌 박스. 첫 스캔 때 만들고, 랙이 떼어지거나 층 높이가 50cm 넘게 바뀌면 다시 만든다. 차량·충전소·승강기·트럭·팔레트·화물·사람은 지도에서 제외한다.
- **관측**: 지도에 없는 물체의 반환점만 모은다. 점 위치는 거리·빔 각도·장착 자세·차량 자세로 계산한다(거리 잡음 약 1cm, 점 누락 0.5~1%). 스캔 때마다 `ObservedObstacles`를 비우므로 **지난 관측을 누적하지 않는다.** 1.25초 넘게 갱신이 없으면 자율 구동은 제동 대기한다.
- **위치**: 엔진 값을 쓴다. 운용 차량에 위치 추정은 연결되어 있지 않다.
- **위치 추정 컴포넌트(소스에는 있으나 미연결)**:
  - `Agv/AgvLocalizerComponent.h` — 기본 클래스. 실제 위치에 주입 오차(`PositionBiasCm`, `YawBiasDeg`)와 사용 불가(`bAvailable`)를 더한 값을 `GetPose`로 준다. **단계 (2)의 출발점**이다.
  - `Agv/AgvLidarLocalizerComponent.h` — 바퀴 오도메트리(보정 오차·잡음 포함)에 LiDAR 한 바퀴를 미리 만든 2D 거리 지도에 맞춰 보정한다. 지도는 **한 번 만들고 다시 만들지 않는다.** **단계 (3)** 이다.
- **지도 갱신**: 센서 지도는 배치 편집 때 `RefreshAfterLayout` → `InvalidateSensorSnapshot`로 무효화되고 새 스캔 전에는 제동 대기한다. 위치 추정 모듈에는 이런 갱신 연결이 없다(확인). 연결할 때 `RefreshAfterLayout` 시점에 2D 지도를 다시 만들어야 한다.

## 3. 이전에 연결했던 코드 (백업 브랜치)
- 로컬 브랜치 `backup/before-main-sync-2026-10-21`(`b9ee3cd7`)의 이력에 `bUseAgvLocalization`, `NavigationTransform()`이 있다(`WarehouseForklift.h`, `WarehouseForkliftAI.cpp`, `Agv/AgvShadowSensors.cpp`, `Agv/FORKLIFT_NAVIGATION.md`). 설계는 이렇다.
  - 이동 중 경로 추종과 경로 계획 출발점은 추정 위치(`NavigationTransform()`)를 쓴다.
  - 하역·삽입 정렬, 충전 정렬, 남은 거리 80cm 이내 정지 접근, 충돌 예측 검사는 **정확한 위치**를 쓴다.
  - 위치 추정 상실은 `AGV LOCALIZATION LOST` 고장이다.
- 현재 브랜치(최신 main 기반)에는 이 연결이 없다. **그대로 가져오지 말고** 아래 주의점을 반영해 다시 연결하는 것을 권한다.
- 이전에 특징이 없는 빈 시험 맵에서 위치 추정을 켜면 `AGV LOCALIZATION LOST`가 나서 시험 스크립트에서 위치 추정을 꺼야 했다.

## 4. 주의점 (이미 겪은 것)
1. **도착 허용치와 추정 오차**: 도리도리의 원인은 끝점 보정이 몇 cm 오차의 방위로 회전한 것이었다([MOTION_SPEED_PORT.md](MOTION_SPEED_PORT.md)). 추정 위치에 2~5cm 잡음이 섞이면 같은 문제가 커진다. 허용치를 잡음보다 크게 잡고 판정에 히스테리시스를 둔다.
2. **갱신 주기**: 스캔이 1초에 한 번이라 사이에는 오도메트리로 이어야 한다.
3. **반복 구조**: 길고 비슷한 랙 통로는 스캔이 비슷해 위치가 미끄러지거나 헷갈린다.
4. **삽입과 충전 도킹**은 정확한 위치를 유지한다.
5. **상층**: 앵커·지도 모두 1층 기준이다([UPPER_FLOOR_ANCHORS.md](UPPER_FLOOR_ANCHORS.md)). 위치 추정 지도도 층별로 다시 만들어야 한다.
6. **도리도리 완화 장치**: `Warehouse.Opt.ArrivalTol` 등 스위치를 지우지 않는다. 적용 범위를 넓히면 `verify_shared_charging_pie.py`가 깨진다.

## 5. 시각화 제안 (미구현)
- 데이터는 이미 있다. 알려진 지도 박스(`KnownObstacles`), 관측 점과 박스(`ObservedObstacles`), 계획 경로(`PlannedRoute`), 앵커(`NavigationAnchors`).
- 엔진 안에서 디버그 선·박스·점으로 3D 월드에 겹쳐 그리고 키로 켜고 끈다. 라이다 컴포넌트에 점 표시 옵션(`bDrawPoints`)이 있으나 기본 꺼짐이며 운용 센서에서 켜지 않았다. 외부 도구(ROS/RViz 등)로 내보내는 방식은 브리지가 필요해 규모가 크다.
- 추정 위치가 생기면 엔진 위치와 같이 그려 차이를 보여 줄 수 있다. 그 전에는 둘이 같다.

## 6. 시험·재현
- 시험 도구는 `Saved/AgvHarness/`에 있다(Git 추적 대상 아님, 이 컴퓨터에만 있음). 주요 파일: `scen.py`·`scen_all.sh`(출고 시나리오), `suite_all.sh`(검증 묶음), `two_jobs.py`, `csv_run.py`(프레임 비용), `anchor_floors.py`, `floor_layout.py`, `floor2_geometry.py`.
- 단일 검증 실행: `UnrealEditor.exe msc_vr.uproject -ExecCmds="py <스크립트>" -nullrhi -unattended -nosplash -nop4 -abslog=<로그>`. 시험마다 에디터를 새로 띄운다(기동에만 약 2분).
- 기준선: 병합 빌드에서 `shared_charging`, `fleet_traffic`, `obstacle_interval`, `forklift_sensors`, `native_forklift_motion`, `elevator_route_visuals`가 통과한다. `fleet_layout_stress`(2/11), `fleet_workflow`, `forklift_recovery`, `simulation_speed`, `forklift_autonomy`, `shipping_workflow`, `dynamic_forklift_transfer`, `mezzanine`은 이 작업과 무관하게 순수 main에서도 같은 이유로 실패한다. `native_elevator`는 문 6개 기대값이 낡아 실패한다.
- 성능: 팀원의 피해·하중 계산이 프레임당 약 26ms를 써서 시험 4배속이 실제로는 약 1.1~1.4배다. 차량 수나 센서를 늘릴 때 먼저 확인한다.

## 7. 브랜치와 작업 규칙
- 작업 브랜치 `feature/forklift-navigation`. 원격은 `d34665e9`까지 올라가 있고, 로컬에는 main 병합(`e0052f3b`)과 문서 커밋이 더 있다. **사용자가 지시하기 전에는 푸시하지 않는다.** main 병합은 팀장이 하므로 하지 않는다.
- main에서 가져올 때 방향은 항상 main → 우리 브랜치다.
- [AGENTS.md](../../../AGENTS.md)의 지침을 따른다(구조 변경 시 `Scripts/update_structure_summary.ps1`, 팀원 알고리즘은 별도 통합 요청 전까지 운용 코드에 병합하지 않음).
- 사용자에게 보이는 모든 문장은 한국어로 쓴다. 확인한 것과 추정을 구분하고, 검증 범위(헤드리스, 날짜)를 적는다.

## 8. 제안하는 첫 단계
1. 이 문서와 [FORKLIFT_NAVIGATION.md](FORKLIFT_NAVIGATION.md)를 읽고, 사용자와 단계 (2)부터 갈지 범위를 확인한다.
2. `UAgvLocalizerComponent`를 운용 차량에 붙이고 주입 오차 모델로 `FollowRoute` 이동 구간만 추정 위치를 쓰게 한다(정렬·도킹은 정확한 위치 유지).
3. 허용치·히스테리시스를 조정하고 `shared_charging`, `two_jobs`, 출고 시나리오로 회귀를 확인한다.
4. 그다음 시각화 또는 스캔 매칭 연결로 넘어간다.
