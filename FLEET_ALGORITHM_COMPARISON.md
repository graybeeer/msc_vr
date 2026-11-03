# 지게차 이동 알고리즘과 실제 관제 시스템 비교

도식 정리 기준은 **2026-10-20**, 위치 추정 연결은 **2026-10-31**에 갱신했다. 현재 맵에서 실행되는 `WarehouseForklift`와 `WarehouseFleetCoordinator`의 코드를 읽어 도식화했다. 아래의 실제 시스템 설명은 제조사·표준 작성자·Open-RMF의 공개 자료에 근거하며, 비공개 제조사 알고리즘까지 같다고 가정하지 않는다. 센서·추정 위치 통합의 진행 상태·정보 경계·실행 검사와 운반 시간의 확인 범위는 [AUTONOMY.md](AUTONOMY.md), 물리 계수·실측 전 가정은 [PHYSICS.md](PHYSICS.md)를 따른다.

## 1. 전체 담당 관계

현재 중앙 교통 시스템은 게임 안에서 실행되는 단일 관제 관리자다. 작업 배정은 관제 컴퓨터·입고 구역이 담당하고, 통로 승인은 중앙 교통 관리자가 담당한다. 차량마다 자체 상태 머신·경로 계획·안전 검사·물리 구동을 수행한다.

```mermaid
flowchart LR
    User["관제 컴퓨터<br/>운반 지시·상태 확인"] --> Job["로컬 작업지시<br/>팔레트·목적지·작업 ID"]
    Inbound["입고 구역<br/>안정된 적재 팔레트·빈 랙 확인"] --> Job
    Job --> AI["차량별 자율 상태 머신<br/>지시 검증·운반·보고"]
    AI --> Plan["차량별 경로 계획<br/>차체·화물·회전·바닥 검사"]
    Building["건물·고정 랙 지도"] --> Plan
    Sensors["차량별 센서 관측<br/>내부 물체는 실측 후 사용"] --> Plan
    Sensors --> Localization["바퀴·자이로 + 1초 LiDAR 지도 매칭<br/>자율 통로 주행의 추정 위치"]
    Building --> Localization
    Localization --> Plan
    Localization --> Control
    Sensors --> Safe
    Plan --> Traffic["중앙 교통 관리자<br/>통로 예약·우회·양보·폐쇄"]
    Traffic --> Control["차량 경로 추종<br/>속도·조향·포크 목표"]
    Safe["차량 안전 검사<br/>사람·물체·고장·하중"] --> Control
    Control --> Physics["Chaos 물리<br/>바퀴 모터·마찰·접촉·승강"]
    Physics --> AI
    AI --> Report["작업 상태·오류·완료 보고"]
    Report --> User
```

차량 AI와 중앙 교통 관리자가 별도 위치에 있어도, 현재 둘은 같은 게임 프로세스의 객체 호출로 연결된다. 실제 WMS/MES 서버, MQTT 브로커, 네트워크 지연·통신 끊김 처리는 아직 연결되어 있지 않다. `Source/msc_vr/Agv/`의 LiDAR 센서와 위치 추정을 기존 경로 추종에 선별 연결하며, 독립 내비게이터·타이어 구동·안전 모듈은 분리한다. 정밀 접근·추정 상실·지도 변경 경계는 [AUTONOMY.md](AUTONOMY.md)의 추정 위치 주행 통합 절을 따른다. 원격 조작은 센서·AI·위치 추정을 거치지 않고 기존 물리 구동에 입력한다.

## 2. 차량의 작업 상태 흐름

