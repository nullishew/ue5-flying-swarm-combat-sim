#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomArea.generated.h"

class UBoxComponent;
class UNavigationMarkerComponent;

UCLASS(BlueprintType, meta=(DisplayName="Room"))
class FLYINGSWARMCOMBATSIM_API ARoomArea : public AActor
{
	GENERATED_BODY()
public:
	ARoomArea();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Room", meta=(DisplayName="Room Bounds")) TObjectPtr<UBoxComponent> RegionBounds;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Room", meta=(DisplayName="Room ID")) FName RegionId = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Room", meta=(DisplayName="Overlap Priority")) int32 TransitionPriority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Room", meta=(DisplayName="Room Name")) FString RoomName;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Room") TObjectPtr<UNavigationMarkerComponent> NavigationMarker;
	UFUNCTION(BlueprintPure, Category="Room") FName GetRoomId() const { return RegionId.IsNone() ? GetFName() : RegionId; }
	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditUndo() override;
#endif
	bool Contains(const FVector& Position, float InsetCm = 0.f) const;
};
