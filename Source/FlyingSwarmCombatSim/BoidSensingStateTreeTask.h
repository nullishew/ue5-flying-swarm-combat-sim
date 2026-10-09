#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoidSensingStateTreeTask.generated.h"

USTRUCT()
struct FBoidSensingTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	float SecondsUntilNextUpdate = 0.0f;

	UPROPERTY(Transient)
	float ElapsedSinceLastUpdate = 0.0f;
};

/** Updates the owning boid's target facts at a low, steady rate while Alive is active. */
USTRUCT(meta = (DisplayName = "Update Boid Sensing", Category = "Boid"))
struct FBoidSensingStateTreeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBoidSensingStateTreeTask()
	{
		bShouldCallTick = true;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBoidSensingTaskInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};
