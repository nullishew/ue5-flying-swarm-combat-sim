#pragma once

#include "CoreMinimal.h"
#include "Swarm/Steering/BoidSteeringBehavior.h"
#include "BoidOrbitSteeringBehavior.generated.h"

/** Native finite-step angular, radius and elevation steering. The coordinator collision-checks its predicted step. */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class FLYINGSWARMCOMBATSIM_API UBoidOrbitSteeringBehavior : public UBoidSteeringBehavior
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Orbit", meta=(ClampMin="0", ToolTip="Exponential correction gain per second. When unconstrained, 1 closes approximately 63 percent of the remaining error in one second.")) float RadialCorrectionGainPerSec = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Orbit", meta=(ClampMin="0", ToolTip="Exponential correction gain per second. When unconstrained, 1 closes approximately 63 percent of the remaining error in one second.")) float ElevationCorrectionGainPerSec = 1.f;
	virtual bool SupportsLocationGoals(bool bContinuous) const override { return bContinuous; }
	virtual bool SupportsNavigationPhase(EBoidNavigationPhase Phase) const override { return Phase == EBoidNavigationPhase::Orbit; }
	virtual bool IsPrimaryMovementForPhase(EBoidNavigationPhase Phase) const override { return Phase == EBoidNavigationPhase::Orbit; }
	/** Predict a bounded, finite-time orbit point without reading world state. */
	FVector PredictPosition(const FBoidSteeringInput& Input, float Seconds) const;
	/** Returns the shortest distance from Center to the straight chord Start-End. */
	static float MinimumChordRange(const FVector& Center, const FVector& Start, const FVector& End);
	/** Allows small actual-range drift only when a segment reduces range error. */
	static bool IsRangeStepValid(const FVector& Center, const FVector& Start, const FVector& End,
		float MinimumRange, float MaximumRange, float DesiredRange, float DriftAllowance);
	/** Returns the average requested velocity to the one-step orbit point in cm/s. */
	FVector CalcDesiredVelocity(const FBoidSteeringInput& Input) const;
	virtual FBoidSteeringResult EvaluateWithResult(const FBoidSteeringInput& Input,
		FBoidSteeringRuntimeState& Runtime) const override;

protected:
	virtual FVector CalculateDirection(const FBoidSteeringInput& Input,
		FBoidSteeringRuntimeState& Runtime) const override;
	virtual bool CalculateDesiredSpeed(const FBoidSteeringInput& Input,
		float& OutSpeedCmPerSec) const override;
};
