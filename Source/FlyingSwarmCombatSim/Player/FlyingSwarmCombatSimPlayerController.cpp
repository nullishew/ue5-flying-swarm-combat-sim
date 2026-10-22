// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/FlyingSwarmCombatSimPlayerController.h"
#include "Player/FlyingCharacter.h"
#include "Player/FlyingSwarmCombatSimCameraManager.h"
#include "Player/SpectatorPlayer.h"
#include "UI/GameplayHUDWidget.h"
#include "UI/HitFeedbackWidget.h"
#include "UI/SwarmDebugOverlayWidget.h"
#include "Swarm/Debug/SwarmDebugPresenter.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Game/FlyingSwarmCombatSimGameMode.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PawnMovementComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "FlyingSwarmCombatSim.h"
#include "UnrealClient.h"
#include "Widgets/Input/SVirtualJoystick.h"

void FSwarmDebugPresenterDeleter::operator()(FSwarmDebugPresenter* Presenter) const
{
	delete Presenter;
}

AFlyingSwarmCombatSimPlayerController::AFlyingSwarmCombatSimPlayerController()
{
	PlayerCameraManagerClass = AFlyingSwarmCombatSimCameraManager::StaticClass();
}

AFlyingSwarmCombatSimGameMode* AFlyingSwarmCombatSimPlayerController::RunMode() const
{
	return GetWorld() ? GetWorld()->GetAuthGameMode<AFlyingSwarmCombatSimGameMode>() : nullptr;
}

UGameplayHUDWidget* AFlyingSwarmCombatSimPlayerController::RunHUD() const
{
	return RunStatsWidget;
}

UEnhancedInputLocalPlayerSubsystem* AFlyingSwarmCombatSimPlayerController::InputSubsystem() const
{
	const ULocalPlayer* Local = GetLocalPlayer();
	return Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

void AFlyingSwarmCombatSimPlayerController::RefreshCombatHUD()
{
	if (!IsLocalPlayerController()) return;
	// Preserve the existing Blueprint HUD; its base class is not a compatibility test.
	UClass* DesiredClass = CombatHUDWidgetClass.Get();
	if (!DesiredClass)
		DesiredClass = LoadClass<UUserWidget>(
			nullptr, TEXT("/Game/Player/WBP_FlyingCombatHUD.WBP_FlyingCombatHUD_C"));
	if (CombatHUDWidget && CombatHUDWidget->GetClass() != DesiredClass)
	{
		CombatHUDWidget->RemoveFromParent();
		CombatHUDWidget = nullptr;
	}
	if (!CombatHUDWidget && Cast<AFlyingCharacter>(GetPawn()) && DesiredClass && !DesiredClass->HasAnyClassFlags(
		CLASS_Abstract))
	{
		CombatHUDWidget = CreateWidget<UUserWidget>(this, DesiredClass);
		if (CombatHUDWidget)
		{
			CombatHUDWidget->AddToPlayerScreen(10);
		}
	}
	// This widget only displays statistics; never replace or reparent the original combat/health HUD.
	UClass* DesiredStatsClass = RunStatsWidgetClass ? RunStatsWidgetClass.Get() : UGameplayHUDWidget::StaticClass();
	if (DesiredStatsClass->HasAnyClassFlags(CLASS_Abstract)) DesiredStatsClass = UGameplayHUDWidget::StaticClass();
	if (RunStatsWidget && RunStatsWidget->GetClass() != DesiredStatsClass)
	{
		RunStatsWidget->RemoveFromParent();
		RunStatsWidget = nullptr;
	}
	if (!RunStatsWidget) RunStatsWidget = CreateWidget<UGameplayHUDWidget>(this, DesiredStatsClass);
	if (RunStatsWidget && !RunStatsWidget->IsInViewport())
	{
		RunStatsWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		RunStatsWidget->AddToPlayerScreen(11);
	}
	PresentHUD();
}

void AFlyingSwarmCombatSimPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalPlayerController()) return;
	SwarmDebugTickHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ThisClass::UpdateSwarmDebug);
	RefreshCombatHUD();
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();
	if (GEngine) TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::HandleTravelFailure);
	// Keep the template's optional touch widget; desktop is the supported run input setup.
	if (ShouldUseTouchControls() && MobileControlsWidgetClass)
	{
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);
		if (MobileControlsWidget) MobileControlsWidget->AddToPlayerScreen(0);
	}
}

void AFlyingSwarmCombatSimPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!IsLocalPlayerController()) return;
	TryBindDebugInput();
	TryBindRunInput();
	TryInstallContexts(false);
}

void AFlyingSwarmCombatSimPlayerController::TryBindDebugInput()
{
	auto* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput || DebugBoundInput.Get() == InputComponent) return;
	DebugBoundInput = InputComponent;

	if (ToggleSwarmStateDebugAction && ToggleSwarmStateDebugAction->ValueType == EInputActionValueType::Boolean)
	{
		EnhancedInput->BindAction(ToggleSwarmStateDebugAction, ETriggerEvent::Started, this,
		                          &ThisClass::ToggleSwarmStateDebug);
	}
	if (ToggleSwarmSpatialDebugAction && ToggleSwarmSpatialDebugAction->
		ValueType == EInputActionValueType::Boolean)
	{
		EnhancedInput->BindAction(ToggleSwarmSpatialDebugAction, ETriggerEvent::Started, this,
		                          &ThisClass::ToggleSwarmSpatialDebug);
	}
	if (ToggleSwarmCollisionDebugAction && ToggleSwarmCollisionDebugAction
		->ValueType == EInputActionValueType::Boolean)
	{
		EnhancedInput->BindAction(ToggleSwarmCollisionDebugAction, ETriggerEvent::Started, this,
		                          &ThisClass::ToggleSwarmCollisionDebug);
	}
}

void AFlyingSwarmCombatSimPlayerController::EnsureSpectatorInputDefaults()
{
	// RetryAction and SessionMappingContext are authored and assigned in the editor.
	const auto* PlayerCharacter = Cast<AFlyingCharacter>(GetPawn());
	if (!PlayerCharacter) return;
	if (!SpectatorMoveAction) SpectatorMoveAction = PlayerCharacter->MoveAction;
	if (!SpectatorLookAction) SpectatorLookAction = PlayerCharacter->LookAction;
	if (!SpectatorMappingContext && SpectatorMoveAction && SpectatorLookAction)
	{
		SpectatorMappingContext = NewObject<UInputMappingContext>(this, TEXT("RuntimeSpectatorMappings"), RF_Transient);
		auto CopyViewMappings = [this](const UInputMappingContext* SourceContext)
		{
			if (!SourceContext) return;
			for (const FEnhancedActionKeyMapping& Mapping : SourceContext->GetMappings())
			{
				if ((Mapping.Action == SpectatorMoveAction || Mapping.Action == SpectatorLookAction) && !
					SpectatorMappingContext->GetMappings().ContainsByPredicate(
						[&Mapping](const FEnhancedActionKeyMapping& Existing)
						{
							return Existing.Equals<false>(Mapping);
						}))
				{
					// Copy keys/modifiers/triggers into a private context; never edit the authored IMC.
					FEnhancedActionKeyMapping& CopiedMapping = SpectatorMappingContext->MapKey(
						Mapping.Action, Mapping.Key);
					CopiedMapping = Mapping;
				}
			}
		};
		for (const UInputMappingContext* Context : DefaultMappingContexts) CopyViewMappings(Context);
		if (!ShouldUseTouchControls())
			for (const UInputMappingContext* Context : MobileExcludedMappingContexts)
				CopyViewMappings(Context);
	}
}

