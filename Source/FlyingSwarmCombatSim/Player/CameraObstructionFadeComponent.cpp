// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/CameraObstructionFadeComponent.h"
#include "Components/MeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/FlyingCharacter.h"


namespace
{
	float SafeNonNegative(float Value, float FallBack)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : FallBack;
	}
}

// Sets default values for this component's properties
UCameraObstructionFadeComponent::UCameraObstructionFadeComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}


// Called when the game starts
void UCameraObstructionFadeComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
	UpwardPitchThreshold = FMath::IsFinite(UpwardPitchThreshold) ? FMath::Clamp(UpwardPitchThreshold, 0.0f, 89.0f) : 45.0f;
	DownwardPitchThreshold = FMath::IsFinite(DownwardPitchThreshold) ? FMath::Clamp(DownwardPitchThreshold, -89.0f, 0.0f) : -45.0f;
	PitchRestoreMargin = FMath::IsFinite(PitchRestoreMargin) ? FMath::Clamp(PitchRestoreMargin, 0.0f, 89.0f) : 5.0f;
	CloseCameraDistance = SafeNonNegative(CloseCameraDistance, 200.0f);
	CloseCameraDistanceRestoreMargin = SafeNonNegative(CloseCameraDistanceRestoreMargin, 25.0f);
	FadeOutSeconds = SafeNonNegative(FadeOutSeconds, 0.1f);
	FadeInSeconds = SafeNonNegative(FadeInSeconds, 0.2f);
	ClearHoldSeconds = SafeNonNegative(ClearHoldSeconds, 0.1f);
	FadedOpacity = FMath::IsFinite(FadedOpacity) ? FMath::Clamp(FadedOpacity, 0.0f, 1.0f) : 0.1f;
	
	if (!Cast<AFlyingCharacter>(GetOwner()) || MeshComponentTag.IsNone() || OpacityParameterName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("PlayerCameraFade: requires FlyingCharacter owner and nonempty names."));
		SetComponentTickEnabled(false);
		return;
	}

	TArray<UMeshComponent*> OwnerMeshes;
	GetOwner()->GetComponents<UMeshComponent>(OwnerMeshes);
	for (UMeshComponent* Mesh : OwnerMeshes)
	{
		if (!IsValid(Mesh) || !Mesh->ComponentHasTag(MeshComponentTag)) continue;
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			if (!Mesh->GetMaterial(Slot))
			{
				UE_LOG(LogTemp, Warning, TEXT("PlayerCameraFade: missing material on %s slot %d."), *Mesh->GetName(), Slot);
				continue;
			}

			UMaterialInstanceDynamic* MID = Mesh->CreateDynamicMaterialInstance(Slot);
			if (IsValid(MID))
			{
				FadeMaterials.Add(MID);
			}
		}

	}

	RestoreOpaqueState();
	ApplyOpacity(1.0f);

	if (FadeMaterials.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("PlayerCameraFade: no tagged meshes or usable materials on %s."), *GetOwner()->GetName());
		SetComponentTickEnabled(false);
	}

}


// Called every frame
void UCameraObstructionFadeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
	// ensure valid player setup
	AFlyingCharacter* Pawn = Cast<AFlyingCharacter>(GetOwner());
	APlayerController* PC = IsValid(Pawn) ? Cast<APlayerController>(Pawn->GetController()) : nullptr;

	if (!IsValid(PC) || !PC->IsLocalController() || !Pawn->IsLocallyControlled() || PC->GetViewTarget() != Pawn)
	{
		RestoreOpaqueState();
		return;
	}

	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f) return;

	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector RootLocation = Pawn->GetActorLocation();
	if (ViewLocation.ContainsNaN() || ViewRotation.ContainsNaN() || RootLocation.ContainsNaN())
	{
		RestoreOpaqueState();
		return;
	}

	// handle pitch fade
	const float CurrentCameraPitch = FRotator::NormalizeAxis(ViewRotation.Pitch);
	const float CameraToRootDistance = static_cast<float>(FVector::Dist(ViewLocation, RootLocation));

	if (bEnablePitchFade)
	{
		// A restore margin never keeps a pitch trigger active across level pitch.
		const float UpwardExitPitch = FMath::Max(0.0f, UpwardPitchThreshold - PitchRestoreMargin);
		const float DownwardExitPitch = FMath::Min(0.0f,DownwardPitchThreshold + PitchRestoreMargin);
		bUpwardPitchActive = bUpwardPitchActive ? CurrentCameraPitch >= UpwardExitPitch : CurrentCameraPitch >= UpwardPitchThreshold;
		bDownwardPitchActive = bDownwardPitchActive ? CurrentCameraPitch <= DownwardExitPitch : CurrentCameraPitch <= DownwardPitchThreshold;
	}
	else
	{
		bUpwardPitchActive = false;
		bDownwardPitchActive = false;
	}
	const bool bPitchFadeActive = bUpwardPitchActive || bDownwardPitchActive;

	if (bEnableDistanceFade)
	{
		const float DistanceLimit = bDistanceFadeActive ? CloseCameraDistance + CloseCameraDistanceRestoreMargin : CloseCameraDistance;
		bDistanceFadeActive = CameraToRootDistance <= DistanceLimit;
	}
	else
	{
		bDistanceFadeActive = false;
	}

	if (bPitchFadeActive || bDistanceFadeActive)
	{
		SecondsClear = 0.0f;
		bShouldFade = true;
	}
	else
	{
		SecondsClear += DeltaTime;
		if (SecondsClear >= ClearHoldSeconds)
		{
			bShouldFade = false;
		}
	}

	// update player material opacity
	const float TargetOpacity = bShouldFade ? FadedOpacity : 1.0f;
	const float Duration = bShouldFade ? FadeOutSeconds : FadeInSeconds;
	const float NewOpacity = Duration <= SMALL_NUMBER ? TargetOpacity : FMath::FInterpConstantTo(CurrentOpacity, TargetOpacity, DeltaTime, (1.0f - FadedOpacity) / Duration);
	if (NewOpacity != CurrentOpacity)
	{
		CurrentOpacity = NewOpacity;
		ApplyOpacity(CurrentOpacity);
	}

}



void UCameraObstructionFadeComponent::ApplyOpacity(float Value)
{
	for (UMaterialInstanceDynamic* MID : FadeMaterials)
	{
		if (!IsValid(MID)) continue;
		MID->SetScalarParameterValue(OpacityParameterName, Value);
	}
}

void UCameraObstructionFadeComponent::RestoreOpaqueState()
{
	bUpwardPitchActive = false;
	bDownwardPitchActive = false;
	bDistanceFadeActive = false;
	bShouldFade = false;
	SecondsClear = 0.0f;
	if (CurrentOpacity != 1.0f)
	{
		CurrentOpacity = 1.0f;
		ApplyOpacity(1.0f);
	}
}

void UCameraObstructionFadeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ApplyOpacity(1.0f);
	FadeMaterials.Reset();
	Super::EndPlay(EndPlayReason);
}
