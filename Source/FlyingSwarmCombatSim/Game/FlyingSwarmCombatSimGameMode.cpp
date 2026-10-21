// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/FlyingSwarmCombatSimGameMode.h"
#include "Player/FlyingCharacter.h"
#include "Player/FlyingSwarmCombatSimPlayerController.h"
#include "Engine/World.h"

AFlyingSwarmCombatSimGameMode::AFlyingSwarmCombatSimGameMode()
{
	bUseSeamlessTravel = false;
}

bool AFlyingSwarmCombatSimGameMode::IsPlaying() const
{
	return State == EGameRunState::Playing && !bDeathPending;
}

bool AFlyingSwarmCombatSimGameMode::IsRunPawn(const AFlyingCharacter* Pawn) const
{
	return Pawn && RunPawn.Get() == Pawn;
}

bool AFlyingSwarmCombatSimGameMode::BeginRun(AFlyingSwarmCombatSimPlayerController* Controller, AFlyingCharacter* Pawn)
{
	if (State != EGameRunState::Starting || !IsValid(Controller) || !IsValid(Pawn) || !Pawn->IsAlive() || Controller->GetPawn() != Pawn)
	{
		return false;
	}
	RunPawn = Pawn;
	RunController = Controller;
	Stats = FPlayerRunStats();
	bShotPending = false;
	bDeathPending = false;
	StartSeconds = GetWorld()->GetTimeSeconds();
	State = EGameRunState::Playing;
	return true;
}

FPlayerRunStats AFlyingSwarmCombatSimGameMode::GetRunStats() const
{
	FPlayerRunStats Result = Stats;
	if (IsPlaying())
	{
		Result.SurvivalSeconds = FMath::Max(0.0, GetWorld()->GetTimeSeconds() - StartSeconds);
	}
	Result.AccuracyPercent = Result.ShotsFired > 0 ? 100.0f * Result.ConfirmedHits / Result.ShotsFired : 0.0f;
	return Result;
}

bool AFlyingSwarmCombatSimGameMode::BeginShot(AFlyingCharacter* Pawn)
{
	if (!IsPlaying() || bShotPending || RunPawn.Get() != Pawn || !IsValid(Pawn) || !Pawn->IsAlive())
	{
		return false;
	}
	bShotPending = true;
	++Stats.ShotsFired;
	return true;
}

void AFlyingSwarmCombatSimGameMode::EndShot(AFlyingCharacter* Pawn, float ActualDamage, bool bKilled)
{
	if (!bShotPending || RunPawn != TWeakObjectPtr<AFlyingCharacter>(Pawn)) return;
	if (FMath::IsFinite(ActualDamage) && ActualDamage > 0.0f)
	{
		++Stats.ConfirmedHits;
		Stats.DamageDealt += ActualDamage;
		if (bKilled) ++Stats.KillCount;
	}
	bShotPending = false;
	if (bDeathPending) FinalizeDeath();
}

void AFlyingSwarmCombatSimGameMode::PlayerDied(AFlyingCharacter* Pawn)
{
	if (!IsPlaying() || RunPawn.Get() != Pawn) return;
	Stats.SurvivalSeconds = FMath::Max(0.0, GetWorld()->GetTimeSeconds() - StartSeconds);
	bDeathPending = true;
	// Capture the view now; possession/destruction waits until a controller tick.
	if (RunController.IsValid()) RunController->CaptureDeathView();
	if (!bShotPending) FinalizeDeath();
}

void AFlyingSwarmCombatSimGameMode::FinalizeDeath()
{
	State = EGameRunState::Spectating;
	bDeathPending = false;
	if (RunController.IsValid()) RunController->QueueSpectator();
}

bool AFlyingSwarmCombatSimGameMode::BeginRestart(AFlyingSwarmCombatSimPlayerController* Controller)
{
	if (State != EGameRunState::Spectating || RunController.Get() != Controller)
	{
		return false;
	}
	State = EGameRunState::Restarting;
	return true;
}

void AFlyingSwarmCombatSimGameMode::CancelRestart()
{
	if (State == EGameRunState::Restarting)
	{
		State = EGameRunState::Spectating;
	}
}
