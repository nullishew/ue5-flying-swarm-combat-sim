#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Swarm/Positioning/BoidRangedPositioning.h"
#include "SwarmSimulationSettings.generated.h"

class UBoidSteeringProfile;

USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FSwarmTuningSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0.0"))
	float CruiseSpeedCmPerSec = 670.0f;
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

/** Immutable project defaults, copied into each gameplay world's simulation. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Swarm Simulation"))
class FLYINGSWARMCOMBATSIM_API USwarmSimulationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }
	UPROPERTY(Config, EditAnywhere, Category = "Simulation")
	FSwarmTuningSettings Tuning;
	UPROPERTY(Config, EditAnywhere, Category = "Positioning")
	FBoidRangedSettings RangedSettings;
	UPROPERTY(Config, EditAnywhere, Category = "Navigation", meta = (ClampMin = "1"))
	int32 RouteQueriesPerFrame = 32;
	UPROPERTY(Config, EditAnywhere, Category = "Navigation", meta = (ClampMin = "1"))
	int32 RouteExpansionsPerFrame = 64;
	UPROPERTY(Config, EditAnywhere, Category = "Navigation", meta = (ClampMin = "0.01"))
	float NavigationRetrySeconds = .5f;
	UPROPERTY(Config, EditAnywhere, Category = "Navigation", meta = (ClampMin = "1"))
	float WaypointReachToleranceCm = 80.f;
	UPROPERTY(Config, EditAnywhere, Category = "Navigation", meta = (ClampMin = "0"))
	float LastKnownPursuitSeconds = 5.f;
	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "1"))
	int32 CombatControlQueriesPerFrame = 64;
	UPROPERTY(Config, EditAnywhere, Category="Steering")
	TSoftObjectPtr<UBoidSteeringProfile> DefaultSteeringProfile;
};
