#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SwarmSimulationRunner.generated.h"

class AFlyingBoid;
class UBoidSteeringProfile;
class UBoxComponent;
class USceneComponent;

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
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Swarm")
	void RegisterBoid(AFlyingBoid* Boid);
	UFUNCTION(BlueprintCallable, Category = "Swarm")
	void UnregisterBoid(AFlyingBoid* Boid);
	UFUNCTION(BlueprintPure, Category = "Swarm")
	int32 GetAgentId(AFlyingBoid* Boid) const;

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
	void ApplyQueuedRegistrations();
	void AdvanceStep(float StepTime);
	bool IsInsideSafeBounds(const FVector& Position, float BoidRadius) const;

	TArray<FRegisteredAgent> Agents;
	TArray<TWeakObjectPtr<AFlyingBoid>> PendingAdditions;
	TArray<TWeakObjectPtr<AFlyingBoid>> PendingRemovals;
	int32 NextAgentId = 1;
	bool bEndingPlay = false;
	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> FallbackSteeringProfile;
};
