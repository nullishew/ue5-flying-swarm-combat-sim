#pragma once

#include "CoreMinimal.h"

/** Pure attack geometry and seeded scheduling helpers; no actor or movement ownership. */
namespace BoidAttack
{
	struct FAimResult
	{
		FVector Point = FVector::ZeroVector;
		FVector Direction = FVector::ZeroVector;
		float PredictionSeconds = 0.f;
	};

	FLYINGSWARMCOMBATSIM_API FAimResult PredictAim(const FVector& Muzzle, const FVector& Target,
		const FVector& Velocity, float Speed, float Strength, float MaximumSeconds, float Lifespan);
	FLYINGSWARMCOMBATSIM_API int32 NormalizeFanCount(int32 Count);
	FLYINGSWARMCOMBATSIM_API TArray<FVector> FanDirections(const FVector& Aim, const FVector& Velocity,
		int32 Count, float TotalAngleDegrees);
	FLYINGSWARMCOMBATSIM_API int32 RandomSeed(int32 SimulationSeed, int32 AgentId);
	FLYINGSWARMCOMBATSIM_API float AssignFanProbability(FRandomStream& Random, float Minimum, float Maximum);
	FLYINGSWARMCOMBATSIM_API bool SelectFan(FRandomStream& Random, bool bEnabled, float Probability,
		float Distance, float MinimumDistance);
	FLYINGSWARMCOMBATSIM_API float AttemptDelay(FRandomStream& Random, float Interval, float Jitter, bool bFirst);
}
