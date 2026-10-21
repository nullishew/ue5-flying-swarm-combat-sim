#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Swarm/Simulation/SwarmSimulationSettings.h"
#include "Swarm/Simulation/SwarmSpatialIndex.h"
#include "Swarm/Simulation/SwarmCollisionQueries.h"
#include "Swarm/Flight/SwarmAvoidanceCandidates.h"
#include "Engine/HitResult.h"
#include "Swarm/Positioning/BoidRangedPositioning.h"
#include "Swarm/Routing/BoidNavigationTypes.h"
#include "Templates/UniquePtr.h"
#include "SwarmSimulationSubsystem.generated.h"

class AFlyingBoid;
class ASwarmSimulationBounds;
DECLARE_MULTICAST_DELEGATE_OneParam(FSwarmPostSimulation, float);
class UBoidSteeringProfile;
class UBoxComponent;
class FBoidRangedPositionPlanner;
class FBoidFlightRouteService;
class FBoidCombatMovementCoordinator;

struct FBoidCombatCoordinatorDeleter
{
	void operator()(FBoidCombatMovementCoordinator* Coordinator) const;
};

class UNavigationGraphAsset;

struct FBoidRouteServiceDeleter
{
	void operator()(FBoidFlightRouteService* Service) const;
};

/** Out-of-line deletion also supports Unreal's generated vtable constructor. */
struct FBoidRangedPlannerDeleter
{
	void operator()(FBoidRangedPositionPlanner* Planner) const;
};

