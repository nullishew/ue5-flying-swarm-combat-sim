#include "Combat/BoidAttackComponent.h"

#include "Combat/BoidAttackProjectile.h"
#include "Combat/BoidAttackAiming.h"
#include "UObject/UnrealType.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "GameFramework/Actor.h"
#include "Player/FlyingCharacter.h"
#include "Swarm/BoidIntentTypes.h"
#include "Swarm/FlyingBoid.h"
#include "Swarm/Simulation/SwarmSimulationSubsystem.h"

UBoidAttackComponent::UBoidAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

FBoidDebugAttackState UBoidAttackComponent::GetDebugAttackState() const
{
	FBoidDebugAttackState State;
	const AFlyingBoid* Boid = Cast<AFlyingBoid>(GetOwner());
	const USwarmSimulationSubsystem* Simulation = IsValid(Boid) ? Boid->GetSimulationSubsystem() : nullptr;
	if (!IsValid(Boid) || !Boid->IsAlive() || !ContextOwnerToken || !IsValid(Simulation)
		|| !Simulation->IsAgentRegistered(Boid) || !Simulation->IsDebugSimulationReady() || !Boid->IsSteeringActive()) return State;
	const FBoidMovementIntent Intent = Boid->GetMovementIntent();
	if (Intent.Policy != EBoidMovementPolicy::RangedOrbit || Intent.bIsTransit) return State;
	const FBoidTargetFacts TargetFacts = Boid->GetTargetFacts();
	AFlyingCharacter* Target = Cast<AFlyingCharacter>(TargetFacts.TargetActor.Get());
	if (!TargetFacts.bDetected || !IsValid(Target) || !Target->IsAlive() || Boid->MovementTarget != Target) return State;
	const bool bCadenceValid = CadenceTarget.Get() == Target && FMath::IsFinite(NextAttemptTime) && NextAttemptTime > 0.0
		&& FMath::IsFinite(ScheduledAttemptIntervalSeconds) && ScheduledAttemptIntervalSeconds > 0.f;
	State.bHasCadence = bCadenceValid;
	if (bCadenceValid) State.AttemptIntervalSeconds = ScheduledAttemptIntervalSeconds;
	if (bCadenceValid)
	{
		const UWorld* World = GetWorld();
		if (World)
		{
			const double SecondsUntilAttempt = NextAttemptTime - World->GetTimeSeconds();
			if (FMath::IsFinite(SecondsUntilAttempt)) State.SecondsUntilAttempt = static_cast<float>(FMath::Clamp(SecondsUntilAttempt, 0.0, static_cast<double>(MAX_flt)));
		}
	}
	if (!bConfigurationValid || !SnapshottedProjectileClass || !TargetFacts.bHasSight) return State;
	const FVector BoidCenter = Boid->GetActorLocation();
	const FVector TargetCenter = Target->GetActorLocation();
	if (BoidCenter.ContainsNaN() || TargetCenter.ContainsNaN()) return State;
	const float CenterDistance = FVector::Distance(BoidCenter, TargetCenter);
	const FBoidPositioningFacts Positioning = Boid->GetPositioningFacts();
	const bool bNormalEligible = TargetFacts.bAttackReady && Positioning.bHasPosition
		&& CenterDistance >= Positioning.EffectiveMinimumRangeCm && CenterDistance <= Positioning.EffectiveMaximumRangeCm;
	const bool bApproachEligible = !Positioning.bOrbiting && SnapshottedApproachDistance > 0.f
		&& CenterDistance <= SnapshottedApproachDistance;
	const bool bCloseEligible = SnapshottedCloseDistance > 0.f && CenterDistance <= SnapshottedCloseDistance;
	State.bCanEngage = bNormalEligible || bApproachEligible || bCloseEligible;
	return State;
}

