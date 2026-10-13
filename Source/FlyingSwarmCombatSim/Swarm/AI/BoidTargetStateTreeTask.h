// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "Swarm/BoidIntentTypes.h"
#include "BoidTargetStateTreeTask.generated.h"

class AActor;
class UBoidSteeringProfile;

/** Inputs and restoration data for the target-seeking state task. */
USTRUCT()
struct FBoidTargetTaskInstanceData
{
	GENERATED_BODY()

	/** Optional StateTree-bound target. If unset, the task uses the boid's current MovementTarget. */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> TargetActor;

	/** Profile containing the Seek behavior used while this state is active. */
	UPROPERTY(EditAnywhere, Category = "Steering")
	TObjectPtr<UBoidSteeringProfile> SteeringProfile;

	UPROPERTY(Transient)
	TObjectPtr<AActor> PreviousTarget;

	UPROPERTY(Transient)
	TObjectPtr<AActor> AssignedTarget;

	UPROPERTY(Transient)
	uint64 IntentToken = 0;

	UPROPERTY(Transient)
	uint64 PreviousTargetOwner = 0;

	UPROPERTY(Transient)
	FBoidMovementIntent PreviousIntent;

	UPROPERTY(Transient)
	uint64 PreviousIntentOwner = 0;

	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> PreviousProfile;

	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> AppliedProfile;

	UPROPERTY(Transient)
	bool bWasSteeringActive = false;

	UPROPERTY(Transient)
	bool bUsesBoundTarget = false;

	UPROPERTY(Transient)
	bool bHasAppliedIntent = false;

	UPROPERTY(Transient)
	bool bDisabledSteeringAfterTargetLoss = false;

	UPROPERTY(Transient)
	bool bLostOwnership = false;
};

/** Selects a Seek profile and keeps the boid's target input current; the runner owns movement. */
USTRUCT(meta = (DisplayName = "Seek Target", Category = "Boid"))
struct FBoidTargetStateTreeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBoidTargetStateTreeTask()
	{
		bShouldCallTick = true;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBoidTargetTaskInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
