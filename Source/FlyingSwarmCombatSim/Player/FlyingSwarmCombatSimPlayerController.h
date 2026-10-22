// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Swarm/Debug/SwarmDebugSettings.h"
#include "Templates/UniquePtr.h"
#include "FlyingSwarmCombatSimPlayerController.generated.h"

class ACameraActor;
class AFlyingCharacter;
class AFlyingSwarmCombatSimGameMode;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UUserWidget;
class UGameplayHUDWidget;
class UHitFeedbackWidget;
class UMaterialInterface;
struct FInputActionValue;
class FSwarmDebugPresenter;
class USwarmDebugOverlayWidget;
class UMaterialInstanceDynamic;
class AFlyingSwarmCombatSimCameraManager;

struct FSwarmDebugPresenterDeleter
{
	void operator()(FSwarmDebugPresenter* Presenter) const;
};

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

	void PlayHitFeedback();

	void CaptureDeathView();
	void QueueSpectator();

protected:
	/** Optional Enhanced Input debug controls. The action assets should be configured to consume input. */
	UPROPERTY(EditDefaultsOnly, Category = "Swarm Debug")
	FSwarmDebugSettings SwarmDebug;

	UPROPERTY(EditDefaultsOnly, Category = "Swarm Debug|Input")
	TObjectPtr<UInputMappingContext> DebugMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Swarm Debug|Input")
	TObjectPtr<UInputAction> ToggleAwarenessAction;

	UPROPERTY(EditDefaultsOnly, Category = "Swarm Debug|Input")
	TObjectPtr<UInputAction> ToggleSpatialAction;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	bool ShouldUseTouchControls() const;

	/** Cosmetic rotation in degrees; zero disables shake. Does not change control rotation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat Feedback|Player Hit", meta = (ClampMin = "0", ClampMax = "2"))
	float ShakeDegrees = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat Feedback|Player Hit", meta = (ClampMin = "0.01", ClampMax = "1"))
	float ShakeSeconds = 0.18f;

	/** UI material with HitColor (vector) and HitOpacity (scalar) parameters. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat Feedback|Vignette")
	TObjectPtr<UMaterialInterface> HitVignetteMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat Feedback|Vignette", meta = (HideAlphaChannel))
	FLinearColor HitColor = FLinearColor(0.55f, 0.015f, 0.025f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat Feedback|Vignette", meta = (ClampMin = "0", ClampMax = "1"))
	float VignetteOpacity = 0.45f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat Feedback|Vignette", meta = (ClampMin = "0.001", ClampMax = "1"))
	float VignetteFadeInSeconds = 0.035f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat Feedback|Vignette", meta = (ClampMin = "0.001", ClampMax = "3"))
	float VignetteFadeOutSeconds = 0.3f;

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
	void ToggleSwarmAwareness();
	void ToggleSwarmSpatial();
	void UpdateSwarmDebug(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void StopSwarmDebug();

	UPROPERTY(Transient)
	TObjectPtr<USwarmDebugOverlayWidget> SwarmDebugOverlay;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DebugOutlineMaterial;
	TWeakObjectPtr<UMaterialInterface> DebugOutlineSource;
	TWeakObjectPtr<AFlyingSwarmCombatSimCameraManager> DebugCameraManager;
	TWeakObjectPtr<UInputComponent> DebugBoundInput;
	bool bDebugAwarenessBindingReady = false;
	bool bDebugSpatialBindingReady = false;
	TUniquePtr<FSwarmDebugPresenter, FSwarmDebugPresenterDeleter> SwarmDebugPresenter;
	FDelegateHandle SwarmDebugTickHandle;
	bool bSwarmAwareness = false;
	bool bSwarmSpatial = false;

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

	UPROPERTY(Transient)
	TObjectPtr<UHitFeedbackWidget> HitFeedbackWidget;

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
