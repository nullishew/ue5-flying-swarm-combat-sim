#include "Swarm/AI/BoidWanderStateTreeTask.h"

#include "Swarm/FlyingBoid.h"
#include "Swarm/Steering/BoidSteeringProfile.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FBoidWanderStateTreeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid) || !Boid->IsAlive())
	{
		return EStateTreeRunStatus::Failed;
	}
	FBoidWanderTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	InstanceData.PreviousProfile = Boid->GetSteeringProfile();
	InstanceData.bWasSteeringActive = Boid->IsSteeringActive();
	InstanceData.PreviousIntent = Boid->GetMovementIntent();
	InstanceData.PreviousIntentOwner = Boid->GetMovementIntentOwner();
	InstanceData.IntentToken = Boid->AcquireIntentToken();
	FBoidMovementIntent CruiseIntent = InstanceData.PreviousIntent;
	CruiseIntent.Policy = EBoidMovementPolicy::Cruise;
	CruiseIntent.bHasGoal = false;
	CruiseIntent.bIsTransit = false;
	CruiseIntent.Goal = FVector::ZeroVector;
	if (CruiseIntent.Policy != InstanceData.PreviousIntent.Policy
		|| InstanceData.PreviousIntent.bHasGoal
		|| Boid->GetMovementIntentOwner() != InstanceData.IntentToken)
	{
		Boid->SetMovementIntent(CruiseIntent, InstanceData.IntentToken);
	}
	if (InstanceData.SteeringProfile)
	{
		Boid->SetSteeringProfile(InstanceData.SteeringProfile);
	}
	else
	{
		// Existing StateTree assets with this task but no profile keep using the
		// runner's default Wander profile until the designer assigns a profile.
		Boid->SetWanderActive(true);
	}
	InstanceData.AppliedProfile = Boid->GetSteeringProfile();
	return EStateTreeRunStatus::Running;
}

void FBoidWanderStateTreeTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	if (AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner()))
	{
		const FBoidWanderTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
		if (!Boid->IsAlive())
		{
			return;
		}
		if (Boid->GetMovementIntentOwner() == InstanceData.IntentToken)
		{
			Boid->SetMovementIntent(InstanceData.PreviousIntent, InstanceData.PreviousIntentOwner);
		}
		if (Boid->IsSteeringActive() && Boid->GetSteeringProfile() == InstanceData.AppliedProfile
			&& InstanceData.bWasSteeringActive)
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
		else if (Boid->IsSteeringActive() && Boid->GetSteeringProfile() == InstanceData.AppliedProfile)
		{
			Boid->SetWanderActive(false);
		}
	}
}
