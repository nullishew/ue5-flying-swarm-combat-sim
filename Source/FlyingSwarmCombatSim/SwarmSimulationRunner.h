#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SwarmSimulationRunner.generated.h"

class AFlyingBoid;

/** Minimal placeable driver for the first one-boid movement test. */
UCLASS()
class FLYINGSWARMCOMBATSIM_API ASwarmSimulationRunner : public AActor
{
	GENERATED_BODY()

public:
	ASwarmSimulationRunner();
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Swarm Test")
	TObjectPtr<AFlyingBoid> TestBoid;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Swarm Test")
	TObjectPtr<AActor> TargetActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm Test")
	bool bDrawTarget = true;
};
