#include "Swarm/Steering/BoidLocalSteeringBehaviors.h"
#include "Swarm/Flight/BoidFlightAlgorithms.h"
#include "Swarm/Flight/SwarmAvoidanceCandidates.h"

namespace
{
	bool Nonnegative(float Value) { return FMath::IsFinite(Value) && Value >= 0.f; }
}

FVector UBoidSeparationSteeringBehavior::OverlapDirection(int32 SelfId, int32 NeighborId)
{
	const uint32 LowId = static_cast<uint32>(FMath::Min(SelfId, NeighborId));
	const uint32 HighId = static_cast<uint32>(FMath::Max(SelfId, NeighborId));
	const uint32 Hash = LowId * 73856093u ^ HighId * 19349663u;
	const float Angle = 2.f * PI * static_cast<float>(Hash % 1024u) / 1024.f;
	const float Height = static_cast<float>((Hash >> 10) % 1024u) / 512.f - 1.f;
	const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), Height);
	return (SelfId < NeighborId ? Direction : -Direction).GetSafeNormal();
}

bool UBoidSeparationSteeringBehavior::ValidateSettings(FString& OutReason) const
{
	if (!Super::ValidateSettings(OutReason)) return false;
	if (Nonnegative(SeparationDistanceCm) && Nonnegative(PredictionSeconds) && Nonnegative(PredictedClearanceCm)) return true;
	OutReason = TEXT("Separation requires finite, nonnegative distance and prediction settings.");
	return false;
}

float UBoidSeparationSteeringBehavior::NeighborQueryRadius(const FBoidSteeringInput& Input, float MaximumRadius, float MaximumSpeed) const
{
	return FMath::Max3(SeparationDistanceCm, Input.Radius + MaximumRadius + BoidFlight::ContactSkinCm, PredictedClearanceCm) + (static_cast<float>(Input.Velocity.Size()) + MaximumSpeed) * PredictionSeconds;
}

float UBoidSeparationSteeringBehavior::NeighborPairRadius(const FBoidSteeringInput& Input, const FBoidSteeringNeighbor& Neighbor) const
{
	return FMath::Max3(SeparationDistanceCm, Input.Radius + Neighbor.Radius + BoidFlight::ContactSkinCm, PredictedClearanceCm) + (Input.Velocity - Neighbor.Velocity).Size() * PredictionSeconds;
}

FBoidSteeringResult UBoidSeparationSteeringBehavior::EvaluateWithResult(const FBoidSteeringInput& Input, FBoidSteeringRuntimeState& Runtime) const
{
	FString Reason;
	const float EffectiveWeight = Weight * Runtime.WeightScale;
	if (!ValidateSettings(Reason) || !FMath::IsFinite(EffectiveWeight) || EffectiveWeight <= 0.f || (Input.bCoordinatedCombat && !SupportsNavigationPhase(Input.NavigationPhase))) return FBoidSteeringResult();
	FBoidSteeringResult Result = CalculateSeparation(Input);
	Result.Direction *= EffectiveWeight * (Result.bCriticalSeparation ? 3.f : 1.f);
	return Result;
}

FBoidSteeringResult UBoidSeparationSteeringBehavior::CalculateSeparation(const FBoidSteeringInput& Input) const
{
	FBoidSteeringResult Result;
	FString Reason;
	if (!ValidateSettings(Reason)) return Result;
	FVector Sum = FVector::ZeroVector, CriticalDirection = FVector::ZeroVector;
	float NearestCriticalDistance = TNumericLimits<float>::Max();
	for (const FBoidSteeringNeighbor& Neighbor : Input.Neighbors)
	{
		const FVector Offset = Input.Position - Neighbor.Position;
		const float Distance = Offset.Size();
		const float PhysicalClearance = Input.Radius + Neighbor.Radius + BoidFlight::ContactSkinCm;
		const float PairRadius = FMath::Max(SeparationDistanceCm, PhysicalClearance);
		const FVector Away = Distance > UE_SMALL_NUMBER ? Offset / Distance : OverlapDirection(Input.AgentId, Neighbor.AgentId);
		if (Distance < PairRadius)
		{
			const float Strength = 1.f - Distance / PairRadius;
			Sum += Away * Strength * Strength;
		}
		bool bCritical = Distance < FMath::Max(PredictedClearanceCm, PhysicalClearance);
		const FVector RelativeVelocity = Input.Velocity - Neighbor.Velocity;
		if (PredictionSeconds > UE_SMALL_NUMBER && FVector::DotProduct(Offset, RelativeVelocity) < 0.f)
		{
			const float Time = FMath::Clamp(static_cast<float>(-FVector::DotProduct(Offset, RelativeVelocity) / FMath::Max(RelativeVelocity.SizeSquared(), static_cast<double>(UE_SMALL_NUMBER))), 0.f, PredictionSeconds);
			const FVector PredictedOffset = Offset + RelativeVelocity * Time;
			const float PredictedDistance = PredictedOffset.Size();
			const float PairClearance = FMath::Max(PredictedClearanceCm, PhysicalClearance);
			if (PredictedDistance < PairClearance)
			{
				Sum += (PredictedDistance > UE_SMALL_NUMBER ? PredictedOffset / PredictedDistance : Away) * 2.f * (1.f - PredictedDistance / PairClearance);
				bCritical = true;
			}
		}
		if (bCritical)
		{
			Result.bCriticalSeparation = true;
			if (Distance < NearestCriticalDistance) { NearestCriticalDistance = Distance; CriticalDirection = Away; }
		}
	}
	FVector Direction = Sum.GetClampedToMaxSize(1.f);
	if (Result.bCriticalSeparation && Direction.IsNearlyZero()) Direction = CriticalDirection;
	Result.Direction = Direction;
	return Result;
}

