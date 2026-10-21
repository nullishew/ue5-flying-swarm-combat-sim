#include "Swarm/Spawner/SwarmPopulationSpawner.h"

#include "Swarm/Spawner/SwarmSpawnVolume.h"
#include "Swarm/FlyingBoid.h"
#include "Swarm/Flight/BoidFlightAlgorithms.h"
#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwarmSpawner, Log, All);

namespace
{
	bool SphereSafeBox(const UBoxComponent* Box, float Radius, FTransform& Transform, FVector& Extent)
	{
		if (!IsValid(Box) || !FMath::IsFinite(Radius) || Radius <= 0.f
			|| Box->GetComponentTransform().ContainsNaN()
			|| Box->GetComponentScale().GetAbs().GetMin() <= UE_SMALL_NUMBER
			|| Box->GetScaledBoxExtent().ContainsNaN()) return false;
		Transform = Box->GetComponentTransform();
		Extent = Box->GetScaledBoxExtent() - FVector(Radius);
		return Extent.GetMin() >= 0.f;
	}
}

ASwarmPopulationSpawner::ASwarmPopulationSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	SpawnBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnBounds"));
	SetRootComponent(SpawnBounds);
	SpawnBounds->InitBoxExtent(FVector(500.f));
	SpawnBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SpawnBounds->SetCollisionObjectType(ECC_WorldDynamic);
	SpawnBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	SpawnBounds->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SpawnBounds->SetGenerateOverlapEvents(true);
	SpawnBounds->SetCanEverAffectNavigation(false);
	SpawnBounds->ShapeColor = FColor::Cyan;
}

void ASwarmPopulationSpawner::BeginPlay()
{
	SpawnBounds->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::HandleBeginOverlap);
	SpawnBounds->OnComponentEndOverlap.AddUniqueDynamic(this, &ThisClass::HandleEndOverlap);
	SpawnBounds->SetCollisionObjectType(ECC_WorldDynamic);
	SpawnBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	SpawnBounds->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SpawnBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SpawnBounds->SetGenerateOverlapEvents(true);
	SetActorTickEnabled(false);
	Super::BeginPlay();
	if (bEndingPlay) return;

	bTimingValid = PopulationQuota >= 0 && FMath::IsFinite(InitialSpawnDelaySeconds)
		&& InitialSpawnDelaySeconds >= 0.f && FMath::IsFinite(MinSpawnIntervalSeconds)
		&& MinSpawnIntervalSeconds > 0.f && FMath::IsFinite(MaxSpawnIntervalSeconds)
		&& MaxSpawnIntervalSeconds >= MinSpawnIntervalSeconds && MaxPlacementAttempts > 0;
	if (!bTimingValid)
	{
		UE_LOG(LogSwarmSpawner, Error, TEXT("%s: Invalid quota, delay, interval range, or placement budget; automatic spawning disabled."), *GetName());
	}
	if (!EnemyClass || SpawnVolumes.IsEmpty())
	{
		UE_LOG(LogSwarmSpawner, Warning, TEXT("%s: Assign EnemyClass and placed SpawnVolumes to enable successful spawning."), *GetName());
	}

	SpawnBounds->UpdateOverlaps();
	TArray<UPrimitiveComponent*> Components;
	SpawnBounds->GetOverlappingComponents(Components);
	for (UPrimitiveComponent* Component : Components)
	{
		if (AFlyingBoid* Boid = Cast<AFlyingBoid>(Component->GetOwner()); Boid && Component == Boid->CollisionSphere)
		{
			RegisterBoid(Boid);
		}
	}
	bInitialized = true;
	PublishPopulation();
}

void ASwarmPopulationSpawner::EndPlay(const EEndPlayReason::Type Reason)
{
	bEndingPlay = true;
	CancelSpawnSequence();
	SpawnBounds->OnComponentBeginOverlap.RemoveDynamic(this, &ThisClass::HandleBeginOverlap);
	SpawnBounds->OnComponentEndOverlap.RemoveDynamic(this, &ThisClass::HandleEndOverlap);
	for (const TWeakObjectPtr<AFlyingBoid>& Entry : Population)
	{
		if (AFlyingBoid* Boid = Entry.Get())
		{
			Boid->OnDeath.RemoveDynamic(this, &ThisClass::HandleBoidDeath);
			Boid->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleBoidEndPlay);
		}
	}
	Population.Empty();
	CurrentPopulation = 0;
	bInitialized = false;
	Super::EndPlay(Reason);
}

