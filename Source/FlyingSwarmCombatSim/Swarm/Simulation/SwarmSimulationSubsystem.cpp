#include "Swarm/Simulation/SwarmSimulationSubsystem.h"

#include "Swarm/Flight/BoidFlightAlgorithms.h"
#include "Swarm/Steering/BoidSteeringBehavior.h"
#include "Swarm/Positioning/BoidRangedPositionPlanner.h"
#include "Swarm/Positioning/BoidCombatMovementCoordinator.h"
#include "Swarm/Routing/BoidFlightRouteService.h"
#include "Swarm/Steering/BoidSteeringProfile.h"

#include "Components/BoxComponent.h"
#include "Swarm/Simulation/SwarmSimulationBounds.h"
#include "Combat/BoidAttackComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StateTreeComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Swarm/FlyingBoid.h"
#include "Player/FlyingCharacter.h"

namespace
{
	TAutoConsoleVariable<int32> CVarExhaustiveAvoidance(TEXT("swarm.ExhaustiveAvoidance"), 0, TEXT("Use all optional avoidance/recovery directions instead of bounded search."), ECVF_Default);

	TAutoConsoleVariable<int32> CVarUseSpatialNeighbors(TEXT("swarm.UseSpatialNeighbors"), 1, TEXT("Use captured spatial neighbor queries for 32 or more snapshots. Set to 0 for exhaustive comparison."), ECVF_Default);

	struct FBoidSnapshot
	{
		TWeakObjectPtr<AFlyingBoid> Boid;
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector Heading = FVector::ForwardVector;
		float Radius = 30.0f;
		int32 Id = INDEX_NONE;
		FVector TargetPosition = FVector::ZeroVector;
		TWeakObjectPtr<AActor> TargetActor;
		FBoidMovementIntent MovementIntent;
		uint64 IntentOwner = 0;
		uint64 TransitMemoryRevision = 0;
		bool bBoundsCorrected = false;
		FVector Goal = FVector::ZeroVector;
		bool bHasGoal = false;
		bool bDestinationBlocked = false;
		bool bRetreating = false;
		bool bHasTarget = false;
		bool bSteeringActive = false;
		bool bPlannerControlled = false;
		bool bCoordinatedCombat = false;
		bool bNavigationHold = false;
		bool bPreferNavigationGoal = false;
		EBoidNavigationPhase NavigationPhase = EBoidNavigationPhase::None;
		FBoidOrbitContext OrbitContext;
		bool bContinuousGoal = false;
		float DesiredSpeed = 0.f;
	};

	struct FSteeringOutput
	{
		FVector ProfileIntent = FVector::ZeroVector;
		FVector Separation = FVector::ZeroVector;
		FVector Alignment = FVector::ZeroVector;
		FVector Cohesion = FVector::ZeroVector;
		FVector Avoidance = FVector::ZeroVector;
		FVector Heading = FVector::ZeroVector;
		FVector Acceleration = FVector::ZeroVector;
		FVector ProbeEnd = FVector::ZeroVector;
		float TargetSpeed = 0.0f;
		float DesiredSpeed = 0.0f;
		bool bHasDesiredSpeed = false;
		bool bThreatened = false;
		bool bIntentionalStop = false;
		bool bSteeringActive = false;
		EBoidCombatMovementConstraint Constraint = EBoidCombatMovementConstraint::None;
	};

	FCollisionQueryParams ObstacleQuery(const FBoidSnapshot& Self)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BoidObstacleProbe), false);
		Params.AddIgnoredActor(Self.Boid.Get());
		Params.AddIgnoredActors(Self.Boid->CollisionSphere->GetMoveIgnoreActors());
		return Params;
	}

	bool HasWallBetween(UWorld* World, const FBoidSnapshot& Self, const FBoidSnapshot& Other)
	{
		FCollisionQueryParams Params = ObstacleQuery(Self);
		Params.AddIgnoredActor(Other.Boid.Get());
		const USphereComponent* Sphere = Self.Boid->CollisionSphere;
		return World->LineTraceTestByChannel(Self.Position, Other.Position, Sphere->GetCollisionObjectType(),
		                                     Params, FCollisionResponseParams(
			                                     Sphere->GetCollisionResponseToChannels()));
	}

	void DrawDirection(UWorld* World, const FVector& Position, const FVector& Direction,
	                   float Length, const FColor& Color)
	{
		if (!Direction.IsNearlyZero())
		{
			DrawDebugDirectionalArrow(World, Position, Position + Direction.GetSafeNormal() * Length,
			                          16.0f, Color, false, 0.0f, 0, 2.0f);
		}
	}
}

USwarmSimulationSubsystem::USwarmSimulationSubsystem() = default;

void USwarmSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CollisionQueries.Initialize(GetWorld());
	RangedPositionPlanner.Reset(new FBoidRangedPositionPlanner());
	CombatCoordinator.Reset(new FBoidCombatMovementCoordinator());
	RouteService.Reset(new FBoidFlightRouteService());
	SteeringService = MakeUnique<FBoidSteeringService>();
	CopyProjectSettings();
}

bool USwarmSimulationSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool USwarmSimulationSubsystem::IsTickable() const
{
	// Do not gate on bounds: queued removals must still be processed while suspended.
	return !IsTemplate() && IsInitialized() && bWorldReady && !bEndingPlay;
}

TStatId USwarmSimulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwarmSimulationSubsystem, STATGROUP_Tickables);
}

void USwarmSimulationSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	bWorldReady = true;
	NotifySetupChanged();
	RefreshSetup();
}

void USwarmSimulationSubsystem::OnWorldEndPlay(UWorld& World)
{
	Shutdown();
	Super::OnWorldEndPlay(World);
}

void USwarmSimulationSubsystem::Deinitialize()
{
	bReleaseServices = true;
	Shutdown();
	if (!bFrameActive)
	{
		RouteService.Reset();
		RangedPositionPlanner.Reset();
		CombatCoordinator.Reset();
		SteeringService.Reset();
	}
	Super::Deinitialize();
}

void FBoidRangedPlannerDeleter::operator()(FBoidRangedPositionPlanner* Planner) const
{
	delete Planner;
}

void FBoidCombatCoordinatorDeleter::operator()(FBoidCombatMovementCoordinator* Coordinator) const
{
	delete Coordinator;
}

UBoidSteeringProfile* USwarmSimulationSubsystem::ResolveSteeringProfile(
	const AFlyingBoid* Boid, UBoidSteeringProfile* Preferred) const
{
	if (IsValid(Preferred)) return Preferred;
	if (IsValid(Boid) && IsValid(Boid->GetSteeringProfile())) return Boid->GetSteeringProfile();
	if (IsValid(DefaultSteeringProfile)) return DefaultSteeringProfile.Get();
	return nullptr;
}

USwarmSimulationSubsystem::~USwarmSimulationSubsystem() = default;

void USwarmSimulationSubsystem::UpdateRangedPositions(float DeltaTime)
{
	PlannerAgents.Reset(Agents.Num());
	for (const FRegisteredAgent& Agent : Agents) PlannerAgents.Add({Agent.Boid.Get(), Agent.Id});
	PlannerAgents.Sort([](const FBoidPlannerAgent& A, const FBoidPlannerAgent& B) { return A.Id < B.Id; });
	RouteService->BeginFrame(this, PlannerAgents);
	RouteService->Tick(this, PlannerAgents);
	RangedPositionPlanner->Update(*this, PlannerAgents, DeltaTime);
	CombatCoordinator->Update(*this, PlannerAgents, *RangedPositionPlanner, DeltaTime);
	UpdateNavigationGoals(DeltaTime);
	CombatCoordinator->FinalizeNavigation(*this, PlannerAgents, DeltaTime);
}

