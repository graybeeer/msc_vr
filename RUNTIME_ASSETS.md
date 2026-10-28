# 게임 사용 에셋 목록

관리자 창고·메인 메뉴·플레이 스테이지 3개, C++/설정의 직접 로딩과 이름 조합 로딩을 시작점으로 실제 하드/소프트 참조를 수집한다. 게임에서 사용하는 외부 모델·재질·모션과 그 의존 에셋을 선별해 Git으로 함께 관리한다. 파일은 원래 경로에 두어 맵·재질 참조가 끊기지 않게 한다.

**2026-10-28 사용 에셋 선별 기준:** 이 목록의 런타임 에셋과 의존 파일을 Git에 포함한다. 1MiB 이상 패키지는 Git LFS로 받고 나머지는 일반 Git으로 받는다. 사용 중인 외부 팩의 파생 충돌·재질·트럭과 Game Animation Sample의 클립30개·블렌드2개도 포함 대상이다. 팩 전체·샘플 전체·원본 FBX ZIP·사용하지 않는 예제와 모션을 함께 추가하지 않는다. JSON의 `git_inclusion=runtime_dependency_subset`은 선별 기준이며 실제 커밋·LFS 수신 상태와 구분한다.

모든 파일·종류·원본 그룹·직접 참조·코드 진입점은 [정확한 목록](Config/RuntimeAssets.json)에 기록한다. 아래에는 이름이 있는 모델·재질·텍스처·모션·맵을 나열한다. GUID 이름의 외부 액터/오브젝트는 같은 JSON의 `map_objects` 그룹에 전부 포함한다.

팀원 설치·Git LFS 받기·선택적 원본 재생성은 [TEAM_ASSETS.md](TEAM_ASSETS.md), 실제 기능은 [WAREHOUSE.md](WAREHOUSE.md)를 따른다. 외부 에셋의 출처·라이선스 원문은 유지하며 Git 포함 자체가 별도의 재배포 권한을 부여하지 않는다. 엔진/플러그인 자체 파일은 저장소에 복사하지 않으며 목록의 `engine_plugin_dependencies`에 기록한다.

충돌 복사본·캐릭터 페이드·창고 벽 페이드는 원본 팩으로 분류한다. 여러 출처를 조합한 맵·공통 표시·모델 캡처 아이콘은 프로젝트 구성으로 분류하되 원본의 이용 조건을 승계한다. 단순히 프로젝트 폴더로 복사하거나 재질을 수정했다고 자체 원본이 되는 것은 아니다.

목록은 에셋 파일의 하드/소프트 참조를 보존하는 보수적인 의존성 폐쇄이며, 파일 내부의 미리보기·기본 재질 참조도 포함될 수 있다. 실제 Cook 결과의 바이트별 최소 목록을 뜻하지 않는다. 에디터 전용 설정의 예제 맵·콘텐츠 브라우저 폴더는 시작점에서 제외한다. 직접 로딩 경로가 없으면 보고서의 `unresolved_direct_references`에 기록하고 최신 목록 발행을 중단한다.

총 3,953개 파일 · 3.45 GiB. C++ 동적 경로·맵 참조가 달라지면 `Scripts/audit_runtime_assets.py`를 Unreal Python에서 다시 실행한다. 새 목록만으로 Git에 자동 추가하지 않는다.

