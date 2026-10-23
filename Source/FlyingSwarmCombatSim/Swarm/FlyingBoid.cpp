#include "Swarm/FlyingBoid.h"
#include "Swarm/Debug/SwarmMovementDebug.h"
#include "Combat/BoidAttackComponent.h"
#include "Sound/SoundBase.h"
#include "StateTreeReference.h"
#include "StateTree.h"
#include "UObject/UnrealType.h"

#include "Swarm/Flight/BoidFlightAlgorithms.h"
#include "Swarm/Flight/SwarmAvoidanceCandidates.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Swarm/Flight/BoidFlightMovementComponent.h"
#include "Player/FlyingCharacter.h"
#include "Swarm/Steering/BoidSteeringProfile.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Crc.h"
#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"

AFlyingBoid::AFlyingBoid()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->InitSphereRadius(30.0f);
	CollisionSphere->SetCollisionProfileName(TEXT("Pawn"));
	// Local separation owns boid spacing; their sweeps must not block one another.
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
	CollisionSphere->SetGenerateOverlapEvents(true);

	FlightMovement = CreateDefaultSubobject<UBoidFlightMovementComponent>(TEXT("FlightMovement"));
	FlightMovement->SetUpdatedComponent(CollisionSphere);

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(CollisionSphere);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetRelativeScale3D(FVector(0.6f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Visual->SetStaticMesh(SphereMesh.Object);
	}

	StateTree = CreateDefaultSubobject<UStateTreeComponent>(TEXT("StateTree"));
	StateTree->SetStartLogicAutomatically(false);

	AttackComponent = CreateDefaultSubobject<UBoidAttackComponent>(TEXT("AttackComponent"));
}

void AFlyingBoid::BeginPlay()
{
	MaxHealth = FMath::IsFinite(MaxHealth) ? FMath::Max(1.0f, MaxHealth) : 1.0f;
	CurrentHealth = MaxHealth;
	bDead = false;
	// Restore the same body invariants used by InitializeWander before any population
	// notifications, even when a Blueprint serialized older collision defaults.
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_Pawn);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
    if (IsValid(StateTree)) StateTree->SetStartLogicAutomatically(false);
	// Saved extra colliders must be excluded before Blueprint BeginPlay can launch projectiles.
	if (USwarmSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<USwarmSimulationSubsystem>()) Simulation->GetCollisionQueries().RefreshBoid(this);
    Super::BeginPlay();
	// Restore overlap notifications after Blueprint defaults for population sensors.
	CollisionSphere->SetGenerateOverlapEvents(true);
	CollisionSphere->UpdateOverlaps();
    if (USwarmSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<USwarmSimulationSubsystem>())
    {
        Simulation->RegisterBoid(this);
    }
}

void AFlyingBoid::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DeathVisualTimer);
	if (USwarmSimulationSubsystem* Simulation = SimulationSubsystem.Get())
	{
		Simulation->UnregisterBoid(this);
		ClearSimulationSubsystem(Simulation);
	}
	Super::EndPlay(EndPlayReason);
}

float AFlyingBoid::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (!IsAlive() || bIsProcessingDamage || !FMath::IsFinite(DamageAmount) || DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	TGuardValue<bool> DamageGuard(bIsProcessingDamage, true);
	const float ProcessedDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!FMath::IsFinite(ProcessedDamage) || ProcessedDamage <= 0.0f)
	{
		return 0.0f;
	}

	const float PreviousHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(PreviousHealth - ProcessedDamage, 0.0f, MaxHealth);
	const float ActualDamage = PreviousHealth - CurrentHealth;
	if (ActualDamage <= 0.0f)
	{
		return 0.0f;
	}

	if (GetNetMode() != NM_DedicatedServer && HitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, HitSound, GetActorLocation(), HitVolume, 1.0f, 0.0f, HitAttenuation);
	}

	const bool bJustDied = PreviousHealth > 0.0f && CurrentHealth <= 0.0f;
	if (bJustDied)
	{
		HandleDeath(); // Disable immediately, before any external notification.
	}
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	if (bJustDied)
	{
		OnDeath.Broadcast();
		// Presentation cannot keep a dead drone visible indefinitely. Schedule after
		// callbacks so Blueprint death effects can spawn independently at this position.
		if (IsValid(this))
		{
			GetWorldTimerManager().SetTimer(DeathVisualTimer, this, &AFlyingBoid::HideDeadVisuals, 0.08f, false);
			SetLifeSpan(0.5f);
		}
	}
	return ActualDamage;
}

