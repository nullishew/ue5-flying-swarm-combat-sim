#pragma once

#include "CoreMinimal.h"
#include "SwarmDebugSettings.generated.h"

class UMaterialInterface;

USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FSwarmDebugSettings
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Awareness") bool bBadges = true;
	UPROPERTY(EditDefaultsOnly, Category = "Awareness") bool bOutlines = true;
	// Post-process material using stencil categories 1/2/3 and UnawareColor/AwareColor/EngagingColor parameters.
	UPROPERTY(EditDefaultsOnly, Category = "Awareness") TObjectPtr<UMaterialInterface> OutlineMaterial;
	UPROPERTY(EditDefaultsOnly, Category = "Awareness") FLinearColor UnawareColor = FLinearColor::White;
	UPROPERTY(EditDefaultsOnly, Category = "Awareness") FLinearColor AwareColor = FLinearColor(1.f, .55f, .05f);
	UPROPERTY(EditDefaultsOnly, Category = "Awareness") FLinearColor EngagingColor = FLinearColor(1.f, .08f, .04f);

	// Legend appearance
	UPROPERTY() int32 FontSize = 12;
	UPROPERTY() FLinearColor BadgeBackground = FLinearColor(.01f, .015f, .025f, .25f);

	UPROPERTY(EditDefaultsOnly, Category = "Badges") int32 BadgeFontSize = 18;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") int32 BadgeReasonFontSize = 10;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") int32 BadgeTimerFontSize = 8;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") int32 BadgeBarLabelFontSize = 6;
	UPROPERTY(EditDefaultsOnly, Category = "Badges", meta = (HideAlphaChannel)) FLinearColor BadgePanelColor = FLinearColor(.01f, .015f, .025f, .3f);
	UPROPERTY(EditDefaultsOnly, Category = "Badges") FLinearColor BadgeTextColor = FLinearColor(.88f, .92f, 1.f);
	// Opacity of panels, accents, bar outlines and bar fills; text uses BadgeTextColor.
	UPROPERTY(EditDefaultsOnly, Category = "Badges") float BadgeOpacity = .3f;
	// Start/end of badge scale interpolation, in camera distance
	UPROPERTY(EditDefaultsOnly, Category = "Badges", meta = (ClampMin = "0.01")) float BadgeNearScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges", meta = (ClampMin = "0.01")) float BadgeDistantScale = 0.67f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges", meta = (Units = "cm")) float BadgeFullSizeDistanceCm = 3000.f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges", meta = (Units = "cm")) float BadgeFalloffEndDistanceCm = 6000.f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") float BadgeWidth = 200.f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") float BadgePaddingX = 4.f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") float BadgePaddingY = 2.f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") float BadgeRowGap = 0.f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") float BadgeColumnGap = 2.f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") float BadgeBarHeight = 2.f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") float BadgeBarOutlineThickness = .5f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") FLinearColor BadgeHealthBarColor = FLinearColor(.12f, .82f, .28f);
	UPROPERTY(EditDefaultsOnly, Category = "Badges") FLinearColor BadgeAttackBarEmptyColor = FLinearColor(.5f, .0f, .0f);
	UPROPERTY(EditDefaultsOnly, Category = "Badges") FLinearColor BadgeAttackBarPartialColor = FLinearColor(1.f, .55f, .05f);
	UPROPERTY(EditDefaultsOnly, Category = "Badges") FLinearColor BadgeAttackBarFullColor = FLinearColor::White;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") float BadgeAccentWidth = 1.f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") float BadgeTimerWidth = 28.f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") float BadgeScreenGap = 8.f;

	// Current enemy projectile collision boundaries
	UPROPERTY(EditDefaultsOnly, Category = "Collision") bool bProjectileShapes = true;
	UPROPERTY(EditDefaultsOnly, Category = "Collision") FLinearColor ProjectileColor = FLinearColor(1.f, .05f, .65f);
	UPROPERTY(EditDefaultsOnly, Category = "Collision", meta = (ClampMin = "8", ClampMax = "64")) int32 ProjectileRingSegments = 32;
	UPROPERTY(EditDefaultsOnly, Category = "Collision", meta = (ClampMin = "0.5", ClampMax = "4")) float ProjectileLineThickness = 1.5f;

	// Active movement-goal link, or a stationary marker while holding.
	UPROPERTY(EditDefaultsOnly, Category = "Spatial") bool bDestinations = true;
	// Continue the valid remaining route beyond the immediate goal.
	UPROPERTY(EditDefaultsOnly, Category = "Spatial") bool bWaypoints = true;
	UPROPERTY(EditDefaultsOnly, Category = "Spatial") bool bVelocity = true;
	UPROPERTY(EditDefaultsOnly, Category = "Spatial") FLinearColor DestinationColor = FLinearColor(1.f, .75f, .1f);
	UPROPERTY(EditDefaultsOnly, Category = "Spatial") FLinearColor WaypointColor = FLinearColor(0.f, .8f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "Spatial") FLinearColor VelocityColor = FLinearColor(.15f, 1.f, .3f);
	UPROPERTY(EditDefaultsOnly, Category = "Badges", meta = (Units = "cm")) float BadgeHeightCm = 20.f;
	UPROPERTY(EditDefaultsOnly, Category = "Spatial", meta = (ClampMin = "1", ClampMax = "8")) float LineThickness = 3.f;
	UPROPERTY(EditDefaultsOnly, Category = "Spatial", meta = (ClampMin = "1", Units = "cm")) float MarkerSizeCm = 18.f;
	UPROPERTY(EditDefaultsOnly, Category = "Spatial", meta = (ClampMin = "0.001", Units = "s")) float VelocitySeconds = .5f;
	UPROPERTY(EditDefaultsOnly, Category = "Spatial", meta = (ClampMin = "1", Units = "cm")) float MaxVelocityArrowCm = 500.f;

	// Max screen-space distance from aim when selecting an agent, in Slate units.
	UPROPERTY(EditDefaultsOnly, Category = "Inspector", meta = (ClampMin = "1"))
	float InspectorSelectionRadius = 80.f;
	UPROPERTY(EditDefaultsOnly, Category = "Inspector", meta = (ClampMin = "160")) float InspectorWidth = 460.f;
	UPROPERTY(EditDefaultsOnly, Category = "Inspector", meta = (ClampMin = "0")) float InspectorViewportMargin = 18.f;
	UPROPERTY(EditDefaultsOnly, Category = "Inspector", meta = (ClampMin = "6", ClampMax = "32")) int32 InspectorFontSize = 12;
	UPROPERTY(EditDefaultsOnly, Category = "Inspector", meta = (ClampMin = "0.25", ClampMax = "3")) float InspectorScale = 1.f;
	UPROPERTY(EditDefaultsOnly, Category = "Inspector") FLinearColor InspectorHighlightColor = FLinearColor(0.f, 1.f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "Inspector", meta = (ClampMin = "1", ClampMax = "8")) float InspectorHighlightThickness = 2.f;
};
