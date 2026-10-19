#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Swarm/Routing/BoidNavigationTypes.h"
#include "SwarmSimulationBounds.generated.h"

class UBoxComponent;
class UNavigationGraphAsset;

/** Place one in the persistent level. This actor never steps agents. */
UCLASS()
class FLYINGSWARMCOMBATSIM_API ASwarmSimulationBounds : public AActor
{
	GENERATED_BODY()

public:
	ASwarmSimulationBounds();
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Swarm|Bounds")
	TObjectPtr<UBoxComponent> FlightBounds;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Swarm|Navigation")
	EBoidNavigationMode NavigationMode = EBoidNavigationMode::DirectOnly;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Swarm|Navigation")
	TObjectPtr<UNavigationGraphAsset> NavigationGraph;
	bool IsAvailableForSimulation() const { return !bEndingPlay && !IsActorBeingDestroyed(); }

private:
	bool bEndingPlay = false;
};