void AFlyingBoid::HandleDeath()
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	if (IsValid(AttackComponent)) AttackComponent->DeactivateCombatContext(0);
	SetWanderActive(false);
	Velocity = FVector::ZeroVector;
	MovementTarget = nullptr;
	MovementTargetOwnerToken = 0;
	SensedTarget.Reset();
	TargetFacts = FBoidTargetFacts();
	RequestedRangedPhase = EBoidRangedPhaseRequest::Approach;
	RangedPhaseOwner = 0;
	NavigationFacts = FBoidNavigationFacts();
	PositioningFacts = FBoidPositioningFacts();
	CombatMovementFacts = FBoidCombatMovementFacts();
	SecondsSinceLastSight = 0.0f;
	FBoidMovementIntent EmptyIntent;
	const bool bIntentChanged = MovementIntent.Policy != EmptyIntent.Policy
		|| MovementIntent.bHasGoal != EmptyIntent.bHasGoal
		|| MovementIntent.Goal != EmptyIntent.Goal
		|| MovementIntent.ArrivalRadiusCm != EmptyIntent.ArrivalRadiusCm
		|| MovementIntent.StandOffDistanceCm != EmptyIntent.StandOffDistanceCm
		|| MovementIntent.MinimumTargetDistanceCm != EmptyIntent.MinimumTargetDistanceCm;
	EmptyIntent.Revision = MovementIntent.Revision + (bIntentChanged ? 1u : 0u);
	MovementIntent = EmptyIntent;
	MovementIntentOwnerToken = 0;
	if (CollisionSphere)
	{
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	SetActorEnableCollision(false);
	if (IsValid(StateTree))
	{
		StateTree->StopLogic(TEXT("Boid died"));
	}
	if (USwarmSimulationSubsystem* Simulation = SimulationSubsystem.Get())
	{
		Simulation->UnregisterBoid(this);
	}
}

FBoidAttackFacts AFlyingBoid::GetAttackFacts() const
{
	return IsValid(AttackComponent) ? AttackComponent->GetAttackFacts() : FBoidAttackFacts();
}

void AFlyingBoid::HideDeadVisuals()
{
	if (bDead)
	{
		// Includes Blueprint-added visuals, rather than only the native mesh.
		SetActorHiddenInGame(true);
	}
}

void AFlyingBoid::UpdateTargetSensing(float ElapsedSeconds)
{
	if (!IsAlive())
	{
		SensedTarget.Reset();
		TargetFacts = FBoidTargetFacts();
		SecondsSinceLastSight = 0.0f;
		return;
	}

	const float Elapsed = FMath::IsFinite(ElapsedSeconds) ? FMath::Max(0.0f, ElapsedSeconds) : 0.0f;
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	AFlyingCharacter* Player = PlayerController ? Cast<AFlyingCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!IsValid(Player) || !Player->IsAlive())
	{
		SensedTarget.Reset();
		TargetFacts = FBoidTargetFacts();
		SecondsSinceLastSight = 0.0f;
		return;
	}

	if (SensedTarget.Get() != Player)
	{
		SensedTarget = Player;
		TargetFacts = FBoidTargetFacts();
		TargetFacts.TargetActor = Player;
		SecondsSinceLastSight = 0.0f;
	}

	TargetFacts.TargetActor = Player;
	const FVector BoidCenter = CollisionSphere ? CollisionSphere->GetComponentLocation() : GetActorLocation();
	const UCapsuleComponent* PlayerCapsule = Player->GetCapsuleComponent();
	const FVector PlayerCenter = PlayerCapsule ? PlayerCapsule->GetComponentLocation() : Player->GetActorLocation();
	TargetFacts.DistanceCm = FVector::Distance(BoidCenter, PlayerCenter);

	bool bHasSight = false;
	if (UWorld* World = GetWorld())
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BoidTargetSight), true);
		QueryParams.AddIgnoredActor(this);
		QueryParams.AddIgnoredActor(Player);
		FHitResult Hit;
		bHasSight = !World->LineTraceSingleByChannel(Hit, BoidCenter, PlayerCenter, ECC_Visibility, QueryParams);
	}
	TargetFacts.bHasSight = bHasSight;
	if (!bHasSight) TargetFacts.bAttackReady = false;

	const float DetectionRadius = FMath::Max(0.0f, DetectionRadiusCm);
	const float LoseRadius = FMath::Max(DetectionRadius, LoseRadiusCm);
	if (bHasSight)
	{
		SecondsSinceLastSight = 0.0f;
		TargetFacts.LastKnownPosition = PlayerCenter;
		if (TargetFacts.DistanceCm <= DetectionRadius)
		{
			TargetFacts.bDetected = true;
		}
		else if (TargetFacts.DistanceCm > LoseRadius)
		{
			TargetFacts.bDetected = false;
		}
	}
	else
	{
		SecondsSinceLastSight += Elapsed;
		if (TargetFacts.DistanceCm > LoseRadius
			|| SecondsSinceLastSight >= FMath::Max(0.0f, LostSightGraceSeconds))
		{
			TargetFacts.bDetected = false;
		}
	}
	if (!TargetFacts.bDetected) TargetFacts.bAttackReady = false;
}

