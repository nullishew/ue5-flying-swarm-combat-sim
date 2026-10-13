#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "Swarm/BoidIntentTypes.h"
#include "BoidRangedPhaseStateTreeTask.generated.h"

USTRUCT()
struct FBoidRangedPhaseTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Phase") EBoidRangedPhaseRequest Phase = EBoidRangedPhaseRequest::Approach;
	UPROPERTY(Transient) uint64 OwnerToken = 0;
	UPROPERTY(Transient) uint64 CombatToken = 0;
};

/** Child phase request only. The Combat parent retains target, profile and intent ownership. */
USTRUCT(meta=(DisplayName="Request Ranged Phase", Category="Boid"))
struct FLYINGSWARMCOMBATSIM_API FBoidRangedPhaseStateTreeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FBoidRangedPhaseTaskInstanceData;
	FBoidRangedPhaseStateTreeTask() { bShouldCallTick = true; bShouldStateChangeOnReselect = false; }
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
