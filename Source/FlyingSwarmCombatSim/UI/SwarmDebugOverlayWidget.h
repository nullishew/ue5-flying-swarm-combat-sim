#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Swarm/Debug/SwarmDebugSettings.h"
#include "SwarmDebugOverlayWidget.generated.h"

struct FSwarmDebugBadge
{
	FVector2D Position = FVector2D::ZeroVector;
	float DistanceAlpha = 0.f;
	FString Text;
	FString Reason;
	FLinearColor Color = FLinearColor::White;
	float HealthFraction = 1.f;
	bool bHasCadence = false;
	float AttackFraction = 0.f;
	float SecondsUntilAttempt = 0.f;
};

struct FSwarmDebugLabel
{
	FVector2D Position = FVector2D::ZeroVector;
	FString Text;
};

struct FSwarmDebugInspectorView
{
	int32 AgentId = INDEX_NONE;
	TArray<FString> Lines;
	bool bHasScreenBounds = false;
	FVector2D ScreenMinimum = FVector2D::ZeroVector;
	FVector2D ScreenMaximum = FVector2D::ZeroVector;
};

UCLASS()
class FLYINGSWARMCOMBATSIM_API USwarmDebugOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	USwarmDebugOverlayWidget(const FObjectInitializer& ObjectInitializer);

	void SetFrame(TArray<FSwarmDebugBadge>&& InBadges, TArray<FSwarmDebugLabel>&& InLabels,
	              const FString& InLegend, const FSwarmDebugSettings& InSettings, const FSwarmDebugInspectorView& InInspector);
	void ClearFrame();

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	                          const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	                          const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TArray<FSwarmDebugBadge> Badges;
	TArray<FSwarmDebugLabel> Labels;
	FString Legend;
	FSwarmDebugSettings Settings;
	FSwarmDebugInspectorView Inspector;
};
