#include "WarehouseForklift.h"
#include "WarehousePhysics.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

void AWarehouseForklift::ConfigureWheelCollision(UStaticMesh* Mesh)
{
#if WITH_EDITOR
 if (!Mesh || !Mesh->GetBodySetup()) return;
 auto* Setup=Mesh->GetBodySetup();
 const FBox Box=Mesh->GetBoundingBox();
 const FVector Center=Box.GetCenter(),Half=Box.GetExtent();
 FKConvexElem Cylinder;
 // Coarse facets make a heavy tyre roll in 3 cm jumps at creep speed.
 // 128 sides reduce radial error to about 0.05 mm at this wheel's radius.
 constexpr int32 Sides=128;
 for (int32 Side : {-1,1}) for (int32 I=0; I<Sides; ++I)
 {
  const double Angle=I*2.*PI/Sides;
  Cylinder.VertexData.Add(Center+FVector(Half.X*FMath::Cos(Angle),Side*Half.Y,Half.Z*FMath::Sin(Angle)));
 }
 Cylinder.UpdateElemBox(); Setup->AggGeom.EmptyElements(); Setup->AggGeom.ConvexElems.Add(Cylinder);
 Setup->CollisionTraceFlag=CTF_UseSimpleAndComplex;
 Setup->InvalidatePhysicsData(); Setup->CreatePhysicsMeshes(); Mesh->MarkPackageDirty();
#endif
}

