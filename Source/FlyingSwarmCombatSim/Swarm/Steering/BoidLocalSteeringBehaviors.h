#pragma once

#include "CoreMinimal.h"
#include "Swarm/Steering/BoidSteeringBehavior.h"
#include "Swarm/Flight/SwarmAvoidanceCandidates.h"
#include "Templates/Function.h"
#include "BoidLocalSteeringBehaviors.generated.h"

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Separation"))
class FLYINGSWARMCOMBATSIM_API UBoidSeparationSteeringBehavior : public UBoidSteeringBehavior
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Separation", meta = (ClampMin = "0", Units = "cm")) float SeparationDistanceCm = 180.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Separation", meta = (ClampMin = "0", Units = "s")) float PredictionSeconds = .5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Separation", meta = (ClampMin = "0", Units = "cm")) float PredictedClearanceCm = 90.f;
	virtual bool IsLocalSteeringBehavior() const override { return true; }
	virtual bool ValidateSettings(FString& OutReason) const override;
	virtual float NeighborQueryRadius(const FBoidSteeringInput& Input, float MaximumRadius, float MaximumSpeed) const override;
	virtual float NeighborPairRadius(const FBoidSteeringInput& Input, const FBoidSteeringNeighbor& Neighbor) const override;
	virtual FBoidSteeringResult EvaluateWithResult(const FBoidSteeringInput& Input, FBoidSteeringRuntimeState& Runtime) const override;
	FBoidSteeringResult CalculateSeparation(const FBoidSteeringInput& Input) const;
	static FVector OverlapDirection(int32 SelfId, int32 NeighborId);
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Alignment"))
class FLYINGSWARMCOMBATSIM_API UBoidAlignmentSteeringBehavior : public UBoidSteeringBehavior
{
	GENERATED_BODY()
public:
	UBoidAlignmentSteeringBehavior();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Alignment", meta = (ClampMin = "0", Units = "cm")) float NeighborRadiusCm = 450.f;
	virtual bool IsLocalSteeringBehavior() const override { return true; }
	virtual bool ValidateSettings(FString& OutReason) const override;
	virtual float NeighborQueryRadius(const FBoidSteeringInput& Input, float MaximumRadius, float MaximumSpeed) const override { return NeighborRadiusCm; }
protected:
	virtual FVector CalculateDirection(const FBoidSteeringInput& Input, FBoidSteeringRuntimeState& Runtime) const override;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Cohesion"))
class FLYINGSWARMCOMBATSIM_API UBoidCohesionSteeringBehavior : public UBoidSteeringBehavior
{
	GENERATED_BODY()
public:
	UBoidCohesionSteeringBehavior();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cohesion", meta = (ClampMin = "0", Units = "cm")) float NeighborRadiusCm = 450.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cohesion", meta = (ClampMin = "0", Units = "cm")) float MinimumNeighborDistanceCm = 180.f;
	virtual bool IsLocalSteeringBehavior() const override { return true; }
	virtual bool ValidateSettings(FString& OutReason) const override;
	virtual float NeighborQueryRadius(const FBoidSteeringInput& Input, float MaximumRadius, float MaximumSpeed) const override { return NeighborRadiusCm; }
protected:
	virtual FVector CalculateDirection(const FBoidSteeringInput& Input, FBoidSteeringRuntimeState& Runtime) const override;
};

struct FBoidAvoidanceResult
{
	FVector Direction = FVector::ZeroVector;
	float Clearance = 0.f;
	int32 Candidates = 0;
	bool bBlocked = false;
};

// Reusable calculation scratch owned by the service, not a shared behavior asset or per-agent decision state.
struct FBoidAvoidanceScratch
{
	TArray<FSwarmAvoidanceCandidate> Candidates;
	TArray<int32> Order;
	TArray<FVector> EscapeDirections;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Obstacle Avoidance"))
class FLYINGSWARMCOMBATSIM_API UBoidObstacleAvoidanceSteeringBehavior : public UBoidSteeringBehavior
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avoidance", meta = (ClampMin = "1", Units = "cm")) float MinLookaheadCm = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avoidance", meta = (ClampMin = "1", Units = "cm")) float MaxLookaheadCm = 700.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avoidance", meta = (ClampMin = "0", Units = "s")) float HoldSeconds = .5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avoidance|Crowding", meta = (ClampMin = "0", Units = "cm")) float NeighborRadiusCm = 450.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avoidance|Crowding", meta = (ClampMin = "0", Units = "s")) float PredictionSeconds = .5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avoidance|Crowding", meta = (ClampMin = "0", Units = "cm")) float PredictedClearanceCm = 90.f;
	virtual bool IsLocalSteeringBehavior() const override { return true; }
	virtual bool ValidateSettings(FString& OutReason) const override;
	virtual float NeighborQueryRadius(const FBoidSteeringInput& Input, float MaximumRadius, float MaximumSpeed) const override;
	virtual float NeighborPairRadius(const FBoidSteeringInput& Input, const FBoidSteeringNeighbor& Neighbor) const override;
	FBoidAvoidanceResult SelectDirection(const FBoidSteeringInput& Input, FBoidSteeringRuntimeState& Runtime, FBoidAvoidanceScratch& Scratch, const FVector& Desired, const FVector& Forward, float CruiseSpeed, float Lookahead, int32 CandidateLimit, TFunctionRef<float(const FVector&)> Probe) const;
};
