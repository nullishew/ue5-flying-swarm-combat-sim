#include "UI/SwarmDebugOverlayWidget.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr float BadgePaddingX = 8.0f;
	constexpr float BadgePaddingY = 6.0f;
	constexpr float OverlayMargin = 14.0f;
	constexpr float LegendMaxWidth = 520.0f;
	constexpr float BadgeBorderThickness = 1.0f;
	constexpr float BadgeTextOpacity = 0.85f;

	FVector2D MeasureMultilineText(const FString& Text, const FSlateFontInfo& Font,
	                               const TSharedRef<FSlateFontMeasure>& FontMeasure)
	{
		float Width = 0.0f;
		int32 LineCount = 0;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);
		for (const FString& Line : Lines)
		{
			Width = FMath::Max(Width, static_cast<float>(FontMeasure->Measure(Line, Font).X));
			++LineCount;
		}
		return FVector2D(Width, FontMeasure->GetMaxCharacterHeight(Font) * FMath::Max(1, LineCount));
	}

	void DrawMultilineText(FSlateWindowElementList& OutDrawElements, int32& DrawLayer, const FGeometry& Geometry,
	                       const FVector2D& Position, const FString& Text, const FSlateFontInfo& Font,
	                       const FLinearColor& Color, float Scale, const TSharedRef<FSlateFontMeasure>& FontMeasure)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);
		float LinePositionY = Position.Y;
		const float LineHeight = FontMeasure->GetMaxCharacterHeight(Font);
		for (const FString& Line : Lines)
		{
			FVector2D LineSize = FontMeasure->Measure(Line, Font);
			LineSize.Y = LineHeight;
			FSlateDrawElement::MakeText(OutDrawElements, ++DrawLayer,
			                            Geometry.ToPaintGeometry(
				                            LineSize, FSlateLayoutTransform(FVector2D(Position.X, LinePositionY)),
				                            FSlateRenderTransform(FScale2D(Scale)), FVector2f::ZeroVector), Line, Font,
			                            ESlateDrawEffect::None, Color);
			LinePositionY += LineHeight * Scale;
		}
	}
}

USwarmDebugOverlayWidget::USwarmDebugOverlayWidget(const FObjectInitializer& ObjectInitializer) : Super(
	ObjectInitializer)
{
	SetIsFocusable(false);
}

