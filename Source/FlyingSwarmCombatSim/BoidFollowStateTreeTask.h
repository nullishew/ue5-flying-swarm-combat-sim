#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoidIntentTypes.h"
#include "BoidFollowStateTreeTask.generated.h"

class AActor;
class UBoidSteeringProfile;

USTRUCT()
struct FBoidFollowTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Steering")
	TObjectPtr<UBoidSteeringProfile> SteeringProfile;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float StandOffDistanceCm = 600.0f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumTargetDistanceCm = 300.0f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float ArrivalRadiusCm = 80.0f;

	UPROPERTY(Transient)
	TObjectPtr<AActor> PreviousTarget;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> AssignedTarget;

	UPROPERTY(Transient)
	uint64 IntentToken = 0;

	UPROPERTY(Transient)
	uint64 PreviousTargetOwner = 0;

	UPROPERTY(Transient)
	uint64 PreviousIntentOwner = 0;

	UPROPERTY(Transient)
	FBoidMovementIntent PreviousIntent;

	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> PreviousProfile;

	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> AppliedProfile;

	UPROPERTY(Transient)
	bool bWasSteeringActive = false;

	UPROPERTY(Transient)
	bool bHasAppliedIntent = false;

	UPROPERTY(Transient)
	bool bDisabledSteeringAfterTargetLoss = false;

	UPROPERTY(Transient)
	bool bLostOwnership = false;
};

/** Assigns the current sensed player and FollowStandOff intent; movement stays with the runner. */
USTRUCT(meta = (DisplayName = "Follow Detected Target", Category = "Boid"))
struct FBoidFollowStateTreeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBoidFollowStateTreeTask()
	{
		bShouldCallTick = true;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBoidFollowTaskInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
