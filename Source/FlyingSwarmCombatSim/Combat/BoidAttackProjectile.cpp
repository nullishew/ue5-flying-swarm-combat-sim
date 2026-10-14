// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/BoidAttackProjectile.h"

#include "Components/SphereComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Player/FlyingCharacter.h"
#include "Swarm/FlyingBoid.h"

AFlyingBoidAttackProjectile::AFlyingBoidAttackProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitSphereRadius(ProjectileRadius);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionComponent->SetCollisionObjectType(ECC_GameTraceChannel1);
	ConfigureCollisionResponses();
	CollisionComponent->SetNotifyRigidBodyCollision(true);
	RootComponent = CollisionComponent;

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionComponent;
	ProjectileMovement->InitialSpeed = ProjectileSpeed;
	ProjectileMovement->MaxSpeed = ProjectileSpeed;
	ProjectileMovement->ProjectileGravityScale = 0.0f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->bInitialVelocityInLocalSpace = false;
	ProjectileMovement->bAutoActivate = false;
	ProjectileMovement->Deactivate();
}

void AFlyingBoidAttackProjectile::BeginPlay()
{
	Super::BeginPlay();
	bHasBegunPlay = true;
	SendLaunchPresentationIfReady();
}

void AFlyingBoidAttackProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld(); World && ActorSpawnedDelegateHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedDelegateHandle);
		ActorSpawnedDelegateHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

bool AFlyingBoidAttackProjectile::InitializeLaunch(AFlyingBoid* InShooter, AFlyingCharacter* InIntendedTarget,
	const FVector& InLaunchDirection, float InSpeed, float InRadius, float InDamage, float InLifespan)
{
	if (bLaunchInitialized || !IsValid(InShooter) || !InShooter->IsAlive()
		|| !IsValid(InIntendedTarget) || !InIntendedTarget->IsAlive()
		|| !FMath::IsFinite(InLaunchDirection.X) || !FMath::IsFinite(InLaunchDirection.Y)
		|| !FMath::IsFinite(InLaunchDirection.Z) || InLaunchDirection.IsNearlyZero()
		|| !FMath::IsFinite(InSpeed) || InSpeed <= 0.0f
		|| !FMath::IsFinite(InRadius) || InRadius <= 0.0f
		|| !FMath::IsFinite(InDamage) || InDamage < 0.0f
		|| !FMath::IsFinite(InLifespan) || InLifespan <= 0.0f
		|| !CollisionComponent || !ProjectileMovement)
	{
		return false;
	}

	bLaunchInitialized = true;
	Shooter = InShooter;
	IntendedTarget = InIntendedTarget;
	LaunchSpeed = InSpeed;
	LaunchRadius = InRadius;
	LaunchDamage = InDamage;
	LaunchLifespan = InLifespan;
	this->LaunchDirection = InLaunchDirection.GetSafeNormal();
	LaunchOrigin = GetActorLocation();
	SetActorScale3D(FVector::OneVector);
	SetActorRotation(this->LaunchDirection.Rotation());
	SetOwner(InShooter);
	SetInstigator(InShooter->GetInstigator());

	CollisionComponent->SetSphereRadius(LaunchRadius);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionComponent->SetCollisionObjectType(ECC_GameTraceChannel1);
	ConfigureCollisionResponses();
	CollisionComponent->IgnoreActorWhenMoving(InShooter, true);
	RegisterExistingBoidsToIgnore();
	if (UWorld* World = GetWorld(); World && !ActorSpawnedDelegateHandle.IsValid())
	{
		ActorSpawnedDelegateHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(this, &AFlyingBoidAttackProjectile::HandleActorSpawned));
	}
	ProjectileMovement->SetUpdatedComponent(CollisionComponent);
	ProjectileMovement->InitialSpeed = LaunchSpeed;
	ProjectileMovement->MaxSpeed = LaunchSpeed;
	ProjectileMovement->Velocity = this->LaunchDirection * LaunchSpeed;
	CollisionComponent->OnComponentHit.AddDynamic(this, &AFlyingBoidAttackProjectile::HandleBlockingHit);
	// Deferred construction must finish before collision or movement is enabled.
	SendLaunchPresentationIfReady();
	return true;
}

