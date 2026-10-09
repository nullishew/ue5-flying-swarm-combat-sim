#pragma once

#include "CoreMinimal.h"
#include "BoidSteeringBehavior.h"
#include "BoidPathFollowingSteeringBehavior.generated.h"

/** Returns the direction to the location goal captured by the swarm runner. */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class FLYINGSWARMCOMBATSIM_API UBoidPathFollowingSteeringBehavior : public UBoidSteeringBehavior
{
	GENERATED_BODY()

protected:
	virtual FVector CalculateDirection(const FBoidSteeringInput& Input,
		FBoidSteeringRuntimeState& Runtime) const override;
};
