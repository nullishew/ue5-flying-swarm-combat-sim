#pragma once

#include "CoreMinimal.h"
#include "Swarm/Steering/BoidSteeringBehavior.h"
#include "Swarm/Steering/BoidLocalSteeringBehaviors.h"
#include "Swarm/Steering/BoidSteeringProfile.h"
#include "Swarm/Simulation/SwarmSpatialIndex.h"

struct FBoidSteeringServiceResult
{
	FVector ProfileIntent = FVector::ZeroVector;
	FVector Separation = FVector::ZeroVector;
	FVector Alignment = FVector::ZeroVector;
	FVector Cohesion = FVector::ZeroVector;
	FVector PhysicalSeparation = FVector::ZeroVector;
	FVector Desired = FVector::ZeroVector;
	float DesiredSpeed = 0.f;
	bool bHasDesiredSpeed = false;
	bool bCriticalSeparation = false;
	bool bPhysicalOverlap = false;
	const UBoidObstacleAvoidanceSteeringBehavior* Avoidance = nullptr;
	int32 AvoidanceIndex = INDEX_NONE;
	FSwarmSpatialQueryStats QueryStats;
	int64 PairChecks = 0, WallTraces = 0, AcceptedNeighbors = 0;
	FString ConfigurationProblem;
};

// Non-ticking calculation service. Evaluate and ApplyAvoidance are paired for one agent before evaluating the next.
// All world access is supplied as narrow visibility/clearance callbacks; no actor movement or whole-runner dependency.
class FLYINGSWARMCOMBATSIM_API FBoidSteeringService
{
public:
	void BeginStep(TConstArrayView<FBoidSteeringNeighbor> Agents, float NeighborCellSizeCm, bool bUseGrid);
	FBoidSteeringServiceResult Evaluate(int32 SelfIndex, FBoidSteeringInput Input, const UBoidSteeringProfile* Profile, TFunctionRef<FBoidSteeringRuntimeState&(int32)> Runtime, TFunctionRef<bool(int32)> WallBetween);
	FBoidAvoidanceResult ApplyAvoidance(const FBoidSteeringServiceResult& Result, FBoidSteeringRuntimeState& Runtime, const FVector& Desired, const FVector& Forward, float CruiseSpeed, float Lookahead, int32 CandidateLimit, TFunctionRef<float(const FVector&)> Probe);
	void Reset();
private:
	TArray<FBoidSteeringNeighbor> CapturedAgents, VisibleNeighbors;
	TArray<FSwarmSpatialSample> Samples;
	TArray<int32> Candidates;
	FSwarmSpatialIndex NeighborIndex;
	FBoidAvoidanceScratch AvoidanceScratch;
	FBoidSteeringInput PreparedInput;
	float MaximumRadius = 0.f, MaximumSpeed = 0.f;
};