void AFlyingSwarmCombatSimPlayerController::TryStartRun()
{
	auto* Mode = RunMode();
	auto* PlayerCharacter = Cast<AFlyingCharacter>(GetPawn());
	// New HUDs and post-death input assets must not gate the existing gameplay controls.
	if (!Mode || Mode->GetGameRunState() != EGameRunState::Starting || !bMappingsReady || !HasGameFocus()) return;
	if (!PlayerCharacter || !PlayerCharacter->HasActorBegunPlay() || !PlayerCharacter->InputComponent || !
		PlayerCharacter->IsAlive())
		return;
	auto HasGameplayAction = [this](const UInputAction* Action)
	{
		if (!Action) return false;
		for (const UInputMappingContext* Context : OwnedContexts)
		{
			if (Context == SessionMappingContext) continue;
			if (Context->GetMappings().ContainsByPredicate([Action](const FEnhancedActionKeyMapping& Mapping)
			{
				return Mapping.Action == Action;
			}))
				return true;
		}
		return false;
	};
	if (!HasGameplayAction(PlayerCharacter->MoveAction) || !HasGameplayAction(PlayerCharacter->LookAction) || !
		HasGameplayAction(PlayerCharacter->FireAction))
		return;
	if (Mode->BeginRun(this, PlayerCharacter) && RunHUD()) RunHUD()->SetRunMessage(FText::GetEmpty());
}

void AFlyingSwarmCombatSimPlayerController::TryBindRunInput()
{
	if (!IsLocalPlayerController() || (bRetryBindingReady && bSpectatorBindingsReady)) return;
	EnsureSpectatorInputDefaults();
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input) return;
	// Retry is an editor-authored action; missing Retry setup must not disable spectator flight.
	if (!bRetryBindingReady && RetryAction && RetryAction->ValueType == EInputActionValueType::Boolean)
	{
		Input->BindAction(RetryAction, ETriggerEvent::Started, this, &ThisClass::RequestRetry);
		bRetryBindingReady = true;
	}
	if (bSpectatorBindingsReady || !SpectatorMoveAction || !SpectatorLookAction) return;
	if (SpectatorMoveAction->ValueType != EInputActionValueType::Axis2D || SpectatorLookAction->ValueType !=
		EInputActionValueType::Axis2D)
		return;
	Input->BindAction(SpectatorMoveAction, ETriggerEvent::Triggered, this, &ThisClass::SpectatorMove);
	Input->BindAction(SpectatorLookAction, ETriggerEvent::Triggered, this, &ThisClass::SpectatorLook);
	bSpectatorBindingsReady = true;
}

void AFlyingSwarmCombatSimPlayerController::RemoveOwnedContexts()
{
	if (auto* Input = InputSubsystem())
	{
		for (UInputMappingContext* Context : OwnedContexts) Input->RemoveMappingContext(Context);
	}
	OwnedContexts.Reset();
	bMappingsReady = false;
}

bool AFlyingSwarmCombatSimPlayerController::TryInstallContexts(bool bSpectating)
{
	auto* Input = InputSubsystem();
	if (!Input || !Input->GetPlayerInput()) return false;
	auto HasMapping = [](const UInputMappingContext* Context, const UInputAction* Action)
	{
		return Context && Action && Context->GetMappings().ContainsByPredicate(
			[Action](const FEnhancedActionKeyMapping& Mapping) { return Mapping.Action == Action; });
	};
	// Validate post-death controls only when entering that phase, never before installing gameplay IMCs.
	if (bSpectating && (!bSpectatorBindingsReady || !HasMapping(SpectatorMappingContext, SpectatorMoveAction) || !
		HasMapping(SpectatorMappingContext, SpectatorLookAction)))
		return false;
	TArray<UInputMappingContext*> Contexts;
	if (bRetryBindingReady && HasMapping(SessionMappingContext, RetryAction)) Contexts.Add(SessionMappingContext);
	if (bSpectating)
	{
		Contexts.AddUnique(SpectatorMappingContext);
	}
	else
	{
		for (UInputMappingContext* Context : DefaultMappingContexts) if (IsValid(Context)) Contexts.AddUnique(Context);
		if (!ShouldUseTouchControls())
		{
			for (UInputMappingContext* Context : MobileExcludedMappingContexts)
				if (IsValid(Context))
					Contexts.
						AddUnique(Context);
		}
	}
	if (IsValid(DebugMappingContext)) Contexts.AddUnique(DebugMappingContext);
	if (Contexts.IsEmpty()) return false;
	if (!bSpectating && !Contexts.ContainsByPredicate([this](const UInputMappingContext* Context)
	{
		return Context != SessionMappingContext;
	}))
		return false;
	RemoveOwnedContexts();
	bControlsArmed = false;
	ReleaseKeys.Reset();
	FlushPressedKeys();
	FModifyContextOptions Options;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	for (UInputMappingContext* Context : Contexts)
	{
		Input->AddMappingContext(
			Context, Context == DebugMappingContext ? 20 : Context == SessionMappingContext ? 10 : 0, Options);
		OwnedContexts.Add(Context);
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Key.IsDigital()) ReleaseKeys.Add(Mapping.Key);
		}
	}
	// Rebuild once after the batch, including removal of old gameplay actions.
	Options.bForceImmediately = true;
	Input->RequestRebuildControlMappings(Options, EInputMappingRebuildType::RebuildWithFlush);
	bMappingsReady = true;
	for (UInputMappingContext* Context : OwnedContexts) bMappingsReady &= Input->HasMappingContext(Context);
	bUsingSpectatorContexts = bSpectating;
	return bMappingsReady;
}

