#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoidWanderStateTreeTask.generated.h"

class UBoidSteeringProfile;

USTRUCT()
struct FBoidWanderTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Steering")
	TObjectPtr<UBoidSteeringProfile> SteeringProfile;

	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> PreviousProfile;

	UPROPERTY(Transient)
	bool bWasSteeringActive = false;
};

/** StateTree selects a profile; the shared runner computes and applies its movement. */
USTRUCT(meta = (DisplayName = "Select Boid Steering Profile", Category = "Boid"))
struct FBoidWanderStateTreeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBoidWanderStateTreeTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBoidWanderTaskInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
