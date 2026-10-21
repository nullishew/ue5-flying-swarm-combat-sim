// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "FlyingSwarmCombatSimCameraManager.generated.h"

/**
 *  Basic First Person camera manager.
 *  Limits min/max look pitch.
 */
UCLASS()
class AFlyingSwarmCombatSimCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()
	
public:

	/** Constructor */
	AFlyingSwarmCombatSimCameraManager();
	void PlayHitShake(float Degrees, float Duration);

protected:
	virtual void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

private:
	float HitShakeAge = 0.0f;
	float HitShakeDegrees = 0.0f;
	float HitShakeDuration = 0.18f;
	bool bHitShakeActive = false;
};
