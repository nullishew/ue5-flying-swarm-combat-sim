#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoidAttackStateTreeTask.generated.h"

USTRUCT()
struct FBoidAttackTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	uint64 ContextToken = 0;
};

/** Enables the single attack scheduler while Combat is active; never owns movement or completes the state. */
USTRUCT(meta=(DisplayName="Run Boid Combat Attacks", Category="Boid|Combat"))
struct FBoidAttackStateTreeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBoidAttackStateTreeTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
#if WITH_EDITORONLY_DATA
		bConsideredForCompletion = false;
		bCanEditConsideredForCompletion = false;
#endif
	}

	using FInstanceDataType = FBoidAttackTaskInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