```mermaid
flowchart TD
    On["전원 켜기"] --> Check{"자체 점검 정상?<br/>스캐너·E-Stop·제동·조향<br/>포크/마스트·위치·배터리"}
    Check -- 아니오 --> Fault["고장 보고·제동 요청<br/>원인 복구 후 재점검"]
    Check -- 예 --> Ready["READY · 작업 대기"]
    Ready --> Battery{"충전 필요?"}
    Battery -- 예 --> Allocate["중앙 공유 충전 예약<br/>필수 충전·낮은 잔량 우선"]
    Allocate --> Free{"사용 가능한 충전 자리?"}
    Free -- 예 --> Charge["충전소 경로·도킹 확인<br/>목표 잔량까지 충전"]
    Free -- 아니오 --> Waiting["일반 대기 자리에서 기다림<br/>시설 누락 오류·예약 재확인"]
    Waiting --> Allocate
    Charge --> ChargeDone{"충전 완료<br/>작업 또는 다른 충전 요청이 있나?"}
    ChargeDone -- 예 --> Leave["LeaveCharger · 실제 바퀴로 출차<br/>출구 도착 후 충전소 예약 해제"]
    ChargeDone -- 대기 자리 여유 --> Leave
    ChargeDone -- 아니오 --> DockIdle["충전 자리에서 충전 없이 대기<br/>다른 차량의 충전 요청에는 출차"]
    DockIdle --> Ready
    Leave --> Ready
    Battery -- 아니오 --> Queued{"대기 작업이 있는가?"}
    Queued -- 아니오 --> Idle{"유휴 기회 충전 대상?"}
    Idle -- 예 --> Allocate
    Idle -- 아니오 --> Parking["일반 대기 자리로 주행·정렬"]
    Parking --> Ready
    Charge -- 기회 충전 중 새 작업 --> Leave
    Queued -- 예 --> Parked{"충전 자리에<br/>무적재로 정차 중?"}
    Parked -- 예 --> Leave
    Parked -- 아니오 --> Validate{"작업 검증 통과?<br/>중복·하중·포크 높이·목적지 지지"}
    Validate -- 아니오 --> Fault
    Validate -- 예 --> PickupFloor{"출발지가 다른 층?"}
    PickupFloor -- 예 --> Lift1["승강기 예약·호출·문/진입 신호<br/>탑승·정지·층 이동·하차 확인"]
    Lift1 --> PickupPlan
    PickupFloor -- 아니오 --> PickupPlan["픽업 접근점까지 경로 계획<br/>중앙 승인 후 주행"]
    PickupPlan --> Detect{"저속 접근·팔레트 가시성 검사<br/>제한 시간 안에 감지했나?"}
    Detect -- 아니오 --> Fault
    Detect -- 예 --> Estimate["위치/각도 읽기·인식 오차 적용"]
    Estimate --> Align["차량 방향·포크 높이 정렬<br/>기울어진 팔레트 거부"]
    Align --> Insert["저속 포크 삽입"]
    Insert --> Verify{"양쪽 포크가 구멍에 들어갔나?<br/>실제 지지·삽입 깊이 정상?"}
    Verify -- 아니오 --> Retry["제동·포크 하강·후진 인출<br/>재인식·재정렬·재삽입"]
    Retry --> Detect
    Verify -- 예 --> Lift["접촉으로 들어 올리기<br/>적재 존재·하중·지지 확인"]
    Lift --> Withdraw["적재물을 싣고 후진 인출<br/>안전 운반 높이로 하강"]
    Withdraw --> DropFloor{"목적지가 다른 층?"}
    DropFloor -- 예 --> Lift2["승강기 층간 이송<br/>하차 후 해당 층에서 재계획"]
    Lift2 --> Delivery
    DropFloor -- 아니오 --> Delivery["목적지 경로 계획<br/>중앙 승인 후 적재 주행"]
    Delivery --> DropAlign["실제 팔레트 위치 기준 하역 정렬<br/>랙 내부 범위·받침 지지 확인"]
    DropAlign --> DropClear{"하역 위치가 실제로 비었나?"}
    DropClear -- 예 --> Lower["포크 하강·받침으로 하중 인계<br/>포크 후진 인출"]
    DropClear -- 다른 차량이 점유 --> DropWait["제동·중앙 관제 양보 요청<br/>실제 점유 해제 확인"]
    DropWait --> DropClear
    DropWait -- 30초 내 해제 불가 --> Fault
    DropClear -- 다른 물체·지지 이상 --> Fault
    Lower --> Confirm{"위치·각도·지지·분리 정상?"}
    Confirm -- 아니오 --> Fault
    Confirm -- 예 --> Complete["완료 보고·예약 해제"]
    Complete --> Ready
```

충전소·일반 대기는 [AUTONOMY.md](AUTONOMY.md)의 공유 운영 정책을 따른다. 시설 존재 여부도 작업 검증에 포함한다. 충전기에 정차한 차량은 지시를 보존한 채 `LeaveCharger`로 출차하고 READY에서 작업을 수락한다. 충전 검증의 실제 범위는 [PHYSICS.md](PHYSICS.md)에 기록한다.

