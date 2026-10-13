#pragma once

#include "CoreMinimal.h"
#include "Swarm/Steering/BoidSteeringBehavior.h"
#include "BoidPathFollowingSteeringBehavior.generated.h"

/** Returns the direction to the location goal captured by the swarm runner. */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class FLYINGSWARMCOMBATSIM_API UBoidPathFollowingSteeringBehavior : public UBoidSteeringBehavior
{
	GENERATED_BODY()

public:
	/** Legacy opt-in for continuous goals when no navigation phase is supplied. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Path Following")
	bool bFollowContinuousGoal = false;
	virtual bool SupportsLocationGoals(bool bContinuous) const override { return true; }
	virtual bool SupportsNavigationPhase(EBoidNavigationPhase Phase) const override
	{ return Phase != EBoidNavigationPhase::Orbit || bFollowContinuousGoal; }
	virtual bool IsPrimaryMovementForPhase(EBoidNavigationPhase Phase) const override
	{ return Phase == EBoidNavigationPhase::Approach || Phase == EBoidNavigationPhase::Transit || Phase == EBoidNavigationPhase::Retreat; }

protected:
	virtual FVector CalculateDirection(const FBoidSteeringInput& Input,
		FBoidSteeringRuntimeState& Runtime) const override;
	virtual bool CalculateDesiredSpeed(const FBoidSteeringInput& Input, float& OutSpeedCmPerSec) const override;
};
