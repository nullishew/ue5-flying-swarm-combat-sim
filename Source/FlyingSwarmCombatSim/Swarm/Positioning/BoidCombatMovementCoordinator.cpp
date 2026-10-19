#include "Swarm/Positioning/BoidCombatMovementCoordinator.h"

#include "Swarm/Steering/BoidOrbitSteeringBehavior.h"
#include "Swarm/Positioning/BoidRangedPositionPlanner.h"
#include "Swarm/Routing/BoidFlightRouteService.h"
#include "Swarm/Positioning/BoidRangedPositioning.h"
#include "Swarm/Steering/BoidSteeringBehavior.h"
#include "Swarm/Steering/BoidSteeringProfile.h"
#include "Swarm/FlyingBoid.h"
#include "Player/FlyingCharacter.h"
#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

namespace
{
	constexpr float OrbitEntryStableSeconds = 0.1f;
	constexpr float SelectionRebaseDistanceCm = 50.f;
	constexpr float MinimumEscapeMarginCm = 50.f;
	constexpr int32 MaximumPredictionSegments = 8;
	constexpr float MaximumPredictionAngleDeg = 5.f;
	constexpr float MaximumPredictionSegmentCm = 100.f;

	float ActiveMinimumRange(const FBoidRangedSettings& Settings, const FBoidRangedSelection& Selection)
	{
		return Selection.bUsingAdaptiveRange && Settings.bAllowAdaptiveRange
			? Settings.AdaptiveMinimumRangeCm : Settings.MinimumRangeCm;
	}

	float ActiveMaximumRange(const FBoidRangedSettings& Settings, const FBoidRangedSelection& Selection)
	{
		return Selection.bUsingAdaptiveRange && Settings.bAllowAdaptiveRange
			? Settings.MinimumRangeCm : Settings.MaximumRangeCm;
	}

	float RetreatSpeed(const USwarmSimulationSubsystem& Runner, const AFlyingBoid& Boid)
	{
		return FMath::Min(FMath::Max(0.f, Runner.Tuning.CruiseSpeedCmPerSec), FMath::Max(0.f, Boid.MaxSpeedCmPerSec));
	}

	FVector SafeDirection(const FVector& Direction, const FVector& Fallback)
	{
		const FVector Unit = Direction.GetSafeNormal();
		return Unit.IsNearlyZero() ? Fallback.GetSafeNormal() : Unit;
	}

	FVector EffectiveOrbitOffset(const FBoidRangedSettings& Settings, const FBoidRangedSelection& Selection,
		const FVector& Center, float ArrivalRadiusCm)
	{
		const FVector Axis = SafeDirection(Selection.Axis, FVector::UpVector);
		const FVector RawOffset = Selection.Offset.IsNearlyZero() ? Selection.Destination - Center : Selection.Offset;
		FVector Radial = FVector::VectorPlaneProject(RawOffset, Axis);
		const float MinimumRange = ActiveMinimumRange(Settings, Selection);
		const float MaximumRange = ActiveMaximumRange(Settings, Selection);
		const float Height = FVector::DotProduct(RawOffset, Axis);
		const float MinimumRadial = FMath::Sqrt(FMath::Max(0.f, FMath::Square(MinimumRange) - FMath::Square(Height)));
		const float MaximumRadial = FMath::Sqrt(FMath::Max(0.f, FMath::Square(MaximumRange) - FMath::Square(Height)));
		const float Margin = FMath::Min(FMath::Max(0.f, ArrivalRadiusCm),
			FMath::Max(0.f, MaximumRadial - MinimumRadial) * 0.25f);
		const float Radius = FMath::Clamp(Radial.Size(), MinimumRadial + Margin, MaximumRadial - Margin);
		Radial = SafeDirection(Radial, FVector::VectorPlaneProject(FVector::ForwardVector, Axis)) * Radius;
		return Radial + Axis * FVector::DotProduct(RawOffset, Axis);
	}

}

bool FBoidCombatMovementCoordinator::HasBudget(const USwarmSimulationSubsystem& Runner) const
{
	return Runner.LastFrameCombatControlQueries < FMath::Max(0, Runner.CombatControlQueriesPerFrame);
}

bool FBoidCombatMovementCoordinator::HasMovementBudget(const USwarmSimulationSubsystem& Runner) const
{
	return Runner.LastFrameCombatControlQueries < FMath::Max(0, Runner.CombatControlQueriesPerFrame - ReadinessReserve);
}

bool FBoidCombatMovementCoordinator::SweepClear(USwarmSimulationSubsystem& Runner, AFlyingBoid& Boid,
	AActor& Target, const FVector& Start, const FVector& End)
{
	const USphereComponent* Sphere = Boid.CollisionSphere;
	if (!Sphere || Start.ContainsNaN() || End.ContainsNaN()) return false;
	const float Radius = Sphere->GetScaledSphereRadius() + FMath::Max(0.f, Runner.Tuning.ObstacleMarginCm);
	if (!Runner.IsInsideSafeBounds(Start, Radius) || !Runner.IsInsideSafeBounds(End, Radius)
		|| !Runner.IsAllowedFlightSegment(Start, End, Radius)) return false;
	if (!HasMovementBudget(Runner)) return false;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BoidCombatControl), false);
	Params.AddIgnoredActor(&Boid);
	Params.AddIgnoredActor(&Target);
	Params.bFindInitialOverlaps = true;
	const FCollisionResponseParams Responses(Sphere->GetCollisionResponseToChannels());
	++Runner.LastFrameCombatControlQueries;
	return !Runner.GetWorld()->SweepTestByChannel(Start, End, FQuat::Identity, Sphere->GetCollisionObjectType(),
		FCollisionShape::MakeSphere(Radius), Params, Responses);
}

bool FBoidCombatMovementCoordinator::FindSafeEscapeGoal(USwarmSimulationSubsystem& Runner, AFlyingBoid& Boid,
	AActor& Target, const FVector& Center, float EscapeRange, TConstArrayView<FVector> Directions,
	FVector& OutGoal, FVector& OutDirection, bool& bPendingBudget, int32& OutRejectedDirections)
{
	for (const FVector& Direction : Directions)
	{
		const FVector Goal = Center + Direction * EscapeRange;
		if (!HasMovementBudget(Runner))
		{
			bPendingBudget = true;
			return false;
		}
		if (SweepClear(Runner, Boid, Target, Boid.GetActorLocation(), Goal))
		{
			OutGoal = Goal;
			OutDirection = Direction;
			return true;
		}
		++OutRejectedDirections;
	}
	return false;
}

void FBoidCombatMovementCoordinator::RecordChange(FAgentState& State, const FString& Message)
{
	if (Message.IsEmpty() || State.Facts.Detail == Message) return;
	State.Facts.Detail = Message;
	if (!State.bTrackHistory) { State.Facts.RecentChanges.Reset(); return; }
	State.Facts.RecentChanges.Add(FString::Printf(TEXT("%.2f: %s"), FPlatformTime::Seconds(), *Message));
	while (State.Facts.RecentChanges.Num() > 16) State.Facts.RecentChanges.RemoveAt(0, 1, EAllowShrinking::No);
}

void FBoidCombatMovementCoordinator::Publish(USwarmSimulationSubsystem& Runner, AFlyingBoid& Boid,
	FAgentState& State, const FBoidRangedSelection* Selection, bool bReady)
{
	FBoidPositioningFacts Positioning;
	const FBoidRangedSettings Defaults = Runner.RangedSettings.Sanitized();
	Positioning.EffectiveMinimumRangeCm = Defaults.MinimumRangeCm;
	Positioning.EffectivePreferredRangeCm = Defaults.PreferredRangeCm;
	Positioning.EffectiveMaximumRangeCm = Defaults.MaximumRangeCm;
	if (Selection && Selection->bHasPosition && IsValid(Boid.MovementTarget))
	{
		const FBoidRangedSettings Settings = Runner.RangedSettings.Sanitized();
		Positioning.bHasPosition = true;
		Positioning.bOrbiting = State.Facts.bOrbiting;
		Positioning.bCanOrbit = State.Facts.bCanOrbit;
		Positioning.bUsingAdaptiveRange = Selection->bUsingAdaptiveRange;
		Positioning.EffectiveMinimumRangeCm = ActiveMinimumRange(Settings, *Selection);
		Positioning.EffectiveMaximumRangeCm = ActiveMaximumRange(Settings, *Selection);
        Positioning.EffectivePreferredRangeCm = FMath::Clamp(Selection->Offset.Size(),
            Positioning.EffectiveMinimumRangeCm, Positioning.EffectiveMaximumRangeCm);
		Positioning.Destination = Selection->Destination;
		Positioning.Score = Selection->Score;
		Positioning.Openness = Selection->Open;
		Positioning.PreferredDirection = Selection->PreferredDirection;
		Positioning.SeedAffinity = Selection->SeedAffinity;
		Positioning.OrbitDirection = State.Orbit.LastDirection;
		Positioning.LastSearchSeconds = Selection->LastSearchSeconds;
	}
	Boid.PublishPositioning(Positioning, bReady);
	State.Facts.ActualSpeedCmPerSec = Boid.GetFlightVelocity().Size();
	State.Facts.bAttackReady = Boid.GetTargetFacts().bAttackReady;
	State.Facts.HoldSeconds = State.HoldSeconds;
	State.Facts.ControlQueriesUsed = Runner.LastFrameCombatControlQueries;
	Boid.PublishCombatMovement(State.Facts);
}

