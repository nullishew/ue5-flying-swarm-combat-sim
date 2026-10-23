#include "Swarm/Steering/BoidSteeringProfile.h"
#include "Swarm/Steering/BoidSteeringBehavior.h"
#include "Swarm/Steering/BoidOrbitSteeringBehavior.h"
#include "Swarm/Steering/BoidPathFollowingSteeringBehavior.h"
#include "Swarm/Steering/BoidLocalSteeringBehaviors.h"
#include "Serialization/CustomVersion.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	// Recognize the package identifier already saved in authored assets. No version-based conversion or evaluation remains.
	FCustomVersionRegistration ProfileAssetVersion(FGuid(0x833E4A14, 0xB8964A01, 0x984B4DD2, 0xBF97A387), 1, TEXT("BoidLocalSteering"));
}

UBoidSteeringProfile::UBoidSteeringProfile()
{
	// Preserve the last authored profile-template defaults so delta-serialized assets do not silently inherit new values.
	UBoidSeparationSteeringBehavior* Separation = CreateDefaultSubobject<UBoidSeparationSteeringBehavior>(TEXT("DefaultSeparation"));
	Separation->SeparationDistanceCm = 500.f;
	UBoidCohesionSteeringBehavior* Cohesion = CreateDefaultSubobject<UBoidCohesionSteeringBehavior>(TEXT("DefaultCohesion"));
	Cohesion->MinimumNeighborDistanceCm = 500.f;
	UBoidObstacleAvoidanceSteeringBehavior* Avoidance = CreateDefaultSubobject<UBoidObstacleAvoidanceSteeringBehavior>(TEXT("DefaultObstacleAvoidance"));
	Avoidance->NeighborRadiusCm = 500.f;
	Behaviors.Add({TEXT("Separation"), Separation});
	Behaviors.Add({TEXT("Alignment"), CreateDefaultSubobject<UBoidAlignmentSteeringBehavior>(TEXT("DefaultAlignment"))});
	Behaviors.Add({TEXT("Cohesion"), Cohesion});
	Behaviors.Add({TEXT("ObstacleAvoidance"), Avoidance});
}

#if WITH_EDITOR
EDataValidationResult UBoidSteeringProfile::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	FString Reason;
	if (!ValidateProfile(Reason)) { Context.AddError(FText::FromString(Reason)); return EDataValidationResult::Invalid; }
	return Parent == EDataValidationResult::Invalid ? Parent : EDataValidationResult::Valid;
}
#endif

bool UBoidSteeringProfile::ValidateProfile(FString& OutReason) const
{
	OutReason.Reset();
	TSet<FName> Identifiers;
	int32 Separation = 0, Alignment = 0, Cohesion = 0, Avoidance = 0;
	for (const FBoidSteeringBehaviorEntry& Entry : Behaviors)
	{
		if (!Entry.EntryId.IsNone() && Identifiers.Contains(Entry.EntryId)) { OutReason = TEXT("Profile has duplicate Entry IDs."); return false; }
		if (!Entry.EntryId.IsNone()) Identifiers.Add(Entry.EntryId);
		const UBoidSteeringBehavior* Behavior = Entry.Behavior;
		if (!IsValid(Behavior)) continue;
		if (Behavior->IsLocalSteeringBehavior() && Entry.EntryId.IsNone()) { OutReason = TEXT("Local steering entries require stable, nonempty Entry IDs."); return false; }
		if (!Behavior->ValidateSettings(OutReason)) return false;
		Separation += Behavior->IsA<UBoidSeparationSteeringBehavior>() ? 1 : 0;
		Alignment += Behavior->IsA<UBoidAlignmentSteeringBehavior>() ? 1 : 0;
		Cohesion += Behavior->IsA<UBoidCohesionSteeringBehavior>() ? 1 : 0;
		Avoidance += Behavior->IsA<UBoidObstacleAvoidanceSteeringBehavior>() ? 1 : 0;
	}
	if (Separation > 1 || Alignment > 1 || Cohesion > 1 || Avoidance > 1) { OutReason = TEXT("A profile permits at most one Separation, Alignment, Cohesion and Obstacle Avoidance entry."); return false; }
	return true;
}

bool UBoidSteeringProfile::ValidateCombatProfile(FString& OutReason) const
{
	if (!ValidateProfile(OutReason)) return false;
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