bool AFlyingSwarmCombatSimPlayerController::HasGameFocus() const
{
	const ULocalPlayer* Local = GetLocalPlayer();
	const UGameViewportClient* Client = Local ? Local->ViewportClient : nullptr;
	return Client && Client->Viewport && Client->Viewport->HasFocus();
}

bool AFlyingSwarmCombatSimPlayerController::AllRunKeysReleased() const
{
	if (!HasGameFocus()) return false;
	const FViewport* Viewport = GetLocalPlayer()->ViewportClient->Viewport;
	for (const FKey& Key : ReleaseKeys) if (Viewport->KeyState(Key)) return false;
	return true;
}

void AFlyingSwarmCombatSimPlayerController::PlayerTick(float DeltaTime)
{
	// Disarm before processing new input when focus changes; a release is required on return.
	if (bControlsArmed && !HasGameFocus())
	{
		bControlsArmed = false;
		FlushPressedKeys();
		if (GetPawn() && GetPawn()->GetMovementComponent())
			GetPawn()->GetMovementComponent()->
			           StopMovementImmediately();
	}
	if (IsLocalPlayerController())
	{
		TryBindRunInput();
		if (!bMappingsReady && (!RunMode() || RunMode()->GetGameRunState() == EGameRunState::Starting))
			TryInstallContexts(false);
		TryStartRun();
	}
	Super::PlayerTick(DeltaTime);
	if (!IsLocalPlayerController()) return;
	auto* Mode = RunMode();
	if (!Mode) return;
	TryBindRunInput();
	if (!RunHUD()) RefreshCombatHUD();
	if (bPendingSpectator && Mode->GetGameRunState() == EGameRunState::Spectating)
	{
		bPendingSpectator = false;
		EnterSpectator();
	}
	const bool bSpectating = Mode->GetGameRunState() == EGameRunState::Spectating;
	if ((!bMappingsReady || bUsingSpectatorContexts != bSpectating) && Mode->GetGameRunState() !=
		EGameRunState::Restarting)
		TryInstallContexts(bSpectating);
	if (!bControlsArmed && bMappingsReady && AllRunKeysReleased() && Mode->GetGameRunState() !=
		EGameRunState::Restarting)
		bControlsArmed = true;
	TryStartRun();
	PresentHUD();
}

void AFlyingSwarmCombatSimPlayerController::SetPawn(APawn* InPawn)
{
	AFlyingCharacter* Previous = Cast<AFlyingCharacter>(GetPawn());
	if (Previous && Previous != InPawn && HasActorBegunPlay())
	{
		if (auto* Mode = RunMode(); Mode && Mode->IsPlaying() && Mode->IsRunPawn(Previous)) Mode->PlayerDied(Previous);
	}
	// Retain the old HUD's Destruct/unbind lifecycle; only the separate stats HUD spans possession.
	if (GetPawn() != InPawn && CombatHUDWidget)
	{
		CombatHUDWidget->RemoveFromParent();
		CombatHUDWidget = nullptr;
	}
	Super::SetPawn(InPawn);
	if (HasActorBegunPlay() && Cast<AFlyingCharacter>(InPawn) && !CombatHUDWidget) RefreshCombatHUD();
	// The additive run-stats widget remains; the legacy combat HUD keeps its original lifecycle.
}

