# 지게차 이동 속도·도리도리 수정 기록 (2026-10-21~22)

이 문서는 `feature/forklift-navigation` 브랜치에서 한 **지게차 이동 코드 수정**과 그 검증 결과를 다음 작업자(사람 또는 AI)에게 넘기기 위한 기록이다.
main 병합은 하지 않았고, 병합 여부는 팀장이 결정한다. 설치 절차·사양은 [README.md](../../../README.md)의 담당 문서를 따른다.

## 1. 한눈에 보기

| 항목 | 내용 |
| --- | --- |
| 브랜치 | `feature/forklift-navigation` (2026-10-22 원격에 푸시) |
| 바탕 | 2026-10-21 `origin/main`을 병합한 상태(`89789031`) 위에 이식 |
| 수정 커밋 | `8d8b256a` 이동 속도 이식, `c3551caa` 경로 실패 진단 로그 |
| 수정 파일 | `Source/msc_vr/WarehouseForklift.cpp`, `Source/msc_vr/WarehouseForkliftAI.cpp` 두 개뿐 |
| 목적 | 이동이 느리고 멈췄다 가는 현상, 목표 지점 근처에서 좌우로 반복해 도는 "도리도리" 줄이기 |
| 성격 | 모두 `Warehouse.Opt.*` 콘솔 변수로 끄고 켤 수 있다. 끄면 병합 전 main 동작으로 돌아간다 |

팀원의 경로 알고리즘·관제 구조는 바꾸지 않았다. [AUTONOMY.md](../../../AUTONOMY.md)의 사용자 지정 개발 기준(원격 조작에 센서 탐색·AI 판단을 넣지 않음)도 그대로다.

## 2. 수정 내용과 스위치

`WarehouseForkliftAI.cpp`의 `FollowRoute`와 정렬·하역 상태 처리, `WarehouseForklift.cpp`의 `ClearToMove`를 고쳤다.

| 스위치 (기본값) | 무엇을 하나 | 적용 범위 |
| --- | --- | --- |
| `Warehouse.Opt.WheelNormal` (1) | 구동 바퀴와 바닥의 접촉 법선이 바닥 수직에서 약 25° 벗어나도 굴러가는 접촉으로 본다(기존 11.5°). 바닥을 장애물로 오인해 정지하던 문제 수정 | 전 차량 |
| `Warehouse.Opt.ArrivalTol` (8) | 일반 경로 도착 위치 허용치를 5cm에서 8cm로 넓힘. **도리도리 원인 수정**(3절) | `NavigatePickup`, `NavigateDelivery`만. 충전·정렬은 기존 값 |
| `Warehouse.Opt.PivotRate` (1) | 제자리 회전 상한을 0.15~0.2에서 0.4rad/s(화물을 실으면 0.3)로 올림 | `NavigatePickup`, `NavigateDelivery`, `NavigateParking`이면서 방향 허용치가 1° 이상일 때만 |
| `Warehouse.Opt.UnloadSpeed` (1) | 하역 슬롯 진입 속도 10→22, 포크 철수 속도 15→30 | `AlignUnload` 진입, `WithdrawFork` |
| `Warehouse.Opt.Align` (1) | 포크 정렬 허용치를 위치 7cm·방향 1.5°로 넓히고, 정렬 창(위치 7cm·방향 1.8° 이내)에 들어오면 더 보정하지 않고 삽입 단계로 넘긴다. 최종 삽입 판정(`VerifyInsertion`)은 그대로 | `AlignVehicle` |
| `Warehouse.Opt.IntermediatePivot` (1) | 뒤에 경로가 더 이어지는 모서리에서는 정지 후 80cm를 기어가며 맞추지 않고, 허용치 12cm·접근 범위 25cm로 바로 다음 구간으로 넘어감 | 경로 중간 모서리 |

추가 로그: 경로를 못 찾으면 `AGV_PLAN_BLOCKERS`가 목표 자세와 접근 차선을 막는 액터 이름을 남긴다. 동작은 바꾸지 않는다.

### 적용 범위를 좁힌 이유 (되돌리지 말 것)
- `ArrivalTol`을 모든 상태에 적용하면 `verify_shared_charging_pie.py`가 `TRAFFIC NO SAFE ESCAPE - OPERATOR REQUIRED`로 실패한다.
- `PivotRate`를 모든 상태에 적용하면 같은 시험이 `Charging without strict physical docking or ownership`로 실패한다. 충전 접근 경로의 중간 구간 방향 허용치가 1°라서 빠른 회전이 적용됐기 때문이다.

## 3. 도리도리 근본 원인

`FollowRoute`의 끝점 보정(남은 거리 80cm 이내)은 위치 오차가 허용치의 0.7배(약 3.5cm)를 넘으면 **오차 벡터의 방위**로 차를 돌린다. 오차가 몇 cm일 때 그 방위는 1cm 수준의 흔들림에도 부호가 뒤집힌다. 그래서 목표 근처에서 좌우로 반복해 돌며 도착하지 못했다.

