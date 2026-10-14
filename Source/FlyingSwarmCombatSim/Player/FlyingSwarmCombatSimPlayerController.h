// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FlyingSwarmCombatSimPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;

/**
 *  Simple first person Player Controller
 *  Manages the input mapping context.
 *  Overrides the Player Camera Manager class.
 */
UCLASS(abstract, config="Game")
class FLYINGSWARMCOMBATSIM_API AFlyingSwarmCombatSimPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:

	/** Recreate the optional combat widget after a runtime widget-class change. */
	UFUNCTION(BlueprintCallable, Category = "UI|Combat")
	void RefreshCombatHUD();

	virtual void SetPawn(APawn* InPawn) override;

	/** Constructor */
	AFlyingSwarmCombatSimPlayerController();

protected:

	/** Assign WBP_FlyingCombatHUD; only shown for a possessed FlyingCharacter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Combat")
	TSubclassOf<UUserWidget> CombatHUDWidgetClass;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Combat")
	TObjectPtr<UUserWidget> CombatHUDWidget;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;
};