삽입 재시도는 무한 반복하지 않는다. 재시도 한도를 넘거나 적재물 지지 상실·과적·전도·장비 고장이 확인되면 정지와 작업자 복구 요청으로 끝낸다. 팔레트·상자는 운반 중에도 동적 강체이며, 차체에 붙이거나 충돌을 꺼서 운반 성공을 만들지 않는다. 승강기 상태·상호 잠금의 상세 담당은 [MEZZANINE.md](MEZZANINE.md)다.

## 3. 경로 계산과 실제 이동

```mermaid
flowchart TD
    Pose["현재 강체 위치·방향 + 목표 위치·방향"] --> Envelope["차체와 실제 화물의 통과 범위 계산"]
    Envelope --> Direct{"직결 경로가 가능한가?<br/>전후진 곡선 또는 회전→직선→회전"}
    Direct -- 아니오 --> Graph["같은 층의 차로 앵커 검색<br/>A* 비용 = 이동 비용 + 목표까지 거리"]
    Graph --> Geometry["곡률·방향·연결 비용 검사<br/>실행 불가능한 곡선 후보 제외"]
    Geometry --> Edge["유효 후보의 모든 자세에서<br/>회전 공간·장애물·관제 예약·바닥 검사"]
    Direct -- 예 --> Permit
    Edge --> Found{"안전 경로를 찾았나?"}
    Found -- 아니오 --> Recovery["기존 차로·임시 옆 경유점·현재 랙 주변으로 복구 계획<br/>실패하면 원인 안내·정지·작업자 요청"]
    Found -- 예 --> Permit{"중앙 통로 승인 요청<br/>주행 허가를 받았나?"}
    Permit -- 아니오 --> TrafficWait["제동·통로 대기 또는 중앙 우회 처리"]
    TrafficWait --> Permit
    Permit -- 예 --> Follow["속도에 따른 전방 주시점 추종<br/>곡선 감속·정밀 정렬·방향 전환 전 제동"]
    Follow --> Safe{"최근 주변 검사와<br/>장비·하중·바닥 검사가 통과했나?"}
    Safe -- 아니오 --> Brake["유한 제동력 요청<br/>사람은 대기·접촉은 제한된 복구"]
    Safe -- 예 --> Drive["바퀴 속도·조향·승강 목표 계산"]
    Drive --> Motor["유한 토크/출력의 물리 조인트 모터"]
    Motor --> Contact["Chaos 서브스텝<br/>마찰·충돌·관성으로 실제 이동"]
    Contact --> Feedback["실제 속도·위치·조향·지지 피드백"]
    Feedback --> Follow
```

계획은 같은 층의 차로 앵커 후보를 최대40개까지 사용한다. 복구 때 추가한 임시 경유점도 이 후보에 포함되며, 현재 위치·목표·방향 보정 노드는 따로 추가한다. 복구 계획에서는 사전 지도에 게시된 고정 랙과 관측 물체의 저장 외곽 주변에서도 경유점을 생성한다. 출발·목표와 가까운 후보부터 최대192개를 검사해 최대48개를 추가하며, 모든 연결은 차량·적재물의 이동·회전 범위와 고정 지도·센서 관측·바닥 지지 검사를 통과해야 한다. 이 후보는 저장 맵에 쓰지 않고 복구 계획마다 다시 계산한다. 임의의 창고 전체를 자유 공간 탐색으로 항상 풀어내는 계획기는 아니다. 적재 중에는 차량에 실린 화물의 실제 돌출 범위를 더하고, 자리 회전은 앞쪽 하중 바퀴 사이를 중심으로 전체 차체·화물의 회전 공간을 검사한다. 경로 추종이 요청한 속도와 실제 속도는 서로 다르며, 실제 바퀴 접촉·모터·부하 때문에 목표 속도에 즉시 도달하지 않는다.

주변 사람·장애물 관측은 **시뮬레이션 시간1초마다** 갱신하고 사이에는 최근 관측을 사용한다. 실제 Chaos 충돌과 물리 서브스텝은 계속 작동한다. `SeePallet`은 같은 스캔에서 관측되고 거리·시야각·가시선을 통과한 팔레트의 저장된 모델 인식 결과를 반환한다. 모델 자세에 인식 오차를 더하는 근사이며, 점군에서 팔레트 형상·구멍을 추출하는 실제 영상/LiDAR 알고리즘은 아니다. 자체 점검의 센서 정상 여부도 교육용으로 주입하는 값이다.