void USwarmSimulationSubsystem::Shutdown()
{
	bEndingPlay = true;
	bWorldReady = false;
	OnPostSimulation.Clear();
	if (!bFrameActive) FinishShutdown();
}

void USwarmSimulationSubsystem::FinishShutdown()
{
	if (bShutdownComplete) return;
	bShutdownComplete = true;
	CollisionQueries.Reset();
	DebugObservers.Reset();
	DebugMovementSamples.Reset();
	// Stop logic while associations still identify this runner. Task exits cannot
	// reactivate a dead boid, and no queued request can restart a shutting-down runner.
	for (const FRegisteredAgent& Agent : Agents)
	{
		if (AFlyingBoid* Boid = Agent.Boid.Get())
		{
			if (IsValid(Boid->StateTree) && Boid->StateTree->IsRunning())
			{
				Boid->StateTree->StopLogic(TEXT("Runner EndPlay"));
			}
			Boid->SetWanderActive(false);
			Boid->ClearSimulationSubsystem(this);
		}
	}
	for (const TWeakObjectPtr<AFlyingBoid>& Pending : PendingAdditions)
	{
		if (AFlyingBoid* Boid = Pending.Get())
		{
			Boid->ClearSimulationSubsystem(this);
		}
	}
	for (const FRegisteredAgent& Agent : Agents)
	{
		RouteService->ResetAgent(Agent.Id);
		RangedPositionPlanner->ResetAgent(Agent.Id);
		CombatCoordinator->ResetAgent(Agent.Id);
	}
	Agents.Reset();
	AgentIds.Reset();
	AgentRecordIndices.Reset();
	PendingAdditions.Reset();
	PendingRemovals.Reset();
	WanderBounds = nullptr;
	BoundsSource.Reset();
	NavigationAgents.Reset();
	PlannerAgents.Reset();
	NavigationTime = 0.;
	if (SteeringService) SteeringService->Reset();
	ReportedSteeringProblems.Reset();
}

void USwarmSimulationSubsystem::RegisterBoid(AFlyingBoid* Boid)
{
	if (bEndingPlay || !IsValid(Boid) || Boid->GetWorld() != GetWorld() || !Boid->HasActorBegunPlay() || !Boid->
		IsAlive() || !Boid->AssociateSimulationSubsystem(this))
	{
		return;
	}
	const TWeakObjectPtr<AFlyingBoid> WeakBoid(Boid);
	CollisionQueries.RefreshBoid(Boid);
	PendingRemovals.Remove(WeakBoid);
	if (GetAgentId(Boid) != INDEX_NONE) CollisionQueries.RegisterMovementBody(Boid);
	if (GetAgentId(Boid) == INDEX_NONE && !PendingAdditions.Contains(WeakBoid))
	{
		PendingAdditions.Add(WeakBoid);
	}
}

void USwarmSimulationSubsystem::UnregisterBoid(AFlyingBoid* Boid)
{
	if (!Boid || Boid->GetSimulationSubsystem() != this)
	{
		return;
	}
	const TWeakObjectPtr<AFlyingBoid> WeakBoid(Boid);
	CollisionQueries.UnregisterMovementBody(Boid);
	PendingAdditions.Remove(WeakBoid);
	if (GetAgentId(Boid) == INDEX_NONE)
	{
		Boid->ClearSimulationSubsystem(this);
		return;
	}
	if (!PendingRemovals.Contains(WeakBoid))
	{
		PendingRemovals.Add(WeakBoid);
	}
}

int32 USwarmSimulationSubsystem::GetAgentId(const AFlyingBoid* Boid) const
{
	if (!IsValid(Boid)) return INDEX_NONE;
	const int32* Id = AgentIds.Find(TWeakObjectPtr<AFlyingBoid>(const_cast<AFlyingBoid*>(Boid)));
	return Id ? *Id : INDEX_NONE;
}

void USwarmSimulationSubsystem::GetDebugAgents(TArray<FSwarmDebugAgent>& OutAgents) const
{
	OutAgents.Reset(Agents.Num());
	for (const FRegisteredAgent& Agent : Agents)
	{
		AFlyingBoid* Boid = Agent.Boid.Get();
		if (!IsValid(Boid) || !Boid->IsAlive() || Boid->GetWorld() != GetWorld() || !IsAgentRegistered(Boid)) continue;
		OutAgents.Add({Agent.Id, Agent.Boid});
	}
	OutAgents.Sort([](const FSwarmDebugAgent& A, const FSwarmDebugAgent& B) { return A.Id < B.Id; });
}

void USwarmSimulationSubsystem::AcquireDebugPresentation()
{
	if (DebugPresentationUsers < MAX_int32) ++DebugPresentationUsers;
}

void USwarmSimulationSubsystem::ReleaseDebugPresentation()
{
	DebugPresentationUsers = FMath::Max(0, DebugPresentationUsers - 1);
}

void USwarmSimulationSubsystem::RebuildAgentLookups()
{
	AgentIds.Reset();
	AgentRecordIndices.Reset();
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		AgentIds.Add(Agents[Index].Boid, Agents[Index].Id);
		AgentRecordIndices.Add(Agents[Index].Id, Index);
	}
}

USwarmSimulationSubsystem::FRegisteredAgent* USwarmSimulationSubsystem::FindAgentRecord(int32 AgentId)
{
	const int32* Index = AgentRecordIndices.Find(AgentId);
	return Index && Agents.IsValidIndex(*Index) && Agents[*Index].Id == AgentId ? &Agents[*Index] : nullptr;
}

bool USwarmSimulationSubsystem::IsInsideSafeBounds(const FVector& Position, float BoidRadius) const
{
	if (!CanSimulate() || Position.ContainsNaN() || !FMath::IsFinite(BoidRadius) || BoidRadius < 0.f) return false;
	const FVector SafeExtent = WanderBounds->GetScaledBoxExtent()
		- FVector(BoidRadius + FMath::Max(0.0f, Tuning.BoundsMarginCm));
	if (SafeExtent.GetMin() <= 0.0f)
	{
		return false;
	}
	const FVector Local = WanderBounds->GetComponentTransform().InverseTransformPositionNoScale(Position);
	return FMath::Abs(Local.X) <= SafeExtent.X && FMath::Abs(Local.Y) <= SafeExtent.Y
		&& FMath::Abs(Local.Z) <= SafeExtent.Z;
}