| 분류 / 출처 | 용도 | 파일 수 | MiB |
| --- | --- | ---: | ---: |
| [Warehouse / Quixel Megascans](https://www.fab.com/listings/a3149fab-3906-4043-b6ee-3937b752a06c) | 창고 랙·팔레트·바닥·설비와 파생 충돌 | 75 | 1817.07 |
| [Vehicle Variety Pack / Switchboard Studios](https://www.fab.com/listings/dc1ada50-2523-44b1-b0e2-a72d14076fb4) | 물류 트럭과 파생 적재함 | 29 | 323.36 |
| [European Beech / Quixel Megascans](https://www.fab.com/listings/d11cc01d-9422-41b7-950f-416c9ce79caf) | 외부 나무·파생 충돌 메시 | 13 | 897.67 |
| [Free Shrubs Pack / Greenleaf Vision](https://www.fab.com/listings/7ca465ab-fb9c-4d6b-bddb-82c20f604657) | 외부 관목·파생 충돌 메시 | 39 | 257.63 |
| [Boxes & Pallets Pack / IINickE](https://www.fab.com/listings/a7c9776e-0a8e-424b-8f7c-f4d746097f0f) | 온전한 상자와 파생 재질 | 11 | 47.08 |
| [Game Animation Sample / Epic Games](https://www.fab.com/listings/880e319a-a59e-4ed2-b268-b32dac7fa016) | 이동·앉기·점프·착지의 사용 클립과 블렌드 | 32 | 32.84 |
| Unreal 기본 캐릭터·입력·1인칭 템플릿 / Epic Games | 플레이어·NPC·입력·파생 캐릭터 페이드 재질 | 66 | 90.25 |
| Megascans 공통 재질 | 사용 중인 외부 에셋이 참조하는 MSPresets | 27 | 9.22 |
| 프로젝트 구성/팀원 제작·사용자 제공 모델 | AGV·승강기·충전기·컨베이어·장식·UI·공통 표시 재질 | 180 | 40.86 |
| 프로젝트 맵 배치 데이터 | 맵이 참조하는 외부 액터/오브젝트 패키지 | 3481 | 15.59 |

## Warehouse / Quixel Megascans

| 에셋 경로 | 종류 | MiB |
| --- | --- | ---: |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_Fac_Container_Drum_Metal_Worn_01/MI_Ind_Fac_Container_Drum_Metal_Worn_01.uasset` | MaterialInstanceConstant | 0.012 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_Fac_Container_Drum_Metal_Worn_01/T_Ind_Fac_Container_Drum_Metal_Worn_01_D.uasset` | Texture2D | 15.243 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_Fac_Container_Drum_Metal_Worn_01/T_Ind_Fac_Container_Drum_Metal_Worn_01_DpR.uasset` | Texture2D | 5.910 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_Fac_Container_Drum_Metal_Worn_01/T_Ind_Fac_Container_Drum_Metal_Worn_01_N.uasset` | Texture2D | 10.076 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Cabinet_Electric_Metal_Dirty_01/MI_Ind_War_Cabinet_Electric_Metal_Dirty_01.uasset` | MaterialInstanceConstant | 0.016 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Cabinet_Electric_Metal_Dirty_01/T_Ind_War_Cabinet_Electric_Metal_Dirty_01_D.uasset` | Texture2D | 9.196 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Cabinet_Electric_Metal_Dirty_01/T_Ind_War_Cabinet_Electric_Metal_Dirty_01_DpR.uasset` | Texture2D | 8.010 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Cabinet_Electric_Metal_Dirty_01/T_Ind_War_Cabinet_Electric_Metal_Dirty_01_N.uasset` | Texture2D | 12.706 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Equipment_Ladder_Metal_Worn_01/MI_Ind_War_Equipment_Ladder_Metal_Worn_01.uasset` | MaterialInstanceConstant | 0.016 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Equipment_Ladder_Metal_Worn_01/T_Ind_War_Equipment_Ladder_Metal_Worn_01_D.uasset` | Texture2D | 21.803 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Equipment_Ladder_Metal_Worn_01/T_Ind_War_Equipment_Ladder_Metal_Worn_01_DpR.uasset` | Texture2D | 6.558 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Equipment_Ladder_Metal_Worn_01/T_Ind_War_Equipment_Ladder_Metal_Worn_01_N.uasset` | Texture2D | 11.124 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Light_Ceiling_Metal_Hanging_01/MI_Ind_War_Light_Ceiling_Metal_Hanging_01.uasset` | MaterialInstanceConstant | 0.015 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Light_Ceiling_Metal_Hanging_01/T_Ind_War_Light_Ceiling_Metal_Hanging_01_D.uasset` | Texture2D | 1.316 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Light_Ceiling_Metal_Hanging_01/T_Ind_War_Light_Ceiling_Metal_Hanging_01_DpR.uasset` | Texture2D | 0.525 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Light_Ceiling_Metal_Hanging_01/T_Ind_War_Light_Ceiling_Metal_Hanging_01_N.uasset` | Texture2D | 0.829 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Pallet_Shelf_Metal_Modular_01/MI_Ind_War_Pallet_Shelf_Metal_Modular_01.uasset` | MaterialInstanceConstant | 0.016 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Pallet_Shelf_Metal_Modular_01/T_Ind_War_Pallet_Shelf_Metal_Modular_01_D.uasset` | Texture2D | 78.423 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Pallet_Shelf_Metal_Modular_01/T_Ind_War_Pallet_Shelf_Metal_Modular_01_DpR.uasset` | Texture2D | 17.121 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Pallet_Shelf_Metal_Modular_01/T_Ind_War_Pallet_Shelf_Metal_Modular_01_N.uasset` | Texture2D | 51.841 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Storage_Barrel_Plastic_Blue_01/MI_Ind_War_Storage_Barrel_Plastic_Blue_01.uasset` | MaterialInstanceConstant | 0.016 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Storage_Barrel_Plastic_Blue_01/T_Ind_War_Storage_Barrel_Plastic_Blue_01_D.uasset` | Texture2D | 4.978 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Storage_Barrel_Plastic_Blue_01/T_Ind_War_Storage_Barrel_Plastic_Blue_01_DpR.uasset` | Texture2D | 1.819 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Storage_Barrel_Plastic_Blue_01/T_Ind_War_Storage_Barrel_Plastic_Blue_01_N.uasset` | Texture2D | 2.784 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Storage_Box_Cardboard_Worn_02/T_Ind_War_Storage_Box_Cardboard_Worn_02_D.uasset` | Texture2D | 16.664 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Storage_Pallet_Wood_Worn_01/MI_Ind_War_Storage_Pallet_Wood_Worn_01.uasset` | MaterialInstanceConstant | 0.015 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Storage_Pallet_Wood_Worn_01/T_Ind_War_Storage_Pallet_Wood_Worn_01_D.uasset` | Texture2D | 18.988 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Storage_Pallet_Wood_Worn_01/T_Ind_War_Storage_Pallet_Wood_Worn_01_DpR.uasset` | Texture2D | 5.829 |
| `Content/Scene_Warehouse/Assets/MS/3D/Ind_War_Storage_Pallet_Wood_Worn_01/T_Ind_War_Storage_Pallet_Wood_Worn_01_N.uasset` | Texture2D | 14.631 |
| `Content/Scene_Warehouse/Assets/MS/Surfaces/Ind_War_Floor_Concrete_Smooth_01/MI_Ind_War_Floor_Concrete_Smooth_01_A.uasset` | MaterialInstanceConstant | 0.014 |
| `Content/Scene_Warehouse/Assets/MS/Surfaces/Ind_War_Floor_Concrete_Smooth_01/T_Ind_War_Floor_Concrete_Smooth_01_D.uasset` | Texture2D | 16.422 |
| `Content/Scene_Warehouse/Assets/MS/Surfaces/Ind_War_Floor_Concrete_Smooth_01/T_Ind_War_Floor_Concrete_Smooth_01_N.uasset` | Texture2D | 9.706 |
| `Content/Scene_Warehouse/Assets/MS/Surfaces/Ind_War_Floor_Concrete_Smooth_01/T_Ind_War_Floor_Concrete_Smooth_01_ORDp.uasset` | Texture2D | 6.431 |
| `Content/Scene_Warehouse/Assets/MS/Surfaces/Ind_War_Wall_Facade_Concrete_New_01/MI_Ind_War_Wall_Facade_Concrete_New_01_A.uasset` | MaterialInstanceConstant | 0.013 |
| `Content/Scene_Warehouse/Assets/MS/Surfaces/Ind_War_Wall_Facade_Concrete_New_01/T_Ind_War_Wall_Facade_Concrete_New_01_D.uasset` | Texture2D | 18.392 |
| `Content/Scene_Warehouse/Assets/MS/Surfaces/Ind_War_Wall_Facade_Concrete_New_01/T_Ind_War_Wall_Facade_Concrete_New_01_N.uasset` | Texture2D | 10.404 |
| `Content/Scene_Warehouse/Assets/MS/Surfaces/Ind_War_Wall_Facade_Concrete_New_01/T_Ind_War_Wall_Facade_Concrete_New_01_ORDp.uasset` | Texture2D | 3.812 |
| `Content/Scene_Warehouse/Materials/MSPresets/MSVTTextures/BlackPlaceholder.uasset` | Texture2D | 0.004 |
| `Content/Scene_Warehouse/Materials/MSPresets/MSVTTextures/DefaultDiffuse.uasset` | Texture2D | 2.033 |
| `Content/Scene_Warehouse/Materials/MSPresets/MSVTTextures/Placeholder_Normal.uasset` | Texture2D | 0.005 |
| `Content/Scene_Warehouse/Materials/MSPresets/MSVTTextures/WhitePlaceholder.uasset` | Texture2D | 0.004 |
| `Content/Scene_Warehouse/Materials/MSPresets/M_MS_Default_Material_VT/Functions/MF_ObjAdjustments.uasset` | MaterialFunction | 0.037 |
| `Content/Scene_Warehouse/Materials/MSPresets/M_MS_Default_Material_VT/M_MS_Default_Material_VT.uasset` | Material | 0.023 |
| `Content/Scene_Warehouse/Materials/MSPresets/M_MS_Surface_Material_VT/Functions/MF_MapAdjustments.uasset` | MaterialFunction | 0.046 |
| `Content/Scene_Warehouse/Materials/MSPresets/M_MS_Surface_Material_VT/Functions/MF_Tiling.uasset` | MaterialFunction | 0.018 |
| `Content/Scene_Warehouse/Materials/MSPresets/M_MS_Surface_Material_VT/M_MS_Surface_Material_VT.uasset` | Material | 0.026 |
| `Content/Warehouse/Collision/SM_Ind_Fac_Container_Drum_Metal_Worn_01.uasset` | StaticMesh | 131.387 |
| `Content/Warehouse/Collision/SM_Ind_War_Cabinet_Electric_Metal_Dirty_01.uasset` | StaticMesh | 144.741 |
| `Content/Warehouse/Collision/SM_Ind_War_Equipment_Ladder_Metal_Worn_01.uasset` | StaticMesh | 74.059 |
| `Content/Warehouse/Collision/SM_Ind_War_Light_Ceiling_Metal_Hanging_01.uasset` | StaticMesh | 61.733 |
| `Content/Warehouse/Collision/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_A.uasset` | StaticMesh | 16.530 |
| `Content/Warehouse/Collision/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_B.uasset` | StaticMesh | 19.655 |
| `Content/Warehouse/Collision/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_C.uasset` | StaticMesh | 16.011 |
| `Content/Warehouse/Collision/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_D.uasset` | StaticMesh | 16.518 |
| `Content/Warehouse/Collision/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_E.uasset` | StaticMesh | 19.546 |
| `Content/Warehouse/Collision/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_F.uasset` | StaticMesh | 15.910 |
| `Content/Warehouse/Collision/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_G.uasset` | StaticMesh | 29.107 |
| `Content/Warehouse/Collision/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_H.uasset` | StaticMesh | 56.649 |
| `Content/Warehouse/Collision/SM_Ind_War_Storage_Barrel_Plastic_Blue_01.uasset` | StaticMesh | 71.573 |
| `Content/Warehouse/Materials/ObserverWalls/MI_ObserverWall_MI_Ind_War_Wall_Facade_Concrete_New_01_A_ae42f6ffe8.uasset` | MaterialInstanceConstant | 0.013 |
| `Content/Warehouse/Materials/ObserverWalls/M_ObserverWall_M_MS_Surface_Material_VT_7893f6a073.uasset` | Material | 0.033 |
| `Content/Warehouse/Physics/SM_Ind_Fac_Container_Drum_Metal_Worn_01.uasset` | StaticMesh | 131.392 |
| `Content/Warehouse/Physics/SM_Ind_War_Cabinet_Electric_Metal_Dirty_01.uasset` | StaticMesh | 144.774 |
| `Content/Warehouse/Physics/SM_Ind_War_Equipment_Ladder_Metal_Worn_01.uasset` | StaticMesh | 74.081 |
| `Content/Warehouse/Physics/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_A.uasset` | StaticMesh | 16.537 |
| `Content/Warehouse/Physics/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_B.uasset` | StaticMesh | 19.662 |
| `Content/Warehouse/Physics/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_C.uasset` | StaticMesh | 16.018 |
| `Content/Warehouse/Physics/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_D.uasset` | StaticMesh | 16.526 |
| `Content/Warehouse/Physics/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_E.uasset` | StaticMesh | 19.553 |
| `Content/Warehouse/Physics/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_F.uasset` | StaticMesh | 15.918 |
| `Content/Warehouse/Physics/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_G.uasset` | StaticMesh | 29.138 |
| `Content/Warehouse/Physics/SM_Ind_War_Pallet_Shelf_Metal_Modular_01_H.uasset` | StaticMesh | 56.680 |
| `Content/Warehouse/Physics/SM_Ind_War_Storage_Barrel_Plastic_Blue_01.uasset` | StaticMesh | 71.578 |
| `Content/Warehouse/Physics/SM_Ind_War_Storage_Pallet_Wood_Worn_01.uasset` | StaticMesh | 73.945 |
| `Content/Warehouse/Physics/SM_Pallet_110.uasset` | StaticMesh | 73.933 |

## Vehicle Variety Pack / Switchboard Studios

| 에셋 경로 | 종류 | MiB |
| --- | --- | ---: |
| `Content/VehicleVarietyPack/Materials/BoxTruck/M_BoxTruck_Body.uasset` | Material | 0.138 |
| `Content/VehicleVarietyPack/Materials/BoxTruck/M_BoxTruck_Box.uasset` | Material | 0.149 |
| `Content/VehicleVarietyPack/Materials/BoxTruck/M_BoxTruck_Chassis.uasset` | Material | 0.151 |
| `Content/VehicleVarietyPack/Materials/BoxTruck/M_BoxTruck_Interior.uasset` | Material | 0.157 |
| `Content/VehicleVarietyPack/Materials/BoxTruck/Mi_BoxTruck_Glass.uasset` | MaterialInstanceConstant | 0.123 |
| `Content/VehicleVarietyPack/Materials/Masters/M_Glass.uasset` | Material | 0.105 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Box_Diffuse.uasset` | Texture2D | 16.842 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Box_Normal.uasset` | Texture2D | 16.798 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Box_ORM.uasset` | Texture2D | 21.090 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Chassis_Diffuse.uasset` | Texture2D | 13.292 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Chassis_EMM.uasset` | Texture2D | 0.094 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Chassis_Normal.uasset` | Texture2D | 20.593 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Chassis_ORM.uasset` | Texture2D | 19.128 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Exterior_Diffuse.uasset` | Texture2D | 17.482 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Exterior_EMM.uasset` | Texture2D | 0.082 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Exterior_Normal.uasset` | Texture2D | 18.431 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Exterior_ORM.uasset` | Texture2D | 20.319 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Glass_Diffuse.uasset` | Texture2D | 11.273 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Glass_Normal.uasset` | Texture2D | 9.113 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Glass_OPC.uasset` | Texture2D | 6.158 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Glass_ORM.uasset` | Texture2D | 9.664 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Interior_Diffuse.uasset` | Texture2D | 13.505 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Interior_Normal.uasset` | Texture2D | 32.022 |
| `Content/VehicleVarietyPack/Textures/BoxTruck/TX_Truck_Interior_ORM.uasset` | Texture2D | 21.252 |
| `Content/VehicleVarietyPack/Textures/Pickup/TX_Pickup_Diffuse_2.uasset` | Texture2D | 19.573 |
| `Content/VehicleVarietyPack/Textures/Pickup/TX_Pickup_Normal_2.uasset` | Texture2D | 10.136 |
| `Content/VehicleVarietyPack/Textures/Pickup/TX_Pickup_OPC_2.uasset` | Texture2D | 1.756 |
| `Content/VehicleVarietyPack/Textures/Pickup/TX_Pickup_ORM.uasset` | Texture2D | 18.860 |
| `Content/Warehouse/Exterior/TruckInterior/SM_Truck_Open.uasset` | StaticMesh | 5.070 |

## European Beech / Quixel Megascans

| 에셋 경로 | 종류 | MiB |
| --- | --- | ---: |
| `Content/EuropeanBeech/Materials/Simplewind/MAI_EuropeanBeech.uasset` | MaterialInstanceConstant | 0.012 |
| `Content/EuropeanBeech/Materials/Simplewind/MI_EuropeanBeech_Tileable.uasset` | MaterialInstanceConstant | 0.019 |
| `Content/EuropeanBeech/Materials/Simplewind/MI_EuropeanBeech_TwoSided.uasset` | MaterialInstanceConstant | 0.038 |
| `Content/EuropeanBeech/Textures/Tileable/T_EuropeanBeech_Tileable_Albedo.uasset` | Texture2D | 84.046 |
| `Content/EuropeanBeech/Textures/Tileable/T_EuropeanBeech_Tileable_Mask.uasset` | Texture2D | 249.661 |
| `Content/EuropeanBeech/Textures/Tileable/T_EuropeanBeech_Tileable_Normal.uasset` | Texture2D | 259.884 |
| `Content/EuropeanBeech/Textures/TwoSided/Summer/T_EuropeanBeech_Twosided_Summer_Albedo.uasset` | Texture2D | 48.251 |
| `Content/EuropeanBeech/Textures/TwoSided/Summer/T_EuropeanBeech_Twosided_Summer_Mask.uasset` | Texture2D | 117.263 |
| `Content/EuropeanBeech/Textures/TwoSided/Summer/T_EuropeanBeech_Twosided_Summer_Normal.uasset` | Texture2D | 100.113 |
| `Content/EuropeanBeech/Textures/TwoSided/Winter/T_EuropeanBeech_TwoSided_Winter_Albedo.uasset` | Texture2D | 7.131 |
| `Content/EuropeanBeech/Textures/TwoSided/Winter/T_EuropeanBeech_TwoSided_Winter_Mask.uasset` | Texture2D | 7.354 |
| `Content/EuropeanBeech/Textures/TwoSided/Winter/T_EuropeanBeech_TwoSided_Winter_Normal.uasset` | Texture2D | 18.895 |
| `Content/Warehouse/Collision/SM_EuropeanBeech_Sapling_01.uasset` | StaticMesh | 5.002 |

## Free Shrubs Pack / Greenleaf Vision

| 에셋 경로 | 종류 | MiB |
| --- | --- | ---: |
| `Content/GV_FreeShrubsPack/Materials/ColorVari/MF/MF_FoliageColorVariation_G2B.uasset` | MaterialFunction | 0.027 |
| `Content/GV_FreeShrubsPack/Materials/MASTER/M_Shrub_Bark_Master_Wind.uasset` | Material | 0.048 |
| `Content/GV_FreeShrubsPack/Materials/MASTER/M_Shrub_Leaf_A_Master_Wind_Opaque.uasset` | Material | 0.063 |
| `Content/GV_FreeShrubsPack/Materials/MASTER/Master_Instance/MI_Shrub_Bark_Master_Wind_Inst.uasset` | MaterialInstanceConstant | 0.022 |
| `Content/GV_FreeShrubsPack/Materials/MASTER/Master_Instance/MI_Shrub_Leaf_A_Master_Wind_Opaque_Inst.uasset` | MaterialInstanceConstant | 0.019 |
| `Content/GV_FreeShrubsPack/Materials/Material_Functions/MF_3LV.uasset` | MaterialFunction | 0.070 |
| `Content/GV_FreeShrubsPack/Materials/Material_Functions/MF_Influence.uasset` | MaterialFunction | 0.067 |
| `Content/GV_FreeShrubsPack/Materials/Material_Functions/MF_PivotPainter2_DecodeAxisVector.uasset` | MaterialFunction | 0.008 |
| `Content/GV_FreeShrubsPack/Materials/Material_Functions/MF_PivotPainter2_ReturnParentTextureInfo.uasset` | MaterialFunction | 0.016 |
| `Content/GV_FreeShrubsPack/Materials/Material_Functions/MF_Wind_LV.uasset` | MaterialFunction | 0.094 |
| `Content/GV_FreeShrubsPack/Materials/Material_Functions/MF_Wind_LV_Leaves.uasset` | MaterialFunction | 0.049 |
| `Content/GV_FreeShrubsPack/Materials/Shrubs/I/MI_Shrub_I_Bark_Master_Wind_LV3_Inst.uasset` | MaterialInstanceConstant | 0.026 |
| `Content/GV_FreeShrubsPack/Materials/Shrubs/I/MI_Shrub_Leaf_A_Master_Wind_LV3_Opaque_Inst.uasset` | MaterialInstanceConstant | 0.024 |
| `Content/GV_FreeShrubsPack/Materials/Snow/MF_Vol1_Winter_MW_T.uasset` | MaterialFunction | 0.017 |
| `Content/GV_FreeShrubsPack/Materials/Snow/MF_Vol1_Winter_P.uasset` | MaterialFunction | 0.017 |
| `Content/GV_FreeShrubsPack/Materials/WindController/GV_trees_mw_WindParameters.uasset` | MaterialParameterCollection | 0.006 |
| `Content/GV_FreeShrubsPack/Textures/ColorVari/C/T_Color_Vari_B.uasset` | Texture2D | 0.106 |
| `Content/GV_FreeShrubsPack/Textures/ColorVari/C/T_Color_Vari_E.uasset` | Texture2D | 0.055 |
| `Content/GV_FreeShrubsPack/Textures/ColorVari/C/T_Color_Vari_K.uasset` | Texture2D | 0.087 |
| `Content/GV_FreeShrubsPack/Textures/Master/T_flat_black.uasset` | Texture2D | 0.005 |
| `Content/GV_FreeShrubsPack/Textures/Master/T_flat_grey.uasset` | Texture2D | 0.005 |
| `Content/GV_FreeShrubsPack/Textures/Master/T_flat_normal.uasset` | Texture2D | 0.006 |
| `Content/GV_FreeShrubsPack/Textures/Master/T_flat_white.uasset` | Texture2D | 0.005 |
| `Content/GV_FreeShrubsPack/Textures/Shrubs/Bark_A/T_Bark_A_diffuse.uasset` | Texture2D | 4.529 |
| `Content/GV_FreeShrubsPack/Textures/Shrubs/Bark_A/T_Bark_A_normal.uasset` | Texture2D | 16.719 |
| `Content/GV_FreeShrubsPack/Textures/Shrubs/Bark_A/T_Bark_A_rough.uasset` | Texture2D | 4.009 |
| `Content/GV_FreeShrubsPack/Textures/Shrubs/Bark_A/T_Bark_D_Winter.uasset` | Texture2D | 4.524 |
| `Content/GV_FreeShrubsPack/Textures/Shrubs/Bark_A/T_Roughness_BB.uasset` | Texture2D | 8.216 |
| `Content/GV_FreeShrubsPack/Textures/Shrubs/Bark_A/T_bark_A_ao.uasset` | Texture2D | 2.295 |
| `Content/GV_FreeShrubsPack/Textures/Shrubs/Leaves/T_GV_Shrub_Typ1_Albedo.uasset` | Texture2D | 14.008 |
| `Content/GV_FreeShrubsPack/Textures/Shrubs/Leaves/T_GV_Shrub_Typ1_N.uasset` | Texture2D | 84.327 |
| `Content/GV_FreeShrubsPack/Textures/Shrubs/Leaves/T_GV_Shrub_Typ2_Albedo.uasset` | Texture2D | 3.616 |
| `Content/GV_FreeShrubsPack/Textures/Shrubs/Leaves/T_GV_Shrub_Typ2_N.uasset` | Texture2D | 28.060 |
| `Content/GV_FreeShrubsPack/Textures/Shrubs/VecT/T_Wind_Shrub_I_PP_PII_UV2.uasset` | Texture2D | 0.090 |
| `Content/GV_FreeShrubsPack/Textures/Shrubs/VecT/T_Wind_Shrub_I_XVec_UV2.uasset` | Texture2D | 0.110 |
| `Content/GV_FreeShrubsPack/Textures/Wind/T_Default_RGB_PivotPos_A_ParentIndexInt_UV2.uasset` | Texture2D | 0.004 |
| `Content/GV_FreeShrubsPack/Textures/Wind/T_Default_RGB_XVector_A_XExtentDividedbyRange_UV2.uasset` | Texture2D | 0.004 |
| `Content/GV_FreeShrubsPack/Textures/Wind/T_Wind_Variation.uasset` | Texture2D | 10.538 |
| `Content/Warehouse/Collision/GV_Vol7_Shrub_I_full_type1.uasset` | StaticMesh | 75.739 |

## Boxes & Pallets Pack / IINickE

| 에셋 경로 | 종류 | MiB |
| --- | --- | ---: |
| `Content/Warehouse/Cargo/BoxesPalletsPack/HeavyTest/Materials/MI_HeavyTest_01_M_Box_worn.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Cargo/BoxesPalletsPack/HeavyTest/Materials/MI_HeavyTest_02_M_Box_worn.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Cargo/BoxesPalletsPack/HeavyTest/Materials/MI_HeavyTest_03_M_Box_worn.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Cargo/BoxesPalletsPack/HeavyTest/Materials/MI_HeavyTest_04_M_Box_worn.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Cargo/BoxesPalletsPack/HeavyTest/Materials/M_HeavyTint_M_Box_worn.uasset` | Material | 0.008 |
| `Content/Warehouse/Cargo/BoxesPalletsPack/Materials/M_Box_worn.uasset` | Material | 0.007 |
| `Content/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_1.uasset` | StaticMesh | 0.019 |
| `Content/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2.uasset` | StaticMesh | 0.019 |
| `Content/Warehouse/Cargo/BoxesPalletsPack/Textures/T_Boxes_and_pallets_Box_ColorV2.uasset` | Texture2D | 28.617 |
| `Content/Warehouse/Cargo/BoxesPalletsPack/Textures/T_Boxes_and_pallets_Box_Normal.uasset` | Texture2D | 13.484 |
| `Content/Warehouse/Cargo/BoxesPalletsPack/Textures/T_Boxes_and_pallets_Box_Roughness.uasset` | Texture2D | 4.915 |

## Game Animation Sample / Epic Games

| 에셋 경로 | 종류 | MiB |
| --- | --- | ---: |
| `Content/Warehouse/AnimationSample/BS_WarehouseCrouching.uasset` | BlendSpace | 0.032 |
| `Content/Warehouse/AnimationSample/BS_WarehouseStanding.uasset` | BlendSpace | 0.073 |
| `Content/Warehouse/AnimationSample/Sequences/M_Neutral_Crouch_Idle_Loop.uasset` | AnimSequence | 1.972 |
| `Content/Warehouse/AnimationSample/Sequences/M_Neutral_Jump_F_Land_Stand_Light_Lfoot.uasset` | AnimSequence | 0.616 |
| `Content/Warehouse/AnimationSample/Sequences/M_Neutral_Jump_F_Start_Stand_Lfoot.uasset` | AnimSequence | 0.564 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Crouch_Loop_B.uasset` | AnimSequence | 1.162 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Crouch_Loop_BL.uasset` | AnimSequence | 1.166 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Crouch_Loop_BR.uasset` | AnimSequence | 1.165 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Crouch_Loop_F.uasset` | AnimSequence | 1.166 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Crouch_Loop_FL.uasset` | AnimSequence | 1.172 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Crouch_Loop_FR.uasset` | AnimSequence | 1.136 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Crouch_Loop_LL.uasset` | AnimSequence | 1.167 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Crouch_Loop_RR.uasset` | AnimSequence | 1.173 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Jump_Loop_Fall.uasset` | AnimSequence | 1.039 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Run_Loop_B.uasset` | AnimSequence | 0.888 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Run_Loop_BL.uasset` | AnimSequence | 0.881 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Run_Loop_BR.uasset` | AnimSequence | 0.881 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Run_Loop_F.uasset` | AnimSequence | 0.937 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Run_Loop_FL.uasset` | AnimSequence | 0.929 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Run_Loop_FR.uasset` | AnimSequence | 0.930 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Run_Loop_LL.uasset` | AnimSequence | 0.888 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Run_Loop_RR_offset.uasset` | AnimSequence | 0.882 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Sprint_Loop_F.uasset` | AnimSequence | 0.850 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Stand_Idle_Loop.uasset` | AnimSequence | 2.282 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Walk_Loop_B.uasset` | AnimSequence | 1.085 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Walk_Loop_BL.uasset` | AnimSequence | 1.089 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Walk_Loop_BR.uasset` | AnimSequence | 1.089 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Walk_Loop_F.uasset` | AnimSequence | 1.133 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Walk_Loop_FL.uasset` | AnimSequence | 1.136 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Walk_Loop_FR.uasset` | AnimSequence | 1.136 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Walk_Loop_LL.uasset` | AnimSequence | 1.091 |
| `Content/Warehouse/AnimationSample/Sequences/M_Relaxed_Walk_Loop_RR_offset.uasset` | AnimSequence | 1.127 |

## Unreal 기본 캐릭터·입력·1인칭 템플릿 / Epic Games

| 에셋 경로 | 종류 | MiB |
| --- | --- | ---: |
| `Content/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.uasset` | AnimBlueprint | 0.368 |
| `Content/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run.uasset` | BlendSpace | 0.050 |
| `Content/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Bwd.uasset` | AnimSequence | 0.461 |
| `Content/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Bwd_Left.uasset` | AnimSequence | 0.398 |
| `Content/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Bwd_Right.uasset` | AnimSequence | 0.398 |
| `Content/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd.uasset` | AnimSequence | 0.507 |
| `Content/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd_Left.uasset` | AnimSequence | 0.445 |
| `Content/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd_Right.uasset` | AnimSequence | 0.446 |
| `Content/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Left.uasset` | AnimSequence | 0.465 |
| `Content/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Right.uasset` | AnimSequence | 0.498 |
| `Content/Characters/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop.uasset` | AnimSequence | 0.882 |
| `Content/Characters/Mannequins/Anims/Unarmed/Jump/MM_Jump.uasset` | AnimSequence | 0.385 |
| `Content/Characters/Mannequins/Anims/Unarmed/Jump/MM_Land.uasset` | AnimSequence | 0.391 |
| `Content/Characters/Mannequins/Anims/Unarmed/MM_Idle.uasset` | AnimSequence | 1.370 |
| `Content/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Bwd.uasset` | AnimSequence | 0.579 |
| `Content/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Bwd_Left.uasset` | AnimSequence | 0.513 |
| `Content/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Bwd_Right.uasset` | AnimSequence | 0.513 |
| `Content/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.uasset` | AnimSequence | 0.454 |
| `Content/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd_Left.uasset` | AnimSequence | 0.392 |
| `Content/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd_Right.uasset` | AnimSequence | 0.392 |
| `Content/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Left.uasset` | AnimSequence | 0.464 |
| `Content/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Right.uasset` | AnimSequence | 0.469 |
| `Content/Characters/Mannequins/Materials/M_Mannequin.uasset` | Material | 0.057 |
| `Content/Characters/Mannequins/Materials/Manny/MI_Manny_01_New.uasset` | MaterialInstanceConstant | 0.017 |
| `Content/Characters/Mannequins/Materials/Manny/MI_Manny_02_New.uasset` | MaterialInstanceConstant | 0.018 |
| `Content/Characters/Mannequins/Materials/Quinn/MI_Quinn_01.uasset` | MaterialInstanceConstant | 0.011 |
| `Content/Characters/Mannequins/Materials/Quinn/MI_Quinn_02.uasset` | MaterialInstanceConstant | 0.010 |
| `Content/Characters/Mannequins/Meshes/SKM_Manny_Simple.uasset` | SkeletalMesh | 15.092 |
| `Content/Characters/Mannequins/Meshes/SKM_Quinn_Simple.uasset` | SkeletalMesh | 15.500 |
| `Content/Characters/Mannequins/Meshes/SK_Mannequin.uasset` | Skeleton | 0.182 |
| `Content/Characters/Mannequins/Rigs/CR_Mannequin_Body.uasset` | ControlRigBlueprint | 16.267 |
| `Content/Characters/Mannequins/Rigs/CR_Mannequin_FootIK.uasset` | ControlRigBlueprint | 0.476 |
| `Content/Characters/Mannequins/Rigs/PA_Mannequin.uasset` | PhysicsAsset | 0.288 |
| `Content/Characters/Mannequins/Textures/Manny/T_Manny_01_BN.uasset` | Texture2D | 1.357 |
| `Content/Characters/Mannequins/Textures/Manny/T_Manny_01_D.uasset` | Texture2D | 0.667 |
| `Content/Characters/Mannequins/Textures/Manny/T_Manny_01_MRA.uasset` | Texture2D | 0.680 |
| `Content/Characters/Mannequins/Textures/Manny/T_Manny_02_BN.uasset` | Texture2D | 20.027 |
| `Content/Characters/Mannequins/Textures/Manny/T_Manny_02_D.uasset` | Texture2D | 0.989 |
| `Content/Characters/Mannequins/Textures/Manny/T_Manny_02_MRA.uasset` | Texture2D | 1.010 |
| `Content/Characters/Mannequins/Textures/Quinn/T_Quinn_01_D.uasset` | Texture2D | 0.644 |
| `Content/Characters/Mannequins/Textures/Quinn/T_Quinn_01_MRA.uasset` | Texture2D | 0.755 |
| `Content/Characters/Mannequins/Textures/Quinn/T_Quinn_01_N.uasset` | Texture2D | 1.205 |
| `Content/Characters/Mannequins/Textures/Quinn/T_Quinn_02_D.uasset` | Texture2D | 0.900 |
| `Content/Characters/Mannequins/Textures/Quinn/T_Quinn_02_MRA.uasset` | Texture2D | 0.929 |
| `Content/Characters/Mannequins/Textures/Quinn/T_Quinn_02_N.uasset` | Texture2D | 1.454 |
| `Content/Characters/Mannequins/Textures/Shared/T_UE_Logo_M.uasset` | Texture2D | 0.057 |
| `Content/FirstPerson/Anims/ABP_FP_Copy.uasset` | AnimBlueprint | 0.055 |
| `Content/FirstPerson/Anims/CtrlRig_FPWarp.uasset` | ControlRigBlueprint | 0.237 |
| `Content/FirstPerson/Blueprints/BP_FirstPersonCharacter.uasset` | Blueprint | 0.055 |
| `Content/FirstPerson/Blueprints/BP_FirstPersonGameMode.uasset` | Blueprint | 0.021 |
| `Content/FirstPerson/Blueprints/BP_FirstPersonPlayerController.uasset` | Blueprint | 0.021 |
| `Content/FirstPerson/Lvl_FirstPerson.umap` | World | 0.020 |
| `Content/Input/Actions/IA_Jump.uasset` | InputAction | 0.002 |
| `Content/Input/Actions/IA_Look.uasset` | InputAction | 0.001 |
| `Content/Input/Actions/IA_MouseLook.uasset` | InputAction | 0.001 |
| `Content/Input/Actions/IA_Move.uasset` | InputAction | 0.001 |
| `Content/Input/IMC_Default.uasset` | InputMappingContext | 0.007 |
| `Content/Input/IMC_MouseLook.uasset` | InputMappingContext | 0.002 |
| `Content/Input/Touch/BPI_TouchInterface.uasset` | Blueprint | 0.016 |
| `Content/Input/Touch/UI_Thumbstick.uasset` | WidgetBlueprint | 0.213 |
| `Content/Input/Touch/UI_TouchSimple.uasset` | WidgetBlueprint | 0.067 |
| `Content/Warehouse/Materials/RemoteCharacter/MI_RemoteFade_MI_Manny_01_New.uasset` | MaterialInstanceConstant | 0.014 |
| `Content/Warehouse/Materials/RemoteCharacter/MI_RemoteFade_MI_Manny_02_New.uasset` | MaterialInstanceConstant | 0.009 |
| `Content/Warehouse/Materials/RemoteCharacter/MI_RemoteFade_MI_Quinn_01.uasset` | MaterialInstanceConstant | 0.011 |
| `Content/Warehouse/Materials/RemoteCharacter/MI_RemoteFade_MI_Quinn_02.uasset` | MaterialInstanceConstant | 0.011 |
| `Content/Warehouse/Materials/RemoteCharacter/M_RemoteFade_M_Mannequin.uasset` | Material | 0.056 |

## Megascans 공통 재질

| 에셋 경로 | 종류 | MiB |
| --- | --- | ---: |
| `Content/MSPresets/MS_Foliage_Material/MasterMaterials/MA_Foliage.uasset` | Material | 0.026 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_AdvancedWind.uasset` | MaterialFunction | 0.120 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_AdvancedWind_Level.uasset` | MaterialFunction | 0.077 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_BarkDetailer.uasset` | MaterialFunction | 0.104 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_BranchBlending.uasset` | MaterialFunction | 0.107 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_DecorationBlending.uasset` | MaterialFunction | 0.105 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_DefaultLit_Trunk.uasset` | MaterialFunction | 0.119 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_GenerateBranchMask.uasset` | MaterialFunction | 0.099 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_HealthSeason.uasset` | MaterialFunction | 0.142 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_LightInfluenceColorVariation.uasset` | MaterialFunction | 0.121 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_Opacity.uasset` | MaterialFunction | 0.014 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_Roughness.uasset` | MaterialFunction | 0.106 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_SimpleWind.uasset` | MaterialFunction | 0.131 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_TwoSidedRoughness.uasset` | MaterialFunction | 0.122 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_TwoSided_Decoration.uasset` | MaterialFunction | 0.118 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_TwoSided_Leaves.uasset` | MaterialFunction | 0.043 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_WindGust.uasset` | MaterialFunction | 0.120 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_WorldspaceColorVariation.uasset` | MaterialFunction | 0.120 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_adjustBaseColor.uasset` | MaterialFunction | 0.107 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_adjustNormal.uasset` | MaterialFunction | 0.100 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_adjustTwoSidedBaseColor.uasset` | MaterialFunction | 0.129 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_adjustTwoSidedNormal.uasset` | MaterialFunction | 0.101 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_generateSpecular.uasset` | MaterialFunction | 0.104 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_generateTranslucency.uasset` | MaterialFunction | 0.107 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Foliage/MF_generateTwoSidedSpecular.uasset` | MaterialFunction | 0.116 |
| `Content/MSPresets/MS_Foliage_Material/MaterialFunctions/Textures/T_WindNoise.uasset` | Texture2D | 6.654 |
| `Content/MSPresets/MS_Foliage_Material/MaterialParameterCollection/MPC_GlobalFoliageActor.uasset` | MaterialParameterCollection | 0.004 |

## 프로젝트 구성/팀원 제작·사용자 제공 모델

| 에셋 경로 | 종류 | MiB |
| --- | --- | ---: |
| `Content/Warehouse/AGV/Materials/M_Black_rubber.uasset` | Material | 0.012 |
| `Content/Warehouse/AGV/Materials/M_Control_enclosure_-_charcoal.uasset` | Material | 0.012 |
| `Content/Warehouse/AGV/Materials/M_Emergency_stop_red.uasset` | Material | 0.013 |
| `Content/Warehouse/AGV/Materials/M_Graphite_powder-coated_steel.uasset` | Material | 0.012 |
| `Content/Warehouse/AGV/Materials/M_Hydraulic_piston_chrome.uasset` | Material | 0.012 |
| `Content/Warehouse/AGV/Materials/M_Machined_steel.uasset` | Material | 0.015 |
| `Content/Warehouse/AGV/Materials/M_Optical_lens.uasset` | Material | 0.011 |
| `Content/Warehouse/AGV/Materials/M_Polyurethane_roller.uasset` | Material | 0.013 |
| `Content/Warehouse/AGV/Materials/M_Safety_orange_-_painted_body.uasset` | Material | 0.013 |
| `Content/Warehouse/AGV/Materials/M_Safety_warning_yellow.uasset` | Material | 0.014 |
| `Content/Warehouse/AGV/Materials/M_Screen_blue.uasset` | Material | 0.014 |
| `Content/Warehouse/AGV/Materials/M_Sensor_status_green.uasset` | Material | 0.014 |
| `Content/Warehouse/AGV/Materials/M_White_print.uasset` | Material | 0.013 |
| `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_Body.uasset` | StaticMesh | 1.295 |
| `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_Carriage.uasset` | StaticMesh | 0.052 |
| `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_DriveSteer.uasset` | StaticMesh | 0.096 |
| `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_DriveWheel.uasset` | StaticMesh | 0.218 |
| `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_ForkL.uasset` | StaticMesh | 0.047 |
| `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_ForkR.uasset` | StaticMesh | 0.046 |
| `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_LiftChains.uasset` | StaticMesh | 0.325 |
| `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_LiftPulley.uasset` | StaticMesh | 0.041 |
| `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_LiftRam.uasset` | StaticMesh | 0.041 |
| `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_LiftStage.uasset` | StaticMesh | 0.029 |
| `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_LoadWheelL.uasset` | StaticMesh | 0.254 |
| `Content/Warehouse/AGV/Meshes/SM_Refined_AGV_LoadWheelR.uasset` | StaticMesh | 0.254 |
| `Content/Warehouse/ChargingStation/Materials/M_Charger_bolt_steel.uasset` | Material | 0.010 |
| `Content/Warehouse/ChargingStation/Materials/M_Charger_brass_contact.uasset` | Material | 0.011 |
| `Content/Warehouse/ChargingStation/Materials/M_Charger_emergency_stop.uasset` | Material | 0.010 |
| `Content/Warehouse/ChargingStation/Materials/M_Charger_gunmetal_frame.uasset` | Material | 0.010 |
| `Content/Warehouse/ChargingStation/Materials/M_Charger_powder_coat.uasset` | Material | 0.011 |
| `Content/Warehouse/ChargingStation/Materials/M_Charger_printed_ivory.uasset` | Material | 0.011 |
| `Content/Warehouse/ChargingStation/Materials/M_Charger_recess_rubber.uasset` | Material | 0.010 |
| `Content/Warehouse/ChargingStation/Materials/M_Charger_safety_amber.uasset` | Material | 0.011 |
| `Content/Warehouse/ChargingStation/Materials/M_Charger_sensor_glass.uasset` | Material | 0.010 |
| `Content/Warehouse/ChargingStation/Materials/M_Charger_status_mint.uasset` | Material | 0.011 |
| `Content/Warehouse/ChargingStation/Meshes/SM_Charger_GameCabinet_Final.uasset` | StaticMesh | 0.468 |
| `Content/Warehouse/ChargingStation/Meshes/SM_Charger_GameContact_Final.uasset` | StaticMesh | 0.561 |
| `Content/Warehouse/ChargingStation/Meshes/SM_Charger_GameStatus_Final.uasset` | StaticMesh | 0.079 |
| `Content/Warehouse/Conveyor/Meshes/SM_Conveyor_Frame.uasset` | StaticMesh | 0.508 |
| `Content/Warehouse/Conveyor/Meshes/SM_Conveyor_Roller.uasset` | StaticMesh | 0.147 |
| `Content/Warehouse/Conveyor/Meshes/SM_Conveyor_TurntableBase.uasset` | StaticMesh | 0.706 |
| `Content/Warehouse/Conveyor/Meshes/SM_Conveyor_TurntableRotor.uasset` | StaticMesh | 0.532 |
| `Content/Warehouse/Elevator/Materials/M_ELV_Black_rubber.uasset` | Material | 0.009 |
| `Content/Warehouse/Elevator/Materials/M_ELV_Brushed_stainless.uasset` | Material | 0.010 |
| `Content/Warehouse/Elevator/Materials/M_ELV_Display_glass.uasset` | Material | 0.009 |
| `Content/Warehouse/Elevator/Materials/M_ELV_Emergency_red.uasset` | Material | 0.010 |
| `Content/Warehouse/Elevator/Materials/M_ELV_Graphite_frame.uasset` | Material | 0.009 |
| `Content/Warehouse/Elevator/Materials/M_ELV_Indicator_mint.uasset` | Material | 0.011 |
| `Content/Warehouse/Elevator/Materials/M_ELV_Loading_deck_steel.uasset` | Material | 0.010 |
| `Content/Warehouse/Elevator/Materials/M_ELV_Safety_yellow.uasset` | Material | 0.010 |
| `Content/Warehouse/Elevator/Materials/M_ELV_Satin_door_steel.uasset` | Material | 0.010 |
| `Content/Warehouse/Elevator/Materials/M_ELV_White_lettering.uasset` | Material | 0.010 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Bollards_L1.uasset` | StaticMesh | 0.582 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Bollards_L2.uasset` | StaticMesh | 0.580 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Bollards_L3.uasset` | StaticMesh | 0.580 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Bollards_L4.uasset` | StaticMesh | 0.147 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_CabinShell.uasset` | StaticMesh | 0.512 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_CallStand_L1.uasset` | StaticMesh | 0.128 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_CallStand_L2.uasset` | StaticMesh | 0.136 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_CallStand_L3.uasset` | StaticMesh | 0.135 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_CallStand_L4.uasset` | StaticMesh | 0.130 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Gate_0_-1.uasset` | StaticMesh | 0.522 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Gate_0_1.uasset` | StaticMesh | 0.522 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Gate_1_-1.uasset` | StaticMesh | 0.520 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Gate_1_1.uasset` | StaticMesh | 0.520 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Gate_2_-1.uasset` | StaticMesh | 0.520 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Gate_2_1.uasset` | StaticMesh | 0.520 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Gate_3_-1.uasset` | StaticMesh | 0.068 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Gate_3_1.uasset` | StaticMesh | 0.068 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_GuideRails.uasset` | StaticMesh | 0.467 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Landing_L1.uasset` | StaticMesh | 0.664 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Landing_L2.uasset` | StaticMesh | 0.641 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Landing_L3.uasset` | StaticMesh | 0.644 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_Landing_L4.uasset` | StaticMesh | 0.195 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_LiftPlatform.uasset` | StaticMesh | 0.499 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_ShaftEnclosure.uasset` | StaticMesh | 0.454 |
| `Content/Warehouse/Elevator/Meshes/SM_ELV_ShaftFrame.uasset` | StaticMesh | 0.498 |
| `Content/Warehouse/Exterior/Materials/MI_BlueBand.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Exterior/Materials/MI_Cladding.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Exterior/Materials/MI_Grass.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Exterior/Materials/MI_Red.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Exterior/Materials/MI_Steel.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Exterior/Materials/MI_Window.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Exterior/Materials/M_Asphalt.uasset` | Material | 0.006 |
| `Content/Warehouse/Materials/MI_DarkSteel.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Materials/MI_Rubber.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Materials/MI_SafetyYellow.uasset` | MaterialInstanceConstant | 0.003 |
| `Content/Warehouse/Materials/M_CarryTransparent.uasset` | Material | 0.024 |
| `Content/Warehouse/Materials/M_PalletWrap.uasset` | Material | 0.020 |
| `Content/Warehouse/Materials/M_PalletWrappingWhite.uasset` | Material | 0.012 |
| `Content/Warehouse/Materials/M_RouteArrow.uasset` | Material | 0.009 |
| `Content/Warehouse/Materials/M_TransportOutline.uasset` | Material | 0.012 |
| `Content/Warehouse/Materials/M_WarehouseSolid.uasset` | Material | 0.005 |
| `Content/Warehouse/Materials/ObserverWalls/DA_ObserverWallPalette.uasset` | WarehouseObserverWallPalette | 0.002 |
| `Content/Warehouse/Materials/ObserverWalls/MI_ObserverWall_MI_Cladding_61abfede68.uasset` | MaterialInstanceConstant | 0.008 |
| `Content/Warehouse/Materials/ObserverWalls/M_ObserverWall_M_WarehouseSolid_21b66039bc.uasset` | Material | 0.013 |
| `Content/Warehouse/Materials/RemoteCharacter/MI_RemoteFade_MI_Rubber.uasset` | MaterialInstanceConstant | 0.007 |
| `Content/Warehouse/Materials/RemoteCharacter/MI_RemoteFade_M_Screen_blue.uasset` | MaterialInstanceConstant | 0.007 |
| `Content/Warehouse/Materials/RemoteCharacter/M_RemoteFade_M_Screen_blue.uasset` | Material | 0.018 |
| `Content/Warehouse/Materials/RemoteCharacter/M_RemoteFade_M_WarehouseSolid.uasset` | Material | 0.013 |
| `Content/Warehouse/PalletLift/Materials/M_Lift_GalvanizedSteel.uasset` | Material | 0.010 |
| `Content/Warehouse/PalletLift/Materials/M_Lift_Graphite.uasset` | Material | 0.009 |
| `Content/Warehouse/PalletLift/Materials/M_Lift_MachinedSteel.uasset` | Material | 0.010 |
| `Content/Warehouse/PalletLift/Materials/M_Lift_RedPowderCoat.uasset` | Material | 0.010 |
| `Content/Warehouse/PalletLift/Materials/M_Lift_Rubber.uasset` | Material | 0.009 |
| `Content/Warehouse/PalletLift/Materials/M_Lift_SafetyYellow.uasset` | Material | 0.011 |
| `Content/Warehouse/PalletLift/Meshes/SM_PalletLift_Carrier.uasset` | StaticMesh | 0.518 |
| `Content/Warehouse/PalletLift/Meshes/SM_PalletLift_CarrierPoweredFrame.uasset` | StaticMesh | 0.037 |
| `Content/Warehouse/PalletLift/Meshes/SM_PalletLift_Frame.uasset` | StaticMesh | 2.153 |
| `Content/Warehouse/PalletLift/Meshes/SM_PalletLift_Frame4.uasset` | StaticMesh | 2.758 |
| `Content/Warehouse/PalletLift/Meshes/SM_PalletLift_FramePowered3.uasset` | StaticMesh | 2.156 |
| `Content/Warehouse/PalletLift/Meshes/SM_PalletLift_FramePowered4.uasset` | StaticMesh | 2.757 |
| `Content/Warehouse/PalletLift/Meshes/SM_PalletLift_Gate.uasset` | StaticMesh | 0.076 |
| `Content/Warehouse/Play/Maps/MainMenu.umap` | World | 0.015 |
| `Content/Warehouse/Play/Maps/Stage01.umap` | World | 0.326 |
| `Content/Warehouse/Play/Maps/Stage02.umap` | World | 0.410 |
| `Content/Warehouse/Play/Maps/Stage03.umap` | World | 0.475 |
| `Content/Warehouse/Services/Materials/M_CP_Matte_molded_rubber.uasset` | Material | 0.009 |
| `Content/Warehouse/Services/Materials/M_CP_Recessed_screw_steel.uasset` | Material | 0.010 |
| `Content/Warehouse/Services/Materials/M_CP_Yellow_warning_strip.uasset` | Material | 0.011 |
| `Content/Warehouse/Services/Materials/M_WS_Cable_black.uasset` | Material | 0.009 |
| `Content/Warehouse/Services/Materials/M_WS_Data_cable_blue.uasset` | Material | 0.010 |
| `Content/Warehouse/Services/Materials/M_WS_Electrical_enclosure.uasset` | Material | 0.010 |
| `Content/Warehouse/Services/Materials/M_WS_Fasteners.uasset` | Material | 0.010 |
| `Content/Warehouse/Services/Materials/M_WS_Fire_pipe_red.uasset` | Material | 0.009 |
| `Content/Warehouse/Services/Materials/M_WS_Galvanized_steel.uasset` | Material | 0.010 |
| `Content/Warehouse/Services/Materials/M_WS_LED_diffuser.uasset` | Material | 0.010 |
| `Content/Warehouse/Services/Materials/M_WS_Sprinkler_brass.uasset` | Material | 0.010 |
| `Content/Warehouse/Services/Materials/M_WS_Structural_graphite.uasset` | Material | 0.010 |
| `Content/Warehouse/Services/Materials/M_WS_Warning_yellow.uasset` | Material | 0.011 |
| `Content/Warehouse/Services/Meshes/SM_WS_BeamConnection.uasset` | StaticMesh | 0.058 |
| `Content/Warehouse/Services/Meshes/SM_WS_Brace_2m.uasset` | StaticMesh | 0.040 |
| `Content/Warehouse/Services/Meshes/SM_WS_CableBundle_2m.uasset` | StaticMesh | 0.053 |
| `Content/Warehouse/Services/Meshes/SM_WS_ConduitClamp.uasset` | StaticMesh | 0.150 |
| `Content/Warehouse/Services/Meshes/SM_WS_ConduitElbow90.uasset` | StaticMesh | 0.068 |
| `Content/Warehouse/Services/Meshes/SM_WS_ConduitStraight_1m.uasset` | StaticMesh | 0.031 |
| `Content/Warehouse/Services/Meshes/SM_WS_DistributionCabinet.uasset` | StaticMesh | 0.067 |
| `Content/Warehouse/Services/Meshes/SM_WS_DistributionDoor.uasset` | StaticMesh | 0.032 |
| `Content/Warehouse/Services/Meshes/SM_WS_DuctElbow90.uasset` | StaticMesh | 0.067 |
| `Content/Warehouse/Services/Meshes/SM_WS_DuctGrille.uasset` | StaticMesh | 0.048 |
| `Content/Warehouse/Services/Meshes/SM_WS_DuctStraight_1500.uasset` | StaticMesh | 0.079 |
| `Content/Warehouse/Services/Meshes/SM_WS_FirePipeElbow90.uasset` | StaticMesh | 0.069 |
| `Content/Warehouse/Services/Meshes/SM_WS_FirePipeStraight_2m.uasset` | StaticMesh | 0.031 |
| `Content/Warehouse/Services/Meshes/SM_WS_FirePipeTee.uasset` | StaticMesh | 0.037 |
| `Content/Warehouse/Services/Meshes/SM_WS_IBeam_4m.uasset` | StaticMesh | 0.024 |
| `Content/Warehouse/Services/Meshes/SM_WS_JunctionBox.uasset` | StaticMesh | 0.060 |
| `Content/Warehouse/Services/Meshes/SM_WS_LEDLinear_1200.uasset` | StaticMesh | 0.032 |
| `Content/Warehouse/Services/Meshes/SM_WS_PipeHanger.uasset` | StaticMesh | 0.033 |
| `Content/Warehouse/Services/Meshes/SM_WS_RubberColumnWrap_450.uasset` | StaticMesh | 0.465 |
| `Content/Warehouse/Services/Meshes/SM_WS_RubberCornerGuard_900.uasset` | StaticMesh | 0.135 |
| `Content/Warehouse/Services/Meshes/SM_WS_RubberFlatGuard_900.uasset` | StaticMesh | 0.074 |
| `Content/Warehouse/Services/Meshes/SM_WS_SprinklerHead.uasset` | StaticMesh | 0.174 |
| `Content/Warehouse/Services/Meshes/SM_WS_TrayElbow90.uasset` | StaticMesh | 0.045 |
| `Content/Warehouse/Services/Meshes/SM_WS_TrayEndCap.uasset` | StaticMesh | 0.017 |
| `Content/Warehouse/Services/Meshes/SM_WS_TrayHanger.uasset` | StaticMesh | 0.073 |
| `Content/Warehouse/Services/Meshes/SM_WS_TrayReducer.uasset` | StaticMesh | 0.028 |
| `Content/Warehouse/Services/Meshes/SM_WS_TrayStraight_2m.uasset` | StaticMesh | 0.041 |
| `Content/Warehouse/Services/Meshes/SM_WS_TrayTee.uasset` | StaticMesh | 0.059 |
| `Content/Warehouse/Services/Meshes/SM_WS_WallBracket.uasset` | StaticMesh | 0.042 |
| `Content/Warehouse/Services/SupportBatches/SM_SM_Support_0.uasset` | StaticMesh | 0.443 |
| `Content/Warehouse/Services/SupportBatches/SM_SM_Support_0_Roof.uasset` | StaticMesh | 3.175 |
| `Content/Warehouse/Services/SupportBatches/SM_SM_Support_1.uasset` | StaticMesh | 1.289 |
| `Content/Warehouse/Services/SupportBatches/SM_SM_Support_2.uasset` | StaticMesh | 1.288 |
| `Content/Warehouse/Services/SupportBatches/SM_SM_Support_2_Roof.uasset` | StaticMesh | 0.108 |
| `Content/Warehouse/UI/Icons/T_Icon_All.uasset` | Texture2D | 0.261 |
| `Content/Warehouse/UI/Icons/T_Icon_Cargo.uasset` | Texture2D | 0.249 |
| `Content/Warehouse/UI/Icons/T_Icon_Charger.uasset` | Texture2D | 0.235 |
| `Content/Warehouse/UI/Icons/T_Icon_Computer.uasset` | Texture2D | 0.221 |
| `Content/Warehouse/UI/Icons/T_Icon_ConveyorBelt.uasset` | Texture2D | 0.242 |
| `Content/Warehouse/UI/Icons/T_Icon_ConveyorRoller.uasset` | Texture2D | 0.241 |
| `Content/Warehouse/UI/Icons/T_Icon_ConveyorTurn.uasset` | Texture2D | 0.261 |
| `Content/Warehouse/UI/Icons/T_Icon_Elevator.uasset` | Texture2D | 0.231 |
| `Content/Warehouse/UI/Icons/T_Icon_Forklift.uasset` | Texture2D | 0.239 |
| `Content/Warehouse/UI/Icons/T_Icon_Pallet.uasset` | Texture2D | 0.287 |
| `Content/Warehouse/UI/Icons/T_Icon_PalletLift.uasset` | Texture2D | 0.259 |
| `Content/Warehouse/UI/Icons/T_Icon_Rack.uasset` | Texture2D | 0.322 |
| `Content/Warehouse/UI/Icons/T_Icon_WaitingBay.uasset` | Texture2D | 0.231 |
| `Content/Warehouse/UI/Icons/T_Icon_Worker.uasset` | Texture2D | 0.248 |
| `Content/Warehouse/UI/Icons/T_Icon_ZoneInbound.uasset` | Texture2D | 0.262 |
| `Content/Warehouse/UI/Icons/T_Icon_ZoneOutbound.uasset` | Texture2D | 0.261 |