void AFlyingBoid::PublishPositioning(const FBoidPositioningFacts& Facts, bool bReady)
{
	PositioningFacts = IsAlive() ? Facts : FBoidPositioningFacts();
	TargetFacts.bAttackReady = IsAlive() && TargetFacts.bDetected && TargetFacts.bHasSight && bReady;
}

void AFlyingBoid::SetMovementIntent(const FBoidMovementIntent& Intent, uint64 OwnerToken)
{
	if (bDead)
	{
		return;
	}

	FBoidMovementIntent Sanitized = Intent;
	if (!Sanitized.bHasGoal || Sanitized.Goal.ContainsNaN())
	{
		Sanitized.bHasGoal = false;
		Sanitized.Goal = FVector::ZeroVector;
	}
	Sanitized.ArrivalRadiusCm = FMath::IsFinite(Sanitized.ArrivalRadiusCm)
		? FMath::Max(0.0f, Sanitized.ArrivalRadiusCm) : 0.0f;
	Sanitized.StandOffDistanceCm = FMath::IsFinite(Sanitized.StandOffDistanceCm)
		? FMath::Max(0.0f, Sanitized.StandOffDistanceCm) : 0.0f;
	Sanitized.MinimumTargetDistanceCm = FMath::IsFinite(Sanitized.MinimumTargetDistanceCm)
		? FMath::Max(0.0f, Sanitized.MinimumTargetDistanceCm) : 0.0f;

	const bool bEffectiveChange = MovementIntent.RangedPhaseControl != Sanitized.RangedPhaseControl
		|| MovementIntent.bIsTransit != Sanitized.bIsTransit
		|| MovementIntent.Policy != Sanitized.Policy
		|| MovementIntent.bHasGoal != Sanitized.bHasGoal
		|| MovementIntent.Goal != Sanitized.Goal
		|| MovementIntent.ArrivalRadiusCm != Sanitized.ArrivalRadiusCm
		|| MovementIntent.StandOffDistanceCm != Sanitized.StandOffDistanceCm
		|| MovementIntent.MinimumTargetDistanceCm != Sanitized.MinimumTargetDistanceCm;
	Sanitized.Revision = MovementIntent.Revision + (bEffectiveChange ? 1u : 0u);
	if (bEffectiveChange)
	{
		PositioningFacts = FBoidPositioningFacts();
		TargetFacts.bAttackReady = false;
	}
	if (OwnerToken != MovementIntentOwnerToken || Sanitized.Policy != EBoidMovementPolicy::RangedOrbit)
	{
		RequestedRangedPhase = EBoidRangedPhaseRequest::Approach;
		RangedPhaseOwner = 0;
	}
	if (Sanitized.Policy != EBoidMovementPolicy::RangedOrbit) CombatMovementFacts = FBoidCombatMovementFacts();
	MovementIntent = Sanitized;
	MovementIntentOwnerToken = OwnerToken;
}

uint64 AFlyingBoid::AcquireIntentToken()
{
	if (bDead)
	{
		return 0;
	}
	++NextIntentToken;
	if (NextIntentToken == 0)
	{
		++NextIntentToken;
	}
	return NextIntentToken;
}

void AFlyingBoid::SetMovementTarget(AActor* Target, uint64 OwnerToken)
{
	if (bDead)
	{
		return;
	}
	if (MovementTarget != Target)
	{
		PositioningFacts = FBoidPositioningFacts();
		TargetFacts.bAttackReady = false;
	}
	MovementTarget = Target;
	MovementTargetOwnerToken = OwnerToken;
}