void USwarmDebugOverlayWidget::SetFrame(TArray<FSwarmDebugBadge>&& InBadges, const FString& InLegend, int32 InFontSize,
                                        const FLinearColor& InBackground)
{
	Swap(Badges, InBadges);
	Legend = InLegend;
	FontSize = FMath::Clamp(InFontSize, 8, 32);
	Background = InBackground;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void USwarmDebugOverlayWidget::ClearFrame()
{
	Badges.Reset();
	Legend.Reset();
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 USwarmDebugOverlayWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                                            const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                                            int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 SuperLayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId,
	                                              InWidgetStyle, bParentEnabled);
	const FVector2D ViewportSize = AllottedGeometry.GetLocalSize();
	if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f) return SuperLayerId;

	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", FontSize);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry));
	int32 DrawLayer = SuperLayerId;
	for (const FSwarmDebugBadge& Badge : Badges)
	{
		if (Badge.Text.IsEmpty()) continue;
		const FVector2D TextSize = MeasureMultilineText(Badge.Text, Font, FontMeasure);
		const float Scale = FMath::Max(0.01f, Badge.Scale);
		const FVector2D PanelSize(TextSize.X + BadgePaddingX * 2.0f, TextSize.Y + BadgePaddingY * 2.0f);
		const FVector2D PanelPosition(Badge.Position.X - PanelSize.X * Scale * 0.5f,
		                              Badge.Position.Y - PanelSize.Y * Scale);
		const FSlateRenderTransform ScaleTransform{FScale2D(Scale)};
		const FPaintGeometry TopBorderGeometry = AllottedGeometry.ToPaintGeometry(
			FVector2D(PanelSize.X, BadgeBorderThickness), FSlateLayoutTransform(PanelPosition), ScaleTransform,
			FVector2f::ZeroVector);
		const FPaintGeometry BottomBorderGeometry = AllottedGeometry.ToPaintGeometry(
			FVector2D(PanelSize.X, BadgeBorderThickness),
			FSlateLayoutTransform(PanelPosition + FVector2D(0.0, (PanelSize.Y - BadgeBorderThickness) * Scale)),
			ScaleTransform, FVector2f::ZeroVector);
		const FPaintGeometry LeftBorderGeometry = AllottedGeometry.ToPaintGeometry(
			FVector2D(BadgeBorderThickness, PanelSize.Y - BadgeBorderThickness * 2.0),
			FSlateLayoutTransform(PanelPosition + FVector2D(0.0, BadgeBorderThickness * Scale)), ScaleTransform,
			FVector2f::ZeroVector);
		const FPaintGeometry RightBorderGeometry = AllottedGeometry.ToPaintGeometry(
			FVector2D(BadgeBorderThickness, PanelSize.Y - BadgeBorderThickness * 2.0),
			FSlateLayoutTransform(PanelPosition + FVector2D((PanelSize.X - BadgeBorderThickness) * Scale,
			                                                BadgeBorderThickness * Scale)), ScaleTransform,
			FVector2f::ZeroVector);
		const FLinearColor BorderColor = Badge.Color.CopyWithNewOpacity(0.9f);
		FSlateDrawElement::MakeBox(OutDrawElements, ++DrawLayer, TopBorderGeometry, WhiteBrush, ESlateDrawEffect::None,
		                           BorderColor);
		FSlateDrawElement::MakeBox(OutDrawElements, ++DrawLayer, BottomBorderGeometry, WhiteBrush,
		                           ESlateDrawEffect::None, BorderColor);
		FSlateDrawElement::MakeBox(OutDrawElements, ++DrawLayer, LeftBorderGeometry, WhiteBrush, ESlateDrawEffect::None,
		                           BorderColor);
		FSlateDrawElement::MakeBox(OutDrawElements, ++DrawLayer, RightBorderGeometry, WhiteBrush,
		                           ESlateDrawEffect::None, BorderColor);
		const FVector2D InnerSize(FMath::Max(0.0, PanelSize.X - BadgeBorderThickness * 2.0),
		                          FMath::Max(0.0, PanelSize.Y - BadgeBorderThickness * 2.0));
		const FPaintGeometry PanelGeometry = AllottedGeometry.ToPaintGeometry(
			InnerSize,
			FSlateLayoutTransform(
				PanelPosition + FVector2D(BadgeBorderThickness * Scale, BadgeBorderThickness * Scale)), ScaleTransform,
			FVector2f::ZeroVector);
		FSlateDrawElement::MakeBox(OutDrawElements, ++DrawLayer, PanelGeometry, WhiteBrush, ESlateDrawEffect::None,
		                           Background);
		DrawMultilineText(OutDrawElements, DrawLayer, AllottedGeometry,
		                  PanelPosition + FVector2D(BadgePaddingX * Scale, BadgePaddingY * Scale), Badge.Text, Font,
		                  FLinearColor(0.88f, 0.92f, 1.0f, BadgeTextOpacity), Scale, FontMeasure);
	}

	if (!Legend.IsEmpty())
	{
		const FVector2D TextSize = MeasureMultilineText(Legend, Font, FontMeasure);
		const double AvailableWidth = FMath::Max(0.0, ViewportSize.X - OverlayMargin * 2.0f);
		const FVector2D PanelSize(FMath::Min(TextSize.X + BadgePaddingX * 2.0f,
		                                     FMath::Min(static_cast<double>(LegendMaxWidth), AvailableWidth)),
		                          TextSize.Y + BadgePaddingY * 2.0f);
		const FVector2D PanelPosition(FMath::Max(0.0, ViewportSize.X - PanelSize.X - OverlayMargin),
		                              FMath::Max(0.0, ViewportSize.Y - PanelSize.Y - OverlayMargin));
		const FPaintGeometry PanelGeometry = AllottedGeometry.ToPaintGeometry(
			PanelSize, FSlateLayoutTransform(PanelPosition));
		FSlateDrawElement::MakeBox(OutDrawElements, ++DrawLayer, PanelGeometry, WhiteBrush, ESlateDrawEffect::None,
		                           Background.CopyWithNewOpacity(0.65f));
		DrawMultilineText(OutDrawElements, DrawLayer, AllottedGeometry,
		                  PanelPosition + FVector2D(BadgePaddingX, BadgePaddingY), Legend, Font,
		                  FLinearColor(0.88f, 0.92f, 1.0f, 0.9f), 1.0f, FontMeasure);
	}

	OutDrawElements.PopClip();
	return DrawLayer;
}
