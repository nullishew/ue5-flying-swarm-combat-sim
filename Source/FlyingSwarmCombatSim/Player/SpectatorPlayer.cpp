// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/SpectatorPlayer.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Math/RotationMatrix.h"

// Sets default values
ASpectatorPlayer::ASpectatorPlayer()
{
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	SetRootComponent(Camera);
	Camera->bUsePawnControlRotation = true;
	Flight = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Flight"));
	Flight->SetUpdatedComponent(Camera);
	Flight->Acceleration = 4000.0f;
	Flight->Deceleration = 4000.0f;
	SetActorEnableCollision(false);
	SetCanBeDamaged(false);
	bReplicates = false;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

void ASpectatorPlayer::InitializeView(float FieldOfView, float Speed)
{
	Camera->SetFieldOfView(FMath::IsFinite(FieldOfView) ? FMath::Clamp(FieldOfView, 5.0f, 170.0f) : 90.0f);
	Flight->MaxSpeed = FMath::IsFinite(Speed) ? FMath::Max(1.0f, Speed) : 900.0f;
}

void ASpectatorPlayer::MoveView(const FVector2D& Input)
{
	if (!Controller) return;
	const FRotationMatrix View(GetControlRotation());
	AddMovementInput(View.GetUnitAxis(EAxis::X), Input.Y);
	AddMovementInput(View.GetUnitAxis(EAxis::Y), Input.X);
}