현재 성능 최적화는 이 센서 주기를 줄이거나 물리를 생략하지 않는다. 주기적 재고·입고 조회는 하중 시스템의 최신 받침 스냅샷을 공유하고, 경로는 실행 불가능하거나 최선 비용을 개선할 수 없는 곡선 후보를 먼저 제외한다. 선택 가능한 후보의 모든 자세는 고정 지도와 현재 사용할 수 있는 관측으로 검사한다. 센서 수집과 각 판단의 소비 시점을 분리하며 새 경로·방향·관측에 맞게 다시 판단한다. 센서 측정은 [AUTONOMY.md](AUTONOMY.md), 하중·경로 최적화와 CPU/GPU 측정은 [PHYSICS.md](PHYSICS.md)를 따른다.

## 4. 중앙 교통 관리의 의사결정

```mermaid
flowchart TD
    Request["차량이 통로 사용 요청"] --> Space["차체+화물의 남은 경로 외곽 계산<br/>정밀 진입은 현재~목표 외곽 계산"]
    Space --> Closed{"사용 중지 구간과 겹치는가?"}
    Closed -- 예 --> Detour["폐쇄 구간·기존 예약을 피한 우회 검사"]
    Closed -- 아니오 --> Escape{"다른 차량의 확보된 퇴로와 겹치는가?"}
    Escape -- 예 --> Wait["제동·진입 대기<br/>작업과 큐 보존"]
    Escape -- 아니오 --> Leased{"기존 통로 승인 또는<br/>먼저 요청한 차량의 경로와 겹치는가?"}
    Leased -- 예 --> Detour
    Leased -- 아니오 --> Occupied{"다른 차량의 관측된 외곽이<br/>통로를 점유하는가?"}
    Occupied -- 아니오 --> Grant["통로 승인<br/>겹치지 않는 작업은 동시 주행"]
    Occupied -- 예 --> Yield{"점유 차량이 정상·자율·낮은 적재인가?<br/>대기 지점과 실제 이동 공간이 있는가?"}
    Yield -- 예 --> Reserve["퇴로 예약<br/>다른 차량의 퇴로 진입 차단"]
    Reserve --> Move["점유 차량이 저속으로 후퇴/비켜 이동"]
    Move --> Timeout{"양보 이동의 제한 시간 초과?"}
    Timeout -- 예 --> Alarm
    Timeout -- 아니오 --> Contact{"물체·접촉 때문에 이동하지 못하는가?"}
    Contact -- 예 --> Separate{"접촉 상태·분리 전이며<br/>안전한60cm 직선 분리가 가능한가?"}
    Separate -- 예 --> SeparationMove["현재 방향으로 직선 분리<br/>실제 위치에서 퇴로 재계획"]
    SeparationMove --> SeparatedRoute{"분리 후 안전한 퇴로를 찾았나?"}
    SeparatedRoute -- 예 --> Move
    SeparatedRoute -- 아니오 --> Alarm
    Separate -- 아니오 --> Replan["현재 위치에서 같은 대기 목표로<br/>제한된 퇴로 재계획"]
    Replan --> Replanned{"새 안전 퇴로를 찾았나?"}
    Replanned -- 예 --> Move
    Replanned -- 아니오 --> HoldEscape["기존 퇴로 예약 유지·제동<br/>추가 시도 또는 제한 시간 만료 대기"]
    HoldEscape --> Move
    Contact -- 아니오 --> Reached{"대기 지점 도착·정지 확인?"}
    Reached -- 아니오 --> Move
    Reached -- 예 --> Arrived["대기 지점 도착<br/>예약을 현재 몸체 외곽으로 축소"]
    Arrived --> Pass["우선 차량 통과 승인"]
    Pass --> PassageClear{"옛 통과 구간이 비었는가?"}
    PassageClear -- 예 --> Resume["검증된 경로로 원래 작업 재개"]
    PassageClear -- 아니오 --> IdleLeader{"관측된 우선 차량이 READY이며<br/>작업·통로 예약을 끝냈는가?"}
    IdleLeader -- 아니오 --> Pass
    IdleLeader -- 예 --> IdleDetour["관측 외곽·다른 예약을 피해서<br/>원래 목적지로 우회 검사"]
    IdleDetour --> IdleClear{"새 전체 경로 검증 성공?"}
    IdleClear -- 예 --> Resume
    IdleClear -- 아니오 --> IdleLimit{"재시도 한도 초과?"}
    IdleLimit -- 아니오 --> Pass
    IdleLimit -- 예 --> Alarm
    Yield -- 아니오 --> Parked["점유 차량의 관측 외곽을 계획 장애물로 넣기"]
    Parked --> Detour
    Detour --> Clear{"안전한 우회로가 있는가?"}
    Clear -- 예 --> Request
    Clear -- 아니오 --> Wait
    Wait --> Limit{"대기·재시도 한도 초과 또는<br/>안전한 이동 공간 없음이 확인됐나?"}
    Limit -- 아니오 --> Request
    Limit -- 예 --> Alarm["정지 유지·비상 UI·경고음<br/>작업자 복구 요청"]
```