bool AFlyingBoid::AssociateSimulationSubsystem(USwarmSimulationSubsystem* Simulation)
{
	if (bDead || !IsValid(Simulation) || Simulation->GetWorld() != GetWorld())
	{
		return false;
	}
	if (USwarmSimulationSubsystem* ExistingSimulation = SimulationSubsystem.Get(); ExistingSimulation && ExistingSimulation != Simulation)
	{
		return false;
	}
	SimulationSubsystem = Simulation;
	return true;
}

USwarmSimulationSubsystem* AFlyingBoid::GetSimulationSubsystem() const
{
	return SimulationSubsystem.Get();
}

void AFlyingBoid::ClearSimulationSubsystem(USwarmSimulationSubsystem* Simulation)
{
	if (SimulationSubsystem.Get() == Simulation)
	{
		SimulationSubsystem.Reset();
	}
}

void AFlyingBoid::StepTowards(const FVector& TargetLocation, float DeltaTime)
{
	if (bDead || DeltaTime <= 0.0f || !FMath::IsFinite(DeltaTime))
	{
		return;
	}
	const FVector Acceleration = CalculateArrivalAcceleration(GetActorLocation(), Velocity,
		TargetLocation, DeltaTime, 1.0f, 1.0f, 1.0f);
	ApplyAcceleration(Acceleration, DeltaTime, 1.0f);
}

FVector AFlyingBoid::CalculateArrivalAcceleration(const FVector& SnapshotPosition, const FVector& SnapshotVelocity,
	const FVector& TargetLocation, float DeltaTime, float SpeedScale, float AccelerationScale,
	float BrakingScale) const
{
	if (DeltaTime <= 0.0f || !FMath::IsFinite(DeltaTime))
	{
		return FVector::ZeroVector;
	}
	const FVector ToTarget = TargetLocation - SnapshotPosition;
	const float Distance = ToTarget.Size();
	const float MaxSpeed = FMath::Max(0.0f, MaxSpeedCmPerSec * SpeedScale);
	const float Acceleration = FMath::Max(0.0f, MaxAccelerationCmPerSec2 * AccelerationScale);
	const float Braking = FMath::Max(0.0f, BrakingAccelerationCmPerSec2 * BrakingScale);
	const float ArrivalRadius = FMath::Max(0.0f, ArrivalRadiusCm);

	// The braking-distance speed cap lets the boid slow before the goal.
	const float RemainingDistance = FMath::Max(0.0f, Distance - ArrivalRadius);
	const float DesiredSpeed = FMath::Min(MaxSpeed, FMath::Sqrt(2.0f * Braking * RemainingDistance));
	const FVector DesiredVelocity = Distance > UE_SMALL_NUMBER
		? ToTarget / Distance * DesiredSpeed
		: FVector::ZeroVector;
	const float AccelerationLimit = DesiredSpeed < SnapshotVelocity.Size() ? Braking : Acceleration;
	return (DesiredVelocity - SnapshotVelocity).GetClampedToMaxSize(AccelerationLimit * DeltaTime) / DeltaTime;
}


void AFlyingBoid::ApplyAcceleration(const FVector& DesiredAcceleration, float DeltaTime, float SpeedScale)
{
	if (bDead || DeltaTime <= 0.0f || !FMath::IsFinite(DeltaTime))
	{
		return;
	}
	const float MaxSpeed = FMath::Max(0.0f, MaxSpeedCmPerSec * SpeedScale);
	Velocity = (Velocity + DesiredAcceleration * DeltaTime).GetClampedToMaxSize(MaxSpeed);
	if (Velocity.SizeSquared() < FMath::Square(5.0f) && DesiredAcceleration.IsNearlyZero())
	{
		Velocity = FVector::ZeroVector;
		return;
	}

	const FVector Start = GetActorLocation();
	FHitResult Hit;
	SetActorLocation(Start + Velocity * DeltaTime, true, &Hit);
	if (bDead)
	{
		return;
	}
	if (Hit.bBlockingHit)
	{
		// Do not retain velocity into a wall after the swept move stops there.
		Velocity = FVector::VectorPlaneProject(Velocity, Hit.Normal);
	}
	else
	{
		Velocity = (GetActorLocation() - Start) / DeltaTime;
	}
}

