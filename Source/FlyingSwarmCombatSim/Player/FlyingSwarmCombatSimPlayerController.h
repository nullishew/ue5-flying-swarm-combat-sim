// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "FlyingSwarmCombatSimPlayerController.generated.h"

class ACameraActor;
class AFlyingCharacter;
class AFlyingSwarmCombatSimGameMode;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UUserWidget;
class UGameplayHUDWidget;
struct FInputActionValue;

/** Owns local run UI and input across the player-to-spectator possession change. */
UCLASS(abstract, Config = Game)
class FLYINGSWARMCOMBATSIM_API AFlyingSwarmCombatSimPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFlyingSwarmCombatSimPlayerController();
	virtual void SetPawn(APawn* InPawn) override;
	virtual void PlayerTick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "UI|Combat")
	void RefreshCombatHUD();

	UFUNCTION(BlueprintCallable, Category = "Run")
	void RequestRetry();

	void CaptureDeathView();
	void QueueSpectator();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	bool ShouldUseTouchControls() const;

	// Retain these names/types so existing controller Blueprint assignments survive.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Combat")
	TSubclassOf<UUserWidget> CombatHUDWidgetClass;

	/** Optional separate statistics widget; the original combat HUD needs no new parent. */
	UPROPERTY(EditDefaultsOnly, Category = "Run|UI")
	TSubclassOf<UGameplayHUDWidget> RunStatsWidgetClass;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Combat")
	TObjectPtr<UUserWidget> CombatHUDWidget;

	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	UPROPERTY(EditAnywhere, Category = "Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Required editor-authored Boolean action for Retry. */
	UPROPERTY(EditDefaultsOnly, Category = "Run|Input")
	TObjectPtr<UInputAction> RetryAction;

	UPROPERTY(EditDefaultsOnly, Category = "Run|Input")
	TObjectPtr<UInputAction> SpectatorMoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Run|Input")
	TObjectPtr<UInputAction> SpectatorLookAction;

	/** Required editor-authored context mapping RetryAction to the desired key. */
	UPROPERTY(EditDefaultsOnly, Category = "Run|Input")
	TObjectPtr<UInputMappingContext> SessionMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Run|Input")
	TObjectPtr<UInputMappingContext> SpectatorMappingContext;

private:
	AFlyingSwarmCombatSimGameMode* RunMode() const;
	UGameplayHUDWidget* RunHUD() const;
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem() const;
	void EnsureSpectatorInputDefaults();
	void TryBindRunInput();
	void TryStartRun();
	bool TryInstallContexts(bool bSpectating);
	void RemoveOwnedContexts();
	bool HasGameFocus() const;
	bool AllRunKeysReleased() const;
	void EnterSpectator();
	void PresentHUD();
	void SpectatorMove(const FInputActionValue& Value);
	void SpectatorLook(const FInputActionValue& Value);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputMappingContext>> OwnedContexts;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> FallbackCamera;

	UPROPERTY(Transient)
	TObjectPtr<UGameplayHUDWidget> RunStatsWidget;

	TWeakObjectPtr<AFlyingCharacter> DeadPawn;
	TSet<FKey> ReleaseKeys;
	FVector DeathLocation = FVector::ZeroVector;
	FRotator DeathRotation = FRotator::ZeroRotator;
	float DeathFOV = 90.0f;
	float SpectatorSpeed = 900.0f;
	bool bControlsArmed = false;
	bool bRetryBindingReady = false;
	bool bSpectatorBindingsReady = false;
	bool bMappingsReady = false;
	bool bUsingSpectatorContexts = false;
	bool bPendingSpectator = false;
	FDelegateHandle TravelFailureHandle;
};
