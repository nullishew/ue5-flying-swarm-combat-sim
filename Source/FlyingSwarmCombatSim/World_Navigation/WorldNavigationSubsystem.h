#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World_Navigation/NavigationGraphAsset.h"
#include "WorldNavigationSubsystem.generated.h"

class ARoomArea;
class ANavigationWaypoint;
class ULevel;

struct FNavigationGraphArc
{
	int32 From = INDEX_NONE;
	int32 To = INDEX_NONE;
	float Length = 0.f;
};

/** Immutable topology shared by all route requests; collision is validated separately. */
struct FLYINGSWARMCOMBATSIM_API FNavigationGraphSnapshot
{
	uint32 Generation = 0;
	bool bHasAuthoredData = false;
	TArray<FNavigationWaypointNode> Nodes;
	TArray<FNavigationGraphEdge> Edges;
	TArray<FString> Diagnostics;
	TArray<FNavigationGraphArc> Arcs;
	TArray<TArray<int32>> Outgoing;
	void BuildAdjacency();
};

/** World data registry, independent of swarm simulation and population ownership. */
UCLASS(meta=(DisplayName="World Navigation"))
class FLYINGSWARMCOMBATSIM_API UWorldNavigationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	void RegisterActor(AActor* Actor);
	void UnregisterActor(AActor* Actor);
	UFUNCTION(BlueprintCallable, Category="Rooms") void Invalidate();
	UFUNCTION(BlueprintPure, Category="Rooms") TArray<ARoomArea*> GetRooms() const;
	UFUNCTION(BlueprintPure, Category="Rooms") TArray<ARoomArea*> GetContainingRooms(const FVector& Position) const;
	UFUNCTION(BlueprintPure, Category="Rooms") ARoomArea* GetPrimaryRoom(const FVector& Position) const;
	TSharedRef<const FNavigationGraphSnapshot> GetSnapshot();
private:
	TSet<TWeakObjectPtr<ARoomArea>> Rooms;
	TSet<TWeakObjectPtr<ANavigationWaypoint>> Waypoints;
	TSharedPtr<const FNavigationGraphSnapshot> Snapshot;
	uint32 Generation = 1;
	bool bDirty = true;
	FDelegateHandle LevelAddedHandle;
	FDelegateHandle LevelRemovedHandle;
	void LevelAdded(ULevel* Level, UWorld* World);
	void LevelRemoved(ULevel* Level, UWorld* World);
};
