#include "UI/HitFeedbackWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"

TSharedRef<SWidget> UHitFeedbackWidget::RebuildWidget()
{
	Initialize();
	if (WidgetTree && !VignetteImage)
	{
		VignetteImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HitVignette"));
		WidgetTree->RootWidget = VignetteImage;
		if (VignetteMaterial)
		{
			VignetteImage->SetBrushFromMaterial(VignetteMaterial);
		}
		else
		{
			VignetteImage->SetRenderOpacity(0.0f);
		}
	}
	return Super::RebuildWidget();
}

void UHitFeedbackWidget::Configure(UMaterialInterface* Material, const FLinearColor& Color, float Opacity, float FadeIn, float FadeOut)
{
	PeakOpacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	FadeInSeconds = FMath::Max(0.001f, FadeIn);
	FadeOutSeconds = FMath::Max(0.001f, FadeOut);
	CurrentOpacity = 0.0f;
	bPulsing = false;
	VignetteMaterial = Material ? UMaterialInstanceDynamic::Create(Material, this) : nullptr;
	if (VignetteMaterial)
	{
		VignetteMaterial->SetVectorParameterValue(TEXT("HitColor"), Color);
		VignetteMaterial->SetScalarParameterValue(TEXT("HitOpacity"), 0.0f);
	}
	if (VignetteImage)
	{
		VignetteImage->SetBrushFromMaterial(VignetteMaterial);
		VignetteImage->SetRenderOpacity(VignetteMaterial ? 1.0f : 0.0f);
	}
}

void UHitFeedbackWidget::Pulse()
{
	StartOpacity = CurrentOpacity;
	Age = 0.0f;
	bPulsing = true;
}

void UHitFeedbackWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bPulsing || !VignetteMaterial) return;
	Age += FMath::Max(0.0f, InDeltaTime);
	if (Age < FadeInSeconds)
	{
		CurrentOpacity = FMath::Lerp(StartOpacity, PeakOpacity, FMath::SmoothStep(0.0f, FadeInSeconds, Age));
	}
	else
	{
		CurrentOpacity = PeakOpacity * (1.0f - FMath::SmoothStep(0.0f, FadeOutSeconds, Age - FadeInSeconds));
	}
	VignetteMaterial->SetScalarParameterValue(TEXT("HitOpacity"), CurrentOpacity);
	bPulsing = Age < FadeInSeconds + FadeOutSeconds;
}
