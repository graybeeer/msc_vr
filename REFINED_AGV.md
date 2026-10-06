# 수정 AGV 모델 적용

원본: `SourceAssets/OrangeAGV/orange_agv_refined.fbx`. 원본 FBX와 최종 리그 메시를 Git에 포함한다. `Content/Warehouse/AGV/RefinedSource/`는 재생성 가능한 임포트 작업물이므로 제외한다.

`Scripts/prepare_refined_agv.py`가 원본을 임포트하고 PBR 재질을 연결하여 부품별 메시를 생성한다. 본체·외부 마스트·센서 타워는 원본 비율을 유지한 균일 배율로 높이 215cm에 맞춘다. 지지 바퀴의 좌우 위치로 전체 폭 99.4cm를 맞춘다. 포크 날은 길이 115cm, 폭 18cm, 두께 6cm이며 좌우 중심 간격은 50cm이다. 본체는 축별로 압축하지 않는다.

이 원본은 본체 비율과 115cm 포크를 함께 유지하면 전체 길이가 약 212.4cm다. 기존 사양 문서의 164.2cm 전체 길이와 동시에 일치하지 않는다. 현재 모델은 사용자 지시인 본체 비율 보존을 우선하며, 길이 차이를 실제 제조사 사양으로 해석하지 않는다. 속도·하중·최대 리프트 높이·배터리와 E키 자율 운행 기능은 유지한다.

강체 부품 리그는 본체, 조향 구동부, 캐리지, 포크 2개, 내부 마스트, 리프트 램, 풀리, 체인, 구동 바퀴, 지지 바퀴 2개다. 바퀴는 실제 메시 반지름에 따라 회전한다. 내부 마스트·램·풀리는 캐리지 이동량의 절반으로 움직이고, 체인은 양 끝을 연결하도록 길이를 보정한다. 이는 시각적 기계 리그이며 실제 체인 링크 물리나 유압 해석은 아니다. 포크의 L자 형상은 날·뒤꿈치 두 충돌 박스로 구성하여 팔레트 구멍을 막는 통짜 충돌 박스를 피한다.

에디터를 종료하고 C++를 빌드한 뒤 `Scripts/apply_refined_agv.py`를 Unreal Python으로 실행한다. 기존 액터에 새 부품을 적용하여 작업 큐·팔레트·충전소·승강기 참조를 유지하고 `/Game/FirstPerson/Lvl_FirstPerson`을 저장한다. 이전 모델 적용 스크립트는 과거 모델 복원용이며 새 모델에 실행하지 않는다.

캐리지는 포크 뒤꿈치 뒤에 맞춰 연결하고, 충전기의 접점은 본체 후면에서 1cm 떨어지도록 보정한다. 검증은 `Scripts/verify_refined_agv.py`(치수·본체 비율·3층 운반)와 `Scripts/verify_forklift_training.py`(삽입·안전 정지·과적·충전)를 사용하며 검사 상태는 저장하지 않는다.

실제 플레이 검증은 `Scripts/verify_refined_agv_pie.py`를 에디터의 Python 명령으로 실행한다. 새 모델을 렌더링하고 리프트 체인 연결, 실제 화물의 1층→3층 운반과 하역 후 물리 복원을 검사한다. 미리보기는 `Saved/RefinedAGV_Game.png`에 출력한다.

2026-10-06 검증 완료: C++ Development Editor 빌드 성공, `REFINED_AGV_GEOMETRY_PASSED`, `MEZZANINE_VERIFY_PASSED`(5회 층간 이동·사람 문 인터록·E 정지·비상정지·충전 복귀·승강기 과적), `FORKLIFT_TRAINING_VERIFIED`, `VNSL14_CHARGING_VERIFIED`, `REFINED_AGV_PIE_PASSED`. 로그는 `Saved/Logs/RefinedAGVFinalVerify.log`, `RefinedAGVTrainingVerify.log`, `RefinedAGVPIE2.log`에 있다.
