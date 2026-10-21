// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Game/PlayerRunStats.h"
#include "Game/GameRunStates.h"
#include "FlyingSwarmCombatSimGameMode.generated.h"

class AFlyingCharacter;
class AFlyingSwarmCombatSimPlayerController;

UCLASS(abstract)
class FLYINGSWARMCOMBATSIM_API AFlyingSwarmCombatSimGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFlyingSwarmCombatSimGameMode();

	UFUNCTION(BlueprintPure, Category = "Run")
	EGameRunState GetGameRunState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "Run")
	FPlayerRunStats GetRunStats() const;

	bool IsPlaying() const;
	bool IsRunPawn(const AFlyingCharacter* Pawn) const;
	bool BeginRun(AFlyingSwarmCombatSimPlayerController* Controller, AFlyingCharacter* Pawn);
	bool BeginShot(AFlyingCharacter* Pawn);
	void EndShot(AFlyingCharacter* Pawn, float ActualDamage, bool bKilled);
	void PlayerDied(AFlyingCharacter* Pawn);
	bool BeginRestart(AFlyingSwarmCombatSimPlayerController* Controller);
	void CancelRestart();

private:
	void FinalizeDeath();

	UPROPERTY(Transient)
	EGameRunState State = EGameRunState::Starting;

	UPROPERTY(Transient)
	FPlayerRunStats Stats;

	TWeakObjectPtr<AFlyingCharacter> RunPawn;
	TWeakObjectPtr<AFlyingSwarmCombatSimPlayerController> RunController;
	double StartSeconds = 0.0;
	bool bShotPending = false;
	bool bDeathPending = false;
};