void ASwarmPopulationSpawner::RegisterBoid(AFlyingBoid* Boid)
{
	if (bEndingPlay || !IsValid(Boid) || Boid->IsActorBeingDestroyed() || !Boid->IsAlive() || Population.Contains(Boid)) return;
	Population.Add(Boid);
	Boid->OnDeath.AddUniqueDynamic(this, &ThisClass::HandleBoidDeath);
	Boid->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleBoidEndPlay);
	PublishPopulation();
}

void ASwarmPopulationSpawner::UnregisterBoid(AFlyingBoid* Boid)
{
	if (bEndingPlay || !Population.Remove(Boid)) return;
	if (IsValid(Boid))
	{
		Boid->OnDeath.RemoveDynamic(this, &ThisClass::HandleBoidDeath);
		Boid->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleBoidEndPlay);
	}
	PublishPopulation();
}

void ASwarmPopulationSpawner::HandleBeginOverlap(UPrimitiveComponent* Component, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (AFlyingBoid* Boid = Cast<AFlyingBoid>(OtherActor); Boid && OtherComponent == Boid->CollisionSphere)
	{
		RegisterBoid(Boid);
	}
}

void ASwarmPopulationSpawner::HandleEndOverlap(UPrimitiveComponent* Component, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex)
{
	if (AFlyingBoid* Boid = Cast<AFlyingBoid>(OtherActor); Boid && OtherComponent == Boid->CollisionSphere)
	{
		UnregisterBoid(Boid);
	}
}

void ASwarmPopulationSpawner::HandleBoidDeath()
{
	const TArray<TWeakObjectPtr<AFlyingBoid>> Entries = Population.Array();
	for (const TWeakObjectPtr<AFlyingBoid>& Entry : Entries)
	{
		AFlyingBoid* Boid = Entry.Get();
		if (!Boid || !Boid->IsAlive())
		{
			if (Boid) UnregisterBoid(Boid);
			else if (Population.Remove(Entry)) PublishPopulation();
		}
	}
}

void ASwarmPopulationSpawner::HandleBoidEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	UnregisterBoid(Cast<AFlyingBoid>(Actor));
}

void ASwarmPopulationSpawner::PublishPopulation()
{
	if (!bInitialized || bEndingPlay) return;
	const int32 Previous = CurrentPopulation;
	CurrentPopulation = Population.Num();
	const int32 PublishedCount = CurrentPopulation;
	const bool bEnteredBelowQuota = ReconcileSpawnState();
	const uint64 PublishedGeneration = SequenceGeneration;
	if (Previous != PublishedCount) OnPopulationChanged.Broadcast(Previous, PublishedCount);
	if (!bEndingPlay && bEnteredBelowQuota && bBelowQuota && PublishedGeneration == SequenceGeneration)
	{
		OnPopulationBelowQuota.Broadcast(CurrentPopulation, PopulationQuota);
	}
}

void ASwarmPopulationSpawner::SetPopulationQuota(int32 NewQuota)
{
	PopulationQuota = FMath::Max(0, NewQuota);
	PublishPopulation();
}

bool ASwarmPopulationSpawner::ReconcileSpawnState()
{
	const bool bWasBelowQuota = bBelowQuota;
	bBelowQuota = CurrentPopulation < PopulationQuota;
	if (!bBelowQuota) CancelSpawnSequence();
	else if (!bWasBelowQuota)
	{
		CancelSpawnSequence();
		if (bTimingValid)
		{
			SpawnState = ESwarmPopulationSpawnState::WaitingInitialDelay;
			ScheduleSpawn(InitialSpawnDelaySeconds);
		}
	}
	return bBelowQuota && !bWasBelowQuota;
}

void ASwarmPopulationSpawner::CancelSpawnSequence()
{
	++SequenceGeneration;
	GetWorldTimerManager().ClearTimer(SpawnTimer);
	SpawnState = ESwarmPopulationSpawnState::AtQuota;
}

void ASwarmPopulationSpawner::ScheduleSpawn(float Delay)
{
	const FTimerDelegate Callback = FTimerDelegate::CreateUObject(this, &ThisClass::HandleSpawnTimer, SequenceGeneration);
	if (Delay == 0.f) SpawnTimer = GetWorldTimerManager().SetTimerForNextTick(Callback);
	else GetWorldTimerManager().SetTimer(SpawnTimer, Callback, Delay, false);
}

