#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "NavigationWaypoint.generated.h"

class ARoomArea;
class ANavigationWaypoint;

/** Editor visualization hook; no movement or runtime ticking. */
UCLASS()
class FLYINGSWARMCOMBATSIM_API UNavigationMarkerComponent : public USceneComponent
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FNavigationWaypointLink
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Navigation", meta=(DisplayName="Destination")) TObjectPtr<ANavigationWaypoint> Destination;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Navigation", meta=(DisplayName="Two Way")) bool bBidirectional = true;
};

UCLASS(BlueprintType, meta=(DisplayName="Navigation Waypoint"))
class FLYINGSWARMCOMBATSIM_API ANavigationWaypoint : public AActor
{
	GENERATED_BODY()
public:
	ANavigationWaypoint();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UNavigationMarkerComponent> Marker;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Navigation", meta=(DisplayName="Room (Optional)", ToolTip="Optional room metadata. Leave empty for tunnel bends or other standalone navigation points. This does not attach the waypoint or create links.")) TObjectPtr<ARoomArea> Room;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Navigation", meta=(TitleProperty="Destination", DisplayName="Connections")) TArray<FNavigationWaypointLink> Links;
	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditUndo() override;
#endif
};
