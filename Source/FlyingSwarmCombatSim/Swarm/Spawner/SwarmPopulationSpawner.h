#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SwarmPopulationSpawner.generated.h"

class AFlyingBoid;
class ASwarmSpawnVolume;
class UBoxComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSwarmPopulationChanged, int32, PreviousCount, int32, CurrentCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSwarmPopulationBelowQuota, int32, CurrentCount, int32, Quota);

UENUM(BlueprintType)
enum class ESwarmPopulationSpawnState : uint8
{
	AtQuota,
	WaitingInitialDelay,
	Spawning
};

/** Counts living boids overlapping its sensor, independently of simulation membership. */
UCLASS(Blueprintable)
class FLYINGSWARMCOMBATSIM_API ASwarmPopulationSpawner : public AActor
{
	GENERATED_BODY()

public:
	ASwarmPopulationSpawner();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	
	UFUNCTION(BlueprintCallable, Category = "Spawner")
	AFlyingBoid* SpawnEnemy();
	UFUNCTION(BlueprintCallable, Category = "Spawner")
	void SetPopulationQuota(int32 NewQuota);

	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner", meta = (DisplayName = "Population Bounds"))
	TObjectPtr<UBoxComponent> SpawnBounds;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner")
	TSubclassOf<AFlyingBoid> EnemyClass;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Spawner")
	TArray<TObjectPtr<ASwarmSpawnVolume>> SpawnVolumes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner", meta = (ClampMin = "0"))
	int32 PopulationQuota = 20;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner|Timing", meta = (ClampMin = "0", Units = "s"))
	float InitialSpawnDelaySeconds = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner|Timing", meta = (ClampMin = "0.001", Units = "s"))
	float MinSpawnIntervalSeconds = .5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner|Timing", meta = (ClampMin = "0.001", Units = "s"))
	float MaxSpawnIntervalSeconds = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner", meta = (ClampMin = "1"))
	int32 MaxPlacementAttempts = 16;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Spawner")
	int32 CurrentPopulation = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Spawner")
	ESwarmPopulationSpawnState SpawnState = ESwarmPopulationSpawnState::AtQuota;
	UPROPERTY(BlueprintAssignable, Category = "Spawner")
	FSwarmPopulationChanged OnPopulationChanged;
	UPROPERTY(BlueprintAssignable, Category = "Spawner")
	FSwarmPopulationBelowQuota OnPopulationBelowQuota;

private:
	TSet<TWeakObjectPtr<AFlyingBoid>> Population;
	FTimerHandle SpawnTimer;
	uint64 SequenceGeneration = 0;
	bool bInitialized = false;
	bool bEndingPlay = false;
	bool bBelowQuota = false;
	bool bTimingValid = false;
	bool bSpawnInProgress = false;

	void RegisterBoid(AFlyingBoid* Boid);
	void UnregisterBoid(AFlyingBoid* Boid);
	void PublishPopulation();
	bool ReconcileSpawnState();
	void CancelSpawnSequence();
	void ScheduleSpawn(float Delay);
	void HandleSpawnTimer(uint64 ExpectedGeneration);
	bool CanSpawnAtLocation(const FVector& Location, float Radius, const AFlyingBoid* Defaults) const;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* Component, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* Component, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);
	UFUNCTION()
	void HandleBoidDeath();
	UFUNCTION()
	void HandleBoidEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
};