void ASwarmPopulationSpawner::HandleSpawnTimer(uint64 ExpectedGeneration)
{
	if (bEndingPlay || ExpectedGeneration != SequenceGeneration || !bBelowQuota || CurrentPopulation >= PopulationQuota) return;
	SpawnState = ESwarmPopulationSpawnState::Spawning;
	SpawnEnemy();
	if (!bEndingPlay && ExpectedGeneration == SequenceGeneration && bBelowQuota
		&& SpawnState == ESwarmPopulationSpawnState::Spawning)
	{
		ScheduleSpawn(FMath::FRandRange(MinSpawnIntervalSeconds, MaxSpawnIntervalSeconds));
	}
}

bool ASwarmPopulationSpawner::CanSpawnAtLocation(const FVector& Location, float Radius, const AFlyingBoid* Defaults) const
{
	FTransform CountingTransform;
	FVector CountingExtent;
	if (!SphereSafeBox(SpawnBounds, Radius, CountingTransform, CountingExtent)
		|| !BoidFlight::InsideBounds(Location, CountingTransform, CountingExtent)) return false;
	const auto* Simulation = GetWorld()->GetSubsystem<USwarmSimulationSubsystem>();
	if (!Simulation || !Simulation->IsInsideSafeBounds(Location, Radius)) return false;

	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(SwarmSpawnPlacement), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Location, FQuat::Identity,
		FCollisionObjectQueryParams::AllObjects, FCollisionShape::MakeSphere(Radius), Query);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!IsValid(Component)) continue;
		const AFlyingBoid* OtherBoid = Cast<AFlyingBoid>(Overlap.GetActor());
		if (OtherBoid && OtherBoid->IsAlive() && Component == OtherBoid->CollisionSphere) return false;
		if (Component->GetCollisionResponseToChannel(Defaults->CollisionSphere->GetCollisionObjectType()) == ECR_Block
			&& Defaults->CollisionSphere->GetCollisionResponseToChannel(Component->GetCollisionObjectType()) == ECR_Block) return false;
	}
	return true;
}

AFlyingBoid* ASwarmPopulationSpawner::SpawnEnemy()
{
	if (!bInitialized || bEndingPlay || bSpawnInProgress || !EnemyClass || !GetWorld()
		|| CurrentPopulation >= PopulationQuota || MaxPlacementAttempts <= 0) return nullptr;
	TGuardValue<bool> SpawnGuard(bSpawnInProgress, true);
	const AFlyingBoid* Defaults = EnemyClass->GetDefaultObject<AFlyingBoid>();
	if (!IsValid(Defaults->CollisionSphere)) return nullptr;
	const float Radius = Defaults->CollisionSphere->GetScaledSphereRadius();

	TArray<ASwarmSpawnVolume*> ValidVolumes;
	for (ASwarmSpawnVolume* Volume : SpawnVolumes)
	{
		FTransform Transform;
		FVector Extent;
		if (IsValid(Volume) && !Volume->IsActorBeingDestroyed() && Volume->GetWorld() == GetWorld()
			&& SphereSafeBox(Volume->SpawnBox, Radius, Transform, Extent)) ValidVolumes.AddUnique(Volume);
	}
	if (ValidVolumes.IsEmpty()) return nullptr;
	const ASwarmSpawnVolume* Volume = ValidVolumes[FMath::RandRange(0, ValidVolumes.Num() - 1)];
	FTransform Transform;
	FVector Extent;
	if (!SphereSafeBox(Volume->SpawnBox, Radius, Transform, Extent)) return nullptr;
	for (int32 Attempt = 0; Attempt < MaxPlacementAttempts; ++Attempt)
	{
		const FVector Local(FMath::FRandRange(-Extent.X, Extent.X), FMath::FRandRange(-Extent.Y, Extent.Y), FMath::FRandRange(-Extent.Z, Extent.Z));
		const FVector Location = Transform.TransformPositionNoScale(Local);
		if (!CanSpawnAtLocation(Location, Radius, Defaults)) continue;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
		AFlyingBoid* Boid = GetWorld()->SpawnActor<AFlyingBoid>(EnemyClass, Location, Volume->SpawnBox->GetComponentRotation(), Params);
		if (IsValid(Boid) && Boid->IsAlive())
		{
			Boid->CollisionSphere->UpdateOverlaps();
			return Boid;
		}
		return nullptr;
	}
	return nullptr;
}
