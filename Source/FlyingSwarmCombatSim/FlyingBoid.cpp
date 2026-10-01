#include "FlyingBoid.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AFlyingBoid::AFlyingBoid()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->InitSphereRadius(30.0f);
	CollisionSphere->SetCollisionProfileName(TEXT("Pawn"));
	CollisionSphere->SetGenerateOverlapEvents(false);

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(CollisionSphere);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetRelativeScale3D(FVector(0.6f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Visual->SetStaticMesh(SphereMesh.Object);
	}
}

void AFlyingBoid::StepTowards(const FVector& TargetLocation, float DeltaTime)
{
	if (DeltaTime <= 0.0f || !FMath::IsFinite(DeltaTime))
	{
		return;
	}

	const FVector ToTarget = TargetLocation - GetActorLocation();
	const float Distance = ToTarget.Size();
	const float MaxSpeed = FMath::Max(0.0f, MaxSpeedCmPerSec);
	const float Acceleration = FMath::Max(0.0f, MaxAccelerationCmPerSec2);
	const float Braking = FMath::Max(0.0f, BrakingAccelerationCmPerSec2);
	const float ArrivalRadius = FMath::Max(0.0f, ArrivalRadiusCm);

	// The braking-distance speed cap lets the boid slow before the goal.
	const float RemainingDistance = FMath::Max(0.0f, Distance - ArrivalRadius);
	const float DesiredSpeed = FMath::Min(MaxSpeed, FMath::Sqrt(2.0f * Braking * RemainingDistance));
	const FVector DesiredVelocity = Distance > UE_SMALL_NUMBER
		? ToTarget / Distance * DesiredSpeed
		: FVector::ZeroVector;
	const float AccelerationLimit = DesiredSpeed < Velocity.Size() ? Braking : Acceleration;
	const FVector VelocityChange = (DesiredVelocity - Velocity).GetClampedToMaxSize(AccelerationLimit * DeltaTime);
	Velocity = (Velocity + VelocityChange).GetClampedToMaxSize(MaxSpeed);

	if (RemainingDistance <= UE_SMALL_NUMBER && Velocity.SizeSquared() < FMath::Square(5.0f))
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
