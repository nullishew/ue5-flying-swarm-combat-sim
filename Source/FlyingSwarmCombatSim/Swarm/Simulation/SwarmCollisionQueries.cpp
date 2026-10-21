#include "Swarm/Simulation/SwarmCollisionQueries.h"

#include "Swarm/FlyingBoid.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

FSwarmCollisionQueries::~FSwarmCollisionQueries() { Reset(); }

void FSwarmCollisionQueries::Initialize(UWorld* InWorld)
{
	Reset();
	World = InWorld;
	if (!InWorld) return;
	SpawnHandle = InWorld->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
	{
		if (AFlyingBoid* Boid = Cast<AFlyingBoid>(Actor)) RefreshBoid(Boid);
	}));
	Refresh();
}

void FSwarmCollisionQueries::SetException(TSet<TWeakObjectPtr<AFlyingBoid>>& Exceptions, AFlyingBoid* Boid, bool bNeeded, bool bProjectile)
{
	if (Exceptions.Contains(Boid) == bNeeded) return;
	if (bNeeded) Exceptions.Add(Boid);
	else Exceptions.Remove(Boid);
	if (bProjectile)
	{
		for (const auto& Weak : Projectiles) if (UPrimitiveComponent* Component = Weak.Get()) Component->IgnoreActorWhenMoving(Boid, bNeeded);
	}
	else
	{
		for (const auto& Weak : MovementBodies) if (AFlyingBoid* Other = Weak.Get(); Other && Other != Boid) Other->CollisionSphere->IgnoreActorWhenMoving(Boid, bNeeded);
	}
}

void FSwarmCollisionQueries::RefreshBoid(AFlyingBoid* Boid)
{
	if (!IsValid(Boid) || Boid->GetWorld() != World.Get()) return;
	// Include every query-enabled extra collider, regardless of object type or Blueprint response overrides.
	bool bBodyException = false;
	bool bProjectileException = false;
	TInlineComponentArray<UPrimitiveComponent*> Components;
	Boid->GetComponents(Components);
	for (const UPrimitiveComponent* Component : Components)
	{
		if (!IsValid(Component) || !Component->IsQueryCollisionEnabled()) continue;
		const bool bNativeBody = Component == Boid->CollisionSphere && Component->GetCollisionObjectType() == ECC_Pawn;
		bBodyException |= !bNativeBody || Component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Ignore;
		bProjectileException |= !bNativeBody || Component->GetCollisionResponseToChannel(ECC_GameTraceChannel1) != ECR_Ignore;
	}
	SetException(BodyExceptions, Boid, bBodyException && MovementBodies.Contains(Boid), false);
	SetException(ProjectileExceptions, Boid, bProjectileException, true);
}

void FSwarmCollisionQueries::Refresh()
{
	if (!World.IsValid() || RefreshFrame == GFrameCounter) return;
	TRACE_CPUPROFILER_EVENT_SCOPE(SwarmCollisionFilterRefresh);
	RefreshFrame = GFrameCounter;
	for (auto It = MovementBodies.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
	for (auto It = BodyExceptions.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
	for (auto It = ProjectileExceptions.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
	for (auto It = Projectiles.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
	for (TActorIterator<AFlyingBoid> It(World.Get()); It; ++It) RefreshBoid(*It);
}

void FSwarmCollisionQueries::RegisterMovementBody(AFlyingBoid* Boid)
{
	if (!IsValid(Boid) || !World.IsValid() || Boid->GetWorld() != World.Get()) return;
	MovementBodies.Add(Boid);
	for (const auto& Weak : BodyExceptions) if (AFlyingBoid* Other = Weak.Get(); Other && Other != Boid) Boid->CollisionSphere->IgnoreActorWhenMoving(Other, true);
	RefreshBoid(Boid);
}

void FSwarmCollisionQueries::UnregisterMovementBody(AFlyingBoid* Boid)
{
	MovementBodies.Remove(Boid);
	for (const auto& Weak : BodyExceptions) if (AFlyingBoid* Other = Weak.Get(); Other && Other != Boid) Boid->CollisionSphere->IgnoreActorWhenMoving(Other, false);
	SetException(BodyExceptions, Boid, false, false);
	RefreshBoid(Boid); // Corpse extras remain excluded from projectile flight until their collision is disabled.
}

void FSwarmCollisionQueries::RegisterProjectile(UPrimitiveComponent* Component)
{
	if (!IsValid(Component) || !World.IsValid() || Component->GetWorld() != World.Get()) return;
	Projectiles.Add(Component);
	for (const auto& Weak : ProjectileExceptions) if (AFlyingBoid* Boid = Weak.Get()) Component->IgnoreActorWhenMoving(Boid, true);
}

void FSwarmCollisionQueries::UnregisterProjectile(UPrimitiveComponent* Component) { Projectiles.Remove(Component); }

void FSwarmCollisionQueries::AddBodyExceptions(FCollisionQueryParams& Params) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SwarmBodyQueryPreparation);
	for (const auto& Weak : BodyExceptions) if (AFlyingBoid* Boid = Weak.Get()) Params.AddIgnoredActor(Boid);
}

void FSwarmCollisionQueries::AddProjectileExceptions(FCollisionQueryParams& Params) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SwarmProjectileQueryPreparation);
	for (const auto& Weak : ProjectileExceptions) if (AFlyingBoid* Boid = Weak.Get()) Params.AddIgnoredActor(Boid);
}

void FSwarmCollisionQueries::Reset()
{
	if (UWorld* CurrentWorld = World.Get(); CurrentWorld && SpawnHandle.IsValid()) CurrentWorld->RemoveOnActorSpawnedHandler(SpawnHandle);
	SpawnHandle.Reset();
	for (const auto& Weak : MovementBodies) if (AFlyingBoid* Boid = Weak.Get()) for (const auto& Other : BodyExceptions) if (Other.IsValid() && Other.Get() != Boid) Boid->CollisionSphere->IgnoreActorWhenMoving(Other.Get(), false);
	for (const auto& Weak : Projectiles) if (UPrimitiveComponent* Component = Weak.Get()) for (const auto& Other : ProjectileExceptions) if (Other.IsValid()) Component->IgnoreActorWhenMoving(Other.Get(), false);
	MovementBodies.Reset();
	BodyExceptions.Reset();
	ProjectileExceptions.Reset();
	Projectiles.Reset();
	World.Reset();
	RefreshFrame = MAX_uint64;
}
