#include "BoidPathFollowingSteeringBehavior.h"

FVector UBoidPathFollowingSteeringBehavior::CalculateDirection(const FBoidSteeringInput& Input,
	FBoidSteeringRuntimeState& Runtime) const
{
	return Input.bHasGoal ? (Input.Goal - Input.Position).GetSafeNormal() : FVector::ZeroVector;
}
