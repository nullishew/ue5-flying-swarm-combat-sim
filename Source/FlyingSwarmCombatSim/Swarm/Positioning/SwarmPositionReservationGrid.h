#pragma once

#include "CoreMinimal.h"

/** Indexes agents' reserved ranged-combat positions by cell; the planner owns reservation positions, lifetime and exact spacing checks. */
class FSwarmPositionReservationGrid
{
public:
	bool Configure(double InCellSize);
	void Set(int32 AgentId, const FVector& Position);
	void Remove(int32 AgentId);
	void Query(const FVector& Position, double Radius, TArray<int32>& OutIds) const;

private:
	bool TryCell(const FVector& Position, FIntVector& Out) const;
	double CellSize = 0.0;
	TMap<FIntVector, TArray<int32>> Cells;
	TMap<int32, FIntVector> AgentCells;
	TSet<int32> Unindexed;
};
