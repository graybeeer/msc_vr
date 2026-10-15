# 주황색 무인 지게차 — 분리형 모델

`orange_agv_unreal.blend`의 `AGV - Unreal modular` 씬이 수정본이다. 기존 `orange_agv.blend`는 보존했다. 사진과 바퀴 개념도를 참고한 모델이며 치수와 기구 배치는 추정값이다.

후면 `coupang`과 `19`는 `Scripts/wrap_orange_agv_labels.py`로 외장 곡면에 투영했다. 돌출 두께 없이 표면에서 약 0.15mm만 띄워 겹침을 피했으며 차체 메쉬에 합쳤다. 수정 결과는 `orange_agv_rear_wrapped.png`와 최신 Blender/FBX에 반영되어 있다. 원본 사진 모델 씬은 보존했다.

## 메쉬 구성

총 24개의 메쉬와 루트 Empty `AGV_ROOT`로 구성했다.

- `SM_AGV_Chassis`: 차체, 외장, 고정 마스트, 지지 다리, 센서 등 고정부를 Join한 메쉬.
- 구동·조향: 중앙 구동 바퀴 1개와 조향 캐리어를 분리했다.
- 안정 바퀴: 좌우 보조 바퀴 2개와 각각의 선회 캐리어를 분리했다.
- 로드 휠: 포크 쪽 지지 다리마다 2개씩 총 4개, 좌우 보기를 분리했다.
- 리프트: 내부 마스트, 캐리지, 좌우 포크, 램, 풀리, 체인을 분리했다.
- 버튼: HMI 버튼 3개와 비상정지 버튼 1개를 분리했다.

바퀴는 총 7개다. 각 바퀴의 허브·볼트·타이어는 함께 회전하도록 하나로 합쳤다. 포크는 캐리지에, 구동 바퀴는 조향 캐리어에 부모 연결했다. 정확한 계층과 피벗은 `assembly_manifest.json`을 참조한다.

Join은 고정부를 하나의 메쉬로 합치는 작업이다. 이번 가동부는 부모 계층과 회전·이동 피벗으로 구성했으며, 물리 조인트나 스켈레탈 리그를 만든 것은 아니다.

## Blender에서 동작 확인

`AGV_ROOT`의 Custom Properties를 조절한다. 저장된 상태는 모두 0인 기본 자세다.

| 속성 | 동작 |
| --- | --- |
| `steer_deg` | 중앙 캐리어의 Z축 조향 |
| `travel_m` | 주행 거리/반지름에 따른 바퀴 Y축 회전 미리보기 |
| `lift_m` | 내부 마스트와 캐리지가 각각 절반씩 상승, 포크의 총 상승량은 입력값 |
| `caster_L_deg`, `caster_R_deg` | 보조 바퀴 캐리어의 독립 선회 |
| `estop_press` | 0~1, 비상정지 버튼을 최대 8mm 누름 |
| `hmi_1_press` ~ `hmi_3_press` | 0~1, 각 HMI 버튼을 최대 3mm 누름 |

주행 컨트롤은 차체를 이동시키지 않으며 선회 시 각 바퀴 속도를 계산하는 차량 시뮬레이션도 아니다. 체인·램은 리프트 부품과 함께 움직이나 체인의 순환, 호스 변형, 실제 유압 기구는 구현하지 않았다.

## Unreal 가져오기

권장 경로는 `UnrealParts/`의 부품별 FBX 24개다. 각 파일은 자신의 피벗을 원점에 둔 단일 메쉬다. 별도 `orange_agv_assembly.fbx`에는 기본 자세의 전체 계층을 저장했다.

1. 부품별 FBX를 Static Mesh로 가져온다. 여러 부품을 하나로 합치는 Combine Meshes는 사용하지 않는다. Import Uniform Scale은 1부터 확인한다.
2. Actor Blueprint에 루트 Scene Component와 각 Static Mesh Component를 만든다.
3. `assembly_manifest.json`의 `parent`대로 연결하고 `relative_location_cm`을 상대 위치에 입력한다. 기준 자세의 상대 회전은 0, 스케일은 1이다.
4. 모델 기준은 X 전방, Y 우측, Z 상방이다. 처음 가져온 부품의 방향·크기를 확인한 뒤 조립한다. Blender에서 FBX 왕복 검사는 통과했으나 Unreal 내 실제 임포트는 아직 검증하지 않았다.
5. 조향은 캐리어의 로컬 Z축(Yaw), 바퀴 회전은 로컬 Y축(Pitch), 리프트는 로컬 Z축 이동으로 구동한다. 회전 부호는 실제 전진 방향에 맞춰 확인한다.
6. Blender 드라이버는 Unreal로 전달되지 않는다. Blueprint 또는 C++에서 해당 컴포넌트의 상대 변환을 제어한다. 재질 슬롯은 유지했으며 셰이더는 엔진에서 확인·설정한다. 원래 색상·금속성·거칠기·발광 값은 manifest에 있다.

전체 FBX의 계층까지 가져오려면 엔진의 FBX Scene Import 경로를 사용하고 결과를 확인한다. 일반 Static Mesh 임포트만으로 Blueprint 계층이 자동 완성되는 것으로 가정하지 않는다. 피벗과 복수 메쉬 처리는 [Epic FBX Static Mesh Pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-static-mesh-pipeline-in-unreal-engine)을 참고한다.

이번 결과에는 Unreal 에셋 생성, 충돌 메쉬, Physics Constraint, 질량·마찰, Chaos 차량 설정, 게임용 UV/LOD 최적화가 포함되지 않는다. 실제 물리 주행이 필요하면 별도 구성해야 한다.

## 검증과 미리보기

`validation.json`에 검증 결과를 기록했다. 7개 바퀴와 24개 메쉬, 리프트 0.8m 상승, 조향 35도, 바퀴 회전, 버튼 8mm 이동을 확인했다. 부품별 FBX 24개를 Blender에 재임포트하여 메쉬 수·원점·크기를 검사했고 최대 경계 오차는 약 0.00000006m였다.

- `orange_agv_modular.png`: 조립 상태 렌더.
- `orange_agv_wheel_layout.png`: 7개 바퀴를 볼 수 있도록 차체를 숨기고 지지 다리를 약간 띄운 설명용 렌더. 실제 에셋 형상·위치와 구분한다.
- `orange_agv_lift_demo.png`: 리프트 상승과 조향 미리보기.

제작 스크립트는 `Scripts/prepare_orange_agv_unreal.py`, `Scripts/export_orange_agv_unreal.py`, `Scripts/verify_orange_agv_export.py`다. Blender MCP에서 순서대로 실행했다. 준비 스크립트는 원본 씬을 기반으로 새 씬을 만들므로 이미 완성된 파일에 무조건 재실행하지 않는다.