#if WITH_EDITOR
void UBoidAttackComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UBoidAttackComponent, FanProjectileCount))
	{
		FanProjectileCount = BoidAttack::NormalizeFanCount(FanProjectileCount);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

bool UBoidAttackComponent::InitializeRandomState()
{
	if (bRandomInitialized) return true;
	AFlyingBoid* Boid = Cast<AFlyingBoid>(GetOwner());
	USwarmSimulationSubsystem* Runner = IsValid(Boid) ? Boid->GetSimulationSubsystem() : nullptr;
	if (!IsValid(Runner)) return false;
	const int32 AgentId = Runner->GetAgentId(Boid);
	if (AgentId == INDEX_NONE) return false;
	AttackRandom.Initialize(BoidAttack::RandomSeed(Boid->GetSimulationSeed(), AgentId));
	AssignedFanProbability = BoidAttack::AssignFanProbability(AttackRandom,
		SnapshottedMinimumFanProbability, SnapshottedMaximumFanProbability);
	bRandomInitialized = true;
	AttackFacts.AssignedFanProbability = AssignedFanProbability;
	return true;
}

void UBoidAttackComponent::BeginPlay()
{
	Super::BeginPlay();
	AFlyingBoid* OwnerBoid = Cast<AFlyingBoid>(GetOwner());
	USceneComponent* ResolvedMuzzle = MuzzleReference.OtherActor.IsValid() ? nullptr : Cast<USceneComponent>(MuzzleReference.GetComponent(OwnerBoid));
	if (!ResolvedMuzzle && MuzzleReference.ComponentProperty.IsNone() && MuzzleReference.PathToComponent.IsEmpty() && !MuzzleReference.OverrideComponent.IsValid() && !MuzzleReference.OtherActor.IsValid()
		&& OwnerBoid) ResolvedMuzzle = OwnerBoid->CollisionSphere;

	// Normalize saved defaults too; editor metadata alone cannot validate old assets.
	const auto SafeValue = [](float Value, float Default, float Minimum, float Maximum)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Default;
	};
	SnapshottedPredictionStrength = SafeValue(PredictionStrength, 1.f, 0.f, 1.f);
	SnapshottedMaximumPredictionSeconds = SafeValue(MaximumPredictionSeconds, 1.25f, 0.f, MAX_flt);
	bSnapshottedEnableFans = bEnableFans;
	SnapshottedFanCount = BoidAttack::NormalizeFanCount(FanProjectileCount);
	SnapshottedFanAngle = SafeValue(FanTotalAngleDegrees, 24.f, 0.f, 180.f);
	SnapshottedMinimumFanDistance = SafeValue(MinimumFanDistanceCm, 400.f, 0.f, MAX_flt);
	SnapshottedFanDamageMultiplier = SafeValue(FanTotalDamageMultiplier, 1.5f, 0.f, 10.f);
	SnapshottedMinimumFanProbability = SafeValue(MinimumFanProbability, 0.10f, 0.f, 1.f);
	SnapshottedMaximumFanProbability = SafeValue(MaximumFanProbability, 0.30f, 0.f, 1.f);
	SnapshottedCadenceJitter = SafeValue(CadenceJitterFraction, 0.15f, 0.f, 0.95f);
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
		SnapshottedProjectileSpeed = Speed;
		SnapshottedProjectileDamage = Defaults ? Defaults->DamageAmount : 0.f;
		SnapshottedProjectileLifespan = Defaults ? Defaults->ProjectileLifespan : 0.f;
		bConfigurationValid = Defaults && Movement && FMath::IsFinite(Speed) && Speed > 0.f
			&& FMath::IsFinite(SnapshottedProjectileDamage) && SnapshottedProjectileDamage >= 0.f
			&& FMath::IsFinite(SnapshottedProjectileLifespan) && SnapshottedProjectileLifespan > 0.f;
	}
	AttackFacts = FBoidAttackFacts();
	AttackFacts.AssignedFanProbability = AssignedFanProbability;
	AttackFacts.LastRejection = bConfigurationValid ? EBoidAttackRejection::ContextInactive : EBoidAttackRejection::InvalidConfiguration;
	// Reapply after saved Blueprint defaults. The Combat task enables firing.
	RefreshSimulationSubscription();
	SetComponentTickEnabled(false);
}

bool UBoidAttackComponent::ActivateCombatContext(uint64 OwnerToken)
{
	if (!OwnerToken || (ContextOwnerToken && ContextOwnerToken != OwnerToken)) return false;
	if (ContextOwnerToken == OwnerToken) return true;
	ContextOwnerToken = OwnerToken;
	AttackFacts.bContextActive = true;
	ResetCadence(EBoidAttackRejection::NoDetectedTarget);
	RefreshSimulationSubscription();
	SetComponentTickEnabled(false);
	return true;
}

void UBoidAttackComponent::DeactivateCombatContext(uint64 OwnerToken)
{
	if (OwnerToken && ContextOwnerToken != OwnerToken) return;
	ContextOwnerToken = 0;
	DetachSimulation();
	SetComponentTickEnabled(false);
	CadenceTarget.Reset();
	NextAttemptTime = 0.0;
	ScheduledAttemptIntervalSeconds = 0.f;
	AttackFacts = FBoidAttackFacts();
	AttackFacts.AssignedFanProbability = AssignedFanProbability;
	AttackFacts.LastRejection = EBoidAttackRejection::ContextInactive;
}

