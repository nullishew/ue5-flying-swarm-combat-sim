#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"

class UBoxComponent;
class UWorld;

namespace BoidFlight
{
	constexpr float ContactSkinCm = 1.0f;

	bool SafeBounds(const UBoxComponent* Bounds, float Radius, float Margin,
		FTransform& Transform, FVector& Extent);
	FVector ClampPosition(const FVector& Position, const FTransform& Transform, const FVector& Extent);
	bool InsideBounds(const FVector& Position, const FTransform& Transform, const FVector& Extent);
	void AppendEscapeDirections(TArray<FVector>& Directions);
	bool IsClear(UWorld* World, const FVector& Position, float Radius, ECollisionChannel Channel,
		const FCollisionQueryParams& Params, const FCollisionResponseParams& Responses);
	float Clearance(UWorld* World, const UBoxComponent* Bounds, const FVector& Position,
		float Radius, const FVector& Direction, float Distance, float BoundsMargin, float ObstacleMargin,
		ECollisionChannel Channel, const FCollisionQueryParams& Params,
		const FCollisionResponseParams& Responses);
}
