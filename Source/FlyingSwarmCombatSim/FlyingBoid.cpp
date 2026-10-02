#include "FlyingBoid.h"

#include "BoidFlightAlgorithms.h"
#include "BoidFlightMovementComponent.h"
#include "BoidSteeringProfile.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/World.h"
#include "Misc/Crc.h"
#include "UObject/ConstructorHelpers.h"

AFlyingBoid::AFlyingBoid()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->InitSphereRadius(30.0f);
	CollisionSphere->SetCollisionProfileName(TEXT("Pawn"));
	// Local separation owns boid spacing; their sweeps must not block one another.
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	CollisionSphere->SetGenerateOverlapEvents(false);

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
}

void AFlyingBoid::StepTowards(const FVector& TargetLocation, float DeltaTime)
{
	if (DeltaTime <= 0.0f || !FMath::IsFinite(DeltaTime))
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
	if (DeltaTime <= 0.0f || !FMath::IsFinite(DeltaTime))
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
	// Reapply invariants after Blueprint defaults have been loaded.
	CollisionSphere->SetSimulatePhysics(false);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_Pawn);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
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
}

void AFlyingBoid::SetSteeringProfile(UBoidSteeringProfile* Profile)
{
	ActiveSteeringProfile = Profile;
	bSteeringActive = true;
	PrepareSteeringRuntime(Profile);
}

void AFlyingBoid::PrepareSteeringRuntime(UBoidSteeringProfile* Profile)
{
	if (!Profile || RuntimeProfile == Profile)
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
	float CruiseSpeed, const UBoxComponent* Bounds, float BoundsMarginCm, float VisualTurnSpeed)
{
	if (DeltaTime <= 0.0f || !FMath::IsFinite(DeltaTime))
	{
		return;
	}
	FlightMovement->ConfigureBounds(Bounds, CollisionSphere->GetScaledSphereRadius(), BoundsMarginCm);
	FTransform Transform;
	FVector Extent;
	if (!BoidFlight::SafeBounds(Bounds, CollisionSphere->GetScaledSphereRadius(), BoundsMarginCm, Transform, Extent))
	{
		return;
	}
	Velocity = (Velocity + DesiredAcceleration * DeltaTime).GetClampedToMaxSize(FMath::Max(0.0f, CruiseSpeed));
	const FVector Start = GetActorLocation();
	const FVector Delta = BoidFlight::ClampPosition(Start + Velocity * DeltaTime, Transform, Extent) - Start;
	FHitResult Hit;
	FlightMovement->SafeMoveUpdatedComponent(Delta, GetActorQuat(), true, Hit);
	if (Hit.bBlockingHit)
	{
		FlightMovement->SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit);
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

bool AFlyingBoid::RecoverWanderFromStall(const UBoxComponent* Bounds, float BoundsMarginCm, float CruiseSpeed)
{
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
	TArray<FVector> Directions;
	Directions.Add(FlightHeading);
	Directions.Add(-FlightHeading);
	BoidFlight::AppendEscapeDirections(Directions);
	// Rotate tie order per boid to avoid identical exits for a stopped group.
	auto FindEscape = [&](const FVector& Position, FVector& Direction)
	{
		float BestClearance = BoidFlight::ContactSkinCm;
		for (int32 Index = 0; Index < Directions.Num(); ++Index)
		{
			const FVector Candidate = Directions[(Index + AgentId) % Directions.Num()];
			const float Clearance = BoidFlight::Clearance(GetWorld(), Bounds, Position, Radius,
				Candidate, 200.0f, BoundsMarginCm, 0.0f, Channel, Params, Responses);
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
		for (const FVector& Direction : Directions)
		{
			if (TryRelocate(Base + Direction * Distance))
			{
				return true;
			}
		}
	}
	return bHasLastValidWanderPosition && TryRelocate(LastValidWanderPosition);
}