void UBoidAttackComponent::ResetCadence(EBoidAttackRejection Reason)
{
	CadenceTarget.Reset();
	NextAttemptTime = 0.0;
	ScheduledAttemptIntervalSeconds = 0.f;
	AttackFacts.bCommonEligible = false;
	AttackFacts.bNormalEligible = false;
	AttackFacts.bApproachEligible = false;
	AttackFacts.bCloseEligible = false;
	AttackFacts.bAttemptDue = false;
	AttackFacts.SecondsUntilAttempt = 0.f;
	AttackFacts.LastRejection = Reason;
}

bool UBoidAttackComponent::ValidateMuzzle(const USphereComponent& ProjectileSphere, const FCollisionQueryParams& Params) const
{
	const FVector MuzzleLocation = ProjectileSphere.GetComponentLocation();
	const AFlyingBoid* Boid = Cast<AFlyingBoid>(GetOwner());
	UWorld* World = GetWorld();
	if (!Boid || !World || !Boid->CollisionSphere || MuzzleLocation.ContainsNaN()) return false;
	const FVector Center = Boid->CollisionSphere->GetComponentLocation();
	if (Center.ContainsNaN()) return false;
	// Keep the target in the projectile-sized sweeps to reject a muzzle inside it.
	const FCollisionShape Shape = ProjectileSphere.GetCollisionShape();
	if (World->OverlapBlockingTestByChannel(MuzzleLocation, FQuat::Identity, ECC_GameTraceChannel1, Shape, Params)) return false;
	FHitResult Hit;
	if (World->SweepSingleByChannel(Hit, Center, MuzzleLocation, FQuat::Identity, ECC_GameTraceChannel1, Shape, Params) && Hit.bBlockingHit) return false;
	return true;
}

bool UBoidAttackComponent::ValidateLaunch(const USphereComponent& ProjectileSphere, const FVector& Direction, AActor* Target, const FCollisionQueryParams& Params) const
{
	const FVector MuzzleLocation = ProjectileSphere.GetComponentLocation();
	UWorld* World = GetWorld();
	if (!World || Direction.ContainsNaN()) return false;
	const FCollisionShape Shape = ProjectileSphere.GetCollisionShape();
	FHitResult Hit;
	const FVector LaunchEnd = MuzzleLocation + Direction * SnapshottedClearance;
	if (LaunchEnd.ContainsNaN()) return false;
	FCollisionQueryParams LaunchParams = Params;
	// A prior fan projectile's presentation may have spawned another exceptional swarm actor.
	if (const USwarmSimulationSubsystem* Simulation = World->GetSubsystem<USwarmSimulationSubsystem>()) Simulation->GetCollisionQueries().AddProjectileExceptions(LaunchParams);
	if (Target) LaunchParams.AddIgnoredActor(Target);
	if (World->SweepSingleByChannel(Hit, MuzzleLocation, LaunchEnd, FQuat::Identity, ECC_GameTraceChannel1, Shape, LaunchParams) && Hit.bBlockingHit) return false;
	return true;
}

