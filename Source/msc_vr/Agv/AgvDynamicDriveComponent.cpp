#include "AgvDynamicDriveComponent.h"
#include "AgvPath.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

namespace
{
 const double StandardGravity=9.81;
 const double LoadTransferLag=.04;

 double EffectiveMass(UPrimitiveComponent* Body,const FVector& Point,const FVector& Direction)
 {
  const FVector Lever=Body->GetComponentTransform().InverseTransformVectorNoScale(
   FVector::CrossProduct(Point-Body->GetCenterOfMass(),Direction));
  const FVector Inertia=Body->BodyInstance.GetBodyInertiaTensor(); // kg*cm^2
  const double Inv=1./FMath::Max(.1f,Body->GetMass())+
   FMath::Square(Lever.X)/FMath::Max(1.,Inertia.X)+
   FMath::Square(Lever.Y)/FMath::Max(1.,Inertia.Y)+
   FMath::Square(Lever.Z)/FMath::Max(1.,Inertia.Z);
  return 1./FMath::Max(1.e-8,Inv);
 }
}

UAgvDynamicDriveComponent::UAgvDynamicDriveComponent()
{
 FAgvPassiveWheel Wheel;
 Wheel.PositionCm=FVector2D(27,45); PassiveWheels.Add(Wheel);
 Wheel.PositionCm=FVector2D(27,-45); PassiveWheels.Add(Wheel);
}

void UAgvDynamicDriveComponent::SetPhysicsBody(UPrimitiveComponent* InBody)
{
 PhysicsBody=InBody;
 SpeedIntegral=0;
 bHaveVelocitySample=false;
 ReadPhysicsTelemetry();
}

UAgvDynamicDriveComponent::FMassProperties UAgvDynamicDriveComponent::MassProperties() const
{
 FMassProperties Body;
 const double ActualMass=IsValid(PhysicsBody) ? FMath::Max(.1f,PhysicsBody->GetMass()) : FMath::Max(1.f,ChassisMassKg);
 const FVector ActualCenter=IsValid(PhysicsBody) ? GetOwner()->GetActorTransform().InverseTransformPosition(PhysicsBody->GetCenterOfMass()) : ChassisCenterOfMassCm;
 Body.Mass=ActualMass+PayloadMassKg;
 // This estimates support/grip only. Independent payload bodies transmit their real weight through contacts.
 Body.CenterOfMass=(ActualCenter*ActualMass+PayloadCenterOfMassCm*PayloadMassKg)/(Body.Mass*100.);
 Body.YawInertia=IsValid(PhysicsBody) ? FMath::Max(.01,PhysicsBody->BodyInstance.GetBodyInertiaTensor().Z/10000.) : FMath::Max(.01f,ChassisYawInertiaKgM2);
 return Body;
}

void UAgvDynamicDriveComponent::SetPayload(float MassKg,FVector InCenterOfMassCm,FVector2D SizeCm)
{
 if (!FMath::IsFinite(MassKg) || InCenterOfMassCm.ContainsNaN() || SizeCm.ContainsNaN()) return;
 PayloadMassKg=FMath::Max(0.f,MassKg);
 PayloadCenterOfMassCm=InCenterOfMassCm;
 PayloadYawInertiaKgM2=static_cast<float>(PayloadMassKg*(SizeCm/100.).SizeSquared()/12.);
 CenterOfMassCm=MassProperties().CenterOfMass*100.;
}

void UAgvDynamicDriveComponent::Halt()
{
 // Request the finite holding brake. Never erase the body's collision momentum or angular motion.
 CommandSpeedCmS=CommandYawRateDegS=0;
 SpeedIntegral=0;
 ReadPhysicsTelemetry();
}

void UAgvDynamicDriveComponent::ReadPhysicsTelemetry()
{
 if (!IsValid(PhysicsBody) || !PhysicsBody->IsSimulatingPhysics()) return;
 const FVector Reference=GetOwner()->GetActorTransform().TransformPosition(FVector(ReferenceOffsetCm,0,0));
 SpeedCmS=static_cast<float>(FVector::DotProduct(PhysicsBody->GetPhysicsLinearVelocityAtPoint(Reference),GetOwner()->GetActorForwardVector()));
 YawRateDegS=static_cast<float>(PhysicsBody->GetPhysicsAngularVelocityInDegrees().Z);
 CenterOfMassCm=MassProperties().CenterOfMass*100.;
 if (GetOwner()->GetActorUpVector().Z<.707f) bTipOver=true;
}

void UAgvDynamicDriveComponent::Step(float Dt)
{
 ReadPhysicsTelemetry();
 if (Dt<=0 || !FMath::IsFinite(Dt) || !IsValid(PhysicsBody) || !PhysicsBody->IsSimulatingPhysics()) return;
 const FVector ActualVelocity=PhysicsBody->GetPhysicsLinearVelocity();
 if (bHaveVelocitySample)
 {
  const FVector Acceleration=(ActualVelocity-LastPhysicsVelocity)/(100.*Dt);
  WorldAccelerationM+=(Acceleration-WorldAccelerationM)*FMath::Min(1.,double(Dt)/LoadTransferLag);
  const FVector LocalAcceleration=GetOwner()->GetActorTransform().InverseTransformVectorNoScale(Acceleration);
  BodyAcceleration+=(FVector2D(LocalAcceleration)-BodyAcceleration)*FMath::Min(1.,double(Dt)/LoadTransferLag);
 }
 LastPhysicsVelocity=ActualVelocity;
 bHaveVelocitySample=true;
 ApplyContactForces(Dt,MassProperties());
}

