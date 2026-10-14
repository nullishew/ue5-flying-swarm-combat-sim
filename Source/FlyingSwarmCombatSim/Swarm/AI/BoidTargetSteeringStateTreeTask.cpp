#include "Swarm/AI/BoidTargetSteeringStateTreeTask.h"

#include "Swarm/Steering/BoidSteeringBehavior.h"
#include "Swarm/Steering/BoidSteeringProfile.h"
#include "Player/FlyingCharacter.h"
#include "Swarm/FlyingBoid.h"
#include "Swarm/SwarmSimulationRunner.h"
#include "StateTreeExecutionContext.h"

// File-specific scope also isolates helpers in Unreal unity translation units.
namespace BoidTargetSteeringTaskPrivate
{
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

	bool SameIntent(const FBoidMovementIntent& A, const FBoidMovementIntent& B)
	{
		return A.RangedPhaseControl == B.RangedPhaseControl && A.bIsTransit == B.bIsTransit && A.Policy == B.Policy && A.bHasGoal == B.bHasGoal && A.Goal.Equals(B.Goal)
			&& FMath::IsNearlyEqual(A.StandOffDistanceCm, B.StandOffDistanceCm)
			&& FMath::IsNearlyEqual(A.MinimumTargetDistanceCm, B.MinimumTargetDistanceCm)
			&& FMath::IsNearlyEqual(A.ArrivalRadiusCm, B.ArrivalRadiusCm);
	}
}
}

EStateTreeRunStatus FBoidTargetSteeringStateTreeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid) || !Boid->IsAlive())
	{
		return EStateTreeRunStatus::Failed;
	}

	FBoidTargetSteeringTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	InstanceData.bHasAppliedIntent = false;
	InstanceData.bDisabledSteeringAfterTargetLoss = false;
	InstanceData.bLostOwnership = false;
	ASwarmSimulationRunner* Runner = Boid->GetSimulationRunner();
	UBoidSteeringProfile* EffectiveProfile = IsValid(Runner)
		? Runner->ResolveSteeringProfile(Boid, InstanceData.SteeringProfile)
		: (IsValid(InstanceData.SteeringProfile) ? InstanceData.SteeringProfile.Get() : Boid->GetSteeringProfile());
	const FBoidTargetFacts& Facts = Boid->GetTargetFacts();
	AActor* Target = Facts.TargetActor;
	if (!Facts.bDetected || !BoidTargetSteeringTaskPrivate::IsUsableTarget(Target, Boid)
		|| !SupportsProfile(EffectiveProfile))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.PreviousTarget = Boid->MovementTarget;
	InstanceData.PreviousTargetOwner = Boid->GetMovementTargetOwner();
	InstanceData.PreviousIntent = Boid->GetMovementIntent();
	InstanceData.PreviousIntentOwner = Boid->GetMovementIntentOwner();
	InstanceData.IntentToken = Boid->AcquireIntentToken();
	InstanceData.PreviousProfile = Boid->GetSteeringProfile();
	InstanceData.AppliedProfile = EffectiveProfile;
	InstanceData.bWasSteeringActive = Boid->IsSteeringActive();
	InstanceData.bHasAppliedIntent = true;

	const FBoidMovementIntent Intent = MakeIntent(InstanceData);
	Boid->SetMovementIntent(Intent, InstanceData.IntentToken);
	Boid->SetMovementTarget(Target, InstanceData.IntentToken);
	InstanceData.AssignedTarget = Target;
	Boid->SetSteeringProfile(EffectiveProfile);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FBoidTargetSteeringStateTreeTask::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	(void)DeltaTime;
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid) || !Boid->IsAlive())
	{
		return EStateTreeRunStatus::Failed;
	}

	FBoidTargetSteeringTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	if (Boid->GetMovementIntentOwner() != InstanceData.IntentToken
		|| Boid->GetMovementTargetOwner() != InstanceData.IntentToken)
	{
		InstanceData.bLostOwnership = true;
		return EStateTreeRunStatus::Failed;
	}

	const FBoidTargetFacts& Facts = Boid->GetTargetFacts();
	AActor* Target = Facts.TargetActor;
	if (!Facts.bDetected || !BoidTargetSteeringTaskPrivate::IsUsableTarget(Target, Boid))
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

	const FBoidMovementIntent Intent = MakeIntent(InstanceData);
	if (!BoidTargetSteeringTaskPrivate::SameIntent(Boid->GetMovementIntent(), Intent))
	{
		Boid->SetMovementIntent(Intent, InstanceData.IntentToken);
	}

	if (Boid->MovementTarget != Target)
	{
		Boid->SetMovementTarget(Target, InstanceData.IntentToken);
		InstanceData.AssignedTarget = Target;
	}
	return EStateTreeRunStatus::Running;
}

void FBoidTargetSteeringStateTreeTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid))
	{
		return;
	}

	FBoidTargetSteeringTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
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

FBoidMovementIntent FBoidTargetSteeringStateTreeTask::MakeIntent(const FBoidTargetSteeringTaskInstanceData& Data) const
{
	return FBoidMovementIntent();
}

bool FBoidTargetSteeringStateTreeTask::SupportsProfile(const UBoidSteeringProfile* Profile) const
{
	return IsValid(Profile);
}
