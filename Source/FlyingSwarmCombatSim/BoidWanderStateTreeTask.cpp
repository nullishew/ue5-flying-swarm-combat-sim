#include "BoidWanderStateTreeTask.h"

#include "FlyingBoid.h"
#include "BoidSteeringProfile.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FBoidWanderStateTreeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid))
	{
		return EStateTreeRunStatus::Failed;
	}
	FBoidWanderTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	InstanceData.PreviousProfile = Boid->GetSteeringProfile();
	InstanceData.bWasSteeringActive = Boid->IsSteeringActive();
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
	return EStateTreeRunStatus::Running;
}

void FBoidWanderStateTreeTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	if (AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner()))
	{
		const FBoidWanderTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
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
}
