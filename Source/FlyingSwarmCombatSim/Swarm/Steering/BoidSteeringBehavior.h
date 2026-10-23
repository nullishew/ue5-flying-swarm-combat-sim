#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Math/RandomStream.h"
#include "UObject/Object.h"
#include "Swarm/Routing/BoidNavigationTypes.h"
#include "BoidSteeringBehavior.generated.h"

class AActor;

// Captured once by the simulation; behaviors never query or move neighboring actors.
struct FBoidSteeringNeighbor
{
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float Radius = 0.f;
	int32 AgentId = INDEX_NONE;
	bool bSteeringActive = false;
};

/** Immutable inputs captured before the runner calculates any agent output. */
struct FBoidSteeringInput
{
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector Heading = FVector::ForwardVector;
	FVector TargetPosition = FVector::ZeroVector;
	bool bHasTarget = false;
	FVector Goal = FVector::ZeroVector;
	bool bHasGoal = false;
	/** True when Goal is a moving waypoint whose planner has already validated its route. */
	bool bContinuousGoal = false;
	/** Optional speed requested by the active movement planner (cm/s). */
	bool bHasDesiredSpeed = false;
	float DesiredSpeedCmPerSec = 0.0f;
	float FlightSpeedLimitCmPerSec = 0.0f;
	EBoidNavigationPhase NavigationPhase = EBoidNavigationPhase::None;
	FBoidOrbitContext OrbitContext;
	bool bPreferNavigationGoal = false;
	bool bCoordinatedCombat = false;
	float DeltaTime = 0.0f;
	int32 AgentId = INDEX_NONE;
	float Radius = 0.f;
	TConstArrayView<FBoidSteeringNeighbor> Neighbors;
};

/** Direction contribution and optional speed request from one behavior. */
struct FBoidSteeringResult
{
	FVector Direction = FVector::ZeroVector;
	bool bHasDesiredSpeed = false;
	float DesiredSpeedCmPerSec = 0.0f;
	bool bCriticalSeparation = false;
};

/** Per-agent mutable state. This is never stored on a shared profile/behavior asset. */
struct FBoidSteeringRuntimeState
{
	FRandomStream Random;
	float WeightScale = 1.0f;
	float DriftChangeRemaining = 0.0f;
	float DriftYawRateDeg = 0.0f;
	float DriftPitchRateDeg = 0.0f;
	FVector HeldAvoidanceDirection = FVector::ZeroVector;
	float AvoidanceHoldSeconds = 0.f;
};

/** A native steering algorithm with editor-authored settings and per-agent runtime state. */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
class FLYINGSWARMCOMBATSIM_API UBoidSteeringBehavior : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Maximum proportional weight variation sampled once for each agent. 0.2 means Â±20%. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering|Variation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SeededWeightVariation = 0.0f;

	/** Advertises goal support so StateTree tasks can validate custom behaviors without class checks. */
	virtual bool SupportsLocationGoals(bool bContinuous) const { return false; }
	/** Captured phase gating; additive contributions never own combat speed. */
	virtual bool SupportsNavigationPhase(EBoidNavigationPhase Phase) const { return true; }
	virtual bool IsPrimaryMovementForPhase(EBoidNavigationPhase Phase) const { return false; }
	virtual bool IsLocalSteeringBehavior() const { return false; }
	virtual bool ValidateSettings(FString& OutReason) const;
	virtual float NeighborQueryRadius(const FBoidSteeringInput& Input, float MaximumRadius, float MaximumSpeed) const { return 0.f; }
	virtual float NeighborPairRadius(const FBoidSteeringInput& Input, const FBoidSteeringNeighbor& Neighbor) const { return NeighborQueryRadius(Input, Neighbor.Radius, Neighbor.Velocity.Size()); }
	void InitializeRuntime(FBoidSteeringRuntimeState& Runtime, int32 Seed) const;
	FVector Evaluate(const FBoidSteeringInput& Input, FBoidSteeringRuntimeState& Runtime) const;
	virtual FBoidSteeringResult EvaluateWithResult(const FBoidSteeringInput& Input,
		FBoidSteeringRuntimeState& Runtime) const;

protected:
	virtual FVector CalculateDirection(const FBoidSteeringInput& Input,
		FBoidSteeringRuntimeState& Runtime) const { return FVector::ZeroVector; }
	virtual bool CalculateDesiredSpeed(const FBoidSteeringInput& Input, float& OutSpeedCmPerSec) const
	{
		return false;
	}
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class FLYINGSWARMCOMBATSIM_API UBoidWanderSteeringBehavior : public UBoidSteeringBehavior
{
	GENERATED_BODY()

public:
	virtual bool SupportsNavigationPhase(EBoidNavigationPhase Phase) const override { return Phase == EBoidNavigationPhase::None; }
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wander", meta = (ClampMin = "0.01"))
	float DriftChangeSeconds = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wander", meta = (ClampMin = "0.0"))
	float MaxDriftYawRateDeg = 20.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wander", meta = (ClampMin = "0.0"))
	float MaxDriftPitchRateDeg = 12.0f;

protected:
	virtual FVector CalculateDirection(const FBoidSteeringInput& Input,
		FBoidSteeringRuntimeState& Runtime) const override;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class FLYINGSWARMCOMBATSIM_API UBoidSeekSteeringBehavior : public UBoidSteeringBehavior
{
	GENERATED_BODY()

public:
	virtual bool SupportsNavigationPhase(EBoidNavigationPhase Phase) const override { return Phase == EBoidNavigationPhase::None; }
	/** Disable in profiles where a planner-provided location goal should take priority over the actor target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Seek")
	bool bSeekWhenGoalPresent = true;

protected:
	virtual FVector CalculateDirection(const FBoidSteeringInput& Input,
		FBoidSteeringRuntimeState& Runtime) const override;
};
