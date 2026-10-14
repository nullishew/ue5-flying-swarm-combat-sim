#pragma once

#include "CoreMinimal.h"
#include "BoidAttackTypes.generated.h"

UENUM(BlueprintType)
enum class EBoidAttackRejection : uint8
{
	None,
	ContextInactive,
	BoidDead,
	NoDetectedTarget,
	TargetInvalid,
	NoSight,
	NotDue,
	NoProjectileClass,
	InvalidConfiguration,
	OutOfRange,
	NotPositionReady,
	MuzzleInvalid,
	MuzzleBlocked,
	SpawnFailed
};

USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FBoidAttackFacts
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boid|Attack") bool bContextActive = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boid|Attack") bool bCommonEligible = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boid|Attack") bool bNormalEligible = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boid|Attack") bool bApproachEligible = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boid|Attack") bool bCloseEligible = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boid|Attack") bool bAttemptDue = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boid|Attack") EBoidAttackRejection LastRejection = EBoidAttackRejection::ContextInactive;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boid|Attack") int32 ShotCount = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boid|Attack", meta=(Units="s")) float SecondsUntilAttempt = 0.f;
};
