#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoidRangedPositioning.h"
#include "BoidNavigationTypes.h"
#include "Templates/UniquePtr.h"
#include "SwarmSimulationRunner.generated.h"

class AFlyingBoid;
class UBoidSteeringProfile;
class UBoxComponent;
class USceneComponent;
class FBoidRangedPositionPlanner;
class FBoidFlightRouteService;
class FBoidCombatMovementCoordinator;
struct FBoidCombatCoordinatorDeleter { void operator()(FBoidCombatMovementCoordinator* Coordinator) const; };
class UNavigationGraphAsset;
struct FBoidRouteServiceDeleter { void operator()(FBoidFlightRouteService* Service) const; };
/** Out-of-line deletion also supports Unreal's generated vtable constructor. */
struct FBoidRangedPlannerDeleter
{
	void operator()(FBoidRangedPositionPlanner* Planner) const;
};

USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FSwarmTuningSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0.0"))
	float CruiseSpeedCmPerSec = 400.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0.0"))
	float MaxTurnRateDegPerSec = 120.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0.0"))
	float AccelerationScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0.0"))
	float BrakingScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0.01"))
	float SteeringResponseSeconds = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0.0"))
	float VisualTurnSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wander", meta = (ClampMin = "0.01"))
	float DriftChangeSeconds = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wander", meta = (ClampMin = "0.0"))
	float MaxDriftYawRateDeg = 20.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wander", meta = (ClampMin = "0.0"))
	float MaxDriftPitchRateDeg = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flocking", meta = (ClampMin = "0.0"))
	float NeighborRadiusCm = 450.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flocking", meta = (ClampMin = "0.0"))
	float SeparationDistanceCm = 180.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flocking", meta = (ClampMin = "0.0"))
	float PredictedSeparationSeconds = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flocking", meta = (ClampMin = "0.0"))
	float PredictedSeparationClearanceCm = 90.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flocking", meta = (ClampMin = "0.0"))
	float SeparationWeight = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flocking", meta = (ClampMin = "0.0"))
	float AlignmentWeight = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flocking", meta = (ClampMin = "0.0"))
	float CohesionWeight = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstacles", meta = (ClampMin = "0.0"))
	float ObstacleMarginCm = 20.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstacles", meta = (ClampMin = "0.0"))
	float BoundsMarginCm = 20.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstacles", meta = (ClampMin = "1.0"))
	float MinLookaheadCm = 200.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstacles", meta = (ClampMin = "1.0"))
	float MaxLookaheadCm = 700.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstacles", meta = (ClampMin = "0.0"))
	float AvoidanceHoldSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0.1"))
	float ProgressWindowSeconds = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0.1"))
	float StuckProgressSeconds = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0.0"))
	float MinimumProgressSpeedCmPerSec = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.001"))
	float MaxStepSeconds = 1.0f / 30.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.001"))
	float MaxFrameSeconds = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxSubsteps = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Determinism")
	int32 InitialSeed = 1337;
};

/** Shared snapshot, steering, bounds, and swept movement for StateTree boids. */
UCLASS()
class FLYINGSWARMCOMBATSIM_API ASwarmSimulationRunner : public AActor
{
	GENERATED_BODY()

public:
	ASwarmSimulationRunner();
	virtual ~ASwarmSimulationRunner() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Swarm")
	void RegisterBoid(AFlyingBoid* Boid);
	UFUNCTION(BlueprintCallable, Category = "Swarm")
	void UnregisterBoid(AFlyingBoid* Boid);
	UFUNCTION(BlueprintPure, Category = "Swarm")
	int32 GetAgentId(AFlyingBoid* Boid) const;
	bool IsInsideSafeBounds(const FVector& Position, float BoidRadius) const;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swarm|Navigation") EBoidNavigationMode NavigationMode = EBoidNavigationMode::DirectOnly;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swarm|Navigation") TObjectPtr<UNavigationGraphAsset> NavigationGraph;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swarm|Navigation", meta=(ClampMin="1")) int32 RouteQueriesPerFrame = 32;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swarm|Navigation", meta=(ClampMin="1")) int32 RouteExpansionsPerFrame = 64;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swarm|Navigation", meta=(ClampMin="0.01")) float NavigationRetrySeconds = .5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swarm|Navigation", meta=(ClampMin="1", Units="cm")) float WaypointReachToleranceCm = 80.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swarm|Navigation", meta=(ClampMin="0")) float LastKnownPursuitSeconds = 5.f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Swarm|Navigation") int32 LastFrameRouteQueries = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Swarm|Navigation") int32 LastFrameRouteExpansions = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swarm Debug") bool bDrawNavigation = false;
	UFUNCTION(BlueprintCallable, Category="Swarm|Navigation") void InvalidateNavigationGraph();
	bool IsAllowedFlightPosition(const FVector& Position, float Radius) const;
	bool IsAllowedFlightSegment(const FVector& Start, const FVector& Goal, float Radius) const;
	FBoidFlightRouteService& GetRouteService();
	UBoidSteeringProfile* ResolveSteeringProfile(const AFlyingBoid* Boid, UBoidSteeringProfile* Preferred = nullptr) const;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Swarm|Combat", meta=(ClampMin="1")) int32 CombatControlQueriesPerFrame = 64;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Swarm|Combat") int32 LastFrameCombatControlQueries = 0;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Swarm|Bounds")
	TObjectPtr<USceneComponent> SceneRoot;
	/** Editor-scalable test volume; it does not physically block anything. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Swarm|Bounds")
	TObjectPtr<UBoxComponent> WanderBounds;

	/** Existing scene reference, also used as the debug selection fallback. */
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
	struct FRegisteredAgent
	{
		TWeakObjectPtr<AFlyingBoid> Boid;
		int32 Id = INDEX_NONE;
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
	TMap<int32,FNavigationAgentState> NavigationAgents;
	TUniquePtr<FBoidFlightRouteService,FBoidRouteServiceDeleter> RouteService;
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
	TArray<TWeakObjectPtr<AFlyingBoid>> PendingAdditions;
	TArray<TWeakObjectPtr<AFlyingBoid>> PendingRemovals;
	int32 NextAgentId = 1;
	bool bEndingPlay = false;
	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> FallbackSteeringProfile;
};
