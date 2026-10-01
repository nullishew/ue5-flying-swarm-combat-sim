#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlyingBoid.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/** One physical flying agent. A runner supplies targets and calls StepTowards. */
UCLASS()
class FLYINGSWARMCOMBATSIM_API AFlyingBoid : public AActor
{
	GENERATED_BODY()

public:
	AFlyingBoid();

	/** Advance one bounded simulation step. The boid never ticks itself. */
	void StepTowards(const FVector& TargetLocation, float DeltaTime);
	FVector CalculateArrivalAcceleration(const FVector& SnapshotPosition, const FVector& SnapshotVelocity,
		const FVector& TargetLocation, float DeltaTime, float SpeedScale, float AccelerationScale,
		float BrakingScale) const;
	void ApplyAcceleration(const FVector& DesiredAcceleration, float DeltaTime, float SpeedScale);

	FVector GetFlightVelocity() const { return Velocity; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boid|Body")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boid|Body")
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boid|Movement", meta = (ClampMin = "0.0"))
	float MaxSpeedCmPerSec = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boid|Movement", meta = (ClampMin = "0.0"))
	float MaxAccelerationCmPerSec2 = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boid|Movement", meta = (ClampMin = "0.0"))
	float BrakingAccelerationCmPerSec2 = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boid|Movement", meta = (ClampMin = "0.0"))
	float ArrivalRadiusCm = 20.0f;

	/** Optional individual target. The runner's TargetActor is used when this is unset. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Boid|Movement")
	TObjectPtr<AActor> MovementTarget;

private:
	FVector Velocity = FVector::ZeroVector;
};
