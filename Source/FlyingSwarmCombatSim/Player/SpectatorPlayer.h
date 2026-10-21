// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SpectatorPlayer.generated.h"

class UCameraComponent;
class UFloatingPawnMovement;

UCLASS()
class FLYINGSWARMCOMBATSIM_API ASpectatorPlayer : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	ASpectatorPlayer();
	void InitializeView(float FieldOfView, float Speed);
	void MoveView(const FVector2D& Input);

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UFloatingPawnMovement> Flight;
};
