# 카운터밸런스 수정본

최신 사용자 첨부 자료에 맞춘 외형 수정본이다. 이전 7륜 스태커형 결과는 상위 폴더에 보존했다. 최신 작업 파일은 `orange_agv_counterbalance.blend`이며 활성 씬은 `AGV - Counterbalance revision`이다.

- 포크 아래 지지 다리와 로드 휠을 제거했다. 마스트 아래 좌우 고정축 하중지지륜(지름 210mm)과 차체 하부 중앙 구동·조향륜, 총 3륜으로 변경했다.
- 옆면 장비함을 돌출된 상자, 뚜껑, 손잡이, 잠금장치, 하단 받침으로 재구성했다. 해당 외장함을 실제 배터리 팩이라고 단정하지 않는다.
- 모델 높이는 약 2.29m, 외부 장비함 포함 폭은 약 1.14m다. 하부 차체 길이는 1.60m, 포크 메쉬 길이는 1.15m다. 첨부 제원표를 참고한 근사 모델이며 제조사 실측 모델이 아니다. 내부 모듈, 최대 리프트 높이, 정격 하중 및 물리 성능은 구현·검증하지 않았다.
- 후면 글자는 곡면 밀착 형태를 유지했다. 고정부는 `SM_AGV_Chassis`에 합쳐져 있고 전체 메쉬는 16개다. 바퀴, 조향부, 리프트, 좌우 포크, 버튼의 분리와 피벗은 유지했다.

## Unreal용 파일

전체 FBX는 `orange_agv_counterbalance.fbx`, 원점 피벗을 가진 개별 FBX 16개는 `UnrealParts/`에 있다. `assembly_manifest.json`의 부모와 cm 단위 상대 위치에 따라 Blueprint 컴포넌트를 구성한다. X 전방/Y 우측/Z 상방, 상대 회전 0, 스케일 1이 기준이다. 여러 가동부를 하나의 메쉬로 합치지 않는다.

Blender 루트 `AGV_ROOT`의 `lift_m`, `steer_deg`, `travel_m`, 버튼 속성으로 동작을 미리 볼 수 있다. Blender 드라이버는 FBX로 전달되지 않으므로 Unreal에서 컴포넌트 변환을 제어해야 한다. Physics Constraint, 충돌, 질량, 게임용 LOD·UV 최적화는 별도다. 이번 수정본은 엔진 내 실제 임포트까지 검증하지 않았다.

Blender에서 0.65m 상승, 30도 조향, 하중지지륜 회전을 확인했다. 원본 7륜 모델용 준비·검증 스크립트는 이 3륜 파일에 적용하지 않는다. 재현 스크립트는 `Scripts/revise_agv_counterbalance.py`와 `Scripts/export_agv_counterbalance.py`다.
