#include "Swarm/Simulation/SwarmSimulationBounds.h"
#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"

ASwarmSimulationBounds::ASwarmSimulationBounds()
{
	PrimaryActorTick.bCanEverTick = false;
	FlightBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("FlightBounds"));
	SetRootComponent(FlightBounds);
	FlightBounds->InitBoxExtent(FVector(2500.f, 2500.f, 1000.f));
	FlightBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlightBounds->SetGenerateOverlapEvents(false);
	FlightBounds->SetCanEverAffectNavigation(false);
}

void ASwarmSimulationBounds::BeginPlay()
{
	bEndingPlay = false;
	Super::BeginPlay();
	if (auto* Simulation = GetWorld()->GetSubsystem<USwarmSimulationSubsystem>()) Simulation->NotifySetupChanged();
}

void ASwarmSimulationBounds::EndPlay(EEndPlayReason::Type Reason)
{
	bEndingPlay = true;
	if (auto* Simulation = GetWorld()->GetSubsystem<USwarmSimulationSubsystem>()) Simulation->NotifySetupChanged();
	Super::EndPlay(Reason);
}