void AFlyingBoid::InitializeWander(int32 InAgentId, int32 InitialSeed)
{
	if (bDead)
	{
		return;
	}
	// Reapply invariants after Blueprint defaults have been loaded.
	CollisionSphere->SetSimulatePhysics(false);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetGenerateOverlapEvents(true);
	CollisionSphere->SetCollisionObjectType(ECC_Pawn);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	// Blueprint collision presets must not turn flying projectiles into obstacles.
	CollisionSphere->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlightMovement->SetUpdatedComponent(CollisionSphere);
	bHasLastValidWanderPosition = false;
	AgentId = InAgentId;
	SimulationSeed = InitialSeed;
	FlightHeading = GetActorForwardVector().GetSafeNormal();
	if (FlightHeading.IsNearlyZero())
	{
		FlightHeading = FVector::ForwardVector;
	}
	Velocity = FVector::ZeroVector;
	AvoidanceHoldSeconds = 0.0f;
	HeldAvoidanceDirection = FVector::ZeroVector;
	bSteeringActive = false;
	ActiveSteeringProfile = nullptr;
	RuntimeProfile = nullptr;
	SteeringRuntimeStates.Reset();
	MovementIntent = FBoidMovementIntent();
	MovementIntentOwnerToken = 0;
	RangedPhaseOwner = 0;
	RequestedRangedPhase = EBoidRangedPhaseRequest::Approach;
	MovementTargetOwnerToken = 0;
}

void AFlyingBoid::SetSteeringProfile(UBoidSteeringProfile* Profile)
{
	if (bDead)
	{
		return;
	}
	ActiveSteeringProfile = Profile;
	bSteeringActive = true;
	PrepareSteeringRuntime(Profile);
}

void AFlyingBoid::SetWanderActive(bool bActive)
{
	if (bDead)
	{
		bActive = false;
	}
	bSteeringActive = bActive;
	if (!bActive)
	{
		ActiveSteeringProfile = nullptr;
		PositioningFacts = FBoidPositioningFacts();
		TargetFacts.bAttackReady = false;
	}
}

void AFlyingBoid::PrepareSteeringRuntime(UBoidSteeringProfile* Profile)
{
	if (bDead || !Profile || RuntimeProfile == Profile)
	{
		return;
	}
	RuntimeProfile = Profile;
	SteeringRuntimeStates.SetNum(Profile->Behaviors.Num());
	for (int32 Index = 0; Index < Profile->Behaviors.Num(); ++Index)
	{
		const FBoidSteeringBehaviorEntry& Entry = Profile->Behaviors[Index];
		if (!Entry.Behavior)
		{
			continue;
		}
		const FString EntryKey = Entry.EntryId.IsNone() ? FString::FromInt(Index) : Entry.EntryId.ToString();
		const FString StableKey = FString::Printf(TEXT("%s:%s"), *Profile->GetPathName(), *EntryKey);
		const uint32 EntryHash = FCrc::StrCrc32(*StableKey);
		const int32 BehaviorSeed = SimulationSeed ^ (AgentId * 196613) ^ static_cast<int32>(EntryHash);
		Entry.Behavior->InitializeRuntime(SteeringRuntimeStates[Index], BehaviorSeed);
	}
}

FBoidSteeringRuntimeState& AFlyingBoid::GetSteeringRuntimeState(int32 BehaviorIndex)
{
	check(SteeringRuntimeStates.IsValidIndex(BehaviorIndex));
	return SteeringRuntimeStates[BehaviorIndex];
}

FVector AFlyingBoid::TurnFlightHeading(const FVector& DesiredDirection, float MaxTurnRateDeg,
	float DeltaTime)
{
	const FVector SafeDesired = DesiredDirection.GetSafeNormal();
	if (!SafeDesired.IsNearlyZero())
	{
		FlightHeading = FMath::VInterpNormalRotationTo(FlightHeading, SafeDesired, DeltaTime,
			FMath::Max(0.0f, MaxTurnRateDeg));
	}
	return FlightHeading;
}

void AFlyingBoid::HoldAvoidanceDirection(const FVector& Direction, float Seconds)
{
	HeldAvoidanceDirection = Direction.GetSafeNormal();
	AvoidanceHoldSeconds = FMath::Max(0.0f, Seconds);
}

namespace
{
	FCollisionQueryParams BoidMovementQuery(const AFlyingBoid* Boid)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BoidFlightRecovery), false);
		Params.AddIgnoredActor(Boid);
		Params.AddIgnoredActors(Boid->CollisionSphere->GetMoveIgnoreActors());
		return Params;
	}
}

