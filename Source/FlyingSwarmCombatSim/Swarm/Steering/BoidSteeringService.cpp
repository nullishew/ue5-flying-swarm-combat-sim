#include "Swarm/Steering/BoidSteeringService.h"
#include "Swarm/Steering/BoidSteeringProfile.h"
#include "Swarm/Flight/BoidFlightAlgorithms.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void FBoidSteeringService::BeginStep(TConstArrayView<FBoidSteeringNeighbor> Agents, float NeighborCellSizeCm, bool bUseGrid)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SwarmSpatialIndexRebuild);
	CapturedAgents.Reset(Agents.Num());
	CapturedAgents.Append(Agents.GetData(), Agents.Num());
	Samples.Reset(Agents.Num());
	MaximumRadius = MaximumSpeed = 0.f;
	bool bFinite = true;
	for (const FBoidSteeringNeighbor& Agent : Agents)
	{
		Samples.Add({Agent.Position, Agent.AgentId});
		bFinite &= FMath::IsFinite(Agent.Radius) && FMath::IsFinite(Agent.Velocity.Size());
		MaximumRadius = FMath::Max(MaximumRadius, Agent.Radius);
		MaximumSpeed = FMath::Max(MaximumSpeed, static_cast<float>(Agent.Velocity.Size()));
	}
	NeighborIndex.Rebuild(Samples, NeighborCellSizeCm, bUseGrid && bFinite && Agents.Num() >= 32);
}

