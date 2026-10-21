#include "UI/GameplayHUDWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Styling/CoreStyle.h"

UGameplayHUDWidget::UGameplayHUDWidget(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetIsFocusable(false);
}

TSharedRef<SWidget> UGameplayHUDWidget::RebuildWidget()
{
	Initialize();
	BuildNativeOverlay();
	return Super::RebuildWidget();
}

void UGameplayHUDWidget::BuildNativeOverlay()
{
	if (!WidgetTree) return;
	if (NativeOverlay && NativeOverlay->GetOuter() != WidgetTree)
	{
		NativeOverlay = nullptr;
		StatsPanel = nullptr;
		StatsText = nullptr;
		StatusText = nullptr;
	}
	if (NativeOverlay && WidgetTree->RootWidget == NativeOverlay) return;

	UWidget* ExistingRoot = WidgetTree->RootWidget;
	NativeOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("NativeGameplayHUDOverlay"));
	if (ExistingRoot)
	{
		if (UOverlaySlot* ExistingSlot = NativeOverlay->AddChildToOverlay(ExistingRoot))
		{
			ExistingSlot->SetHorizontalAlignment(HAlign_Fill);
			ExistingSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
	WidgetTree->RootWidget = NativeOverlay;

	StatsPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("NativeRunStatsPanel"));
	StatsPanel->SetBrushColor(FLinearColor(0.015f, 0.025f, 0.04f, 0.78f));
	StatsPanel->SetPadding(FMargin(14.0f));
	StatsPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	UVerticalBox* StatsLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NativeRunStatsLayout"));
	StatsPanel->SetContent(StatsLayout);

	UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NativeRunStatsTitle"));
	TitleText->SetText(FText::FromString(TEXT("RUN STATS")));
	TitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 18));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.85f, 1.0f, 1.0f)));
	StatsLayout->AddChildToVerticalBox(TitleText);

	StatsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NativeRunMetrics"));
	StatsText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 16));
	StatsText->SetColorAndOpacity(FSlateColor(FLinearColor(0.93f, 0.95f, 1.0f, 1.0f)));
	StatsLayout->AddChildToVerticalBox(StatsText);

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NativeRunStatus"));
	StatusText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 12));
	StatusText->SetAutoWrapText(true);
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.78f, 0.26f, 1.0f)));
	StatsLayout->AddChildToVerticalBox(StatusText);

	USizeBox* PanelSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("NativeRunStatsSize"));
	PanelSize->SetWidthOverride(300.0f);
	PanelSize->SetContent(StatsPanel);
	PanelSize->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* PanelSlot = NativeOverlay->AddChildToOverlay(PanelSize))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Left);
		PanelSlot->SetVerticalAlignment(VAlign_Top);
		PanelSlot->SetPadding(FMargin(18.0f));
	}
	UpdateRunMessage();
	UpdateStatsDisplay();
}

void UGameplayHUDWidget::UpdateRun(EGameRunState State, const FPlayerRunStats& Stats)
{
	if (CurrentRunState != State && State == EGameRunState::Playing) AdditionalMessage = FText::GetEmpty();
	CurrentRunState = State;
	CurrentStats = Stats;
	UpdateRunMessage();
	UpdateStatsDisplay();
	PresentRun(State, Stats);
}

void UGameplayHUDWidget::UpdateStatsDisplay()
{
	const int32 TotalSeconds = FMath::Max(0, FMath::FloorToInt(CurrentStats.SurvivalSeconds));
	const int32 Minutes = TotalSeconds / 60;
	const int32 Seconds = TotalSeconds % 60;
	const FString Accuracy = CurrentStats.ShotsFired > 0 ? FString::Printf(TEXT("%.0f%%"), CurrentStats.AccuracyPercent) : TEXT("—");
	if (StatsText)
	{
		StatsText->SetText(FText::FromString(FString::Printf(TEXT("Time: %02d:%02d\nKills: %d\nShots: %d\nHits: %d\nAccuracy: %s\nDamage: %.1f"), Minutes, Seconds, CurrentStats.KillCount, CurrentStats.ShotsFired, CurrentStats.ConfirmedHits, *Accuracy, CurrentStats.DamageDealt)));
	}
}

void UGameplayHUDWidget::UpdateRunMessage()
{
	if (!StatusText) return;
	FText Message;
	switch (CurrentRunState)
	{
	case EGameRunState::Starting:
		Message = FText::FromString(TEXT("Focus game window to start."));
		break;
	case EGameRunState::Spectating:
		Message = FText::FromString(TEXT("RUN ENDED - SPECTATING\nWASD: fly | Mouse: look | R: retry"));
		break;
	case EGameRunState::Restarting:
		Message = FText::FromString(TEXT("Restarting..."));
		break;
	default:
		Message = FText::GetEmpty();
		break;
	}
	if (!AdditionalMessage.IsEmpty()) Message = FText::Format(FText::FromString(TEXT("{0}\n{1}")), Message, AdditionalMessage);
	StatusText->SetText(Message);
}

void UGameplayHUDWidget::SetRunMessage(const FText& Message)
{
	AdditionalMessage = Message;
	UpdateRunMessage();
	ShowRunMessage(Message);
}
