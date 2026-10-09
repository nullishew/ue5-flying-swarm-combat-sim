#include "BoidSensingStateTreeTask.h"

#include "FlyingBoid.h"
#include "StateTreeExecutionContext.h"

namespace
{
	constexpr float SensingIntervalSeconds = 0.2f;
}

EStateTreeRunStatus FBoidSensingStateTreeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid) || !Boid->IsAlive())
	{
		return EStateTreeRunStatus::Failed;
	}

	FBoidSensingTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	InstanceData.SecondsUntilNextUpdate = SensingIntervalSeconds;
	InstanceData.ElapsedSinceLastUpdate = 0.0f;
	Boid->UpdateTargetSensing(0.0f);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FBoidSensingStateTreeTask::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid) || !Boid->IsAlive())
	{
		return EStateTreeRunStatus::Failed;
	}

	FBoidSensingTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	const float SafeDeltaTime = FMath::IsFinite(DeltaTime) ? FMath::Max(0.0f, DeltaTime) : 0.0f;
	InstanceData.ElapsedSinceLastUpdate += SafeDeltaTime;
	InstanceData.SecondsUntilNextUpdate -= SafeDeltaTime;
	if (InstanceData.SecondsUntilNextUpdate <= 0.0f)
	{
		const float ElapsedSeconds = InstanceData.ElapsedSinceLastUpdate;
		InstanceData.ElapsedSinceLastUpdate = 0.0f;
		// A long frame consumes one observation, not a burst of catch-up traces.
		InstanceData.SecondsUntilNextUpdate = SensingIntervalSeconds;
		Boid->UpdateTargetSensing(ElapsedSeconds);
	}
	return EStateTreeRunStatus::Running;
}
