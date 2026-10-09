#include "BoidLocationGoalStateTreeTask.h"

#include "BoidSteeringBehavior.h"
#include "BoidPathFollowingSteeringBehavior.h"
#include "BoidSteeringProfile.h"
#include "FlyingBoid.h"
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
			if (IsValid(Entry.Behavior) && Entry.Behavior->IsA<UBoidPathFollowingSteeringBehavior>())
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
	if (!IsValid(Boid) || !Boid->IsAlive() || InstanceData.Goal.ContainsNaN()
		|| !HasPathFollowingBehavior(InstanceData.SteeringProfile))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.PreviousIntent = Boid->GetMovementIntent();
	InstanceData.PreviousIntentOwner = Boid->GetMovementIntentOwner();
	InstanceData.IntentToken = Boid->AcquireIntentToken();
	InstanceData.PreviousProfile = Boid->GetSteeringProfile();
	InstanceData.AppliedProfile = InstanceData.SteeringProfile;
	InstanceData.bWasSteeringActive = Boid->IsSteeringActive();

	FBoidMovementIntent GoalIntent = InstanceData.PreviousIntent;
	GoalIntent.Policy = EBoidMovementPolicy::LocationArrival;
	GoalIntent.bHasGoal = true;
	GoalIntent.Goal = InstanceData.Goal;
	GoalIntent.ArrivalRadiusCm = FMath::Max(0.0f, InstanceData.ArrivalRadiusCm);
	Boid->SetMovementIntent(GoalIntent, InstanceData.IntentToken);
	Boid->SetSteeringProfile(InstanceData.SteeringProfile);
	InstanceData.bHasAppliedIntent = true;
	return EStateTreeRunStatus::Running;
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