UBoidAlignmentSteeringBehavior::UBoidAlignmentSteeringBehavior() { Weight = .25f; }
UBoidCohesionSteeringBehavior::UBoidCohesionSteeringBehavior() { Weight = .1f; }

bool UBoidAlignmentSteeringBehavior::ValidateSettings(FString& OutReason) const
{
	if (!Super::ValidateSettings(OutReason)) return false;
	if (Nonnegative(NeighborRadiusCm)) return true;
	OutReason = TEXT("Alignment requires a finite, nonnegative neighbor radius.");
	return false;
}

bool UBoidCohesionSteeringBehavior::ValidateSettings(FString& OutReason) const
{
	if (!Super::ValidateSettings(OutReason)) return false;
	if (Nonnegative(NeighborRadiusCm) && Nonnegative(MinimumNeighborDistanceCm)) return true;
	OutReason = TEXT("Cohesion requires finite, nonnegative neighbor distances.");
	return false;
}

FVector UBoidAlignmentSteeringBehavior::CalculateDirection(const FBoidSteeringInput& Input, FBoidSteeringRuntimeState& Runtime) const
{
	FVector Sum = FVector::ZeroVector;
	for (const FBoidSteeringNeighbor& Neighbor : Input.Neighbors)
	{
		if (Neighbor.bSteeringActive && FVector::Dist(Input.Position, Neighbor.Position) < NeighborRadiusCm && !Neighbor.Velocity.IsNearlyZero()) Sum += Neighbor.Velocity.GetSafeNormal();
	}
	return Sum.GetSafeNormal();
}

FVector UBoidCohesionSteeringBehavior::CalculateDirection(const FBoidSteeringInput& Input, FBoidSteeringRuntimeState& Runtime) const
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const FBoidSteeringNeighbor& Neighbor : Input.Neighbors)
	{
		const float Distance = FVector::Dist(Input.Position, Neighbor.Position);
		const float MinimumDistance = FMath::Max(MinimumNeighborDistanceCm, Input.Radius + Neighbor.Radius + BoidFlight::ContactSkinCm);
		if (Neighbor.bSteeringActive && Distance < NeighborRadiusCm && Distance >= MinimumDistance) { Sum += Neighbor.Position; ++Count; }
	}
	return Count ? (Sum / Count - Input.Position).GetSafeNormal() : FVector::ZeroVector;
}

bool UBoidObstacleAvoidanceSteeringBehavior::ValidateSettings(FString& OutReason) const
{
	if (!Super::ValidateSettings(OutReason)) return false;
	if (Nonnegative(MinLookaheadCm) && Nonnegative(MaxLookaheadCm) && MinLookaheadCm >= 1.f && MaxLookaheadCm >= MinLookaheadCm && Nonnegative(HoldSeconds) && Nonnegative(NeighborRadiusCm) && Nonnegative(PredictionSeconds) && Nonnegative(PredictedClearanceCm)) return true;
	OutReason = TEXT("Obstacle Avoidance requires finite nonnegative settings and ordered lookahead distances of at least 1 cm.");
	return false;
}

float UBoidObstacleAvoidanceSteeringBehavior::NeighborQueryRadius(const FBoidSteeringInput& Input, float MaximumRadius, float MaximumSpeed) const
{
	return FMath::Max(NeighborRadiusCm, FMath::Max(PredictedClearanceCm, Input.Radius + MaximumRadius + BoidFlight::ContactSkinCm) + (static_cast<float>(Input.Velocity.Size()) + MaximumSpeed) * PredictionSeconds);
}

