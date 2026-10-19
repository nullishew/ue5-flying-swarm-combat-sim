#include "Swarm/AI/BoidLocationGoalStateTreeTask.h"

#include "Swarm/Steering/BoidSteeringBehavior.h"
#include "Swarm/Steering/BoidPathFollowingSteeringBehavior.h"
#include "Swarm/Steering/BoidSteeringProfile.h"
#include "Swarm/FlyingBoid.h"
#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "StateTreeExecutionContext.h"

namespace
{
	bool HasPathFollowingBehavior(const UBoidSteeringProfile* Profile)
	{
		if (!IsValid(Profile))
		{
			return false;
		}

		for (const FBoidSteeringBehaviorEntry& Entry : Profile->Behaviors)
		{
			if (IsValid(Entry.Behavior) && FMath::IsFinite(Entry.Behavior->Weight)
				&& Entry.Behavior->Weight > 0.f && Entry.Behavior->IsA<UBoidPathFollowingSteeringBehavior>())
			{
				return true;
			}
		}
		return false;
	}
}

EStateTreeRunStatus FBoidLocationGoalStateTreeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	FBoidLocationGoalTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	InstanceData.bHasAppliedIntent = false;
	USwarmSimulationSubsystem* Runner = IsValid(Boid) ? Boid->GetSimulationSubsystem() : nullptr;
	UBoidSteeringProfile* EffectiveProfile = IsValid(Runner)
		? Runner->ResolveSteeringProfile(Boid, InstanceData.SteeringProfile)
		: (IsValid(InstanceData.SteeringProfile) ? InstanceData.SteeringProfile.Get() : (IsValid(Boid) ? Boid->GetSteeringProfile() : nullptr));
	if (!IsValid(Boid) || !Boid->IsAlive() || InstanceData.Goal.ContainsNaN()
		|| !HasPathFollowingBehavior(EffectiveProfile))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (InstanceData.bUseLastKnownTransitGoal && !Boid->GetNavigationFacts().bHasTransitGoal) return EStateTreeRunStatus::Failed;
	InstanceData.PreviousIntent = Boid->GetMovementIntent();
	InstanceData.PreviousIntentOwner = Boid->GetMovementIntentOwner();
	InstanceData.IntentToken = Boid->AcquireIntentToken();
	InstanceData.PreviousProfile = Boid->GetSteeringProfile();
	InstanceData.AppliedProfile = EffectiveProfile;
	InstanceData.bWasSteeringActive = Boid->IsSteeringActive();

	FBoidMovementIntent GoalIntent = InstanceData.PreviousIntent;
	GoalIntent.Policy = EBoidMovementPolicy::LocationArrival;
	GoalIntent.bHasGoal = true;
	GoalIntent.bIsTransit = InstanceData.bUseLastKnownTransitGoal;
	GoalIntent.Goal = InstanceData.bUseLastKnownTransitGoal ? Boid->GetNavigationFacts().TransitGoal : InstanceData.Goal;
	GoalIntent.ArrivalRadiusCm = FMath::Max(0.0f, InstanceData.ArrivalRadiusCm);
	Boid->SetMovementIntent(GoalIntent, InstanceData.IntentToken);
	Boid->SetSteeringProfile(EffectiveProfile);
	InstanceData.bHasAppliedIntent = true;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FBoidLocationGoalStateTreeTask::Tick(FStateTreeExecutionContext& Context,const float DeltaTime) const
{
	const auto& Data=Context.GetInstanceData(*this);
	const AFlyingBoid* Boid=Cast<AFlyingBoid>(Context.GetOwner());
	return IsValid(Boid)&&Boid->IsAlive()&&Boid->GetMovementIntentOwner()==Data.IntentToken
		&&(!Data.bUseLastKnownTransitGoal||Boid->GetNavigationFacts().bHasTransitGoal)?EStateTreeRunStatus::Running:EStateTreeRunStatus::Failed;
}

void FBoidLocationGoalStateTreeTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid) || !Boid->IsAlive())
	{
		return;
	}

	FBoidLocationGoalTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.bHasAppliedIntent)
	{
		return;
	}
	if (Boid->GetMovementIntentOwner() == InstanceData.IntentToken)
	{
		Boid->SetMovementIntent(InstanceData.PreviousIntent, InstanceData.PreviousIntentOwner);
	}
	if (Boid->GetSteeringProfile() == InstanceData.AppliedProfile)
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
	InstanceData.bHasAppliedIntent = false;
}
