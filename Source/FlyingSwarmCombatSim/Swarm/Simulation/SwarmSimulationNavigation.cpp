#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "Swarm/Routing/BoidFlightRouteService.h"
#include "Swarm/Positioning/BoidRangedPositionPlanner.h"
#include "Swarm/Positioning/BoidCombatMovementCoordinator.h"
#include "Swarm/FlyingBoid.h"
#include "Player/FlyingCharacter.h"
#include "Components/SphereComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

namespace BoidNavigationPrivate
{
	bool LivingTarget(AActor* Actor)
	{
		if (!IsValid(Actor))return false;
		if (const auto* Player = Cast<AFlyingCharacter>(Actor))return Player->IsAlive();
		if (const auto* Boid = Cast<AFlyingBoid>(Actor))return Boid->IsAlive();
		return true;
	}
}

void FBoidRouteServiceDeleter::operator()(FBoidFlightRouteService* Service) const { delete Service; }
FBoidFlightRouteService& USwarmSimulationSubsystem::GetRouteService() { return *RouteService; }

bool USwarmSimulationSubsystem::IsAllowedFlightPosition(const FVector& Position, float Radius) const
{
	return CanSimulate() && RouteService->IsAllowedPosition(const_cast<USwarmSimulationSubsystem*>(this), Position,
	                                                        Radius);
}

bool USwarmSimulationSubsystem::IsAllowedFlightSegment(const FVector& Start, const FVector& Goal, float Radius) const
{
	return CanSimulate() && RouteService->IsAllowedSegment(const_cast<USwarmSimulationSubsystem*>(this), Start, Goal,
	                                                       Radius);
}

void USwarmSimulationSubsystem::InvalidateNavigationGraph()
{
	if (!RouteService) return;
	RouteService->InvalidateGraph();
	for (auto& Pair : NavigationAgents)
	{
		Pair.Value.SegmentValidatedAt = -1.;
		Pair.Value.Status = EBoidRouteStatus::Pending;
		Pair.Value.Waypoints.Reset();
	}
}

FBoidNavigationGoal USwarmSimulationSubsystem::GetNavigationGoal(AFlyingBoid* Boid) const
{
	if (const FNavigationAgentState* State = NavigationAgents.Find(GetAgentId(Boid)))return State->Navigation;
	return FBoidNavigationGoal();
}

