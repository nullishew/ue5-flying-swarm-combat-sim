#include "Swarm/Steering/BoidSteeringBehavior.h"

bool UBoidSteeringBehavior::ValidateSettings(FString& OutReason) const
{
	if (FMath::IsFinite(Weight) && Weight >= 0.f && FMath::IsFinite(SeededWeightVariation) && SeededWeightVariation >= 0.f && SeededWeightVariation <= 1.f) return true;
	OutReason = TEXT("Behavior weight must be finite and nonnegative; seeded weight variation must be between zero and one.");
	return false;
}

void UBoidSteeringBehavior::InitializeRuntime(FBoidSteeringRuntimeState& Runtime, int32 Seed) const
{
	Runtime = FBoidSteeringRuntimeState();
	Runtime.Random.Initialize(Seed);
	const float Variation = FMath::Clamp(SeededWeightVariation, 0.0f, 1.0f);
	Runtime.WeightScale = FMath::Max(0.0f, 1.0f + Runtime.Random.FRandRange(-Variation, Variation));
}

FVector UBoidSteeringBehavior::Evaluate(const FBoidSteeringInput& Input,
	FBoidSteeringRuntimeState& Runtime) const
{
	return EvaluateWithResult(Input, Runtime).Direction;
}

FBoidSteeringResult UBoidSteeringBehavior::EvaluateWithResult(const FBoidSteeringInput& Input,
	FBoidSteeringRuntimeState& Runtime) const
{
	FBoidSteeringResult Result;
	if (Input.bCoordinatedCombat && !SupportsNavigationPhase(Input.NavigationPhase)) return Result;
	Result.Direction = CalculateDirection(Input, Runtime).GetSafeNormal()
		* FMath::Max(0.0f, Weight) * Runtime.WeightScale;
	Result.bHasDesiredSpeed = !Result.Direction.IsNearlyZero()
		&& FMath::Max(0.0f, Weight) * Runtime.WeightScale > 0.0f
		&& CalculateDesiredSpeed(Input, Result.DesiredSpeedCmPerSec);
	Result.bHasDesiredSpeed &= FMath::IsFinite(Result.DesiredSpeedCmPerSec);
	if (Result.bHasDesiredSpeed)
	{
		Result.DesiredSpeedCmPerSec = FMath::Max(0.0f, Result.DesiredSpeedCmPerSec);
	}
	return Result;
}

FVector UBoidWanderSteeringBehavior::CalculateDirection(const FBoidSteeringInput& Input,
	FBoidSteeringRuntimeState& Runtime) const
{
	if (Runtime.DriftChangeRemaining <= 0.0f)
	{
		Runtime.DriftYawRateDeg = Runtime.Random.FRandRange(-MaxDriftYawRateDeg, MaxDriftYawRateDeg);
		Runtime.DriftPitchRateDeg = Runtime.Random.FRandRange(-MaxDriftPitchRateDeg, MaxDriftPitchRateDeg);
		Runtime.DriftChangeRemaining = FMath::Max(0.01f, DriftChangeSeconds);
	}
	Runtime.DriftChangeRemaining -= FMath::Max(0.0f, Input.DeltaTime);
	const FVector Yawed = Input.Heading.RotateAngleAxis(Runtime.DriftYawRateDeg * Input.DeltaTime, FVector::UpVector);
	FVector Right = FVector::CrossProduct(FVector::UpVector, Yawed).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::RightVector;
	}
	return Yawed.RotateAngleAxis(Runtime.DriftPitchRateDeg * Input.DeltaTime, Right).GetSafeNormal();
}

FVector UBoidSeekSteeringBehavior::CalculateDirection(const FBoidSteeringInput& Input,
	FBoidSteeringRuntimeState& Runtime) const
{
	if (Input.bPreferNavigationGoal && Input.bHasGoal)
	{
		return (Input.Goal - Input.Position).GetSafeNormal();
	}
	return Input.bHasTarget && (bSeekWhenGoalPresent || !Input.bHasGoal)
		? (Input.TargetPosition - Input.Position).GetSafeNormal() : FVector::ZeroVector;
}
