#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Swarm/Spawner/SwarmPopulationSpawner.h"
#include "Swarm/Spawner/SwarmSpawnVolume.h"
#include "Swarm/Simulation/SwarmSimulationBounds.h"
#include "Swarm/Simulation/SwarmSimulationSettings.h"
#include "Swarm/FlyingBoid.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "TimerManager.h"

namespace
{
	/** Real game-world overlaps and timers, without running AI or advancing boid movement. */
	struct FSpawnerTestWorld
	{
		UWorld* World;

		FSpawnerTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->SpawnActor<ASwarmSimulationBounds>();
		}

		~FSpawnerTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void Start()
		{
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
			World->GetTimerManager().Tick(0.f);
		}

		void Advance(float Seconds)
		{
			// FTimerManager is frame-guarded. No world tick means no simulation movement.
			++GFrameCounter;
			World->GetTimerManager().Tick(Seconds);
		}

		ASwarmPopulationSpawner* Spawner(int32 Quota = 3, float Delay = 1.f)
		{
			ASwarmSpawnVolume* Volume = World->SpawnActor<ASwarmSpawnVolume>();
			auto* Result = World->SpawnActorDeferred<ASwarmPopulationSpawner>(ASwarmPopulationSpawner::StaticClass(),
				FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			Result->EnemyClass = AFlyingBoid::StaticClass();
			Result->SpawnVolumes.Add(Volume);
			Result->PopulationQuota = Quota;
			Result->InitialSpawnDelaySeconds = Delay;
			Result->MinSpawnIntervalSeconds = .25f;
			Result->MaxSpawnIntervalSeconds = .25f;
			Result->FinishSpawning(FTransform::Identity);
			return Result;
		}

		AFlyingBoid* Boid(FVector Location = FVector::ZeroVector)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<AFlyingBoid>(AFlyingBoid::StaticClass(), Location, FRotator::ZeroRotator, Params);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmPopulationLifecycleTest, "FlyingSwarm.Spawner.PopulationLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmPopulationLifecycleTest::RunTest(const FString& Parameters)
{
	FSpawnerTestWorld Fixture;
	AFlyingBoid* Existing = Fixture.Boid();
	ASwarmPopulationSpawner* Sensor = Fixture.Spawner();
	Fixture.Start();
	TestEqual(TEXT("Initial placed boid is seeded"), Sensor->CurrentPopulation, 1);
	ASwarmPopulationSpawner* OtherSensor = Fixture.Spawner();
	TestEqual(TEXT("Runtime sensor seeds existing boids"), OtherSensor->CurrentPopulation, 1);
	Existing->SetActorLocation(FVector(520.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	TestEqual(TEXT("Body still overlaps when center is outside"), Sensor->CurrentPopulation, 1);
	Existing->SetActorLocation(FVector(1000.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	TestEqual(TEXT("Teleport exit removes member immediately"), Sensor->CurrentPopulation, 0);
	TestEqual(TEXT("Other sensor sees exit"), OtherSensor->CurrentPopulation, 0);
	Existing->SetActorLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
	Existing->CollisionSphere->UpdateOverlaps();
	TestEqual(TEXT("Reentry and duplicate updates count once"), Sensor->CurrentPopulation, 1);

	AFlyingBoid* Spawned = Fixture.Boid(FVector(100.f, 0.f, 0.f));
	TestEqual(TEXT("External spawn counts immediately"), Sensor->CurrentPopulation, 2);
	Spawned->TakeDamage(100.f, FDamageEvent(), nullptr, nullptr);
	TestEqual(TEXT("Death removes before corpse destruction"), Sensor->CurrentPopulation, 1);
	TestEqual(TEXT("Death updates every sensor"), OtherSensor->CurrentPopulation, 1);
	Spawned->Destroy();
	TestEqual(TEXT("Death followed by destruction is idempotent"), Sensor->CurrentPopulation, 1);
	Existing->Destroy();
	TestEqual(TEXT("Direct destruction removes member"), Sensor->CurrentPopulation, 0);

	AFlyingBoid* Removed = Fixture.Boid();
	Removed->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
	TestEqual(TEXT("Level removal removes member"), Sensor->CurrentPopulation, 0);
	OtherSensor->Destroy();
	Fixture.Boid();
	TestEqual(TEXT("Destroying one sensor does not affect the other"), Sensor->CurrentPopulation, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmPopulationSchedulingTest, "FlyingSwarm.Spawner.Scheduling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmPopulationSchedulingTest::RunTest(const FString& Parameters)
{
	FSpawnerTestWorld Fixture;
	ASwarmPopulationSpawner* Sensor = Fixture.Spawner();
	Fixture.Start();
	TestEqual(TEXT("Initially under quota starts delay"), Sensor->SpawnState, ESwarmPopulationSpawnState::WaitingInitialDelay);
	Fixture.Advance(.6f);
	AFlyingBoid* First = Fixture.Boid();
	AFlyingBoid* Second = Fixture.Boid(FVector(100.f, 0.f, 0.f));
	First->Destroy();
	Fixture.Advance(.41f);
	TestEqual(TEXT("Additional losses do not postpone initial spawn"), Sensor->CurrentPopulation, 2);
	TestEqual(TEXT("Initial delay enters spawning"), Sensor->SpawnState, ESwarmPopulationSpawnState::Spawning);
	Fixture.Advance(.24f);
	TestEqual(TEXT("No early recurring spawn"), Sensor->CurrentPopulation, 2);
	Fixture.Advance(.02f);
	TestEqual(TEXT("Exactly one recurring spawn reaches quota"), Sensor->CurrentPopulation, 3);
	TestEqual(TEXT("Quota cancels spawning"), Sensor->SpawnState, ESwarmPopulationSpawnState::AtQuota);
	Fixture.Advance(2.f);
	TestEqual(TEXT("No spawning at quota"), Sensor->CurrentPopulation, 3);

	Second->Destroy();
	Fixture.Advance(.6f);
	AFlyingBoid* Visitor = Fixture.Boid(FVector(100.f, 0.f, 0.f));
	TestEqual(TEXT("Entry cancels pending initial delay"), Sensor->SpawnState, ESwarmPopulationSpawnState::AtQuota);
	Visitor->Destroy(); // Reach and leave quota in the same frame.
	Fixture.Advance(.41f);
	TestEqual(TEXT("Old delay cannot spawn after a same-frame restart"), Sensor->CurrentPopulation, 2);
	Fixture.Advance(.6f);
	TestEqual(TEXT("Full fresh delay expires"), Sensor->CurrentPopulation, 3);

	Sensor->SetPopulationQuota(0);
	TestNull(TEXT("Manual spawning also respects quota zero"), Sensor->SpawnEnemy());
	TestEqual(TEXT("Lower quota never despawns members"), Sensor->CurrentPopulation, 3);
	Sensor->Destroy();
	Fixture.Advance(5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmPopulationPlacementTest, "FlyingSwarm.Spawner.PlacementAndRetries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmPopulationPlacementTest::RunTest(const FString& Parameters)
{
	FSpawnerTestWorld Fixture;
	ASwarmPopulationSpawner* Sensor = Fixture.Spawner(50, 0.f);
	ASwarmSpawnVolume* Volume = Sensor->SpawnVolumes[0];
	Sensor->SpawnVolumes.Add(Volume); // Duplicate entries must not change selection weighting.
	Sensor->SpawnVolumes.Add(nullptr);
	Sensor->SpawnBounds->SetBoxExtent(FVector(800.f));
	Sensor->SetActorRotation(FRotator(0.f, 35.f, 0.f));
	Volume->SetActorTransform(FTransform(FRotator(20.f, 60.f, 15.f), FVector::ZeroVector, FVector(1.5f, .7f, 1.2f)));
	Fixture.Start();
	TestEqual(TEXT("Zero delay does not spawn synchronously"), Sensor->CurrentPopulation, 0);
	Fixture.Advance(.01f);
	TestEqual(TEXT("Zero delay spawns on next tick despite sensor overlaps"), Sensor->CurrentPopulation, 1);
	for (int32 Index = 0; Index < 12; ++Index)
	{
		AFlyingBoid* Boid = Sensor->SpawnEnemy();
		if (!Boid) continue; // Crowding may exhaust a bounded placement attempt.
		const float Radius = Boid->CollisionSphere->GetScaledSphereRadius();
		const FVector Local = Volume->SpawnBox->GetComponentTransform().InverseTransformPositionNoScale(Boid->GetActorLocation());
		const FVector Extent = Volume->SpawnBox->GetScaledBoxExtent() - FVector(Radius);
		TestTrue(TEXT("Sampled body fits the scaled and rotated spawn volume"),
			FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z);
	}
	Volume->SetActorLocation(FVector(5000.f, 0.f, 0.f));
	const int32 BeforeFailure = Sensor->CurrentPopulation;
	Fixture.Advance(.26f);
	TestEqual(TEXT("Out-of-bounds volume creates no boid"), Sensor->CurrentPopulation, BeforeFailure);
	TestEqual(TEXT("Failed placement remains in spawning state"), Sensor->SpawnState, ESwarmPopulationSpawnState::Spawning);
	Volume->SpawnBox->SetBoxExtent(FVector(10.f));
	Volume->SetActorLocation(FVector::ZeroVector);
	TestNull(TEXT("Undersized volume fails safely"), Sensor->SpawnEnemy());
	Volume->SpawnBox->SetBoxExtent(FVector(200.f));
	Fixture.Advance(.26f);
	TestEqual(TEXT("A later interval recovers after fixing volume"), Sensor->CurrentPopulation, BeforeFailure + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmPopulationRetryTimingTest, "FlyingSwarm.Spawner.BlockedPlacementAndRandomIntervals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmPopulationRetryTimingTest::RunTest(const FString& Parameters)
{
	FSpawnerTestWorld Fixture;
	ASwarmPopulationSpawner* Sensor = Fixture.Spawner(3, .1f);
	Sensor->MinSpawnIntervalSeconds = .2f;
	Sensor->MaxSpawnIntervalSeconds = .4f;
	ASwarmSpawnVolume* Volume = Sensor->SpawnVolumes[0];
	Volume->SpawnBox->SetBoxExtent(FVector(31.f));
	Fixture.Start();
	AFlyingBoid* Occupant = Sensor->SpawnEnemy();
	if (!TestNotNull(TEXT("A body fits in an almost exact-sized spawn box"), Occupant)) return false;
	Fixture.Advance(.11f);
	TestEqual(TEXT("Occupied volume cannot spawn overlapping boids"), Sensor->CurrentPopulation, 1);
	TestEqual(TEXT("A blocked attempt continues the spawn sequence"), Sensor->SpawnState, ESwarmPopulationSpawnState::Spawning);
	Occupant->Destroy();
	Volume->SpawnBox->SetBoxExtent(FVector(200.f));
	Fixture.Advance(.19f);
	TestEqual(TEXT("Retry never occurs before the configured minimum"), Sensor->CurrentPopulation, 0);
	Fixture.Advance(.22f);
	TestEqual(TEXT("Retry occurs by the maximum and creates only one boid"), Sensor->CurrentPopulation, 1);
	Sensor->SpawnVolumes.Empty();
	Fixture.Advance(.41f);
	TestEqual(TEXT("Empty volume list has no center fallback"), Sensor->CurrentPopulation, 1);
	Sensor->SpawnVolumes.Add(Volume);
	Volume->Destroy();
	TestNull(TEXT("Destroyed volume references are ignored"), Sensor->SpawnEnemy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmPopulationGlobalCapTest, "FlyingSwarm.Spawner.GlobalPopulationCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmPopulationGlobalCapTest::RunTest(const FString& Parameters)
{
	TGuardValue<int32> CapGuard(GetMutableDefault<USwarmSimulationSettings>()->MaxPopulation, 1);
	FSpawnerTestWorld Fixture;
	ASwarmPopulationSpawner* Sensor = Fixture.Spawner(3);
	Fixture.Start();
	TestNotNull(TEXT("Spawn below the global cap succeeds"), Sensor->SpawnEnemy());
	TestNull(TEXT("Global cap blocks spawning before the local quota is reached"), Sensor->SpawnEnemy());
	return true;
}

#endif