void AFlyingBoid::ApplyWanderAcceleration(const FVector& DesiredAcceleration, float DeltaTime,
	float CruiseSpeed, const UBoxComponent* Bounds, float BoundsMarginCm, float VisualTurnSpeed, FSwarmMovementCommitObservation* Observation)
{
	if (Observation) *Observation = FSwarmMovementCommitObservation();
	if (bDead || DeltaTime <= 0.0f || !FMath::IsFinite(DeltaTime))
	{
		if (Observation) Observation->Result = ESwarmMovementCommitResult::InvalidStep;
		return;
	}
	FlightMovement->ConfigureBounds(Bounds, CollisionSphere->GetScaledSphereRadius(), BoundsMarginCm);
	FTransform Transform;
	FVector Extent;
	if (!BoidFlight::SafeBounds(Bounds, CollisionSphere->GetScaledSphereRadius(), BoundsMarginCm, Transform, Extent))
	{
		if (Observation) Observation->Result = ESwarmMovementCommitResult::InvalidBounds;
		return;
	}
	Velocity = (Velocity + DesiredAcceleration * DeltaTime).GetClampedToMaxSize(FMath::Max(0.0f, CruiseSpeed));
	const FVector Start = GetActorLocation();
	const FVector Delta = BoidFlight::ClampPosition(Start + Velocity * DeltaTime, Transform, Extent) - Start;
	if (Observation)
	{
		Observation->RequestedDelta = Velocity * DeltaTime;
		Observation->ClampedDelta = Delta;
		Observation->bBoundsClamped = !Delta.Equals(Observation->RequestedDelta, .001f);
	}
	// Authored flight space constrains actual movement as well as route planning.
	USwarmSimulationSubsystem* Simulation = GetSimulationSubsystem();
	const bool bAuthoredSpace = IsValid(Simulation) && Simulation->NavigationMode == EBoidNavigationMode::AuthoredRoutes;
	if (bAuthoredSpace && !Simulation->IsAllowedFlightSegment(Start, Start + Delta,
		CollisionSphere->GetScaledSphereRadius() + FMath::Max(0.f, Simulation->Tuning.ObstacleMarginCm)))
	{
		Velocity = FVector::ZeroVector;
		if (Observation) Observation->Result = ESwarmMovementCommitResult::FlightSpaceRejected;
		return;
	}
	FHitResult Hit;
	const bool bMoveReturned = FlightMovement->SafeMoveUpdatedComponent(Delta, GetActorQuat(), true, Hit);
	if (Observation)
	{
		Observation->bMoveReturned = bMoveReturned;
		Observation->bBlockingHit = Hit.bBlockingHit;
		Observation->bStartedPenetrating = Hit.bStartPenetrating;
		Observation->HitNormal = Hit.Normal;
		Observation->HitComponent = GetNameSafe(Hit.GetComponent());
		Observation->Result = Hit.bStartPenetrating ? ESwarmMovementCommitResult::StartedPenetrating : (Hit.bBlockingHit ? ESwarmMovementCommitResult::SweepBlocked : ESwarmMovementCommitResult::Completed);
	}
	if (bDead)
	{
		if (Observation) Observation->Result = ESwarmMovementCommitResult::Interrupted;
		return;
	}
	// A slide can generate additional segments; authored routes stop at contact
	// and let the runner select a new validated steering segment next step.
	if (Hit.bBlockingHit && !bAuthoredSpace)
	{
		FlightMovement->SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit);
		if (Observation && Hit.bBlockingHit)
		{
			Observation->HitNormal = Hit.Normal;
			Observation->HitComponent = GetNameSafe(Hit.GetComponent());
			Observation->bStartedPenetrating |= Hit.bStartPenetrating;
		}
	}
	if (bDead)
	{
		if (Observation) Observation->Result = ESwarmMovementCommitResult::Interrupted;
		return;
	}
	// Depenetration can reposition the sphere; it must not create unbounded velocity.
	Velocity = ((GetActorLocation() - Start) / DeltaTime).GetClampedToMaxSize(FMath::Max(0.0f, CruiseSpeed));
	if (!Velocity.IsNearlyZero())
	{
		Visual->SetWorldRotation(FMath::RInterpTo(Visual->GetComponentRotation(),
			Velocity.Rotation(), DeltaTime, FMath::Max(0.0f, VisualTurnSpeed)));
	}
	// Commanded heading deliberately survives a clipped move. Otherwise each
	// collision-adjusted velocity would erase the heading's accumulated turn.
	const FCollisionQueryParams Params = BoidMovementQuery(this);
	const FCollisionResponseParams Responses(CollisionSphere->GetCollisionResponseToChannels());
	if (BoidFlight::SafeBounds(Bounds, CollisionSphere->GetScaledSphereRadius(), BoundsMarginCm, Transform, Extent)
		&& BoidFlight::InsideBounds(GetActorLocation(), Transform, Extent)
		&& BoidFlight::IsClear(GetWorld(), GetActorLocation(),
			CollisionSphere->GetScaledSphereRadius() + BoidFlight::ContactSkinCm,
			CollisionSphere->GetCollisionObjectType(), Params, Responses))
	{
		LastValidWanderPosition = GetActorLocation();
		bHasLastValidWanderPosition = true;
	}
}

