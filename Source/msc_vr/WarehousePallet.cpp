#include "WarehousePallet.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AWarehousePallet::AWarehousePallet()
{
 RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Wood(TEXT("/Game/Warehouse/Materials/MI_PalletWood"));
 auto Board = [&](FString Name, FVector Position, FVector Size)
 {
  auto* Part = CreateDefaultSubobject<UStaticMeshComponent>(FName(*Name));
  Part->SetupAttachment(RootComponent);
  Part->SetStaticMesh(Cube.Object);
  Part->SetMaterial(0,Wood.Object);
  Part->SetRelativeLocation(Position);
  Part->SetRelativeScale3D(Size / 100.f);
  Part->SetCollisionProfileName(TEXT("BlockAllDynamic"));
 };
 for (int I=0; I<5; ++I) Board(FString::Printf(TEXT("Deck%d"), I), FVector(-48+24*I,0,13.5), FVector(20,100,3));
 for (int I=0; I<3; ++I)
 {
  Board(FString::Printf(TEXT("Runner%d"), I), FVector(0,-44+44*I,7.5), FVector(120,12,9));
  Board(FString::Printf(TEXT("Foot%d"), I), FVector(0,-44+44*I,1.5), FVector(120,12,3));
 }
}

bool AWarehousePallet::CanEngage(const FTransform& Frame) const
{
 if (!Frame.GetScale3D().Equals(FVector::OneVector, .001f) || !GetActorScale3D().Equals(FVector::OneVector, .001f)) return false;
 if (FVector::DotProduct(Frame.GetUnitAxis(EAxis::X), GetActorForwardVector()) < FMath::Cos(FMath::DegreesToRadians(2.f)) ||
     FVector::DotProduct(Frame.GetUnitAxis(EAxis::Z), GetActorUpVector()) < FMath::Cos(FMath::DegreesToRadians(1.f))) return false;
 auto Local = [&](FVector P) { return GetActorTransform().InverseTransformPosition(Frame.TransformPosition(P)); };
 for (float Side : {-1.f, 1.f})
 {
  if (Local(FVector(50,Side*22,7.5)).X > -50 || Local(FVector(170,Side*22,7.5)).X < 40 || Local(FVector(170,Side*22,7.5)).X > 60) return false;
  for (float X : {50.f,170.f}) for (float Y : {-4.f,4.f}) for (float Z : {-2.f,2.f})
  {
   const FVector P = Local(FVector(X,Side*22+Y,7.5+Z));
   if (Side*P.Y < 6.25f || Side*P.Y > 37.75f || P.Z < 3.25f || P.Z > 11.75f) return false;
  }
 }
 return true;
}
