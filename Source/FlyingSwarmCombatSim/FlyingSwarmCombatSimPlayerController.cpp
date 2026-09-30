// Copyright Epic Games, Inc. All Rights Reserved.


#include "FlyingSwarmCombatSimPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "FlyingSwarmCombatSimCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "FlyingSwarmCombatSim.h"
#include "Widgets/Input/SVirtualJoystick.h"

AFlyingSwarmCombatSimPlayerController::AFlyingSwarmCombatSimPlayerController()
{
	// set the player camera manager class
	PlayerCameraManagerClass = AFlyingSwarmCombatSimCameraManager::StaticClass();
}

void AFlyingSwarmCombatSimPlayerController::BeginPlay()
{
	Super::BeginPlay();

	
	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController() && ShouldUseTouchControls())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogFlyingSwarmCombatSim, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void AFlyingSwarmCombatSimPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Context
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
	
}

bool AFlyingSwarmCombatSimPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