void AFlyingSwarmCombatSimPlayerController::PresentHUD()
{
	if (RunHUD() && RunMode()) RunHUD()->UpdateRun(RunMode()->GetGameRunState(), RunMode()->GetRunStats());
}

void AFlyingSwarmCombatSimPlayerController::PlayHitFeedback()
{
	if (!IsLocalPlayerController()) return;
	if (auto* CameraManager = Cast<AFlyingSwarmCombatSimCameraManager>(PlayerCameraManager))
		CameraManager->
			PlayHitShake(ShakeDegrees, ShakeSeconds);
	if (!HitVignetteMaterial) return;
	if (!HitFeedbackWidget)
	{
		HitFeedbackWidget = CreateWidget<UHitFeedbackWidget>(this);
		if (HitFeedbackWidget)
		{
			HitFeedbackWidget->Configure(HitVignetteMaterial, HitColor, VignetteOpacity, VignetteFadeInSeconds,
			                             VignetteFadeOutSeconds);
			HitFeedbackWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
			HitFeedbackWidget->AddToPlayerScreen(9);
		}
	}
	if (HitFeedbackWidget) HitFeedbackWidget->Pulse();
}

void AFlyingSwarmCombatSimPlayerController::CaptureDeathView()
{
	GetPlayerViewPoint(DeathLocation, DeathRotation);
	DeathFOV = PlayerCameraManager ? PlayerCameraManager->GetFOVAngle() : 90.0f;
	AFlyingCharacter* PlayerCharacter = Cast<AFlyingCharacter>(GetPawn());
	DeadPawn = PlayerCharacter;
	if (PlayerCharacter)
	{
		SpectatorSpeed = PlayerCharacter->FlightSpeed;
		PlayerCharacter->SetActorEnableCollision(false);
	}
}

void AFlyingSwarmCombatSimPlayerController::QueueSpectator()
{
	bPendingSpectator = true;
	bControlsArmed = false;
}

void AFlyingSwarmCombatSimPlayerController::EnterSpectator()
{
	AFlyingCharacter* DeadCharacter = DeadPawn.Get();
	RemoveOwnedContexts();
	TryInstallContexts(true);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASpectatorPlayer* CameraPawn = GetWorld()->SpawnActor<ASpectatorPlayer>(
		ASpectatorPlayer::StaticClass(), DeathLocation, DeathRotation, Params);
	bool bRetainOldView = false;
	if (CameraPawn)
	{
		CameraPawn->InitializeView(DeathFOV, SpectatorSpeed);
		Possess(CameraPawn);
		SetControlRotation(DeathRotation);
		SetViewTarget(CameraPawn);
	}
	else
	{
		FallbackCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), DeathLocation, DeathRotation,
		                                                      Params);
		UnPossess();
		if (FallbackCamera)
		{
			FallbackCamera->GetCameraComponent()->SetFieldOfView(DeathFOV);
			SetViewTarget(FallbackCamera);
		}
		else if (DeadCharacter)
		{
			SetViewTarget(DeadCharacter);
			bRetainOldView = true;
		}
		if (RunHUD()) RunHUD()->SetRunMessage(FText::FromString(TEXT("Free camera unavailable. Press R to retry.")));
	}
	if (IsValid(DeadCharacter) && !bRetainOldView) DeadCharacter->Destroy();
	DeadPawn.Reset();
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
}

void AFlyingSwarmCombatSimPlayerController::SpectatorMove(const FInputActionValue& Value)
{
	if (!bControlsArmed || !HasGameFocus() || !RunMode() || RunMode()->GetGameRunState() != EGameRunState::Spectating)
		return;
	if (auto* SpectatorCameraPawn = Cast<ASpectatorPlayer>(GetPawn()))
		SpectatorCameraPawn->MoveView(
			Value.Get<FVector2D>());
}

