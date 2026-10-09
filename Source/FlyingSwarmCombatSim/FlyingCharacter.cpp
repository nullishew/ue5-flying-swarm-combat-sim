// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlyingCharacter.h"

#include "FlyingBoid.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Components/SceneComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
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

	FireOrigin = CreateDefaultSubobject<USceneComponent>(TEXT("FireOrigin"));
	FireOrigin->SetupAttachment(ShipVisualRoot);

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
	MaxHealth = FMath::IsFinite(MaxHealth) ? FMath::Max(1.0f, MaxHealth) : 100.0f;
	CurrentHealth = MaxHealth;
	bDead = false;
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

float AFlyingCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (!IsAlive() || bIsProcessingDamage || !FMath::IsFinite(DamageAmount) || DamageAmount <= 0.0f)
	{
		return 0.0f;
	}
	TGuardValue<bool> DamageGuard(bIsProcessingDamage, true);

	const float ProcessedDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!FMath::IsFinite(ProcessedDamage) || ProcessedDamage <= 0.0f)
	{
		return 0.0f;
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(OldHealth - ProcessedDamage, 0.0f, MaxHealth);
	const float ActualHealthLoss = OldHealth - CurrentHealth;
	if (ActualHealthLoss <= 0.0f)
	{
		return 0.0f;
	}

	const bool bJustDied = OldHealth > 0.0f && CurrentHealth <= 0.0f;
	if (bJustDied)
	{
		// Set the guard before callbacks so reentrant damage cannot announce death twice.
		bDead = true;
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		DisableInput(Cast<APlayerController>(GetController()));
	}

	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	if (bJustDied)
	{
		OnDeath.Broadcast();
	}
	return ActualHealthLoss;
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

	if (ensureMsgf(FireAction, TEXT("Assign IA_Fire on the AFlyingCharacter Blueprint defaults.")))
	{
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Started, this, &AFlyingCharacter::Fire);
	}
}

void AFlyingCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MoveInput = Value.Get<FVector2D>();
	if (!IsAlive() || Controller == nullptr)
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
	if (!IsAlive())
	{
		return;
	}

	const FVector2D LookInput = Value.Get<FVector2D>();
	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(LookInput.Y);

}

void AFlyingCharacter::Fire()
{
	if (!IsAlive() || !IsValid(FireOrigin) || !IsValid(Camera) || !GetWorld())
	{
		return;
	}

	const FVector CameraLocation = Camera->GetComponentLocation();
	const FVector CameraEnd = CameraLocation + Camera->GetForwardVector() * AimTraceDistance;
	FHitResult AimHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PlayerFireAim), true, this);
	const bool bHasAimHit = GetWorld()->LineTraceSingleByChannel(
		AimHit,
		CameraLocation,
		CameraEnd,
		ECC_Visibility,
		QueryParams);
	const FVector AimPoint = bHasAimHit ? AimHit.ImpactPoint : CameraEnd;

	const FVector BeamStart = FireOrigin->GetComponentLocation();
	FHitResult ShotHit;
	const bool bShotBlocked = GetWorld()->LineTraceSingleByChannel(
		ShotHit,
		BeamStart,
		AimPoint,
		ECC_Visibility,
		QueryParams);
	const FVector BeamEnd = bShotBlocked ? ShotHit.ImpactPoint : AimPoint;

	// Prefer the muzzle trace for nearby obstruction, but preserve the camera hit
	// when the muzzle trace ends exactly at or grazes the aimed surface.
	const FHitResult& ResolvedHit = bShotBlocked ? ShotHit : AimHit;

	// Damage uses the first blocking muzzle-path hit. Extend slightly beyond the
	// camera's aim point so the intended surface can register at the trace endpoint.
	const FVector MuzzleDirection = (AimPoint - BeamStart).GetSafeNormal();
	if (!MuzzleDirection.IsNearlyZero() && FMath::IsFinite(DamagePerShot) && DamagePerShot > 0.0f)
	{
		FHitResult DamageHit;
		const FVector DamageTraceEnd = AimPoint + MuzzleDirection * 10.0f;
		if (GetWorld()->LineTraceSingleByChannel(
			DamageHit,
			BeamStart,
			DamageTraceEnd,
			ECC_Visibility,
			QueryParams))
		{
			AFlyingBoid* HitBoid = Cast<AFlyingBoid>(DamageHit.GetActor());
			if (IsValid(HitBoid) && HitBoid->IsAlive())
			{
				UGameplayStatics::ApplyPointDamage(
					HitBoid,
					DamagePerShot,
					MuzzleDirection,
					DamageHit,
					GetController(),
					this,
					nullptr);
			}
		}
	}

	OnShotResolved(BeamStart, BeamEnd, ResolvedHit);
}
