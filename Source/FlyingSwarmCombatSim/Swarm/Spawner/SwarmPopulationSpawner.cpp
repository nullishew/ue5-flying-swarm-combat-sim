// Fill out your copyright notice in the Description page of Project Settings.


#include "Swarm/Spawner/SwarmPopulationSpawner.h"
#include "Swarm/FlyingBoid.h"
#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "Engine/World.h"
#include "Components/SphereComponent.h"

// Sets default values
ASwarmPopulationSpawner::ASwarmPopulationSpawner()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SpawnBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnBounds"));
	SetRootComponent(SpawnBounds);
	SpawnBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnBounds->InitBoxExtent(FVector(500.f, 500.f, 500.f));
	SpawnBounds->SetCanEverAffectNavigation(false);

	SpawnBounds->SetGenerateOverlapEvents(false);



}

// Called when the game starts or when spawned
void ASwarmPopulationSpawner::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Verbose, TEXT("Swarm spawner beginning play."));
	SpawnEnemy();
}

// Called every frame
void ASwarmPopulationSpawner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}


bool ASwarmPopulationSpawner::CanSpawnAtLocation(const FVector& Location, const float &Radius)
{
	const auto* Simulation = GetWorld()->GetSubsystem<USwarmSimulationSubsystem>();
    if (!Simulation || !Simulation->IsInsideSafeBounds(Location, Radius)
        || SpawnBounds->GetScaledBoxExtent().GetMax() < Radius) return false;
	const FCollisionShape Body = FCollisionShape::MakeSphere(Radius);
	const FCollisionObjectQueryParams ObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
	if (GetWorld()->OverlapAnyTestByObjectType(Location, FQuat::Identity, ObjectQueryParams, Body)) return false;
	return true;
}

AFlyingBoid* ASwarmPopulationSpawner::SpawnEnemy()
{
	if (!EnemyClass || !GetWorld()) return nullptr;

	const AFlyingBoid* EnemyDefaults = EnemyClass->GetDefaultObject<AFlyingBoid>();
	const float EnemyRadius = EnemyDefaults->CollisionSphere->GetScaledSphereRadius();

	const FVector Location = SpawnBounds->GetComponentLocation();

	if (!CanSpawnAtLocation(Location, EnemyRadius)) return nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
	AFlyingBoid* SpawnedEnemy = GetWorld()->SpawnActor<AFlyingBoid>(EnemyClass, Location, SpawnBounds->GetComponentRotation(), SpawnParams);
	if (IsValid(SpawnedEnemy))
	{
		UE_LOG(LogTemp, Warning, TEXT("Spawned boid"));
	}
	UE_LOG(LogTemp, Warning, TEXT("At least the spawn code made it here"));
	return SpawnedEnemy;
}