bool USwarmSimulationSubsystem::GetDebugMovementView(const AFlyingBoid* Boid, FBoidNavigationGoal& OutGoal, TArray<FVector>& OutRemainingRoute) const
{
	OutGoal = FBoidNavigationGoal();
	OutRemainingRoute.Reset();
	if (!IsValid(Boid) || !Boid->IsAlive() || Boid->GetWorld() != GetWorld() || !IsAgentRegistered(Boid)) return false;

	const FBoidMovementIntent Intent = Boid->GetMovementIntent();
	const int32 AgentId = GetAgentId(Boid);
	const int32* AgentIndex = AgentRecordIndices.Find(AgentId);
	const FRegisteredAgent* Agent = AgentIndex && Agents.IsValidIndex(*AgentIndex) && Agents[*AgentIndex].Id == AgentId
		? &Agents[*AgentIndex] : nullptr;
	const FNavigationAgentState* State = NavigationAgents.Find(AgentId);
	const bool bUsesNavigation = Intent.Policy == EBoidMovementPolicy::RangedOrbit || Intent.bIsTransit
		|| (NavigationMode == EBoidNavigationMode::AuthoredRoutes
			&& (Intent.Policy == EBoidMovementPolicy::FollowStandOff
				|| Intent.Policy == EBoidMovementPolicy::LocationArrival));
	const bool bCurrentState = State && State->IntentRevision == Intent.Revision
		&& State->IntentOwner == Boid->GetMovementIntentOwner() && State->RouteTarget.Get() == Boid->MovementTarget
		&& State->Mode == NavigationMode
		&& (NavigationMode == EBoidNavigationMode::DirectOnly || (RouteService && State->RouteGeneration == RouteService->GetGeneration()));

	if (bUsesNavigation)
	{
		if (bCurrentState) OutGoal = State->Navigation;
		else if (State && CombatCoordinator && Intent.Policy == EBoidMovementPolicy::RangedOrbit)
		{
			// Local combat goals deliberately preserve the underlying route identity. Compare
			// the two existing published outputs rather than requiring that old route to match.
			const FBoidNavigationGoal Local = CombatCoordinator->GetNavigation(AgentId, Boid->MovementTarget);
			const bool bLocalPhase = Local.Phase == EBoidNavigationPhase::Orbit || Local.Phase == EBoidNavigationPhase::Retreat;
			if (bLocalPhase && Local.bHasGoal && !Local.bHoldPosition && State->Navigation.bHasGoal && !State->Navigation.bHoldPosition
				&& State->Navigation.Phase == Local.Phase && State->Navigation.Goal.Equals(Local.Goal, 1.f)) OutGoal = State->Navigation;
		}
	}
	else
	{
		OutGoal.Goal = Intent.Goal;
		OutGoal.bHasGoal = Intent.bHasGoal;
		OutGoal.bPreferNavigationGoal = false;
	}

	const AActor* Target = Boid->MovementTarget;
	const AFlyingBoid* TargetBoid = Cast<AFlyingBoid>(Target);
	const AFlyingCharacter* TargetPlayer = Cast<AFlyingCharacter>(Target);
	const bool bTargetLiving = IsValid(Target) && (!TargetBoid || TargetBoid->IsAlive())
		&& (!TargetPlayer || TargetPlayer->IsAlive());
	if (Agent && Agent->bRetreating && Intent.Policy == EBoidMovementPolicy::FollowStandOff && bTargetLiving
		&& Agent->RetreatTarget.Get() == Target)
	{
		OutGoal.Goal = Agent->RetreatGoal;
		OutGoal.bHasGoal = true;
		OutGoal.bHoldPosition = false;
		OutGoal.bPreferNavigationGoal = true;
		OutGoal.Phase = EBoidNavigationPhase::Retreat;
	}

	if (!bUsesNavigation || !bCurrentState || State->Status != EBoidRouteStatus::Ready || State->WaypointIndex < 0
		|| !State->Waypoints.IsValidIndex(State->WaypointIndex)) return true;
	if (OutGoal.bHoldPosition || !OutGoal.bHasGoal || OutGoal.Goal.ContainsNaN() || !OutGoal.Goal.Equals(State->Waypoints[State->WaypointIndex], 1.f)) return true;
	for (int32 Index = State->WaypointIndex; Index < State->Waypoints.Num(); ++Index)
	{
		if (State->Waypoints[Index].ContainsNaN())
		{
			OutRemainingRoute.Reset();
			return true;
		}
		OutRemainingRoute.Add(State->Waypoints[Index]);
	}
	return true;
}

void USwarmSimulationSubsystem::AdoptNavigationRoute(int32 AgentId, const FVector& Goal,
                                                     const FBoidFlightRouteResult& Route)
{
	FNavigationAgentState& State = NavigationAgents.FindOrAdd(AgentId);
	if (State.RouteHandle)RouteService->Cancel(State.RouteHandle);
	State.RouteHandle = 0;
	State.Destination = Goal;
	State.bHasDestination = true;
	State.Waypoints = Route.Waypoints;
	State.WaypointIndex = 0;
	State.LengthCm = Route.LengthCm;
	State.Tier = Route.Tier;
	State.Status = Route.Status;
	State.FailureReason = Route.FailureReason;
	State.SegmentValidatedAt = -1.;
	State.ValidatedWaypointIndex = INDEX_NONE;
	State.Mode = NavigationMode;
	State.RouteGeneration = RouteService->GetGeneration();
	++State.GoalVersion;
	if (FRegisteredAgent* Agent = FindAgentRecord(AgentId); Agent && Agent->Boid.IsValid())
	{
		State.IntentRevision = Agent->Boid->GetMovementIntent().Revision;
		State.IntentOwner = Agent->Boid->GetMovementIntentOwner();
		State.RouteTarget = Agent->Boid->MovementTarget;
	}
}

