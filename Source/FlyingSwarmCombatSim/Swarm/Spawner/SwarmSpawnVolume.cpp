#include "Swarm/Spawner/SwarmSpawnVolume.h"
#include "Components/BoxComponent.h"

ASwarmSpawnVolume::ASwarmSpawnVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	SpawnBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnBox"));
	SetRootComponent(SpawnBox);
	SpawnBox->InitBoxExtent(FVector(200.f));
	SpawnBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnBox->SetGenerateOverlapEvents(false);
	SpawnBox->SetCanEverAffectNavigation(false);
	SpawnBox->ShapeColor = FColor::Green;
}
