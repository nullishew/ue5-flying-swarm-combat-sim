#pragma once

#include "CoreMinimal.h"
#include "BoidNavigationTypes.h"

class ASwarmSimulationRunner;
class AFlyingBoid;
class AActor;
struct FNavigationGraphSnapshot;

/** Runner-owned, incremental route validation and search. */
class FBoidFlightRouteService
{
public:
	uint64 Request(ASwarmSimulationRunner* Runner, AFlyingBoid* Boid, int32 AgentId, int32 GoalVersion,
		const FVector& Start, const FVector& Goal, AActor* IgnoreTarget);
	const FBoidFlightRouteResult* FindResult(uint64 Handle) const;
	void Cancel(uint64 Handle);
	void ResetAgent(int32 AgentId);
	void BeginFrame(ASwarmSimulationRunner* Runner, TConstArrayView<FBoidPlannerAgent> Agents);
	void Tick(ASwarmSimulationRunner* Runner, TConstArrayView<FBoidPlannerAgent> Agents);
	bool IsAllowedPosition(ASwarmSimulationRunner* Runner, const FVector& Position, float Radius) const;
	bool IsAllowedSegment(ASwarmSimulationRunner* Runner, const FVector& Start, const FVector& Goal, float Radius) const;
	const FString& GetConfigurationFailure() const { return ConfigurationFailure; }
	uint32 GetGeneration() const { return GraphGeneration; }
	FName GetRegion(ASwarmSimulationRunner* Runner, const FVector& Position, FName PreviousRegion = NAME_None) const;
	void InvalidateGraph();
	bool FindTransitAnchor(ASwarmSimulationRunner* Runner, const FVector& LastKnownPoint, FName PreviousRegion, FVector& OutAnchor) const;
	EBoidRouteStatus ValidateActiveSegment(ASwarmSimulationRunner* Runner, AFlyingBoid* Boid, const FVector& Start,
		const FVector& Goal, AActor* IgnoreTarget);

private:
	struct FRequestState;
	TMap<uint64, TSharedPtr<FRequestState>> Requests;
	TMap<uint64, FBoidFlightRouteResult> Results;
	TMap<uint64, int32> ResultOwners;
	TSharedPtr<const FNavigationGraphSnapshot> CurrentGraph;
	TArray<uint64> Queue;
	TArray<FBoidPlannerAgent> CurrentAgents;
	uint64 NextHandle = 1;
	int32 Cursor = 0;
	int32 QueriesLeft = 0;
	int32 SearchQueriesLeft = 0;
	int32 ExpansionsLeft = 0;
	int32 ActiveQueriesLeft = 0;
	int32 RequestQueryFloor = 0;
	int32 RequestExpansionFloor = 0;
	bool bSingleQuerySearchFrame = false;
	bool bValidatingActiveSegment = false;
	uint32 GraphGeneration = 1;
	uint32 LastGeometryHash = 0;
	bool bHasGeometryHash = false;
	FString ConfigurationFailure;
	FString LastLoggedConfigurationFailure;
	bool ValidateSegment(ASwarmSimulationRunner& Runner, const FVector& A, const FVector& B, float Radius, const FRequestState& Request);
	void RefreshGraph(ASwarmSimulationRunner* Runner);
	void ProcessOne(ASwarmSimulationRunner& Runner, FRequestState& Request);
};