void FBoidCombatMovementCoordinator::WarnIfHeld(const AFlyingBoid& Boid, FAgentState& State,
	bool bHeld, float DeltaTime)
{
	if (!bHeld)
	{
		State.HoldSeconds = 0.f;
		State.bWarnedHold = false;
		return;
	}
	State.HoldSeconds += FMath::Max(0.f, DeltaTime);
	if (!State.bWarnedHold && State.HoldSeconds >= 2.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Boid combat movement held: %s (%s) reason=%d status=%d"),
			*Boid.GetPathName(), *State.Facts.Detail, static_cast<int32>(State.Facts.HoldReason),
			static_cast<int32>(State.Facts.Status));
		State.bWarnedHold = true;
	}
}

void FBoidCombatMovementCoordinator::Update(USwarmSimulationSubsystem& Runner,
	TConstArrayView<FBoidPlannerAgent> Agents, FBoidRangedPositionPlanner& Planner, float DeltaTime)
{
	Runner.LastFrameCombatControlQueries = 0;
	const FBoidRangedSettings Settings = Runner.RangedSettings.Sanitized();
	ReadinessReserve = FMath::Min(Agents.Num(), FMath::Min(16,
		FMath::Max(0, Runner.CombatControlQueriesPerFrame / 4)));
	TArray<int32> Order;
	Order.Reserve(Agents.Num());
	for (int32 Index = 0; Index < Agents.Num(); ++Index) Order.Add(Index);
	if (!Order.IsEmpty())
	{
		const int32 Start = FMath::Abs(FairCursor) % Order.Num();
		TArray<int32> Rotated;
		Rotated.Reserve(Order.Num());
		for (int32 Offset = 0; Offset < Order.Num(); ++Offset) Rotated.Add((Start + Offset) % Order.Num());
		Order = MoveTemp(Rotated);
		FairCursor = (FairCursor + 1) % Order.Num();
	}

	TArray<int32> OrbitCandidates;
	OrbitCandidates.Reserve(Agents.Num());
	TMap<int32, UBoidOrbitSteeringBehavior*> OrbitBehaviors;
	TMap<int32, float> OrbitSpeedScales;
	for (const int32 AgentIndex : Order)
	{
		const FBoidPlannerAgent& Agent = Agents[AgentIndex];
		AFlyingBoid* Boid = Agent.Boid;
		if (!IsValid(Boid)) continue;
		FAgentState& State = RuntimeByAgent.FindOrAdd(Agent.Id);
		State.bTrackHistory = Boid == (IsValid(Runner.DebugBoid) ? Runner.DebugBoid.Get() : Runner.TestBoid.Get());
		const FBoidMovementIntent Intent = Boid->GetMovementIntent();
		if (Intent.Policy != EBoidMovementPolicy::RangedOrbit)
		{
			State.Navigation = FBoidNavigationGoal();
			State.Navigation.bHoldPosition = true;
			continue;
		}
		AActor* Target = Boid->MovementTarget;
		const FBoidTargetFacts TargetFacts = Boid->GetTargetFacts();
		const FBoidRangedSelection* Selection = Planner.FindSelection(Agent.Id);
		const bool bTargetChanged = State.Target.Get() != Target;
		State.Orbit.bWasOrbiting = State.Navigation.Phase == EBoidNavigationPhase::Orbit;
		const uint64 OwnerToken = Boid->GetMovementIntentOwner();
		const uint32 IntentRevision = Selection ? Selection->IntentRevision : 0;
		const bool bIntentChanged = OwnerToken != State.LastOwnerToken || IntentRevision != State.LastIntentRevision;
		if (bTargetChanged || bIntentChanged)
		{
			State.Orbit.StableSeconds = 0.f;
			State.Orbit.bWasOrbiting = false;
			State.Orbit.bHasClearProof = false;
			State.Orbit.ClearProofAgeSeconds = 0.f;
			State.Recovery.bActive = false;
			State.Readiness.ElapsedSeconds = 0.f;
		}
		State.LastOwnerToken = OwnerToken;
		State.LastIntentRevision = IntentRevision;
		State.Facts.RequestedPhase = Boid->GetRequestedRangedPhase();
		State.Facts.PhaseControl = Intent.RangedPhaseControl;
		State.Facts.bCanOrbit = false;
		State.Facts.bOrbiting = false;
		State.Facts.bRetreating = false;
		State.Facts.bReadinessEligible = false;
		State.Facts.Status = EBoidCombatControlStatus::NeedsApproach;
		State.Facts.HoldReason = EBoidCombatHoldReason::None;
		State.Facts.Constraint = EBoidCombatMovementConstraint::None;
		State.Facts.DesiredSpeedCmPerSec = 0.f;
		State.Facts.PredictionSegments = 0;
		State.Facts.ControlQueriesUsed = Runner.LastFrameCombatControlQueries;
		State.Facts.bHasSelection = Selection && Selection->bHasPosition;
		State.Facts.SelectionVersion = Selection ? Selection->Version : 0;
		State.Facts.CandidateCount = Selection ? Selection->CandidateCount : 0;
		State.Facts.CandidatesEvaluated = Selection ? Selection->CandidatesEvaluated : 0;
		State.Facts.Rebases = Selection ? Selection->Rebases : 0;
		State.Facts.Rejections = Selection ? Selection->Rejections : 0;
		State.Facts.StaleResults = Selection ? Selection->StaleResults : 0;
		State.Facts.SearchAgeSeconds = Selection ? Selection->SearchAgeSeconds : 0.f;
		State.Facts.TargetRangeCm = IsValid(Target) ? FVector::Distance(Boid->GetActorLocation(), Target->GetActorLocation()) : 0.f;
		State.Facts.MinimumRangeCm = Selection ? ActiveMinimumRange(Settings, *Selection) : Settings.MinimumRangeCm;
		State.Facts.MaximumRangeCm = Selection ? ActiveMaximumRange(Settings, *Selection) : Settings.MaximumRangeCm;
		State.Facts.DesiredRangeCm = Selection ? EffectiveOrbitOffset(Settings, *Selection,
			IsValid(Target) ? Target->GetActorLocation() : Selection->Destination, Boid->GetMovementIntent().ArrivalRadiusCm).Size()
			: Settings.PreferredRangeCm;
		State.Facts.RangeErrorCm = State.Facts.TargetRangeCm - State.Facts.DesiredRangeCm;
		State.Facts.SelectionReason = Selection ? Selection->FailureReason : TEXT("No current ranged selection.");
		State.Facts.EffectiveProfile.Reset();
		State.Facts.BlueprintClass = Boid->GetClass()->GetPathName();
		State.Facts.StateTreeAsset = Boid->GetStateTreeAssetPath();
		State.Facts.RouteReason = Boid->GetNavigationFacts().FailureReason;
		State.Facts.MovementReason = Intent.RangedPhaseControl == EBoidRangedPhaseControl::Automatic
			? TEXT("Ranged phase control is automatic.") : TEXT("Ranged phase control is StateTree-owned.");

		if (!IsValid(Target) || !TargetFacts.bDetected || TargetFacts.TargetActor != Target
			|| (Cast<AFlyingCharacter>(Target) && !Cast<AFlyingCharacter>(Target)->IsAlive())
			|| (Cast<AFlyingBoid>(Target) && !Cast<AFlyingBoid>(Target)->IsAlive()))
		{
			State.Target = nullptr;
			State.Navigation = FBoidNavigationGoal();
			State.Navigation.bHoldPosition = true;
			State.Facts.HoldReason = EBoidCombatHoldReason::InvalidTarget;
			State.Facts.Status = EBoidCombatControlStatus::Blocked;
			State.Facts.OrbitReason = TEXT("The active target is missing, dead, or no longer detected.");
			RecordChange(State, State.Facts.OrbitReason);
			Publish(Runner, *Boid, State, nullptr, false);
			continue;
		}
		State.Target = Target;
		if (!Boid->IsAlive() || !Boid->IsSteeringActive())
		{
			State.Navigation = FBoidNavigationGoal();
			State.Navigation.bHoldPosition = true;
			State.Facts.HoldReason = EBoidCombatHoldReason::SteeringDisabled;
			State.Facts.Status = EBoidCombatControlStatus::Blocked;
			State.Facts.OrbitReason = TEXT("Steering is inactive.");
			RecordChange(State, State.Facts.OrbitReason);
			Publish(Runner, *Boid, State, Selection, false);
			continue;
		}
		if (Intent.Policy != EBoidMovementPolicy::RangedOrbit
			|| (Intent.RangedPhaseControl == EBoidRangedPhaseControl::StateTree && Boid->GetMovementIntentOwner() == 0)
			|| (Boid->GetMovementIntentOwner() != 0 && Boid->GetMovementTargetOwner() != Boid->GetMovementIntentOwner()))
		{
			State.Navigation = FBoidNavigationGoal();
			State.Navigation.bHoldPosition = true;
			State.Facts.HoldReason = EBoidCombatHoldReason::InvalidOwnership;
			State.Facts.Status = EBoidCombatControlStatus::Blocked;
			State.Facts.OrbitReason = TEXT("A Combat owner with a RangedOrbit intent is required.");
			RecordChange(State, State.Facts.OrbitReason);
			Publish(Runner, *Boid, State, Selection, false);
			continue;
		}

		UBoidSteeringProfile* Profile = Runner.ResolveSteeringProfile(Boid);
		// A proof belongs to one configuration, not just one selected position.
		uint32 Signature = PointerHash(Profile);
		for (const float Value : { Settings.MinimumRangeCm, Settings.PreferredRangeCm, Settings.MaximumRangeCm, Settings.AdaptiveMinimumRangeCm,
			Settings.AngularSpeedDegPerSec, Settings.OrbitLeadSeconds, Intent.ArrivalRadiusCm,
			Runner.Tuning.CruiseSpeedCmPerSec, Runner.Tuning.ObstacleMarginCm, Runner.Tuning.BoundsMarginCm,
			Boid->MaxSpeedCmPerSec, Boid->CollisionSphere->GetScaledSphereRadius() })
			Signature = HashCombineFast(Signature, GetTypeHash(Value));
		Signature = HashCombineFast(Signature, GetTypeHash(Settings.bAllowAdaptiveRange));
		Signature = HashCombineFast(Signature, Runner.GetRouteService().GetGeneration());
		Signature = HashCombineFast(Signature, GetTypeHash(Runner.WanderBounds->GetComponentLocation()));
		Signature = HashCombineFast(Signature, GetTypeHash(Runner.WanderBounds->GetScaledBoxExtent()));
		const FRotator BoundsRotation = Runner.WanderBounds->GetComponentRotation();
		Signature = HashCombineFast(Signature, GetTypeHash(BoundsRotation.Pitch));
		Signature = HashCombineFast(Signature, GetTypeHash(BoundsRotation.Yaw));
		Signature = HashCombineFast(Signature, GetTypeHash(BoundsRotation.Roll));
		if (State.ControlSignature != Signature)
		{
			const bool bWasOrbiting = State.Orbit.bWasOrbiting;
			const int32 Direction = State.Orbit.LastDirection;
			const int32 SelectionVersion = State.Orbit.SelectionVersion;
			State.Orbit = FAgentState::FOrbitControlState();
			State.Orbit.bWasOrbiting = bWasOrbiting;
			State.Orbit.LastDirection = Direction;
			State.Orbit.SelectionVersion = SelectionVersion;
			State.Recovery = FAgentState::FRangeRecoveryState();
			State.Readiness = FAgentState::FReadinessState();
			State.ControlSignature = Signature;
		}

		State.Facts.EffectiveProfile = IsValid(Profile) ? Profile->GetPathName() : TEXT("<none>");
		if (Profile) Boid->PrepareSteeringRuntime(Profile);
		UBoidOrbitSteeringBehavior* Orbit = nullptr;
		float PathFollowingWeightScale = 0.f;
		if (Profile)
		{
			FString ProfileReason;
			if (!Profile->ValidateCombatProfile(ProfileReason))
			{
				bool bCanEmergencyRetreat = !Selection && State.Facts.TargetRangeCm < Settings.MinimumRangeCm;
				if (bCanEmergencyRetreat)
				{
					bCanEmergencyRetreat = false;
					for (int32 BehaviorIndex = 0; BehaviorIndex < Profile->Behaviors.Num(); ++BehaviorIndex)
					{
						const FBoidSteeringBehaviorEntry& Entry = Profile->Behaviors[BehaviorIndex];
						if (IsValid(Entry.Behavior) && Entry.Behavior->Weight > 0.f
							&& Entry.Behavior->SupportsNavigationPhase(EBoidNavigationPhase::Retreat)
							&& Entry.Behavior->IsPrimaryMovementForPhase(EBoidNavigationPhase::Retreat)
							&& Boid->GetSteeringRuntimeState(BehaviorIndex).WeightScale > UE_SMALL_NUMBER)
						{
							bCanEmergencyRetreat = true;
							break;
						}
					}
				}
				if (!bCanEmergencyRetreat)
				{
					State.Navigation = FBoidNavigationGoal();
					State.Navigation.bHoldPosition = true;
					State.Facts.HoldReason = EBoidCombatHoldReason::InvalidProfile;
					State.Facts.Status = EBoidCombatControlStatus::Blocked;
					State.Facts.OrbitReason = ProfileReason;
					RecordChange(State, ProfileReason);
					Publish(Runner, *Boid, State, Selection, false);
					continue;
				}
			}
			for (int32 BehaviorIndex = 0; BehaviorIndex < Profile->Behaviors.Num(); ++BehaviorIndex)
			{
				const FBoidSteeringBehaviorEntry& Entry = Profile->Behaviors[BehaviorIndex];
				if (!IsValid(Entry.Behavior) || Entry.Behavior->Weight <= 0.f) continue;
				if (Entry.Behavior->SupportsNavigationPhase(EBoidNavigationPhase::Approach)
					&& Entry.Behavior->IsPrimaryMovementForPhase(EBoidNavigationPhase::Approach))
					PathFollowingWeightScale = Boid->GetSteeringRuntimeState(BehaviorIndex).WeightScale;
				if (auto* Candidate = Cast<UBoidOrbitSteeringBehavior>(Entry.Behavior))
					if (Candidate->SupportsNavigationPhase(EBoidNavigationPhase::Orbit)
						&& Candidate->IsPrimaryMovementForPhase(EBoidNavigationPhase::Orbit))
					{
						Orbit = Candidate;
						OrbitSpeedScales.Add(Agent.Id, Boid->GetSteeringRuntimeState(BehaviorIndex).WeightScale);
					}
			}
		}
		if (PathFollowingWeightScale <= UE_SMALL_NUMBER)
		{
			State.Navigation = FBoidNavigationGoal();
			State.Navigation.bHoldPosition = true;
			State.Facts.HoldReason = EBoidCombatHoldReason::InvalidProfile;
			State.Facts.Status = EBoidCombatControlStatus::Blocked;
			State.Facts.OrbitReason = TEXT("The per-boid primary path-following weight scale is zero or missing.");
			if (!WarnedConfiguration.Contains(State.Facts.EffectiveProfile + TEXT("|path")))
			{
				WarnedConfiguration.Add(State.Facts.EffectiveProfile + TEXT("|path"));
				UE_LOG(LogTemp, Warning, TEXT("Boid combat movement: profile %s has no effective per-boid primary path-following contribution."), *State.Facts.EffectiveProfile);
			}
			RecordChange(State, State.Facts.OrbitReason);
			Publish(Runner, *Boid, State, Selection, false);
			continue;
		}
		if (!Selection || !Selection->bHasPosition)
		{
			const float MinimumRange = Settings.MinimumRangeCm;
			const float MaximumRange = Settings.MaximumRangeCm;
			const float EscapeHysteresis = FMath::Min(MinimumEscapeMarginCm,
				FMath::Max(0.f, MaximumRange - MinimumRange) * 0.25f);
			bool bFoundEscape = false;
			bool bPendingBudget = Selection && Selection->FailureReason.Contains(TEXT("budget"), ESearchCase::IgnoreCase);
			if (State.Facts.TargetRangeCm < MinimumRange)
			{
				const FVector Center = Target->GetActorLocation();
				const FVector Away = SafeDirection(Boid->GetActorLocation() - Center, -Boid->GetFlightHeading());
				FVector Side = FVector::CrossProduct(Away, FVector::UpVector).GetSafeNormal();
				if (Side.IsNearlyZero()) Side = FVector::CrossProduct(Away, FVector::RightVector).GetSafeNormal();
				const float EscapeRange = FMath::Min(MaximumRange, MinimumRange + EscapeHysteresis
					+ FMath::Max(0.f, Intent.ArrivalRadiusCm));
				const TArray<FVector> EscapeDirections = { Away, SafeDirection(Away + Side * 0.5f, Away),
					SafeDirection(Away - Side * 0.5f, Away) };
				FVector EscapeGoal;
				FVector EscapeDirection;
				int32 RejectedDirections = 0;
				if (FindSafeEscapeGoal(Runner, *Boid, *Target, Center, EscapeRange, EscapeDirections,
					EscapeGoal, EscapeDirection, bPendingBudget, RejectedDirections))
				{
					State.Recovery.Goal = EscapeGoal;
					State.Recovery.Center = Center;
					State.Recovery.Direction = EscapeDirection;
					State.Recovery.bActive = true;
					State.Navigation = FBoidNavigationGoal();
					State.Navigation.Goal = EscapeGoal;
					State.Navigation.bHasGoal = true;
					State.Navigation.bContinuousGoal = true;
					State.Navigation.bPreferNavigationGoal = true;
					State.Navigation.Phase = EBoidNavigationPhase::Retreat;
					State.Navigation.DesiredSpeed = RetreatSpeed(Runner, *Boid);
					State.Facts.Goal = EscapeGoal;
					State.Facts.bRetreating = true;
					State.Facts.Status = EBoidCombatControlStatus::Ready;
					State.Facts.HoldReason = EBoidCombatHoldReason::None;
					State.Facts.Constraint = EBoidCombatMovementConstraint::EmergencySeparation;
					State.Facts.DesiredSpeedCmPerSec = State.Navigation.DesiredSpeed;
					State.Facts.ExecutedPhase = EBoidNavigationPhase::Retreat;
					State.Facts.OrbitReason = TEXT("Retreating to the nominal minimum range while selection is pending.");
					bFoundEscape = true;
				}
				State.Facts.Rejections += RejectedDirections;
			}
			if (!bFoundEscape && bPendingBudget && State.Recovery.bActive
				&& FVector::Distance(State.Recovery.Center, Target->GetActorLocation()) < SelectionRebaseDistanceCm)
			{
				State.Navigation.Goal = State.Recovery.Goal;
				State.Navigation.bHasGoal = true;
				State.Navigation.bPreferNavigationGoal = true;
				State.Navigation.Phase = EBoidNavigationPhase::Retreat;
				State.Navigation.DesiredSpeed = RetreatSpeed(Runner, *Boid);
				State.Facts.Goal = State.Recovery.Goal;
				State.Facts.bRetreating = true;
				State.Facts.Status = EBoidCombatControlStatus::Ready;
				State.Facts.HoldReason = EBoidCombatHoldReason::None;
				State.Facts.Constraint = EBoidCombatMovementConstraint::EmergencySeparation;
				State.Facts.DesiredSpeedCmPerSec = State.Navigation.DesiredSpeed;
				State.Facts.ExecutedPhase = EBoidNavigationPhase::Retreat;
			}
			else if (!bFoundEscape)
			{
				State.Recovery.bActive = false;
				State.Navigation = FBoidNavigationGoal();
				State.Navigation.bHoldPosition = true;
				State.Facts.HoldReason = bPendingBudget ? EBoidCombatHoldReason::PendingBudget
					: State.Facts.TargetRangeCm < MinimumRange ? EBoidCombatHoldReason::RetreatBlocked : EBoidCombatHoldReason::NoSelection;
				State.Facts.Status = bPendingBudget ? EBoidCombatControlStatus::PendingBudget
					: State.Facts.TargetRangeCm < MinimumRange ? EBoidCombatControlStatus::Blocked : EBoidCombatControlStatus::NeedsApproach;
				State.Facts.Constraint = EBoidCombatMovementConstraint::Hold;
				State.Facts.OrbitReason = bPendingBudget
					? TEXT("Waiting for a combat query to validate an emergency escape.")
					: TEXT("Waiting for a safe, live-validated ranged selection.");
			}
			RecordChange(State, State.Facts.OrbitReason);
			Publish(Runner, *Boid, State, nullptr, false);
			continue;
		}

		const FVector Center = Target->GetActorLocation();
		const FVector Offset = EffectiveOrbitOffset(Settings, *Selection, Center, Intent.ArrivalRadiusCm);
		const FVector Destination = Selection->Destination;
		Planner.SetReservation(Agent.Id, Destination);
		State.Facts.Goal = Destination;
		State.Facts.SelectionVersion = Selection->Version;
		State.Facts.OrbitRadiusCm = FVector::VectorPlaneProject(Offset, Selection->Axis).Size();
		const FVector Relative = Boid->GetActorLocation() - Center;
		State.Facts.RadialErrorCm = FMath::Abs(FVector::VectorPlaneProject(Relative, Selection->Axis).Size() - State.Facts.OrbitRadiusCm);
		State.Facts.HeightErrorCm = FMath::Abs(FVector::DotProduct(Relative - Offset, Selection->Axis));
		if (bTargetChanged || bIntentChanged || State.Orbit.SelectionVersion != Selection->Version)
		{
			State.Orbit.StableSeconds = 0.f;
			State.Orbit.bWasOrbiting = false;
			State.Orbit.bHasClearProof = false;
			State.Orbit.ClearProofAgeSeconds = 0.f;
			State.Recovery.bActive = false;
			State.Orbit.SelectionVersion = Selection->Version;
			State.Orbit.LastDirection = static_cast<int32>(BoidRanged::OrbitDirection(Selection->ChoiceSeed));
		}
		State.Target = Target;

		const float ActualRange = State.Facts.TargetRangeCm;
		const float MinimumRange = ActiveMinimumRange(Settings, *Selection);
		const float MaximumRange = ActiveMaximumRange(Settings, *Selection);
		const float EscapeHysteresis = FMath::Min(MinimumEscapeMarginCm,
			FMath::Max(0.f, MaximumRange - MinimumRange) * 0.25f);
		if (State.Recovery.bActive && ActualRange >= MinimumRange + EscapeHysteresis)
		{
			State.Recovery.bActive = false;
			RecordChange(State, TEXT("Close-range recovery completed; resuming selected position."));
		}
		if (ActualRange < MinimumRange || State.Recovery.bActive)
		{
			const float EscapeRange = FMath::Min(MaximumRange, MinimumRange + EscapeHysteresis
				+ FMath::Max(0.f, Intent.ArrivalRadiusCm));
			const FVector Away = SafeDirection(Relative, -Boid->GetFlightHeading());
			FVector Side = FVector::CrossProduct(Away, FVector::UpVector).GetSafeNormal();
			if (Side.IsNearlyZero()) Side = FVector::CrossProduct(Away, FVector::RightVector).GetSafeNormal();
			const FVector Preferred = SafeDirection(Selection->PreferredDirection, Away);
			TArray<FVector> EscapeDirections = { Away, SafeDirection(Away + Preferred, Away),
				SafeDirection(Away + Side * 0.5f, Away), SafeDirection(Away - Side * 0.5f, Away),
				SafeDirection(Away + Preferred * 0.5f + Side * 0.5f, Away) };
			EscapeDirections.RemoveAll([&Away](const FVector& Direction)
			{
				return FVector::DotProduct(Direction, Away) < 0.25f;
			});
			bool bPendingBudget = false;
			int32 RejectedDirections = 0;
			FVector EscapeGoal;
			FVector EscapeDirection;
			const bool bFoundEscape = FindSafeEscapeGoal(Runner, *Boid, *Target, Center, EscapeRange,
				EscapeDirections, EscapeGoal, EscapeDirection, bPendingBudget, RejectedDirections);
			State.Facts.Rejections += RejectedDirections;
			if (bFoundEscape)
			{
				State.Recovery.Goal = EscapeGoal;
				State.Recovery.Center = Center;
				State.Recovery.Direction = EscapeDirection;
				State.Recovery.bActive = true;
				State.Navigation = FBoidNavigationGoal();
				State.Navigation.Goal = EscapeGoal;
				State.Navigation.bHasGoal = true;
				State.Navigation.bContinuousGoal = true;
				State.Navigation.bPreferNavigationGoal = true;
				State.Navigation.Phase = EBoidNavigationPhase::Retreat;
				State.Navigation.DesiredSpeed = RetreatSpeed(Runner, *Boid);
				State.Facts.Goal = EscapeGoal;
				State.Facts.bRetreating = true;
				State.Facts.Status = EBoidCombatControlStatus::Ready;
				State.Facts.HoldReason = EBoidCombatHoldReason::None;
				State.Facts.Constraint = EBoidCombatMovementConstraint::EmergencySeparation;
				State.Facts.DesiredSpeedCmPerSec = State.Navigation.DesiredSpeed;
				State.Facts.ExecutedPhase = EBoidNavigationPhase::Retreat;
				State.Facts.OrbitReason = TEXT("Retreating from the active minimum target range.");
				State.Facts.MovementReason = TEXT("Body-swept escape goal; line of sight is not required.");
			}
			if (!bFoundEscape)
			{
                const bool bCanReuseRetreat = bPendingBudget && State.Recovery.bActive
                    && FVector::Distance(State.Recovery.Center, Center) < SelectionRebaseDistanceCm;
                if (bCanReuseRetreat)
                {
                    State.Navigation.Goal = State.Recovery.Goal;
                    State.Navigation.bHasGoal = true;
				State.Navigation.bContinuousGoal = true;
                    State.Navigation.bPreferNavigationGoal = true;
                    State.Navigation.Phase = EBoidNavigationPhase::Retreat;
					State.Navigation.DesiredSpeed = RetreatSpeed(Runner, *Boid);
                    State.Facts.Goal = State.Recovery.Goal;
                    State.Facts.bRetreating = true;
                    State.Facts.ExecutedPhase = EBoidNavigationPhase::Retreat;
                    State.Facts.Status = EBoidCombatControlStatus::Ready;
                    State.Facts.HoldReason = EBoidCombatHoldReason::None;
                    State.Facts.Constraint = EBoidCombatMovementConstraint::EmergencySeparation;
					State.Facts.DesiredSpeedCmPerSec = State.Navigation.DesiredSpeed;
                }
                else
                {
                    State.Navigation = FBoidNavigationGoal();
                    State.Navigation.bHoldPosition = true;
				State.Facts.bRetreating = State.Recovery.bActive;
                State.Facts.Status = bPendingBudget ? EBoidCombatControlStatus::PendingBudget : EBoidCombatControlStatus::Blocked;
                State.Facts.HoldReason = bPendingBudget ? EBoidCombatHoldReason::PendingBudget : EBoidCombatHoldReason::RetreatBlocked;
				State.Facts.Constraint = EBoidCombatMovementConstraint::Hold;
                State.Facts.OrbitReason = bPendingBudget
                    ? TEXT("Waiting for a combat control query to validate an escape.")
                    : TEXT("No body-clear outward escape is currently available.");
				State.Facts.ExecutedPhase = EBoidNavigationPhase::None;
                }
			}
			RecordChange(State, State.Facts.OrbitReason);
			Publish(Runner, *Boid, State, Selection, false);
			continue;
		}
		State.Recovery.bActive = false;

		State.Navigation = FBoidNavigationGoal();
		State.Navigation.Goal = Destination;
		State.Navigation.bHasGoal = true;
		State.Navigation.bPreferNavigationGoal = true;
		State.Navigation.Phase = EBoidNavigationPhase::Approach;
		State.Facts.ExecutedPhase = EBoidNavigationPhase::Approach;
		State.Facts.bRetreating = false;
		State.Facts.Constraint = EBoidCombatMovementConstraint::None;

		const float FlightSpeed = FMath::Min(FMath::Max(0.f, Runner.Tuning.CruiseSpeedCmPerSec),
			FMath::Max(0.f, Boid->MaxSpeedCmPerSec));
		const float OrbitWeightScale = OrbitSpeedScales.FindRef(Agent.Id);
		if (!Orbit || OrbitWeightScale <= UE_SMALL_NUMBER || Settings.AngularSpeedDegPerSec <= UE_SMALL_NUMBER || FlightSpeed <= UE_SMALL_NUMBER)
		{
			State.Facts.Status = EBoidCombatControlStatus::NeedsApproach;
			State.Facts.HoldReason = !Orbit || OrbitWeightScale <= UE_SMALL_NUMBER ? EBoidCombatHoldReason::InvalidProfile : EBoidCombatHoldReason::ZeroSpeed;
			State.Facts.OrbitReason = !Orbit ? TEXT("Effective steering profile has no primary orbit behavior.")
				: OrbitWeightScale <= UE_SMALL_NUMBER ? TEXT("The per-boid orbit speed scale is zero.")
				: TEXT("Orbit or flight speed is zero.");
			State.Orbit.StableSeconds = 0.f;
			if ((!Orbit || OrbitWeightScale <= UE_SMALL_NUMBER) && !WarnedConfiguration.Contains(State.Facts.EffectiveProfile + TEXT("|orbit")))
			{
				WarnedConfiguration.Add(State.Facts.EffectiveProfile + TEXT("|orbit"));
				UE_LOG(LogTemp, Warning, TEXT("Boid combat movement: profile %s has no primary orbit behavior."), *State.Facts.EffectiveProfile);
			}
			RecordChange(State, State.Facts.OrbitReason);
			Publish(Runner, *Boid, State, Selection, false);
			continue;
		}
		OrbitBehaviors.Add(Agent.Id, Orbit);
		OrbitCandidates.Add(AgentIndex);
		State.Facts.Status = EBoidCombatControlStatus::NeedsApproach;
		State.Facts.HoldReason = EBoidCombatHoldReason::None;
		State.Facts.OrbitReason = TEXT("Approaching while orbit capability is validated.");
	}

	// First prove one safe step for every active boid before spending budget on
	// longer predictions. Selection remains intact if orbit is temporarily blocked.
	TMap<int32, FVector> FirstGoals;
	for (const int32 AgentIndex : OrbitCandidates)
	{
		const FBoidPlannerAgent& Agent = Agents[AgentIndex];
		AFlyingBoid* Boid = Agent.Boid;
		if (!IsValid(Boid)) continue;
		FAgentState& State = RuntimeByAgent.FindChecked(Agent.Id);
		const FBoidRangedSelection* Selection = Planner.FindSelection(Agent.Id);
		AActor* Target = Boid->MovementTarget;
		UBoidOrbitSteeringBehavior* Orbit = OrbitBehaviors.FindRef(Agent.Id);
		if (!Selection || !Selection->bHasPosition || !IsValid(Target) || !Orbit) continue;
		const FVector Center = Target->GetActorLocation();
		const FVector Offset = EffectiveOrbitOffset(Settings, *Selection, Center, Boid->GetMovementIntent().ArrivalRadiusCm);
		const float EntryTolerance = FMath::Max(1.f, Boid->GetMovementIntent().ArrivalRadiusCm);
		const bool bRetainOrbitPosition = State.Orbit.bWasOrbiting
			&& State.Facts.RadialErrorCm <= 2.f * EntryTolerance
			&& State.Facts.HeightErrorCm <= 2.f * EntryTolerance;
		bool bAtOrbitPosition = bRetainOrbitPosition;
		if (!bAtOrbitPosition)
		{
			const float DistanceToDestination = FVector::Distance(Boid->GetActorLocation(), Selection->Destination);
			const FBoidNavigationFacts NavigationFacts = Boid->GetNavigationFacts();
			const bool bFinalApproachReady = NavigationFacts.Status == EBoidRouteStatus::Ready
				&& NavigationFacts.Phase == EBoidNavigationPhase::Approach
				&& NavigationFacts.Destination.Equals(Selection->Destination, 1.f);
			bAtOrbitPosition = DistanceToDestination <= EntryTolerance + 1.f && bFinalApproachReady;
			// The first-step sweep below validates the immediate body-swept path
			// from the current location. The route service already validates the
			// approach corridor to the fixed selection destination.
			if (!bAtOrbitPosition)
			{
				State.Orbit.StableSeconds = 0.f;
				State.Facts.bCanOrbit = false;
				State.Facts.Status = EBoidCombatControlStatus::NeedsApproach;
				State.Facts.HoldReason = EBoidCombatHoldReason::None;
				State.Facts.OrbitReason = DistanceToDestination <= EntryTolerance + 1.f && !bFinalApproachReady
					? TEXT("Waiting for the validated final approach route to the selection destination.")
					: TEXT("Approaching the selected position before orbit entry.");
				continue;
			}
		}
		const float MinRange = ActiveMinimumRange(Settings, *Selection);
		const float MaxRange = ActiveMaximumRange(Settings, *Selection);
		FBoidSteeringInput Input;
		Input.Position = Boid->GetActorLocation();
		Input.Velocity = Boid->GetFlightVelocity();
		Input.Heading = Boid->GetFlightHeading();
		Input.TargetPosition = Center;
		Input.bHasTarget = true;
		Input.NavigationPhase = EBoidNavigationPhase::Orbit;
		Input.FlightSpeedLimitCmPerSec = FMath::Min(FMath::Max(0.f, Runner.Tuning.CruiseSpeedCmPerSec),
			FMath::Max(0.f, Boid->MaxSpeedCmPerSec));
		Input.DeltaTime = FMath::Max(0.001f, DeltaTime);
		Input.OrbitContext.bActive = true;
		Input.OrbitContext.Center = Center;
		Input.OrbitContext.Axis = Selection->Axis.GetSafeNormal();
		Input.OrbitContext.RadiusCm = FVector::VectorPlaneProject(Offset, Input.OrbitContext.Axis).Size();
		Input.OrbitContext.HeightCm = FVector::DotProduct(Offset, Input.OrbitContext.Axis);
		Input.OrbitContext.AngularSpeedDegPerSec = Settings.AngularSpeedDegPerSec;
		Input.OrbitContext.Direction = State.Orbit.LastDirection;

		const float MaxSpeed = FMath::Max(1.f, Input.FlightSpeedLimitCmPerSec);
		const float AngularRadians = FMath::DegreesToRadians(FMath::Abs(Settings.AngularSpeedDegPerSec));
		const float StepSeconds = FMath::Min(0.1f, FMath::Min(
			AngularRadians > UE_SMALL_NUMBER ? FMath::DegreesToRadians(MaximumPredictionAngleDeg) / AngularRadians : 0.1f,
			MaximumPredictionSegmentCm / MaxSpeed));
		const FVector Proposed = Orbit->PredictPosition(Input, StepSeconds);
		const float DriftAllowance = FMath::Max(20.f, FMath::Min(50.f, Boid->GetMovementIntent().ArrivalRadiusCm * 0.5f));
		bool bClear = false;
		bool bPendingBudget = false;
		bool bObservedBlockedSegment = false;
		if (UBoidOrbitSteeringBehavior::IsRangeStepValid(Center, Input.Position, Proposed, MinRange, MaxRange, Offset.Size(), DriftAllowance))
		{
            if (!HasMovementBudget(Runner))
			{
				bPendingBudget = true;
				State.Facts.Status = EBoidCombatControlStatus::PendingBudget;
				State.Facts.HoldReason = EBoidCombatHoldReason::PendingBudget;
				State.Facts.OrbitReason = TEXT("Waiting for a combat control query to validate the next orbit segment.");
			}
			else
			{
				bClear = SweepClear(Runner, *Boid, *Target, Input.Position, Proposed);
				bObservedBlockedSegment = !bClear;
			}
		}
		if (!bClear && HasMovementBudget(Runner))
		{
			Input.OrbitContext.Direction *= -1;
			const FVector Reverse = Orbit->PredictPosition(Input, StepSeconds);
			if (UBoidOrbitSteeringBehavior::IsRangeStepValid(Center, Input.Position, Reverse, MinRange, MaxRange, Offset.Size(), DriftAllowance)
				&& HasMovementBudget(Runner) && SweepClear(Runner, *Boid, *Target, Input.Position, Reverse))
			{
				State.Orbit.LastDirection = Input.OrbitContext.Direction;
				FirstGoals.Add(Agent.Id, Reverse);
				bClear = true;
			}
			else if (HasMovementBudget(Runner)) bObservedBlockedSegment = true;
		}
		else if (!bClear && !HasMovementBudget(Runner)) bPendingBudget = true;
		if (bClear && !FirstGoals.Contains(Agent.Id)) FirstGoals.Add(Agent.Id, Proposed);
		State.Facts.PredictionSegments = bClear ? 1 : 0;
		State.Facts.ControlQueriesUsed = Runner.LastFrameCombatControlQueries;
		if (bClear)
		{
			State.Orbit.StableSeconds += FMath::Clamp(DeltaTime, 0.f, 0.05f);
			State.Orbit.bHasClearProof = true;
			State.Orbit.ClearProofAgeSeconds = 0.f;
			State.Orbit.ClearProofStart = Input.Position;
			State.Orbit.ClearProofGoal = FirstGoals.FindRef(Agent.Id);
			State.Orbit.ClearProofCenter = Center;
			State.Orbit.ClearProofSelectionVersion = Selection->Version;
			State.Facts.bCanOrbit = bAtOrbitPosition && (State.Orbit.bWasOrbiting || State.Orbit.StableSeconds >= OrbitEntryStableSeconds);
			State.Facts.OrbitReason = State.Facts.bCanOrbit ? TEXT("First orbit segment is body-clear and stable.")
				: TEXT("Waiting for stable first-segment clearance at the selected position.");
			State.Facts.Status = State.Facts.bCanOrbit ? EBoidCombatControlStatus::Ready : EBoidCombatControlStatus::NeedsApproach;
			State.Facts.HoldReason = EBoidCombatHoldReason::None;
		}
		else
		{
			const bool bCanReuseClearProof = bPendingBudget && !bObservedBlockedSegment && State.Orbit.bHasClearProof
				&& State.Orbit.ClearProofSelectionVersion == Selection->Version
				&& State.Orbit.ClearProofAgeSeconds <= 0.25f
				&& FVector::Distance(State.Orbit.ClearProofStart, Input.Position) <= 50.f
				&& FVector::Distance(State.Orbit.ClearProofCenter, Center) <= 50.f
				&& UBoidOrbitSteeringBehavior::IsRangeStepValid(Center, Input.Position,
					State.Orbit.ClearProofGoal, MinRange, MaxRange, Offset.Size(), DriftAllowance);
			if (bCanReuseClearProof)
			{
				FirstGoals.Add(Agent.Id, State.Orbit.ClearProofGoal);
				State.Orbit.ClearProofAgeSeconds += FMath::Max(0.f, DeltaTime);
				State.Facts.bCanOrbit = bAtOrbitPosition
					&& (State.Orbit.bWasOrbiting || State.Orbit.StableSeconds >= OrbitEntryStableSeconds);
			}
			else
			{
				if (!bPendingBudget) State.Orbit.StableSeconds = 0.f;
				State.Facts.bCanOrbit = false;
				State.Orbit.bHasClearProof = false;
			}
			State.Facts.OrbitReason = bPendingBudget
				? State.Facts.OrbitReason : TEXT("Both orbit directions are blocked or violate the range band.");
			if (bPendingBudget)
			{
				State.Facts.Status = EBoidCombatControlStatus::PendingBudget;
				State.Facts.HoldReason = EBoidCombatHoldReason::PendingBudget;
			}
			else
			{
				State.Facts.Status = EBoidCombatControlStatus::NeedsApproach;
				State.Facts.HoldReason = EBoidCombatHoldReason::OrbitBlocked;
				Planner.RequestRefresh(Agent.Id);
			}
		}
	}

	// Optional longer lookahead never gates a safe first step. It only records
	// how much of the bounded prediction horizon fit this frame's remaining budget.
	for (const int32 AgentIndex : OrbitCandidates)
	{
		const FBoidPlannerAgent& Agent = Agents[AgentIndex];
		AFlyingBoid* Boid = Agent.Boid;
		if (!IsValid(Boid) || !HasMovementBudget(Runner)) continue;
		FAgentState& State = RuntimeByAgent.FindChecked(Agent.Id);
		const FBoidRangedSelection* Selection = Planner.FindSelection(Agent.Id);
		AActor* Target = Boid->MovementTarget;
		UBoidOrbitSteeringBehavior* Orbit = OrbitBehaviors.FindRef(Agent.Id);
		if (!Selection || !Selection->bHasPosition || !IsValid(Target) || !Orbit || !FirstGoals.Contains(Agent.Id)) continue;
		const FVector Center = Target->GetActorLocation();
		const FVector Offset = Selection->Offset.IsNearlyZero() ? Selection->Destination - Center : Selection->Offset;
		FBoidSteeringInput Input;
		Input.Position = Boid->GetActorLocation();
		Input.Velocity = Boid->GetFlightVelocity();
		Input.Heading = Boid->GetFlightHeading();
		Input.NavigationPhase = EBoidNavigationPhase::Orbit;
		Input.FlightSpeedLimitCmPerSec = FMath::Min(FMath::Max(0.f, Runner.Tuning.CruiseSpeedCmPerSec),
			FMath::Max(0.f, Boid->MaxSpeedCmPerSec));
		Input.OrbitContext.bActive = true;
		Input.OrbitContext.Center = Center;
		Input.OrbitContext.Axis = Selection->Axis.GetSafeNormal();
		Input.OrbitContext.RadiusCm = FVector::VectorPlaneProject(Offset, Input.OrbitContext.Axis).Size();
		Input.OrbitContext.HeightCm = FVector::DotProduct(Offset, Input.OrbitContext.Axis);
		Input.OrbitContext.AngularSpeedDegPerSec = Settings.AngularSpeedDegPerSec;
		Input.OrbitContext.Direction = State.Orbit.LastDirection;
		const float MaxSpeed = FMath::Max(1.f, Input.FlightSpeedLimitCmPerSec);
		const float AngularRadians = FMath::DegreesToRadians(FMath::Abs(Settings.AngularSpeedDegPerSec));
		const float SegmentSeconds = FMath::Min(AngularRadians > UE_SMALL_NUMBER
			? FMath::DegreesToRadians(MaximumPredictionAngleDeg) / AngularRadians : 0.1f,
			MaximumPredictionSegmentCm / MaxSpeed);
		const float HorizonSeconds = FMath::Min(FMath::Max(0.01f, Settings.OrbitLeadSeconds), SegmentSeconds * MaximumPredictionSegments);
		const int32 SegmentCount = FMath::Clamp(FMath::CeilToInt(HorizonSeconds / FMath::Max(0.001f, SegmentSeconds)), 1, MaximumPredictionSegments);
		FVector Previous = Input.Position;
		for (int32 Segment = 1; Segment <= SegmentCount && HasMovementBudget(Runner); ++Segment)
		{
			const float Seconds = HorizonSeconds * Segment / SegmentCount;
            const FVector Next = Orbit->PredictPosition(Input, Seconds);
			++State.Facts.PredictionSegments;
			const float MinimumRange = ActiveMinimumRange(Settings, *Selection);
			const float MaximumRange = ActiveMaximumRange(Settings, *Selection);
			const float DriftAllowance = FMath::Max(20.f, FMath::Min(50.f, Boid->GetMovementIntent().ArrivalRadiusCm * 0.5f));
			if (!UBoidOrbitSteeringBehavior::IsRangeStepValid(Center, Previous, Next, MinimumRange, MaximumRange, Offset.Size(), DriftAllowance)
				|| !SweepClear(Runner, *Boid, *Target, Previous, Next))
			{
				State.Facts.OrbitReason = TEXT("Longer orbit prediction meets an obstacle or range boundary; the validated next step remains active.");
				break;
			}
			Previous = Next;
		}
	}

	for (const int32 AgentIndex : OrbitCandidates)
	{
		const FBoidPlannerAgent& Agent = Agents[AgentIndex];
		AFlyingBoid* Boid = Agent.Boid;
		if (!IsValid(Boid)) continue;
		FAgentState& State = RuntimeByAgent.FindChecked(Agent.Id);
		const FBoidRangedSelection* Selection = Planner.FindSelection(Agent.Id);
		AActor* Target = Boid->MovementTarget;
		const bool bRequestsOrbit = Boid->GetMovementIntent().RangedPhaseControl == EBoidRangedPhaseControl::Automatic
			|| Boid->GetRequestedRangedPhase() == EBoidRangedPhaseRequest::Orbit;
		if (State.Recovery.bActive)
		{
			State.Navigation.Goal = State.Recovery.Goal;
			State.Navigation.bHasGoal = true;
                    State.Navigation.bContinuousGoal = true;
			State.Navigation.Phase = EBoidNavigationPhase::Retreat;
			State.Facts.ExecutedPhase = EBoidNavigationPhase::Retreat;
			State.Facts.bRetreating = true;
		}
		else if (bRequestsOrbit && State.Facts.bCanOrbit && FirstGoals.Contains(Agent.Id))
		{
			State.Navigation = FBoidNavigationGoal();
			State.Navigation.Goal = FirstGoals.FindRef(Agent.Id);
			State.Navigation.bHasGoal = true;
			State.Navigation.bContinuousGoal = true;
			State.Navigation.bPreferNavigationGoal = true;
			State.Navigation.Phase = EBoidNavigationPhase::Orbit;
			State.Navigation.OrbitContext.bActive = true;
			State.Navigation.OrbitContext.Center = IsValid(Target) ? Target->GetActorLocation() : FVector::ZeroVector;
			State.Navigation.OrbitContext.Axis = Selection ? Selection->Axis : FVector::UpVector;
			const FVector OrbitOffset = Selection ? EffectiveOrbitOffset(Settings, *Selection,
				State.Navigation.OrbitContext.Center, Boid->GetMovementIntent().ArrivalRadiusCm) : FVector::ZeroVector;
			State.Navigation.OrbitContext.RadiusCm = Selection ? FVector::VectorPlaneProject(OrbitOffset,
				State.Navigation.OrbitContext.Axis).Size() : 0.f;
			State.Navigation.OrbitContext.HeightCm = Selection ? FVector::DotProduct(OrbitOffset,
				State.Navigation.OrbitContext.Axis) : 0.f;
			State.Navigation.OrbitContext.AngularSpeedDegPerSec = Settings.AngularSpeedDegPerSec;
			State.Navigation.OrbitContext.Direction = State.Orbit.LastDirection;
            FBoidSteeringInput SpeedInput;
            SpeedInput.Position = Boid->GetActorLocation();
            SpeedInput.Velocity = Boid->GetFlightVelocity();
            SpeedInput.Heading = Boid->GetFlightHeading();
            SpeedInput.NavigationPhase = EBoidNavigationPhase::Orbit;
            SpeedInput.DeltaTime = FMath::Max(0.001f, Runner.Tuning.MaxStepSeconds);
            SpeedInput.FlightSpeedLimitCmPerSec = FMath::Min(Runner.Tuning.CruiseSpeedCmPerSec, Boid->MaxSpeedCmPerSec);
            SpeedInput.OrbitContext = State.Navigation.OrbitContext;
			UBoidOrbitSteeringBehavior* Orbit = OrbitBehaviors.FindRef(Agent.Id);
			State.Navigation.DesiredSpeed = Orbit ? Orbit->CalcDesiredVelocity(SpeedInput).Size()
				* OrbitSpeedScales.FindRef(Agent.Id) : 0.f;
			State.Facts.bOrbiting = true;
			State.Facts.Status = EBoidCombatControlStatus::Ready;
			State.Facts.HoldReason = EBoidCombatHoldReason::None;
			State.Facts.ExecutedPhase = EBoidNavigationPhase::Orbit;
			State.Facts.DesiredSpeedCmPerSec = State.Navigation.DesiredSpeed;
			State.Facts.Goal = State.Navigation.Goal;
		}
		else if (Selection && Selection->bHasPosition)
		{
			State.Navigation.Goal = Selection->Destination;
			State.Navigation.bHasGoal = true;
			State.Navigation.bContinuousGoal = false;
			State.Navigation.bPreferNavigationGoal = true;
			State.Navigation.Phase = EBoidNavigationPhase::Approach;
			State.Facts.bOrbiting = false;
			State.Facts.ExecutedPhase = EBoidNavigationPhase::Approach;
			State.Facts.Goal = State.Navigation.Goal;
			if (bRequestsOrbit && !State.Facts.bCanOrbit)
			{
				State.Facts.Status = State.Facts.Status == EBoidCombatControlStatus::PendingBudget
					? EBoidCombatControlStatus::PendingBudget : EBoidCombatControlStatus::NeedsApproach;
				State.Facts.HoldReason = State.Facts.Status == EBoidCombatControlStatus::PendingBudget
					? EBoidCombatHoldReason::PendingBudget : EBoidCombatHoldReason::None;
			}
		}
		else
		{
			State.Navigation = FBoidNavigationGoal();
			State.Navigation.bHoldPosition = true;
		}
		State.Facts.DesiredSpeedCmPerSec = State.Navigation.DesiredSpeed;
		State.Facts.bReadinessEligible = Selection && Selection->bHasPosition
			&& State.Facts.TargetRangeCm >= State.Facts.MinimumRangeCm
			&& State.Facts.TargetRangeCm <= State.Facts.MaximumRangeCm
			&& Boid->GetTargetFacts().bHasSight;
		State.Facts.ControlQueriesUsed = Runner.LastFrameCombatControlQueries;
		State.Facts.bAttackReady = Boid->GetTargetFacts().bAttackReady;
		if (State.Facts.HoldReason != EBoidCombatHoldReason::None) State.Facts.Constraint = EBoidCombatMovementConstraint::Hold;
		else if (State.Navigation.Phase == EBoidNavigationPhase::Orbit) State.Facts.Constraint = EBoidCombatMovementConstraint::SpeedLimit;
		RecordChange(State, State.Facts.OrbitReason);
		Publish(Runner, *Boid, State, Selection, Boid->GetTargetFacts().bAttackReady);
	}
}

