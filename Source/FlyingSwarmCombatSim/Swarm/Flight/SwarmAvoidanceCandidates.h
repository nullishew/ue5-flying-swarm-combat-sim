#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

struct FSwarmAvoidanceCandidate
{
	FVector Direction = FVector::ZeroVector;
	float Crowding = 0.0f;
	float Rank = 0.0f;
	int32 Priority = MAX_int32;
};

/** Prepares and selects avoidance candidates; the subsystem performs clearance queries, final scoring and movement. */
namespace SwarmAvoidanceCandidates
{
	void AddDirection(TArray<FSwarmAvoidanceCandidate>& Candidates, const FVector& Direction, int32 Priority = MAX_int32);
	/** Limit <= 0 retains exhaustive original order. Equal ranks retain insertion order. */
	void Select(TConstArrayView<FSwarmAvoidanceCandidate> Candidates, int32 Limit, TArray<int32>& OutIndices);
}
