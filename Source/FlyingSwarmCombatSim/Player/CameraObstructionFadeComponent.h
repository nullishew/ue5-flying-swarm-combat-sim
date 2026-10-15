// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CameraObstructionFadeComponent.generated.h"


class UMaterialInstanceDynamic;
class UMeshComponent;


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class FLYINGSWARMCOMBATSIM_API UCameraObstructionFadeComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UCameraObstructionFadeComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade")
	FName MeshComponentTag = TEXT("PlayerCameraFade");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade")
	FName OpacityParameterName = TEXT("PlayerOpacity");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade|Pitch")
	bool bEnablePitchFade = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade|Pitch", meta = (ClampMin = 0.0, ClampMax = 90.0, Units = "deg"))
	float UpwardPitchThreshold = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade|Pitch", meta = (ClampMin = -90.0, ClampMax = 0.0, Units = "deg"))
	float DownwardPitchThreshold = -45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade|Pitch", meta = (ClampMin = 0.0, ClampMax = 90.0, Units = "deg"))
	float PitchRestoreMargin = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade|Distance")
	bool bEnableDistanceFade = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade|Distance", meta = (ClampMin = 0.0, Units = "cm"))
	float CloseCameraDistance = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade|Distance", meta = (ClampMin = 0.0, Units = "cm"))
	float CloseCameraDistanceRestoreMargin = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float FadedOpacity = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade", meta = (ClampMin = 0.0, Units = "s"))
	float FadeOutSeconds = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade", meta = (ClampMin = 0.0, Units = "s"))
	float FadeInSeconds = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerCameraFade", meta = (ClampMin = 0.0, Units = "s"))
	float ClearHoldSeconds = 0.1f;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:	
	void ApplyOpacity(float Value);
	void RestoreOpaqueState();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FadeMaterials;

	bool bUpwardPitchActive = false;
	bool bDownwardPitchActive = false;
	bool bShouldFade = false;
	bool bDistanceFadeActive = false;
	float CurrentOpacity = 1.0f;
	float SecondsClear = 0.0f;
		
};
