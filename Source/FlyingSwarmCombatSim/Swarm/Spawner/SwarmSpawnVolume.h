#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SwarmSpawnVolume.generated.h"

class UBoxComponent;

/** Authored spawn space; assign placed instances to a SwarmPopulationSpawner. */
UCLASS(Blueprintable)
class FLYINGSWARMCOMBATSIM_API ASwarmSpawnVolume : public AActor
{
	GENERATED_BODY()

public:
	ASwarmSpawnVolume();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner")
	TObjectPtr<UBoxComponent> SpawnBox;
};
