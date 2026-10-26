#include "WarehouseExperimentStorage.h"
#include "WarehousePlayMode.h"
#include "WarehouseBuildingLayout.h"
#include "WarehouseCargo.h"
#include "WarehouseComputer.h"
#include "WarehouseConstructionEditor.h"
#include "WarehouseConveyor.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseElevator.h"
#include "WarehouseElevatorLayout.h"
#include "WarehouseForklift.h"
#include "WarehouseInboundZone.h"
#include "WarehouseLayoutEditor.h"
#include "WarehouseLayoutInventory.h"
#include "WarehouseLogisticsTruck.h"
#include "WarehouseObserverWallFade.h"
#include "WarehousePallet.h"
#include "WarehousePalletLift.h"
#include "WarehouseShippingSystem.h"
#include "msc_vrCharacter.h"
#include "msc_vrPlayerController.h"
#include "Components/LightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Light.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/TargetPoint.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
 constexpr int32 Schema=1;
 const FString IdPrefix=TEXT("ExperimentId:");
 using FObject=TSharedPtr<FJsonObject>;
 using FValue=TSharedPtr<FJsonValue>;
 using FActors=TMap<FString,AActor*>;
 FValue String(const FString& S) { return MakeShared<FJsonValueString>(S); }
 FValue Object(const FObject& O) { return MakeShared<FJsonValueObject>(O); }
 bool Fail(FString& Error,const FString& Text) { Error=Text;return false; }
 bool IsDataActor(const AActor* A)
 {
  if (!IsValid(A) || A->IsActorBeingDestroyed()) return false;
  const FString Class=A->GetClass()->GetPathName();
  if (A->IsA<AStaticMeshActor>() || A->IsA<ATargetPoint>() || A->IsA<ATextRenderActor>() || A->IsA<ALight>()) return true;
  if (!Class.StartsWith(TEXT("/Script/msc_vr.Warehouse"))) return false;
  return !Class.Contains(TEXT("SimulationClock")) && !Class.Contains(TEXT("TransportVisualizer")) &&
   !Class.Contains(TEXT("ConstructionEditor")) && !Class.Contains(TEXT("ObserverWallFade")) && !Class.Contains(TEXT("Experiment"));
 }
 bool AllowedActorClass(UClass* Type)
 { return Type && Type->IsChildOf(AActor::StaticClass()) && (Type->IsChildOf(ALight::StaticClass()) || IsDataActor(Type->GetDefaultObject<AActor>())); }
 bool IsProperty(const UObject* O,const FProperty* P)
 {
  if (P->HasAnyPropertyFlags(CPF_Transient|CPF_Deprecated|CPF_DuplicateTransient|CPF_BlueprintAssignable)) return false;
  const FString N=P->GetName();
  if (N==TEXT("Tags")) return true;
  if (O->IsA<AWarehouseCargo>() && N==TEXT("Packing")) return true;
  if (O->IsA<AWarehouseLayoutInventory>() && (N==TEXT("HeldCargo") || N==TEXT("RackCatalogue") || N==TEXT("Prototypes"))) return true;
  if (O->IsA<AWarehouseBuildingLayout>() && (N==TEXT("Settings") || N==TEXT("bHasAppliedChanges") || N==TEXT("GeneratedStructure"))) return true;
  if (O->IsA<AWarehouseElevator>() && N==TEXT("InitialShaftPosition")) return true;
  const bool OwnClass=P->GetOwnerClass() && P->GetOwnerClass()->GetPathName().StartsWith(TEXT("/Script/msc_vr."));
  return OwnClass && P->HasAnyPropertyFlags(CPF_Edit) && !P->HasAnyPropertyFlags(CPF_EditConst);
 }
 FString Canonical(const FValue& V)
 {
  if (!V.IsValid() || V->IsNull()) return TEXT("null");
  if (V->Type==EJson::Object)
  {
   TArray<FString> Keys;for (const auto& Pair:V->AsObject()->Values) Keys.Add(FString(*Pair.Key));Keys.Sort();FString Out=TEXT("{");
   for (const FString& K:Keys) { if (Out.Len()>1) Out+=TEXT(",");Out+=Canonical(String(K))+TEXT(":")+Canonical(V->AsObject()->TryGetField(K)); }
   return Out+TEXT("}");
  }
  if (V->Type==EJson::Array)
  { FString Out=TEXT("[");for (const auto& Item:V->AsArray()) { if (Out.Len()>1) Out+=TEXT(",");Out+=Canonical(Item); }return Out+TEXT("]"); }
  FString Out;auto Writer=TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
  FJsonSerializer::Serialize(V,TEXT(""),Writer);return Out;
 }
 FString StorageDigest(const FString& S)
 {
  FTCHARToUTF8 Bytes(*S);FSHA1 Hash;Hash.Update(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length());Hash.Final();uint8 Result[20];Hash.GetHash(Result);return BytesToHex(Result,20).ToLower();
 }
 FString SnapshotHash(const FObject& O)
 {
  auto Copy=MakeShared<FJsonObject>();Copy->Values=O->Values;Copy->RemoveField(TEXT("hash"));return StorageDigest(Canonical(Object(Copy)));
 }
 FObject PoseJson(const FTransform& Pose)
 {
  auto VectorText=[](const FVector& V) { return FString::Printf(TEXT("X=%.9f Y=%.9f Z=%.9f"),V.X,V.Y,V.Z); };
  auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("translation"),VectorText(Pose.GetTranslation()));
  O->SetStringField(TEXT("rotation"),Pose.GetRotation().ToString());O->SetStringField(TEXT("scale"),VectorText(Pose.GetScale3D()));return O;
 }
 bool PoseFrom(const FObject& O,FTransform& Pose)
 {
  if (!O) return false;FString T,R,S;FVector Position,Scale;FQuat Rotation;
  if (!O->TryGetStringField(TEXT("translation"),T) || !O->TryGetStringField(TEXT("rotation"),R) || !O->TryGetStringField(TEXT("scale"),S) ||
      !Position.InitFromString(T) || !Rotation.InitFromString(R) || !Scale.InitFromString(S) || Position.ContainsNaN() || Rotation.ContainsNaN() || Scale.ContainsNaN() ||
      Position.GetAbsMax()>1000000. || Scale.GetAbsMax()>10000. || Scale.GetAbsMin()<.000001 || !Rotation.IsNormalized()) return false;
  Pose=FTransform(Rotation,Position,Scale);return true;
 }
 FValue Encode(FProperty* P,const void* Address,const FActors& Actors,FString& Error);
 FObject EncodeProperties(UObject* O,const FActors& Actors,FString& Error)
 {
  auto Result=MakeShared<FJsonObject>();
  for (TFieldIterator<FProperty> It(O->GetClass());It;++It) if (IsProperty(O,*It))
  { auto V=Encode(*It,It->ContainerPtrToValuePtr<void>(O),Actors,Error);if (!Error.IsEmpty()) return nullptr;Result->SetField(It->GetName(),V); }
  return Result;
 }
 FValue Encode(FProperty* P,const void* Address,const FActors& Actors,FString& Error)
 {
  if (auto* Soft=CastField<FSoftObjectProperty>(P))
  { auto Value=MakeShared<FJsonObject>();Value->SetStringField(TEXT("softAsset"),Soft->GetPropertyValue(Address).ToSoftObjectPath().ToString());return Object(Value); }
  if (auto* Ref=CastField<FObjectPropertyBase>(P))
  {
   UObject* O=Ref->GetObjectPropertyValue(Address);if (!IsValid(O)) return MakeShared<FJsonValueNull>();
   if (auto* Dynamic=Cast<UMaterialInstanceDynamic>(O)) O=Dynamic->Parent;
   auto Value=MakeShared<FJsonObject>();
   if (auto* Actor=Cast<AActor>(O))
   {
    const FString Id=WarehouseExperimentStorage::StableActorId(Actor);
    if (!Actors.Contains(Id)) { Error=TEXT("저장 범위 밖 액터 참조: ")+P->GetName()+TEXT(" → ")+Actor->GetName();return nullptr; }
    Value->SetStringField(TEXT("actor"),Id);
   }
   else if (auto* Component=Cast<UActorComponent>(O))
   { Value->SetStringField(TEXT("actor"),WarehouseExperimentStorage::StableActorId(Component->GetOwner()));Value->SetStringField(TEXT("component"),Component->GetName()); }
   else if (O->IsAsset() || O->IsA<UClass>()) Value->SetStringField(TEXT("asset"),O->GetPathName());
   else { Error=TEXT("영구 저장할 수 없는 객체 참조: ")+O->GetPathName();return nullptr; }
   return Object(Value);
  }
  if (auto* Array=CastField<FArrayProperty>(P))
  { FScriptArrayHelper H(Array,Address);TArray<FValue> Values;for (int32 I=0;I<H.Num();++I) Values.Add(Encode(Array->Inner,H.GetRawPtr(I),Actors,Error));return MakeShared<FJsonValueArray>(Values); }
  if (auto* Struct=CastField<FStructProperty>(P))
  {
   auto Value=MakeShared<FJsonObject>();
   for (TFieldIterator<FProperty> It(Struct->Struct);It;++It) if (!It->HasAnyPropertyFlags(CPF_Transient|CPF_Deprecated))
    Value->SetField(It->GetName(),Encode(*It,It->ContainerPtrToValuePtr<void>(Address),Actors,Error));
   return Object(Value);
  }
  if (auto* Map=CastField<FMapProperty>(P))
  {
   FScriptMapHelper H(Map,Address);TArray<FValue> Values;
   for (int32 I=0;I<H.GetMaxIndex();++I) if (H.IsValidIndex(I))
   { auto Row=MakeShared<FJsonObject>();Row->SetField(TEXT("key"),Encode(Map->KeyProp,H.GetKeyPtr(I),Actors,Error));Row->SetField(TEXT("value"),Encode(Map->ValueProp,H.GetValuePtr(I),Actors,Error));Values.Add(Object(Row)); }
   Values.Sort([](const FValue& A,const FValue& B) { return Canonical(A)<Canonical(B); });return MakeShared<FJsonValueArray>(Values);
  }
  if (auto* Set=CastField<FSetProperty>(P))
  { FScriptSetHelper H(Set,Address);TArray<FValue> Values;for (int32 I=0;I<H.GetMaxIndex();++I) if (H.IsValidIndex(I)) Values.Add(Encode(Set->ElementProp,H.GetElementPtr(I),Actors,Error));Values.Sort([](const FValue& A,const FValue& B) { return Canonical(A)<Canonical(B); });return MakeShared<FJsonValueArray>(Values); }
  FString Value;P->ExportTextItem_Direct(Value,Address,nullptr,nullptr,PPF_None);return String(Value);
 }
 bool Decode(FProperty* P,void* Address,const FValue& Value,const FActors& Actors,FString& Error)
 {
  if (!Value.IsValid()) return Fail(Error,TEXT("누락된 속성: ")+P->GetName());
  if (auto* Soft=CastField<FSoftObjectProperty>(P))
  {
   FString Path;if (Value->Type!=EJson::Object || !Value->AsObject()->TryGetStringField(TEXT("softAsset"),Path)) return Fail(Error,TEXT("잘못된 에셋 경로"));
   Soft->SetPropertyValue(Address,FSoftObjectPtr(FSoftObjectPath(Path)));return true;
  }
  if (auto* Ref=CastField<FObjectPropertyBase>(P))
  {
   if (Value->IsNull()) { Ref->SetObjectPropertyValue(Address,nullptr);return true; }
   if (Value->Type!=EJson::Object) return Fail(Error,TEXT("잘못된 객체 참조"));
   const auto O=Value->AsObject();FString Id,Name,Asset;UObject* Target=nullptr;
   if (O->TryGetStringField(TEXT("actor"),Id))
   {
    Target=Actors.FindRef(Id);
    if (Target && O->TryGetStringField(TEXT("component"),Name)) Target=FindObjectFast<UObject>(Target,FName(*Name));
   }
   else if (O->TryGetStringField(TEXT("asset"),Asset)) Target=StaticLoadObject(Ref->PropertyClass,nullptr,*Asset);
   if (!Target || !Target->IsA(Ref->PropertyClass)) return Fail(Error,TEXT("객체 참조 형식 또는 에셋이 맞지 않습니다: ")+P->GetName()+TEXT(" ")+Id+Asset+Name);
   Ref->SetObjectPropertyValue(Address,Target);return true;
  }
  if (auto* Array=CastField<FArrayProperty>(P))
  {
   if (Value->Type!=EJson::Array) return Fail(Error,TEXT("잘못된 배열 속성"));FScriptArrayHelper H(Array,Address);H.Resize(Value->AsArray().Num());
   for (int32 I=0;I<H.Num();++I) if (!Decode(Array->Inner,H.GetRawPtr(I),Value->AsArray()[I],Actors,Error)) return false;return true;
  }
  if (auto* Struct=CastField<FStructProperty>(P))
  {
   if (Value->Type!=EJson::Object) return Fail(Error,TEXT("잘못된 구조체 속성"));
   for (const auto& Pair:Value->AsObject()->Values)
   {
    auto* Field=FindFProperty<FProperty>(Struct->Struct,*Pair.Key);
    if (!Field || Field->HasAnyPropertyFlags(CPF_Transient|CPF_Deprecated) || !Decode(Field,Field->ContainerPtrToValuePtr<void>(Address),Pair.Value,Actors,Error)) return Fail(Error,TEXT("구조체 속성 불일치: ")+FString(*Pair.Key)+TEXT(" ")+Error);
   }
   return true;
  }
  if (auto* Map=CastField<FMapProperty>(P))
  {
   if (Value->Type!=EJson::Array) return Fail(Error,TEXT("잘못된 사전 속성"));FScriptMapHelper H(Map,Address);H.EmptyValues();
   for (const auto& V:Value->AsArray())
   { if (V->Type!=EJson::Object) return false;const auto Row=V->AsObject();const int32 I=H.AddDefaultValue_Invalid_NeedsRehash();if (!Decode(Map->KeyProp,H.GetKeyPtr(I),Row->TryGetField(TEXT("key")),Actors,Error) || !Decode(Map->ValueProp,H.GetValuePtr(I),Row->TryGetField(TEXT("value")),Actors,Error)) return false; }
   H.Rehash();return true;
  }
  if (auto* Set=CastField<FSetProperty>(P))
  {
   if (Value->Type!=EJson::Array) return Fail(Error,TEXT("잘못된 집합 속성"));FScriptSetHelper H(Set,Address);H.EmptyElements();
   for (const auto& V:Value->AsArray()) { const int32 I=H.AddDefaultValue_Invalid_NeedsRehash();if (!Decode(Set->ElementProp,H.GetElementPtr(I),V,Actors,Error)) return false; }H.Rehash();return true;
  }
  if (Value->Type!=EJson::String) return Fail(Error,TEXT("잘못된 값 형식: ")+P->GetName());
  // ImportText returns a pointer into its input. Keep the JSON string alive
  // through the consumption check instead of dereferencing a temporary buffer.
  const FString ScalarText=Value->AsString();
  const TCHAR* End=P->ImportText_Direct(*ScalarText,Address,nullptr,PPF_None);
  if (!End || *End) return Fail(Error,TEXT("속성 값을 읽을 수 없습니다: ")+P->GetName());
  if (const auto* Numeric=CastField<FNumericProperty>(P);Numeric && Numeric->IsFloatingPoint() && !FMath::IsFinite(Numeric->GetFloatingPointPropertyValue(Address))) return Fail(Error,TEXT("유한하지 않은 값: ")+P->GetName());
  return true;
 }
 bool DecodeProperties(UObject* Target,const FObject& Data,const FActors& Actors,FString& Error)
 {
  if (!Data) return false;
  for (const auto& Pair:Data->Values)
  {
   auto* P=FindFProperty<FProperty>(Target->GetClass(),*Pair.Key);
   if (!P || !IsProperty(Target,P)) return Fail(Error,TEXT("지원하지 않는 저장 속성: ")+Target->GetName()+TEXT(".")+FString(*Pair.Key));
   if (!Decode(P,P->ContainerPtrToValuePtr<void>(Target),Pair.Value,Actors,Error)) return false;
  }
  return true;
 }
 bool SavePart(const USceneComponent* C)
 {
  if (!IsValid(C) || C->IsA<UPhysicsConstraintComponent>() || C->GetName().Contains(TEXT("WrapFilm"))) return false;
  // Runtime controllers recreate sensors, guide anchors, rollers and displays.
  if (C->CreationMethod==EComponentCreationMethod::Instance && C->GetOwner()->IsA<AWarehouseConveyor>()) return false;
  // Runtime building/floor actors are intentionally transient, including their
  // default mesh/light components. Their geometry is still part of the layout.
  return C->HasAnyFlags(RF_DefaultSubObject) || C->GetOwner()->IsA<AStaticMeshActor>() || C->GetOwner()->IsA<ALight>();
 }
 FObject PartJson(USceneComponent* C,const FActors& Actors,FString& Error)
 {
  auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("name"),C->GetName());O->SetStringField(TEXT("class"),C->GetClass()->GetPathName());
  O->SetObjectField(TEXT("pose"),PoseJson(C->GetComponentTransform()));O->SetNumberField(TEXT("mobility"),int32(C->Mobility));O->SetBoolField(TEXT("visible"),C->IsVisible());
  TArray<FValue> Tags;for (FName Tag:C->ComponentTags) Tags.Add(String(Tag.ToString()));O->SetArrayField(TEXT("tags"),Tags);
  if (auto* Mesh=Cast<UStaticMeshComponent>(C))
  {
   O->SetStringField(TEXT("mesh"),Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetPathName() : FString());TArray<FValue> Materials;
   for (int32 I=0;I<Mesh->GetNumMaterials();++I)
   {
    UMaterialInterface* M=Mesh->GetMaterial(I);
    if (auto* PC=C->GetWorld()->GetFirstPlayerController()) if (auto* Fade=PC->FindComponentByClass<UWarehouseObserverWallFade>()) M=Fade->GetOriginalMaterial(Mesh,I);
    while (auto* Dynamic=Cast<UMaterialInstanceDynamic>(M)) M=Dynamic->Parent;Materials.Add(String(M ? M->GetPathName() : FString()));
   }
   O->SetArrayField(TEXT("materials"),Materials);
  }
  if (auto* P=Cast<UPrimitiveComponent>(C))
  {
   O->SetNumberField(TEXT("collision"),int32(P->GetCollisionEnabled()));O->SetNumberField(TEXT("objectType"),int32(P->GetCollisionObjectType()));
   TArray<FValue> Responses;for (int32 I=0;I<ECC_MAX;++I) Responses.Add(MakeShared<FJsonValueNumber>(int32(P->GetCollisionResponseToChannel(ECollisionChannel(I)))));O->SetArrayField(TEXT("responses"),Responses);
   O->SetBoolField(TEXT("gravity"),P->IsGravityEnabled());
   // Actors with native rig setup rebuild their own physics. Static furniture may be intentionally dynamic.
   O->SetBoolField(TEXT("simulate"),P->IsSimulatingPhysics());
   if (auto* Physical=P->BodyInstance.GetSimplePhysicalMaterial()) if (Physical->IsAsset()) O->SetStringField(TEXT("physicalMaterial"),Physical->GetPathName());
  }
  if (C->IsA<ULightComponent>())
  {
   auto Settings=MakeShared<FJsonObject>();
   for (const TCHAR* Name:{TEXT("Intensity"),TEXT("LightColor"),TEXT("CastShadows"),TEXT("AttenuationRadius"),TEXT("IntensityUnits"),TEXT("InnerConeAngle"),TEXT("OuterConeAngle"),
       TEXT("SourceWidth"),TEXT("SourceHeight"),TEXT("BarnDoorAngle"),TEXT("BarnDoorLength"),TEXT("SourceRadius"),TEXT("SoftSourceRadius"),TEXT("SourceLength"),TEXT("Temperature"),TEXT("bUseTemperature")})
    if (auto* P=FindFProperty<FProperty>(C->GetClass(),Name)) Settings->SetField(Name,Encode(P,P->ContainerPtrToValuePtr<void>(C),Actors,Error));
   O->SetObjectField(TEXT("light"),Settings);
  }
  if (C->IsA<UTextRenderComponent>())
  {
   auto Settings=MakeShared<FJsonObject>();
   for (const TCHAR* Name:{TEXT("Text"),TEXT("Font"),TEXT("TextMaterial"),TEXT("TextRenderColor"),TEXT("WorldSize"),TEXT("XScale"),TEXT("YScale"),
       TEXT("HorizontalAlignment"),TEXT("VerticalAlignment"),TEXT("HorizSpacingAdjust"),TEXT("VertSpacingAdjust"),TEXT("bAlwaysRenderAsText")})
    if (auto* P=FindFProperty<FProperty>(C->GetClass(),Name)) Settings->SetField(Name,Encode(P,P->ContainerPtrToValuePtr<void>(C),Actors,Error));
   O->SetObjectField(TEXT("text"),Settings);
  }
  return O;
 }
 bool AssetAllowed(const FString& Path) { return Path.IsEmpty() || Path.StartsWith(TEXT("/Game/")) || Path.StartsWith(TEXT("/Engine/")) || Path.StartsWith(TEXT("/Script/")); }
 bool CheckReferences(const FValue& Value,const TSet<FString>& Ids,FString& Error)
 {
  if (!Value) return false;
  if (Value->Type==EJson::Object)
  {
   const auto O=Value->AsObject();FString Id,Asset;
   if (O->TryGetStringField(TEXT("actor"),Id) && !Ids.Contains(Id)) return Fail(Error,TEXT("끊어진 액터 참조: ")+Id);
   if (O->TryGetStringField(TEXT("asset"),Asset) && (!AssetAllowed(Asset) || !StaticLoadObject(UObject::StaticClass(),nullptr,*Asset))) return Fail(Error,TEXT("필요 에셋 누락: ")+Asset);
   if (O->TryGetStringField(TEXT("softAsset"),Asset) && !Asset.IsEmpty() && (!AssetAllowed(Asset) || !StaticLoadObject(UObject::StaticClass(),nullptr,*Asset))) return Fail(Error,TEXT("필요 에셋 누락: ")+Asset);
   for (const auto& Pair:O->Values) if (!CheckReferences(Pair.Value,Ids,Error)) return false;
  }
  else if (Value->Type==EJson::Array) for (const auto& V:Value->AsArray()) if (!CheckReferences(V,Ids,Error)) return false;
  return true;
 }
 bool SafeId(const FString& Id) { FGuid Guid;return FGuid::ParseExact(Id,EGuidFormats::Digits,Guid); }
}

