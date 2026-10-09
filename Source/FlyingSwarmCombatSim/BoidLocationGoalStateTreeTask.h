#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoidIntentTypes.h"
#include "BoidLocationGoalStateTreeTask.generated.h"

class UBoidSteeringProfile;

USTRUCT()
struct FBoidLocationGoalTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Goal")
	FVector Goal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Goal", meta = (ClampMin = "0.0", Units = "cm"))
	float ArrivalRadiusCm = 80.0f;

	UPROPERTY(EditAnywhere, Category = "Steering")
	TObjectPtr<UBoidSteeringProfile> SteeringProfile;

	UPROPERTY(Transient)
	FBoidMovementIntent PreviousIntent;

	UPROPERTY(Transient)
	uint64 IntentToken = 0;

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

/** Selects a world-space arrival goal; the runner validates the segment and applies movement. */
USTRUCT(meta = (DisplayName = "Follow Location Goal", Category = "Boid"))
struct FBoidLocationGoalStateTreeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBoidLocationGoalStateTreeTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBoidLocationGoalTaskInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
