#include "Swarm/Flight/BoidFlightMovementComponent.h"

#include "Swarm/Flight/BoidFlightAlgorithms.h"
#include "Components/SceneComponent.h"

UBoidFlightMovementComponent::UBoidFlightMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UBoidFlightMovementComponent::ConfigureBounds(const UBoxComponent* Bounds, float Radius, float Margin)
{
	bBoundsValid = BoidFlight::SafeBounds(Bounds, Radius, Margin, BoundsTransform, SafeExtent);
}

bool UBoidFlightMovementComponent::MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& NewRotation,
	bool bSweep, FHitResult* OutHit, ETeleportType Teleport)
{
	if (!UpdatedComponent || !bBoundsValid)
	{
		if (OutHit)
		{
			OutHit->Reset(1.0f);
		}
		return false;
	}
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector End = BoidFlight::ClampPosition(Start + Delta, BoundsTransform, SafeExtent);
	return Super::MoveUpdatedComponentImpl(End - Start, NewRotation, bSweep, OutHit, Teleport);
}
