#include "BoidFollowStateTreeTask.h"

#include "BoidSteeringBehavior.h"
#include "BoidSteeringProfile.h"
#include "FlyingCharacter.h"
#include "FlyingBoid.h"
#include "StateTreeExecutionContext.h"

// File-specific scope also isolates helpers in Unreal unity translation units.
namespace BoidFollowTaskPrivate
{
namespace
{
	bool HasSeekBehavior(const UBoidSteeringProfile* Profile)
	{
		if (!IsValid(Profile))
		{
			return false;
		}

		for (const FBoidSteeringBehaviorEntry& Entry : Profile->Behaviors)
		{
			if (IsValid(Entry.Behavior) && Entry.Behavior->IsA<UBoidSeekSteeringBehavior>())
			{
				return true;
			}
		}
		return false;
	}

	bool IsUsableTarget(AActor* Target, const AFlyingBoid* Boid)
	{
		if (!IsValid(Target) || Target == Boid)
		{
			return false;
		}
		if (const AFlyingCharacter* Player = Cast<AFlyingCharacter>(Target))
		{
			return Player->IsAlive();
		}
		if (const AFlyingBoid* OtherBoid = Cast<AFlyingBoid>(Target))
		{
			return OtherBoid->IsAlive();
		}
		return true;
	}

	bool HasFollowIntent(const FBoidMovementIntent& Intent, const float StandOffDistanceCm,
		const float MinimumTargetDistanceCm, const float ArrivalRadiusCm)
	{
		return Intent.Policy == EBoidMovementPolicy::FollowStandOff
			&& !Intent.bHasGoal
			&& FMath::IsNearlyEqual(Intent.StandOffDistanceCm, StandOffDistanceCm)
			&& FMath::IsNearlyEqual(Intent.MinimumTargetDistanceCm, MinimumTargetDistanceCm)
			&& FMath::IsNearlyEqual(Intent.ArrivalRadiusCm, ArrivalRadiusCm);
	}
}
}

