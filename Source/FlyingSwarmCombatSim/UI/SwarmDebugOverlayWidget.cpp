#include "UI/SwarmDebugOverlayWidget.h"

#include "Swarm/Debug/SwarmDebugPresentation.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	void DrawText(FSlateWindowElementList& Elements, int32& Layer, const FGeometry& Geometry, const FVector2D& Position,
	              const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Color,
	              const TSharedRef<FSlateFontMeasure>& Measure)
	{
		FSlateDrawElement::MakeText(Elements, ++Layer,
		                            Geometry.ToPaintGeometry(Measure->Measure(Text, Font),
		                                                     FSlateLayoutTransform(Position)), Text, Font,
		                            ESlateDrawEffect::None, Color);
	}

	void DrawBox(FSlateWindowElementList& Elements, int32& Layer, const FGeometry& Geometry, const FVector2D& Position,
	             const FVector2D& Size, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(Elements, ++Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)),
		                           FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
	}

	FString FitText(const FString& Text, const FSlateFontInfo& Font, double Width,
	                const TSharedRef<FSlateFontMeasure>& Measure)
	{
		if (Measure->Measure(Text, Font).X <= Width) return Text;
		const FString Ellipsis(TEXT("\x2026"));
		if (Measure->Measure(Ellipsis, Font).X > Width) return FString();
		FString Short = Text;
		while (!Short.IsEmpty() && Measure->Measure(Short + Ellipsis, Font).X > Width) Short.LeftChopInline(1);
		return Short + Ellipsis;
	}

	void WrapInspectorLine(const FString& Source, const FSlateFontInfo& Font, double Width,
	                       const TSharedRef<FSlateFontMeasure>& Measure, TArray<FString>& OutLines)
	{
		FString Remaining = Source.Left(1024);
		while (!Remaining.IsEmpty())
		{
			int32 Low = 0;
			int32 High = Remaining.Len();
			while (Low < High)
			{
				const int32 Count = (Low + High + 1) / 2;
				if (Measure->Measure(Remaining.Left(Count), Font).X <= Width) Low = Count;
				else High = Count - 1;
			}
			if (Low == 0) return;
			int32 Count = Low;
			if (Count < Remaining.Len())
			{
				int32 Space;
				if (Remaining.Left(Count).FindLastChar(TEXT(' '), Space) && Space > 0) Count = Space;
			}
			OutLines.Add(Remaining.Left(Count));
			Remaining.RightChopInline(Count);
			Remaining.TrimStartInline();
		}
	}

	FLinearColor AttackColor(float Fraction, const FSwarmDebugSettings& Settings)
	{
		constexpr float ColorMidpoint = .5f;
		return Fraction >= ColorMidpoint
			       ? FMath::Lerp(Settings.BadgeAttackBarPartialColor, Settings.BadgeAttackBarFullColor,
			                     (Fraction - ColorMidpoint) * (1.f / ColorMidpoint))
			       : FMath::Lerp(Settings.BadgeAttackBarEmptyColor, Settings.BadgeAttackBarPartialColor,
			                     Fraction * (1.f / ColorMidpoint));
	}
}

USwarmDebugOverlayWidget::USwarmDebugOverlayWidget(const FObjectInitializer& ObjectInitializer) : Super(
	ObjectInitializer)
{
	SetIsFocusable(false);
}