FString WarehouseExperimentStorage::StableActorId(const AActor* Actor)
{
 if (!IsValid(Actor)) return FString();for (const FName Tag:Actor->Tags) if (Tag.ToString().StartsWith(IdPrefix)) return Tag.ToString().Mid(IdPrefix.Len());
 return TEXT("actor:")+Actor->GetName();
}
AActor* WarehouseExperimentStorage::ResolveActorId(UWorld* World,const FString& Id)
{ if (World && !Id.IsEmpty()) for (TActorIterator<AActor> It(World);It;++It) if (StableActorId(*It)==Id) return *It;return nullptr; }
FString WarehouseExperimentStorage::MapIdentity(const UWorld* World)
{ return World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString(); }
FString WarehouseExperimentStorage::LayoutDirectory() { return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("WarehouseExperiments/Layouts")); }
bool WarehouseExperimentStorage::CanCapture(UWorld* World,FString& Error)
{
 if (AWarehousePlayStage::Find(World)) return Fail(Error,TEXT("배치 저장·실험은 관리자 모드에서 사용하세요."));
 Error.Reset();if (!World || !World->IsGameWorld() || !UGameplayStatics::IsGamePaused(World)) return Fail(Error,TEXT("배치 편집으로 시뮬레이션을 일시정지한 뒤 저장하세요."));
 for (TActorIterator<AWarehouseLayoutEditor> It(World);It;++It) if (It->bDragging || It->bMarqueeSelecting) return Fail(Error,TEXT("진행 중인 드래그·선택을 끝내세요."));
 if (auto* PC=Cast<Amsc_vrPlayerController>(World->GetFirstPlayerController()))
  if (const auto* BuildingEdit=PC->GetConstructionEditor();BuildingEdit && BuildingEdit->bDragging) return Fail(Error,TEXT("진행 중인 건물 크기·계단 편집을 끝내세요."));
 for (TActorIterator<Amsc_vrCharacter> It(World);It;++It) if (It->GetHeldCargo()) return Fail(Error,TEXT("플레이어가 들고 있는 물체를 내려놓고 저장하세요."));
 for (TActorIterator<AWarehouseForklift> It(World);It;++It)
  if (!It->IsIdleForLayout() || (It->AIState!=EWarehouseAIState::Off && It->AIState!=EWarehouseAIState::Ready) || It->GetForkHeightCm()>20 || FMath::Abs(It->CurrentSpeedCm)>1)
   return Fail(Error,TEXT("모든 지게차를 작업·원격 조작 없이 정지하고 포크를 내린 초기 상태로 두세요: ")+It->VehicleName);
 for (TActorIterator<AWarehouseElevator> It(World);It;++It) if (It->State!=EWarehouseElevatorState::Idle || It->ReservedVehicle || It->CurrentFloor!=0) return Fail(Error,TEXT("범용 승강기를 빈 1층 대기 상태로 두세요."));
 for (TActorIterator<AWarehousePalletLift> It(World);It;++It) if (!It->IsIdleForLayout() || It->CurrentFloor!=0) return Fail(Error,TEXT("화물 승강기를 빈 1층 대기 상태로 두세요."));
 for (TActorIterator<AWarehouseConveyor> It(World);It;++It) if (!It->IsIdleForLayout()) return Fail(Error,TEXT("컨베이어 화물과 진행 중인 인계를 정리한 뒤 저장하세요."));
 for (TActorIterator<AWarehouseShippingSystem> It(World);It;++It) if (!It->Shipments.IsEmpty()) return Fail(Error,TEXT("배송편 실행 전 초기 조건만 저장할 수 있습니다. 새 실행에서 준비하세요."));
 for (TActorIterator<AWarehouseComputer> It(World);It;++It) if (!It->Tasks.IsEmpty()) return Fail(Error,TEXT("관제 작업 실행 전 초기 조건만 저장할 수 있습니다. 새 실행에서 준비하세요."));
 for (TActorIterator<AWarehouseLogisticsTruck> It(World);It;++It) if (It->State!=EWarehouseTruckState::AtDock) return Fail(Error,TEXT("모든 트럭이 도크 대기 상태여야 합니다."));
 return true;
}
bool WarehouseExperimentStorage::Capture(UWorld* World,const FString& Name,TSharedPtr<FJsonObject>& Snapshot,FString& Error)
{
 if (!CanCapture(World,Error)) return false;
 if (Name.TrimStartAndEnd().IsEmpty() || Name.Len()>80) return Fail(Error,TEXT("배치 이름을 1~80자로 입력하세요."));
 FActors Actors;for (TActorIterator<AActor> It(World);It;++It) if (IsDataActor(*It))
 { const FString Id=StableActorId(*It);if (Actors.Contains(Id)) return Fail(Error,TEXT("중복 배치 ID: ")+Id);Actors.Add(Id,*It); }
 TArray<FString> Keys;Actors.GetKeys(Keys);Keys.Sort();TArray<FValue> Rows;int32 CargoCount=0,HeldCount=0;
 for (const FString& Id:Keys)
 {
  AActor* Actor=Actors[Id];auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("id"),Id);Row->SetStringField(TEXT("name"),Actor->GetName());
  Row->SetStringField(TEXT("label"),Actor->GetActorNameOrLabel());Row->SetStringField(TEXT("class"),Actor->GetClass()->GetPathName());Row->SetObjectField(TEXT("pose"),PoseJson(Actor->GetActorTransform()));
  Row->SetBoolField(TEXT("hidden"),Actor->IsHidden());Row->SetBoolField(TEXT("collision"),Actor->GetActorEnableCollision());
  Row->SetObjectField(TEXT("properties"),EncodeProperties(Actor,Actors,Error));if (!Error.IsEmpty()) return false;
  TArray<USceneComponent*> Parts;Actor->GetComponents(Parts);Parts.Sort([](const auto& A,const auto& B) { return A.GetName()<B.GetName(); });TArray<FValue> PartRows;
  for (auto* Part:Parts) if (SavePart(Part)) PartRows.Add(Object(PartJson(Part,Actors,Error)));Row->SetArrayField(TEXT("parts"),PartRows);
  if (auto* Pallet=Cast<AWarehousePallet>(Actor))
  {
   Row->SetNumberField(TEXT("receipt"),int32(Pallet->ReceiptState));TArray<FValue> Wrapped,Supported;
   for (auto* Cargo:Pallet->GetWrappedCargo()) Wrapped.Add(String(StableActorId(Cargo)));Row->SetArrayField(TEXT("wrapped"),Wrapped);
   if (auto* Strength=AWarehouseDamageSystem::Find(Actor))
   { Strength->EnsureSupportSnapshot();for (auto* Cargo:Strength->GetSupportedActors(Actor,false)) if (Cargo->IsA<AWarehouseCargo>() || Cargo->IsA<AWarehousePallet>()) Supported.Add(String(StableActorId(Cargo))); }
   Supported.Sort([](const auto& A,const auto& B) { return A->AsString()<B->AsString(); });Row->SetArrayField(TEXT("supported"),Supported);
  }
  if (Actor->IsA<AWarehouseCargo>()) ++CargoCount;if (auto* Store=Cast<AWarehouseLayoutInventory>(Actor)) HeldCount+=Store->HeldCargo.Num();
  Rows.Add(Object(Row));
 }
 Snapshot=MakeShared<FJsonObject>();Snapshot->SetNumberField(TEXT("schema"),Schema);Snapshot->SetStringField(TEXT("id"),FGuid::NewGuid().ToString(EGuidFormats::Digits));
 Snapshot->SetStringField(TEXT("name"),Name.TrimStartAndEnd());Snapshot->SetStringField(TEXT("map"),MapIdentity(World));Snapshot->SetStringField(TEXT("savedAt"),FDateTime::UtcNow().ToIso8601());
 Snapshot->SetArrayField(TEXT("actors"),Rows);Snapshot->SetNumberField(TEXT("cargoCount"),CargoCount);Snapshot->SetNumberField(TEXT("heldCargoCount"),HeldCount);
 TArray<WarehouseElevatorLayout::FSnapshotSource> FloorSources;TArray<AActor*> FloorReplacements;
 if (!Keys.IsEmpty()) WarehouseElevatorLayout::ExportExperimentState(Actors[Keys[0]],FloorSources,FloorReplacements);
 TArray<FValue> SourceRows,ReplacementRows;
 for (const auto& Source:FloorSources)
 {
  auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("actor"),StableActorId(Source.Actor));Row->SetBoolField(TEXT("visible"),Source.Visible);Row->SetBoolField(TEXT("hidden"),Source.Hidden);
  Row->SetBoolField(TEXT("collision"),Source.Collision);Row->SetNumberField(TEXT("collisionMode"),Source.CollisionMode);SourceRows.Add(Object(Row));
 }
 for (AActor* A:FloorReplacements) { auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("actor"),StableActorId(A));ReplacementRows.Add(Object(Row)); }
 Snapshot->SetArrayField(TEXT("floorSources"),SourceRows);Snapshot->SetArrayField(TEXT("floorReplacements"),ReplacementRows);
 Snapshot->SetStringField(TEXT("cargoFingerprint"),CargoFingerprint(Snapshot));Snapshot->SetStringField(TEXT("hash"),SnapshotHash(Snapshot));
 return Validate(World,Snapshot,Error);
}
FString WarehouseExperimentStorage::CargoFingerprint(const TSharedPtr<FJsonObject>& Snapshot)
{
 if (!Snapshot) return FString();const TArray<FValue>* Rows=nullptr;if (!Snapshot->TryGetArrayField(TEXT("actors"),Rows)) return FString();TArray<FString> Items;
 for (const auto& V:*Rows)
 {
  const auto Row=V->AsObject();const FString Class=Row->GetStringField(TEXT("class"));const auto Props=Row->GetObjectField(TEXT("properties"));auto Data=MakeShared<FJsonObject>();
  if (Class.EndsWith(TEXT(".WarehouseCargo")))
  {
   for (const TCHAR* Key:{TEXT("CargoId"),TEXT("CargoKind"),TEXT("GrossMassKg"),TEXT("Packing")}) if (Props->HasField(Key)) Data->SetField(Key,Props->TryGetField(Key));
   TArray<FValue> Shapes;for (const auto& Part:Row->GetArrayField(TEXT("parts")))
   { auto Shape=MakeShared<FJsonObject>();const auto P=Part->AsObject();if (P->HasField(TEXT("mesh"))) Shape->SetField(TEXT("mesh"),P->TryGetField(TEXT("mesh")));Shape->SetStringField(TEXT("scale"),P->GetObjectField(TEXT("pose"))->GetStringField(TEXT("scale")));Shapes.Add(Object(Shape)); }Data->SetArrayField(TEXT("shape"),Shapes);
   Data->SetStringField(TEXT("id"),Row->GetStringField(TEXT("id")));Items.Add(Canonical(Object(Data)));
  }
  else if (Class.EndsWith(TEXT(".WarehousePallet")))
  {
   Data->SetStringField(TEXT("id"),Row->GetStringField(TEXT("id")));for (const TCHAR* Key:{TEXT("PalletMassKg"),TEXT("PayloadMassKg")}) if (Props->HasField(Key)) Data->SetField(Key,Props->TryGetField(Key));
   for (const TCHAR* Key:{TEXT("receipt"),TEXT("wrapped"),TEXT("supported")}) if (Row->HasField(Key)) Data->SetField(Key,Row->TryGetField(Key));Items.Add(Canonical(Object(Data)));
  }
  else if (Class.EndsWith(TEXT(".WarehouseLayoutInventory")) && Props->HasField(TEXT("HeldCargo"))) Items.Add(Canonical(Props->TryGetField(TEXT("HeldCargo"))));
 }
 Items.Sort();return StorageDigest(FString::Join(Items,TEXT("\n")));
}
bool WarehouseExperimentStorage::Validate(UWorld* World,const TSharedPtr<FJsonObject>& Snapshot,FString& Error)
{
 Error.Reset();if (!Snapshot) return Fail(Error,TEXT("배치 파일을 읽을 수 없습니다."));
 double Version=0;FString Id,Map,Hash;const TArray<FValue>* Rows=nullptr;
 if (!Snapshot->TryGetNumberField(TEXT("schema"),Version) || Version!=Schema || !Snapshot->TryGetStringField(TEXT("id"),Id) || !SafeId(Id) ||
     !Snapshot->TryGetStringField(TEXT("map"),Map) || (World && Map!=MapIdentity(World)) || !Snapshot->TryGetStringField(TEXT("hash"),Hash) || Hash!=SnapshotHash(Snapshot) ||
     !Snapshot->TryGetArrayField(TEXT("actors"),Rows) || Rows->Num()>20000) return Fail(Error,TEXT("배치 버전·지도·무결성 검사가 실패했습니다. 원본 맵은 변경하지 않았습니다."));
 TSet<FString> Ids,CargoIds;
 for (const auto& Value:*Rows)
 {
  if (!Value || Value->Type!=EJson::Object) return Fail(Error,TEXT("잘못된 배치 액터"));const auto Row=Value->AsObject();FString ActorId,Class;FTransform Pose;const TSharedPtr<FJsonObject>* Transform=nullptr;const TArray<FValue>* Parts=nullptr;
  if (!Row->TryGetStringField(TEXT("id"),ActorId) || ActorId.IsEmpty() || Ids.Contains(ActorId) || !Row->TryGetStringField(TEXT("class"),Class) ||
      !Row->TryGetObjectField(TEXT("pose"),Transform) || !PoseFrom(*Transform,Pose) || !Row->TryGetArrayField(TEXT("parts"),Parts)) return Fail(Error,TEXT("중복 ID 또는 유효하지 않은 액터 자세: ")+ActorId);
  UClass* Type=AssetAllowed(Class) ? LoadObject<UClass>(nullptr,*Class) : nullptr;
  if (!AllowedActorClass(Type)) return Fail(Error,TEXT("지원하지 않거나 누락된 액터 클래스: ")+Class);
  if ((Type->IsChildOf(AStaticMeshActor::StaticClass()) || Type->IsChildOf(ALight::StaticClass())) && Parts->IsEmpty())
   return Fail(Error,TEXT("배치의 메시·조명 부품 기록이 없습니다. 배치를 다시 저장하세요: ")+ActorId);
  Ids.Add(ActorId);TSet<FString> PartNames;
  for (const auto& P:*Parts)
  {
   if (P->Type!=EJson::Object) return false;const auto Part=P->AsObject();FString PartName,PartClass,Mesh;const TSharedPtr<FJsonObject>* PartPose=nullptr;
   if (!Part->TryGetStringField(TEXT("name"),PartName) || PartNames.Contains(PartName) || !Part->TryGetStringField(TEXT("class"),PartClass) ||
       !Part->TryGetObjectField(TEXT("pose"),PartPose) || !PoseFrom(*PartPose,Pose)) return Fail(Error,TEXT("중복 부품 또는 유효하지 않은 부품 자세: ")+ActorId);
   PartNames.Add(PartName);auto* ComponentClass=AssetAllowed(PartClass) ? LoadObject<UClass>(nullptr,*PartClass) : nullptr;
   if (!ComponentClass || !ComponentClass->IsChildOf(USceneComponent::StaticClass()) || ComponentClass->IsChildOf(UPhysicsConstraintComponent::StaticClass())) return Fail(Error,TEXT("지원하지 않는 부품: ")+PartClass);
   if (Part->TryGetStringField(TEXT("mesh"),Mesh) && !Mesh.IsEmpty() && (!AssetAllowed(Mesh) || !LoadObject<UStaticMesh>(nullptr,*Mesh))) return Fail(Error,TEXT("필요 메시 누락: ")+Mesh);
   const TArray<FValue>* Materials=nullptr;if (Part->TryGetArrayField(TEXT("materials"),Materials)) for (const auto& M:*Materials)
    if (M->Type!=EJson::String || (!M->AsString().IsEmpty() && (!AssetAllowed(M->AsString()) || !LoadObject<UMaterialInterface>(nullptr,*M->AsString())))) return Fail(Error,TEXT("필요 재질 누락"));
   FString Physical;if (Part->TryGetStringField(TEXT("physicalMaterial"),Physical) && (!AssetAllowed(Physical) || !StaticLoadObject(UObject::StaticClass(),nullptr,*Physical))) return Fail(Error,TEXT("물리 재질 누락: ")+Physical);
  }
  const TSharedPtr<FJsonObject>* Props=nullptr;if (!Row->TryGetObjectField(TEXT("properties"),Props)) return Fail(Error,TEXT("액터 설정 누락"));
  FString CargoId;if ((*Props)->TryGetStringField(TEXT("CargoId"),CargoId) && CargoId!=TEXT("None") && !CargoId.IsEmpty())
  { if (CargoIds.Contains(CargoId)) return Fail(Error,TEXT("중복 화물 번호: ")+CargoId);CargoIds.Add(CargoId); }
  const TArray<FValue>* Held=nullptr;if ((*Props)->TryGetArrayField(TEXT("HeldCargo"),Held)) for (const auto& H:*Held)
  { if (H->Type!=EJson::Object || !H->AsObject()->TryGetStringField(TEXT("CargoId"),CargoId) || CargoIds.Contains(CargoId)) return Fail(Error,TEXT("중복 보류 화물 번호"));CargoIds.Add(CargoId); }
 }
 if (!CheckReferences(Object(Snapshot),Ids,Error)) return false;
 for (const auto& V:*Rows) for (const TCHAR* Field:{TEXT("wrapped"),TEXT("supported")})
 {
  const TArray<FValue>* Members=nullptr;if (V->AsObject()->TryGetArrayField(Field,Members)) for (const auto& Member:*Members)
   if (Member->Type!=EJson::String || !Ids.Contains(Member->AsString())) return Fail(Error,TEXT("끊어진 팔레트 물류 참조"));
 }
 FString Fingerprint;if (!Snapshot->TryGetStringField(TEXT("cargoFingerprint"),Fingerprint) || Fingerprint!=CargoFingerprint(Snapshot)) return Fail(Error,TEXT("물류 초기 조건이 일치하지 않습니다."));
 return true;
}
bool WarehouseExperimentStorage::ApplyBeforeBeginPlay(UWorld* World,const TSharedPtr<FJsonObject>& Snapshot,FString& Error)
{
 if (!World || World->HasBegunPlay()) return Fail(Error,TEXT("배치는 새 레벨의 BeginPlay 이전에만 불러올 수 있습니다."));
 if (!Validate(World,Snapshot,Error)) return false;
 FActors Actors;for (TActorIterator<AActor> It(World);It;++It) if (IsDataActor(*It)) Actors.Add(StableActorId(*It),*It);
 TSet<FString> Wanted;const auto& Rows=Snapshot->GetArrayField(TEXT("actors"));TArray<AActor*> Spawned;
 for (const auto& V:Rows)
 {
  const auto Row=V->AsObject();const FString Id=Row->GetStringField(TEXT("id"));Wanted.Add(Id);UClass* Class=LoadObject<UClass>(nullptr,*Row->GetStringField(TEXT("class")));
  AActor* A=Actors.FindRef(Id);if (A && A->GetClass()!=Class) return Fail(Error,TEXT("원본 맵의 액터 클래스가 달라졌습니다: ")+Id);
  if (!A)
  {
   FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;Params.bDeferConstruction=true;Params.Name=FName(*Row->GetStringField(TEXT("name")));
   A=World->SpawnActor<AActor>(Class,FTransform::Identity,Params);if (!A) return Fail(Error,TEXT("배치 액터 생성 실패: ")+Id);Spawned.Add(A);Actors.Add(Id,A);
  }
 }
 // Capture source geometry and its existing fixed contacts while the original
 // static bodies still have their authored mobility/poses. The placement pass
 // temporarily makes them movable, which must not erase that safety baseline.
 for (const auto& V:Rows)
  if (auto* Building=Cast<AWarehouseBuildingLayout>(Actors[V->AsObject()->GetStringField(TEXT("id"))])) Building->PrepareExperimentRestore();
 // Decode every reference only after all actors exist; no live simulation frame runs during the transaction.
 for (const auto& V:Rows)
 {
  const auto Row=V->AsObject();AActor* A=Actors[Row->GetStringField(TEXT("id"))];
  if (!DecodeProperties(A,Row->GetObjectField(TEXT("properties")),Actors,Error)) return false;
  A->Tags.RemoveAll([](FName Tag) { return Tag.ToString().StartsWith(IdPrefix); });A->Tags.Add(FName(*(IdPrefix+Row->GetStringField(TEXT("id")))));
  FTransform Pose;PoseFrom(Row->GetObjectField(TEXT("pose")),Pose);
  // Restore runs before play. The serialized component pass restores mobility
  // after placement; static roots must not reject this temporary transaction.
  if (auto* Root=A->GetRootComponent()) Root->SetMobility(EComponentMobility::Movable);
  if (Spawned.Contains(A)) UGameplayStatics::FinishSpawningActor(A,Pose);
  else { A->SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics);A->OnConstruction(Pose); }
 }
 for (const auto& V:Rows)
 {
  const auto Row=V->AsObject();AActor* A=Actors[Row->GetStringField(TEXT("id"))];TArray<USceneComponent*> Components;A->GetComponents(Components);
  auto PartRows=Row->GetArrayField(TEXT("parts"));
  auto Depth=[&](const FValue& Value)
  { int32 Result=0;for (auto* C:Components) if (C->GetName()==Value->AsObject()->GetStringField(TEXT("name"))) { for (auto* Parent=C->GetAttachParent();Parent;Parent=Parent->GetAttachParent()) ++Result;break; }return Result; };
  PartRows.StableSort([&](const FValue& APart,const FValue& BPart) { return Depth(APart)<Depth(BPart); });
  for (const auto& PartValue:PartRows)
  {
   const auto Part=PartValue->AsObject();const FString Name=Part->GetStringField(TEXT("name"));USceneComponent* C=nullptr;
   for (auto* Candidate:Components) if (Candidate->GetName()==Name) { C=Candidate;break; }
   if (!C) return Fail(Error,TEXT("복원할 원본 부품이 없습니다: ")+A->GetName()+TEXT(".")+Name);
   if (C->GetClass()->GetPathName()!=Part->GetStringField(TEXT("class"))) return Fail(Error,TEXT("부품 클래스가 변경되었습니다: ")+Name);
   C->ComponentTags.Reset();for (const auto& Tag:Part->GetArrayField(TEXT("tags"))) C->ComponentTags.Add(FName(*Tag->AsString()));
   C->SetMobility(EComponentMobility::Movable);
   if (auto* P=Cast<UPrimitiveComponent>(C)) { P->SetSimulatePhysics(false);P->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
   if (auto* Mesh=Cast<UStaticMeshComponent>(C))
   {
    FString MeshPath;if (Part->TryGetStringField(TEXT("mesh"),MeshPath)) Mesh->SetStaticMesh(MeshPath.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr,*MeshPath));
    const TArray<FValue>* Materials=nullptr;if (Part->TryGetArrayField(TEXT("materials"),Materials)) for (int32 I=0;I<Materials->Num();++I)
     Mesh->SetMaterial(I,(*Materials)[I]->AsString().IsEmpty() ? nullptr : LoadObject<UMaterialInterface>(nullptr,*(*Materials)[I]->AsString()));
   }
   const TSharedPtr<FJsonObject>* Light=nullptr;if (Part->TryGetObjectField(TEXT("light"),Light))
    for (const auto& Setting:(*Light)->Values) { auto* P=FindFProperty<FProperty>(C->GetClass(),*Setting.Key);if (!P || !Decode(P,P->ContainerPtrToValuePtr<void>(C),Setting.Value,Actors,Error)) return false; }
   const TSharedPtr<FJsonObject>* Text=nullptr;if (Part->TryGetObjectField(TEXT("text"),Text))
   {
    auto* Label=Cast<UTextRenderComponent>(C);if (!Label) return Fail(Error,TEXT("문자 부품 형식이 바뀌었습니다: ")+Name);
    for (const auto& Setting:(*Text)->Values) { auto* P=FindFProperty<FProperty>(C->GetClass(),*Setting.Key);if (!P || !Decode(P,P->ContainerPtrToValuePtr<void>(C),Setting.Value,Actors,Error)) return false; }
    Label->SetFont(Label->Font);
   }
   FTransform Pose;PoseFrom(Part->GetObjectField(TEXT("pose")),Pose);C->SetWorldTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics);C->SetVisibility(Part->GetBoolField(TEXT("visible")));
   if (auto* P=Cast<UPrimitiveComponent>(C))
   {
    P->SetCollisionObjectType(ECollisionChannel(int32(Part->GetNumberField(TEXT("objectType")))));
    const auto& Responses=Part->GetArrayField(TEXT("responses"));for (int32 I=0;I<FMath::Min(int32(ECC_MAX),Responses.Num());++I) P->SetCollisionResponseToChannel(ECollisionChannel(I),ECollisionResponse(int32(Responses[I]->AsNumber())));
    P->SetEnableGravity(Part->GetBoolField(TEXT("gravity")));P->SetCollisionEnabled(ECollisionEnabled::Type(int32(Part->GetNumberField(TEXT("collision")))));
    FString Material;if (Part->TryGetStringField(TEXT("physicalMaterial"),Material)) P->SetPhysMaterialOverride(LoadObject<UPhysicalMaterial>(nullptr,*Material));
    if (A->IsA<AStaticMeshActor>()) P->SetSimulatePhysics(Part->GetBoolField(TEXT("simulate")));
   }
   C->SetMobility(EComponentMobility::Type(int32(Part->GetNumberField(TEXT("mobility")))));
   C->MarkRenderStateDirty();
  }
  A->SetActorHiddenInGame(Row->GetBoolField(TEXT("hidden")));A->SetActorEnableCollision(Row->GetBoolField(TEXT("collision")));
