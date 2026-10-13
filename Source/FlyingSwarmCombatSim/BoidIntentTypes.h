#pragma once

#include "CoreMinimal.h"
#include "BoidIntentTypes.generated.h"

UENUM(BlueprintType)
enum class EBoidRangedPhaseControl : uint8 { Automatic, StateTree };
UENUM(BlueprintType)
enum class EBoidRangedPhaseRequest : uint8 { Approach, Orbit };

UENUM(BlueprintType)
enum class EBoidMovementPolicy : uint8
{
	Cruise,
	FollowStandOff,
	LocationArrival,
	RangedOrbit
};

/** Runner-consumed destination and arrival settings selected by boid state tasks. */
USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FBoidMovementIntent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	EBoidMovementPolicy Policy = EBoidMovementPolicy::Cruise;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	bool bHasGoal = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	FVector Goal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float ArrivalRadiusCm = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float StandOffDistanceCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumTargetDistanceCm = 300.0f;

	/** Only explicit last-known transit tasks use this flag; readiness is never allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement") bool bIsTransit = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement") EBoidRangedPhaseControl RangedPhaseControl = EBoidRangedPhaseControl::Automatic;

	/** Advances only when the effective policy, destination, or arrival settings change. */
	uint32 Revision = 0;
};
