// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FlyingCharacter.generated.h"

class UInputAction;
class USceneComponent;
struct FInputActionValue;

UCLASS()
class FLYINGSWARMCOMBATSIM_API AFlyingCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AFlyingCharacter();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<class USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<class UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Visual")
	TObjectPtr<USceneComponent> ShipVisualRoot;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Visual", meta = (ClampMin = "0.0"))
	float ShipRotationInterpSpeed = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Visual", meta = (ClampMin = "0.0"))
	float ShipRotationTiltScale = 4.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Visual", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float ShipMaxPitchTilt = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Visual", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float ShipMaxRollTilt = 30.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Visual", meta = (ClampMin = "0.0"))
	float ShipTiltRecenteringDelay = 0.08f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Visual")
	FRotator ShipVisualRotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0"))
	float FlightSpeed = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0"))
	float FlightAcceleration = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0"))
	float FlightBraking = 1800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	void UpdateShipVisualRotation(float DeltaTime);
	FRotator PreviousControlRotation = FRotator::ZeroRotator;
	FRotator TargetShipVisualTilt = FRotator::ZeroRotator;
	float TimeSinceViewRotationChanged = 0.0f;
	bool bControlRotationInitialized = false;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

public:
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

};
