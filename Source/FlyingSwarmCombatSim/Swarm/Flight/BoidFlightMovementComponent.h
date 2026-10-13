#pragma once

#include "CoreMinimal.h"
#include "GameFramework/MovementComponent.h"
#include "BoidFlightMovementComponent.generated.h"

class UBoxComponent;

/** Runner-driven collision movement; no independent simulation tick. */
UCLASS()
class FLYINGSWARMCOMBATSIM_API UBoidFlightMovementComponent : public UMovementComponent
{
	GENERATED_BODY()

public:
	UBoidFlightMovementComponent();
	void ConfigureBounds(const UBoxComponent* Bounds, float Radius, float Margin);
	virtual bool MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& NewRotation,
		bool bSweep, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) override;

private:
	FTransform BoundsTransform;
	FVector SafeExtent = FVector::ZeroVector;
	bool bBoundsValid = false;
};