void USwarmDebugOverlayWidget::SetFrame(TArray<FSwarmDebugBadge>&& InBadges, TArray<FSwarmDebugLabel>&& InLabels,
                                        const FString& InLegend, const FSwarmDebugSettings& InSettings,
                                        const FSwarmDebugInspectorView& InInspector)
{
	Swap(Badges, InBadges);
	Swap(Labels, InLabels);
	Legend = InLegend;
	Settings = InSettings;
	Inspector = InInspector;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void USwarmDebugOverlayWidget::ClearFrame()
{
	Badges.Reset();
	Labels.Reset();
	Legend.Reset();
	Inspector = FSwarmDebugInspectorView();
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 USwarmDebugOverlayWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                                            const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                                            int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
	                                 bParentEnabled);
	const FVector2D ViewportSize = AllottedGeometry.GetLocalSize();
	if (ViewportSize.X <= 0.f || ViewportSize.Y <= 0.f) return Layer;
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FLinearColor PanelColor = Settings.BadgePanelColor.CopyWithNewOpacity(Settings.BadgeOpacity);
	OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry));

	for (const FSwarmDebugLabel& Label : Labels)
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 12);
		const FVector2D Size = Measure->Measure(Label.Text, Font) + FVector2D(12.f, 6.f);
		const FVector2D Position = Label.Position - Size * .5f;
		DrawBox(OutDrawElements, Layer, AllottedGeometry, Position, Size, PanelColor);
		DrawText(OutDrawElements, Layer, AllottedGeometry, Position + FVector2D(6.f, 3.f), Label.Text, Font,
		         Settings.BadgeTextColor, Measure);
	}

	for (const FSwarmDebugBadge& Badge : Badges)
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", Settings.BadgeFontSize);
		const FSlateFontInfo ReasonFont = FCoreStyle::GetDefaultFontStyle("Regular", Settings.BadgeReasonFontSize);
		const FSlateFontInfo BarFont = FCoreStyle::GetDefaultFontStyle("Regular", Settings.BadgeBarLabelFontSize);
		const FSlateFontInfo TimerFont = FCoreStyle::GetDefaultFontStyle("Regular", Settings.BadgeTimerFontSize);

		const float HeaderHeight = FMath::Max(Measure->GetMaxCharacterHeight(Font),
		                                      Measure->GetMaxCharacterHeight(ReasonFont));
		const float BarTextHeight = Measure->GetMaxCharacterHeight(BarFont);
		const float TimerTextHeight = Measure->GetMaxCharacterHeight(TimerFont);
		const float BarRowHeight = FMath::Max(BarTextHeight, Settings.BadgeBarHeight);
		const float AttackRowHeight = FMath::Max(BarRowHeight, TimerTextHeight);
		const float Height = Settings.BadgePaddingY * 2.f + HeaderHeight + Settings.BadgeRowGap + BarRowHeight + (
			Badge.bHasCadence ? Settings.BadgeRowGap + AttackRowHeight : 0.f);


		const FVector2D Size(Settings.BadgeWidth, Height);
		const float Scale = SwarmDebugPresentation::CalcBadgeScale(Settings, Badge.DistanceAlpha);
		const FVector2D ScreenPosition(Badge.Position.X - Size.X * Scale * 0.5f, Badge.Position.Y - Size.Y * Scale);
		const FGeometry BadgeGeometry = AllottedGeometry.MakeChild(Size, FSlateLayoutTransform(Scale, ScreenPosition));
		const FVector2D Position = FVector2D::ZeroVector;


		const float ContentX = Position.X + Settings.BadgeAccentWidth + Settings.BadgePaddingX;
		const float ContentWidth = Settings.BadgeWidth - Settings.BadgeAccentWidth - Settings.BadgePaddingX * 2.f;
		OutDrawElements.PushClip(FSlateClippingZone(BadgeGeometry));
		DrawBox(OutDrawElements, Layer, BadgeGeometry, Position, Size, PanelColor);
		DrawBox(OutDrawElements, Layer, BadgeGeometry, Position, FVector2D(Settings.BadgeAccentWidth, Height),
		        Badge.Color.CopyWithNewOpacity(Settings.BadgeOpacity));

		const FString Header = FitText(Badge.Text, Font, ContentWidth, Measure);
		const float ReasonX = ContentX + Measure->Measure(Header, Font).X + Settings.BadgeColumnGap;
		const FString Reason = FitText(Badge.Reason, ReasonFont, ContentX + ContentWidth - ReasonX, Measure);
		DrawText(OutDrawElements, Layer, BadgeGeometry,
		         FVector2D(ContentX,
		                   Position.Y + Settings.BadgePaddingY + (HeaderHeight - Measure->GetMaxCharacterHeight(Font)) *
		                   .5f), Header, Font, Settings.BadgeTextColor, Measure);
		DrawText(OutDrawElements, Layer, BadgeGeometry,
		         FVector2D(ReasonX,
		                   Position.Y + Settings.BadgePaddingY + (HeaderHeight - Measure->
			                   GetMaxCharacterHeight(ReasonFont)) * .5f), Reason, ReasonFont, Settings.BadgeTextColor,
		         Measure);

		const float LabelWidth = Measure->Measure(TEXT("ATK"), BarFont).X;
		const float TrackX = ContentX + LabelWidth + Settings.BadgeColumnGap;
		const float TrackWidth = ContentWidth - LabelWidth - Settings.BadgeColumnGap * 2.f - Settings.BadgeTimerWidth;
		float RowY = Position.Y + Settings.BadgePaddingY + HeaderHeight + Settings.BadgeRowGap;
		auto DrawBar = [&](const TCHAR* Label, float Fraction, const FLinearColor& Color, bool bTimer)
		{
			const float RowHeight = bTimer ? AttackRowHeight : BarRowHeight;
			const float TrackY = RowY + (RowHeight - Settings.BadgeBarHeight) * .5f;
			const float Edge = Settings.BadgeBarOutlineThickness;
			const FLinearColor Tint = Color.CopyWithNewOpacity(Settings.BadgeOpacity);
			DrawText(OutDrawElements, Layer, BadgeGeometry,
			         FVector2D(ContentX, RowY + (RowHeight - BarTextHeight) * .5f), Label, BarFont,
			         Settings.BadgeTextColor, Measure);
			DrawBox(OutDrawElements, Layer, BadgeGeometry, FVector2D(TrackX, TrackY), FVector2D(TrackWidth, Edge),
			        Tint);
			DrawBox(OutDrawElements, Layer, BadgeGeometry, FVector2D(TrackX, TrackY + Settings.BadgeBarHeight - Edge),
			        FVector2D(TrackWidth, Edge), Tint);
			DrawBox(OutDrawElements, Layer, BadgeGeometry, FVector2D(TrackX, TrackY),
			        FVector2D(Edge, Settings.BadgeBarHeight), Tint);
			DrawBox(OutDrawElements, Layer, BadgeGeometry, FVector2D(TrackX + TrackWidth - Edge, TrackY),
			        FVector2D(Edge, Settings.BadgeBarHeight), Tint);
			DrawBox(OutDrawElements, Layer, BadgeGeometry, FVector2D(TrackX, TrackY),
			        FVector2D(TrackWidth * Fraction, Settings.BadgeBarHeight), Tint);
			if (bTimer)
			{
				const FString Seconds = FitText(FString::Printf(TEXT("%.1fs"), Badge.SecondsUntilAttempt), TimerFont,
				                                Settings.BadgeTimerWidth, Measure);
				DrawText(OutDrawElements, Layer, BadgeGeometry,
				         FVector2D(TrackX + TrackWidth + Settings.BadgeColumnGap,
				                   RowY + (RowHeight - TimerTextHeight) * .5f), Seconds, TimerFont,
				         Settings.BadgeTextColor, Measure);
			}
			RowY += RowHeight + Settings.BadgeRowGap;
		};
		DrawBar(TEXT("HP"), Badge.HealthFraction, Settings.BadgeHealthBarColor, false);
		if (Badge.bHasCadence) DrawBar(TEXT("ATK"), Badge.AttackFraction, AttackColor(Badge.AttackFraction, Settings), true);
		OutDrawElements.PopClip();
	}

	// Selection uses screen-space brackets, independent of collision outlines and distance scaling.
	if (Inspector.AgentId != INDEX_NONE && Inspector.bHasScreenBounds)
	{
		const FVector2D Minimum = Inspector.ScreenMinimum;
		const FVector2D Maximum = Inspector.ScreenMaximum;
		const double CornerLength = FMath::Min(14., FMath::Min(Maximum.X - Minimum.X, Maximum.Y - Minimum.Y) * .4);
		const float Stroke = FMath::IsFinite(Settings.InspectorHighlightThickness)
			                     ? FMath::Clamp(Settings.InspectorHighlightThickness, 1.f, 8.f)
			                     : 2.f;
		if (CornerLength >= Stroke)
		{
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const bool bRight = (Corner & 1) != 0;
				const bool bBottom = (Corner & 2) != 0;
				const double X = bRight ? Maximum.X : Minimum.X;
				const double Y = bBottom ? Maximum.Y : Minimum.Y;
				DrawBox(OutDrawElements, Layer, AllottedGeometry,
				        FVector2D(bRight ? X - CornerLength : X, bBottom ? Y - Stroke : Y),
				        FVector2D(CornerLength, Stroke), Settings.InspectorHighlightColor);
				DrawBox(OutDrawElements, Layer, AllottedGeometry,
				        FVector2D(bRight ? X - Stroke : X, bBottom ? Y - CornerLength : Y),
				        FVector2D(Stroke, CornerLength), Settings.InspectorHighlightColor);
			}
		}
		const FSlateFontInfo MarkerFont = FCoreStyle::GetDefaultFontStyle("Bold", 12);
		const FString Marker = FString::Printf(TEXT("SELECTED #%d"), Inspector.AgentId);
		const FVector2D TextSize = Measure->Measure(Marker, MarkerFont);
		const FVector2D MarkerPosition(FMath::Clamp(Minimum.X, 0., FMath::Max(0., ViewportSize.X - TextSize.X)),
		                               FMath::Max(0., Minimum.Y - TextSize.Y - 4.));
		DrawBox(OutDrawElements, Layer, AllottedGeometry, MarkerPosition, TextSize, FLinearColor(0.f, 0.f, 0.f, .85f));
		DrawText(OutDrawElements, Layer, AllottedGeometry, MarkerPosition, Marker, MarkerFont,
		         Settings.InspectorHighlightColor, Measure);
	}

	double LegendBottom = ViewportSize.Y - 14.;
	if (Inspector.AgentId != INDEX_NONE)
	{
		const float Scale = FMath::IsFinite(Settings.InspectorScale)
			                    ? FMath::Clamp(Settings.InspectorScale, .25f, 3.f)
			                    : 1.f;
		const double AuthoredMargin = FMath::IsFinite(Settings.InspectorViewportMargin)
			                              ? FMath::Max(0.f, Settings.InspectorViewportMargin)
			                              : 18.f;
		const double Margin = FMath::Min(AuthoredMargin, FMath::Min(ViewportSize.X, ViewportSize.Y) * .2);
		const double AuthoredWidth = FMath::IsFinite(Settings.InspectorWidth)
			                             ? FMath::Max(160.f, Settings.InspectorWidth)
			                             : 460.f;
		const double Width = FMath::Min(AuthoredWidth, (ViewportSize.X - Margin * 2.) / Scale);
		const double MaximumHeight = (ViewportSize.Y - Margin * 2.) / Scale;
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(
			"Regular", FMath::Clamp(Settings.InspectorFontSize, 6, 32));
		const double LineHeight = Measure->GetMaxCharacterHeight(Font) + 2.;
		const int32 MaximumRows = FMath::Max(0, FMath::FloorToInt((MaximumHeight - 16.) / LineHeight) - 1);
		if (Width > 24. && MaximumRows > 0)
		{
			TArray<FString> Lines;
			for (const FString& Line : Inspector.Lines) WrapInspectorLine(Line, Font, Width - 20., Measure, Lines);
			if (Lines.Num() > MaximumRows)
			{
				Lines.SetNum(MaximumRows);
				Lines.Last() = TEXT("... more detail: reduce Inspector Scale");
			}
			const FVector2D Size(Width, 16. + LineHeight * (Lines.Num() + 1));
			const FVector2D Position(ViewportSize.X - Margin - Size.X * Scale,
			                         ViewportSize.Y - Margin - Size.Y * Scale);
			const FGeometry PanelGeometry = AllottedGeometry.MakeChild(Size, FSlateLayoutTransform(Scale, Position));
			OutDrawElements.PushClip(FSlateClippingZone(PanelGeometry));
			DrawBox(OutDrawElements, Layer, PanelGeometry, FVector2D::ZeroVector, Size,
			        FLinearColor(.01f, .015f, .025f, .92f));
			DrawBox(OutDrawElements, Layer, PanelGeometry, FVector2D::ZeroVector, FVector2D(2., Size.Y),
			        Settings.InspectorHighlightColor);
			const FString Title = FString::Printf(
				TEXT("AGENT #%d%s | SELECT replaces / CLEAR closes"), Inspector.AgentId,
				Inspector.bHasScreenBounds ? TEXT("") : TEXT(" (OFF SCREEN)"));
			DrawText(OutDrawElements, Layer, PanelGeometry, FVector2D(10., 8.),
			         FitText(Title, Font, Width - 20., Measure), Font, Settings.InspectorHighlightColor, Measure);
			for (int32 Index = 0; Index < Lines.Num(); ++Index) DrawText(
				OutDrawElements, Layer, PanelGeometry, FVector2D(10., 8. + LineHeight * (Index + 1)),
				FitText(Lines[Index], Font, Width - 20., Measure), Font, Settings.BadgeTextColor, Measure);
			OutDrawElements.PopClip();
			LegendBottom = Position.Y - 8.;
		}
	}

	if (!Legend.IsEmpty())
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", Settings.FontSize);
		TArray<FString> Lines;
		Legend.ParseIntoArrayLines(Lines, false);
		const float LineHeight = Measure->GetMaxCharacterHeight(Font);
		const int32 MaximumRows = FMath::Max(0, FMath::FloorToInt((LegendBottom - 26.) / FMath::Max(1.f, LineHeight)));
		if (Lines.Num() > MaximumRows) Lines.SetNum(MaximumRows);
		if (!Lines.IsEmpty())
		{
			float Width = 0.f;
			for (const FString& Line : Lines) Width = FMath::Max(
				Width, static_cast<float>(Measure->Measure(Line, Font).X));
			const FVector2D Size(
				FMath::Max(1., FMath::Min(static_cast<double>(Width + 16.f), FMath::Min(520., ViewportSize.X - 28.))),
				LineHeight * Lines.Num() + 12.f);
			const FVector2D Position(FMath::Max(0., ViewportSize.X - Size.X - 14.),
			                         FMath::Max(0., LegendBottom - Size.Y));
			DrawBox(OutDrawElements, Layer, AllottedGeometry, Position, Size,
			        Settings.BadgeBackground.CopyWithNewOpacity(.65f));
			for (int32 Index = 0; Index < Lines.Num(); ++Index) DrawText(
				OutDrawElements, Layer, AllottedGeometry, Position + FVector2D(8.f, 6.f + LineHeight * Index),
				FitText(Lines[Index], Font, Size.X - 16., Measure), Font, FLinearColor(.88f, .92f, 1.f, .9f), Measure);
		}
	}

	OutDrawElements.PopClip();
	return Layer;
}
