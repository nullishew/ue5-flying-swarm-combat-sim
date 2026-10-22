#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "Swarm/Simulation/SwarmSimulationBounds.h"
#include "Swarm/FlyingBoid.h"
#include "Swarm/Steering/BoidSteeringProfile.h"
#include "World_Navigation/NavigationGraphAsset.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Swarm/Routing/BoidFlightRouteService.h"
#include "Swarm/Positioning/BoidRangedPositionPlanner.h"
#include "Swarm/Positioning/BoidCombatMovementCoordinator.h"

void USwarmSimulationSubsystem::CopyProjectSettings()
{
	const auto* Settings = GetDefault<USwarmSimulationSettings>();
	MaxPopulation = FMath::Max(0, Settings->MaxPopulation);
	Tuning = Settings->Tuning;
	RangedSettings = Settings->RangedSettings;
	RouteQueriesPerFrame = Settings->RouteQueriesPerFrame;
	RouteExpansionsPerFrame = Settings->RouteExpansionsPerFrame;
	NavigationRetrySeconds = Settings->NavigationRetrySeconds;
	WaypointReachToleranceCm = Settings->WaypointReachToleranceCm;
	LastKnownPursuitSeconds = Settings->LastKnownPursuitSeconds;
	CombatControlQueriesPerFrame = Settings->CombatControlQueriesPerFrame;
	DefaultSteeringProfile = Settings->DefaultSteeringProfile.LoadSynchronous();
}

bool USwarmSimulationSubsystem::CanSpawnBoid() const
{
	UWorld* World = GetWorld();
	if (bEndingPlay || !IsInitialized() || !World || MaxPopulation <= 0) return false;
	int32 LivingPopulation = 0;
	// Query only on spawn attempts. World actors include placed and not-yet-registered enemies.
	for (TActorIterator<AFlyingBoid> It(World); It; ++It)
	{
		const AFlyingBoid* Boid = *It;
		if (IsValid(Boid) && !Boid->IsActorBeingDestroyed() && Boid->IsAlive() && ++LivingPopulation >= MaxPopulation) return false;
	}
	return true;
}

void USwarmSimulationSubsystem::NotifySetupChanged()
{
	SetupRefreshFrame = MAX_uint64;
}

void USwarmSimulationSubsystem::RefreshSetup()
{
	if (bEndingPlay || !IsInitialized() || !GetWorld() || SetupRefreshFrame == GFrameCounter) return;
	SetupRefreshFrame = GFrameCounter;
	// Lazy resolution also works from a spawner's BeginPlay, before bounds BeginPlay.
	ASwarmSimulationBounds* Bounds = nullptr;
	int32 BoundsCount = 0;
	for (TActorIterator<ASwarmSimulationBounds> It(GetWorld()); It; ++It)
	{
		if (It->IsAvailableForSimulation())
		{
			Bounds = *It;
			++BoundsCount;
		}
	}
	ASwarmSimulationBounds* Source = nullptr;
	UBoxComponent* Box = nullptr;
	FString Problem;
	if (BoundsCount > 1) Problem = TEXT(
		"Multiple SwarmSimulationBounds actors are active; movement and firing are suspended.");
	else if (BoundsCount == 1)
	{
		Source = Bounds;
		Box = Bounds->FlightBounds;
	}
	else Problem = TEXT(
		"No swarm bounds source. Place one SwarmSimulationBounds in the persistent level; movement and firing are suspended.");
	if (Source && (!IsValid(Box) || Box->GetScaledBoxExtent().ContainsNaN()
		|| Box->GetScaledBoxExtent().GetMin() <= 0.f || Box->GetComponentTransform().ContainsNaN()))
	{
		Problem = TEXT("Swarm bounds geometry is invalid; movement and firing are suspended.");
		Source = nullptr;
		Box = nullptr;
	}
	if (Problem != LastSetupProblem)
	{
		if (!Problem.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("%s"), *Problem);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Swarm simulation setup is ready: %s"), *GetNameSafe(Source));
		}
		LastSetupProblem = Problem;
	}
	const bool bSourceChanged = BoundsSource.Get() != Source;
	if (bSourceChanged)
	{
		BoundsSource = Source;
		InvalidateNavigationGraph();
		for (FRegisteredAgent& Agent : Agents)
		{
			RouteService->ResetAgent(Agent.Id);
			RangedPositionPlanner->ResetAgent(Agent.Id);
			CombatCoordinator->ResetAgent(Agent.Id);
		}
		NavigationAgents.Reset();
	}
	WanderBounds = Box;
	if (Source)
	{
		NavigationMode = Source->NavigationMode;
		NavigationGraph = Source->NavigationGraph;
	}
	else
	{
		NavigationMode = EBoidNavigationMode::DirectOnly;
		NavigationGraph = nullptr;
	}
}

bool USwarmSimulationSubsystem::CanSimulate() const
{
	if (bEndingPlay || !bWorldReady || !IsInitialized()) return false;
	const_cast<USwarmSimulationSubsystem*>(this)->RefreshSetup();
	const ASwarmSimulationBounds* Source = BoundsSource.Get();
	return IsValid(Source) && Source->IsAvailableForSimulation() && IsValid(WanderBounds);
}

bool USwarmSimulationSubsystem::IsDebugSimulationReady() const
{
	if (bEndingPlay || !bWorldReady || !IsInitialized()) return false;
	const ASwarmSimulationBounds* Source = BoundsSource.Get();
	return IsValid(Source) && Source->IsAvailableForSimulation() && IsValid(WanderBounds);
}

bool USwarmSimulationSubsystem::IsAgentRegistered(const AFlyingBoid* Boid) const
{
	return !bEndingPlay && IsValid(Boid) && Boid->IsAlive() && Boid->GetWorld() == GetWorld()
		&& Boid->GetSimulationSubsystem() == this && GetAgentId(Boid) != INDEX_NONE
		&& !PendingRemovals.Contains(TWeakObjectPtr<AFlyingBoid>(const_cast<AFlyingBoid*>(Boid)));
}