float UBoidObstacleAvoidanceSteeringBehavior::NeighborPairRadius(const FBoidSteeringInput& Input, const FBoidSteeringNeighbor& Neighbor) const
{
	return FMath::Max(NeighborRadiusCm, FMath::Max(PredictedClearanceCm, Input.Radius + Neighbor.Radius + BoidFlight::ContactSkinCm) + static_cast<float>((Input.Velocity - Neighbor.Velocity).Size()) * PredictionSeconds);
}

FBoidAvoidanceResult UBoidObstacleAvoidanceSteeringBehavior::SelectDirection(const FBoidSteeringInput& Input, FBoidSteeringRuntimeState& Runtime, FBoidAvoidanceScratch& Scratch,
	const FVector& Desired, const FVector& Forward, float CruiseSpeed, float Lookahead, int32 CandidateLimit, TFunctionRef<float(const FVector&)> Probe) const
{
	FBoidAvoidanceResult Result;
	Result.Direction = Desired;
	TArray<FSwarmAvoidanceCandidate>& Candidates = Scratch.Candidates;
	Candidates.Reset();
	SwarmAvoidanceCandidates::AddDirection(Candidates, Desired, 0);
	SwarmAvoidanceCandidates::AddDirection(Candidates, Forward, 2);
	SwarmAvoidanceCandidates::AddDirection(Candidates, -Forward);
	const bool bHeld = Runtime.AvoidanceHoldSeconds > 0.f;
	if (bHeld) SwarmAvoidanceCandidates::AddDirection(Candidates, Runtime.HeldAvoidanceDirection, 1);
	if (Scratch.EscapeDirections.IsEmpty()) BoidFlight::AppendEscapeDirections(Scratch.EscapeDirections);
	for (const FVector& Direction : Scratch.EscapeDirections) SwarmAvoidanceCandidates::AddDirection(Candidates, Direction);
	for (FSwarmAvoidanceCandidate& Candidate : Candidates)
	{
		const FVector CandidateVelocity = Candidate.Direction * CruiseSpeed;
		for (const FBoidSteeringNeighbor& Neighbor : Input.Neighbors)
		{
			const FVector Offset = Input.Position - Neighbor.Position;
			const FVector Relative = CandidateVelocity - Neighbor.Velocity;
			const float Time = FMath::Clamp(static_cast<float>(-FVector::DotProduct(Offset, Relative) / FMath::Max(Relative.SizeSquared(), static_cast<double>(UE_SMALL_NUMBER))), 0.f, PredictionSeconds);
			const float PredictedDistance = (Offset + Relative * Time).Size();
			const float ClearanceNeeded = FMath::Max(PredictedClearanceCm, Input.Radius + Neighbor.Radius);
			Candidate.Crowding += FMath::Max(0.f, 1.f - PredictedDistance / FMath::Max(1.f, ClearanceNeeded));
			if (Candidate.Crowding >= 2.f) break;
		}
		const float Continuity = bHeld ? static_cast<float>(FVector::DotProduct(Candidate.Direction, Runtime.HeldAvoidanceDirection)) : 0.f;
		Candidate.Rank = .8f * FVector::DotProduct(Candidate.Direction, Desired) + .6f * Continuity - 2.f * FMath::Min(Candidate.Crowding, 2.f);
	}
	TArray<int32>& Order = Scratch.Order;
	SwarmAvoidanceCandidates::Select(Candidates, CandidateLimit, Order);
	Result.Candidates = Order.Num();
	float BestScore = -TNumericLimits<float>::Max();
	bool bFoundUsable = false;
	for (int32 Index : Order)
	{
		const FSwarmAvoidanceCandidate& Candidate = Candidates[Index];
		const float Clearance = Probe(Candidate.Direction);
		const bool bUsable = Clearance > BoidFlight::ContactSkinCm;
		if (bFoundUsable && !bUsable) continue;
		const float Score = 3.f * Clearance / Lookahead + Candidate.Rank;
		if ((bUsable && !bFoundUsable) || Score > BestScore)
		{
			bFoundUsable = bUsable;
			BestScore = Score;
			Result.Direction = Candidate.Direction;
			Result.Clearance = Clearance;
		}
	}
	Result.bBlocked = !bFoundUsable;
	if (bFoundUsable && (!bHeld || FVector::DotProduct(Result.Direction, Runtime.HeldAvoidanceDirection) < .98f))
	{
		Runtime.HeldAvoidanceDirection = Result.Direction;
		Runtime.AvoidanceHoldSeconds = HoldSeconds;
	}
	Result.Direction = FMath::Lerp(Desired, Result.Direction, FMath::Clamp(Weight * Runtime.WeightScale, 0.f, 1.f)).GetSafeNormal();
	if (Result.Direction.IsNearlyZero()) Result.Direction = Desired;
	Result.Clearance = Probe(Result.Direction);
	return Result;
}
