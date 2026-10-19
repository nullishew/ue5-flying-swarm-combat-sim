// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "SwarmPopulationSpawner.generated.h"

class AFlyingBoid;
class ASwarmSimulationRunner;

UCLASS(BLUEPRINTABLE)
class FLYINGSWARMCOMBATSIM_API ASwarmPopulationSpawner : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASwarmPopulationSpawner();

	AFlyingBoid* SpawnEnemy();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner")
	TObjectPtr<UBoxComponent> SpawnBounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner")
	TSubclassOf<AFlyingBoid> EnemyClass;


	bool CanSpawnAtLocation(const FVector& Location, const float &Radius);





	// spaghetti???
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Spawner")
	TObjectPtr<ASwarmSimulationRunner> SwarmSimulationRunner;




public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

};
