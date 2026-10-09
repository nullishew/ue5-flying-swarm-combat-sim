#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "UObject/Object.h"
#include "BoidSteeringBehavior.generated.h"

class AActor;

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
	float DeltaTime = 0.0f;
	int32 AgentId = INDEX_NONE;
};

/** Per-agent mutable state. This is never stored on a shared profile/behavior asset. */
struct FBoidSteeringRuntimeState
{
	FRandomStream Random;
	float WeightScale = 1.0f;
	float DriftChangeRemaining = 0.0f;
	float DriftYawRateDeg = 0.0f;
	float DriftPitchRateDeg = 0.0f;
};

/** A native steering algorithm with editor-authored settings and per-agent runtime state. */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
class FLYINGSWARMCOMBATSIM_API UBoidSteeringBehavior : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Maximum proportional weight variation sampled once for each agent. 0.2 means ±20%. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering|Variation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SeededWeightVariation = 0.0f;

	void InitializeRuntime(FBoidSteeringRuntimeState& Runtime, int32 Seed) const;
	FVector Evaluate(const FBoidSteeringInput& Input, FBoidSteeringRuntimeState& Runtime) const;

protected:
	virtual FVector CalculateDirection(const FBoidSteeringInput& Input,
		FBoidSteeringRuntimeState& Runtime) const { return FVector::ZeroVector; }
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class FLYINGSWARMCOMBATSIM_API UBoidWanderSteeringBehavior : public UBoidSteeringBehavior
{
	GENERATED_BODY()

public:
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

protected:
	virtual FVector CalculateDirection(const FBoidSteeringInput& Input,
		FBoidSteeringRuntimeState& Runtime) const override;
};
