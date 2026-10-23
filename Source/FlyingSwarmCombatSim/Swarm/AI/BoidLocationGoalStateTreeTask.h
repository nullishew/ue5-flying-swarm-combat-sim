#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "Swarm/BoidIntentTypes.h"
#include "BoidLocationGoalStateTreeTask.generated.h"

class UBoidSteeringProfile;

USTRUCT()
struct FBoidLocationGoalTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Goal")
	FVector Goal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Goal")
	bool bUseLastKnownTransitGoal = false;

	UPROPERTY(EditAnywhere, Category = "Goal", meta = (ClampMin = "0.0", Units = "cm"))
	float ArrivalRadiusCm = 80.0f;

	UPROPERTY(EditAnywhere, Category = "Steering")
	TObjectPtr<UBoidSteeringProfile> SteeringProfile;

	UPROPERTY(Transient)
	FBoidMovementIntent PreviousIntent;

	UPROPERTY(Transient)
	uint64 IntentToken = 0;

	UPROPERTY(Transient)
	uint64 TransitGoalRevision = 0;

	UPROPERTY(Transient)
	uint64 PreviousIntentOwner = 0;

	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> PreviousProfile;

	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> AppliedProfile;

	UPROPERTY(Transient)
	bool bWasSteeringActive = false;

	UPROPERTY(Transient)
	bool bHasAppliedIntent = false;
};

// Selects an arrival goal, or a remembered-goal fly-through that succeeds when the runner records arrival.
USTRUCT(meta = (DisplayName = "Follow Location Goal", Category = "Boid"))
struct FBoidLocationGoalStateTreeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBoidLocationGoalStateTreeTask()
	{
		bShouldCallTick = true;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBoidLocationGoalTaskInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
