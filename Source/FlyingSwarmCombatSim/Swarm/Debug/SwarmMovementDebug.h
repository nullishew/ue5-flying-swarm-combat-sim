#pragma once

#include "CoreMinimal.h"
#include "Swarm/BoidIntentTypes.h"
#include "Swarm/Positioning/BoidCombatMovementTypes.h"

// Observations only. None of these values participate in movement decisions.
enum class ESwarmMovementCommitResult : uint8
{
	NotAttempted,
	InvalidStep,
	InvalidBounds,
	FlightSpaceRejected,
	SweepBlocked,
	StartedPenetrating,
	Completed,
	Interrupted
};

struct FSwarmMovementCommitObservation
{
	ESwarmMovementCommitResult Result = ESwarmMovementCommitResult::NotAttempted;
	FVector RequestedDelta = FVector::ZeroVector;
	FVector ClampedDelta = FVector::ZeroVector;
	FVector HitNormal = FVector::ZeroVector;
	FString HitComponent;
	bool bBoundsClamped = false;
	bool bBlockingHit = false;
	bool bStartedPenetrating = false;
	bool bMoveReturned = false;
};

struct FSwarmMovementDebugSnapshot
{
	bool bHasSample = false;
	uint64 FrameNumber = MAX_uint64;
	uint64 StepNumber = 0;
	double WorldSeconds = 0.;
	float StepSeconds = 0.f;
	FVector StartPosition = FVector::ZeroVector;
	FVector BoundsCorrection = FVector::ZeroVector;
	FVector ActualDelta = FVector::ZeroVector;
	FVector Acceleration = FVector::ZeroVector;
	FVector SteeringHeading = FVector::ZeroVector;
	FVector FlightHeading = FVector::ZeroVector;
	float RequestedSpeed = 0.f;
	float ActualSpeed = 0.f;
	EBoidMovementPolicy RequestedPolicy = EBoidMovementPolicy::Cruise;
	EBoidMovementPolicy ResolvedPolicy = EBoidMovementPolicy::Cruise;
	bool bHasResolvedPolicy = false;
	bool bSteeringActive = false;
	bool bIntentionalStop = false;
	bool bHasGoal = false;
	float GoalDistance = 0.f;
	float ArrivalRadius = 0.f;
	EBoidCombatMovementConstraint Constraint = EBoidCombatMovementConstraint::None;
	FSwarmMovementCommitObservation Commit;
	FString Stage = TEXT("WAITING FOR SAMPLE");
	FString EffectiveProfile;
	float SightLossAge = 0.f;
	float SightLossGrace = 0.f;
	float MemoryAge = -1.f;
	float MemoryRemaining = 0.f;
	float StalledSeconds = 0.f;
	int32 RecoveryCount = 0;
	int32 RecoveryCountAtStart = 0;
	bool bRecoveryAttempted = false;
};
