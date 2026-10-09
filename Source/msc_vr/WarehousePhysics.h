#pragma once
#include "CoreMinimal.h"

class UPrimitiveComponent;

/** Contact assumptions for training. These are not measured factory material values. */
enum class EWarehouseSurface : uint8 { Cardboard, Wood, Steel, Rubber, Concrete };

namespace WarehousePhysics
{
 MSC_VR_API bool IsPhysicalContact(const UPrimitiveComponent* Component);
 MSC_VR_API EWarehouseSurface SurfaceFor(const UPrimitiveComponent* Component);
 // A negative mass preserves the owner's mass. Never changes simulation, pose or velocity.
 MSC_VR_API void ConfigureContact(UPrimitiveComponent* Component, EWarehouseSurface Surface = EWarehouseSurface::Steel, float MassKg = -1.f);
}
