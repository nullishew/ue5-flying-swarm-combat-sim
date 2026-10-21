#include "Swarm/Flight/BoidFlightAlgorithms.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"

namespace BoidFlight
{
	bool SafeBounds(const UBoxComponent* Bounds, float Radius, float Margin,
		FTransform& Transform, FVector& Extent)
	{
		if (!IsValid(Bounds))
		{
			return false;
		}
		Transform = Bounds->GetComponentTransform();
		Extent = Bounds->GetScaledBoxExtent() - FVector(Radius + FMath::Max(0.0f, Margin));
		return Extent.GetMin() > ContactSkinCm;
	}

	FVector ClampPosition(const FVector& Position, const FTransform& Transform, const FVector& Extent)
	{
		const FVector Local = Transform.InverseTransformPositionNoScale(Position);
		return Transform.TransformPositionNoScale(FVector(
			FMath::Clamp(Local.X, -Extent.X, Extent.X),
			FMath::Clamp(Local.Y, -Extent.Y, Extent.Y),
			FMath::Clamp(Local.Z, -Extent.Z, Extent.Z)));
	}

	bool InsideBounds(const FVector& Position, const FTransform& Transform, const FVector& Extent)
	{
		const FVector Local = Transform.InverseTransformPositionNoScale(Position);
		return FMath::Abs(Local.X) <= Extent.X + UE_KINDA_SMALL_NUMBER
			&& FMath::Abs(Local.Y) <= Extent.Y + UE_KINDA_SMALL_NUMBER
			&& FMath::Abs(Local.Z) <= Extent.Z + UE_KINDA_SMALL_NUMBER;
	}

	void AppendEscapeDirections(TArray<FVector>& Directions)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				for (int32 Z = -1; Z <= 1; ++Z)
				{
					if (X || Y || Z)
					{
						Directions.Add(FVector(X, Y, Z).GetSafeNormal());
					}
				}
			}
		}
	}

	bool IsClear(UWorld* World, const FVector& Position, float Radius, ECollisionChannel Channel,
		const FCollisionQueryParams& Params, const FCollisionResponseParams& Responses)
	{
		return !World->OverlapBlockingTestByChannel(Position, FQuat::Identity, Channel,
			FCollisionShape::MakeSphere(Radius), Params, Responses);
	}

	float Clearance(UWorld* World, const UBoxComponent* Bounds, const FVector& Position,
		float Radius, const FVector& Direction, float Distance, float BoundsMargin, float ObstacleMargin,
		ECollisionChannel Channel, const FCollisionQueryParams& Params,
		const FCollisionResponseParams& Responses, TArray<FHitResult>* ScratchHits)
	{
		const FVector Unit = Direction.GetSafeNormal();
		if (Unit.IsNearlyZero() || Distance <= 0.0f)
		{
			return 0.0f;
		}
		FTransform Transform;
		FVector Extent;
		if (!SafeBounds(Bounds, Radius, BoundsMargin, Transform, Extent))
		{
			return 0.0f;
		}
		const FVector Local = Transform.InverseTransformPositionNoScale(Position);
		const FVector LocalDirection = Transform.InverseTransformVectorNoScale(Unit);
		float Result = Distance;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(LocalDirection[Axis]) > UE_SMALL_NUMBER)
			{
				const float Face = LocalDirection[Axis] > 0.0f ? Extent[Axis] : -Extent[Axis];
				Result = FMath::Min(Result, FMath::Max(0.0f, (Face - Local[Axis]) / LocalDirection[Axis]));
			}
		}

		TArray<FHitResult> LocalHits;
		TArray<FHitResult>& Hits = ScratchHits ? *ScratchHits : LocalHits;
		Hits.Reset();
		const FVector End = Position + Unit * Distance;
		World->SweepMultiByChannel(Hits, Position, End, FQuat::Identity, Channel,
			FCollisionShape::MakeSphere(Radius + FMath::Max(0.0f, ObstacleMargin)), Params, Responses);
		const bool bMarginOverlap = Hits.ContainsByPredicate([](const FHitResult& Hit)
		{
			return Hit.bBlockingHit && Hit.bStartPenetrating;
		});
		if (bMarginOverlap)
		{
			// The safety envelope is advisory. Once inside it, physical clearance
			// must still expose tangential and outward escape directions.
			Hits.Reset();
			World->SweepMultiByChannel(Hits, Position, End, FQuat::Identity, Channel,
				FCollisionShape::MakeSphere(Radius), Params, Responses);
		}
		for (const FHitResult& Hit : Hits)
		{
			if (!Hit.bBlockingHit)
			{
				continue;
			}
			if ((Hit.bStartPenetrating || Hit.Time <= UE_KINDA_SMALL_NUMBER)
				&& FVector::DotProduct(Unit, Hit.Normal) >= -UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Result = FMath::Min(Result, Hit.bStartPenetrating ? 0.0f : static_cast<float>(Hit.Distance));
		}
		return FMath::Clamp(Result, 0.0f, Distance);
	}
}
