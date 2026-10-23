#pragma once

#include "CoreMinimal.h"

namespace SwarmTransit
{
	// The caller verifies the final route segment or local pursuit destination; only actual, unclipped motion qualifies.
	inline bool ReachedGoal(bool bEligibleDestination, bool bUnobstructedMove, const FVector& Start, const FVector& End, const FVector& Goal, float Radius)
	{
		if (!bEligibleDestination || !bUnobstructedMove || Start.ContainsNaN() || End.ContainsNaN() || Goal.ContainsNaN() || !FMath::IsFinite(Radius) || Radius < 0.f) return false;
		const FVector Segment = End - Start;
		const double LengthSquared = Segment.SizeSquared();
		const double Alpha = LengthSquared > UE_SMALL_NUMBER ? FMath::Clamp(FVector::DotProduct(Goal - Start, Segment) / LengthSquared, 0., 1.) : 0.;
		return FVector::DistSquared(Goal, Start + Segment * Alpha) <= FMath::Square(static_cast<double>(Radius));
	}
}