void UBoidAttackComponent::OnSimulationFrame(float DeltaTime)
{
	AFlyingBoid* Boid = Cast<AFlyingBoid>(GetOwner());
	if (!IsValid(Boid) || !Boid->IsAlive())
	{
		DeactivateCombatContext(0);
		AttackFacts.LastRejection = EBoidAttackRejection::BoidDead;
		return;
	}
	if (!ContextOwnerToken)
	{
        DetachSimulation();
		SetComponentTickEnabled(false);
		return;
	}
	RefreshSimulationSubscription();
	const FBoidMovementIntent Intent = Boid->GetMovementIntent();
	if (Intent.Policy != EBoidMovementPolicy::RangedOrbit || Intent.bIsTransit
		|| !Boid->IsSteeringActive() || (!IsValid(Boid->GetSimulationSubsystem()) || !Boid->GetSimulationSubsystem()->IsAgentRegistered(Boid) || !Boid->GetSimulationSubsystem()->CanSimulate()))
	{
		// Retain task ownership until ExitState; only suspend firing here.
		AttackFacts.bContextActive = false;
		ResetCadence(EBoidAttackRejection::ContextInactive);
		return;
	}
	if (!InitializeRandomState())
	{
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
		ScheduledAttemptIntervalSeconds = BoidAttack::AttemptDelay(AttackRandom, SnapshottedInterval, SnapshottedCadenceJitter, true);
		NextAttemptTime = Now + ScheduledAttemptIntervalSeconds;
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
	const float Delay = BoidAttack::AttemptDelay(AttackRandom, SnapshottedInterval, SnapshottedCadenceJitter, false);
	ScheduledAttemptIntervalSeconds = Delay;
	NextAttemptTime = Now + Delay;
	AttackFacts.bAttemptDue = true;
	AttackFacts.SecondsUntilAttempt = Delay;
	AttackFacts.LaunchedProjectileCount = 0;
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
	const FVector TargetVelocity = Target->GetVelocity();
	const BoidAttack::FAimResult Prediction = BoidAttack::PredictAim(MuzzleLocation, TargetCenter, TargetVelocity,
		SnapshottedProjectileSpeed, SnapshottedPredictionStrength, SnapshottedMaximumPredictionSeconds, SnapshottedProjectileLifespan);
	const FVector Aim = Prediction.Direction;
	AttackFacts.PredictedAimPoint = Prediction.Point;
	AttackFacts.AppliedPredictionSeconds = Prediction.PredictionSeconds;
	UWorld* World = GetWorld();
	if (!World) { AttackFacts.LastRejection = EBoidAttackRejection::SpawnFailed; return; }
	FCollisionQueryParams SightParams(SCENE_QUERY_STAT(BoidAttackFreshSight), true);
	SightParams.AddIgnoredActor(Boid);
	SightParams.AddIgnoredActor(Target);
	FHitResult SightHit;
	const bool bFreshSight = !World->LineTraceSingleByChannel(SightHit, BoidCenter, TargetCenter, ECC_Visibility, SightParams);
	if (!bFreshSight || Aim.IsNearlyZero())
	{
		AttackFacts.bCommonEligible = false;
		AttackFacts.LastRejection = !bFreshSight ? EBoidAttackRejection::NoSight : EBoidAttackRejection::MuzzleBlocked;
		return;
	}

	const bool bFan = BoidAttack::SelectFan(AttackRandom, bSnapshottedEnableFans, AssignedFanProbability,
		CenterDistance, SnapshottedMinimumFanDistance);
	AttackFacts.SelectedPattern = bFan ? EBoidAttackPattern::Fan : EBoidAttackPattern::Single;
	const TArray<FVector> Directions = bFan
		? BoidAttack::FanDirections(Aim, TargetVelocity, SnapshottedFanCount, SnapshottedFanAngle)
		: TArray<FVector>{Aim};
	const float Damage = bFan
		? SnapshottedProjectileDamage * (SnapshottedFanDamageMultiplier / SnapshottedFanCount)
		: SnapshottedProjectileDamage;
	if (!FMath::IsFinite(Damage)) { AttackFacts.LastRejection = EBoidAttackRejection::InvalidConfiguration; return; }
	const uint64 LaunchContext = ContextOwnerToken;
	const auto CanContinue = [this, Boid, Target, LaunchContext]()
	{
		if (!IsValid(this) || !IsValid(Boid) || !Boid->IsAlive() || ContextOwnerToken != LaunchContext
			|| !IsValid(Target) || !Target->IsAlive() || CadenceTarget.Get() != Target) return false;
		const FBoidTargetFacts Facts = Boid->GetTargetFacts();
		const FBoidMovementIntent CurrentIntent = Boid->GetMovementIntent();
		return Facts.bDetected && Facts.bHasSight && Facts.TargetActor.Get() == Target && Boid->MovementTarget == Target
			&& CurrentIntent.Policy == EBoidMovementPolicy::RangedOrbit && !CurrentIntent.bIsTransit
			&& Boid->IsSteeringActive() && IsValid(Boid->GetSimulationSubsystem()) && Boid->GetSimulationSubsystem()->IsAgentRegistered(Boid) && Boid->GetSimulationSubsystem()->CanSimulate();
	};
	AttackFacts.bCommonEligible = false;
	AttackFacts.LastRejection = EBoidAttackRejection::MuzzleBlocked;
	TRACE_CPUPROFILER_EVENT_SCOPE(SwarmAttackAttempt);
	if (!CanContinue()) return;
	FCollisionQueryParams MuzzleParams(SCENE_QUERY_STAT(BoidProjectileMuzzle), false);
	MuzzleParams.AddIgnoredActor(Boid);
	Boid->GetSimulationSubsystem()->GetCollisionQueries().AddProjectileExceptions(MuzzleParams);
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Boid;
	SpawnParams.Instigator = Boid->GetInstigator();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	bool bPlayedFireSound = false;
	for (const FVector& Direction : Directions)
	{
		if (!CanContinue()) return;
		const FTransform SpawnTransform(Direction.Rotation(), MuzzleLocation);
		AFlyingBoidAttackProjectile* Projectile = World->SpawnActor<AFlyingBoidAttackProjectile>(SnapshottedProjectileClass, SpawnTransform, SpawnParams);
		// Construction/BeginPlay must finish before reading the authored collision shape.
		if (!CanContinue())
		{
			if (IsValid(Projectile)) Projectile->Destroy();
			return;
		}
		if (!IsValid(Projectile))
		{
			AttackFacts.LastRejection = EBoidAttackRejection::SpawnFailed;
			continue;
		}
		USphereComponent* Sphere = Projectile->CollisionComponent;
		if (!IsValid(Sphere) || !Sphere->IsRegistered() || Projectile->GetRootComponent() != Sphere
			|| !FMath::IsFinite(Sphere->GetScaledSphereRadius()) || Sphere->GetScaledSphereRadius() <= 0.f)
		{
			Projectile->Destroy();
			AttackFacts.LastRejection = EBoidAttackRejection::InvalidConfiguration;
			continue;
		}
		// Native projectiles remain inactive until InitializeLaunch; validate the final component geometry.
		FCollisionQueryParams ProjectileParams = MuzzleParams;
		ProjectileParams.AddIgnoredActor(Projectile);
		if (!ValidateMuzzle(*Sphere, ProjectileParams) || !ValidateLaunch(*Sphere, Direction, Target, ProjectileParams))
		{
			Projectile->Destroy();
			continue;
		}
		AttackFacts.bCommonEligible = true;
		if (!Projectile->InitializeLaunch(Boid, Target, Direction, SnapshottedProjectileSpeed, Damage, SnapshottedProjectileLifespan))
		{
			if (IsValid(Projectile)) Projectile->Destroy();
			AttackFacts.LastRejection = EBoidAttackRejection::SpawnFailed;
			continue;
		}
		if (!bPlayedFireSound)
		{
			bPlayedFireSound = true;
			if (IsValid(Boid) && GetNetMode() != NM_DedicatedServer && Boid->FireSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, Boid->FireSound, MuzzleLocation, Boid->FireVolume, 1.0f, 0.0f, Boid->FireAttenuation);
			}
		}
		// Launch presentation can kill the shooter or stop its StateTree.
		if (!CanContinue()) return;
		++AttackFacts.LaunchedProjectileCount;
		AttackFacts.ShotCount = FMath::Min(AttackFacts.ShotCount, MAX_int32 - 1) + 1;
		if (AttackFacts.LaunchedProjectileCount == 1)
		{
			AttackFacts.VolleyCount = FMath::Min(AttackFacts.VolleyCount, MAX_int32 - 1) + 1;
		}
	}
	if (AttackFacts.LaunchedProjectileCount > 0) AttackFacts.LastRejection = EBoidAttackRejection::None;
}

void UBoidAttackComponent::RefreshSimulationSubscription()
{
    USwarmSimulationSubsystem* Simulation = ContextOwnerToken && GetWorld()
        ? GetWorld()->GetSubsystem<USwarmSimulationSubsystem>() : nullptr;
    if (SimulationSubscription.Get() == Simulation && SimulationHandle.IsValid()) return;
    DetachSimulation();
    if (Simulation)
    {
        SimulationSubscription = Simulation;
        SimulationHandle = Simulation->OnPostSimulation.AddUObject(this, &UBoidAttackComponent::OnSimulationFrame);
    }
}

void UBoidAttackComponent::DetachSimulation()
{
    if (auto* Simulation = SimulationSubscription.Get()) Simulation->OnPostSimulation.Remove(SimulationHandle);
    SimulationSubscription.Reset();
    SimulationHandle.Reset();
}

void UBoidAttackComponent::SuspendSimulation()
{
    AttackFacts.bContextActive = false;
    ResetCadence(EBoidAttackRejection::ContextInactive);
}

void UBoidAttackComponent::EndPlay(EEndPlayReason::Type Reason)
{
    DeactivateCombatContext(0);
    Super::EndPlay(Reason);
}
