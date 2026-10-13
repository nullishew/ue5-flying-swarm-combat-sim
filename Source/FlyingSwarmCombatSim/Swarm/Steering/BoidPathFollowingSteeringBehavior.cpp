#include "Swarm/Steering/BoidPathFollowingSteeringBehavior.h"

FVector UBoidPathFollowingSteeringBehavior::CalculateDirection(const FBoidSteeringInput& Input,
	FBoidSteeringRuntimeState& Runtime) const
{
	return Input.bHasGoal && (Input.NavigationPhase == EBoidNavigationPhase::Transit
		|| Input.NavigationPhase == EBoidNavigationPhase::Approach || Input.NavigationPhase == EBoidNavigationPhase::Retreat || !Input.bContinuousGoal || bFollowContinuousGoal)
		? (Input.Goal - Input.Position).GetSafeNormal() : FVector::ZeroVector;
}

bool UBoidPathFollowingSteeringBehavior::CalculateDesiredSpeed(const FBoidSteeringInput& Input,
	float& OutSpeedCmPerSec) const
{
	const bool bPhaseOwnedGoal = Input.NavigationPhase == EBoidNavigationPhase::Transit
		|| Input.NavigationPhase == EBoidNavigationPhase::Approach || Input.NavigationPhase == EBoidNavigationPhase::Retreat;
	if ((!bPhaseOwnedGoal && !bFollowContinuousGoal) || !Input.bHasDesiredSpeed) return false;
	OutSpeedCmPerSec = Input.DesiredSpeedCmPerSec;
	return true;
}
