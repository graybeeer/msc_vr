# 충전기 외형 모델

2026-10-14 사용자 제공 상상 이미지를 참고해 Blender MCP로 만든 디자인이다. 캐비닛·통풍구·문과 손잡이, 다단 황동 접점, 녹색 표시등, 도킹 범퍼, 센서와 비상정지 버튼을 모델링했다. 현재 `Lvl_FirstPerson`의 기존 충전기 두 대에 외형을 적용했다.

## 모델 파일과 기준

- Blender 원본: `Art/Equipment/AGVCharger/agv_charger.blend`, 씬 `AGV Charger - concept`.
- 전체 FBX: 같은 폴더의 `agv_charger.fbx`.
- 팀원 전달용 단일 FBX: `Art/Equipment/AGVCharger/Delivery/AGV_ChargingStation.fbx`. 2026-10-14 Blender 재임포트 검사에서 확인한 충전기 8개 메쉬만 포함한 파일의 동일 복사본이다. 벽·바닥·안전 볼라드는 포함하지 않는다.
- 부품별 FBX 8개와 조립 위치·재질 값: `UnrealParts/`, `assembly_manifest.json`.
- 렌더: `charger_hero.png`, `charger_front.png`, `charger_contact_detail.png`.
- 재현: `Scripts/build_agv_charger.py`, `Scripts/export_agv_charger.py`를 Blender MCP에서 순서대로 실행한다. 기존 완료 씬에 제작 스크립트를 재실행하지 않는다.

캐비닛 크기는 약 폭 74cm·깊이 40cm·높이 161cm다. X 전방(차량을 향함), Y 우측, Z 상방이며 원점은 접점 면 X=0 아래 바닥이다. 접점 모듈은 이미지의 외형을 참고하되, 현재 코드의 접점 중심 높이 28cm에 맞는 패드를 포함하도록 낮게 배치했다. 패드 중심은 20/28/36/44/52cm이며 좌우 한 쌍씩이다. 장면의 벽·바닥·노란 선·안전 볼라드는 설명용으로 FBX에서 제외했다.

캐비닛, 문, 접점 모듈, 황동 접점, 상태등, 도킹 범퍼, 센서, 비상정지 버튼을 8개 메쉬로 나눴다. 비상정지 버튼의 원점은 버튼 중심, 나머지는 조립 기준 원점이다. 충전 전기 회로, 실제 센서, 전기 안전 인증을 재현한 모델은 아니다.

## Unreal 적용 준비

현재 충전기 루트 `DockPose`는 차량 도킹 자세다. `WarehouseChargingStation.cpp`의 기존 접점 면은 `-52 - RearExtension`cm이며 모델 루트를 그 위치로 놓는 초기 배치 값을 manifest에 기록했다. 기존 Cabinet/ContactPlate/StatusLight의 외형을 이 메쉬들로 교체하고, 버튼 상대 위치는 manifest에 따라 조립한다. 상태등은 발광 머티리얼을 연결한다. Blender의 노이즈 표면 셰이더는 FBX로 그대로 전달되지 않으므로 엔진 재질에서 처리한다.

Blender 원본은 8개 부품을 유지한다. 현재 맵 적용에는 기존 액터의 세 컴포넌트에 맞춘 `SM_Charger_GameCabinet_Final`, `SM_Charger_GameContact_Final`, `SM_Charger_GameStatus_Final`을 사용했다. 에셋과 재질은 `Content/Warehouse/ChargingStation/`에 저장했다. 비상정지 버튼은 이 맵 적용안에서는 외형 부품이며 입력 기능을 연결하지 않았다. 기존 충전 안내 TextRender는 숨겼고 모델의 AGV 글자를 사용한다.

적용 스크립트는 `Scripts/apply_agv_charger.py`다. 충전기 루트 위치·차량 연결·충전 코드와 전력 설정은 유지하고 세 메쉬와 상대 변환·재질을 교체한다. 캐비닛에 임포트된 단순 충돌을 사용하며 접점·상태등에는 충돌을 끈다. 향후 도킹 동작 검사에서 접점 프레임과 실제 차량 간격을 확인해야 한다. 충전 정책과 기능은 [AUTONOMY.md](AUTONOMY.md), 전원·배터리 가정은 [VNSL14_SPEC.md](VNSL14_SPEC.md)를 따른다.

## 검증

2026-10-14 Blender에서 전체·정면·접점 상세 렌더를 확인했다. 전체 FBX를 독립된 임시 씬에 재임포트해 메쉬 8개와 각 메쉬 크기가 일치하는지 검사했다. 결과는 `assembly_manifest.json`에 기록한다.

같은 날짜에 Development Editor 빌드를 통과했고 Unreal에서 재질과 세 메쉬를 임포트해 두 충전 액터에 적용·저장했다. 캐비닛 높이 161cm와 전방 축을 임포트 경계로 검사하고, 액터 위치와 차량 연결 유지 및 양쪽 `IsDocked` 판정을 확인했다. `Saved/Testing/ChargerModel/Report.json`에 실제 범위를 기록한다. 실행 중 자동 도킹·충전량 증가·출차 회귀 검사는 이번 작업에서 수행하지 않았다.

Unreal 에디터 캡처는 `Saved/Testing/ChargerModel/ChargerInWarehouse.png`다. 캡처용 임시 카메라는 맵 저장 후 생성했고 촬영 후 제거했다.

### 2026-10-15 기본 외형 복원

기존 적용은 맵 인스턴스의 교체였으며 `WarehouseChargingStation.cpp`의 기본 외형은 여전히 큐브 세 개였다. 생성·기본값 재적용에서도 새 외형을 사용하도록 클래스 생성자가 위의 `*_Final` 메시 세 개와 조립 변환·재질·충돌 구성을 기본값으로 사용하게 수정했다. 최종 에셋을 로드할 수 없는 경우에만 기존 기본 도형을 사용한다.

Development Editor 빌드를 통과한 뒤 공식 Unreal MCP로 두 충전기의 메시·상대 변환을 확인하고 저장했다. 위치·배정 차량·출력·효율 보존과 맵 재로드 후 부품 여섯 개의 최종 메시 참조를 검증했다. 결과는 `Saved/Testing/ChargerRoofRestore/Report.json`, 실제 MCP 캡처는 같은 폴더의 `ChargerAfter.png`다. 자동 주행·충전량 증가·출차 회귀 검사는 이번 외형 복원 범위에 포함하지 않았다. 같은 작업의 건축 보정은 [창고 안내](WAREHOUSE.md)의 현재 맵 기록을 따른다.
