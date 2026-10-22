#pragma once

#include "CoreMinimal.h"
#include "SwarmDebugSettings.generated.h"

class UMaterialInterface;

/** Presentation only. These defaults never modify simulation or shared steering assets. */
USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FSwarmDebugSettings
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Awareness") bool bBadges = true;
	UPROPERTY(EditDefaultsOnly, Category = "Awareness") bool bOutlines = true;
	/** Post-process material using stencil categories 1/2/3 and UnawareColor/AwareColor/EngagingColor parameters. */
	UPROPERTY(EditDefaultsOnly, Category = "Awareness") TObjectPtr<UMaterialInterface> OutlineMaterial;
	UPROPERTY(EditDefaultsOnly, Category = "Awareness") FLinearColor UnawareColor = FLinearColor::White;
	UPROPERTY(EditDefaultsOnly, Category = "Awareness") FLinearColor AwareColor = FLinearColor(1.f, .55f, .05f);
	UPROPERTY(EditDefaultsOnly, Category = "Awareness") FLinearColor EngagingColor = FLinearColor(1.f, .08f, .04f);
	UPROPERTY(EditDefaultsOnly, Category = "Badges", meta = (ClampMin = "8", ClampMax = "32")) int32 FontSize = 12;
	UPROPERTY(EditDefaultsOnly, Category = "Badges") FLinearColor BadgeBackground = FLinearColor(.01f, .015f, .025f, .25f);
	/** Active movement-goal link, or a stationary marker while holding. */
	UPROPERTY(EditDefaultsOnly, Category = "Spatial") bool bDestinations = true;
	/** Continue the valid remaining route beyond the immediate goal. */
	UPROPERTY(EditDefaultsOnly, Category = "Spatial") bool bWaypoints = true;
	UPROPERTY(EditDefaultsOnly, Category = "Spatial") bool bVelocity = true;
	/** Serialized compatibility only; disconnected requested markers are no longer drawn. */
	UPROPERTY() bool bRequestedGoals = false;
	UPROPERTY(EditDefaultsOnly, Category = "Spatial") FLinearColor DestinationColor = FLinearColor(1.f, .75f, .1f);
	UPROPERTY(EditDefaultsOnly, Category = "Spatial") FLinearColor WaypointColor = FLinearColor(0.f, .8f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "Spatial") FLinearColor VelocityColor = FLinearColor(.15f, 1.f, .3f);
	UPROPERTY(EditDefaultsOnly, Category = "Badges", meta = (ClampMin = "1", Units = "cm")) float BadgeReferenceDepthCm = 1000.f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges", meta = (ClampMin = "0.1", ClampMax = "1")) float BadgeMinScale = .55f;
	UPROPERTY(EditDefaultsOnly, Category = "Badges", meta = (ClampMin = "0", Units = "cm")) float BadgeHeightCm = 20.f;
	UPROPERTY(EditDefaultsOnly, Category = "Spatial", meta = (ClampMin = "1", ClampMax = "8")) float LineThickness = 3.f;
	UPROPERTY(EditDefaultsOnly, Category = "Spatial", meta = (ClampMin = "1", Units = "cm")) float MarkerSizeCm = 18.f;
	UPROPERTY(EditDefaultsOnly, Category = "Spatial", meta = (ClampMin = "0.001", Units = "s")) float VelocitySeconds = .5f;
	UPROPERTY(EditDefaultsOnly, Category = "Spatial", meta = (ClampMin = "1", Units = "cm")) float MaxVelocityArrowCm = 500.f;
};
