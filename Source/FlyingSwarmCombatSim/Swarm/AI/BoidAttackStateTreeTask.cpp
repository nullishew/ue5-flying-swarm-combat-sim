#include "Swarm/AI/BoidAttackStateTreeTask.h"

#include "Combat/BoidAttackComponent.h"
#include "Swarm/FlyingBoid.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FBoidAttackStateTreeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	auto& Data = Context.GetInstanceData(*this);
	Data.ContextToken = 0;
	AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (IsValid(Boid) && Boid->IsAlive() && IsValid(Boid->AttackComponent))
	{
		const uint64 Token = Boid->AcquireIntentToken();
		if (Boid->AttackComponent->ActivateCombatContext(Token))
		{
			Data.ContextToken = Token;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Run Boid Combat Attacks already has an owner on %s; use one task on the Combat parent."), *Boid->GetPathName());
		}
	}
	// Firing eligibility and cadence belong to the component. Never fail or
	// complete Combat because a shot is unavailable or configuration is missing.
	return EStateTreeRunStatus::Running;
}

void FBoidAttackStateTreeTask::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	auto& Data = Context.GetInstanceData(*this);
	if (Data.ContextToken)
	{
		if (AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner()); IsValid(Boid) && IsValid(Boid->AttackComponent))
		{
			Boid->AttackComponent->DeactivateCombatContext(Data.ContextToken);
		}
		Data.ContextToken = 0;
	}
}
