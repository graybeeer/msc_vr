#include "WarehouseLayoutInventory.h"
#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "WarehouseWaitingBay.h"
#include "WarehouseChargingStation.h"
#include "WarehouseComputer.h"
#include "WarehouseElevator.h"
#include "WarehousePalletLift.h"
#include "WarehouseConveyor.h"
#include "WarehouseWorker.h"
#include "WarehouseElevatorLayout.h"
#include "msc_vrCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

namespace
{
 FName GroupTag(const AActor* Actor)
 { for (FName Tag:Actor->Tags) if (Tag.ToString().StartsWith(TEXT("LayoutGroup:"))) return Tag;return NAME_None; }
 bool HasObjectReference(const FProperty* Property)
 {
  if (CastField<FObjectPropertyBase>(Property) || CastField<FSoftObjectProperty>(Property) || CastField<FWeakObjectProperty>(Property)) return true;
  if (const auto* Array=CastField<FArrayProperty>(Property)) return HasObjectReference(Array->Inner);
  if (const auto* Struct=CastField<FStructProperty>(Property))
   for (TFieldIterator<FProperty> It(Struct->Struct);It;++It) if (HasObjectReference(*It)) return true;
  return CastField<FMapProperty>(Property) || CastField<FSetProperty>(Property);
 }
 FString NewId(const TCHAR* Prefix) { return FString(Prefix)+FGuid::NewGuid().ToString(EGuidFormats::Digits); }
}
AWarehouseLayoutInventory::AWarehouseLayoutInventory()
{
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Inventory")));Tags.Add(TEXT("LayoutFixed"));
 DefaultCargoMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2.SM_Carton_Intact_2")));
 NewCargoStrength.Failure=EWarehouseFailure::Crush;NewCargoStrength.RatedLoadKg=120;
}
AWarehouseLayoutInventory* AWarehouseLayoutInventory::Find(const AActor* Context)
{
 if (!IsValid(Context) || !Context->GetWorld()) return nullptr;
 for (TActorIterator<AWarehouseLayoutInventory> It(Context->GetWorld());It;++It) return *It;
 return Context->GetWorld()->SpawnActor<AWarehouseLayoutInventory>();
}
bool AWarehouseLayoutInventory::CreateHeldCargo(FName Id,FText Product,float MassKg,FVector SizeCm)
{
 if (!HasAuthority()) return false;
 if (Id.IsNone() || Id.ToString().Len()>64 || Product.IsEmpty() || !FMath::IsFinite(MassKg) || MassKg<.1f || MassKg>10000 ||
     SizeCm.ContainsNaN() || SizeCm.GetMin()<5 || SizeCm.GetMax()>300)
 { LastMessage=TEXT("화물 번호·품목, 무게 0.1~10000kg, 크기 각 변 5~300cm를 입력하세요.");return false; }
 if (HeldCargo.ContainsByPredicate([&](const auto& Row) { return Row.CargoId==Id; }))
 { LastMessage=TEXT("보류된 물류에 같은 화물 번호가 있습니다.");return false; }
 for (TActorIterator<AWarehouseCargo> It(GetWorld());It;++It) if (It->CargoId==Id)
 { LastMessage=TEXT("현장에 같은 화물 번호가 있습니다.");return false; }
 auto* Mesh=DefaultCargoMesh.LoadSynchronous();
 if (!Mesh) { LastMessage=TEXT("온전한 상자 에셋이 없습니다. 팀원 에셋 설치 안내를 확인하세요.");return false; }
 FWarehouseHeldCargo Row;Row.CargoId=Id;Row.Product=Product;Row.MassKg=MassKg;Row.SizeCm=SizeCm;Row.Mesh=Mesh;
 Row.ActorScale=SizeCm/Mesh->GetBoundingBox().GetSize();
 Row.Strength=NewCargoStrength;Row.Strength.Name=Id.ToString();Row.Strength.MassKg=MassKg;Row.Strength.Members.Reset();Row.Strength.PhysicsMeshes.Reset();
 Row.Packing.bValid=true;Row.Packing.SizeCm=SizeCm;Row.Packing.SizeLimitCm=SizeCm;
 Row.Packing.ProductKind=Product;Row.Packing.GrossMassKg=MassKg;Row.Packing.NetMassKg=FMath::Max(0.f,MassKg-.1f);
 Row.Packing.PackagingMassKg=MassKg-Row.Packing.NetMassKg;
 Row.Packing.PackedDensityKgM3=MassKg/(SizeCm.X*SizeCm.Y*SizeCm.Z/1000000.);
 HeldCargo.Add(Row);++Revision;LastMessage=TEXT("새 물류 데이터를 보류된 물류의 마지막에 추가했습니다.");return true;
}
bool AWarehouseLayoutInventory::DeleteHeldCargo(FName Id)
{
 if (!HasAuthority()) return false;
 const int32 Index=HeldCargo.IndexOfByPredicate([&](const auto& Row) { return Row.CargoId==Id; });
 if (Index==INDEX_NONE) { LastMessage=TEXT("삭제할 보류 물류가 없습니다.");return false; }
 HeldCargo.RemoveAt(Index);++Revision;LastMessage=TEXT("선택한 보류 물류 데이터를 삭제했습니다.");return true;
}
bool AWarehouseLayoutInventory::HoldCargo(AWarehouseCargo* Cargo)
{
 if (!HasAuthority() || !IsValid(Cargo) || Cargo->GetWorld()!=GetWorld()) return false;
 FWarehouseHeldCargo Row;Row.CargoId=Cargo->CargoId.IsNone() ? FName(*NewId(TEXT("CARGO-"))) : Cargo->CargoId;
 if (HeldCargo.ContainsByPredicate([&](const auto& Existing) { return Existing.CargoId==Row.CargoId; }))
 { LastMessage=TEXT("화물 번호가 중복되어 보류할 수 없습니다.");return false; }
 Row.Product=Cargo->CargoKind;Row.MassKg=Cargo->GrossMassKg;Row.Packing=Cargo->Packing;
 auto* Body=Cargo->GetCargoBody();Row.Mesh=Body->GetStaticMesh();Row.ActorScale=Cargo->GetActorScale3D();
 Row.BodyPose=Body->GetComponentTransform().GetRelativeTransform(Cargo->GetActorTransform());
 Row.SizeCm=Row.Mesh ? Row.Mesh->GetBoundingBox().GetSize()*Body->GetComponentScale() : FVector::ZeroVector;
 for (int32 I=0;I<Body->GetNumMaterials();++I) Row.Materials.Add(Body->GetMaterial(I));
 Row.Strength=NewCargoStrength;
 if (auto* Strength=AWarehouseDamageSystem::Find(this))
  if (const auto* Profile=Strength->Objects.FindByPredicate([&](const auto& Entry) { return Entry.Members.Contains(Cargo); })) Row.Strength=*Profile;
 Row.Strength.Members.Reset();Row.Strength.PhysicsMeshes.Reset();Row.Strength.MassKg=Row.MassKg;
 HeldCargo.Add(Row);++Revision;Cargo->WakeStackAbove();LastMessage=TEXT("상자의 품목·무게·크기·재질·손상 데이터를 보류했습니다.");return true;
}
bool AWarehouseLayoutInventory::ConsumeHeldCargo(FName Id)
{
 if (!HasAuthority() || HeldCargo.IsEmpty() || HeldCargo[0].CargoId!=Id) return false;
 HeldCargo.RemoveAt(0);++Revision;LastMessage=TEXT("첫 보류 물류를 현장에 배치했습니다. 다음 물류를 배치할 수 있습니다.");return true;
}
AWarehouseCargo* AWarehouseLayoutInventory::PreviewHeldCargo(FVector Point)
{
 if (!HasAuthority() || HeldCargo.IsEmpty() || Point.ContainsNaN()) { LastMessage=TEXT("보류된 물류가 없습니다. 중앙 컴퓨터에서 데이터를 추가하세요.");return nullptr; }
 const auto& Row=HeldCargo[0];if (!IsValid(Row.Mesh)) return nullptr;
 FTransform Pose(FQuat::Identity,Point,Row.ActorScale);
 auto* Cargo=GetWorld()->SpawnActorDeferred<AWarehouseCargo>(AWarehouseCargo::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if (!Cargo) return nullptr;
 Cargo->CargoId=Row.CargoId;Cargo->CargoKind=Row.Product;Cargo->Packing=Row.Packing;Cargo->SetCargoMesh(Row.Mesh);Cargo->SetGrossMassKg(Row.MassKg);
 // CargoBody is the actor root. Resetting its relative transform here would
 // erase the deferred spawn scale, producing the mesh's original dimensions.
 if (Cargo->GetCargoBody()!=Cargo->GetRootComponent()) Cargo->GetCargoBody()->SetRelativeTransform(Row.BodyPose);
 for (int32 I=0;I<Row.Materials.Num();++I) Cargo->GetCargoBody()->SetMaterial(I,Row.Materials[I]);
 Cargo->Tags.Add(TEXT("LayoutType:Other"));UGameplayStatics::FinishSpawningActor(Cargo,Pose);return Cargo;
}
void AWarehouseLayoutInventory::Capture(EWarehouseLayoutCategory Category,const FString& Name,const TArray<AActor*>& Members,FVector Origin,const FWarehouseLayoutZone* Zone)
{
 if (Members.IsEmpty()) return;
 FWarehouseLayoutPrototype Prototype;Prototype.Category=Category;Prototype.Name=Name;
 const FTransform Anchor(FQuat::Identity,Origin);
 for (AActor* Actor:Members)
 {
  FWarehouseLayoutActorTemplate Item;Item.Class=Actor->GetClass();Item.LocalPose=Actor->GetActorTransform().GetRelativeTransform(Anchor);Item.Tags=Actor->Tags;
  for (TFieldIterator<FProperty> It(Actor->GetClass());It;++It)
   if (It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_Transient) && !HasObjectReference(*It) && It->GetFName()!=TEXT("Tags"))
   { FString Value;It->ExportTextItem_Direct(Value,It->ContainerPtrToValuePtr<void>(Actor),nullptr,Actor,PPF_Copy);Item.Properties.Add(It->GetFName(),Value); }
  TArray<USceneComponent*> Parts;Actor->GetComponents(Parts);
  for (auto* Part:Parts)
  {
   FWarehouseLayoutPartTemplate SavedPart;SavedPart.Name=Part->GetFName();SavedPart.LocalPose=Part->GetComponentTransform().GetRelativeTransform(Actor->GetActorTransform());
   if (auto* Mesh=Cast<UStaticMeshComponent>(Part))
   {
    SavedPart.Mesh=Mesh->GetStaticMesh();SavedPart.Collision=Mesh->GetCollisionEnabled();SavedPart.Responses=Mesh->GetCollisionResponseToChannels();
    for (int32 I=0;I<Mesh->GetNumMaterials();++I) SavedPart.Materials.Add(Mesh->GetMaterial(I));
   }
   Item.Parts.Add(SavedPart);
  }
  Prototype.Actors.Add(Item);
 }
 if (auto* Strength=AWarehouseDamageSystem::Find(this)) for (const auto& Profile:Strength->Objects)
 {
  if (Profile.Members.IsEmpty() || !Profile.Members.ContainsByPredicate([&](AActor* A) { return Members.Contains(A); })) continue;
  if (Profile.Members.ContainsByPredicate([&](AActor* A) { return !Members.Contains(A); })) continue;
  FWarehouseLayoutProfileTemplate SavedProfile;SavedProfile.Strength=Profile;
  for (AActor* Actor:Profile.Members) SavedProfile.Members.Add(Members.IndexOfByKey(Actor));
  SavedProfile.Strength.Members.Reset();SavedProfile.Strength.Damage=0;SavedProfile.Strength.bFailed=false;
  for (float& Height:SavedProfile.Strength.ShelfHeightsCm) Height-=Origin.Z;
  Prototype.Profiles.Add(SavedProfile);
 }
 for (TActorIterator<AWarehouseInboundZone> It(GetWorld());It;++It)
  for (auto Slot:It->RackSlots) if (Members.Contains(Slot.Rack))
  { Slot.Rack=nullptr;Slot.Pose=Slot.Pose.GetRelativeTransform(Anchor);Prototype.RackSlots.Add(Slot); }
 if (Zone)
 {
  Prototype.ZoneHalfSize=Zone->HalfSize;Prototype.bOutbound=Zone->Name==TEXT("Outbound");
  if (Prototype.bOutbound) for (auto Pose:Zone->Destinations) Prototype.ZoneDestinations.Add(Pose.GetRelativeTransform(Anchor));
 }
 Prototypes.Add(MoveTemp(Prototype));
}
void AWarehouseLayoutInventory::CapturePrototypes(AWarehouseLayoutEditor* Layout)
{
 if (bCaptured || !Layout) return;bCaptured=true;
 for (TActorIterator<AWarehouseInboundZone> It(GetWorld());It;++It)
  for (auto Slot:It->RackSlots) if (IsValid(Slot.Rack) && !RackCatalogue.ContainsByPredicate([&](const auto& Existing) { return Existing.Name==Slot.Name && Existing.Rack==Slot.Rack; }))
  { Slot.Pose=Slot.Pose.GetRelativeTransform(Slot.Rack->GetActorTransform());RackCatalogue.Add(Slot); }
 // Racks share one storage model. Prefer the complete, registered slot template
 // instead of whichever frame happens to appear first in the actor iterator.
 auto RackKey=[](const AActor* A) { const FName Group=GroupTag(A);return Group.IsNone() ? A->GetFName() : Group; };
 TMap<FName,int32> SlotCounts;
 for (const auto& Slot:RackCatalogue) if (IsValid(Slot.Rack)) ++SlotCounts.FindOrAdd(RackKey(Slot.Rack));
 AActor* RackTemplate=nullptr;int32 BestSlotRank=-1,BestModel=-1;FString BestRackKey;
 for (TActorIterator<AActor> It(GetWorld());It;++It)
 {
  AActor* A=*It;if (!A->ActorHasTag(TEXT("LayoutType:Rack")) || A->ActorHasTag(TEXT("LayoutFixed"))) continue;
  const FName Group=RackKey(A);const int32 Slots=SlotCounts.FindRef(Group);
  const int32 SlotRank=Slots==4 ? MAX_int32 : Slots;
  const FString Key=Group.ToString()+TEXT("/")+A->GetActorNameOrLabel();
  const int32 Model=Group.ToString().StartsWith(TEXT("LayoutGroup:WH_InboundRack_")) ? 2 :
   Group==TEXT("LayoutGroup:PalletRack_Left_00") ? 1 : 0;
  if (SlotRank>BestSlotRank || (SlotRank==BestSlotRank && (Model>BestModel || (Model==BestModel && Key<BestRackKey))))
  { RackTemplate=A;BestSlotRank=SlotRank;BestModel=Model;BestRackKey=Key; }
 }
 // Legacy/test maps may have no registered storage slots. Preserve their real
 // template data rather than inventing load positions from visual bounds.
 for (auto& Zone:Layout->Zones) if (Zone.Name==TEXT("Outbound") && Zone.Destinations.IsEmpty())
  for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It) Zone.Destinations=It->OutboundDestinations;
 TSet<FName> Groups;TSet<FString> Singles;
 for (TActorIterator<AActor> It(GetWorld());It;++It)
 {
  AActor* A=*It;EWarehouseLayoutCategory Category=EWarehouseLayoutCategory::Other;
  if (A->IsA<AWarehouseCargo>() || A==this || A==Layout || (A->ActorHasTag(TEXT("LayoutFixed")) && !A->IsA<AWarehouseChargingStation>()) || A->IsA<Amsc_vrCharacter>()) continue;
  if (A->IsA<AWarehouseForklift>()) Category=EWarehouseLayoutCategory::Forklift;
  else if (A->IsA<AWarehouseConveyor>()) Category=EWarehouseLayoutCategory::Conveyor;
  else if (A->IsA<AWarehouseChargingStation>()) Category=EWarehouseLayoutCategory::Charger;
  else if (A->IsA<AWarehouseWaitingBay>()) Category=EWarehouseLayoutCategory::WaitingBay;
  else if (A->IsA<AWarehousePallet>()) Category=EWarehouseLayoutCategory::Pallet;
  else if (A->IsA<AWarehouseElevator>() || A->IsA<AWarehousePalletLift>() || A->ActorHasTag(TEXT("LayoutType:Elevator"))) Category=EWarehouseLayoutCategory::Elevator;
  else if (A->ActorHasTag(TEXT("LayoutType:Rack"))) Category=EWarehouseLayoutCategory::Rack;
  else if (!A->ActorHasTag(TEXT("LayoutType:Other"))) continue;
  if (Category==EWarehouseLayoutCategory::Rack && A!=RackTemplate) continue;
  const FName Group=GroupTag(A);TArray<AActor*> Members;
  if (Group!=NAME_None)
  {
   if (Groups.Contains(Group)) continue;Groups.Add(Group);
   for (TActorIterator<AActor> Member(GetWorld());Member;++Member) if (Member->ActorHasTag(Group) && !Member->IsA<AWarehouseCargo>() && !Member->IsA<AWarehousePallet>()) Members.Add(*Member);
  }
  else
  {
   // Workers can share a class but have different roles; static furniture
   // shares StaticMeshActor but has different meshes. Keep those presets.
   const FString Key=A->GetClass()->GetPathName()+FString::FromInt(int32(Category))+
    (A->IsA<AWarehouseConveyor>() ? FString::FromInt(int32(Cast<AWarehouseConveyor>(A)->Kind)) : FString())+
    (Category==EWarehouseLayoutCategory::Other ? A->GetActorNameOrLabel() : FString());
   if (Singles.Contains(Key)) continue;Singles.Add(Key);Members.Add(A);
  }
  if (Members.IsEmpty()) continue;
  FVector Origin=A->GetActorLocation();FString Name=A->GetActorNameOrLabel();
  if (Category==EWarehouseLayoutCategory::Rack) Name=TEXT("팔레트 보관 랙");
  if (const auto* Conveyor=Cast<AWarehouseConveyor>(A)) Name=Conveyor->Kind==EWarehouseConveyorKind::PalletRoller ? TEXT("팔레트 롤러 컨베이어") :
   Conveyor->Kind==EWarehouseConveyorKind::RotaryTransfer ? TEXT("90° 회전 이송대") : TEXT("상자 컨베이어");
  if (Category==EWarehouseLayoutCategory::Elevator) for (AActor* Member:Members)
  {
   if (Member->IsA<AWarehouseElevator>()) { Origin=Member->GetActorLocation();Name=TEXT("범용 엘리베이터");break; }
   if (Member->IsA<AWarehousePalletLift>()) { Origin=Member->GetActorLocation();Name=TEXT("화물용 엘리베이터");break; }
  }
  Origin.Z=400.*FMath::FloorToDouble((FMath::Max(0.,Origin.Z)+1)/400.);
  Capture(Category,Name,Members,Origin);
 }
 for (const auto& Zone:Layout->Zones)
 {
  TArray<AActor*> Members;for (AActor* A:Zone.Members) if (IsValid(A)) Members.Add(A);
  Capture(EWarehouseLayoutCategory::Zone,Zone.Name==TEXT("Outbound") ? TEXT("출고 대기 구역") : TEXT("입고 대기 구역"),Members,Zone.Centre,&Zone);
 }
 auto AddDefault=[&](UClass* Class,EWarehouseLayoutCategory Category,const TCHAR* Name)
 {
  if (Prototypes.ContainsByPredicate([&](const auto& P)
   { return P.Category==Category && (Category!=EWarehouseLayoutCategory::Elevator ||
     P.Actors.ContainsByPredicate([&](const auto& A) { return A.Class && A.Class->IsChildOf(Class); })); })) return;
  FWarehouseLayoutPrototype P;P.Name=Name;P.Category=Category;FWarehouseLayoutActorTemplate A;A.Class=Class;P.Actors.Add(A);Prototypes.Add(P);
 };
 AddDefault(AWarehouseWaitingBay::StaticClass(),EWarehouseLayoutCategory::WaitingBay,TEXT("일반 대기 자리"));
 AddDefault(AWarehouseChargingStation::StaticClass(),EWarehouseLayoutCategory::Charger,TEXT("공유 충전 자리"));
 AddDefault(AWarehouseForklift::StaticClass(),EWarehouseLayoutCategory::Forklift,TEXT("무인 지게차"));
 AddDefault(AWarehousePallet::StaticClass(),EWarehouseLayoutCategory::Pallet,TEXT("빈 팔레트"));
 AddDefault(AWarehouseElevator::StaticClass(),EWarehouseLayoutCategory::Elevator,TEXT("범용 엘리베이터"));
 AddDefault(AWarehousePalletLift::StaticClass(),EWarehouseLayoutCategory::Elevator,TEXT("화물용 엘리베이터"));
 for (int32 Kind=0;Kind<3;++Kind)
 {
  const FString Name=Kind==0 ? TEXT("팔레트 롤러 컨베이어") : Kind==2 ? TEXT("90° 회전 이송대") : TEXT("상자 컨베이어");
  if (Prototypes.ContainsByPredicate([&](const auto& P) { return P.Category==EWarehouseLayoutCategory::Conveyor && P.Name==Name; })) continue;
  FWarehouseLayoutPrototype P;P.Name=Name;P.Category=EWarehouseLayoutCategory::Conveyor;
  FWarehouseLayoutActorTemplate Item;Item.Class=AWarehouseConveyor::StaticClass();
  Item.Properties.Add(TEXT("Kind"),Kind==0 ? TEXT("PalletRoller") : Kind==2 ? TEXT("RotaryTransfer") : TEXT("CartonBelt"));
  if (Kind==1) { Item.Properties.Add(TEXT("WidthCm"),TEXT("80"));Item.Properties.Add(TEXT("RatedMassKg"),TEXT("100")); }
  if (Kind==2) { Item.Properties.Add(TEXT("LengthCm"),TEXT("200"));Item.Properties.Add(TEXT("WidthCm"),TEXT("200")); }
  P.Actors.Add(Item);Prototypes.Add(P);
 }
}
TArray<FString> AWarehouseLayoutInventory::Choices(EWarehouseLayoutCategory Category) const
{
 TArray<FString> Names;
 if (Category==EWarehouseLayoutCategory::All) return Names;
 if (Category==EWarehouseLayoutCategory::Cargo)
 { if (!HeldCargo.IsEmpty()) Names.Add(HeldCargo[0].CargoId.ToString()+TEXT(" · ")+HeldCargo[0].Product.ToString());return Names; }
 for (const auto& P:Prototypes) if (P.Category==Category) Names.Add(P.Name);
 return Names;
}
TArray<AActor*> AWarehouseLayoutInventory::SpawnPrototype(EWarehouseLayoutCategory Category,int32 Choice,FVector Point)
{
 TArray<AActor*> Result;if (!HasAuthority() || Point.ContainsNaN()) return Result;
 if (Category==EWarehouseLayoutCategory::All) { LastMessage=TEXT("추가할 물체의 개별 종류를 먼저 선택하세요.");return Result; }
 int32 Index=INDEX_NONE,Count=0;
 for (int32 I=0;I<Prototypes.Num();++I) if (Prototypes[I].Category==Category && Count++==Choice) { Index=I;break; }
 if (Index==INDEX_NONE) { LastMessage=TEXT("추가할 요소의 원본이 없습니다.");return Result; }
 const auto& P=Prototypes[Index];const FTransform Anchor(FQuat::Identity,Point);const FName Group(*NewId(TEXT("LayoutGroup:Runtime-")));
 for (const auto& Item:P.Actors)
 {
  const FTransform Pose=Item.LocalPose*Anchor;
  auto* Actor=GetWorld()->SpawnActorDeferred<AActor>(Item.Class,Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if (!Actor) { for (AActor* Added:Result) Added->Destroy();return {}; }
  for (const auto& Property:Item.Properties)
   if (auto* Field=Actor->GetClass()->FindPropertyByName(Property.Key)) Field->ImportText_Direct(*Property.Value,Field->ContainerPtrToValuePtr<void>(Actor),Actor,PPF_Copy);
  for (FName Tag:Item.Tags) Actor->Tags.AddUnique(Tag);
  Actor->Tags.RemoveAll([](FName T) { return T.ToString().StartsWith(TEXT("LayoutGroup:")) || T.ToString().StartsWith(TEXT("WarehouseFloor")) || T==TEXT("LayoutFixed"); });
  Actor->Tags.Add(Group);Actor->Tags.Add(FName(*FString::Printf(TEXT("WarehouseFloor%d"),FMath::Clamp(FMath::FloorToInt((Pose.GetLocation().Z+1)/400.),0,3))));
  TArray<USceneComponent*> Parts;Actor->GetComponents(Parts);
  for (const auto& Part:Item.Parts)
   if (auto** Found=Parts.FindByPredicate([&](const auto* C) { return C->GetFName()==Part.Name; }))
   {
    auto* Component=*Found;
    const auto Mobility=Component->Mobility;Component->SetMobility(EComponentMobility::Movable);
    if (auto* Mesh=Cast<UStaticMeshComponent>(Component);Mesh && Part.Mesh)
    {
     Mesh->SetStaticMesh(Part.Mesh);Mesh->SetCollisionEnabled(Part.Collision);Mesh->SetCollisionResponseToChannels(Part.Responses);
     for (int32 I=0;I<Part.Materials.Num();++I) Mesh->SetMaterial(I,Part.Materials[I]);
    }
    Component->SetWorldTransform(Part.LocalPose*Pose);
    Component->SetMobility(Mobility);
   }
  if (auto* Vehicle=Cast<AWarehouseForklift>(Actor))
  {
   Vehicle->bAutoStart=false;Vehicle->PendingJobs.Reset();Vehicle->VehicleName=TEXT("지게차 ")+Actor->GetName();
   for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (*It!=Vehicle)
   { Vehicle->NavigationAnchors=It->NavigationAnchors;break; }
  }
  if (auto* Pallet=Cast<AWarehousePallet>(Actor)) Pallet->PayloadMassKg=0;
  UGameplayStatics::FinishSpawningActor(Actor,Pose);Result.Add(Actor);
 }
 if (!Result.IsEmpty()) PreviewPrototypes.Add(Result[0],Index);
 return Result;
}
FVector AWarehouseLayoutInventory::PreviewZoneSize(const TArray<AActor*>& Members) const
{
 if (Members.IsEmpty()) return FVector::ZeroVector;
 const int32* Index=PreviewPrototypes.Find(Members[0]);
 return Index && Prototypes.IsValidIndex(*Index) ? Prototypes[*Index].ZoneHalfSize : FVector::ZeroVector;
}
void AWarehouseLayoutInventory::CommitPrototype(const TArray<AActor*>& Members)
{
 if (Members.IsEmpty()) return;
 const int32* Index=PreviewPrototypes.Find(Members[0]);if (!Index || !Prototypes.IsValidIndex(*Index)) return;
 const auto& P=Prototypes[*Index];const FTransform Anchor=P.Actors[0].LocalPose.Inverse()*Members[0]->GetActorTransform();
 if (auto* Strength=AWarehouseDamageSystem::Find(this)) for (const auto& Profile:P.Profiles)
 {
  auto Spec=Profile.Strength;Spec.Name=Members[0]->GetName();
  for (int32 I:Profile.Members) if (Members.IsValidIndex(I)) Spec.Members.Add(Members[I]);
  for (float& Height:Spec.ShelfHeightsCm) Height+=Anchor.GetLocation().Z;
  Strength->RegisterLayoutProfile(Spec);
 }
 if (P.Profiles.IsEmpty()) if (auto* Strength=AWarehouseDamageSystem::Find(this))
  for (AActor* A:Members)
  {
   FWarehouseStrength Spec;Spec.Name=A->GetName();Spec.Members={A};
   if (auto* V=Cast<AWarehouseForklift>(A)) { Spec.MassKg=V->VehicleMassKg;Spec.RatedLoadKg=V->RatedLoadKg;Spec.Failure=EWarehouseFailure::Machine; }
   else if (auto* Pallet=Cast<AWarehousePallet>(A)) { Spec.MassKg=Pallet->PalletMassKg;Spec.RatedLoadKg=1500;Spec.Failure=EWarehouseFailure::Crush; }
   else continue;
   Strength->RegisterLayoutProfile(Spec);
  }
 for (auto Slot:P.RackSlots)
 {
  Slot.Name=Members[0]->GetName()+TEXT("-")+Slot.Name;Slot.Rack=Members[0];Slot.Pose=Slot.Pose*Anchor;
  for (TActorIterator<AWarehouseInboundZone> It(GetWorld());It;++It) It->RackSlots.Add(Slot);
  Slot.Pose=Slot.Pose.GetRelativeTransform(Members[0]->GetActorTransform());RackCatalogue.Add(Slot);
 }
 if (!P.ZoneHalfSize.IsNearlyZero())
 {
  auto* Layout=AWarehouseLayoutEditor::Find(this);FWarehouseLayoutZone Zone;Zone.Name=P.bOutbound ? TEXT("Outbound") : TEXT("Inbound");
  Zone.Centre=Anchor.GetLocation();Zone.HalfSize=P.ZoneHalfSize;for (AActor* A:Members) Zone.Members.Add(A);
  for (auto Pose:P.ZoneDestinations) Zone.Destinations.Add(Pose*Anchor);Layout->Zones.Add(Zone);
  for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It) It->RefreshLayoutDestinations();
  for (AActor* A:Members) if (auto* Inbound=Cast<AWarehouseInboundZone>(A))
  {
   Inbound->Fleet.Reset();for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) Inbound->Fleet.Add(*It);
   Inbound->RackSlots.Reset();for (auto Slot:RackCatalogue) if (IsValid(Slot.Rack))
   { Slot.Pose=Slot.Pose*Slot.Rack->GetActorTransform();Inbound->RackSlots.Add(Slot); }
  }
 }
 for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It) It->RefreshLayoutDestinations();
 PreviewPrototypes.Remove(Members[0]);++Revision;
}
void AWarehouseLayoutInventory::ForgetActors(const TArray<AActor*>& Members)
{
 RackCatalogue.RemoveAll([&](const auto& Slot) { return !IsValid(Slot.Rack) || Members.Contains(Slot.Rack); });
 if (auto* Strength=AWarehouseDamageSystem::Find(this)) Strength->RemoveLayoutActors(Members);
 for (TActorIterator<AWarehouseInboundZone> It(GetWorld());It;++It)
 { It->Fleet.RemoveAll([&](AWarehouseForklift* V) { return Members.Contains(V); });It->RackSlots.RemoveAll([&](const auto& Slot) { return Members.Contains(Slot.Rack); }); }
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
 {
  if (Members.Contains(It->ChargingStation)) It->ChargingStation=nullptr;
  if (Members.Contains(It->Elevator)) It->Elevator=nullptr;
  if (Members.Contains(It->WaitingBay)) It->WaitingBay=nullptr;
 }
 if (auto* Layout=AWarehouseLayoutEditor::Find(this))
  for (int32 I=Layout->Zones.Num()-1;I>=0;--I)
  {
   auto& Zone=Layout->Zones[I];Zone.Members.RemoveAll([&](AActor* A) { return Members.Contains(A); });
   if (!Zone.Members.IsEmpty()) continue;
   Layout->Zones.RemoveAt(I);
  }
 for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It) It->RefreshLayoutDestinations();
 ++Revision;
}
