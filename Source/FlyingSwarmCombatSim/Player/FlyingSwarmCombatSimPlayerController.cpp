// Copyright Epic Games, Inc. All Rights Reserved.


#include "Player/FlyingSwarmCombatSimPlayerController.h"
#include "Player/FlyingCharacter.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Player/FlyingSwarmCombatSimCameraManager.h"
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
	RefreshCombatHUD();

	
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

void AFlyingSwarmCombatSimPlayerController::SetPawn(APawn* InPawn)
{
	APawn* PreviousPawn = GetPawn();
	Super::SetPawn(InPawn);
	if (PreviousPawn != GetPawn() && HasActorBegunPlay())
	{
		RefreshCombatHUD();
	}
}

void AFlyingSwarmCombatSimPlayerController::RefreshCombatHUD()
{
	if (CombatHUDWidget)
	{
		// The widget's Destruct event unbinds its stored old pawn, not GetPawn().
		CombatHUDWidget->RemoveFromParent();
		CombatHUDWidget = nullptr;
	}
	if (IsLocalPlayerController() && IsValid(Cast<AFlyingCharacter>(GetPawn())) && CombatHUDWidgetClass)
	{
		CombatHUDWidget = CreateWidget<UUserWidget>(this, CombatHUDWidgetClass);
		if (CombatHUDWidget)
		{
			CombatHUDWidget->AddToPlayerScreen(10);
		}
	}
}

void AFlyingSwarmCombatSimPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CombatHUDWidget)
	{
		CombatHUDWidget->RemoveFromParent();
		CombatHUDWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
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