void UAgvDynamicDriveComponent::SolveWheelLoads(const FMassProperties& Body,const TArray<FVector2D>& Contacts,const TArray<double>& Stiffness,TArray<double>& OutLoads)
{
 const int32 Count=Contacts.Num();
 const double Weight=Body.Mass*StandardGravity;
 const double Height=Body.CenterOfMass.Z;
 const double Rhs[3]={Weight,-Body.Mass*BodyAcceleration.X*Height,-Body.Mass*BodyAcceleration.Y*Height};
 TArray<bool> Active; Active.Init(true,Count);
 OutLoads.Init(0.,Count);
 bool Stable=false;
 for (int32 Pass=0;Pass<Count;++Pass)
 {
  double Matrix[3][3]={};
  for (int32 I=0;I<Count;++I) if (Active[I])
  {
   const double Row[3]={1.,Contacts[I].X,Contacts[I].Y};
   for (int32 A=0;A<3;++A) for (int32 B=0;B<3;++B) Matrix[A][B]+=Stiffness[I]*Row[A]*Row[B];
  }
  double Right[3]={Rhs[0],Rhs[1],Rhs[2]},Plane[3];
  if (!AgvMath::Solve3(Matrix,Right,Plane)) break;
  int32 Lowest=INDEX_NONE;
  for (int32 I=0;I<Count;++I)
  {
   OutLoads[I]=Active[I] ? Stiffness[I]*(Plane[0]+Plane[1]*Contacts[I].X+Plane[2]*Contacts[I].Y) : 0;
   if (Active[I] && (Lowest==INDEX_NONE || OutLoads[I]<OutLoads[Lowest])) Lowest=I;
  }
  if (Lowest==INDEX_NONE) break;
  if (Pass==0) StabilityMargin=static_cast<float>(OutLoads[Lowest]/Weight);
  if (OutLoads[Lowest]>=0) { Stable=true; break; }
  Active[Lowest]=false;
 }
 if (!Stable)
 {
  bTipOver=true;
  for (double& Load : OutLoads) Load=FMath::Max(0.,Load);
 }
}

