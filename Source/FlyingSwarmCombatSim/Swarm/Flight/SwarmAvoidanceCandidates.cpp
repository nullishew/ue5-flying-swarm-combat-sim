#include "Swarm/Flight/SwarmAvoidanceCandidates.h"

void SwarmAvoidanceCandidates::AddDirection(TArray<FSwarmAvoidanceCandidate>& Candidates, const FVector& Direction, int32 Priority)
{
	const FVector Unit = Direction.GetSafeNormal();
	if (Unit.ContainsNaN() || Unit.IsNearlyZero()) return;
	for (FSwarmAvoidanceCandidate& Candidate : Candidates)
	{
		if (Candidate.Direction == Unit) { Candidate.Priority = FMath::Min(Candidate.Priority, Priority); return; }
	}
	FSwarmAvoidanceCandidate& Candidate = Candidates.AddDefaulted_GetRef();
	Candidate.Direction = Unit;
	Candidate.Priority = Priority;
}

void SwarmAvoidanceCandidates::Select(TConstArrayView<FSwarmAvoidanceCandidate> Candidates, int32 Limit, TArray<int32>& OutIndices)
{
	OutIndices.Reset(Candidates.Num());
	for (int32 Index = 0; Index < Candidates.Num(); ++Index) OutIndices.Add(Index);
	if (Limit <= 0) return;
	OutIndices.Sort([Candidates](int32 A, int32 B)
	{
		if (Candidates[A].Priority != Candidates[B].Priority) return Candidates[A].Priority < Candidates[B].Priority;
		const float RankA = FMath::IsFinite(Candidates[A].Rank) ? Candidates[A].Rank : -MAX_flt;
		const float RankB = FMath::IsFinite(Candidates[B].Rank) ? Candidates[B].Rank : -MAX_flt;
		if (RankA != RankB) return RankA > RankB;
		return A < B;
	});
	OutIndices.SetNum(FMath::Min(Limit, OutIndices.Num()), EAllowShrinking::No);
}
