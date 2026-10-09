#include "BoidTargetStateTreeTask.h"

#include "BoidSteeringBehavior.h"
#include "BoidSteeringProfile.h"
#include "FlyingCharacter.h"
#include "FlyingBoid.h"
#include "StateTreeExecutionContext.h"

namespace
{
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
}

EStateTreeRunStatus FBoidTargetStateTreeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid) || !Boid->IsAlive())
	{
		return EStateTreeRunStatus::Failed;
	}

	FBoidTargetTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	InstanceData.bHasAppliedIntent = false;
	InstanceData.bDisabledSteeringAfterTargetLoss = false;
	InstanceData.bLostOwnership = false;
	InstanceData.bUsesBoundTarget = IsValid(InstanceData.TargetActor);
	AActor* Target = InstanceData.bUsesBoundTarget ? InstanceData.TargetActor.Get() : Boid->MovementTarget.Get();
	if (!IsUsableTarget(Target, Boid) || !HasSeekBehavior(InstanceData.SteeringProfile))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.PreviousTarget = Boid->MovementTarget;
	InstanceData.PreviousTargetOwner = Boid->GetMovementTargetOwner();
	InstanceData.PreviousIntent = Boid->GetMovementIntent();
	InstanceData.PreviousIntentOwner = Boid->GetMovementIntentOwner();
	InstanceData.IntentToken = Boid->AcquireIntentToken();
	InstanceData.AssignedTarget = nullptr;
	InstanceData.PreviousProfile = Boid->GetSteeringProfile();
	InstanceData.AppliedProfile = InstanceData.SteeringProfile;
	InstanceData.bWasSteeringActive = Boid->IsSteeringActive();
	InstanceData.bHasAppliedIntent = true;
	FBoidMovementIntent CruiseIntent = InstanceData.PreviousIntent;
	CruiseIntent.Policy = EBoidMovementPolicy::Cruise;
	CruiseIntent.bHasGoal = false;
	CruiseIntent.Goal = FVector::ZeroVector;
	Boid->SetMovementIntent(CruiseIntent, InstanceData.IntentToken);
	if (InstanceData.bUsesBoundTarget)
	{
		Boid->SetMovementTarget(Target, InstanceData.IntentToken);
		InstanceData.AssignedTarget = Target;
	}
	Boid->SetSteeringProfile(InstanceData.SteeringProfile);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FBoidTargetStateTreeTask::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	(void)DeltaTime;
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid) || !Boid->IsAlive())
	{
		return EStateTreeRunStatus::Failed;
	}

	FBoidTargetTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	if (Boid->GetMovementIntentOwner() != InstanceData.IntentToken
		|| (InstanceData.bUsesBoundTarget
			&& Boid->GetMovementTargetOwner() != InstanceData.IntentToken))
	{
		InstanceData.bLostOwnership = true;
		return EStateTreeRunStatus::Failed;
	}

	AActor* Target = InstanceData.bUsesBoundTarget ? InstanceData.TargetActor.Get() : Boid->MovementTarget.Get();
	if (!IsUsableTarget(Target, Boid))
	{
		if (Boid->GetSteeringProfile() == InstanceData.AppliedProfile)
		{
			Boid->SetWanderActive(false);
			InstanceData.bDisabledSteeringAfterTargetLoss = true;
		}
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.bUsesBoundTarget && Boid->MovementTarget != Target)
	{
		Boid->SetMovementTarget(Target, InstanceData.IntentToken);
		InstanceData.AssignedTarget = Target;
	}
	return EStateTreeRunStatus::Running;
}

void FBoidTargetStateTreeTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	if (AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner()))
	{
		FBoidTargetTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
		if (!InstanceData.bHasAppliedIntent)
		{
			return;
		}

		if (Boid->IsAlive() && !InstanceData.bLostOwnership && InstanceData.bUsesBoundTarget
			&& Boid->GetMovementTargetOwner() == InstanceData.IntentToken)
		{
			Boid->SetMovementTarget(InstanceData.PreviousTarget, InstanceData.PreviousTargetOwner);
		}
		if (Boid->IsAlive() && !InstanceData.bLostOwnership
			&& Boid->GetMovementIntentOwner() == InstanceData.IntentToken)
		{
			Boid->SetMovementIntent(InstanceData.PreviousIntent, InstanceData.PreviousIntentOwner);
		}

		if (Boid->IsAlive() && !InstanceData.bLostOwnership
			&& (Boid->GetSteeringProfile() == InstanceData.AppliedProfile
			|| (InstanceData.bDisabledSteeringAfterTargetLoss && !Boid->IsSteeringActive()))
			)
		{
			if (InstanceData.bWasSteeringActive)
			{
				if (InstanceData.PreviousProfile)
				{
					Boid->SetSteeringProfile(InstanceData.PreviousProfile);
				}
				else
				{
					Boid->SetWanderActive(true);
				}
			}
			else
			{
				Boid->SetWanderActive(false);
			}
		}
		InstanceData.bHasAppliedIntent = false;
	}
}
