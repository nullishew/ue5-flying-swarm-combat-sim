#pragma once

#include "CoreMinimal.h"
#include "PlayerRunStats.generated.h"

USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FPlayerRunStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Run Stats")
	float SurvivalSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Run Stats")
	int32 ShotsFired = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Run Stats")
	int32 ConfirmedHits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Run Stats")
	int32 KillCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Run Stats")
	float DamageDealt = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Run Stats")
	float AccuracyPercent = 0.0f;
};