bool AFlyingBoid::ConstrainWanderToBounds(const UBoxComponent* Bounds, float BoundsMarginCm)
{
	if (bDead)
	{
		return false;
	}
	FTransform Transform;
	FVector Extent;
	const float Radius = CollisionSphere->GetScaledSphereRadius();
	if (!BoidFlight::SafeBounds(Bounds, Radius, BoundsMarginCm, Transform, Extent))
	{
		return false; // Registration reports an unusable volume once.
	}
	const FVector Start = GetActorLocation();
	if (BoidFlight::InsideBounds(Start, Transform, Extent))
	{
		return false;
	}
	const FVector Corrected = BoidFlight::ClampPosition(Start, Transform, Extent - FVector(BoidFlight::ContactSkinCm));
	const FCollisionQueryParams Params = BoidMovementQuery(this);
	const FCollisionResponseParams Responses(CollisionSphere->GetCollisionResponseToChannels());
	if (!BoidFlight::IsClear(GetWorld(), Corrected, Radius + BoidFlight::ContactSkinCm,
		CollisionSphere->GetCollisionObjectType(), Params, Responses))
	{
		return RecoverWanderFromStall(Bounds, BoundsMarginCm, FMath::Min(MaxSpeedCmPerSec, 100.0f));
	}
	SetActorLocation(Corrected, false, nullptr, ETeleportType::TeleportPhysics);
	FlightHeading = (Corrected - Start).GetSafeNormal();
	Velocity = FVector::ZeroVector;
	LastValidWanderPosition = Corrected;
	bHasLastValidWanderPosition = true;
	return true;
}