- 재현 위치: 서쪽 차선 앵커 약 (−750,−1570).
- 구동 진단(`-DriveDiagnostics`, `AGV_DRIVE_DIAG`)에서 `Turning=1 Orienting=0 Driving=0` 상태가 오래 이어졌고, 바퀴는 바닥에 정상 접지(바닥 z=0, 바퀴 중심 z=15.5)해 접지 문제가 아님을 확인했다.
- 수정은 도착 허용치를 8cm로 넓혀 방위가 뒤집히는 구간을 피하는 것이다. 근본적으로는 "몇 cm 오차의 방위로 회전하지 않는 끝점 제어"가 필요하다(남은 문제 참조).

## 4. 검증 결과 (2026-10-22, 확인 범위 명시)

환경: Windows, UE 5.8, `-nullrhi -unattended` 헤드리스 PIE, `Lvl_FirstPerson`, 저장하지 않는 시험. 시뮬레이션 속도 4배(별도 표기 제외). 같은 코드는 같은 숫자를 낸다(결정적).
화면 렌더링·실제 에디터 Play·사람 조작은 이 문서의 검증에 포함되지 않는다.

### 개선 확인 (출고 1건, 콘솔 출고 지시)
- 1호가 하역을 끝내는 시간: 병합 전 main 200초 이상 → 이식 후 약 133~140초.
- 서쪽 앵커에 갇히던 현상은 사라졌다. 같은 시나리오에서 순수 main은 `NavigatePickup`에서 80초 이후에도 같은 자리에 있었다.
- 고장 없이 대기 자리까지 복귀(133초), 4배속과 1배속 결과 동일.

### 회귀 확인
| 시험 | 현재 소스 | 비고 |
| --- | --- | --- |
| `verify_shared_charging_pie.py` | 통과 | 위 두 범위 제한 후 |
| `verify_fleet_traffic_pie.py` | 통과 | |
| `verify_obstacle_interval_pie.py` | 통과 | |
| `verify_forklift_sensors_pie.py` | 통과 | |
| `verify_native_forklift_motion_pie.py` | 통과 | |
| `verify_native_elevator_pie.py` | 통과 | 승강기 호출까지만, 적재 탑승은 시험하지 않음 |
| `verify_fleet_layout_stress_pie.py` | 2/11 통과 | 순수 main도 동일 |
| `verify_fleet_workflow_pie.py` | 실패 `Console inventory must include every saved-map cargo exactly once` | 순수 main도 동일 |
| `verify_forklift_recovery_pie.py` | 실패 `RECOVERY 1/3: OBSTACLE / NO PICKUP ROUTE` | 순수 main도 동일 |
| `verify_simulation_speed_pie.py` | 실패 `RECOVERY FLOOR UNSUPPORTED` | 순수 main도 동일 |
| `verify_forklift_autonomy_pie.py` | 실패 `PIE timeout` | 순수 main도 동일 |
| `verify_shipping_workflow_pie.py` | 실패 `NO SAFE DETOUR - CLEAR SPACE / OPERATOR REQUIRED` | 순수 main도 동일 |
| `verify_dynamic_forklift_transfer_pie.py` | 실패 `Dynamic transfer timeout` | 순수 main도 동일 |
| `verify_mezzanine_pie.py` | 실패 `AssertionError` | 순수 main도 동일 |
| `verify_dual_forklifts_remote_pie.py` | 실패 `NO SAFE DETOUR` (2호) | 순수 main은 같은 단계 통과 후 에디터가 크래시해 **비교하지 못함** |

실패 8건은 이번 수정과 무관한 기존 실패다(오래된 기대값이거나 다른 영역). 원격 이중 차량은 같은 종류의 고장으로 보이나 확정하지 못했다.

### 시나리오 시험
- 출고 지시를 동시에 여러 건 넣으면 1건만 수락된다(출고 위치 예약, `출고 위치가 다른 작업에 예약되어 있습니다`). 설계 제약이다.
- 통로 한가운데에 큰 장애물을 두고 출고하면 2초에 `NO CONNECTED ROUTE AFTER LAYOUT CHANGE`로 즉시 고장 정지한다(경로 막힘=고장 정책). 장애물을 치워도 운영자가 `E`(자가 점검)를 하기 전에는 복귀하지 않는다.
- 출고 4건 순차 시험은 두 번째 건에서 `DESTINATION OCCUPIED / UNSUPPORTED`가 나 확정하지 못했다. 이전 출고 팔레트를 시험 코드가 완전히 치우지 못했을 가능성이 있다. 차량 결함으로 단정하지 않는다.

## 5. 이식했다가 제거한 것
- 경로 계획의 제자리 회전 비용 상향(`Warehouse.Opt.PivotCost`, 비용 = 반경×각도×3+300). 켜면 출고 시험에서 2호가 `RECOVERY WITHDRAW BLOCKED`로 고장나고 1호가 주차 이동에서 멈춰 코드까지 제거했다. 필요하면 백업 브랜치 `backup/before-main-sync-2026-10-21`(로컬)에서 찾을 수 있다.

