// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoidAttackProjectile.generated.h"

class AFlyingBoid;
class AFlyingCharacter;
class UProjectileMovementComponent;
class USphereComponent;
class UPrimitiveComponent;
class UWorld;

/** Straight-flight projectile launched by a boid. Damage is restricted to its intended player. */
UCLASS()
class FLYINGSWARMCOMBATSIM_API AFlyingBoidAttackProjectile : public AActor
{
	GENERATED_BODY()

public:
	AFlyingBoidAttackProjectile();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<USphereComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "1.0", Units = "cm/s"))
	float ProjectileSpeed = 1800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "0.1", Units = "cm"))
	float ProjectileRadius = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "0.0"))
	float DamageAmount = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "0.1", Units = "s"))
	float ProjectileLifespan = 5.0f;

	/** Configure and start after spawning at the validated muzzle transform. */
	UFUNCTION(BlueprintCallable, Category = "Projectile")
	bool InitializeLaunch(AFlyingBoid* Shooter, AFlyingCharacter* IntendedTarget, const FVector& LaunchDirection,
		float InSpeed, float InRadius, float InDamage, float InLifespan);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Projectile", meta = (DisplayName = "On Projectile Launched"))
	void BP_OnProjectileLaunched();

	UFUNCTION(BlueprintImplementableEvent, Category = "Projectile", meta = (DisplayName = "On Projectile Resolved"))
	void BP_OnProjectileResolved(const FHitResult& Hit, bool bDamagedIntendedTarget);

private:
	UFUNCTION()
	void HandleBlockingHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse, const FHitResult& Hit);

	void HandleActorSpawned(AActor* SpawnedActor);
	void RegisterExistingBoidsToIgnore();

	void ResolveImpact(const FHitResult& Hit, bool bMayDamageIntendedTarget);

	TWeakObjectPtr<AFlyingBoid> Shooter;
	TWeakObjectPtr<AFlyingCharacter> IntendedTarget;
	bool bLaunchInitialized = false;
	bool bResolved = false;
	bool bHasBegunPlay = false;
	bool bLaunchPresentationSent = false;
	float LaunchSpeed = 0.0f;
	float LaunchRadius = 0.0f;
	float LaunchDamage = 0.0f;
	float LaunchLifespan = 0.0f;
	FVector LaunchDirection = FVector::ForwardVector;
	FVector LaunchOrigin = FVector::ZeroVector;
	FDelegateHandle ActorSpawnedDelegateHandle;

	void SendLaunchPresentationIfReady();
	void ConfigureCollisionResponses();
};