EStateTreeRunStatus FBoidFollowStateTreeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid) || !Boid->IsAlive())
	{
		return EStateTreeRunStatus::Failed;
	}

	FBoidFollowTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	InstanceData.bHasAppliedIntent = false;
	InstanceData.bDisabledSteeringAfterTargetLoss = false;
	InstanceData.bLostOwnership = false;
	const FBoidTargetFacts& Facts = Boid->GetTargetFacts();
	AActor* Target = Facts.TargetActor;
	if (!Facts.bDetected || !BoidFollowTaskPrivate::IsUsableTarget(Target, Boid)
		|| !BoidFollowTaskPrivate::HasSeekBehavior(InstanceData.SteeringProfile))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.PreviousTarget = Boid->MovementTarget;
	InstanceData.PreviousTargetOwner = Boid->GetMovementTargetOwner();
	InstanceData.PreviousIntent = Boid->GetMovementIntent();
	InstanceData.PreviousIntentOwner = Boid->GetMovementIntentOwner();
	InstanceData.IntentToken = Boid->AcquireIntentToken();
	InstanceData.PreviousProfile = Boid->GetSteeringProfile();
	InstanceData.AppliedProfile = InstanceData.SteeringProfile;
	InstanceData.bWasSteeringActive = Boid->IsSteeringActive();
	InstanceData.bHasAppliedIntent = true;

	FBoidMovementIntent FollowIntent = InstanceData.PreviousIntent;
	FollowIntent.Policy = EBoidMovementPolicy::FollowStandOff;
	FollowIntent.bHasGoal = false;
	FollowIntent.Goal = FVector::ZeroVector;
	FollowIntent.StandOffDistanceCm = FMath::Max(0.0f, InstanceData.StandOffDistanceCm);
	FollowIntent.MinimumTargetDistanceCm = FMath::Max(0.0f, InstanceData.MinimumTargetDistanceCm);
	FollowIntent.ArrivalRadiusCm = FMath::Max(0.0f, InstanceData.ArrivalRadiusCm);
	if (!BoidFollowTaskPrivate::HasFollowIntent(InstanceData.PreviousIntent, InstanceData.StandOffDistanceCm,
		InstanceData.MinimumTargetDistanceCm, InstanceData.ArrivalRadiusCm)
		|| Boid->GetMovementIntentOwner() != InstanceData.IntentToken)
	{
		Boid->SetMovementIntent(FollowIntent, InstanceData.IntentToken);
	}
	Boid->SetMovementTarget(Target, InstanceData.IntentToken);
	InstanceData.AssignedTarget = Target;
	Boid->SetSteeringProfile(InstanceData.SteeringProfile);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FBoidFollowStateTreeTask::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	(void)DeltaTime;
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid) || !Boid->IsAlive())
	{
		return EStateTreeRunStatus::Failed;
	}

	FBoidFollowTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	if (Boid->GetMovementIntentOwner() != InstanceData.IntentToken
		|| Boid->GetMovementTargetOwner() != InstanceData.IntentToken)
	{
		InstanceData.bLostOwnership = true;
		return EStateTreeRunStatus::Failed;
	}

	const FBoidTargetFacts& Facts = Boid->GetTargetFacts();
	AActor* Target = Facts.TargetActor;
	if (!Facts.bDetected || !BoidFollowTaskPrivate::IsUsableTarget(Target, Boid))
	{
		if (Boid->GetMovementTargetOwner() == InstanceData.IntentToken)
		{
			Boid->SetMovementTarget(nullptr, InstanceData.IntentToken);
		}
		if (Boid->GetSteeringProfile() == InstanceData.AppliedProfile)
		{
			Boid->SetWanderActive(false);
			InstanceData.bDisabledSteeringAfterTargetLoss = true;
		}
		return EStateTreeRunStatus::Failed;
	}

	const FBoidMovementIntent CurrentIntent = Boid->GetMovementIntent();
	if (!BoidFollowTaskPrivate::HasFollowIntent(CurrentIntent, InstanceData.StandOffDistanceCm,
			InstanceData.MinimumTargetDistanceCm, InstanceData.ArrivalRadiusCm))
	{
		FBoidMovementIntent FollowIntent = CurrentIntent;
		FollowIntent.Policy = EBoidMovementPolicy::FollowStandOff;
		FollowIntent.bHasGoal = false;
		FollowIntent.Goal = FVector::ZeroVector;
		FollowIntent.StandOffDistanceCm = FMath::Max(0.0f, InstanceData.StandOffDistanceCm);
		FollowIntent.MinimumTargetDistanceCm = FMath::Max(0.0f, InstanceData.MinimumTargetDistanceCm);
		FollowIntent.ArrivalRadiusCm = FMath::Max(0.0f, InstanceData.ArrivalRadiusCm);
		Boid->SetMovementIntent(FollowIntent, InstanceData.IntentToken);
	}

	if (Boid->MovementTarget != Target)
	{
		Boid->SetMovementTarget(Target, InstanceData.IntentToken);
		InstanceData.AssignedTarget = Target;
	}
	return EStateTreeRunStatus::Running;
}

void FBoidFollowStateTreeTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid))
	{
		return;
	}

	FBoidFollowTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.bHasAppliedIntent)
	{
		return;
	}

	if (Boid->IsAlive())
	{
		if (Boid->GetMovementTargetOwner() == InstanceData.IntentToken)
		{
			Boid->SetMovementTarget(InstanceData.PreviousTarget, InstanceData.PreviousTargetOwner);
		}
		if (!InstanceData.bLostOwnership && Boid->GetMovementIntentOwner() == InstanceData.IntentToken)
		{
			Boid->SetMovementIntent(InstanceData.PreviousIntent, InstanceData.PreviousIntentOwner);
		}
		if (!InstanceData.bLostOwnership && (Boid->GetSteeringProfile() == InstanceData.AppliedProfile
			|| (InstanceData.bDisabledSteeringAfterTargetLoss && !Boid->IsSteeringActive())))
		{
			if (InstanceData.bWasSteeringActive && InstanceData.PreviousProfile)
			{
				Boid->SetSteeringProfile(InstanceData.PreviousProfile);
			}
			else if (InstanceData.bWasSteeringActive)
			{
				Boid->SetWanderActive(true);
			}
			else
			{
				Boid->SetWanderActive(false);
			}
		}
	}
	InstanceData.bHasAppliedIntent = false;
}
