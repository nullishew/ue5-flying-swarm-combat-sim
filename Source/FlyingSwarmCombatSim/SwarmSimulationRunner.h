#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "SwarmSimulationRunner.generated.h"

class AFlyingBoid;

/** Shared movement scales and bounded simulation timing for this test swarm. */
USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FSwarmTuningSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0"))
	float SpeedScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0"))
	float AccelerationScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0"))
	float BrakingScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.001"))
	float MaxStepSeconds = 1.0f / 30.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.001"))
	float MaxFrameSeconds = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxSubsteps = 8;
	/** Reserved for repeatable candidate/tie decisions in later components. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Determinism")
	int32 InitialSeed = 1337;
};

/** One update owner for every boid in the test scene. */
UCLASS()
class FLYINGSWARMCOMBATSIM_API ASwarmSimulationRunner : public AActor
{
	GENERATED_BODY()

public:
	ASwarmSimulationRunner();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Registration is queued and applied before the next simulation step. */
	UFUNCTION(BlueprintCallable, Category = "Swarm")
	void RegisterBoid(AFlyingBoid* Boid);
	UFUNCTION(BlueprintCallable, Category = "Swarm")
	void UnregisterBoid(AFlyingBoid* Boid);
	UFUNCTION(BlueprintPure, Category = "Swarm")
	int32 GetAgentId(AFlyingBoid* Boid) const;

	/** Kept for existing one-boid test-level assignments. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Swarm Test")
	TObjectPtr<AFlyingBoid> TestBoid;

	/** Shared fallback target for agents without MovementTarget. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Swarm Test")
	TObjectPtr<AActor> TargetActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm Test")
	bool bDrawTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm|Tuning")
	FSwarmTuningSettings Tuning;

private:
	struct FRegisteredAgent
	{
		TWeakObjectPtr<AFlyingBoid> Boid;
		int32 Id = INDEX_NONE;
	};

	void ApplyQueuedRegistrations();
	void AdvanceStep(float StepTime);

	TArray<FRegisteredAgent> Agents;
	TArray<TWeakObjectPtr<AFlyingBoid>> PendingAdditions;
	TArray<TWeakObjectPtr<AFlyingBoid>> PendingRemovals;
	int32 NextAgentId = 1;
	FRandomStream RandomStream;
};
