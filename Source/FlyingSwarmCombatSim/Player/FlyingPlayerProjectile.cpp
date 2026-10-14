// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/FlyingPlayerProjectile.h"

#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"

AFlyingPlayerProjectile::AFlyingPlayerProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitSphereRadius(8.0f);
	CollisionComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	CollisionComponent->SetNotifyRigidBodyCollision(true);
	RootComponent = CollisionComponent;

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionComponent;
	ProjectileMovement->InitialSpeed = ProjectileSpeed;
	ProjectileMovement->MaxSpeed = ProjectileSpeed;
	ProjectileMovement->ProjectileGravityScale = 0.0f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;

	InitialLifeSpan = ProjectileLifespan;
}

void AFlyingPlayerProjectile::BeginPlay()
{
	Super::BeginPlay();

	ProjectileMovement->InitialSpeed = ProjectileSpeed;
	ProjectileMovement->MaxSpeed = ProjectileSpeed;
	ProjectileMovement->Velocity = GetActorForwardVector() * ProjectileSpeed;
	SetLifeSpan(ProjectileLifespan);
	CollisionComponent->OnComponentHit.AddDynamic(this, &AFlyingPlayerProjectile::HandleImpact);

	if (AActor* ProjectileOwner = GetOwner())
	{
		CollisionComponent->IgnoreActorWhenMoving(ProjectileOwner, true);
	}
}

void AFlyingPlayerProjectile::HandleImpact(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (!IsValid(OtherActor) || OtherActor == GetOwner())
	{
		return;
	}

	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileMovement->StopMovementImmediately();
	BP_OnProjectileImpact(Hit);
	Destroy();
}
