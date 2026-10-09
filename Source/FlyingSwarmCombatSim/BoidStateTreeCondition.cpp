#include "BoidStateTreeCondition.h"

#include "FlyingBoid.h"
#include "StateTreeExecutionContext.h"

bool FBoidStateTreeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FBoidStateTreeConditionInstanceData& InstanceData = Context.GetInstanceData(*this);
	const AFlyingBoid* Boid = Cast<AFlyingBoid>(Context.GetOwner());
	if (!IsValid(Boid))
	{
		return false;
	}

	bool bResult = false;
	if (InstanceData.Fact == EBoidStateTreeFact::Dead)
	{
		bResult = !Boid->IsAlive();
	}
	else
	{
		const FBoidTargetFacts& Facts = Boid->GetTargetFacts();
		switch (InstanceData.Fact)
		{
		case EBoidStateTreeFact::Detected:
			bResult = Facts.bDetected;
			break;
		case EBoidStateTreeFact::HasSight:
			bResult = Facts.bHasSight;
			break;
		case EBoidStateTreeFact::AttackReady:
			bResult = Facts.bAttackReady;
			break;
		case EBoidStateTreeFact::Dead:
			break;
		}
	}

	return InstanceData.bInvert ? !bResult : bResult;
}