## 6. 렉 원인 (이동 코드와 무관)

`csvprofile` 계측(지게차 2대, 1배속, 헤드리스):
- 프레임 평균 약 40ms. `Warehouse/GameThread/DamageTick` 13.9ms + `LoadGraph` 12.7ms ≈ 26.6ms로 프레임의 약 3분의 2.
- 두 항목은 `WarehouseDamageSystem.cpp`이며 매 프레임 지지력 검사 레이가 약 2900개 나간다(화물 728개·팔레트 382개).
- 이식 전 순수 main에서도 13.8ms·12.7ms로 같아 이번 수정과 무관하다. 지게차 모터 0.07ms, 지게차 Tick 전체 5ms, Chaos 물리 2.2ms.
- 제안: 움직이지 않는 화물은 매 프레임 다시 계산하지 말고 변경분만 재계산하거나 검사 주기를 늘린다. 해당 파일은 이번 작업에서 수정하지 않았다.
- 시작 직후 한 프레임이 약 4.7초 멈추는 현상이 한 번 관측됐다. 원인은 조사하지 않았다.
- 조명·렌더링 비용은 계측에 포함되지 않았다.

## 7. 남은 문제
1. 서쪽 앵커 (−750,−1570)에서 40~60초 머무는 구간과 방향 반전이 남아 있다(출고 1건 기준 약 25~50회, 주행 중 정지 약 40%). 도리도리가 완전히 사라진 것은 아니다.
   - 2026-10-27 병합 빌드, 출고 1건, 1배속, 1호 0.5초 단위 기록: 작업 완료 130.5초 중 정지(속도 2 미만) **42.0초(32%)**. 주행 상태(`NAVIGATE_PICKUP` 14.0초, `NAVIGATE_DELIVERY` 19.0초)가 33초이고 나머지는 정렬·하역 등 짧은 정지다.
   - 가장 긴 정지는 `NAVIGATE_DELIVERY` 102.0~113.0초(11.5초), 위치 (−576,−1569)이고, 다음은 `NAVIGATE_PICKUP` (−742,−1565)에서 4.0초, (−492,−1397)에서 3.5초다.
   - 방향 반전 25회 중 11회가 (−700,−1600) 근처(서쪽 앵커)이고, 나머지는 픽업 접근 (−500~−400,−1400)에서 발생했다.
   - 원인은 아직 모른다. 같은 위치에서 `-DriveDiagnostics`(`AGV_DRIVE_DIAG`)로 어느 분기(회전 완료 대기, 끝점 보정, 허가 대기)가 붙잡는지 보는 것이 다음 단계다.
2. 근본 해결은 끝점 보정이 몇 cm 오차의 방위로 회전하지 않게 바꾸는 것이다. 이번에는 허용치 확대로 완화했다.
3. 순차 다건 출고와 원격 이중 차량 시험이 확정되지 않았다.
4. 2층(승강기 경유) 이동은 이번 수정 후 실제 층간 이송으로 시험하지 않았다(승강기 호출만 확인).
5. 구조 재설계안은 [NAVIGATION_REDESIGN.md](NAVIGATION_REDESIGN.md)에 있다. 구현된 것이 아니라 설계안이다.

## 8. 재현 방법

모든 시험은 저장하지 않는 헤드리스 PIE다. 시험마다 에디터를 새로 띄운다.

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\AGVForkLift\msc_vr\msc_vr.uproject" -ExecCmds="py C:/AGVForkLift/msc_vr/Scripts/verify_shared_charging_pie.py" -nullrhi -unattended -nosplash -nop4 -abslog="C:/AGVForkLift/msc_vr/Saved/Logs/check.log"
```

- 스위치를 바꿔 비교하려면 `-ExecCmds="Warehouse.Opt.ArrivalTol 5, Warehouse.Opt.PivotRate 0, py ..."`처럼 콘솔 명령을 스크립트 앞에 둔다.
- 구동 진단은 실행 인자에 `-DriveDiagnostics`를 추가하면 0.5초마다 `AGV_DRIVE_DIAG`가 나온다.
- 프레임 비용은 `csvprofile start`/`csvprofile stop` 사이의 `Warehouse/GameThread/*` 열을 본다.
- 출고 시나리오·프로파일·바퀴 접지 탐사 스크립트는 `Saved/AgvHarness/`에 있다. `Saved/`는 Git 추적 대상이 아니라 원격에 없다.

## 9. 다음 작업자 주의
- 위 스위치를 지우거나 적용 범위를 넓히기 전에 `verify_shared_charging_pie.py`를 먼저 돌린다(2절 참조).
- 병합 시 충돌이 나면 `FollowRoute`의 `HeadingTolerance`·`PositionTolerance`·`PivotRate` 부분과 `AlignVehicle` 분기, `AlignUnload`·`WithdrawFork` 속도 호출이 이 수정의 위치다.
- 실패 8건은 이번 수정 이전부터 있던 것이므로 이 브랜치의 합격 기준에서 제외했다. 병합 판단 시 같은 기준으로 보려면 순수 main과 먼저 비교한다.
