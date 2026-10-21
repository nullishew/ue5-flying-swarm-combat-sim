// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/FlyingCharacter.h"
#include "Player/FlyingSwarmCombatSimPlayerController.h"
#include "Sound/SoundBase.h"

#include "Swarm/FlyingBoid.h"
#include "Game/FlyingSwarmCombatSimGameMode.h"
#include "Engine/World.h"
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
	auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AFlyingSwarmCombatSimGameMode>() : nullptr;
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

	if (IsLocallyControlled() && HitSound) UGameplayStatics::PlaySound2D(this, HitSound, HitVolume, 1.0f, 0.0f, nullptr, this, false);
	if (auto* LocalController = Cast<AFlyingSwarmCombatSimPlayerController>(GetController())) LocalController->PlayHitFeedback();

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

	if (bJustDied)
	{
		if (IsValid(Mode)) Mode->PlayerDied(this);
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

FFlyingShotPreview AFlyingCharacter::GetShotPreview() const
{
	FFlyingShotPreview Preview;
	if (!IsAlive() || !IsValid(FireOrigin) || !IsValid(Camera) || !GetWorld()
		|| !FMath::IsFinite(AimTraceDistance) || AimTraceDistance <= 0.0f)
	{
		return Preview;
	}

	const FVector CameraLocation = Camera->GetComponentLocation();
	const FVector CameraEnd = CameraLocation + Camera->GetForwardVector() * AimTraceDistance;
	FHitResult AimHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PlayerShotPreview), true, this);
	const bool bHasAimHit = GetWorld()->LineTraceSingleByChannel(
		AimHit, CameraLocation, CameraEnd, ECC_Visibility, QueryParams);
	const FVector AimPoint = bHasAimHit ? AimHit.ImpactPoint : CameraEnd;

	Preview.BeamStart = FireOrigin->GetComponentLocation();
	const FVector MuzzleDirection = (AimPoint - Preview.BeamStart).GetSafeNormal();
	if (MuzzleDirection.IsNearlyZero())
	{
		return Preview;
	}

	// A tiny extension registers an aimed surface at the trace endpoint. The same
	// first blocking hit drives preview, presentation and damage; no camera-hit fallback.
	const FVector MuzzleEnd = AimPoint + MuzzleDirection * 10.0f;
	const bool bShotBlocked = GetWorld()->LineTraceSingleByChannel(
		Preview.HitResult, Preview.BeamStart, MuzzleEnd, ECC_Visibility, QueryParams);
	Preview.BeamEnd = bShotBlocked ? Preview.HitResult.ImpactPoint : AimPoint;
	// Endpoint extension and surface precision are not muzzle obstructions.
	Preview.bMuzzleObstructed = bShotBlocked
		&& FVector::Dist(Preview.BeamStart, Preview.BeamEnd) + 1.0f
			< FVector::Dist(Preview.BeamStart, AimPoint);
	Preview.bValid = true;
	return Preview;
}

void AFlyingCharacter::Fire()
{
	if (bIsFiringRunShot) return;
	TGuardValue<bool> ShotGuard(bIsFiringRunShot, true);
	const FFlyingShotPreview Shot = GetShotPreview();
	if (!Shot.bValid)
	{
		return;
	}

	TWeakObjectPtr<AFlyingSwarmCombatSimGameMode> Mode(GetWorld()->GetAuthGameMode<AFlyingSwarmCombatSimGameMode>());
	const bool bRecordShot = Mode.IsValid() && Mode->IsRunPawn(this);
	if (bRecordShot && !Mode->BeginShot(this)) return;

	if (IsLocallyControlled() && PlayerShotSound) UGameplayStatics::PlaySound2D(this, PlayerShotSound, ShotVolume, 1.0f, 0.0f, nullptr, this, false);

	float ActualDamage = 0.0f;
	bool bKilled = false;
	if (Shot.HitResult.bBlockingHit && FMath::IsFinite(DamagePerShot) && DamagePerShot > 0.0f)
	{
		AFlyingBoid* HitBoid = Cast<AFlyingBoid>(Shot.HitResult.GetActor());
		if (IsValid(HitBoid) && HitBoid->IsAlive())
		{
			const float HealthBeforeShot = HitBoid->CurrentHealth;
			const TWeakObjectPtr<AFlyingBoid> DamagedBoid(HitBoid);
			ActualDamage = UGameplayStatics::ApplyPointDamage(
				HitBoid, DamagePerShot, (Shot.HitResult.TraceEnd - Shot.BeamStart).GetSafeNormal(),
				Shot.HitResult, GetController(), this, nullptr);
			// Damage delegates may destroy the target. Never dereference a raw target
			// after ApplyPointDamage; capture the result before player effects run.
			bKilled = ActualDamage > 0.0f && (ActualDamage >= HealthBeforeShot
				|| (DamagedBoid.IsValid() && !DamagedBoid->IsAlive()));
		}
	}

	// Close the shot transaction before any player presentation callback can reenter gameplay.
	if (bRecordShot && Mode.IsValid()) Mode->EndShot(this, ActualDamage, bKilled);
	if (!IsValid(this)) return;
	OnShotResolved(Shot.BeamStart, Shot.BeamEnd, Shot.HitResult);
	if (IsValid(this) && FMath::IsFinite(ActualDamage) && ActualDamage > 0.0f)
	{
		OnShotDamageConfirmed.Broadcast(ActualDamage, bKilled, Shot.BeamEnd);
	}
}