이미 진입 승인받은 차량을 우선하고 신규 충돌 요청은 요청 순서로 대기한다. 예약은 남은 경로의 공간 외곽을 비교하는 방식이며, 모든 차량의 미래 도착시각을 동시에 최적화하는 시간표는 아니다. 긴 경로 전체의 예약은 충돌 예방을 쉽게 하지만 겹치는 길을 오래 묶어 처리량을 낮출 수 있다.

퇴로는 뒤쪽 후보를 먼저 검사하고 앞쪽·옆쪽 후보도 검사한다. 차량·화물이 통과할 실제 공간, 바닥 지지, 다른 예약을 확인한 후 유한 구동력으로 이동한다. 양보 도중 접촉·장애물이 생기면 같은 대기 목표로 제한된 재계획을 수행하며, 접촉 분리 이동도 검사를 통과한 방향만 사용한다. 사람이 있는 방향으로 자동 탈출하지 않는다. 세부 한도와 실행 검증은 [AUTONOMY.md](AUTONOMY.md)의 중앙 관제·끼임 복구 절을 따른다.

## 5. 실제 시스템과의 비교

비교 자료의 범위는 서로 다르다. VisionNav는 실제 제조사의 공개 운용 사례이며, VDA5050은 관제↔차량 통신 규격이고, Open-RMF는 공개된 다중 로봇 교통 조정 구현이다. 이 셋을 하나의 특정 공장 시스템이나 동일한 제조사 내부 알고리즘으로 묶지 않는다.

