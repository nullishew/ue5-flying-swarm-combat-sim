#include "Swarm/Positioning/SwarmPositionReservationGrid.h"

#include <cmath>

bool FSwarmPositionReservationGrid::TryCell(const FVector& Position, FIntVector& Out) const
{
	if (!FMath::IsFinite(CellSize) || CellSize < 1.0) return false;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double Value = std::floor(Position[Axis] / CellSize);
		if (!FMath::IsFinite(Value) || Value < MIN_int32 || Value > MAX_int32) return false;
		Out[Axis] = static_cast<int32>(Value);
	}
	return true;
}

bool FSwarmPositionReservationGrid::Configure(double InCellSize)
{
	if (CellSize == InCellSize) return false;
	CellSize = InCellSize;
	Cells.Reset();
	AgentCells.Reset();
	Unindexed.Reset();
	return true;
}

void FSwarmPositionReservationGrid::Remove(int32 AgentId)
{
	if (const FIntVector* Previous = AgentCells.Find(AgentId))
	{
		const FIntVector Cell = *Previous;
		if (TArray<int32>* Bucket = Cells.Find(Cell))
		{
			Bucket->RemoveSwap(AgentId);
			if (Bucket->IsEmpty()) Cells.Remove(Cell);
		}
		AgentCells.Remove(AgentId);
	}
	Unindexed.Remove(AgentId);
}

void FSwarmPositionReservationGrid::Set(int32 AgentId, const FVector& Position)
{
	FIntVector Cell;
	const bool bIndexable = TryCell(Position, Cell);
	if (const FIntVector* Previous = AgentCells.Find(AgentId); bIndexable && Previous && *Previous == Cell) return;
	Remove(AgentId);
	if (!bIndexable)
	{
		Unindexed.Add(AgentId);
		return;
	}
	AgentCells.Add(AgentId, Cell);
	Cells.FindOrAdd(Cell).Add(AgentId);
}

void FSwarmPositionReservationGrid::Query(const FVector& Position, double Radius, TArray<int32>& OutIds) const
{
	OutIds.Reset();
	FIntVector Minimum, Maximum;
	bool bExhaustive = !FMath::IsFinite(Radius) || Radius < 0.0 || !TryCell(Position - FVector(Radius), Minimum) || !
		TryCell(Position + FVector(Radius), Maximum);
	const int64 Budget = FMath::Max<int64>(64, (static_cast<int64>(AgentCells.Num()) + Unindexed.Num()) * 4);
	int64 Count = 1;
	for (int32 Axis = 0; !bExhaustive && Axis < 3; ++Axis)
	{
		const int64 Span = static_cast<int64>(Maximum[Axis]) - Minimum[Axis] + 1;
		if (Span <= 0 || Span > Budget / Count) bExhaustive = true;
		else Count *= Span;
	}
	if (bExhaustive)
	{
		for (const auto& Pair : AgentCells) OutIds.Add(Pair.Key);
	}
	else
	{
		for (int64 X = Minimum.X; X <= Maximum.X; ++X)
			for (int64 Y = Minimum.Y; Y <= Maximum.Y; ++Y)
				for (int64 Z = Minimum.Z; Z <= Maximum.Z; ++Z)
				{
					if (const TArray<int32>* Bucket = Cells.Find(
						FIntVector(static_cast<int32>(X), static_cast<int32>(Y), static_cast<int32>(Z)))) OutIds.Append(
						*Bucket);
				}
	}
	for (int32 Id : Unindexed) OutIds.Add(Id);
	OutIds.Sort();
}
