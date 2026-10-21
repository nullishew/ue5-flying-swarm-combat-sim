#include "Swarm/Simulation/SwarmSpatialIndex.h"

#include <cmath>

bool FSwarmSpatialIndex::TryGetCell(const FVector& Position, FIntVector& OutCell) const
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double Coordinate = std::floor(Position[Axis] / CellSize);
		if (!FMath::IsFinite(Coordinate) || Coordinate < MIN_int32 || Coordinate > MAX_int32) return false;
		OutCell[Axis] = static_cast<int32>(Coordinate);
	}
	return true;
}

void FSwarmSpatialIndex::Rebuild(TConstArrayView<FSwarmSpatialSample> InSamples, double InCellSize, bool bEnableGrid)
{
	Samples.Reset(InSamples.Num());
	Samples.Append(InSamples.GetData(), InSamples.Num());
	StableOrder.Reset(Samples.Num());
	for (int32 Index = 0; Index < Samples.Num(); ++Index) StableOrder.Add(Index);
	StableOrder.Sort([this](int32 A, int32 B) { return Samples[A].AgentId == Samples[B].AgentId ? A < B : Samples[A].AgentId < Samples[B].AgentId; });
	CellHeads.Reset();
	NextIndices.Reset();
	CellSize = InCellSize;
	bGridValid = bEnableGrid && FMath::IsFinite(CellSize) && CellSize >= 1.0;
	if (!bGridValid) return;
	CellHeads.Reserve(Samples.Num());
	NextIndices.Init(INDEX_NONE, Samples.Num());
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		FIntVector Cell;
		if (!TryGetCell(Samples[Index].Position, Cell))
		{
			bGridValid = false; // Never omit an unindexable sample; queries fall back to the complete input.
			return;
		}
		int32& Head = CellHeads.FindOrAdd(Cell, INDEX_NONE);
		NextIndices[Index] = Head;
		Head = Index;
	}
}

FSwarmSpatialQueryStats FSwarmSpatialIndex::QueryRadius(const FVector& Position, double Radius, TArray<int32>& OutIndices) const
{
	FSwarmSpatialQueryStats Stats;
	OutIndices.Reset();
	auto Exhaustive = [&]()
	{
		OutIndices.Append(StableOrder);
		Stats.SamplesExamined = Samples.Num();
		return Stats;
	};
	if (Samples.IsEmpty()) return Stats;
	FIntVector Minimum, Maximum;
	const double RadiusSquared = Radius * Radius;
	if (!bGridValid || Radius < 0.0 || !FMath::IsFinite(RadiusSquared) || !TryGetCell(Position - FVector(Radius), Minimum) || !TryGetCell(Position + FVector(Radius), Maximum)) return Exhaustive();
	const int64 CellBudget = FMath::Max<int64>(64, static_cast<int64>(Samples.Num()) * 4);
	int64 CellCount = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 Span = static_cast<int64>(Maximum[Axis]) - Minimum[Axis] + 1;
		if (Span <= 0 || Span > CellBudget / CellCount) return Exhaustive();
		CellCount *= Span;
	}
	for (int64 X = Minimum.X; X <= Maximum.X; ++X)
	{
		for (int64 Y = Minimum.Y; Y <= Maximum.Y; ++Y)
		{
			for (int64 Z = Minimum.Z; Z <= Maximum.Z; ++Z)
			{
				++Stats.CellsVisited;
				const int32* Head = CellHeads.Find(FIntVector(static_cast<int32>(X), static_cast<int32>(Y), static_cast<int32>(Z)));
				for (int32 Index = Head ? *Head : INDEX_NONE; Index != INDEX_NONE; Index = NextIndices[Index])
				{
					++Stats.SamplesExamined;
					if (FVector::DistSquared(Position, Samples[Index].Position) <= RadiusSquared) OutIndices.Add(Index);
				}
			}
		}
	}
	OutIndices.Sort([this](int32 A, int32 B) { return Samples[A].AgentId == Samples[B].AgentId ? A < B : Samples[A].AgentId < Samples[B].AgentId; });
	return Stats;
}
