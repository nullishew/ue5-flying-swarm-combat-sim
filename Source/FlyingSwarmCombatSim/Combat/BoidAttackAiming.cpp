#include "Combat/BoidAttackAiming.h"

namespace BoidAttack
{
	FAimResult PredictAim(const FVector& Muzzle, const FVector& Target, const FVector& Velocity,
		float Speed, float Strength, float MaximumSeconds, float Lifespan)
	{
		FAimResult Result;
		if (Muzzle.ContainsNaN() || Target.ContainsNaN()) return Result;
		Result.Point = Target;
		Result.Direction = (Target - Muzzle).GetSafeNormal();
		if (Velocity.ContainsNaN() || !FMath::IsFinite(Speed) || Speed <= 0.f
			|| !FMath::IsFinite(Strength) || Strength <= 0.f || !FMath::IsFinite(MaximumSeconds)
			|| MaximumSeconds <= 0.f || !FMath::IsFinite(Lifespan) || Lifespan <= 0.f) return Result;

		const FVector Relative = Target - Muzzle;
		const double A = Velocity.SizeSquared() - static_cast<double>(Speed) * Speed;
		const double B = 2.0 * FVector::DotProduct(Relative, Velocity);
		const double C = Relative.SizeSquared();
		double Time = TNumericLimits<double>::Max();
		const auto Consider = [&Time](double Candidate)
		{
			if (FMath::IsFinite(Candidate) && Candidate > 0.0) Time = FMath::Min(Time, Candidate);
		};
		const double Tolerance = 1.e-8 * FMath::Max(1.0, Velocity.SizeSquared() + static_cast<double>(Speed) * Speed);
		if (FMath::Abs(A) <= Tolerance)
		{
			if (FMath::Abs(B) > 1.e-8) Consider(-C / B);
		}
		else
		{
			const double Discriminant = B * B - 4.0 * A * C;
			if (FMath::IsFinite(Discriminant) && Discriminant >= 0.0)
			{
				// Stable quadratic roots avoid cancellation for nearly equal speeds.
				const double Root = FMath::Sqrt(Discriminant);
				const double Q = -0.5 * (B + (B >= 0.0 ? Root : -Root));
				if (FMath::Abs(Q) > 1.e-8)
				{
					Consider(Q / A);
					Consider(C / Q);
				}
				else Consider(-B / (2.0 * A));
			}
		}
		if (Time == TNumericLimits<double>::Max()) return Result;
		const double AppliedTime = FMath::Min(Time, static_cast<double>(FMath::Min(MaximumSeconds, Lifespan)))
			* FMath::Clamp(Strength, 0.f, 1.f);
		const FVector Predicted = Target + Velocity * AppliedTime;
		if (Predicted.ContainsNaN()) return Result;
		Result.Point = Predicted;
		Result.Direction = (Predicted - Muzzle).GetSafeNormal();
		Result.PredictionSeconds = static_cast<float>(AppliedTime);
		return Result;
	}

	int32 NormalizeFanCount(int32 Count)
	{
		Count = FMath::Clamp(Count, 3, 9);
		return Count % 2 == 0 ? Count + 1 : Count;
	}

	TArray<FVector> FanDirections(const FVector& Aim, const FVector& Velocity, int32 Count, float TotalAngleDegrees)
	{
		TArray<FVector> Directions;
		if (Aim.ContainsNaN()) return Directions;
		const FVector Forward = Aim.GetSafeNormal();
		if (Forward.IsNearlyZero()) return Directions;
		FVector Side = Velocity.ContainsNaN() ? FVector::ZeroVector : FVector::VectorPlaneProject(Velocity, Forward);
		// Ignore tiny lateral velocity to avoid an unstable fan orientation.
		if (Side.SizeSquared() < 1.0) Side = FVector::CrossProduct(FVector::UpVector, Forward);
		if (Side.IsNearlyZero()) Side = FVector::CrossProduct(FVector::RightVector, Forward);
		Side.Normalize();
		Count = NormalizeFanCount(Count);
		const float Angle = FMath::IsFinite(TotalAngleDegrees) ? FMath::Clamp(TotalAngleDegrees, 0.f, 180.f) : 24.f;
		Directions.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Radians = FMath::DegreesToRadians(static_cast<double>(Angle) * (Index / static_cast<double>(Count - 1) - 0.5));
			Directions.Add((Forward * FMath::Cos(Radians) + Side * FMath::Sin(Radians)).GetSafeNormal());
		}
		return Directions;
	}

	int32 RandomSeed(int32 SimulationSeed, int32 AgentId)
	{
		return static_cast<int32>(HashCombine(GetTypeHash(SimulationSeed), GetTypeHash(AgentId)));
	}

	float AssignFanProbability(FRandomStream& Random, float Minimum, float Maximum)
	{
		Minimum = FMath::Clamp(Minimum, 0.f, 1.f);
		Maximum = FMath::Clamp(Maximum, 0.f, 1.f);
		return Random.FRandRange(FMath::Min(Minimum, Maximum), FMath::Max(Minimum, Maximum));
	}

	bool SelectFan(FRandomStream& Random, bool bEnabled, float Probability, float Distance, float MinimumDistance)
	{
		if (!bEnabled) return false;
		// Roll once even on close passes; proximity only substitutes the resulting pattern.
		const bool bSelected = Random.FRand() < FMath::Clamp(Probability, 0.f, 1.f);
		return bSelected && Distance >= MinimumDistance;
	}

	float AttemptDelay(FRandomStream& Random, float Interval, float Jitter, bool bFirst)
	{
		Jitter = FMath::Clamp(Jitter, 0.f, 0.95f);
		return Interval * Random.FRandRange(bFirst ? 0.5f : 1.f - Jitter, bFirst ? 1.5f : 1.f + Jitter);
	}
}
