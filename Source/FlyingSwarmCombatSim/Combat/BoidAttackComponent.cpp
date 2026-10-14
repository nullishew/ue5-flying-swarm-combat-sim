#include "Combat/BoidAttackComponent.h"

#include "Combat/BoidAttackProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "Player/FlyingCharacter.h"
#include "Swarm/BoidIntentTypes.h"
#include "Swarm/FlyingBoid.h"
#include "Swarm/SwarmSimulationRunner.h"

UBoidAttackComponent::UBoidAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UBoidAttackComponent::BeginPlay()
{
	Super::BeginPlay();
	AFlyingBoid* OwnerBoid = Cast<AFlyingBoid>(GetOwner());
	USceneComponent* ResolvedMuzzle = MuzzleReference.OtherActor.IsValid() ? nullptr : Cast<USceneComponent>(MuzzleReference.GetComponent(OwnerBoid));
	if (!ResolvedMuzzle && MuzzleReference.ComponentProperty.IsNone() && MuzzleReference.PathToComponent.IsEmpty() && !MuzzleReference.OverrideComponent.IsValid() && !MuzzleReference.OtherActor.IsValid()
		&& OwnerBoid) ResolvedMuzzle = OwnerBoid->CollisionSphere;

	SnapshottedProjectileClass = ProjectileClass;
	const float RequestedInterval = AttackIntervalSeconds;
	SnapshottedInterval = AttackIntervalSeconds;
	if (!FMath::IsFinite(SnapshottedInterval) || SnapshottedInterval <= 0.f) SnapshottedInterval = 0.75f;
	SnapshottedApproachDistance = ApproachAttackDistanceCm;
	SnapshottedCloseDistance = CloseAttackDistanceCm;
	SnapshottedClearance = LaunchClearanceDistanceCm;
	SnapshottedMuzzleComponent = ResolvedMuzzle;
	SnapshottedMuzzleSocketName = MuzzleSocketName;
	bConfigurationValid = SnapshottedProjectileClass
		&& FMath::IsFinite(RequestedInterval) && RequestedInterval > 0.f
		&& FMath::IsFinite(SnapshottedApproachDistance) && SnapshottedApproachDistance >= 0.f
		&& FMath::IsFinite(SnapshottedCloseDistance) && SnapshottedCloseDistance >= 0.f
		&& FMath::IsFinite(SnapshottedClearance) && SnapshottedClearance > 0.f
		&& IsValid(ResolvedMuzzle) && ResolvedMuzzle->GetOwner() == OwnerBoid;
	if (bConfigurationValid)
	{
		const AFlyingBoidAttackProjectile* Defaults = SnapshottedProjectileClass->GetDefaultObject<AFlyingBoidAttackProjectile>();
		const UProjectileMovementComponent* Movement = Defaults ? Defaults->FindComponentByClass<UProjectileMovementComponent>() : nullptr;
		const float Speed = Defaults ? Defaults->ProjectileSpeed : 0.f;
		SnapshottedProjectileRadius = Defaults ? Defaults->ProjectileRadius : 0.f;
		SnapshottedProjectileSpeed = Speed;
		SnapshottedProjectileDamage = Defaults ? Defaults->DamageAmount : 0.f;
		SnapshottedProjectileLifespan = Defaults ? Defaults->ProjectileLifespan : 0.f;
		bConfigurationValid = Defaults && Movement && FMath::IsFinite(Speed) && Speed > 0.f
			&& FMath::IsFinite(SnapshottedProjectileRadius) && SnapshottedProjectileRadius > 0.f
			&& FMath::IsFinite(SnapshottedProjectileDamage) && SnapshottedProjectileDamage >= 0.f
			&& FMath::IsFinite(SnapshottedProjectileLifespan) && SnapshottedProjectileLifespan > 0.f;
	}
	AttackFacts = FBoidAttackFacts();
	AttackFacts.LastRejection = bConfigurationValid ? EBoidAttackRejection::ContextInactive : EBoidAttackRejection::InvalidConfiguration;
	// Reapply after saved Blueprint defaults. The Combat task enables firing.
	RefreshTickPrerequisites();
	SetComponentTickEnabled(ContextOwnerToken != 0 && IsValid(OwnerBoid) && OwnerBoid->IsAlive());
}

