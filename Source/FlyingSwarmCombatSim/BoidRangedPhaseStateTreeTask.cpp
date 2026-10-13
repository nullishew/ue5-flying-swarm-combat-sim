#include "BoidRangedPhaseStateTreeTask.h"
#include "FlyingBoid.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FBoidRangedPhaseStateTreeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	auto* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid)) return EStateTreeRunStatus::Failed;
	auto& Data = Context.GetInstanceData(*this);
	Data.OwnerToken = Boid->AcquireIntentToken(); Data.CombatToken = Boid->GetMovementIntentOwner();
	if (!Boid->RequestRangedPhase(Data.Phase, Data.OwnerToken, Data.CombatToken))
	{
		UE_LOG(LogTemp, Warning, TEXT("Request Ranged Phase requires a Combat parent with StateTree phase control."));
		return EStateTreeRunStatus::Failed;
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FBoidRangedPhaseStateTreeTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	const auto* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	const auto& Data = Context.GetInstanceData(*this);
	return IsValid(Boid) && Boid->IsAlive() && Boid->GetRangedPhaseOwner() == Data.OwnerToken
		&& Boid->GetMovementIntentOwner() == Data.CombatToken
		&& Boid->GetMovementIntent().Policy == EBoidMovementPolicy::RangedOrbit
		&& Boid->GetMovementIntent().RangedPhaseControl == EBoidRangedPhaseControl::StateTree
		? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

void FBoidRangedPhaseStateTreeTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (auto* Boid = Cast<AFlyingBoid>(Context.GetOwner())) Boid->ReleaseRangedPhase(Context.GetInstanceData(*this).OwnerToken);
}