void AWarehouseForklift::InitializePhysicalRig()
{
 TArray<UStaticMeshComponent*> Parts; GetComponents(Parts);
 for (auto* Part : Parts)
 {
  if (Part->GetName()==TEXT("Body")) ChassisBody=Part;
  if (Part->GetName()==TEXT("Carriage")) CarriageBody=Part;
 }
 if (!ChassisBody || !CarriageBody || !LiftStage || Wheels.Num()!=3)
 { SetMechanicalFailure(); return; }
 USceneComponent* PreviousRoot=RootComponent;
 ChassisBody->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
 SetRootComponent(ChassisBody);
 PreviousRoot->AttachToComponent(ChassisBody,FAttachmentTransformRules::KeepWorldTransform);
 CarriageBody->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
 LiftStage->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
 for (UStaticMeshComponent* Wheel : Wheels) Wheel->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

 for (auto* Part : Parts)
 {
  if (!Part->IsCollisionEnabled()) continue;
  WarehousePhysics::ConfigureContact(Part,Part->GetName().Contains(TEXT("Wheel")) ? EWarehouseSurface::Rubber : EWarehouseSurface::Steel);
  float Mass=0;
  const FString Name=Part->GetName();
  if (Part==ChassisBody) Mass=790;
  else if (Part==CarriageBody || Part==LiftStage) Mass=30;
  else if (Name==TEXT("DriveSteer")) Mass=40;
  else if (Name==TEXT("ForkL") || Name==TEXT("ForkR")) Mass=25;
  else if (Name==TEXT("LiftRam") || Name==TEXT("LiftPulley")) Mass=10;
  else if (Name==TEXT("DriveWheel")) Mass=20;
  else if (Name.Contains(TEXT("LoadWheel"))) Mass=10;
  if (Mass>0) Part->SetMassOverrideInKg(NAME_None,Mass*FMath::Max(1.f,VehicleMassKg)/1000.f);
 }
 ChassisBody->SetCollisionObjectType(ECC_Vehicle);
 ChassisBody->SetSimulatePhysics(true);
 CarriageBody->SetSimulatePhysics(true); LiftStage->SetSimulatePhysics(true);
 for (UStaticMeshComponent* Wheel : Wheels) Wheel->SetSimulatePhysics(true);

 // Fixed equipment is welded, while lift and axle joints remain separate bodies.
 for (auto* Part : Parts)
 {
  if (Part==ChassisBody || Part==CarriageBody || Part==LiftStage || Wheels.Contains(Part) || !Part->IsCollisionEnabled()) continue;
  UStaticMeshComponent* Parent=ChassisBody;
  if (Part->GetName().StartsWith(TEXT("Fork"))) Parent=CarriageBody;
  else if (Part->GetName()==TEXT("LiftRam") || Part->GetName()==TEXT("LiftPulley")) Parent=LiftStage;
  Part->AttachToComponent(Parent,FAttachmentTransformRules(EAttachmentRule::KeepWorld,true));
 }
 Carriage->AttachToComponent(CarriageBody,FAttachmentTransformRules::KeepWorldTransform);
 // One mass ledger: 830+80+50+20+10+10=1000kg, no duplicate payload mass.
 const float Scale=FMath::Max(1.f,VehicleMassKg)/1000.f;
 // Chaos combines each welded member's mass; overrides remain per-member, not per-cluster.
 ChassisBody->SetMassOverrideInKg(NAME_None,790.f*Scale);
 CarriageBody->SetMassOverrideInKg(NAME_None,30.f*Scale);
 LiftStage->SetMassOverrideInKg(NAME_None,30.f*Scale);
 for (int32 I=0;I<Wheels.Num();++I) Wheels[I]->SetMassOverrideInKg(NAME_None,(I==0 ? 20.f : 10.f)*Scale);
 // Battery/counterweight distribution is a calibration assumption, exposed by the physics documentation.
 const FVector CurrentCOM=GetActorTransform().InverseTransformPosition(ChassisBody->GetCenterOfMass());
 ChassisBody->SetCenterOfMass((FVector(-45,0,38)-CurrentCOM)*(830.f/790.f));

 auto Joint=[&](const TCHAR* Name,UStaticMeshComponent* Moving,const FTransform& WorldFrame)
 {
  auto* Constraint=NewObject<UPhysicsConstraintComponent>(this,Name);
  AddInstanceComponent(Constraint); Constraint->RegisterComponent();
  Constraint->SetWorldTransform(WorldFrame);
  Constraint->SetDisableCollision(true); // Only the connected internal mounting pair.
  Constraint->SetProjectionEnabled(false);
  Constraint->ConstraintInstance.DisableMassConditioning();
  Constraint->ConstraintInstance.DisableParentDominates();
  Constraint->ConstraintInstance.SetShockPropagationParams(false,0);
  Constraint->SetLinearXLimit(LCM_Locked,0); Constraint->SetLinearYLimit(LCM_Locked,0); Constraint->SetLinearZLimit(LCM_Locked,0);
  Constraint->SetAngularTwistLimit(ACM_Locked,0); Constraint->SetAngularSwing1Limit(ACM_Locked,0); Constraint->SetAngularSwing2Limit(ACM_Locked,0);
  Constraint->SetLinearBreakable(true,25000000.f); Constraint->SetAngularBreakable(true,100000000.f);
  Constraint->SetConstrainedComponents(ChassisBody,NAME_None,Moving,NAME_None);
  return Constraint;
 };
 const float Range=FMath::Max(0.f,FMath::Min(160.f,MaxForkHeightCm)-9.5f);
 CarriageJoint=Joint(TEXT("CarriageHydraulicJoint"),CarriageBody,Carriage->GetComponentTransform());
 CarriageJoint->SetLinearZLimit(LCM_Limited,Range*.5f);
 CarriageJoint->SetConstraintReferencePosition(EConstraintFrame::Frame1,FVector(0,0,Range*.5f));
 CarriageJoint->SetConstraintReferencePosition(EConstraintFrame::Frame2,FVector(10,0,0));
 CarriageJoint->SetLinearPositionDrive(false,false,true);
 CarriageJoint->SetLinearVelocityDrive(false,false,true);
 CarriageJoint->SetLinearDriveAccelerationMode(false);
 CarriageJoint->SetLinearDriveParams(220000.f,22000.f,1600000.f); // 16kN hydraulic force, finite.
 CarriageJoint->SetLinearPositionTarget(FVector(0,0,Range*.5f));
 StageJoint=Joint(TEXT("InnerMastHydraulicJoint"),LiftStage,GetActorTransform());
 StageJoint->SetLinearZLimit(LCM_Limited,Range*.25f);
 StageJoint->SetConstraintReferencePosition(EConstraintFrame::Frame1,FVector(0,0,Range*.25f));
 StageJoint->SetConstraintReferencePosition(EConstraintFrame::Frame2,FVector::ZeroVector);
 StageJoint->SetLinearPositionDrive(false,false,true); StageJoint->SetLinearVelocityDrive(false,false,true);
 StageJoint->SetLinearDriveAccelerationMode(false); StageJoint->SetLinearDriveParams(120000.f,12000.f,800000.f);
 StageJoint->SetLinearPositionTarget(FVector(0,0,Range*.25f));
 // The nested guides overlap by design. Their mount permits travel but excludes internal self-contact.
 auto* InternalGuide=NewObject<UPhysicsConstraintComponent>(this,TEXT("MastCarriageMount"));
 AddInstanceComponent(InternalGuide); InternalGuide->RegisterComponent();
 InternalGuide->SetLinearXLimit(LCM_Free,0); InternalGuide->SetLinearYLimit(LCM_Free,0); InternalGuide->SetLinearZLimit(LCM_Free,0);
 InternalGuide->SetAngularTwistLimit(ACM_Free,0); InternalGuide->SetAngularSwing1Limit(ACM_Free,0); InternalGuide->SetAngularSwing2Limit(ACM_Free,0);
 InternalGuide->SetDisableCollision(true); InternalGuide->SetProjectionEnabled(false);
 InternalGuide->SetConstrainedComponents(LiftStage,NAME_None,CarriageBody,NAME_None);

 for (int32 I=0;I<Wheels.Num();++I)
 {
  const FQuat Axle=FRotationMatrix::MakeFromXZ(GetActorRightVector(),GetActorUpVector()).ToQuat();
  auto* Constraint=Joint(*FString::Printf(TEXT("PhysicalAxle_%d"),I),Wheels[I],FTransform(Axle,Wheels[I]->GetComponentLocation()));
  Constraint->SetAngularTwistLimit(ACM_Free,0);
  if (I==0)
  {
   Constraint->SetAngularSwing1Limit(ACM_Limited,85);
   Constraint->SetAngularDriveMode(EAngularDriveMode::TwistAndSwing);
   Constraint->SetOrientationDriveTwistAndSwing(false,true);
   Constraint->SetAngularDriveAccelerationMode(false);
   Constraint->SetAngularDriveParams(15000000.f,1500000.f,2500000.f); // steering actuator, 250Nm
  }
  WheelJoints.Add(Constraint);
 }
 DesiredLift=LiftMotorTarget=LiftOffset;
 bPhysicsReady=true;
 UE_LOG(LogTemp,Log,TEXT("PHYSICAL_AGV_READY %s mass=%.2f kg"),*GetName(),GetPhysicalMassKg());
}

