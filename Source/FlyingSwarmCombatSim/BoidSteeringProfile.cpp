#include "BoidSteeringProfile.h"
#include "BoidSteeringBehavior.h"
#include "BoidOrbitSteeringBehavior.h"
#include "BoidPathFollowingSteeringBehavior.h"

bool UBoidSteeringProfile::ValidateCombatProfile(FString& OutReason) const
{
	int32 Approach = 0, Orbit = 0;
	TSet<FName> Identifiers;
	for (const FBoidSteeringBehaviorEntry& Entry : Behaviors)
	{
		if (!Entry.EntryId.IsNone() && Identifiers.Contains(Entry.EntryId))
		{ OutReason = TEXT("Combat profile has duplicate Entry IDs."); return false; }
		Identifiers.Add(Entry.EntryId);
		const UBoidSteeringBehavior* Behavior = Entry.Behavior;
		if (!IsValid(Behavior)) continue;
		if (!FMath::IsFinite(Behavior->Weight) || Behavior->Weight < 0.f)
		{ OutReason = TEXT("Combat profile contains an invalid behavior weight."); return false; }
		if (Behavior->Weight <= 0.f) continue;
		if (Behavior->IsA<UBoidPathFollowingSteeringBehavior>() && Behavior->SupportsNavigationPhase(EBoidNavigationPhase::Approach)
			&& Behavior->IsPrimaryMovementForPhase(EBoidNavigationPhase::Approach)) ++Approach;
		if (Behavior->IsA<UBoidOrbitSteeringBehavior>() && Behavior->SupportsNavigationPhase(EBoidNavigationPhase::Orbit)
			&& Behavior->IsPrimaryMovementForPhase(EBoidNavigationPhase::Orbit)) ++Orbit;
	}
	if (Approach != 1 || Orbit != 1)
	{
		OutReason = FString::Printf(TEXT("Combat profile requires exactly one positive primary Path Following and native Orbit entry (found %d approach, %d orbit)."), Approach, Orbit);
		return false;
	}
	OutReason.Reset();
	return true;
}