bool UBoidAttackComponent::ActivateCombatContext(uint64 OwnerToken)
{
	if (!OwnerToken || (ContextOwnerToken && ContextOwnerToken != OwnerToken)) return false;
	if (ContextOwnerToken == OwnerToken) return true;
	ContextOwnerToken = OwnerToken;
	AttackFacts.bContextActive = true;
	ResetCadence(EBoidAttackRejection::NoDetectedTarget);
	RefreshTickPrerequisites();
	SetComponentTickEnabled(true);
	return true;
}

void UBoidAttackComponent::DeactivateCombatContext(uint64 OwnerToken)
{
	if (OwnerToken && ContextOwnerToken != OwnerToken) return;
	ContextOwnerToken = 0;
	SetComponentTickEnabled(false);
	CadenceTarget.Reset();
	NextAttemptTime = 0.0;
	AttackFacts = FBoidAttackFacts();
	AttackFacts.LastRejection = EBoidAttackRejection::ContextInactive;
}

void UBoidAttackComponent::ResetCadence(EBoidAttackRejection Reason)
{
	CadenceTarget.Reset();
	NextAttemptTime = 0.0;
	AttackFacts.bCommonEligible = false;
	AttackFacts.bNormalEligible = false;
	AttackFacts.bApproachEligible = false;
	AttackFacts.bCloseEligible = false;
	AttackFacts.bAttemptDue = false;
	AttackFacts.SecondsUntilAttempt = 0.f;
	AttackFacts.LastRejection = Reason;
}

bool UBoidAttackComponent::ValidateMuzzleAndLaunch(const FVector& MuzzleLocation, const FVector& Direction, AActor* Target) const
{
	const AFlyingBoid* Boid = Cast<AFlyingBoid>(GetOwner());
	UWorld* World = GetWorld();
	if (!Boid || !World || !Boid->CollisionSphere || MuzzleLocation.ContainsNaN() || Direction.ContainsNaN()) return false;
	const FVector Center = Boid->CollisionSphere->GetComponentLocation();
	if (Center.ContainsNaN()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BoidProjectileMuzzle), false);
	Params.AddIgnoredActor(Boid);
	// Clearance uses the same ignored actors as projectile flight.
	for (TActorIterator<AFlyingBoid> It(World); It; ++It) Params.AddIgnoredActor(*It);
	// Keep the target in the projectile-sized sweeps to reject a muzzle inside it.
	const FCollisionShape Shape = FCollisionShape::MakeSphere(SnapshottedProjectileRadius);
	if (World->OverlapBlockingTestByChannel(MuzzleLocation, FQuat::Identity, ECC_GameTraceChannel1, Shape, Params)) return false;
	FHitResult Hit;
	if (World->SweepSingleByChannel(Hit, Center, MuzzleLocation, FQuat::Identity, ECC_GameTraceChannel1, Shape, Params) && Hit.bBlockingHit) return false;
	const FVector LaunchEnd = MuzzleLocation + Direction * SnapshottedClearance;
	if (LaunchEnd.ContainsNaN()) return false;
	FCollisionQueryParams LaunchParams = Params;
	if (Target) LaunchParams.AddIgnoredActor(Target);
	if (World->SweepSingleByChannel(Hit, MuzzleLocation, LaunchEnd, FQuat::Identity, ECC_GameTraceChannel1, Shape, LaunchParams) && Hit.bBlockingHit) return false;
	return true;
}

void UBoidAttackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AFlyingBoid* Boid = Cast<AFlyingBoid>(GetOwner());
	if (!IsValid(Boid) || !Boid->IsAlive())
	{
		DeactivateCombatContext(0);
		AttackFacts.LastRejection = EBoidAttackRejection::BoidDead;
		return;
	}
	if (!ContextOwnerToken)
	{
		SetComponentTickEnabled(false);
		return;
	}
	RefreshTickPrerequisites();
	const FBoidMovementIntent Intent = Boid->GetMovementIntent();
	if (Intent.Policy != EBoidMovementPolicy::RangedOrbit || Intent.bIsTransit
		|| !Boid->IsSteeringActive() || !IsValid(Boid->GetSimulationRunner()))
	{
		// Retain task ownership until ExitState; only suspend firing here.
		AttackFacts.bContextActive = false;
		ResetCadence(EBoidAttackRejection::ContextInactive);
		return;
	}
	AttackFacts.bContextActive = true;

	const FBoidTargetFacts TargetFacts = Boid->GetTargetFacts();
	AFlyingCharacter* Target = Cast<AFlyingCharacter>(TargetFacts.TargetActor.Get());
	if (!TargetFacts.bDetected || !IsValid(Target)) { ResetCadence(EBoidAttackRejection::NoDetectedTarget); return; }
	if (!Target->IsAlive()) { ResetCadence(EBoidAttackRejection::TargetInvalid); return; }
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (CadenceTarget.Get() != Target)
	{
		ResetCadence(EBoidAttackRejection::NotDue);
		CadenceTarget = Target;
		NextAttemptTime = Now + SnapshottedInterval;
	}
	if (!bConfigurationValid || !TargetFacts.bHasSight) AttackFacts.bCommonEligible = false;
	const FVector TargetCenter = Target->GetActorLocation();
	const FVector BoidCenter = Boid->GetActorLocation();
	if (BoidCenter.ContainsNaN() || TargetCenter.ContainsNaN())
	{
		ResetCadence(EBoidAttackRejection::TargetInvalid);
		return;
	}
	const float CenterDistance = FVector::Distance(BoidCenter, TargetCenter);
	const FBoidPositioningFacts Positioning = Boid->GetPositioningFacts();
	AttackFacts.bNormalEligible = TargetFacts.bAttackReady && Positioning.bHasPosition
		&& CenterDistance >= Positioning.EffectiveMinimumRangeCm && CenterDistance <= Positioning.EffectiveMaximumRangeCm;
	// Orbit readiness remains a positioning fact. While approaching or retreating,
	// the attack distance alone can authorize this branch after common live checks.
	AttackFacts.bApproachEligible = !Positioning.bOrbiting && SnapshottedApproachDistance > 0.f
		&& CenterDistance <= SnapshottedApproachDistance;
	AttackFacts.bCloseEligible = SnapshottedCloseDistance > 0.f && CenterDistance <= SnapshottedCloseDistance;
	AttackFacts.bAttemptDue = Now >= NextAttemptTime;
	AttackFacts.SecondsUntilAttempt = FMath::Max(0.f, static_cast<float>(NextAttemptTime - Now));
	if (!AttackFacts.bAttemptDue)
	{
		return;
	}
	// A failed opportunity is discarded. A hitch can never accumulate extra shots.
	NextAttemptTime = Now + SnapshottedInterval;
	AttackFacts.bAttemptDue = true;
	AttackFacts.SecondsUntilAttempt = SnapshottedInterval;
	AttackFacts.bCommonEligible = false;
	if (!SnapshottedProjectileClass) { AttackFacts.LastRejection = EBoidAttackRejection::NoProjectileClass; return; }
	if (!bConfigurationValid) { AttackFacts.LastRejection = EBoidAttackRejection::InvalidConfiguration; return; }
	if (!TargetFacts.bHasSight) { AttackFacts.LastRejection = EBoidAttackRejection::NoSight; return; }
	if (!AttackFacts.bNormalEligible && !AttackFacts.bApproachEligible && !AttackFacts.bCloseEligible)
	{
		AttackFacts.LastRejection = !Positioning.bOrbiting && SnapshottedApproachDistance > 0.f
			? EBoidAttackRejection::OutOfRange
			: (TargetFacts.bAttackReady ? EBoidAttackRejection::OutOfRange : EBoidAttackRejection::NotPositionReady);
		return;
	}
	USceneComponent* Muzzle = SnapshottedMuzzleComponent.Get();
	if (!IsValid(Muzzle) || Muzzle->GetOwner() != Boid
		|| (!SnapshottedMuzzleSocketName.IsNone() && !Muzzle->DoesSocketExist(SnapshottedMuzzleSocketName)))
	{
		AttackFacts.LastRejection = EBoidAttackRejection::MuzzleInvalid;
		return;
	}
	const FTransform MuzzleTransform = SnapshottedMuzzleSocketName.IsNone()
		? Muzzle->GetComponentTransform() : Muzzle->GetSocketTransform(SnapshottedMuzzleSocketName, RTS_World);
	if (!MuzzleTransform.IsValid()) { AttackFacts.LastRejection = EBoidAttackRejection::MuzzleInvalid; return; }
	const FVector MuzzleLocation = MuzzleTransform.GetLocation();
	const FVector Aim = (TargetCenter - MuzzleLocation).GetSafeNormal();
	UWorld* World = GetWorld();
	if (!World) { AttackFacts.LastRejection = EBoidAttackRejection::SpawnFailed; return; }
	FCollisionQueryParams SightParams(SCENE_QUERY_STAT(BoidAttackFreshSight), true);
	SightParams.AddIgnoredActor(Boid);
	SightParams.AddIgnoredActor(Target);
	FHitResult SightHit;
	const bool bFreshSight = !World->LineTraceSingleByChannel(SightHit, BoidCenter, TargetCenter, ECC_Visibility, SightParams);
	if (!bFreshSight || Aim.IsNearlyZero() || !ValidateMuzzleAndLaunch(MuzzleLocation, Aim, Target))
	{
		AttackFacts.bCommonEligible = false;
		AttackFacts.LastRejection = !bFreshSight ? EBoidAttackRejection::NoSight : EBoidAttackRejection::MuzzleBlocked;
		return;
	}

	AttackFacts.bCommonEligible = true;
	const uint64 LaunchContext = ContextOwnerToken;
	const FTransform SpawnTransform(Aim.Rotation(), MuzzleLocation);
	AFlyingBoidAttackProjectile* Projectile = World->SpawnActorDeferred<AFlyingBoidAttackProjectile>(
		SnapshottedProjectileClass, SpawnTransform, Boid, Boid->GetInstigator(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile || !Projectile->InitializeLaunch(Boid, Target, Aim, SnapshottedProjectileSpeed,
		SnapshottedProjectileRadius, SnapshottedProjectileDamage, SnapshottedProjectileLifespan))
	{
		if (Projectile) Projectile->Destroy();
		AttackFacts.LastRejection = EBoidAttackRejection::SpawnFailed;
		return;
	}
	UGameplayStatics::FinishSpawningActor(Projectile, SpawnTransform);
	// BeginPlay/presentation can kill the shooter or stop its StateTree.
	if (!IsValid(Boid) || !Boid->IsAlive() || ContextOwnerToken != LaunchContext) return;
	AttackFacts.ShotCount = FMath::Min(AttackFacts.ShotCount, MAX_int32 - 1) + 1;
	AttackFacts.LastRejection = EBoidAttackRejection::None;
}

void UBoidAttackComponent::RefreshTickPrerequisites()
{
	const AFlyingBoid* Boid = Cast<AFlyingBoid>(GetOwner());
	ASwarmSimulationRunner* Runner = Boid ? Boid->GetSimulationRunner() : nullptr;
	if (TickRunner.Get() == Runner) return;
	if (ASwarmSimulationRunner* Previous = TickRunner.Get()) RemoveTickPrerequisiteActor(Previous);
	TickRunner = Runner;
	if (Runner) AddTickPrerequisiteActor(Runner);
}
