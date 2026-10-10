# 최종 AGV 모델과 기계 리그

현재 게임 모델은 사용자가 제공한 `SourceAssets/OrangeAGV/orange_agv_refined.fbx`를 기준으로 한다. 본체 비율을 보존하고 포크·체인을 보정한 버전이다. 최초 `orange_agv.fbx` 재조합이나 원본 몸통만 복원한 과거 버전은 현재 적용 기준이 아니다.

## 크기와 형상

| 항목 | 현재 게임 모델 |
| --- | --- |
| 전체 외형 | 약 212.4 × 91.7 × 215cm |
| 본체·외부 마스트·센서 타워 | 원본 비율을 유지하는 균일 배율 |
| 포크 날 1개 | 길이 115 × 폭 18 × 두께 6cm |
| 좌우 포크 중심 간격 | 50cm, 바깥 폭 68cm |
| 최대 포크 윗면 높이 | 160cm |
| 앞쪽 지지 바퀴 | 원본 장착 위치 유지, 중심 Y=±34.24cm |

제조사 문서의 전체 길이 164.2cm·폭99.4cm와 현재 모델 치수는 다르다. 본체 비율과115cm 포크를 유지하고, 앞바퀴는 원본 위치에 두라는 사용자 지시를 우선했다. 모델 치수 차이를 제조사 실물 사양으로 해석하지 않는다. 실제 공개 사양·속도·하중·배터리 제한은 [VNSL14_SPEC.md](VNSL14_SPEC.md)를 기준으로 한다.

`Scripts/prepare_refined_agv.py`는 Unreal에 임포트한 작업용 복사본의 정점·피벗을 가공하고 기존 PBR 재질을 연결해 최종 부품을 만든다. 본체를 축별로 압축하지 않으며 포크의 수평 도달 거리·두께와 체인 길이를 보정한다. **원본 FBX 파일은 변경하지 않는다.**

## 리그와 물리

골격·스킨 애니메이션 대신 `AWarehouseForklift`의 강체 컴포넌트 계층을 사용한다. 최종 부품은 본체·조향 구동부·캐리지·좌우 포크·내부 마스트·리프트 램·풀리·체인·구동 바퀴·좌우 지지 바퀴의 12종이다.

캐리지와 포크, 내부 마스트와 램·풀리는 별도 강체 그룹으로 가이드 조인트와 제한된 유압 힘에 따라 이동한다. 내부 마스트 목표는 캐리지 목표의 절반이며 체인은 실제 양 끝의 위치에 맞춰 시각적으로 길이를 보정한다. 바퀴는 물리 축과 실제 모터 토크로 회전한다. 충전기 접점은 본체 후면에서1cm 떨어지도록 배치한다.

포크의 L자 충돌은 날·뒤꿈치 두 박스로 구성하여 팔레트 구멍을 막지 않는다. 팔레트와 상자는 차량에 부착하지 않고 계속 동적 물리를 유지하며 포크의 실제 접촉으로 운반한다. 자세한 삽입·지지·미끄러짐·하중 검사는 [AUTONOMY.md](AUTONOMY.md)와 [WAREHOUSE.md](WAREHOUSE.md)를 확인한다.

질량 분배·모터·조향·제동·마찰·전도와 검증 범위는 [PHYSICS.md](PHYSICS.md)를 따른다. 체인 링크·유압 회로·탄성 타이어·현가와 제조사 CAD를 모두 재현하지는 않는다.

## 파일과 적용 순서

| 구분 | 위치 / 관리 |
| --- | --- |
| 제공 원본 | `SourceAssets/OrangeAGV/orange_agv_refined.fbx`, Git 공유 |
| 최종 리그 메시 | `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_*`, Git 공유 |
| 공통 PBR 재질 | `Content/Warehouse/AGV/Materials/`, Git 공유 |
| 중간 임포트·작업 복사본 | `Content/Warehouse/AGV/RefinedSource/`, 재생성 대상·Git 제외 |
| 보정 보고서 | `Saved/RefinedAGVSource.json`, `Saved/RefinedAGVCalibration.json`, 로컬 생성물 |

1. 에디터의 작업을 저장하고 종료한다. C++ Editor 타깃을 빌드한다.
2. 모델을 새로 생성해야 할 때 `Scripts/prepare_refined_agv.py`를 Unreal Python으로 실행한다. 재질을 유지하는 메시 병합을 위해 `-RenderOffscreen`을 사용한다.
3. `Scripts/prepare_warehouse_dynamics.py`로 바퀴의 원통형 충돌체를 준비한 뒤 `Scripts/apply_refined_agv.py`를 실행하여 저장된1호 지게차에 최종 부품과 충전기 접점을 적용한다. 차량·작업 큐·팔레트·충전소·승강기 참조를 보존한다.
4. 현재 두 차량 배치를 사용할 때는 `Scripts/configure_dual_forklifts.py`를 마지막으로 실행한다. 1호의 최종 리그를 2호에 복사하고 두 차량의 시작 위치·시범 작업·충전소 설정을 적용한다. 이 단계는 해당 시범 작업과 배치를 기본값으로 재설정한다.

최종 에셋을 이미 Git으로 받은 팀원은 모델 재생성 없이 Editor 타깃 빌드와 필요한 외부 팩 설치로 사용할 수 있다. 설치 절차는 [TEAM_ASSETS.md](TEAM_ASSETS.md)에 있다. 운반 중인 런타임 상태에 모델 적용 스크립트를 실행하지 않는다.

기존 맵의 두 차량에서 앞바퀴 위치만 복원할 때는 `Scripts/restore_front_wheel_mounts.py`를 사용한다. 작업 큐·차량 배치·다른 부품을 재설정하지 않는다. 폭을 맞추기 위해 바퀴만 옆으로 벌리는 보정은 사용하지 않는다.

`prepare_orange_agv.py`, `apply_orange_agv.py`, `prepare_original_body.py`, `apply_visual_fixes.py`는 과거 모델 복원용이다. 최종 refined 모델을 유지하는 작업에는 실행하지 않는다.

## 검증

| 검사 | 진입점 |
| --- | --- |
| 치수·본체 비율·리그 | `Scripts/verify_refined_agv.py` |
| 기존 삽입·안전 정지·과적·충전 | `Scripts/verify_forklift_training.py`의 회귀 모드 |
| 모델 렌더링·체인·실제 화물 | `Scripts/verify_refined_agv_pie.py`, 미리보기 `Saved/RefinedAGV_Game.png` |
| 현재 2대와 G 원격 | `Scripts/verify_dual_forklifts_remote_pie.py` |
| 동적 화물 층간 이동 | [MEZZANINE.md](MEZZANINE.md)의 검사 진입점 |

검사 상태는 맵에 저장하지 않는다. 과거 통합 검증은 검사 당시 상태의 기록이며 현재 전체 기능의 재검증으로 해석하지 않는다.

2026-10-06에는 Development Editor 빌드, 모델 치수, 층간 이동·삽입·안전 정지·과적·충전·실제 플레이 검사를 통과했다. 당시 로그는 `Saved/Logs/RefinedAGVFinalVerify.log`, `RefinedAGVTrainingVerify.log`, `RefinedAGVPIE2.log`다. 이후 동적 팔레트와 2대 원격 조작 검사는 위 별도 진입점에서 수행한다.
