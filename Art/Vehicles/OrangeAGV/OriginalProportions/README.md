# 원래 비율을 유지한 최신 수정본

현재 사용할 파일은 `orange_agv_refined.blend`와 `orange_agv_refined.fbx`다. 기존 `orange_agv_unreal.blend`를 기준으로 다시 제작했으며, 직전 Counterbalance 버전의 가로 확대·높이 축소·두꺼운 통짜 하부 판은 적용하지 않았다.

- 원래 차체, 마스트, 포크의 치수와 비율을 유지했다. 제원표 수치에 맞추기 위한 전체 리스케일은 취소했다.
- 원래 하부 차체, 고무 범퍼, 얇은 금속 테두리 메쉬를 복원하고 바퀴 부분만 개구부를 만들었다.
- 포크 아래 지지 다리·로드 휠은 제거했다. 마스트 아래 하중지지륜 2개와 중앙 구동·조향륜 1개를 사용한다.
- 옆면 글자가 있는 장비함은 돌출 케이스·손잡이·잠금장치·아래 받침으로 구성했다.
- 후면 coupang 및 19 글자의 차체 곡면 밀착을 유지했다.
- 고정부는 하나의 차체 메쉬, 가동부는 분리된 총 16개 메쉬다. 리프트·포크·바퀴·조향·버튼 피벗 및 부모 계층을 유지했다.

`UnrealParts/`의 부품별 FBX를 `assembly_manifest.json`의 부모와 cm 상대 위치에 맞게 조립한다. X 전방/Y 우측/Z 상방, 기본 상대 회전 0·스케일 1이다. 루트 `AGV_ROOT`의 Blender 속성으로 리프트와 조향 등을 미리 볼 수 있으나, Unreal에는 드라이버가 전달되지 않으므로 컴포넌트 변환 제어를 구현해야 한다. 물리 조인트·충돌 설정과 Unreal 실제 임포트는 별도다.

제작: `Scripts/refine_agv_original_proportions.py`를 이전 `orange_agv_unreal.blend`의 modular 씬에서 실행한 뒤 `Scripts/export_agv_refined.py`로 내보낸다. 완료된 수정본에 제작 스크립트를 다시 실행하지 않는다.