void UAgvDynamicDriveComponent::ApplyContactForces(float Dt,const FMassProperties& Body)
{
 AActor* Owner=GetOwner();
 const FTransform Pose=Owner->GetActorTransform();
 const FVector2D LocalCenter(Body.CenterOfMass);
 TArray<FVector2D> Contacts{FVector2D(DriveWheelOffsetCm/100.,0)-LocalCenter};
 TArray<double> Stiffness{double(DriveWheelStiffness)};
 for (const FAgvPassiveWheel& Wheel : PassiveWheels)
 {
  Contacts.Add(Wheel.PositionCm/100.-LocalCenter); Stiffness.Add(FMath::Max(.01f,Wheel.Stiffness));
 }
 TArray<double> Loads; SolveWheelLoads(Body,Contacts,Stiffness,Loads);

 double TargetSteer,TargetSpeedCm;
 WheelTarget(TargetSteer,TargetSpeedCm);
 if (!FMath::IsFinite(TargetSteer) || !FMath::IsFinite(TargetSpeedCm)) { TargetSteer=0; TargetSpeedCm=0; }
 StepSteering(TargetSteer,TargetSpeedCm,Dt);
 const bool Brake=SafetySpeedLimitCmS<=0 || PayloadMassKg>RatedPayloadKg ||
  (FMath::Abs(CommandSpeedCmS)<.01 && FMath::Abs(CommandYawRateDegS)<.01);
 if (Brake) TargetSpeedCm=0;
 MotorTorqueNm=0; DriveWheelLoadN=0; DriveWheelSlipCmS=0;
 for (FAgvPassiveWheel& Wheel : PassiveWheels) Wheel.LoadN=0;

 FCollisionQueryParams Query(SCENE_QUERY_STAT(AgvTyreGround),false,Owner);
 for (int32 I=0;I<Contacts.Num();++I)
 {
  const FVector2D LocalPoint=I==0 ? FVector2D(DriveWheelOffsetCm,0) : PassiveWheels[I-1].PositionCm;
  const double RadiusCm=I==0 ? DriveWheelRadiusCm : PassiveWheels[I-1].RadiusCm;
  const FVector Center=Pose.TransformPosition(FVector(LocalPoint,RadiusCm));
  FHitResult Hit;
  const bool Ground=GetWorld()->LineTraceSingleByChannel(Hit,Center+Owner->GetActorUpVector()*3,
   Center-Owner->GetActorUpVector()*(RadiusCm+.75),ECC_Visibility,Query) && Hit.ImpactNormal.Z>.2f &&
   FVector::Dist(Center,Hit.ImpactPoint)<=RadiusCm+.5;
  if (!Ground) continue; // No tyre motor/grip in mid-air.
  UPrimitiveComponent* Support=Hit.GetComponent();
  const FVector SupportVelocity=Support ? Support->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint,Hit.BoneName) : FVector::ZeroVector;
  const FVector Relative=PhysicsBody->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint)-SupportVelocity;
  FVector Rolling=Pose.TransformVectorNoScale(FVector(FMath::Cos(I==0 ? FMath::DegreesToRadians(SteerAngleDeg) : 0.),
   FMath::Sin(I==0 ? FMath::DegreesToRadians(SteerAngleDeg) : 0.),0));
  Rolling=FVector::VectorPlaneProject(Rolling,Hit.ImpactNormal).GetSafeNormal();
  const bool Caster=I>0 && PassiveWheels[I-1].bCaster;
  if (Caster) Rolling=FVector::VectorPlaneProject(Relative,Hit.ImpactNormal).GetSafeNormal();
  if (Rolling.IsNearlyZero()) continue;
  const FVector Side=FVector::CrossProduct(Hit.ImpactNormal,Rolling).GetSafeNormal();
  const double AlongCm=FVector::DotProduct(Relative,Rolling),SideCm=FVector::DotProduct(Relative,Side);
  const FVector GravityM(0,0,GetWorld()->GetGravityZ()/100.);
  // In common free fall the normal reaction is zero, even when a ray still touches a falling platform.
  const double ReactionScale=FMath::Clamp(FVector::DotProduct(WorldAccelerationM-GravityM,Hit.ImpactNormal)/StandardGravity,0.,4.);
  const double NormalN=FMath::Max(0.,Loads[I])*ReactionScale;
  const double GripN=FMath::Max(.01f,FrictionCoefficient)*NormalN;
  const double EffectiveAlong=EffectiveMass(PhysicsBody,Hit.ImpactPoint,Rolling);
  const double EffectiveSide=EffectiveMass(PhysicsBody,Hit.ImpactPoint,Side);
  double AlongN=0;
  if (I==0)
  {
   const double Radius=FMath::Max(.01,RadiusCm/100.);
   const double Error=(TargetSpeedCm-AlongCm)/100.;
   const double Integral=SpeedIntegral+Error*Dt;
   const double Request=Brake ? -AlongCm*.01*EffectiveAlong/FMath::Max(.05f,Dt) :
    ChassisMassKg*(SpeedLoopGain*Error+SpeedLoopIntegralGain*Integral);
   const double MotorLimit=(Brake ? SafetyBrakeTorqueNm : MaxDriveTorqueNm)/Radius;
   const double PowerLimit=Brake ? MotorLimit : MaxDrivePowerW/FMath::Max(.05,FMath::Abs(AlongCm)*.01);
   const double Limit=FMath::Min(MotorLimit,PowerLimit);
   AlongN=FMath::Clamp(Request,-Limit,Limit);
   if (!Brake && FMath::Abs(Request)<FMath::Min(Limit,GripN)) SpeedIntegral=Integral;
   if (Brake) SpeedIntegral=0;
   WheelSpeedCmS=static_cast<float>(AlongCm);
   DriveWheelTravelCm+=AlongCm*Dt;
   DriveWheelLoadN=static_cast<float>(NormalN);
   DriveWheelSlipCmS=static_cast<float>(TargetSpeedCm-AlongCm);
  }
  else
  {
   AlongN=-FMath::Sign(AlongCm)*FMath::Min(FMath::Max(0.f,RollingResistance)*NormalN,
    FMath::Abs(AlongCm)*.01*EffectiveAlong/FMath::Max(.05f,Dt));
   FAgvPassiveWheel& Wheel=PassiveWheels[I-1];
   Wheel.TravelCm+=AlongCm*Dt; Wheel.LoadN=static_cast<float>(NormalN);
   if (Caster) Wheel.SwivelDeg=static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Rolling,Owner->GetActorRightVector()),
    FVector::DotProduct(Rolling,Owner->GetActorForwardVector()))));
  }
  const double SideN=Caster ? 0 : -SideCm*.01*EffectiveSide/FMath::Max(.05f,Dt);
  FVector2D Tyre(AlongN,SideN);
  if (Tyre.Size()>GripN) Tyre*=GripN/FMath::Max(1.e-8,Tyre.Size());
  // UE force units are kg*cm/s^2; N -> UE force is x100. Chaos produces all translation/rotation.
  const FVector Force=(Rolling*Tyre.X+Side*Tyre.Y)*100.;
  PhysicsBody->AddForceAtLocation(Force,Hit.ImpactPoint);
  if (Support && Support->IsSimulatingPhysics(Hit.BoneName)) Support->AddForceAtLocation(-Force,Hit.ImpactPoint,Hit.BoneName);
  if (I==0) MotorTorqueNm=static_cast<float>(Tyre.X*RadiusCm/100.);
 }
}