void USwarmSimulationSubsystem::ApplyQueuedRegistrations()
{
	TSet<TWeakObjectPtr<AFlyingBoid>> Removals;
	for (const auto& Weak : PendingRemovals) Removals.Add(Weak);
	PendingRemovals.Reset();
	// Detach records before invoking StateTree exits, which can destroy actors or end the world.
	TArray<FRegisteredAgent> RemovedAgents;
	TSet<int32> RemovedIds;
	for (const FRegisteredAgent& Agent : Agents)
	{
		if (!Agent.Boid.IsValid() || !Agent.Boid->IsAlive() || Removals.Contains(Agent.Boid)) { RemovedAgents.Add(Agent); RemovedIds.Add(Agent.Id); }
	}
	if (!RemovedAgents.IsEmpty())
	{
		Agents.RemoveAll([&RemovedIds](const FRegisteredAgent& Agent) { return RemovedIds.Contains(Agent.Id); });
		RebuildAgentLookups();
	}
	for (const FRegisteredAgent& Removed : RemovedAgents)
	{
		if (bEndingPlay) return;
		RouteService->ResetAgent(Removed.Id);
		RangedPositionPlanner->ResetAgent(Removed.Id);
		CombatCoordinator->ResetAgent(Removed.Id);
		NavigationAgents.Remove(Removed.Id);
		AFlyingBoid* Boid = Removed.Boid.Get();
		if (!Boid) continue;
		CollisionQueries.UnregisterMovementBody(Boid);
		if (IsValid(Boid->AttackComponent)) Boid->AttackComponent->DeactivateCombatContext(0);
		Boid->SetWanderActive(false);
		if (IsValid(Boid->StateTree) && Boid->StateTree->IsRunning()) Boid->StateTree->StopLogic(TEXT("Unregistered"));
		if (IsValid(Boid)) Boid->ClearSimulationSubsystem(this);
	}
	if (!CanSimulate()) return;
	if (!bInitialBatchCommitted)
	{
		PendingAdditions.Sort([](const TWeakObjectPtr<AFlyingBoid>& A, const TWeakObjectPtr<AFlyingBoid>& B)
		{
			return A.IsValid() && (!B.IsValid() || A->GetPathName() < B->GetPathName());
		});
		bInitialBatchCommitted = true;
	}
	TArray<TWeakObjectPtr<AFlyingBoid>> Additions = MoveTemp(PendingAdditions);
	PendingAdditions.Reset();
	for (const TWeakObjectPtr<AFlyingBoid>& WeakBoid : Additions)
	{
		AFlyingBoid* Boid = WeakBoid.Get();
		if (!IsValid(Boid) || !Boid->IsAlive() || Boid->GetSimulationSubsystem() != this || GetAgentId(Boid) !=
			INDEX_NONE)
		{
			if (IsValid(Boid) && !Boid->IsAlive())
			{
				Boid->ClearSimulationSubsystem(this);
			}
			continue;
		}
		FRegisteredAgent& Agent = Agents.AddDefaulted_GetRef();
		Agent.Boid = WeakBoid;
		Agent.Id = NextAgentId++;
		AgentIds.Add(WeakBoid, Agent.Id);
		AgentRecordIndices.Add(Agent.Id, Agents.Num() - 1);
		Boid->InitializeWander(Agent.Id, Tuning.InitialSeed);
		if (Tuning.CruiseSpeedCmPerSec <= 0.0f || Boid->MaxSpeedCmPerSec <= 0.0f
			|| Boid->MaxAccelerationCmPerSec2 * Tuning.AccelerationScale <= 0.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("Boid %s cannot cruise with zero speed or acceleration settings."),
			       *Boid->GetName());
		}
		CollisionQueries.RegisterMovementBody(Boid);
		const FVector SafeExtent = WanderBounds->GetScaledBoxExtent()
			- FVector(Boid->CollisionSphere->GetScaledSphereRadius() + FMath::Max(0.0f, Tuning.BoundsMarginCm));
		if (SafeExtent.GetMin() <= BoidFlight::ContactSkinCm)
		{
			UE_LOG(LogTemp, Error,
			       TEXT("WanderBounds cannot contain boid %s; enlarge the bounds or reduce its radius."),
			       *Boid->GetName());
		}
		else if (!IsInsideSafeBounds(Boid->GetActorLocation(), Boid->CollisionSphere->GetScaledSphereRadius()))
		{
			UE_LOG(LogTemp, Warning, TEXT("Boid %s starts outside WanderBounds; it will be pulled back inside."),
			       *Boid->GetName());
		}
	}
}

void USwarmSimulationSubsystem::InitializePendingLogic()
{
	TArray<TWeakObjectPtr<AFlyingBoid>> ToStart;
	for (FRegisteredAgent& Agent : Agents)
	{
		if (!Agent.bLogicStarted && IsAgentRegistered(Agent.Boid.Get()))
		{
			ToStart.Add(Agent.Boid);
		}
	}
	for (const auto& Weak : ToStart)
	{
		AFlyingBoid* Boid = Weak.Get();
		if (!CanSimulate()) break;
		if (!IsAgentRegistered(Boid)) continue;
		if (FRegisteredAgent* Agent = FindAgentRecord(GetAgentId(Boid))) Agent->bLogicStarted = true;
		if (IsValid(Boid->StateTree)) Boid->StateTree->StartLogic();
	}
}

void USwarmSimulationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFrameActive || bEndingPlay) return;
	PruneDebugObservations();
	LastFrameNeighborCellsVisited = LastFrameNeighborSamplesExamined = LastFrameNeighborPairChecks = 0;
	LastFrameNeighborWallTraces = LastFrameAcceptedNeighbors = LastFrameMovementSubsteps = 0;
	LastFrameProjectileLaunches = LastFrameVisibilityQueryRebuilds = 0;
	LastFrameAvoidanceCandidates = LastFrameClearanceProbes = LastFrameMandatoryClearanceRequests = LastFrameRecoveryClearanceProbes = 0;
	LastFramePlannerSpacingScores = LastFramePlannerSamplesExamined = LastFramePlannerReservationChecks = 0;
	CollisionQueries.Refresh();
	CollisionExceptionActors = CollisionQueries.GetExceptionCount();
	LiveAttackProjectiles = CollisionQueries.GetProjectileCount();
	bFrameActive = true;
	ON_SCOPE_EXIT
	{
		bFrameActive = false;
		if (bEndingPlay) FinishShutdown();
		if (bReleaseServices)
		{
			RouteService.Reset();
			RangedPositionPlanner.Reset();
			CombatCoordinator.Reset();
			SteeringService.Reset();
		}
	};
	RefreshSetup();
	ApplyQueuedRegistrations();
	if (!CanSimulate())
	{
		for (const FRegisteredAgent& Agent : Agents)
		{
			if (AFlyingBoid* Boid = Agent.Boid.Get())
			{
				Boid->PublishPositioning(FBoidPositioningFacts(), false);
				Boid->PublishCombatMovement(FBoidCombatMovementFacts());
				if (IsValid(Boid->AttackComponent)) Boid->AttackComponent->SuspendSimulation();
			}
		}
		LastFrameRouteQueries = LastFrameRouteExpansions = LastFrameCombatControlQueries = 0;
		LastFrameCandidates = LastFramePositioningQueries = 0;
		LastFramePositioningMilliseconds = 0.f;
		return;
	}
	InitializePendingLogic();
	if (!CanSimulate()) return;
	if (DeltaTime <= 0.0f || !FMath::IsFinite(DeltaTime))
	{
		return;
	}
	if (bDrawBounds)
	{
		DrawDebugBox(GetWorld(), WanderBounds->GetComponentLocation(), WanderBounds->GetScaledBoxExtent(),
		             WanderBounds->GetComponentQuat(), FColor::Cyan, false, 0.0f, 0, 1.0f);
	}
	const float MaxStep = FMath::Max(0.001f, Tuning.MaxStepSeconds);
	ApplyQueuedRegistrations();
	UpdateRangedPositions(DeltaTime);
	float RemainingTime = FMath::Min(DeltaTime, FMath::Max(0.001f, Tuning.MaxFrameSeconds));
	const int32 MaxSteps = FMath::Clamp(Tuning.MaxSubsteps, 1, 16);
	for (int32 Step = 0; Step < MaxSteps && RemainingTime > UE_SMALL_NUMBER && CanSimulate(); ++Step)
	{
		const float StepTime = FMath::Min(RemainingTime, MaxStep);
		AdvanceStep(StepTime);
		RemainingTime -= StepTime;
	}
	if (!CanSimulate()) return;
	TArray<FBoidPlannerAgent> ReadinessAgents;
	for (const FRegisteredAgent& Agent : Agents) ReadinessAgents.Add({Agent.Boid.Get(), Agent.Id});
	CombatCoordinator->ValidateReadinessAfterMovement(*this, ReadinessAgents,
	                                                  FMath::Min(DeltaTime, FMath::Max(.001f, Tuning.MaxFrameSeconds)));
	AFlyingBoid* Selected = IsValid(DebugBoid) ? DebugBoid.Get() : TestBoid.Get();
	if (bDrawPositioning && !IsDebugPresentationSuppressed() && IsValid(Selected) && Selected->IsAlive())
	{
		const FBoidCombatMovementFacts Facts = Selected->GetCombatMovementFacts();
		const auto PhaseName = [](EBoidNavigationPhase Phase)
		{
			return StaticEnum<EBoidNavigationPhase>()->GetNameStringByValue(static_cast<int64>(Phase));
		};
		const FString Text = FString::Printf(
			TEXT(
				"%s\nrequested %s / executed %s / status %s\nrange %.0f ideal %.0f band %.0f-%.0f orbit %d retreat %d\nselection %d candidates %d/%d rebases %d stale %d\nhold %s %.2fs / speed %.0f desired %.0f / %s\n%s\nprofile %s\ntree %s"),
			*Facts.BlueprintClass,
			*StaticEnum<EBoidRangedPhaseRequest>()->GetNameStringByValue(static_cast<int64>(Facts.RequestedPhase)),
			*PhaseName(Facts.ExecutedPhase),
			*StaticEnum<EBoidCombatControlStatus>()->GetNameStringByValue(static_cast<int64>(Facts.Status)),
			Facts.TargetRangeCm, Facts.DesiredRangeCm, Facts.MinimumRangeCm, Facts.MaximumRangeCm, Facts.bOrbiting,
			Facts.bRetreating,
			Facts.SelectionVersion, Facts.CandidatesEvaluated, Facts.CandidateCount, Facts.Rebases, Facts.StaleResults,
			*StaticEnum<EBoidCombatHoldReason>()->GetNameStringByValue(static_cast<int64>(Facts.HoldReason)),
			Facts.HoldSeconds,
			Facts.ActualSpeedCmPerSec, Facts.DesiredSpeedCmPerSec, *Facts.MovementReason, *Facts.Detail,
			*Facts.EffectiveProfile, *Facts.StateTreeAsset);
		DrawDebugString(GetWorld(), Selected->GetActorLocation() + FVector(0, 0, 130), Text, nullptr, FColor::White,
		                0.f);
	}

	OnPostSimulation.Broadcast(DeltaTime);
}

