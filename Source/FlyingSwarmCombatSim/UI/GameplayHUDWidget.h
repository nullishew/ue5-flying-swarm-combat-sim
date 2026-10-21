#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/GameRunStates.h"
#include "Game/PlayerRunStats.h"
#include "GameplayHUDWidget.generated.h"

class UBorder;
class UOverlay;
class UTextBlock;

/** Additive run statistics and spectator/retry instructions. Health belongs to the existing combat HUD. */
UCLASS(Blueprintable)
class FLYINGSWARMCOMBATSIM_API UGameplayHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UGameplayHUDWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "Run")
	void UpdateRun(EGameRunState State, const FPlayerRunStats& Stats);

	UFUNCTION(BlueprintCallable, Category = "Run")
	void SetRunMessage(const FText& Message);

	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "Run")
	void PresentRun(EGameRunState State, const FPlayerRunStats& Stats);

	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "Run")
	void ShowRunMessage(const FText& Message);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void BuildNativeOverlay();
	void UpdateStatsDisplay();
	void UpdateRunMessage();

	UPROPERTY(Transient)
	TObjectPtr<UOverlay> NativeOverlay;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> StatsPanel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatsText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	EGameRunState CurrentRunState = EGameRunState::Starting;
	FPlayerRunStats CurrentStats;
	FText AdditionalMessage;
};