FBoidNavigationGoal FBoidCombatMovementCoordinator::GetNavigation(int32 AgentId, const AActor* Target) const
{
	const FAgentState* State = RuntimeByAgent.Find(AgentId);
	if (!State || !IsValid(Target) || State->Target.Get() != Target)
	{
		FBoidNavigationGoal Hold;
		Hold.bHoldPosition = true;
		return Hold;
	}
	return State->Navigation;
}

void FBoidCombatMovementCoordinator::FinalizeNavigation(USwarmSimulationSubsystem& Runner,
	TConstArrayView<FBoidPlannerAgent> Agents, float DeltaTime)
{
	for (const FBoidPlannerAgent& Agent : Agents)
	{
		AFlyingBoid* Boid = Agent.Boid;
		if (!IsValid(Boid) || Boid->GetMovementIntent().Policy != EBoidMovementPolicy::RangedOrbit) continue;
		FAgentState* State = RuntimeByAgent.Find(Agent.Id);
		if (!State) continue;
		const FBoidNavigationGoal ActualNavigation = Runner.GetNavigationGoal(Boid);
		const FBoidNavigationFacts RouteFacts = Boid->GetNavigationFacts();
		const bool bHeld = ActualNavigation.bHoldPosition || !ActualNavigation.bHasGoal;
		State->Facts.ExecutedPhase = ActualNavigation.Phase;
		State->Facts.bOrbiting = ActualNavigation.Phase == EBoidNavigationPhase::Orbit && !bHeld;
		State->Facts.bRetreating = ActualNavigation.Phase == EBoidNavigationPhase::Retreat && !bHeld;
		State->Facts.RouteReason = RouteFacts.FailureReason;
		if (bHeld)
		{
			State->Facts.Constraint = EBoidCombatMovementConstraint::Hold;
			if (State->Facts.HoldReason == EBoidCombatHoldReason::None)
			{
				State->Facts.HoldReason = EBoidCombatHoldReason::PendingRoute;
				State->Facts.Status = RouteFacts.Status == EBoidRouteStatus::Failed
					? EBoidCombatControlStatus::Blocked : EBoidCombatControlStatus::NeedsApproach;
				State->Facts.OrbitReason = RouteFacts.FailureReason.IsEmpty()
					? TEXT("Waiting for the active navigation route.") : RouteFacts.FailureReason;
				RecordChange(*State, State->Facts.OrbitReason);
			}
		}
		else if (State->Facts.HoldReason == EBoidCombatHoldReason::PendingRoute)
		{
			State->Facts.HoldReason = EBoidCombatHoldReason::None;
			State->Facts.Status = ActualNavigation.Phase == EBoidNavigationPhase::Orbit
				|| ActualNavigation.Phase == EBoidNavigationPhase::Retreat
				? EBoidCombatControlStatus::Ready : EBoidCombatControlStatus::NeedsApproach;
		}
		WarnIfHeld(*Boid, *State, bHeld, DeltaTime);
		State->Facts.HoldSeconds = State->HoldSeconds;
		Boid->PublishCombatMovement(State->Facts);
	}
}