float AWarehouseForklift::GetPhysicalMassKg() const
{
 if (!bPhysicsReady) return VehicleMassKg;
 float Mass=ChassisBody->GetMass()+CarriageBody->GetMass()+LiftStage->GetMass();
 for (UStaticMeshComponent* Wheel : Wheels) Mass+=Wheel->GetMass();
 return Mass;
}
void AWarehouseForklift::BrakeDrive() { DesiredDriveSpeed=0; DesiredCurvature=0; }

bool AWarehouseForklift::CommandDrive(float Speed,float Curvature,float Dt)
{
 if (!bPhysicsReady || !FMath::IsFinite(Speed) || !FMath::IsFinite(Curvature)) { BrakeDrive(); return false; }
 // Routes specify floor-plane travel. Chassis suspension/tyre pitch must not
 // turn a normal rolling contact into a predicted downward floor collision.
 const FVector Preview=GetActorForwardVector().GetSafeNormal2D()*Speed*FMath::Max(.008f,Dt);
 if (!ClearToMove(Preview,false)) { BrakeDrive(); return false; }
 DesiredDriveSpeed=Speed;
 DesiredCurvature=FMath::Clamp(Curvature,-1.f/FMath::Max(117.3f,MinimumTurningRadiusCm),1.f/FMath::Max(117.3f,MinimumTurningRadiusCm));
 return true;
}

