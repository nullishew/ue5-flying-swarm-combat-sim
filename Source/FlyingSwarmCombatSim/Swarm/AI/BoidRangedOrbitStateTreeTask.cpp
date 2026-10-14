#include "Swarm/AI/BoidRangedOrbitStateTreeTask.h"
#include "Swarm/Steering/BoidSteeringBehavior.h"
#include "Swarm/Steering/BoidSteeringProfile.h"
#include "Swarm/Steering/BoidPathFollowingSteeringBehavior.h"
#include "Swarm/Steering/BoidOrbitSteeringBehavior.h"

FBoidMovementIntent FBoidRangedOrbitStateTreeTask::MakeIntent(const FBoidTargetSteeringTaskInstanceData& Data) const
{
	FBoidMovementIntent Intent;
	Intent.Policy = EBoidMovementPolicy::RangedOrbit;
	Intent.ArrivalRadiusCm = FMath::Max(0.f, static_cast<const FBoidRangedOrbitTaskInstanceData&>(Data).ArrivalRadiusCm);
	Intent.RangedPhaseControl = static_cast<const FBoidRangedOrbitTaskInstanceData&>(Data).PhaseControl;
	return Intent;
}

bool FBoidRangedOrbitStateTreeTask::SupportsProfile(const UBoidSteeringProfile* Profile) const
{
	if (!IsValid(Profile)) return false;
	FString Reason;
	const bool bValid = Profile->ValidateCombatProfile(Reason);
	if (!bValid)
	{
		static TSet<FString> WarnedConfigurations;
		const FString Key = Profile->GetPathName() + Reason;
		if (!WarnedConfigurations.Contains(Key))
		{ WarnedConfigurations.Add(Key); UE_LOG(LogTemp, Warning, TEXT("%s: %s"), *Profile->GetPathName(), *Reason); }
	}
	return bValid;
}