void AFlyingSwarmCombatSimPlayerController::SpectatorLook(const FInputActionValue& Value)
{
	if (!bControlsArmed || !HasGameFocus() || !Cast<ASpectatorPlayer>(GetPawn()) || !RunMode() || RunMode()->
		GetGameRunState() != EGameRunState::Spectating)
		return;
	const FVector2D Axis = Value.Get<FVector2D>();
	AddYawInput(Axis.X);
	AddPitchInput(Axis.Y);
}

void AFlyingSwarmCombatSimPlayerController::RequestRetry()
{
	auto* Mode = RunMode();
	if (!bControlsArmed || !HasGameFocus() || !Mode || !Mode->BeginRestart(this)) return;
	const FString MapName = UGameplayStatics::GetCurrentLevelName(this, true);
	if (MapName.IsEmpty())
	{
		Mode->CancelRestart();
		if (RunHUD())
			RunHUD()->SetRunMessage(
				FText::FromString(TEXT("Cannot resolve current map. Press R to try again.")));
		return;
	}
	bControlsArmed = false;
	if (GetPawn() && GetPawn()->GetMovementComponent()) GetPawn()->GetMovementComponent()->StopMovementImmediately();
	PresentHUD();
	UGameplayStatics::OpenLevel(this, FName(*MapName), true);
}

void AFlyingSwarmCombatSimPlayerController::HandleTravelFailure(UWorld* World, ETravelFailure::Type Type,
                                                                const FString& Error)
{
	if (World != GetWorld() || !RunMode() || RunMode()->GetGameRunState() != EGameRunState::Restarting) return;
	RunMode()->CancelRestart();
	RemoveOwnedContexts();
	bControlsArmed = false;
	TryInstallContexts(true);
	if (RunHUD()) RunHUD()->SetRunMessage(FText::FromString(Error));
}

