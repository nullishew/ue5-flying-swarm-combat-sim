#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HitFeedbackWidget.generated.h"

class UImage;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Native full-screen image; no widget Blueprint or animation tracks required. */
UCLASS()
class FLYINGSWARMCOMBATSIM_API UHitFeedbackWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(UMaterialInterface* Material, const FLinearColor& Color, float Opacity, float FadeIn, float FadeOut);
	void Pulse();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UImage> VignetteImage;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> VignetteMaterial;

	float Age = 0.0f;
	float StartOpacity = 0.0f;
	float CurrentOpacity = 0.0f;
	float PeakOpacity = 0.45f;
	float FadeInSeconds = 0.035f;
	float FadeOutSeconds = 0.3f;
	bool bPulsing = false;
};
