#include "BoidFollowStateTreeTask.h"
#include "BoidSteeringBehavior.h"
#include "BoidSteeringProfile.h"

FBoidMovementIntent FBoidFollowStateTreeTask::MakeIntent(const FBoidTargetSteeringTaskInstanceData& Data) const
{
	const FBoidFollowTaskInstanceData& Follow = static_cast<const FBoidFollowTaskInstanceData&>(Data);
	FBoidMovementIntent Intent;
	Intent.Policy = EBoidMovementPolicy::FollowStandOff;
	Intent.StandOffDistanceCm = FMath::Max(0.f, Follow.StandOffDistanceCm);
	Intent.MinimumTargetDistanceCm = FMath::Max(0.f, Follow.MinimumTargetDistanceCm);
	Intent.ArrivalRadiusCm = FMath::Max(0.f, Follow.ArrivalRadiusCm);
	return Intent;
}

bool FBoidFollowStateTreeTask::SupportsProfile(const UBoidSteeringProfile* Profile) const
{
	if (!IsValid(Profile)) return false;
	for (const FBoidSteeringBehaviorEntry& Entry : Profile->Behaviors)
	{
		if (IsValid(Entry.Behavior) && Entry.Behavior->IsA<UBoidSeekSteeringBehavior>()) return true;
	}
	return false;
}
