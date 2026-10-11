#include "WarehouseLayoutSnap.h"
#include "WarehousePhysics.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
 constexpr double Reach=20.,Gap=.1;
 struct FTarget { FBox Bounds{ForceInit};FString Key;bool Wall=false; };
 FName GroupOf(const AActor* Actor)
 {
  for (FName Tag:Actor->Tags) if (Tag.ToString().StartsWith(TEXT("LayoutGroup:"))) return Tag;
  return Actor->GetFName();
 }
 FBox PhysicalBounds(const AActor* Actor)
 {
  FBox Result(ForceInit);
  if (!Actor->GetActorEnableCollision()) return Result;
  TArray<UPrimitiveComponent*> Parts;Actor->GetComponents(Parts);
  for (const auto* Part:Parts) if (WarehousePhysics::IsPhysicalContact(Part)) Result+=Part->Bounds.GetBox();
  return Result;
 }
 bool SameFloor(const AActor* Actor,int32 Floor,const FBox& Bounds)
 {
  const FName Expected(*FString::Printf(TEXT("WarehouseFloor%d"),Floor));
  for (int32 I=0;I<3;++I) if (Actor->ActorHasTag(FName(*FString::Printf(TEXT("WarehouseFloor%d"),I))))
   return Actor->ActorHasTag(Expected);
  return FMath::FloorToInt((FMath::Max(0.,Bounds.Min.Z)+1.)/400.)==Floor;
 }
 bool Wall(const AActor* Actor,const FBox& Bounds,int32 Floor)
 {
  if (!Actor->ActorHasTag(TEXT("LayoutFixed"))) return false;
  const FVector Size=Bounds.GetSize();const FRotator Rotation=Actor->GetActorRotation();
  const double Cardinal=FMath::RoundToDouble(Rotation.Yaw/90.)*90.;
  return FMath::Abs(Rotation.Pitch)<.01 && FMath::Abs(Rotation.Roll)<.01 && FMath::Abs(Rotation.Yaw-Cardinal)<.01 &&
   FMath::Min(Size.X,Size.Y)<=50 && FMath::Max(Size.X,Size.Y)>=400 && Size.Z>=150 &&
   Bounds.Min.Z<=Floor*400.+40 && Bounds.Max.Z>=Floor*400.+150;
 }
 bool Viable(const FBox& Box,const TArray<FTarget>& Targets)
 {
  for (const auto& Target:Targets)
  {
   const FBox& Other=Target.Bounds;
   const double X=FMath::Min(Box.Max.X,Other.Max.X)-FMath::Max(Box.Min.X,Other.Min.X);
   const double Y=FMath::Min(Box.Max.Y,Other.Max.Y)-FMath::Max(Box.Min.Y,Other.Min.Y);
   const double Z=FMath::Min(Box.Max.Z,Other.Max.Z)-FMath::Max(Box.Min.Z,Other.Min.Z);
   if (X>.02 && Y>.02 && Z>1.) return false;
  }
  return true;
 }
}

FVector WarehouseLayoutSnap::Compute(const AActor* Context,const FBox& Original,FVector Raw,
 const TArray<AActor*>& Moving,int32 Floor,FString& OutHint)
{
 OutHint.Reset();
 if (Raw.ContainsNaN()) return FVector::ZeroVector;
 FVector Base(FMath::GridSnap(Raw.X,10.),FMath::GridSnap(Raw.Y,10.),0);
 UWorld* World=IsValid(Context) ? Context->GetWorld() : nullptr;
 if (!World || !Original.IsValid || Original.Min.ContainsNaN() || Original.Max.ContainsNaN() || Floor<0 || Floor>2) return Base;
 const FVector Size=Original.GetSize();
 if (Size.X<20 || Size.Y<20 || Size.Z<60) return Base;
 TSet<const AActor*> Ignored;for (AActor* Actor:Moving) Ignored.Add(Actor);
 TMap<FName,FTarget> Groups;
 for (TActorIterator<AActor> It(World);It;++It)
 {
  if (*It==Context || Ignored.Contains(*It)) continue;
  const FBox Bounds=PhysicalBounds(*It);if (!Bounds.IsValid) continue;
  const bool Rack=It->ActorHasTag(TEXT("LayoutType:Rack")) && SameFloor(*It,Floor,Bounds);
  const bool IsWall=Wall(*It,Bounds,Floor);
  if (!Rack && !IsWall) continue;
  const FName Key=Rack ? GroupOf(*It) : It->GetFName();
  auto& Target=Groups.FindOrAdd(Key);Target.Bounds+=Bounds;Target.Key=Key.ToString();Target.Wall=IsWall;
 }
 TArray<FTarget> Targets;
 for (const auto& Pair:Groups)
 {
  const FTarget& Target=Pair.Value;const FVector Extent=Target.Bounds.GetSize();
  if (!Target.Wall && (FMath::Min(Extent.X,Extent.Y)<20 || FMath::Max(Extent.X,Extent.Y)<60 || Extent.Z<80)) continue;
  Targets.Add(Target);
 }
 const FBox Current=Original.ShiftBy(Base);
 FVector Best=Base;double BestCost=TNumericLimits<double>::Max();FString BestKey;
 auto Offer=[&](FVector Candidate,const FTarget& Target,bool Edge)
 {
  if (!Viable(Original.ShiftBy(Candidate),Targets)) return false;
  const double Cost=(Candidate-Base).SizeSquared2D();
  if (Cost<BestCost-.0001 || (FMath::Abs(Cost-BestCost)<.0001 && (BestKey.IsEmpty() || Target.Key<BestKey)))
  {
   Best=Candidate;BestCost=Cost;BestKey=Target.Key;
   OutHint=Target.Wall ? TEXT("벽 면에 맞춤") : TEXT("랙 면에 맞춤");
   if (Edge) OutHint+=TEXT(" · 가장자리 정렬");
  }
  return true;
 };
 for (const FTarget& Target:Targets)
  for (int32 Axis=0;Axis<2;++Axis)
  {
   const int32 Across=1-Axis;
   const double Overlap=FMath::Min(Current.Max[Across],Target.Bounds.Max[Across])-FMath::Max(Current.Min[Across],Target.Bounds.Min[Across]);
   if (Overlap<=10) continue;
   for (double Correction:{Target.Bounds.Min[Axis]-Current.Max[Axis]-Gap,Target.Bounds.Max[Axis]-Current.Min[Axis]+Gap})
   {
    if (FMath::Abs(Correction)>Reach) continue;
    FVector Face=Base;Face[Axis]+=Correction;
    const double NearMin=Target.Bounds.Min[Across]-Current.Min[Across];
    const double NearMax=Target.Bounds.Max[Across]-Current.Max[Across];
    const double First=FMath::Abs(NearMin)<=FMath::Abs(NearMax) ? NearMin : NearMax;
    const double Second=First==NearMin ? NearMax : NearMin;
    bool Aligned=false;
    for (double Offset:{First,Second}) if (FMath::Abs(Offset)<=Reach)
    {
     FVector Edge=Face;Edge[Across]+=Offset;
     if (Offer(Edge,Target,true)) { Aligned=true;break; }
    }
    if (!Aligned) Offer(Face,Target,false);
   }
  }
 return Best;
}
