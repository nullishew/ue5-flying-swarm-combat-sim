#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

struct FSwarmSpatialSample
{
	FVector Position = FVector::ZeroVector;
	int32 AgentId = INDEX_NONE;
};

struct FSwarmSpatialQueryStats
{
	int64 CellsVisited = 0;
	int64 SamplesExamined = 0;
};

/** Captured positions only. Returned indices refer to the input sample order, sorted by stable agent ID. */
class FSwarmSpatialIndex
{
public:
	void Rebuild(TConstArrayView<FSwarmSpatialSample> InSamples, double InCellSize, bool bEnableGrid = true);
	FSwarmSpatialQueryStats QueryRadius(const FVector& Position, double Radius, TArray<int32>& OutIndices) const;

private:
	bool TryGetCell(const FVector& Position, FIntVector& OutCell) const;
	TArray<FSwarmSpatialSample> Samples;
	TArray<int32> StableOrder;
	TMap<FIntVector, int32> CellHeads;
	TArray<int32> NextIndices;
	double CellSize = 900.0;
	bool bGridValid = false;
};
