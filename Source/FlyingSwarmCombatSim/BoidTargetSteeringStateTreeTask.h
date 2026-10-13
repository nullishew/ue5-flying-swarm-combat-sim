#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "BoidIntentTypes.h"
#include "BoidTargetSteeringStateTreeTask.generated.h"

class AActor;
class UBoidSteeringProfile;

USTRUCT()
struct FLYINGSWARMCOMBATSIM_API FBoidTargetSteeringTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Steering")
	TObjectPtr<UBoidSteeringProfile> SteeringProfile;

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

/** Shared target/profile ownership lifecycle. Derived tasks supply their movement intent. */
USTRUCT(meta = (Hidden))
struct FLYINGSWARMCOMBATSIM_API FBoidTargetSteeringStateTreeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBoidTargetSteeringStateTreeTask()
	{
		bShouldCallTick = true;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBoidTargetSteeringTaskInstanceData;
	virtual FBoidMovementIntent MakeIntent(const FBoidTargetSteeringTaskInstanceData& Data) const;
	virtual bool SupportsProfile(const UBoidSteeringProfile* Profile) const;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