void FBoidCombatMovementCoordinator::ValidateReadinessForAgent(USwarmSimulationSubsystem& Runner,
	AFlyingBoid& Boid, FAgentState& State, float DeltaTime)
{
	const FBoidRangedSettings Settings = Runner.RangedSettings.Sanitized();
	AActor* Target = Boid.MovementTarget;
	const FBoidTargetFacts TargetFacts = Boid.GetTargetFacts();
	const FBoidNavigationGoal ActualNavigation = Runner.GetNavigationGoal(&Boid);
	const FBoidCombatMovementFacts ActualMovementFacts = Boid.GetCombatMovementFacts();
	const FBoidNavigationFacts ActualRouteFacts = Boid.GetNavigationFacts();
	State.Facts.TargetRangeCm = IsValid(Target)
		? FVector::Distance(Boid.GetActorLocation(), Target->GetActorLocation()) : 0.f;
	const bool bRangeReady = State.Facts.bHasSelection
		&& State.Facts.TargetRangeCm >= State.Facts.MinimumRangeCm
		&& State.Facts.TargetRangeCm <= State.Facts.MaximumRangeCm;
	bool bEligible = IsValid(Target) && State.Target.Get() == Target && TargetFacts.TargetActor == Target
		&& State.LastOwnerToken == Boid.GetMovementIntentOwner() && State.LastIntentRevision == Boid.GetMovementIntent().Revision
		&& (!Cast<AFlyingCharacter>(Target) || Cast<AFlyingCharacter>(Target)->IsAlive())
		&& (!Cast<AFlyingBoid>(Target) || Cast<AFlyingBoid>(Target)->IsAlive())
		&& TargetFacts.bDetected && TargetFacts.bHasSight && bRangeReady
		&& Boid.IsAlive() && Boid.IsSteeringActive()
		&& !State.Navigation.bHoldPosition && State.Navigation.bHasGoal
		&& !ActualNavigation.bHoldPosition && ActualNavigation.bHasGoal
		&& ActualNavigation.Phase != EBoidNavigationPhase::Retreat
		&& ActualNavigation.Phase != EBoidNavigationPhase::Transit
		&& ActualRouteFacts.Status == EBoidRouteStatus::Ready
		&& ActualRouteFacts.Phase != EBoidNavigationPhase::Transit;
	if (bEligible)
	{
		if (!HasBudget(Runner)) State.Readiness.bPendingBudget = true;
		else
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(BoidCombatReadiness), true);
			Params.AddIgnoredActor(&Boid);
			Params.AddIgnoredActor(Target);
			++Runner.LastFrameCombatControlQueries;
			bEligible = !Runner.GetWorld()->LineTraceTestByChannel(Boid.GetActorLocation(),
				Target->GetActorLocation(), ECC_Visibility, Params);
			State.Readiness.bPendingBudget = false;
		}
	}
	else State.Readiness.bPendingBudget = false;
	if (State.Readiness.bPendingBudget) State.Facts.bAttackReady = false;
	else State.Facts.bAttackReady = BoidRanged::AdvanceReadiness(bEligible, DeltaTime,
		Settings.ReadyDelaySeconds, State.Readiness.ElapsedSeconds);
	State.Facts.bReadinessEligible = bEligible || State.Readiness.bPendingBudget;
	State.Facts.ActualSpeedCmPerSec = Boid.GetFlightVelocity().Size();
	State.Facts.ControlQueriesUsed = Runner.LastFrameCombatControlQueries;
	State.Facts.ExecutedPhase = ActualNavigation.Phase;
	State.Facts.Constraint = ActualMovementFacts.Constraint;
	State.Facts.MovementReason = ActualMovementFacts.MovementReason;
	State.Facts.RouteReason = ActualRouteFacts.FailureReason;
	Boid.PublishPositioning(Boid.GetPositioningFacts(), State.Facts.bAttackReady);
	Boid.PublishCombatMovement(State.Facts);
}