bool AFlyingSwarmCombatSimPlayerController::ShouldUseTouchControls() const
{
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

void AFlyingSwarmCombatSimPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FWorldDelegates::OnWorldTickEnd.Remove(SwarmDebugTickHandle);
	StopSwarmDebug();
	SwarmDebugPresenter.Reset();
	if (SwarmDebugOverlay) SwarmDebugOverlay->RemoveFromParent();
	SwarmDebugOverlay = nullptr;
	DebugBoundInput.Reset();
	bControlsArmed = false;
	bPendingSpectator = false;
	if (GEngine) GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	RemoveOwnedContexts();
	FlushPressedKeys();
	if (CombatHUDWidget) CombatHUDWidget->RemoveFromParent();
	CombatHUDWidget = nullptr;
	if (RunStatsWidget) RunStatsWidget->RemoveFromParent();
	RunStatsWidget = nullptr;
	if (HitFeedbackWidget) HitFeedbackWidget->RemoveFromParent();
	HitFeedbackWidget = nullptr;
	if (MobileControlsWidget) MobileControlsWidget->RemoveFromParent();
	MobileControlsWidget = nullptr;
	FallbackCamera = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AFlyingSwarmCombatSimPlayerController::ToggleSwarmStateDebug()
{
	if (!IsLocalPlayerController() || !HasGameFocus()) return;
	bSwarmState = !bSwarmState;
	if (!bSwarmCollision && !bSwarmState && !bSwarmSpatial) StopSwarmDebug();
}

void AFlyingSwarmCombatSimPlayerController::ToggleSwarmSpatialDebug()
{
	if (!IsLocalPlayerController() || !HasGameFocus()) return;
	bSwarmSpatial = !bSwarmSpatial;
	if (!bSwarmCollision && !bSwarmState && !bSwarmSpatial) StopSwarmDebug();
}

void AFlyingSwarmCombatSimPlayerController::ToggleSwarmCollisionDebug()
{
	if (!IsLocalPlayerController() || !HasGameFocus()) return;
	bSwarmCollision = !bSwarmCollision;
	if (!bSwarmCollision) DisableCollision();
	if (!bSwarmCollision && !bSwarmState && !bSwarmSpatial) StopSwarmDebug();
}

void AFlyingSwarmCombatSimPlayerController::DisableCollision()
{
	if (SwarmDebugPresenter) SwarmDebugPresenter->DisableCollision();
	if (auto* Camera = DebugCameraManager.Get()) Camera->SetSwarmDebugBlendable(nullptr);
	DebugCameraManager.Reset();
	DebugOutlineMaterial = nullptr;
	DebugOutlineSource.Reset();
}

void AFlyingSwarmCombatSimPlayerController::StopSwarmDebug()
{
	if (SwarmDebugPresenter) SwarmDebugPresenter->Shutdown();
	if (SwarmDebugOverlay)
	{
		SwarmDebugOverlay->ClearFrame();
		SwarmDebugOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
	DisableCollision();
}

void AFlyingSwarmCombatSimPlayerController::UpdateSwarmDebug(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld() || !IsLocalPlayerController() || IsActorBeingDestroyed()) return;
	if (!bSwarmCollision && !bSwarmState && !bSwarmSpatial) return;
	if (!SwarmDebugPresenter) SwarmDebugPresenter.Reset(new FSwarmDebugPresenter());
	if (!SwarmDebugOverlay)
	{
		SwarmDebugOverlay = CreateWidget<USwarmDebugOverlayWidget>(this);
		if (SwarmDebugOverlay) SwarmDebugOverlay->AddToPlayerScreen(20);
	}
	if (!SwarmDebugOverlay) return;
	SwarmDebugOverlay->SetVisibility(ESlateVisibility::HitTestInvisible);
	auto* Camera = Cast<AFlyingSwarmCombatSimCameraManager>(PlayerCameraManager);
	if (DebugCameraManager.Get() != Camera)
	{
		if (auto* Previous = DebugCameraManager.Get()) Previous->SetSwarmDebugBlendable(nullptr);
		DebugCameraManager = Camera;
	}
	UMaterialInterface* Source = SwarmDebug.OutlineMaterial.Get();
	const IConsoleVariable* DepthMode = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));
	const UMaterial* BaseMaterial = Source ? Source->GetMaterial() : nullptr;
	FString OutlineFailure;
	if (!bSwarmCollision) OutlineFailure = TEXT("Collision outlines are disabled.");
	else if (!SwarmDebug.bOutlines) OutlineFailure = TEXT("Outline rendering is disabled in debug settings.");
	else if (!Camera) OutlineFailure = TEXT("The debug camera manager is unavailable.");
	else if (!Source) OutlineFailure = TEXT("No outline post-process material is assigned.");
	else if (!BaseMaterial || BaseMaterial->MaterialDomain != MD_PostProcess)
		OutlineFailure = TEXT(
			"The assigned outline material must use the Post Process material domain.");
	else if (!DepthMode) OutlineFailure = TEXT("The r.CustomDepth setting is unavailable.");
	else if (DepthMode->GetInt() != 3) OutlineFailure = TEXT("Set r.CustomDepth to 3 to enable stencil outlines.");
	const bool bOutlineReady = OutlineFailure.IsEmpty();
	if (bOutlineReady)
	{
		if (!DebugOutlineMaterial || DebugOutlineSource.Get() != Source)
		{
			DebugOutlineMaterial = UMaterialInstanceDynamic::Create(Source, this);
			DebugOutlineSource = Source;
		}
		if (DebugOutlineMaterial)
		{
			DebugOutlineMaterial->SetVectorParameterValue(TEXT("UnawareColor"), SwarmDebug.UnawareColor);
			DebugOutlineMaterial->SetVectorParameterValue(TEXT("AwareColor"), SwarmDebug.AwareColor);
			DebugOutlineMaterial->SetVectorParameterValue(TEXT("EngagingColor"), SwarmDebug.EngagingColor);
			DebugOutlineMaterial->SetScalarParameterValue(TEXT("OutlinePixels"), 2.0f);
		}
		else OutlineFailure = TEXT("The outline material instance could not be created.");
	}
	else
	{
		DebugOutlineMaterial = nullptr;
		DebugOutlineSource.Reset();
	}
	if (Camera) Camera->SetSwarmDebugBlendable(DebugOutlineMaterial);
	SwarmDebugPresenter->Update(*this, *SwarmDebugOverlay, SwarmDebug, bSwarmCollision, bSwarmState,
	                            bSwarmSpatial, bOutlineReady && DebugOutlineMaterial != nullptr, OutlineFailure);
}