void AFlyingBoidAttackProjectile::SendLaunchPresentationIfReady()
{
	if (!IsValid(this) || !bHasBegunPlay || !bLaunchInitialized || bLaunchPresentationSent || bResolved)
	{
		return;
	}
	// Blueprint construction/BeginPlay may change component defaults. Restore native
	// geometry before publishing the authoritative launch to presentation.
	SetRootComponent(CollisionComponent);
	SetActorScale3D(FVector::OneVector);
	SetActorLocation(LaunchOrigin, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(LaunchDirection.Rotation());
	CollisionComponent->SetSphereRadius(LaunchRadius);
	CollisionComponent->SetCollisionObjectType(ECC_GameTraceChannel1);
	ConfigureCollisionResponses();
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	bLaunchPresentationSent = true;
	BP_OnProjectileLaunched();

	// Keep the Blueprint hook presentation-only even if a child implementation changes defaults.
	if (!bResolved && IsValid(this) && CollisionComponent && ProjectileMovement)
	{
		CollisionComponent->SetSphereRadius(LaunchRadius);
		CollisionComponent->SetCollisionObjectType(ECC_GameTraceChannel1);
		ConfigureCollisionResponses();
		CollisionComponent->IgnoreActorWhenMoving(Shooter.Get(), true);
		RegisterExistingBoidsToIgnore();
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		SetActorScale3D(FVector::OneVector);
		SetActorLocation(LaunchOrigin, false, nullptr, ETeleportType::TeleportPhysics);
		SetActorRotation(LaunchDirection.Rotation());
		ProjectileMovement->ProjectileGravityScale = 0.0f;
		ProjectileMovement->bShouldBounce = false;
		ProjectileMovement->bIsHomingProjectile = false;
		ProjectileMovement->bInitialVelocityInLocalSpace = false;
		ProjectileMovement->bRotationFollowsVelocity = true;
		ProjectileMovement->InitialSpeed = LaunchSpeed;
		ProjectileMovement->MaxSpeed = LaunchSpeed;
		ProjectileMovement->Velocity = LaunchDirection * LaunchSpeed;
		ProjectileMovement->SetUpdatedComponent(CollisionComponent);
		ProjectileMovement->Activate(true);
		SetLifeSpan(LaunchLifespan);
	}
}

void AFlyingBoidAttackProjectile::HandleBlockingHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	if (OtherActor == Shooter.Get())
	{
		return;
	}
	ResolveImpact(Hit, OtherActor == IntendedTarget.Get());
}

void AFlyingBoidAttackProjectile::RegisterExistingBoidsToIgnore()
{
	if (!CollisionComponent || !GetWorld()) return;
	for (TActorIterator<AFlyingBoid> It(GetWorld()); It; ++It)
	{
		if (*It && *It != Shooter.Get()) CollisionComponent->IgnoreActorWhenMoving(*It, true);
	}
}

void AFlyingBoidAttackProjectile::HandleActorSpawned(AActor* SpawnedActor)
{
	if (AFlyingBoid* Boid = Cast<AFlyingBoid>(SpawnedActor); Boid && Boid != Shooter.Get() && CollisionComponent)
	{
		CollisionComponent->IgnoreActorWhenMoving(Boid, true);
	}
}

void AFlyingBoidAttackProjectile::ResolveImpact(const FHitResult& Hit, bool bMayDamageIntendedTarget)
{
	if (bResolved)
	{
		return;
	}
	bResolved = true;

	if (CollisionComponent)
	{
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (ProjectileMovement)
	{
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}

	bool bDamagedIntendedTarget = false;
	AFlyingCharacter* Target = IntendedTarget.Get();
	if (bMayDamageIntendedTarget && IsValid(Target) && Target->IsAlive()
		&& (!Hit.GetActor() || Hit.GetActor() == Target) && LaunchDamage > 0.0f)
	{
		// ApplyDamage dispatches through AActor's public virtual damage interface,
		// including the player's protected override and existing health guards.
		const float AppliedDamage = UGameplayStatics::ApplyDamage(Target, LaunchDamage,
			IsValid(Shooter.Get()) ? Shooter->GetInstigatorController() : nullptr, this, nullptr);
		bDamagedIntendedTarget = FMath::IsFinite(AppliedDamage) && AppliedDamage > 0.0f;
	}

	if (IsValid(this))
	{
		BP_OnProjectileResolved(Hit, bDamagedIntendedTarget);
		Destroy();
	}
}

void AFlyingBoidAttackProjectile::ConfigureCollisionResponses()
{
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	// A projectile can hit bodies and cover, but must not occlude sensing,
	// ranged-position selection, readiness, or camera/aim traces.
	CollisionComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// Fan bullets share a muzzle and must never collide with one another.
	CollisionComponent->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
}
