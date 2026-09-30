// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlyingCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Math/RotationMatrix.h"

AFlyingCharacter::AFlyingCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(GetRootComponent());
	SpringArm->TargetArmLength = 450.f;
	SpringArm->bUsePawnControlRotation = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->bUsePawnControlRotation = false;

	ShipVisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ShipVisualRoot"));
	ShipVisualRoot->SetupAttachment(GetRootComponent());

	// The pawn/capsule stays upright. Camera pitch is represented by the visual pivot instead.
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = true;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxFlySpeed = FlightSpeed;
	Movement->MaxAcceleration = FlightAcceleration;
	Movement->BrakingDecelerationFlying = FlightBraking;
	Movement->bUseControllerDesiredRotation = false;
	Movement->bOrientRotationToMovement = false;
}

void AFlyingCharacter::BeginPlay()
{
	Super::BeginPlay();

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxFlySpeed = FlightSpeed;
	Movement->MaxAcceleration = FlightAcceleration;
	Movement->BrakingDecelerationFlying = FlightBraking;
	Movement->SetMovementMode(MOVE_Flying);
	ShipVisualRoot->SetRelativeRotation(ShipVisualRotationOffset);
}

void AFlyingCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateShipVisualRotation(DeltaTime);
}

void AFlyingCharacter::UpdateShipVisualRotation(float DeltaTime)
{
	if (Controller == nullptr || ShipVisualRoot == nullptr)
	{
		return;
	}

	const FRotator CurrentControlRotation = Controller->GetControlRotation();
	if (!bControlRotationInitialized)
	{
		PreviousControlRotation = CurrentControlRotation;
		bControlRotationInitialized = true;
	}
	else
	{
		const FRotator ViewRotationDelta = (CurrentControlRotation - PreviousControlRotation).GetNormalized();
		PreviousControlRotation = CurrentControlRotation;

		if (!ViewRotationDelta.IsNearlyZero())
		{
			TargetShipVisualTilt.Pitch = FMath::Clamp(
				ViewRotationDelta.Pitch * ShipRotationTiltScale,
				-ShipMaxPitchTilt,
				ShipMaxPitchTilt);
			TargetShipVisualTilt.Roll = FMath::Clamp(
				ViewRotationDelta.Yaw * ShipRotationTiltScale,
				-ShipMaxRollTilt,
				ShipMaxRollTilt);
			TimeSinceViewRotationChanged = 0.0f;
		}
		else
		{
			TimeSinceViewRotationChanged += DeltaTime;
			if (TimeSinceViewRotationChanged >= ShipTiltRecenteringDelay)
			{
				TargetShipVisualTilt = FRotator::ZeroRotator;
			}
		}
	}

	const FRotator TargetRelativeRotation = ShipVisualRotationOffset + TargetShipVisualTilt;
	const FRotator SmoothedRelativeRotation = FMath::RInterpTo(
		ShipVisualRoot->GetRelativeRotation(),
		TargetRelativeRotation,
		DeltaTime,
		ShipRotationInterpSpeed);
	ShipVisualRoot->SetRelativeRotation(SmoothedRelativeRotation);
}

void AFlyingCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!ensureMsgf(EnhancedInput, TEXT("AFlyingCharacter requires an Enhanced Input component.")))
	{
		return;
	}

	if (!ensureMsgf(MoveAction, TEXT("Assign IA_Move on the AFlyingCharacter Blueprint defaults.")) ||
		!ensureMsgf(LookAction, TEXT("Assign IA_Look on the AFlyingCharacter Blueprint defaults.")))
	{
		return;
	}

	EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AFlyingCharacter::Move);
	EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFlyingCharacter::Look);
}

void AFlyingCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MoveInput = Value.Get<FVector2D>();
	if (Controller == nullptr)
	{
		return;
	}

	// Full control rotation makes forward/back input climb or dive with the camera pitch.
	const FRotationMatrix ViewRotationMatrix(Controller->GetControlRotation());
	const FVector Forward = ViewRotationMatrix.GetUnitAxis(EAxis::X);
	const FVector Right = ViewRotationMatrix.GetUnitAxis(EAxis::Y);

	AddMovementInput(Forward, MoveInput.Y);
	AddMovementInput(Right, MoveInput.X);
}

void AFlyingCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookInput = Value.Get<FVector2D>();
	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(LookInput.Y);

}
