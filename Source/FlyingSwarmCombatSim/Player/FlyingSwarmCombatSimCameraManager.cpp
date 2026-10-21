// Copyright Epic Games, Inc. All Rights Reserved.


#include "Player/FlyingSwarmCombatSimCameraManager.h"

AFlyingSwarmCombatSimCameraManager::AFlyingSwarmCombatSimCameraManager()
{
	// set the min/max pitch
	ViewPitchMin = -70.0f;
	ViewPitchMax = 80.0f;
}

void AFlyingSwarmCombatSimCameraManager::PlayHitShake(float Degrees, float Duration)
{
	// Restart one bounded impulse instead of stacking shakes during rapid hits.
	HitShakeAge = 0.0f;
	HitShakeDegrees = FMath::Clamp(Degrees, 0.0f, 2.0f);
	HitShakeDuration = FMath::Max(0.01f, Duration);
	bHitShakeActive = true;
}

void AFlyingSwarmCombatSimCameraManager::ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	Super::ApplyCameraModifiers(DeltaTime, InOutPOV);
	if (!bHitShakeActive) return;
	HitShakeAge += FMath::Max(0.0f, DeltaTime);
	const float Progress = FMath::Clamp(HitShakeAge / HitShakeDuration, 0.0f, 1.0f);
	const float Amplitude = HitShakeDegrees * FMath::Square(1.0f - Progress);
	const float Phase = HitShakeAge * 2.0f * PI;
	InOutPOV.Rotation.Pitch += Amplitude * FMath::Sin(Phase * 19.0f);
	InOutPOV.Rotation.Yaw += Amplitude * 0.5f * FMath::Sin(Phase * 23.0f);
	InOutPOV.Rotation.Roll += Amplitude * 0.4f * FMath::Sin(Phase * 17.0f);
	bHitShakeActive = Progress < 1.0f;
}