void USwarmSimulationSubsystem::UpdateNavigationGoals(float DeltaTime)
{
	NavigationTime += DeltaTime;
	for (const FBoidPlannerAgent& Agent : PlannerAgents)NavigationAgents.FindOrAdd(Agent.Id);
	// Rotate validation priority so a large population cannot starve later agents.
	for (int32 Visit = 0; Visit < PlannerAgents.Num(); ++Visit)
	{
		const FBoidPlannerAgent& Agent = PlannerAgents[(NavigationCursor + Visit) % PlannerAgents.Num()];
		AFlyingBoid* Boid = Agent.Boid;
		if (!IsValid(Boid) || !Boid->IsAlive())continue;
		FNavigationAgentState& N = NavigationAgents.FindChecked(Agent.Id);
		const FBoidMovementIntent Intent = Boid->GetMovementIntent();
		const FBoidTargetFacts TargetFacts = Boid->GetTargetFacts();
		AActor* Target = Boid->MovementTarget;
		if (TargetFacts.bDetected && TargetFacts.bHasSight && BoidNavigationPrivate::LivingTarget(
			TargetFacts.TargetActor))
		{
			N.LastSeenTarget = TargetFacts.TargetActor;
			N.LastSeenPosition = TargetFacts.LastKnownPosition;
			N.LastSeenAt = NavigationTime;
		}
		const bool MemoryValid = N.LastSeenAt >= 0 && NavigationTime - N.LastSeenAt <= FMath::Max(
				0.f, LastKnownPursuitSeconds)
			&& BoidNavigationPrivate::LivingTarget(N.LastSeenTarget.Get());
		FBoidNavigationFacts Facts;
		Facts.RoomId = RouteService->GetRegion(this, Boid->GetActorLocation());
		Facts.bHasTransitGoal = MemoryValid;
		Facts.TransitGoal = N.LastSeenPosition;
		N.RetrySeconds = FMath::Max(0.f, N.RetrySeconds - DeltaTime);
		FBoidNavigationGoal Wanted;
		bool Controlled = false;
		if (Intent.Policy == EBoidMovementPolicy::RangedOrbit)
		{
			Wanted = CombatCoordinator->GetNavigation(Agent.Id, Target);
			Controlled = true;
		}
		else if (Intent.Policy == EBoidMovementPolicy::LocationArrival)
		{
			Controlled = NavigationMode == EBoidNavigationMode::AuthoredRoutes || Intent.bIsTransit;
			Wanted.Goal = Intent.Goal;
			Wanted.bHasGoal = Intent.bHasGoal;
			Wanted.bPreferNavigationGoal = true;
			Wanted.Phase = Intent.bIsTransit ? EBoidNavigationPhase::Transit : EBoidNavigationPhase::Approach;
			if (Intent.bIsTransit && !MemoryValid)
			{
				Wanted.bHasGoal = false;
				Wanted.bHoldPosition = true;
				Facts.bHasTransitGoal = false;
			}
		}
		else if (Intent.Policy == EBoidMovementPolicy::FollowStandOff && NavigationMode ==
			EBoidNavigationMode::AuthoredRoutes)
		{
			Controlled = true;
			if (BoidNavigationPrivate::LivingTarget(Target) && TargetFacts.bDetected)
			{
				const FVector Center = Target->GetActorLocation();
				const FName Region = RouteService->GetRegion(this, Center, N.PlayerRegion);
				const bool Refresh = !N.bHasDestination || N.IntentRevision != Intent.Revision || Region != N.
					PlayerRegion
					|| FVector::Distance(Center, N.GoalTargetPosition) > RangedSettings.TargetMovementThresholdCm
					|| FVector::Distance(Boid->GetActorLocation(), Center) < Intent.MinimumTargetDistanceCm;
				FVector Away = (Boid->GetActorLocation() - Center).GetSafeNormal();
				if (Away.IsNearlyZero())Away = -Boid->GetFlightHeading();
				Wanted.Goal = Refresh
					              ? Center + Away * FMath::Max(Intent.StandOffDistanceCm,
					                                           Intent.MinimumTargetDistanceCm)
					              : N.Destination;
				Wanted.bHasGoal = true;
				Wanted.bPreferNavigationGoal = true;
				Wanted.Phase = EBoidNavigationPhase::Approach;
				if (Refresh)N.GoalTargetPosition = Center;
				N.PlayerRegion = Region;
			}
			else Wanted.bHoldPosition = true;
		}
		if (!Controlled)
		{
			if (N.RouteHandle)RouteService->Cancel(N.RouteHandle);
			N.RouteHandle = 0;
			N.bHasDestination = false;
			N.Waypoints.Reset();
			N.Navigation = FBoidNavigationGoal();
			Boid->PublishNavigation(Facts);
			continue;
		}
		if (!Boid->IsSteeringActive() || !Wanted.bHasGoal)
		{
			// Hold is temporary: retain a version-matched combat route for resumption.
			if (Intent.Policy != EBoidMovementPolicy::RangedOrbit || N.IntentRevision != Intent.Revision || N.
				IntentOwner != Boid->GetMovementIntentOwner() || N.RouteTarget.Get() != Target)
			{
				if (N.RouteHandle)RouteService->Cancel(N.RouteHandle);
				N.RouteHandle = 0;
				N.bHasDestination = false;
				N.Waypoints.Reset();
			}
			Wanted.bHasGoal = false;
			Wanted.bHoldPosition = true;
			N.Navigation = Wanted;
			Facts.Phase = Wanted.Phase;
			Facts.FailureReason = RouteService->GetConfigurationFailure();
			if (Facts.FailureReason.IsEmpty() && Intent.Policy == EBoidMovementPolicy::RangedOrbit)
				Facts.FailureReason
					= Boid->GetCombatMovementFacts().Detail;
			const bool bConfigurationFailed = !RouteService->GetConfigurationFailure().IsEmpty()
				|| (Intent.Policy == EBoidMovementPolicy::RangedOrbit && Boid->GetCombatMovementFacts().HoldReason ==
					EBoidCombatHoldReason::InvalidProfile);
			Facts.Status = bConfigurationFailed ? EBoidRouteStatus::Failed : EBoidRouteStatus::Pending;
			Boid->PublishNavigation(Facts);
			continue;
		}
		if (Wanted.Phase == EBoidNavigationPhase::Orbit || Wanted.Phase == EBoidNavigationPhase::Retreat)
		{
			// Temporary local combat motion preserves the underlying approach route.
			N.Navigation = Wanted;
			Facts.Phase = Wanted.Phase;
			Facts.Status = EBoidRouteStatus::Ready;
			Facts.Tier = EBoidRouteTier::Direct;
			Facts.Destination = Facts.Waypoint = Wanted.Goal;
			Facts.Waypoints.Add(Wanted.Goal);
			Facts.GoalVersion = N.GoalVersion;
			Boid->PublishNavigation(Facts);
			continue;
		}
		if (Intent.Policy == EBoidMovementPolicy::RangedOrbit)
		{
			const FBoidRangedSelection* Selection = RangedPositionPlanner->FindSelection(Agent.Id);
			if (Selection && Selection->bHasPosition && (Selection->Version != N.SelectionVersion || Selection->Route.
					GoalVersion != N.SelectionRouteVersion
					|| Selection->RouteGeneration != N.RouteGeneration || Selection->NavigationMode != N.Mode || N.
					RouteTarget.Get() != Selection->Target.Get())
				&& Selection->Destination.Equals(Wanted.Goal, 1.f) && Selection->NavigationMode == NavigationMode
				&& Selection->Route.Status == EBoidRouteStatus::Ready
				&& (NavigationMode == EBoidNavigationMode::DirectOnly || Selection->RouteGeneration == RouteService->
					GetGeneration()))
			{
				AdoptNavigationRoute(Agent.Id, Selection->Destination, Selection->Route);
				N.SelectionVersion = Selection->Version;
				N.SelectionRouteVersion = Selection->Route.GoalVersion;
			}
		}
		const bool Changed = !N.bHasDestination || !N.Destination.Equals(Wanted.Goal, 1.f) || N.IntentRevision != Intent
			.Revision || N.IntentOwner != Boid->GetMovementIntentOwner() || N.RouteTarget.Get() != Target || N.Mode !=
			NavigationMode
			|| (NavigationMode == EBoidNavigationMode::AuthoredRoutes && N.RouteGeneration != RouteService->
				GetGeneration());
		if (Changed)
		{
			if (N.RouteHandle)RouteService->Cancel(N.RouteHandle);
			N.RouteHandle = 0;
			N.Destination = Wanted.Goal;
			N.bHasDestination = true;
			N.Waypoints.Reset();
			N.WaypointIndex = 0;
			N.Status = EBoidRouteStatus::Pending;
			N.RetrySeconds = 0;
			N.FailureReason.Reset();
			N.IntentRevision = Intent.Revision;
			N.IntentOwner = Boid->GetMovementIntentOwner();
			N.RouteTarget = Target;
			N.Mode = NavigationMode;
			N.RouteGeneration = RouteService->GetGeneration();
			N.SegmentValidatedAt = -1.;
			++N.GoalVersion;
		}
		if (N.RouteHandle)
		{
			if (const FBoidFlightRouteResult* Result = RouteService->FindResult(N.RouteHandle); Result && Result->Status
				!= EBoidRouteStatus::Pending)
			{
				const FBoidFlightRouteResult Copy = *Result;
				RouteService->Cancel(N.RouteHandle);
				N.RouteHandle = 0;
				if (Copy.GoalVersion == N.GoalVersion)
				{
					N.Status = Copy.Status;
					N.Tier = Copy.Tier;
					N.LengthCm = Copy.LengthCm;
					N.FailureReason = Copy.FailureReason;
					N.Waypoints = Copy.Waypoints;
					N.WaypointIndex = 0;
					N.SegmentValidatedAt = -1.;
					N.RetrySeconds = FMath::Max(.01f, NavigationRetrySeconds);
				}
			}
		}
		if (N.Status == EBoidRouteStatus::Ready && !N.Waypoints.IsEmpty())
		{
			while (N.WaypointIndex + 1 < N.Waypoints.Num() && FVector::Distance(
				Boid->GetActorLocation(), N.Waypoints[N.WaypointIndex]) <= FMath::Max(1.f, WaypointReachToleranceCm))
			{
				const EBoidRouteStatus Next = RouteService->ValidateActiveSegment(
					this, Boid, Boid->GetActorLocation(), N.Waypoints[N.WaypointIndex + 1], Target);
				if (Next != EBoidRouteStatus::Ready) break;
				++N.WaypointIndex;
				N.SegmentValidatedAt = NavigationTime;
				N.ValidatedWaypointIndex = N.WaypointIndex;
			}
			const bool NeedsCheck = N.SegmentValidatedAt < 0 || N.ValidatedWaypointIndex != N.WaypointIndex ||
				NavigationTime - N.SegmentValidatedAt >= .25;
			if (NeedsCheck && NavigationMode == EBoidNavigationMode::AuthoredRoutes)
			{
				const EBoidRouteStatus Check = RouteService->ValidateActiveSegment(
					this, Boid, Boid->GetActorLocation(), N.Waypoints[N.WaypointIndex], Target);
				if (Check == EBoidRouteStatus::Ready)
				{
					N.SegmentValidatedAt = NavigationTime;
					N.ValidatedWaypointIndex = N.WaypointIndex;
				}
				else if (Check == EBoidRouteStatus::Failed)
				{
					N.Status = Check;
					N.FailureReason = TEXT("Active segment blocked");
					N.Waypoints.Reset();
					N.RetrySeconds = FMath::Max(.01f, NavigationRetrySeconds);
				}
				else
				{
					Wanted.bHasGoal = false;
					Wanted.bHoldPosition = true;
				}
			}
			if (N.Status == EBoidRouteStatus::Ready && Wanted.bHasGoal)
			{
				Wanted.Goal = N.Waypoints[N.WaypointIndex];
				Wanted.bContinuousGoal = N.WaypointIndex + 1 < N.Waypoints.Num();
				if (Wanted.bContinuousGoal)Wanted.Phase = EBoidNavigationPhase::Transit;
			}
		}
		if (N.Status != EBoidRouteStatus::Ready || N.Waypoints.IsEmpty())
		{
			Wanted.bHasGoal = false;
			Wanted.bHoldPosition = true;
			if (!N.RouteHandle && N.RetrySeconds <= 0)
			{
				FVector Goal = N.Destination;

				N.RouteHandle = RouteService->Request(this, Boid, Agent.Id, N.GoalVersion, Boid->GetActorLocation(),
				                                      Goal, Target);
				N.Status = EBoidRouteStatus::Pending;
			}
		}
		N.Navigation = Wanted;
		Facts.Phase = Wanted.Phase;
		Facts.Status = N.Status == EBoidRouteStatus::Failed
			               ? EBoidRouteStatus::Failed
			               : (Wanted.bHoldPosition ? EBoidRouteStatus::Pending : N.Status);
		Facts.Tier = N.Tier;
		Facts.Destination = N.Destination;
		Facts.Waypoint = Wanted.Goal;
		Facts.Waypoints = N.Waypoints;
		Facts.GoalVersion = N.GoalVersion;
		Facts.LengthCm = N.LengthCm;
		Facts.FailureReason = N.FailureReason;
		Boid->PublishNavigation(Facts);
		if (bDrawNavigation && !IsDebugPresentationSuppressed())
		{
			DrawDebugLine(GetWorld(), Boid->GetActorLocation(), Wanted.Goal,
			              Wanted.bHasGoal ? FColor::Green : FColor::Red, false, 0, 0, 2);
			DrawDebugString(GetWorld(), Boid->GetActorLocation() + FVector(0, 0, 130),
			                FString::Printf(
				                TEXT("route %d tier %d phase %d version %d: %s"), static_cast<int32>(N.Status),
				                static_cast<int32>(N.Tier), static_cast<int32>(Wanted.Phase), N.GoalVersion,
				                *N.FailureReason), nullptr, FColor::White, 0);
		}
	}
	if (!PlannerAgents.IsEmpty())NavigationCursor = (NavigationCursor + 1) % PlannerAgents.Num();
}