FBoidSteeringServiceResult FBoidSteeringService::Evaluate(int32 SelfIndex, FBoidSteeringInput Input, const UBoidSteeringProfile* Profile, TFunctionRef<FBoidSteeringRuntimeState&(int32)> Runtime, TFunctionRef<bool(int32)> WallBetween)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SwarmSteeringEvaluation);
	FBoidSteeringServiceResult Result;
	const bool bValidProfile = Profile && Profile->ValidateProfile(Result.ConfigurationProblem);
	if (!Profile) Result.ConfigurationProblem = TEXT("No steering profile is assigned.");
	if (!bValidProfile) { Result.bHasDesiredSpeed = true; Result.DesiredSpeed = 0.f; }
	TArray<int32, TInlineAllocator<8>> ActiveEntries;
	float QueryRadius = Input.Radius + MaximumRadius + BoidFlight::ContactSkinCm;
	if (bValidProfile)
	{
		for (int32 Index = 0; Index < Profile->Behaviors.Num(); ++Index)
		{
			const UBoidSteeringBehavior* Behavior = Profile->Behaviors[Index].Behavior;
			if (!IsValid(Behavior)) continue;
			FBoidSteeringRuntimeState& State = Runtime(Index);
			State.AvoidanceHoldSeconds = FMath::Max(0.f, State.AvoidanceHoldSeconds - Input.DeltaTime);
			const float EffectiveWeight = Behavior->Weight * State.WeightScale;
			if (!FMath::IsFinite(EffectiveWeight) || EffectiveWeight <= 0.f || (Input.bCoordinatedCombat && !Behavior->SupportsNavigationPhase(Input.NavigationPhase))) continue;
			ActiveEntries.Add(Index);
			QueryRadius = FMath::Max(QueryRadius, Behavior->NeighborQueryRadius(Input, MaximumRadius, MaximumSpeed));
			if (const auto* Avoidance = Cast<UBoidObstacleAvoidanceSteeringBehavior>(Behavior)) { Result.Avoidance = Avoidance; Result.AvoidanceIndex = Index; }
		}
	}
	VisibleNeighbors.Reset();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SwarmNeighborGathering);
		Result.QueryStats = NeighborIndex.QueryRadius(Input.Position, static_cast<double>(QueryRadius) + 1., Candidates);
		for (int32 Index : Candidates)
		{
			if (Index == SelfIndex) continue;
			const FBoidSteeringNeighbor& Neighbor = CapturedAgents[Index];
			++Result.PairChecks;
			const float Distance = FVector::Dist(Input.Position, Neighbor.Position);
			const float PhysicalClearance = Input.Radius + Neighbor.Radius + BoidFlight::ContactSkinCm;
			float PairRadius = PhysicalClearance;
			for (int32 EntryIndex : ActiveEntries) PairRadius = FMath::Max(PairRadius, Profile->Behaviors[EntryIndex].Behavior->NeighborPairRadius(Input, Neighbor));
			if (Distance > PairRadius) continue;
			++Result.WallTraces;
			if (WallBetween(Index)) continue;
			VisibleNeighbors.Add(Neighbor);
			++Result.AcceptedNeighbors;
			if (Distance < PhysicalClearance)
			{
				Result.bPhysicalOverlap = true;
				const FVector Away = Distance > UE_SMALL_NUMBER ? (Input.Position - Neighbor.Position) / Distance : UBoidSeparationSteeringBehavior::OverlapDirection(Input.AgentId, Neighbor.AgentId);
				Result.PhysicalSeparation += Away * FMath::Square(1.f - Distance / PhysicalClearance);
			}
		}
	}
	Input.Neighbors = VisibleNeighbors;
	PreparedInput = Input;
	auto Accumulate = [&](const UBoidSteeringBehavior* Behavior, FBoidSteeringRuntimeState& State)
	{
		if (Behavior->IsA<UBoidObstacleAvoidanceSteeringBehavior>()) return;
		const FBoidSteeringResult Contribution = Behavior->EvaluateWithResult(Input, State);
		if (Contribution.Direction.ContainsNaN()) return;
		if (Behavior->IsA<UBoidSeparationSteeringBehavior>()) { Result.Separation += Contribution.Direction; Result.bCriticalSeparation |= Contribution.bCriticalSeparation; }
		else if (Behavior->IsA<UBoidAlignmentSteeringBehavior>()) Result.Alignment += Contribution.Direction;
		else if (Behavior->IsA<UBoidCohesionSteeringBehavior>()) Result.Cohesion += Contribution.Direction;
		else Result.ProfileIntent += Contribution.Direction;
		if (Contribution.bHasDesiredSpeed && FMath::IsFinite(Contribution.DesiredSpeedCmPerSec) && (!Input.bCoordinatedCombat || Behavior->IsPrimaryMovementForPhase(Input.NavigationPhase)))
		{
			Result.DesiredSpeed = Result.bHasDesiredSpeed ? FMath::Min(Result.DesiredSpeed, Contribution.DesiredSpeedCmPerSec) : Contribution.DesiredSpeedCmPerSec;
			Result.bHasDesiredSpeed = true;
		}
	};
	for (int32 Index : ActiveEntries) Accumulate(Profile->Behaviors[Index].Behavior, Runtime(Index));
	Result.Desired = (Result.ProfileIntent + Result.Separation + Result.Alignment + Result.Cohesion).GetSafeNormal();
	if (Result.bCriticalSeparation && !Result.Separation.IsNearlyZero()) Result.Desired = Result.Separation.GetSafeNormal();
	if (Result.bPhysicalOverlap)
	{
		if (Result.PhysicalSeparation.IsNearlyZero())
		{
			for (const FBoidSteeringNeighbor& Neighbor : VisibleNeighbors)
			{
				if (FVector::Dist(Input.Position, Neighbor.Position) < Input.Radius + Neighbor.Radius + BoidFlight::ContactSkinCm)
				{
					Result.PhysicalSeparation = UBoidSeparationSteeringBehavior::OverlapDirection(Input.AgentId, Neighbor.AgentId);
					break;
				}
			}
		}
		if (!Result.bCriticalSeparation) Result.Desired = Result.PhysicalSeparation.GetSafeNormal();
		Result.bCriticalSeparation = true;
	}
	if (Result.Desired.IsNearlyZero()) Result.Desired = Input.Heading;
	return Result;
}

FBoidAvoidanceResult FBoidSteeringService::ApplyAvoidance(const FBoidSteeringServiceResult& Result, FBoidSteeringRuntimeState& Runtime, const FVector& Desired, const FVector& Forward, float CruiseSpeed, float Lookahead, int32 CandidateLimit, TFunctionRef<float(const FVector&)> Probe)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SwarmAvoidanceSelection);
	if (Result.Avoidance) return Result.Avoidance->SelectDirection(PreparedInput, Runtime, AvoidanceScratch, Desired, Forward, CruiseSpeed, Lookahead, CandidateLimit, Probe);
	FBoidAvoidanceResult Unchanged;
	Unchanged.Direction = Desired;
	Unchanged.Clearance = Probe(Desired);
	return Unchanged;
}

void FBoidSteeringService::Reset()
{
	CapturedAgents.Reset();
	VisibleNeighbors.Reset();
	Samples.Reset();
	Candidates.Reset();
	NeighborIndex.Rebuild(Samples, 1.f, false);
	PreparedInput = FBoidSteeringInput();
	AvoidanceScratch.Candidates.Reset();
	AvoidanceScratch.Order.Reset();
	AvoidanceScratch.EscapeDirections.Reset();
}