void FBoidCombatMovementCoordinator::ValidateReadinessAfterMovement(USwarmSimulationSubsystem& Runner,
	TConstArrayView<FBoidPlannerAgent> Agents, float DeltaTime)
{
	TArray<int32> Order;
	Order.Reserve(Agents.Num());
	for (int32 Index = 0; Index < Agents.Num(); ++Index) Order.Add(Index);
	if (!Order.IsEmpty())
	{
		const int32 Start = FMath::Abs(ReadinessCursor) % Order.Num();
		TArray<int32> Rotated;
		Rotated.Reserve(Order.Num());
		for (int32 Offset = 0; Offset < Order.Num(); ++Offset) Rotated.Add((Start + Offset) % Order.Num());
		Order = MoveTemp(Rotated);
		ReadinessCursor = (ReadinessCursor + FMath::Max(1, ReadinessReserve)) % Order.Num();
	}
	for (const int32 AgentIndex : Order)
	{
		const FBoidPlannerAgent& Agent = Agents[AgentIndex];
		AFlyingBoid* Boid = Agent.Boid;
		if (!IsValid(Boid)) continue;
		if (Boid->GetMovementIntent().Policy != EBoidMovementPolicy::RangedOrbit) continue;
		FAgentState* State = RuntimeByAgent.Find(Agent.Id);
		if (!State) continue;
		ValidateReadinessForAgent(Runner, *Boid, *State, DeltaTime);
	}
}

void FBoidCombatMovementCoordinator::ResetAgent(int32 AgentId)
{
	RuntimeByAgent.Remove(AgentId);
}

void FBoidCombatMovementCoordinator::ResetPosition(int32 AgentId)
{
	if (FAgentState* State = RuntimeByAgent.Find(AgentId))
	{
		State->Navigation = FBoidNavigationGoal();
		State->Navigation.bHoldPosition = true;
		State->Orbit.bWasOrbiting = false;
		State->Orbit.StableSeconds = 0.f;
		State->Orbit.bHasClearProof = false;
		State->Orbit.ClearProofAgeSeconds = 0.f;
		State->Recovery.bActive = false;
		State->Readiness.ElapsedSeconds = 0.f;
		State->Readiness.bPendingBudget = false;
		State->Facts.bAttackReady = false;
		State->Facts.bReadinessEligible = false;
		State->Facts.bCanOrbit = State->Facts.bOrbiting = State->Facts.bRetreating = false;
	}
}