void AWarehouseForklift::UpdatePhysicalRig(float Dt)
{
 if (!bPhysicsReady || Dt<=0) return;
 CurrentSpeedCm=FVector::DotProduct(ChassisBody->GetPhysicsLinearVelocity(),GetActorForwardVector());
 LiftOffset=GetActorTransform().InverseTransformPosition(Carriage->GetComponentLocation()).Z;
 if (GetActorUpVector().Z<.75f || CarriageJoint->IsBroken() || StageJoint->IsBroken() || WheelJoints.ContainsByPredicate([](UPhysicsConstraintComponent* Joint) { return Joint->IsBroken(); }))
 {
  if (!bMechanicalFailure) SetMechanicalFailure();
 }
 if (!bPowered || bMechanicalFailure || !bEStopReleased || !bBrakeHealthy) BrakeDrive();
 DriveSpeedTarget=FMath::FInterpConstantTo(DriveSpeedTarget,DesiredDriveSpeed,Dt,50.f);
 const float Wheelbase=FMath::Abs((-74.f+7.f)*(215.f/282.55f));
 const float RequestedSteering=-FMath::Atan(Wheelbase*DesiredCurvature);
 if (bSteeringHealthy) SteeringAngle=FMath::FInterpConstantTo(SteeringAngle,RequestedSteering,Dt,FMath::DegreesToRadians(60.f));
 WheelJoints[0]->SetAngularOrientationTarget(FRotator(0,-FMath::RadiansToDegrees(SteeringAngle),0));
 auto* Drive=Wheels[0].Get();
 const FVector Axle=Drive->GetRightVector();
 const float Spin=FVector::DotProduct(Drive->GetPhysicsAngularVelocityInRadians()-ChassisBody->GetPhysicsAngularVelocityInRadians(),Axle);
 const float Radius=FMath::Max(1.f,float(Drive->GetStaticMesh()->GetBoundingBox().GetExtent().Z));
 const bool Braking=FMath::Abs(DesiredDriveSpeed)<.01f;
 const float DesiredSpin=Braking ? 0.f : DriveSpeedTarget/(Radius*FMath::Max(.2f,FMath::Cos(SteeringAngle)));
 FHitResult Ground;
 FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(DriveTyreContact),false,this);
 const bool Grounded=GetWorld()->LineTraceSingleByChannel(Ground,Drive->GetComponentLocation(),Drive->GetComponentLocation()-FVector(0,0,Radius+2.f),ECC_Visibility,GroundQuery);
 const float Error=DesiredSpin-Spin;
 const float Limit=Braking ? 532.f : FMath::Min(266.f,2200.f/FMath::Max(1.f,FMath::Abs(Spin)));
 if (Braking || !Grounded) DriveSpinIntegral=0;
 else if (FMath::Abs(Error*35.f+DriveSpinIntegral*25.f)<Limit)
  DriveSpinIntegral=FMath::Clamp(DriveSpinIntegral+Error*Dt,-10.64f,10.64f);
 const float WheelInertia=FMath::Max(.001f,float(Drive->BodyInstance.GetBodyInertiaTensor().Y/10000.));
 const float Gain=Grounded ? 35.f : FMath::Min(35.f,.8f*WheelInertia/FMath::Max(.008f,Dt));
 float TorqueNm=FMath::Clamp(Error*Gain+DriveSpinIntegral*25.f,-Limit,Limit);
 if (Braking && !bBrakeHealthy) TorqueNm=0; // A failed brake must physically coast.
 Drive->AddTorqueInRadians(Axle*TorqueNm*10000.f);
 ChassisBody->AddTorqueInRadians(-Axle*TorqueNm*10000.f);
 // Bearing resistance dissipates wheel motion and applies the opposite reaction to the chassis.
 for (UStaticMeshComponent* Wheel : Wheels)
 {
  const FVector Axis=Wheel->GetRightVector();
  const float W=FVector::DotProduct(Wheel->GetPhysicsAngularVelocityInRadians()-ChassisBody->GetPhysicsAngularVelocityInRadians(),Axis);
  const FVector Resistance=Axis*(-FMath::Clamp(W*1.2f,-8.f,8.f)*10000.f);
  Wheel->AddTorqueInRadians(Resistance); ChassisBody->AddTorqueInRadians(-Resistance);
 }
 if (bPowered && !bMechanicalFailure)
 {
  LiftMotorTarget=FMath::FInterpConstantTo(LiftMotorTarget,DesiredLift,Dt,DesiredLift>LiftMotorTarget ? 11.5f : 16.f);
  const float Range=FMath::Max(0.f,FMath::Min(160.f,MaxForkHeightCm)-9.5f);
  const float LiftWeight=(CarriageBody->GetMass()+(bSupportingPallet ? GetLoadMassKg() : 0.f))*980.f*FMath::Max(0.f,GetActorUpVector().Z);
  // Chaos stores the relative connector target as Frame1 minus Frame2.
  CarriageJoint->SetLinearPositionTarget(FVector(0,0,Range*.5f-LiftMotorTarget-LiftWeight/220000.f));
  StageJoint->SetLinearPositionTarget(FVector(0,0,Range*.25f-LiftMotorTarget*.5f-LiftStage->GetMass()*980.f/120000.f));
  // Position drives must also respond after an idle rig has gone to sleep.
  if (FMath::Abs(LiftOffset-LiftMotorTarget)>.05f) CarriageBody->WakeAllRigidBodies();
  const float StageHeight=GetActorTransform().InverseTransformPosition(LiftStage->GetComponentLocation()).Z;
  if (FMath::Abs(StageHeight-LiftMotorTarget*.5f)>.05f) LiftStage->WakeAllRigidBodies();
 }
 if (LiftChains && LiftChains->GetStaticMesh())
 {
  constexpr float Base=3.5f+15.1f*(215.f/282.55f);
  const float Length=LiftChains->GetStaticMesh()->GetBoundingBox().GetSize().Z;
  const float Stage=GetActorTransform().InverseTransformPosition(LiftStage->GetComponentLocation()).Z;
  LiftChains->SetRelativeLocation(FVector(0,0,Base+LiftOffset));
  LiftChains->SetRelativeScale3D(FVector(1,1,FMath::Max(.01f,(Length+Stage-LiftOffset)/Length)));
 }
 if (bPowered && !bMechanicalFailure)
 {
  if (!Braking) ConsumeEnergy(FMath::Max(0.f,TorqueNm*Spin)*Dt/(.75f*3600.f));
  const FVector Point=Carriage->GetComponentLocation();
  const float RelativeLiftSpeed=FVector::DotProduct(CarriageBody->GetPhysicsLinearVelocityAtPoint(Point)-ChassisBody->GetPhysicsLinearVelocityAtPoint(Point),GetActorUpVector());
  // Energy follows measured motion, not the distance to a repeatedly requested target.
  const float LiftPower=(CarriageBody->GetMass()+.5f*LiftStage->GetMass()+(bSupportingPallet ? GetLoadMassKg() : 0.f))*9.81f*FMath::Max(0.f,RelativeLiftSpeed)*.01f;
  ConsumeEnergy(LiftPower*Dt/(.75f*3600.f));
 }
}
