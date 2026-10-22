#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SwarmDebugOverlayWidget.generated.h"

struct FSwarmDebugBadge
{
	FVector2D Position = FVector2D::ZeroVector;
	float Scale = 1.0f;
	FString Text;
	FLinearColor Color = FLinearColor::White;
};

UCLASS()
class FLYINGSWARMCOMBATSIM_API USwarmDebugOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	USwarmDebugOverlayWidget(const FObjectInitializer& ObjectInitializer);

	void SetFrame(TArray<FSwarmDebugBadge>&& InBadges, const FString& InLegend, int32 InFontSize,
	              const FLinearColor& InBackground);
	void ClearFrame();

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	                          const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	                          const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TArray<FSwarmDebugBadge> Badges;
	FString Legend;
	FLinearColor Background = FLinearColor(0.015f, 0.02f, 0.03f, 0.25f);
	int32 FontSize = 12;
};