/** Shared snapshot, steering, bounds, and swept movement for StateTree boids. */
UCLASS(BlueprintType)
class FLYINGSWARMCOMBATSIM_API USwarmSimulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	USwarmSimulationSubsystem();
	virtual ~USwarmSimulationSubsystem() override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void OnWorldEndPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual TStatId GetStatId() const override;
	UFUNCTION(BlueprintPure, Category="Swarm")
	bool IsAgentRegistered(const AFlyingBoid* Boid) const;
	UFUNCTION(BlueprintPure, Category="Swarm")
	bool CanSimulate() const;
	/** Spawn-time admission only; counts living enemies even outside population sensors or before registration. */
	bool CanSpawnBoid() const;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Population")
	int32 MaxPopulation = 67;
	FSwarmCollisionQueries& GetCollisionQueries() { return CollisionQueries; }
	const FSwarmCollisionQueries& GetCollisionQueries() const { return CollisionQueries; }
	/** Refreshed by the shared exception cache; ordinary native enemies require no exclusions. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Collision Queries")
	int32 CollisionExceptionActors = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Collision Queries")
	int32 LiveAttackProjectiles = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Collision Queries")
	int32 LastFrameProjectileLaunches = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Collision Queries")
	int32 LastFrameVisibilityQueryRebuilds = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Avoidance")
	int64 LastFrameAvoidanceCandidates = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Avoidance")
	int64 LastFrameClearanceProbes = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Avoidance")
	int64 LastFrameMandatoryClearanceRequests = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Avoidance")
	int64 LastFrameRecoveryClearanceProbes = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Ranged")
	int64 LastFramePlannerSpacingScores = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Ranged")
	int64 LastFramePlannerSamplesExamined = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Ranged")
	int64 LastFramePlannerReservationChecks = 0;
	/** Per-frame work counts, summed over substeps; sample visits include self entries. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Neighbor Queries")
	int64 LastFrameNeighborCellsVisited = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Neighbor Queries")
	int64 LastFrameNeighborSamplesExamined = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Neighbor Queries")
	int64 LastFrameNeighborPairChecks = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Neighbor Queries")
	int64 LastFrameNeighborWallTraces = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Neighbor Queries")
	int64 LastFrameAcceptedNeighbors = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Neighbor Queries")
	int32 LastFrameMovementSubsteps = 0;
	UFUNCTION(BlueprintCallable, Category="Swarm")
	void NotifySetupChanged();
	/** Native attack callbacks; never used for boid movement. */
	FSwarmPostSimulation OnPostSimulation;

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Swarm")
	int32 GetAgentId(const AFlyingBoid* Boid) const;
	UFUNCTION(BlueprintPure, Category = "Swarm")
	bool IsInsideSafeBounds(const FVector& Position, float BoidRadius) const;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm|Navigation")
	EBoidNavigationMode NavigationMode = EBoidNavigationMode::DirectOnly;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm|Navigation")
	TObjectPtr<UNavigationGraphAsset> NavigationGraph;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm|Navigation", meta = (ClampMin = "1"))
	int32 RouteQueriesPerFrame = 32;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm|Navigation", meta = (ClampMin = "1"))
	int32 RouteExpansionsPerFrame = 64;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm|Navigation", meta = (ClampMin = "0.01"))
	float NavigationRetrySeconds = .5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm|Navigation", meta = (ClampMin = "1", Units = "cm"))
	float WaypointReachToleranceCm = 80.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm|Navigation", meta = (ClampMin = "0"))
	float LastKnownPursuitSeconds = 5.f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Navigation")
	int32 LastFrameRouteQueries = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Navigation")
	int32 LastFrameRouteExpansions = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm Debug")
	bool bDrawNavigation = false;
	UFUNCTION(BlueprintCallable, Category="Swarm|Navigation")
	void InvalidateNavigationGraph();
	bool IsAllowedFlightPosition(const FVector& Position, float Radius) const;
	bool IsAllowedFlightSegment(const FVector& Start, const FVector& Goal, float Radius) const;
	FBoidFlightRouteService& GetRouteService();
	UBoidSteeringProfile* ResolveSteeringProfile(const AFlyingBoid* Boid,
	                                             UBoidSteeringProfile* Preferred = nullptr) const;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swarm|Combat", meta=(ClampMin="1"))
	int32 CombatControlQueriesPerFrame = 64;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Swarm|Combat")
	int32 LastFrameCombatControlQueries = 0;
	void AdoptNavigationRoute(int32 AgentId, const FVector& Goal, const FBoidFlightRouteResult& Route);
	FBoidNavigationGoal GetNavigationGoal(AFlyingBoid* Boid) const;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm|Ranged")
	FBoidRangedSettings RangedSettings;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm Debug")
	bool bDrawPositioning = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Ranged")
	int32 LastFrameCandidates = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Ranged")
	int32 LastFramePositioningQueries = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Swarm|Ranged")
	float LastFramePositioningMilliseconds = 0.f;

	/** Resolved level geometry; not owned by the simulation. */
	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> WanderBounds;

	/** Optional runtime debug selection fallback; never used to register agents. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Swarm Debug")
	TObjectPtr<AFlyingBoid> TestBoid;
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Swarm Debug")
	TObjectPtr<AFlyingBoid> DebugBoid;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm Debug")
	bool bDrawSteering = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm Debug")
	bool bDrawBounds = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm|Tuning")
	FSwarmTuningSettings Tuning;
	/** Shared fallback for StateTree states that have not assigned a profile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm|Steering")
	TObjectPtr<UBoidSteeringProfile> DefaultSteeringProfile;

private:
	friend class FSwarmMembershipIndexTest;
	friend class AFlyingBoid;
	// Membership requests are exclusively issued by the agent lifecycle.
	void RegisterBoid(AFlyingBoid* Boid);
	void UnregisterBoid(AFlyingBoid* Boid);
	void RefreshSetup();
	void CopyProjectSettings();
	void Shutdown();
	void FinishShutdown();
	void InitializePendingLogic();
	void CreateFallbackProfile();
	TWeakObjectPtr<ASwarmSimulationBounds> BoundsSource;
	uint64 SetupRefreshFrame = MAX_uint64;
	FString LastSetupProblem;
	bool bWorldReady = false;
	bool bFrameActive = false;
	bool bShutdownComplete = false;
	bool bReleaseServices = false;
	bool bInitialBatchCommitted = false;

	struct FRegisteredAgent
	{
		TWeakObjectPtr<AFlyingBoid> Boid;
		int32 Id = INDEX_NONE;
		bool bLogicStarted = false;
		float NoProgressSeconds = 0.0f;
		float ProgressWindowSeconds = 0.0f;
		float ProgressDistanceCm = 0.0f;
		FVector ProgressWindowStart = FVector::ZeroVector;
		FVector ProgressDestination = FVector::ZeroVector;
		bool bProgressHasDestination = false;
		int32 RecoveryCount = 0;
		uint32 IntentRevision = 0;
		TWeakObjectPtr<AActor> ProgressTarget;
		FVector RetreatGoal = FVector::ZeroVector;
		TWeakObjectPtr<AActor> RetreatTarget;
		bool bRetreating = false;
		bool bProgressRetreating = false;
	};

	struct FNavigationAgentState
	{
		uint64 RouteHandle = 0;
		int32 GoalVersion = 0;
		uint32 RouteGeneration = 0;
		int32 SelectionVersion = INDEX_NONE;
		int32 SelectionRouteVersion = INDEX_NONE;
		FVector Destination = FVector::ZeroVector;
		TArray<FVector> Waypoints;
		int32 WaypointIndex = 0;
		float LengthCm = 0.f;
		float RetrySeconds = 0.f;
		double SegmentValidatedAt = -1.;
		int32 ValidatedWaypointIndex = INDEX_NONE;
		EBoidNavigationMode Mode = EBoidNavigationMode::DirectOnly;
		bool bHasDestination = false;
		uint32 IntentRevision = 0;
		uint64 IntentOwner = 0;
		TWeakObjectPtr<AActor> RouteTarget;
		EBoidRouteStatus Status = EBoidRouteStatus::Pending;
		EBoidRouteTier Tier = EBoidRouteTier::None;
		FString FailureReason;
		FName PlayerRegion = NAME_None;
		TWeakObjectPtr<AActor> LastSeenTarget;
		FVector LastSeenPosition = FVector::ZeroVector;
		FVector GoalTargetPosition = FVector::ZeroVector;
		double LastSeenAt = -1.;
		FBoidNavigationGoal Navigation;
	};

	TMap<int32, FNavigationAgentState> NavigationAgents;
	TUniquePtr<FBoidFlightRouteService, FBoidRouteServiceDeleter> RouteService;
	TArray<FBoidPlannerAgent> PlannerAgents;
	double NavigationTime = 0.;
	int32 NavigationCursor = 0;
	void UpdateNavigationGoals(float DeltaTime);
	void ApplyQueuedRegistrations();
	void AdvanceStep(float StepTime);
	void UpdateRangedPositions(float DeltaTime);
	TUniquePtr<FBoidRangedPositionPlanner, FBoidRangedPlannerDeleter> RangedPositionPlanner;
	TUniquePtr<FBoidCombatMovementCoordinator, FBoidCombatCoordinatorDeleter> CombatCoordinator;

	TArray<FRegisteredAgent> Agents;
	TMap<TWeakObjectPtr<AFlyingBoid>, int32> AgentIds;
	TMap<int32, int32> AgentRecordIndices;
	void RebuildAgentLookups();
	FRegisteredAgent* FindAgentRecord(int32 AgentId);
	FSwarmCollisionQueries CollisionQueries;
	FSwarmSpatialIndex NeighborIndex;
	TArray<FSwarmSpatialSample> NeighborSamples;
	TArray<int32> NeighborCandidates;
	TArray<FSwarmAvoidanceCandidate> AvoidanceCandidates;
	TArray<int32> AvoidanceOrder;
	TArray<FHitResult> ClearanceHits;
	TArray<TWeakObjectPtr<AFlyingBoid>> PendingAdditions;
	TArray<TWeakObjectPtr<AFlyingBoid>> PendingRemovals;
	int32 NextAgentId = 1;
	bool bEndingPlay = false;
	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> FallbackSteeringProfile;
};