bool AFlyingBoid::RecoverWanderFromStall(const UBoxComponent* Bounds, float BoundsMarginCm, float CruiseSpeed, int32 MaxCandidates)
{
	if (bDead)
	{
		return false;
	}
	FTransform Transform;
	FVector Extent;
	const float Radius = CollisionSphere->GetScaledSphereRadius();
	if (!BoidFlight::SafeBounds(Bounds, Radius, BoundsMarginCm, Transform, Extent))
	{
		return false;
	}
	const FCollisionQueryParams Params = BoidMovementQuery(this);
	const FCollisionResponseParams Responses(CollisionSphere->GetCollisionResponseToChannels());
	const ECollisionChannel Channel = CollisionSphere->GetCollisionObjectType();
	TRACE_CPUPROFILER_EVENT_SCOPE(SwarmRecovery);
	TArray<FVector> RawDirections;
	RawDirections.Add(FlightHeading);
	RawDirections.Add(-FlightHeading);
	BoidFlight::AppendEscapeDirections(RawDirections);
	TArray<FSwarmAvoidanceCandidate> Prepared;
	for (const FVector& Direction : RawDirections) SwarmAvoidanceCandidates::AddDirection(Prepared, Direction);
	TArray<FVector> Directions;
	for (const FSwarmAvoidanceCandidate& Candidate : Prepared) Directions.Add(Candidate.Direction);
	const int32 DirectionLimit = MaxCandidates > 0 ? FMath::Min(MaxCandidates, Directions.Num()) : Directions.Num();
	const int32 PositionLimit = MaxCandidates > 0 ? MaxCandidates : MAX_int32;
	const int32 SearchPositionLimit = FMath::Max(1, PositionLimit - (bHasLastValidWanderPosition ? 1 : 0));
	int32 PositionsTested = 0;
	TArray<FHitResult> ScratchHits;
	// Rotate tie order per boid to avoid identical exits for a stopped group.
	auto FindEscape = [&](const FVector& Position, FVector& Direction)
	{
		float BestClearance = BoidFlight::ContactSkinCm;
		for (int32 Index = 0; Index < DirectionLimit; ++Index)
		{
			const FVector Candidate = Directions[(Index + FMath::Max(0, AgentId)) % Directions.Num()];
			if (USwarmSimulationSubsystem* Simulation = GetSimulationSubsystem()) ++Simulation->LastFrameRecoveryClearanceProbes;
			const float Clearance = BoidFlight::Clearance(GetWorld(), Bounds, Position, Radius,
				Candidate, 200.0f, BoundsMarginCm, 0.0f, Channel, Params, Responses, &ScratchHits);
			if (Clearance > BestClearance)
			{
				BestClearance = Clearance;
				Direction = Candidate;
			}
		}
		return BestClearance > BoidFlight::ContactSkinCm;
	};
	auto Restart = [&](const FVector& Direction)
	{
		FlightHeading = Direction;
		HoldAvoidanceDirection(Direction, 0.5f);
		Velocity = Direction * FMath::Min(100.0f, FMath::Min(FMath::Max(0.0f, CruiseSpeed),
			FMath::Max(0.0f, MaxSpeedCmPerSec)));
	};
	const FVector Origin = GetActorLocation();
	const bool bInside = BoidFlight::InsideBounds(Origin, Transform, Extent);
	const bool bEmbedded = !BoidFlight::IsClear(GetWorld(), Origin, Radius, Channel, Params, Responses);
	FVector Escape;
	if (bInside && !bEmbedded)
	{
		if (FindEscape(Origin, Escape))
		{
			Restart(Escape);
			return true;
		}
		return false; // Never teleport a clear sphere through a blocker.
	}

	const FVector Base = BoidFlight::ClampPosition(Origin, Transform, Extent - FVector(BoidFlight::ContactSkinCm));
	const float Offset = Radius * 2.0f + FMath::Max(0.0f, BoundsMarginCm);
	auto TryRelocate = [&](const FVector& Position)
	{
		if (PositionsTested >= PositionLimit) return false;
		++PositionsTested;
		if (!BoidFlight::InsideBounds(Position, Transform, Extent)
			|| !BoidFlight::IsClear(GetWorld(), Position, Radius + BoidFlight::ContactSkinCm,
				Channel, Params, Responses) || !FindEscape(Position, Escape))
		{
			return false;
		}
		SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
		Restart(Escape);
		LastValidWanderPosition = Position;
		bHasLastValidWanderPosition = true;
		return true;
	};
	if (TryRelocate(Base))
	{
		return true;
	}
	for (float Distance : { Offset, Offset * 1.5f, Offset * 2.0f })
	{
		if (PositionsTested >= SearchPositionLimit) break;
		for (const FVector& Direction : Directions)
		{
			if (PositionsTested >= SearchPositionLimit) break;
			if (TryRelocate(Base + Direction * Distance))
			{
				return true;
			}
		}
	}
	return bHasLastValidWanderPosition && TryRelocate(LastValidWanderPosition);
}


bool AFlyingBoid::RequestRangedPhase(EBoidRangedPhaseRequest Phase, uint64 OwnerToken, uint64 CombatToken)
{
	if (!IsAlive() || !OwnerToken || !CombatToken || CombatToken != MovementIntentOwnerToken
		|| MovementIntent.Policy != EBoidMovementPolicy::RangedOrbit
		|| MovementIntent.RangedPhaseControl != EBoidRangedPhaseControl::StateTree) return false;
	RequestedRangedPhase = Phase; RangedPhaseOwner = OwnerToken;
	return true;
}

void AFlyingBoid::ReleaseRangedPhase(uint64 OwnerToken)
{
	if (RangedPhaseOwner != OwnerToken) return;
	RequestedRangedPhase = EBoidRangedPhaseRequest::Approach;
	RangedPhaseOwner = 0;
}

void AFlyingBoid::PublishMovementConstraint(EBoidCombatMovementConstraint Constraint, float Speed, const FString& Reason)
{
	if (!IsAlive()) return;
	CombatMovementFacts.Constraint = Constraint;
	CombatMovementFacts.ActualSpeedCmPerSec = Speed;
	CombatMovementFacts.MovementReason = Reason;
}

FString AFlyingBoid::GetStateTreeAssetPath() const
{
	if (!IsValid(StateTree)) return TEXT("<none>");
	// Read the component's authored reference without changing or starting it.
	const FStructProperty* Property = FindFProperty<FStructProperty>(StateTree->GetClass(), TEXT("StateTreeRef"));
	if (!Property || Property->Struct != FStateTreeReference::StaticStruct()) return TEXT("<unavailable>");
	const FStateTreeReference* Reference = Property->ContainerPtrToValuePtr<FStateTreeReference>(StateTree.Get());
	const UStateTree* Asset = Reference ? Reference->GetStateTree() : nullptr;
	return IsValid(Asset) ? Asset->GetPathName() : TEXT("<none>");
}