void USwarmSimulationSubsystem::AdvanceStep(float StepTime)
{
	if (!CanSimulate()) return;
	ApplyQueuedRegistrations();
	++LastFrameMovementSubsteps;
	BeginDebugMovementStep(StepTime);
	ON_SCOPE_EXIT { FinishDebugMovementStep(); };
	TArray<FBoidSnapshot> Snapshots;
	Snapshots.Reserve(Agents.Num());
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SwarmSnapshotCapture);
		for (const FRegisteredAgent& Agent : Agents)
		{
			AFlyingBoid* Boid = Agent.Boid.Get();
			if (!Agent.bLogicStarted || !IsValid(Boid) || !Boid->IsAlive() || !IsAgentRegistered(Boid))
			{
				continue;
			}
			const FVector UnconstrainedPosition = Boid->GetActorLocation();
			if (Boid->IsSteeringActive())
			{
				Boid->ConstrainWanderToBounds(WanderBounds, Tuning.BoundsMarginCm);
			}
			if (!CanSimulate()) return;
			if (!IsAgentRegistered(Boid)) continue;
			FBoidSnapshot& Snapshot = Snapshots.AddDefaulted_GetRef();
			Snapshot.Boid = Boid;
			Snapshot.Position = Boid->GetActorLocation();
			if (FSwarmMovementDebugSnapshot* Debug = DebugMovementSamples.Find(Agent.Id)) Debug->BoundsCorrection = Snapshot.Position - Debug->StartPosition;
			Snapshot.Velocity = Boid->GetFlightVelocity();
			Snapshot.Heading = Boid->GetFlightHeading();
			Snapshot.Radius = Boid->CollisionSphere->GetScaledSphereRadius();
			Snapshot.Id = Agent.Id;
			Snapshot.bSteeringActive = Boid->IsSteeringActive();
			Snapshot.MovementIntent = Boid->GetMovementIntent();
			Snapshot.IntentOwner = Boid->GetMovementIntentOwner();
			Snapshot.TransitMemoryRevision = Snapshot.MovementIntent.bIsTransit ? GetTransitGoalRevision(Boid) : 0;
			Snapshot.bBoundsCorrected = !Snapshot.Position.Equals(UnconstrainedPosition, .001f);
			Snapshot.Goal = Snapshot.MovementIntent.Goal;
			Snapshot.bHasGoal = Snapshot.MovementIntent.bHasGoal;
			if (IsValid(Boid->MovementTarget))
			{
				Snapshot.TargetPosition = Boid->MovementTarget->GetActorLocation();
				Snapshot.TargetActor = Boid->MovementTarget;
				const AFlyingCharacter* Player = Cast<AFlyingCharacter>(Boid->MovementTarget.Get());
				const AFlyingBoid* TargetBoid = Cast<AFlyingBoid>(Boid->MovementTarget.Get());
				Snapshot.bHasTarget = (!Player || Player->IsAlive()) && (!TargetBoid || TargetBoid->IsAlive());
			}
			Snapshot.bCoordinatedCombat = Snapshot.MovementIntent.Policy == EBoidMovementPolicy::RangedOrbit;
			const bool UsesNavigation = Snapshot.bCoordinatedCombat
				|| Snapshot.MovementIntent.bIsTransit
				|| (NavigationMode == EBoidNavigationMode::AuthoredRoutes
					&& (Snapshot.MovementIntent.Policy == EBoidMovementPolicy::FollowStandOff || Snapshot.MovementIntent.
						Policy == EBoidMovementPolicy::LocationArrival));
			if (UsesNavigation)
			{
				const FBoidNavigationGoal Navigation = GetNavigationGoal(Boid);
				Snapshot.bPlannerControlled = true;
				Snapshot.MovementIntent.Policy = EBoidMovementPolicy::LocationArrival;
				Snapshot.Goal = Navigation.Goal;
				Snapshot.bHasGoal = Navigation.bHasGoal;
				Snapshot.bNavigationHold = Navigation.bHoldPosition;
				Snapshot.bContinuousGoal = Navigation.bContinuousGoal;
				Snapshot.bPreferNavigationGoal = Navigation.bPreferNavigationGoal;
				Snapshot.NavigationPhase = Navigation.Phase;
				Snapshot.OrbitContext = Navigation.OrbitContext;
				Snapshot.DesiredSpeed = Navigation.DesiredSpeed;
			}
			if (FSwarmMovementDebugSnapshot* Debug = DebugMovementSamples.Find(Agent.Id))
			{
				Debug->bHasResolvedPolicy = true;
				Debug->ResolvedPolicy = Snapshot.MovementIntent.Policy;
				Debug->bHasGoal = Snapshot.bHasGoal;
				Debug->GoalDistance = Snapshot.bHasGoal ? FVector::Distance(Snapshot.Position, Snapshot.Goal) : 0.f;
				Debug->ArrivalRadius = Snapshot.MovementIntent.ArrivalRadiusCm;
				Debug->Stage = Snapshot.bSteeringActive ? TEXT("AWAITING COMMIT") : TEXT("STEERING DISABLED");
			}
		}
	}

	for (FBoidSnapshot& Snapshot : Snapshots)
	{
		const AFlyingBoid* Boid = Snapshot.Boid.Get();
		const USphereComponent* Sphere = Boid->CollisionSphere;
		FCollisionQueryParams Params = ObstacleQuery(Snapshot);
		const FCollisionResponseParams Responses(Sphere->GetCollisionResponseToChannels());
		auto IsDirectGoalClear = [&](const FVector& Goal)
		{
			if (Goal.ContainsNaN() || !IsInsideSafeBounds(Goal, Snapshot.Radius))
			{
				return false;
			}
			const FCollisionShape Shape = FCollisionShape::MakeSphere(
				Snapshot.Radius + FMath::Max(0.0f, Tuning.ObstacleMarginCm));
			return !GetWorld()->SweepTestByChannel(Snapshot.Position, Goal, FQuat::Identity,
			                                       Sphere->GetCollisionObjectType(), Shape, Params, Responses)
				&& !GetWorld()->OverlapBlockingTestByChannel(Goal, FQuat::Identity,
				                                             Sphere->GetCollisionObjectType(), Shape, Params,
				                                             Responses);
		};
		if (Snapshot.MovementIntent.Policy == EBoidMovementPolicy::LocationArrival)
		{
			// Transit and authored route sweeps are owned by the frame-budgeted service. Local
			// Transit can seek a blocked destination; obstacle probes and swept movement protect each step.
			Snapshot.bDestinationBlocked = !Snapshot.bHasGoal
				|| ((Snapshot.MovementIntent.bIsTransit || NavigationMode == EBoidNavigationMode::AuthoredRoutes) && Snapshot.bPlannerControlled
					    ? !IsAllowedFlightSegment(Snapshot.Position, Snapshot.Goal,
					                              Snapshot.Radius + FMath::Max(0.f, Tuning.ObstacleMarginCm))
					    : !IsDirectGoalClear(Snapshot.Goal));
		}
		FRegisteredAgent* Record = FindAgentRecord(Snapshot.Id);
		if (!Record)
		{
			continue;
		}
		if (Snapshot.MovementIntent.Policy != EBoidMovementPolicy::FollowStandOff || !Snapshot.bHasTarget
			|| Record->RetreatTarget != Snapshot.TargetActor)
		{
			Record->bRetreating = false;
		}
		if (Snapshot.MovementIntent.Policy == EBoidMovementPolicy::FollowStandOff && Snapshot.bHasTarget)
		{
			const float Distance = FVector::Distance(Snapshot.Position, Snapshot.TargetPosition);
			Params.AddIgnoredActor(Snapshot.TargetActor.Get());
			if (Record->bRetreating && (FVector::Distance(Snapshot.Position, Record->RetreatGoal)
				<= FMath::Max(0.0f, Snapshot.MovementIntent.ArrivalRadiusCm)
				|| Distance >= Snapshot.MovementIntent.StandOffDistanceCm || !IsDirectGoalClear(Record->RetreatGoal)))
			{
				Record->bRetreating = false;
			}
			if (!Record->bRetreating && Distance < FMath::Max(0.0f, Snapshot.MovementIntent.MinimumTargetDistanceCm))
			{
				FVector Away = (Snapshot.Position - Snapshot.TargetPosition).GetSafeNormal();
				if (Away.IsNearlyZero())
				{
					Away = -Snapshot.Heading;
				}
				const float RetreatDistance = FMath::Max(Snapshot.MovementIntent.StandOffDistanceCm,
				                                         Snapshot.MovementIntent.MinimumTargetDistanceCm) + Snapshot.
					MovementIntent.ArrivalRadiusCm;
				const FVector RetreatGoal = Snapshot.TargetPosition + Away * RetreatDistance;
				// Ignore the target only for goal validation; actual swept movement
				// continues to use configured collision and shared separation.
				Params.AddIgnoredActor(Snapshot.TargetActor.Get());
				if (IsDirectGoalClear(RetreatGoal))
				{
					Record->RetreatGoal = RetreatGoal;
					Record->RetreatTarget = Snapshot.TargetActor;
					Record->bRetreating = true;
				}
			}
			if (Record->bRetreating)
			{
				Snapshot.Goal = Record->RetreatGoal;
				Snapshot.bHasGoal = true;
				Snapshot.bRetreating = true;
			}
		}
	}

	TArray<FSteeringOutput> Outputs;
	Outputs.Reserve(Snapshots.Num());
	const int32 AvoidanceLimit = CVarExhaustiveAvoidance.GetValueOnGameThread() != 0 ? 0 : FMath::Max(1, Tuning.MaxAvoidanceCandidates);
	TArray<FBoidSteeringNeighbor> SteeringSnapshots;
	SteeringSnapshots.Reserve(Snapshots.Num());
	for (const FBoidSnapshot& Snapshot : Snapshots) SteeringSnapshots.Add({Snapshot.Position, Snapshot.Velocity, Snapshot.Radius, Snapshot.Id, Snapshot.bSteeringActive});
	SteeringService->BeginStep(SteeringSnapshots, Tuning.NeighborCellSizeCm, CVarUseSpatialNeighbors.GetValueOnGameThread() != 0);
	for (int32 SelfIndex = 0; SelfIndex < Snapshots.Num(); ++SelfIndex)
	{
		const FBoidSnapshot& Self = Snapshots[SelfIndex];
		FSteeringOutput& Output = Outputs.AddDefaulted_GetRef();
		if (!Self.bSteeringActive) continue;
		AFlyingBoid* Boid = Self.Boid.Get();
		Output.bSteeringActive = true;
		UBoidSteeringProfile* Profile = ResolveSteeringProfile(Boid);
		if (FSwarmMovementDebugSnapshot* Debug = DebugMovementSamples.Find(Self.Id)) Debug->EffectiveProfile = GetNameSafe(Profile);
		Boid->PrepareSteeringRuntime(Profile);
		FBoidSteeringInput SteeringInput;
		SteeringInput.Position = Self.Position;
		SteeringInput.Velocity = Self.Velocity;
		SteeringInput.Heading = Self.Heading;
		SteeringInput.TargetPosition = Self.TargetPosition;
		SteeringInput.bHasTarget = Self.bHasTarget;
		SteeringInput.Goal = Self.Goal;
		SteeringInput.bHasGoal = Self.bHasGoal && !Self.bDestinationBlocked;
		SteeringInput.bContinuousGoal = Self.bContinuousGoal;
		SteeringInput.NavigationPhase = Self.NavigationPhase;
		SteeringInput.OrbitContext = Self.OrbitContext;
		SteeringInput.bPreferNavigationGoal = Self.bPreferNavigationGoal || Self.bRetreating;
		SteeringInput.FlightSpeedLimitCmPerSec = FMath::Min(FMath::Max(0.f, Tuning.CruiseSpeedCmPerSec),
		                                                    FMath::Max(0.f, Boid->MaxSpeedCmPerSec));
		SteeringInput.bCoordinatedCombat = Self.bCoordinatedCombat;
		SteeringInput.bHasDesiredSpeed = Self.NavigationPhase == EBoidNavigationPhase::Orbit || Self.NavigationPhase
			== EBoidNavigationPhase::Retreat;
		SteeringInput.DesiredSpeedCmPerSec = Self.DesiredSpeed;
		SteeringInput.DeltaTime = StepTime;
		SteeringInput.AgentId = Self.Id;
		SteeringInput.Radius = Self.Radius;
		const FBoidSteeringServiceResult Steering = SteeringService->Evaluate(SelfIndex, SteeringInput, Profile,
			[Boid](int32 Index) -> FBoidSteeringRuntimeState& { return Boid->GetSteeringRuntimeState(Index); },
			[&](int32 Index) { return HasWallBetween(GetWorld(), Self, Snapshots[Index]); });
		LastFrameNeighborCellsVisited += Steering.QueryStats.CellsVisited;
		LastFrameNeighborSamplesExamined += Steering.QueryStats.SamplesExamined;
		LastFrameNeighborPairChecks += Steering.PairChecks;
		LastFrameNeighborWallTraces += Steering.WallTraces;
		LastFrameAcceptedNeighbors += Steering.AcceptedNeighbors;
		Output.ProfileIntent = Steering.ProfileIntent;
		Output.Separation = Steering.Separation;
		Output.Alignment = Steering.Alignment;
		Output.Cohesion = Steering.Cohesion;
		Output.DesiredSpeed = Steering.DesiredSpeed;
		Output.bHasDesiredSpeed = Steering.bHasDesiredSpeed;
		const bool bCriticalSeparation = Steering.bCriticalSeparation;
		FVector Desired = Steering.Desired;
		if (!Steering.ConfigurationProblem.IsEmpty())
		{
			const FString Problem = GetPathNameSafe(Profile) + TEXT(": ") + Steering.ConfigurationProblem;
			if (!ReportedSteeringProblems.Contains(Problem)) { ReportedSteeringProblems.Add(Problem); UE_LOG(LogTemp, Error, TEXT("Invalid steering profile %s"), *Problem); }
		}
		const float CruiseSpeed = FMath::Min(FMath::Max(0.0f, Tuning.CruiseSpeedCmPerSec),
		                                     FMath::Max(0.0f, Boid->MaxSpeedCmPerSec));
		const float Brake = FMath::Max(1.0f, Boid->BrakingAccelerationCmPerSec2 * Tuning.BrakingScale);
		const float Speed = Self.Velocity.Size();
		const float MinLookahead = FMath::Max(1.0f, FMath::Min(Tuning.MinLookaheadCm, Tuning.MaxLookaheadCm));
		const float MaxLookahead = FMath::Max(MinLookahead, FMath::Max(Tuning.MinLookaheadCm, Tuning.MaxLookaheadCm));
		const float SafetyLookahead = FMath::Clamp(Speed * .6f + Speed * Speed / (2.f * Brake), MinLookahead, MaxLookahead);
		const float OptionalLookahead = Steering.Avoidance ? FMath::Clamp(Speed * .6f + Speed * Speed / (2.f * Brake), Steering.Avoidance->MinLookaheadCm, Steering.Avoidance->MaxLookaheadCm) : SafetyLookahead;
		// One cached query per direction serves both independent horizons.
		const float Lookahead = FMath::Max(SafetyLookahead, OptionalLookahead);
		const FCollisionQueryParams QueryParams = ObstacleQuery(Self);
		const USphereComponent* Sphere = Self.Boid->CollisionSphere;
		const FCollisionResponseParams Responses(Sphere->GetCollisionResponseToChannels());
		struct FCachedProbe { FVector Direction; float Clearance; };
		TArray<FCachedProbe, TInlineAllocator<32>> ProbeCache;
		auto Probe = [&](const FVector& Direction)
		{
			const FVector Unit = Direction.GetSafeNormal();
			for (const FCachedProbe& Cached : ProbeCache) if (Cached.Direction == Unit) return Cached.Clearance;
			TRACE_CPUPROFILER_EVENT_SCOPE(SwarmAvoidanceClearance);
			++LastFrameClearanceProbes;
			const float Clearance = BoidFlight::Clearance(GetWorld(), WanderBounds, Self.Position, Self.Radius, Unit, Lookahead, Tuning.BoundsMarginCm, Tuning.ObstacleMarginCm, Sphere->GetCollisionObjectType(), QueryParams, Responses, &ClearanceHits);
			ProbeCache.Add({Unit, Clearance});
			return Clearance;
		};
		const FVector Forward = Speed > 1.0f ? Self.Velocity / Speed : Self.Heading;
		LastFrameMandatoryClearanceRequests += 2;
		const float ForwardClearance = FMath::Min(Probe(Forward), SafetyLookahead);
		const float IntentClearance = FMath::Min(Probe(Desired), SafetyLookahead);
		Output.ProbeEnd = Self.Position + Forward * ForwardClearance;
		Output.bThreatened = FMath::Min(ForwardClearance, IntentClearance) < SafetyLookahead - BoidFlight::ContactSkinCm;
		const bool bOptionalThreatened = Steering.Avoidance && FMath::Min(Probe(Forward), Probe(Desired)) < OptionalLookahead - BoidFlight::ContactSkinCm;
		float ChosenClearance = IntentClearance;
		float ThreatenedSpeedScale = FMath::Clamp(ChosenClearance / SafetyLookahead, .25f, 1.f);
		bool bAvoidanceBlocked = false;
		if (Steering.Avoidance && (bOptionalThreatened || bCriticalSeparation))
		{
			auto OptionalProbe = [&](const FVector& Direction) { return FMath::Min(Probe(Direction), OptionalLookahead); };
			const FBoidAvoidanceResult Avoidance = SteeringService->ApplyAvoidance(Steering, Boid->GetSteeringRuntimeState(Steering.AvoidanceIndex), Desired, Forward, CruiseSpeed, OptionalLookahead, AvoidanceLimit, OptionalProbe);
			LastFrameAvoidanceCandidates += Avoidance.Candidates;
			bAvoidanceBlocked = Avoidance.bBlocked;
			Desired = Avoidance.Direction;
			Output.Avoidance = Avoidance.Direction;
			ChosenClearance = Avoidance.Clearance;
			ThreatenedSpeedScale = FMath::Clamp(ChosenClearance / OptionalLookahead, .25f, 1.f);
		}
		if (FSwarmMovementDebugSnapshot* Debug = DebugMovementSamples.Find(Self.Id))
		{
			Debug->SteeringConfigurationProblem = Steering.ConfigurationProblem;
			Debug->OptionalSeparation = Steering.Separation;
			Debug->OptionalAlignment = Steering.Alignment;
			Debug->OptionalCohesion = Steering.Cohesion;
			Debug->OptionalAvoidance = Output.Avoidance;
			Debug->bPhysicalOverlapProtection = Steering.bPhysicalOverlap;
		}

		Output.Heading = Boid->TurnFlightHeading(Desired, Tuning.MaxTurnRateDegPerSec, StepTime);
		const bool bEmergency = bCriticalSeparation || (Output.bThreatened
			&& (Speed < 20.0f || ForwardClearance < FMath::Max(Self.Radius + Tuning.ObstacleMarginCm,
			                                                   Speed * Tuning.SteeringResponseSeconds)));
		const FVector SteeringDirection = bEmergency ? Desired : Output.Heading;
		++LastFrameMandatoryClearanceRequests;
		const float SteeringClearance = FMath::Min(Probe(SteeringDirection), SafetyLookahead);
		// Clearance is sphere-center travel; the probe already accounts for radius.
		const float SafeSpeed = FMath::Sqrt(2.0f * Brake * FMath::Max(0.0f,
		                                                              SteeringClearance - BoidFlight::ContactSkinCm));
		Output.TargetSpeed = FMath::Min(CruiseSpeed, SafeSpeed);
		Output.Constraint = SafeSpeed < CruiseSpeed
			                    ? EBoidCombatMovementConstraint::Obstacle
			                    : EBoidCombatMovementConstraint::SpeedLimit;
		const bool bArrivalPolicy = Self.MovementIntent.Policy != EBoidMovementPolicy::Cruise;
		float RemainingDistance = 0.0f;
		if (Self.bRetreating || Self.MovementIntent.Policy == EBoidMovementPolicy::LocationArrival)
		{
			if (Self.bHasGoal && !Self.bDestinationBlocked)
			{
				RemainingDistance = FMath::Max(0.0f, static_cast<float>(FVector::Distance(Self.Position, Self.Goal))
				                               - FMath::Max(0.0f, Self.MovementIntent.ArrivalRadiusCm));
			}
		}
		else if (Self.MovementIntent.Policy == EBoidMovementPolicy::FollowStandOff && Self.bHasTarget)
		{
			RemainingDistance = FMath::Max(
				0.0f, static_cast<float>(FVector::Distance(Self.Position, Self.TargetPosition))
				- FMath::Max(0.0f, Self.MovementIntent.StandOffDistanceCm));
		}
		// Nearby geometry seen at a safe distance must not unseat an arrived boid.
		// Only imminent collision or critical crowding can override deliberate braking.
		const bool bArrivalEmergency = bCriticalSeparation || (Output.bThreatened
			&& ForwardClearance < FMath::Max(Self.Radius + Tuning.ObstacleMarginCm,
			                                 Speed * Tuning.SteeringResponseSeconds));
		if (Output.bHasDesiredSpeed)
		{
			Output.TargetSpeed = FMath::Min(Output.TargetSpeed, Output.DesiredSpeed);
		}
		if (bArrivalPolicy && !bArrivalEmergency && !Self.bContinuousGoal)
		{
			const float ArrivalCap = FMath::Sqrt(2.0f * Brake * RemainingDistance);
			Output.TargetSpeed = FMath::Min(Output.TargetSpeed, ArrivalCap);
			Output.Constraint = EBoidCombatMovementConstraint::Arrival;
			Output.bIntentionalStop = !Self.bDestinationBlocked
				&& (Self.bHasTarget || Self.bHasGoal)
				&& ArrivalCap <= FMath::Max(1.0f, Tuning.MinimumProgressSpeedCmPerSec);
		}
		if (Self.MovementIntent.Policy == EBoidMovementPolicy::FollowStandOff && !Self.bHasTarget)
		{
			// No target means no requested travel; never recover into stale pursuit.
			Output.bIntentionalStop = true;
			if (!bArrivalEmergency)
			{
				Output.TargetSpeed = 0.0f;
			}
		}
		if (Self.bNavigationHold && !bArrivalEmergency)
		{
			Output.TargetSpeed = 0.f;
			Output.bIntentionalStop = true;
			Output.Constraint = EBoidCombatMovementConstraint::Hold;
		}
		if (Output.bThreatened || bOptionalThreatened)
		{
			Output.TargetSpeed = FMath::Min(Output.TargetSpeed, CruiseSpeed * ThreatenedSpeedScale);
		}
		if (bAvoidanceBlocked) { Output.TargetSpeed = 0.0f; Output.Constraint = EBoidCombatMovementConstraint::Obstacle; }
		if (bCriticalSeparation) Output.Constraint = EBoidCombatMovementConstraint::EmergencySeparation;
		if (!Steering.ConfigurationProblem.IsEmpty())
		{
			Output.TargetSpeed = 0.f;
			Output.bIntentionalStop = true;
			Output.Constraint = EBoidCombatMovementConstraint::Hold;
		}
		const float Accel = FMath::Max(0.0f, Boid->MaxAccelerationCmPerSec2 * Tuning.AccelerationScale);
		const float Limit = Output.TargetSpeed < Speed ? FMath::Max(Accel, Brake) : Accel;
		Output.Acceleration = ((SteeringDirection * Output.TargetSpeed - Self.Velocity)
			/ FMath::Max(0.01f, Tuning.SteeringResponseSeconds)).GetClampedToMaxSize(Limit);
	}

	const AFlyingBoid* Selected = IsValid(DebugBoid) ? DebugBoid.Get() : TestBoid.Get();
	for (int32 Index = 0; Index < Snapshots.Num(); ++Index)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SwarmMovementCommit);
		AFlyingBoid* Boid = Snapshots[Index].Boid.Get();
		if (FSwarmMovementDebugSnapshot* Debug = DebugMovementSamples.Find(Snapshots[Index].Id))
		{
			Debug->RequestedSpeed = Outputs[Index].TargetSpeed;
			Debug->Acceleration = Outputs[Index].Acceleration;
			Debug->SteeringHeading = Outputs[Index].Heading;
			Debug->bHasGoal = Snapshots[Index].bHasGoal;
			Debug->GoalDistance = Snapshots[Index].bHasGoal ? FVector::Distance(Snapshots[Index].Position, Snapshots[Index].Goal) : 0.f;
			Debug->Constraint = Outputs[Index].Constraint;
			Debug->bIntentionalStop = Outputs[Index].bIntentionalStop;
			Debug->Stage = Outputs[Index].bSteeringActive ? TEXT("COMMIT SKIPPED") : TEXT("STEERING DISABLED");
		}
		if (!IsValid(Boid) || !Boid->IsAlive() || !Boid->IsSteeringActive()
			|| !IsAgentRegistered(Boid) || !CanSimulate() || !Outputs[Index].bSteeringActive)
		{
			continue;
		}
		const float SpeedLimit = FMath::Min(FMath::Max(0.0f, Tuning.CruiseSpeedCmPerSec),
		                                    FMath::Max(0.0f, Boid->MaxSpeedCmPerSec));
		FSwarmMovementCommitObservation Commit;
		const bool bObserve = DebugMovementSamples.Contains(Snapshots[Index].Id);
		const bool bTrackTransit = Snapshots[Index].MovementIntent.bIsTransit && Snapshots[Index].MovementIntent.Policy == EBoidMovementPolicy::LocationArrival;
		const FVector MovementStart = Boid->GetActorLocation();
		bool bUnobstructedMove = false;
		Boid->ApplyWanderAcceleration(Outputs[Index].Acceleration, StepTime, SpeedLimit, WanderBounds, Tuning.BoundsMarginCm, Tuning.VisualTurnSpeed, bObserve ? &Commit : nullptr, bTrackTransit ? &bUnobstructedMove : nullptr);
		if (bObserve)
		{
			if (FSwarmMovementDebugSnapshot* Debug = DebugMovementSamples.Find(Snapshots[Index].Id))
			{
				Debug->Commit = MoveTemp(Commit);
				Debug->Stage = TEXT("COMMIT EXECUTED");
			}
		}
		if (!IsValid(Boid) || !Boid->IsAlive() || !Boid->IsSteeringActive()
			|| !IsAgentRegistered(Boid) || !CanSimulate())
		{
			continue; // A swept-movement callback may have killed or removed the actor.
		}
		if (bTrackTransit)
		{
			RecordTransitArrival(Boid, Snapshots[Index].IntentOwner, Snapshots[Index].MovementIntent.Revision, Snapshots[Index].TransitMemoryRevision, MovementStart, bUnobstructedMove && !Snapshots[Index].bBoundsCorrected);
		}
		Boid->PublishMovementConstraint(Outputs[Index].Constraint, Boid->GetFlightVelocity().Size(),
		                                StaticEnum<EBoidCombatMovementConstraint>()->GetNameStringByValue(
			                                static_cast<int64>(Outputs[Index].Constraint)));
		float StalledSeconds = 0.0f;
		int32 RecoveryCount = 0;
		if (FRegisteredAgent* Record = FindAgentRecord(Snapshots[Index].Id))
		{
			FRegisteredAgent& Agent = *Record;
			if (Agent.IntentRevision != Snapshots[Index].MovementIntent.Revision
				|| Agent.ProgressTarget != Snapshots[Index].TargetActor
				|| Agent.bProgressRetreating != Snapshots[Index].bRetreating)
			{
				Agent.IntentRevision = Snapshots[Index].MovementIntent.Revision;
				Agent.ProgressTarget = Snapshots[Index].TargetActor;
				Agent.bProgressRetreating = Snapshots[Index].bRetreating;
				Agent.NoProgressSeconds = Agent.ProgressWindowSeconds = Agent.ProgressDistanceCm = 0.0f;
			}
			if (!Outputs[Index].bIntentionalStop && SpeedLimit > 0.0f
				&& Boid->MaxAccelerationCmPerSec2 * Tuning.AccelerationScale > 0.0f)
			{
				if (Agent.ProgressWindowSeconds <= 0.0f)
				{
					Agent.ProgressWindowStart = Snapshots[Index].Position;
					const FBoidSnapshot& Snapshot = Snapshots[Index];
					Agent.bProgressHasDestination = Snapshot.bRetreating
						|| (Snapshot.MovementIntent.Policy == EBoidMovementPolicy::LocationArrival && Snapshot.bHasGoal
							&& !Snapshot.bContinuousGoal)
						|| (Snapshot.MovementIntent.Policy == EBoidMovementPolicy::FollowStandOff && Snapshot.
							bHasTarget);
					Agent.ProgressDestination = (Snapshot.bRetreating
						                            || Snapshot.MovementIntent.Policy ==
						                            EBoidMovementPolicy::LocationArrival)
						                            ? Snapshot.Goal
						                            : Snapshot.TargetPosition;
				}
				Agent.ProgressDistanceCm += FVector::Distance(Snapshots[Index].Position, Boid->GetActorLocation());
				Agent.ProgressWindowSeconds += StepTime;
				if (Agent.ProgressWindowSeconds >= FMath::Max(0.1f, Tuning.ProgressWindowSeconds))
				{
					const float MinimumSpeed = FMath::Min(FMath::Max(0.0f, Tuning.MinimumProgressSpeedCmPerSec),
					                                      SpeedLimit * 0.1f);
					// Net displacement also catches rapid back-and-forth jitter at a
					// contact, which can accumulate path length without escaping it.
					const float NetDistance = FVector::Distance(Agent.ProgressWindowStart, Boid->GetActorLocation());
					float ProgressDistance = FMath::Min(Agent.ProgressDistanceCm, NetDistance);
					if (Agent.bProgressHasDestination)
					{
						// Measure toward a fixed destination captured at the window start;
						// circling and a moving target must not manufacture travel progress.
						const float GoalProgress = FVector::Distance(Agent.ProgressWindowStart,
						                                             Agent.ProgressDestination)
							- FVector::Distance(Boid->GetActorLocation(), Agent.ProgressDestination);
						ProgressDistance = FMath::Min(ProgressDistance, FMath::Max(0.0f, GoalProgress));
					}
					const float ProgressSpeed = ProgressDistance / Agent.ProgressWindowSeconds;
					Agent.NoProgressSeconds = ProgressSpeed < MinimumSpeed
						                          ? Agent.NoProgressSeconds + Agent.ProgressWindowSeconds
						                          : 0.0f;
					Agent.ProgressWindowSeconds = 0.0f;
					Agent.ProgressDistanceCm = 0.0f;
				}
				if (Agent.NoProgressSeconds >= FMath::Max(0.1f, Tuning.StuckProgressSeconds))
				{
					Agent.NoProgressSeconds = 0.0f;
					Boid->ResetSteeringAvoidanceRuntime();
					++Agent.RecoveryCount;
					if (Snapshots[Index].bPlannerControlled)
					{
						RangedPositionPlanner->RequestRefresh(Agent.Id);
						CombatCoordinator->ResetPosition(Agent.Id);
						if (FNavigationAgentState* State = NavigationAgents.Find(Agent.Id))
						{
							if (State->RouteHandle)RouteService->Cancel(State->RouteHandle);
							State->RouteHandle = 0;
							State->Status = EBoidRouteStatus::Pending;
							State->Waypoints.Reset();
							State->RetrySeconds = NavigationRetrySeconds;
							State->SegmentValidatedAt = -1.;
						}
					}
					else if (NavigationMode == EBoidNavigationMode::AuthoredRoutes
						|| !Boid->RecoverWanderFromStall(WanderBounds, Tuning.BoundsMarginCm, SpeedLimit, AvoidanceLimit))
					{
						UE_LOG(LogTemp, Warning, TEXT("Boid %s cannot recover: no free escape inside WanderBounds."),
						       *Boid->GetName());
					}
					if (!CanSimulate() || !IsAgentRegistered(Boid)) return;
				}
			}
			else
			{
				Agent.NoProgressSeconds = Agent.ProgressWindowSeconds = Agent.ProgressDistanceCm = 0.0f;
			}
			if (const FRegisteredAgent* Current = FindAgentRecord(Snapshots[Index].Id))
			{
				StalledSeconds = Current->NoProgressSeconds;
				RecoveryCount = Current->RecoveryCount;
			}
		}
		if (bDrawSteering && !IsDebugPresentationSuppressed() && Boid == Selected)
		{
			const FVector Position = Snapshots[Index].Position;
			DrawDirection(GetWorld(), Position, Outputs[Index].ProfileIntent, 180.0f, FColor::Blue);
			DrawDirection(GetWorld(), Position, Outputs[Index].Separation, 180.0f, FColor::Red);
			DrawDirection(GetWorld(), Position, Outputs[Index].Alignment, 180.0f, FColor::Yellow);
			DrawDirection(GetWorld(), Position, Outputs[Index].Cohesion, 180.0f, FColor::Magenta);
			DrawDirection(GetWorld(), Position, Outputs[Index].Avoidance, 250.0f, FColor::Orange);
			DrawDirection(GetWorld(), Position, Outputs[Index].Heading, 250.0f, FColor::White);
			DrawDebugLine(GetWorld(), Position, Outputs[Index].ProbeEnd,
			              Outputs[Index].bThreatened ? FColor::Red : FColor::Green, false, 0.0f, 0, 2.0f);
			DrawDebugString(GetWorld(), Position + FVector(0.0f, 0.0f, 75.0f),
			                FString::Printf(TEXT("Steering %.0f cm/s | stalled %.1fs | recovery %d"),
			                                Boid->GetFlightVelocity().Size(), StalledSeconds, RecoveryCount),
			                nullptr, FColor::White, 0.0f);
		}
	}
}