#if WITH_EDITOR
  A->SetActorLabel(Row->GetStringField(TEXT("label")));
#endif
 }
 TArray<AActor*> Removed;for (const auto& Pair:Actors) if (!Wanted.Contains(Pair.Key)) Removed.Add(Pair.Value);
 for (AActor* A:Removed) A->Destroy();
 for (TActorIterator<AWarehouseBuildingLayout> It(World);It;++It) It->RestoreExperimentRuntimeState();
 for (TActorIterator<AWarehouseLayoutInventory> It(World);It;++It) It->RestoreExperimentCatalogue();
 TArray<WarehouseElevatorLayout::FSnapshotSource> FloorSources;TArray<AActor*> FloorReplacements;const TArray<FValue>* SourceRows=nullptr;const TArray<FValue>* ReplacementRows=nullptr;
 if (Snapshot->TryGetArrayField(TEXT("floorSources"),SourceRows)) for (const auto& V:*SourceRows)
 { const auto R=V->AsObject();FloorSources.Add({Actors.FindRef(R->GetStringField(TEXT("actor"))),R->GetBoolField(TEXT("visible")),R->GetBoolField(TEXT("hidden")),R->GetBoolField(TEXT("collision")),int32(R->GetNumberField(TEXT("collisionMode")))}); }
 if (Snapshot->TryGetArrayField(TEXT("floorReplacements"),ReplacementRows)) for (const auto& V:*ReplacementRows) FloorReplacements.Add(Actors.FindRef(V->AsObject()->GetStringField(TEXT("actor"))));
 AActor* Context=Rows.IsEmpty() ? nullptr : Actors.FindRef(Rows[0]->AsObject()->GetStringField(TEXT("id")));
 if (Context && !WarehouseElevatorLayout::RestoreExperimentState(Context,FloorSources,FloorReplacements)) return Fail(Error,TEXT("승강기 층별 바닥 기록을 복원할 수 없습니다."));
 return true;
}
bool WarehouseExperimentStorage::FinalizeAfterBeginPlay(UWorld* World,const TSharedPtr<FJsonObject>& Snapshot,FString& Error)
{
 if (!World || !World->HasBegunPlay() || !Snapshot) return Fail(Error,TEXT("복원된 레벨 초기화를 기다리세요."));
 for (const auto& V:Snapshot->GetArrayField(TEXT("actors")))
 {
  const auto Row=V->AsObject();double Receipt=0;if (!Row->TryGetNumberField(TEXT("receipt"),Receipt)) continue;
  auto* Pallet=Cast<AWarehousePallet>(ResolveActorId(World,Row->GetStringField(TEXT("id"))));if (!Pallet) return Fail(Error,TEXT("복원 팔레트가 없습니다."));
  if (int32(Receipt)==int32(EWarehouseReceiptState::Confirmed))
  { TArray<AActor*> Loads;for (const auto& Id:Row->GetArrayField(TEXT("wrapped"))) { AActor* Cargo=ResolveActorId(World,Id->AsString());if (!Cargo) return Fail(Error,TEXT("랩 안의 화물 참조가 끊어졌습니다."));Loads.Add(Cargo); }if (!Pallet->ConfirmInbound(Loads)) return Fail(Error,TEXT("팔레트 랩 초기화 실패: ")+Pallet->GetName()); }
  else if (int32(Receipt)==int32(EWarehouseReceiptState::WrapTorn)) Pallet->ReceiptState=EWarehouseReceiptState::WrapTorn;
 }
 return true;
}
void WarehouseExperimentStorage::ResetInitialVelocities(UWorld* World)
{
 if (!World || !World->HasBegunPlay()) return;
 // BeginPlay can precede the pause by one gravity/constraint step. Restore the
 // zero-momentum initial condition once, without sleeping bodies or changing
 // collisions, joints, masses, friction or any subsequent simulation step.
 for (TActorIterator<AActor> It(World);It;++It) if (IsDataActor(*It))
 {
  TArray<UPrimitiveComponent*> Parts;It->GetComponents(Parts);
  for (auto* Part:Parts) if (Part->IsSimulatingPhysics())
  { Part->SetPhysicsLinearVelocity(FVector::ZeroVector);Part->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector); }
 }
}
WarehouseExperimentStorage::FLayoutInfo WarehouseExperimentStorage::Describe(const TSharedPtr<FJsonObject>& Snapshot)
{
 FLayoutInfo I;if (!Snapshot) return I;Snapshot->TryGetStringField(TEXT("id"),I.Id);Snapshot->TryGetStringField(TEXT("name"),I.Name);Snapshot->TryGetStringField(TEXT("map"),I.MapPackage);Snapshot->TryGetStringField(TEXT("hash"),I.Hash);Snapshot->TryGetStringField(TEXT("cargoFingerprint"),I.CargoFingerprint);Snapshot->TryGetStringField(TEXT("savedAt"),I.SavedAt);
 const TArray<FValue>* Rows=nullptr;if (Snapshot->TryGetArrayField(TEXT("actors"),Rows)) I.ActorCount=Rows->Num();Snapshot->TryGetNumberField(TEXT("cargoCount"),I.CargoCount);Snapshot->TryGetNumberField(TEXT("heldCargoCount"),I.HeldCargoCount);return I;
}
bool WarehouseExperimentStorage::Archive(const FString& Id,FString& Error)
{
 if (!SafeId(Id)) return Fail(Error,TEXT("유효하지 않은 배치 ID"));
 const FString Path=FPaths::Combine(LayoutDirectory(),Id+TEXT(".json"));
 if (!FPaths::FileExists(Path)) return Fail(Error,TEXT("삭제·교체할 활성 배치를 찾을 수 없습니다."));
 // Archiving frees a slot even if the old layout's optional assets are missing.
 // Preserve the original bytes; only loading requires asset/schema validation.
 const FString Directory=FPaths::Combine(LayoutDirectory(),TEXT("Archive")),Archived=FPaths::Combine(Directory,Id+TEXT(".json"));
 if (FPaths::FileExists(Archived)) return Fail(Error,TEXT("같은 ID의 과거 배치가 이미 보관되어 있습니다."));
 if (!IFileManager::Get().MakeDirectory(*Directory,true) || !IFileManager::Get().Move(*Archived,*Path,false,true))
  return Fail(Error,TEXT("과거 실험 보관함으로 배치를 이동하지 못했습니다. 활성 배치는 유지됩니다."));
 return true;
}
bool WarehouseExperimentStorage::Write(const TSharedPtr<FJsonObject>& Snapshot,FLayoutInfo& Info,FString& Error,const FString& ReplaceId)
{
 if (!Validate(nullptr,Snapshot,Error)) return false;const auto NewInfo=Describe(Snapshot);
 const FString Path=FPaths::Combine(LayoutDirectory(),NewInfo.Id+TEXT(".json"));
 const FString ArchiveDirectory=FPaths::Combine(LayoutDirectory(),TEXT("Archive"));
 if (FPaths::FileExists(Path) || FPaths::FileExists(FPaths::Combine(ArchiveDirectory,NewInfo.Id+TEXT(".json"))))
  return Fail(Error,TEXT("같은 저장 ID가 이미 있습니다. 새 배치로 저장하세요."));
 if (ReplaceId.IsEmpty())
 { if (List().Num()>=MaxSavedLayouts) return Fail(Error,FString::Printf(TEXT("배치는 최대 %d개까지 저장할 수 있습니다. 기존 배치를 삭제하거나 교체하세요."),MaxSavedLayouts)); }
 else
 {
  if (!SafeId(ReplaceId) || !FPaths::FileExists(FPaths::Combine(LayoutDirectory(),ReplaceId+TEXT(".json"))))
   return Fail(Error,TEXT("교체할 활성 배치를 찾을 수 없습니다."));
 }
 if (!IFileManager::Get().MakeDirectory(*LayoutDirectory(),true)) return Fail(Error,TEXT("배치 저장 폴더를 만들 수 없습니다."));
 const FString Pending=Path+TEXT(".tmp");
 if (!FFileHelper::SaveStringToFile(Canonical(Object(Snapshot)),*Pending,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return Fail(Error,TEXT("새 배치 임시 파일 저장 실패. 기존 배치는 유지됩니다."));
 if (!ReplaceId.IsEmpty() && !Archive(ReplaceId,Error)) { IFileManager::Get().Delete(*Pending,false,true);return false; }
 if (!IFileManager::Get().Move(*Path,*Pending,false,true))
 {
  bool Restored=true;
  if (!ReplaceId.IsEmpty()) Restored=IFileManager::Get().Move(*FPaths::Combine(LayoutDirectory(),ReplaceId+TEXT(".json")),*FPaths::Combine(ArchiveDirectory,ReplaceId+TEXT(".json")),false,true);
  IFileManager::Get().Delete(*Pending,false,true);
  return Fail(Error,Restored ? TEXT("새 배치 저장 실패. 기존 활성 배치를 복구했습니다.") : TEXT("새 배치 저장과 활성 슬롯 복구가 실패했습니다. 기존 자료는 Archive 폴더에 보존되어 있습니다."));
 }
 Info=NewInfo;return true;
}
bool WarehouseExperimentStorage::Read(const FString& Id,TSharedPtr<FJsonObject>& Snapshot,FString& Error)
{
 if (!SafeId(Id)) return Fail(Error,TEXT("유효하지 않은 배치 ID"));FString Path=FPaths::Combine(LayoutDirectory(),Id+TEXT(".json"));
 if (!FPaths::FileExists(Path)) Path=FPaths::Combine(LayoutDirectory(),TEXT("Archive"),Id+TEXT(".json"));
 const int64 Size=IFileManager::Get().FileSize(*Path);if (Size<0 || Size>64*1024*1024) return Fail(Error,TEXT("배치 파일이 없거나 64MB 제한을 넘었습니다."));
 FString Text;if (!FFileHelper::LoadFileToString(Text,*Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Snapshot)) return Fail(Error,TEXT("배치 JSON 형식 오류"));
 if (!Validate(nullptr,Snapshot,Error)) return false;return Snapshot->GetStringField(TEXT("id"))==Id || Fail(Error,TEXT("파일과 배치 ID가 다릅니다."));
}
TArray<WarehouseExperimentStorage::FLayoutInfo> WarehouseExperimentStorage::List()
{
 // UI polling must not deserialize thousands of actors or reload assets each second.
 struct FCachedInfo { FDateTime Modified;int64 Size=-1;FLayoutInfo Info; };
 static TMap<FString,FCachedInfo> Cache;
 TArray<FLayoutInfo> Result;TArray<FString> Files;IFileManager::Get().FindFiles(Files,*FPaths::Combine(LayoutDirectory(),TEXT("*.json")),true,false);
 for (const FString& File:Files)
 {
  const FString Id=FPaths::GetBaseFilename(File);if (!SafeId(Id)) continue;
  const FString Path=FPaths::Combine(LayoutDirectory(),File);const auto Modified=IFileManager::Get().GetTimeStamp(*Path);const int64 Size=IFileManager::Get().FileSize(*Path);
  if (const auto* Existing=Cache.Find(Path);Existing && Existing->Modified==Modified && Existing->Size==Size) { Result.Add(Existing->Info);continue; }
  FString Text;TSharedPtr<FJsonObject> Snapshot;
  if (Size<0 || Size>64*1024*1024 || !FFileHelper::LoadFileToString(Text,*Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Snapshot)) continue;
  const auto Info=Describe(Snapshot);FString Hash;
  if (Info.Id!=Id || !Snapshot->TryGetStringField(TEXT("hash"),Hash) || Hash!=SnapshotHash(Snapshot)) continue;
  Cache.Add(Path,{Modified,Size,Info});Result.Add(Info);
 }
 Result.Sort([](const auto& A,const auto& B) { return A.SavedAt>B.SavedAt; });return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarehouseExperimentStorageValues,
 "Warehouse.Experiments.SnapshotValues",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWarehouseExperimentStorageValues::RunTest(const FString& Parameters)
{
 FActors Actors;FString Error;
 const FTransform FinePose(FRotator(0,19.123456789,0),FVector(1234.123456789,-987.987654321,.000012345),FVector(.571095123,1.335154321,2.132712345));
 FTransform FineRestored;TestTrue(TEXT("Precise layout pose parses"),PoseFrom(PoseJson(FinePose),FineRestored));
 TestTrue(TEXT("Layout translation retains sub-millimetre precision"),FinePose.GetLocation().Equals(FineRestored.GetLocation(),.00000001));
 TestTrue(TEXT("Layout scale retains fixed-contact tolerance"),FinePose.GetScale3D().Equals(FineRestored.GetScale3D(),.00000001));
 auto RoundTrip=[&](FProperty* Property,const void* Before,void* After,const TCHAR* Label)
 {
  if (!TestNotNull(Label,Property)) return false;
  auto Encoded=Encode(Property,Before,Actors,Error);
  if (!TestTrue(FString(Label)+TEXT(" export: ")+Error,Error.IsEmpty() && Encoded.IsValid())) return false;
  if (!TestTrue(FString(Label)+TEXT(" import: ")+Error,Decode(Property,After,Encoded,Actors,Error))) return false;
  auto Reencoded=Encode(Property,After,Actors,Error);
  return TestEqual(FString(Label)+TEXT(" values preserved"),Canonical(Reencoded),Canonical(Encoded));
 };
 FWarehouseCargoRecipe Recipe,RestoredRecipe;
 Recipe.ItemIndex=0;Recipe.SizeCm=FVector(30.125,25.375,20.625);Recipe.GrossMassKg=14.25f;Recipe.ProductKind=FText::AsCultureInvariant(TEXT("검사 상품"));Recipe.bValid=true;
 RoundTrip(FindFProperty<FProperty>(AWarehouseCargo::StaticClass(),TEXT("Packing")),&Recipe,&RestoredRecipe,TEXT("Nested recipe: zero, bool, text and vector"));
 TestEqual(TEXT("Zero item index survives temporary JSON access"),RestoredRecipe.ItemIndex,0);
 TestEqual(TEXT("Recipe vector survives"),RestoredRecipe.SizeCm,Recipe.SizeCm);
 TArray<FWarehouseHeldCargo> Held,RestoredHeld;
 auto& Cargo=Held.AddDefaulted_GetRef();Cargo.CargoId=TEXT("CHECK-001");Cargo.Product=FText::AsCultureInvariant(TEXT(""));Cargo.MassKg=14.25f;Cargo.Packing=Recipe;
 Cargo.BodyPose=FTransform(FRotator(0,90,0),FVector(0,-125.5,300.25),FVector(.125,.25,.5));
 RoundTrip(FindFProperty<FProperty>(AWarehouseLayoutInventory::StaticClass(),TEXT("HeldCargo")),&Held,&RestoredHeld,TEXT("Held FIFO array and transform"));
 TestEqual(TEXT("Held FIFO length"),RestoredHeld.Num(),1);
 if (!RestoredHeld.IsEmpty()) TestEqual(TEXT("FName preserved"),RestoredHeld[0].CargoId,Cargo.CargoId);
 TArray<FWarehouseLayoutPrototype> Prototypes,RestoredPrototypes;
 auto& Prototype=Prototypes.AddDefaulted_GetRef();Prototype.Name=TEXT("저장 검사");Prototype.Category=EWarehouseLayoutCategory::Rack;
 auto& Actor=Prototype.Actors.AddDefaulted_GetRef();Actor.Class=AWarehouseCargo::StaticClass();Actor.Tags={TEXT("A"),TEXT("B")};
 Actor.Properties.Add(TEXT("Empty"),TEXT(""));Actor.Properties.Add(TEXT("Escaped"),TEXT("Line 1\n\"Line 2\"\\path"));
 Prototype.ZoneDestinations.Add(FTransform::Identity);Prototype.Profiles.AddDefaulted();
 RoundTrip(FindFProperty<FProperty>(AWarehouseLayoutInventory::StaticClass(),TEXT("Prototypes")),&Prototypes,&RestoredPrototypes,TEXT("Nested arrays, enum, class asset and string map"));
 return !HasAnyErrors();
}
#endif