| 항목 | 현재 게임 | 실제 공개 시스템/규격 | 차이가 시뮬레이션에 주는 의미 |
| --- | --- | --- | --- |
| 작업과 관제 분담 | 로컬 컴퓨터/입고 구역이 지시하고 차량 상태 머신이 실행·보고 | VisionNav 사례는 WMS→중앙 관제→차량 운반이며 RCS는 차량·작업·이상·실시간 경로를 관리한다. [제조사 운용 사례](https://www.visionnav.com/application/vnsl14productionlinemanufacturingindustry), [RCS](https://www.visionnav.com/product/robot-control-system.php) | 작업 단계와 병목 위치는 비교할 수 있다. 실제 IT 연동 시간은 포함되지 않는다. |
| 관제 통신 | 같은 프로세스의 함수·이벤트, 작업 큐 | VDA5050은 MQTT/JSON의 지시·상태·접속·차량 정보 교환을 정의한다. [공식 명세4·5절](https://github.com/VDA5050/VDA5050/blob/main/VDA5050_EN.md#4-transport-protocol) | 무선 지연·손실·끊김·재접속은 아직 시험할 수 없다. |
| 위치와 팔레트 인식 | 바퀴/자이로 오도메트리와 공통1초 LiDAR 스냅샷의 지도 매칭을 기존 경로 추종에 연결. 관측 후 팔레트 모델 자세와 오차 주입 | VisionNav VNSL14 공개 사례는3D laser SLAM과3D 센서 기반 팔레트 자세·포크 구멍 인식을 설명한다. [제조사 사례](https://www.visionnav.com/application/vnsl14productionlinemanufacturingindustry) | 완전한3D SLAM이나 영상/점군 기반 팔레트 ML 인식기는 아니다. 정밀 접근은 기존 정확한 자세를 유지한다. |
| 경로 계획 | 차량별 차로 앵커 A*와 곡선/자리 회전 연결 | VDA5050은 선로 추종과 차량 자체 계획을 구분하지만 특정 계획 알고리즘을 지정하지 않는다. [공식 명세2·3·5절](https://github.com/VDA5050/VDA5050/blob/main/VDA5050_EN.md#2-scope) | 현재 A*가 제조사와 동일하거나 표준이 요구하는 알고리즘이라고 말할 수 없다. |
| 교통 승인 | 남은 경로 외곽 예약, 진입 차량 우선·신규 요청 순서 | VDA5050의 실행 허가와 요청/응답은 승인 교환을 표현한다. Open-RMF는 시간 정보를 가진 궤적·계획을 제안·거절·대안 협상한다. [VDA5050 6.9절](https://github.com/VDA5050/VDA5050/blob/main/VDA5050_EN.md#69-requestresponse-mechanism), [RMF 협상 코드](https://github.com/open-rmf/rmf_traffic/blob/main/rmf_traffic/include/rmf_traffic/schedule/Negotiation.hpp), [RMF 궤적 코드](https://github.com/open-rmf/rmf_traffic/blob/main/rmf_traffic/include/rmf_traffic/Trajectory.hpp) | 현재 예약은 시간 슬롯 최적화·도착시각 기반 협상보다 보수적이며 처리량이 달라질 수 있다. |
| 정체와 복구 | 우회·퇴로 확보·실제 후퇴·접촉 분리·재개·안전 공간 없으면 작업자 요청 | VDA5050은 관제의 막힘 해소 역할과 실행 허가 상실 시 대응을 설명하되, 상세 교통 해법은 범위 밖이다. [공식 명세5.3·6.6.10절](https://github.com/VDA5050/VDA5050/blob/main/VDA5050_EN.md#53-functions-of-the-fleet-control) | 이 게임의60cm 분리·후퇴 후보·횟수 제한은 프로젝트 구현이며 제조사 복구 절차로 일반화하지 않는다. |
| 안전 정지 | 주기적 주변 검사·고장 플래그·유한 제동·Chaos 충돌 | VDA5050은 안전 표준이 아니며 기능·운영·시스템 안전 요구를 정의하지 않는다고 명시한다. [공식 명세2절](https://github.com/VDA5050/VDA5050/blob/main/VDA5050_EN.md#2-scope) | 현재1초 검사를 실제 안전 스캐너 응답이나 안전 인증에 해당한다고 취급하지 않는다. |
| 차량 물리 | 질량·마찰·모터·제동·하중·지지에 따른 강체 거동 | 실제 차량의 모터·타이어·바닥·부하 특성은 실측과 제조사 데이터로 확인해야 한다. 게임의 검증·가정은 [PHYSICS.md](PHYSICS.md) | 배치별 상대 효율 비교에 사용하며 실제 처리량 예측의 정확도는 실측 보정 수준에 달린다. |

**현재 비교 가능한 대상은 배치·동선·작업 순서에 따른 상대적인 대기와 처리 시간이다.** 센서·통신·안전 장치의 현실성을 추가하려면 실제 장비 데이터로 지연·오차·제동·구동·하중을 보정해야 한다. 제조사 비공개 관제 알고리즘을 재현했다고 주장하지 않는다.

## 6. 다시 확인할 코드

| 확인할 알고리즘 | 진입점 |
| --- | --- |
| 작업 수신·상태 전환·완료 보고 | `WarehouseForkliftAI.cpp`의 `SubmitJob`, `AdvanceAutonomy`, `ReportJob` |
| 경로의 공간·곡률·앵커 탐색 | 같은 파일의 `NavigationEnvelope`, `NavigationClear`, `ConnectPoses`, `ConnectLanePoses`, `PlanAutonomousRoute` |
| 경로 추종·실제 팔레트 감지 | 같은 파일의 `FollowRoute`, `SeePallet`, `RackLoadAligned` |
| 통로 승인·점유 차량 양보·접촉 분리·재개 | `WarehouseFleetCoordinator.cpp`의 `Permit`, `Retreat`, `AdvanceYield`, `PlanningClear` |
| 삽입 재시도·일반 접촉 후진·우회 | `WarehouseForkliftRecovery.cpp`의 `BeginRecovery`, `AdvanceRecovery`, `PlanRecoveryDetour` |
| 바퀴·조향·브레이크·승강 물리 제어 | `WarehouseForkliftPhysics.cpp`의 `CommandDrive`, `CommandTurn`, `UpdatePhysicalRig` |

VDA5050 공식 저장소는 확인 당시3.0.0을 공개하고 있으며, 저장소의 Markdown과 공식 PDF가 다르면 VDA가 게시한 PDF가 우선한다고 안내한다. 표의 프로토콜 비교는 공개 명세와 개념의 비교이며, 게임이 VDA5050을 구현하거나 적합성 검사를 통과했다는 뜻은 아니다. [VDA5050 공식 저장소](https://github.com/VDA5050/VDA5050)
