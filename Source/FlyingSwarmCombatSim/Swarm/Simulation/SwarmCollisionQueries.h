#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"

class AFlyingBoid;
class UPrimitiveComponent;
class UWorld;

/** Compatibility exclusions for extra swarm colliders. Native bodies need no pairwise ignores. */
class FSwarmCollisionQueries
{
public:
	~FSwarmCollisionQueries();
	void Initialize(UWorld* World);
	void Refresh();
	void RefreshBoid(AFlyingBoid* Boid);
	void RegisterMovementBody(AFlyingBoid* Boid);
	void UnregisterMovementBody(AFlyingBoid* Boid);
	void RegisterProjectile(UPrimitiveComponent* Component);
	void UnregisterProjectile(UPrimitiveComponent* Component);
	void AddBodyExceptions(FCollisionQueryParams& Params) const;
	void AddProjectileExceptions(FCollisionQueryParams& Params) const;
	void Reset();
	int32 GetProjectileCount() const { return Projectiles.Num(); }
	int32 GetExceptionCount() const { return ProjectileExceptions.Num(); }

private:
	void SetException(TSet<TWeakObjectPtr<AFlyingBoid>>& Exceptions, AFlyingBoid* Boid, bool bNeeded, bool bProjectile);
	TWeakObjectPtr<UWorld> World;
	FDelegateHandle SpawnHandle;
	uint64 RefreshFrame = MAX_uint64;
	TSet<TWeakObjectPtr<AFlyingBoid>> MovementBodies;
	TSet<TWeakObjectPtr<AFlyingBoid>> BodyExceptions;
	TSet<TWeakObjectPtr<AFlyingBoid>> ProjectileExceptions;
	TSet<TWeakObjectPtr<UPrimitiveComponent>> Projectiles;
};
