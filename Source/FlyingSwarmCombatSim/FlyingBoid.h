#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoidSteeringBehavior.h"
#include "FlyingBoid.generated.h"

class UBoxComponent;
class USphereComponent;
class UStaticMeshComponent;
class UStateTreeComponent;
class UBoidFlightMovementComponent;
class UBoidSteeringProfile;
struct FSwarmTuningSettings;

/** Physical flying agent. StateTree selects intent; the runner owns movement. */
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
	void ApplyWanderAcceleration(const FVector& DesiredAcceleration, float DeltaTime, float CruiseSpeed,
		const UBoxComponent* Bounds, float BoundsMarginCm, float VisualTurnSpeed);
	bool ConstrainWanderToBounds(const UBoxComponent* Bounds, float BoundsMarginCm);
	bool RecoverWanderFromStall(const UBoxComponent* Bounds, float BoundsMarginCm, float CruiseSpeed);
	void InitializeWander(int32 InAgentId, int32 InitialSeed);
	FVector TurnFlightHeading(const FVector& DesiredDirection, float MaxTurnRateDeg, float DeltaTime);
	void HoldAvoidanceDirection(const FVector& Direction, float Seconds);
	FVector GetHeldAvoidanceDirection() const { return HeldAvoidanceDirection; }
	float GetAvoidanceHoldSeconds() const { return AvoidanceHoldSeconds; }
	FVector GetFlightHeading() const { return FlightHeading; }
	void SetSteeringProfile(UBoidSteeringProfile* Profile);
	void SetWanderActive(bool bActive) { bSteeringActive = bActive; if (!bActive) ActiveSteeringProfile = nullptr; }
	bool IsWanderActive() const { return bSteeringActive; } // Kept for source/asset compatibility.
	bool IsSteeringActive() const { return bSteeringActive; }
	UBoidSteeringProfile* GetSteeringProfile() const { return ActiveSteeringProfile; }
	void PrepareSteeringRuntime(UBoidSteeringProfile* Profile);
	FBoidSteeringRuntimeState& GetSteeringRuntimeState(int32 BehaviorIndex);

	FVector GetFlightVelocity() const { return Velocity; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boid|Body")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boid|Body")
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boid|Behavior")
	TObjectPtr<UStateTreeComponent> StateTree;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boid|Movement")
	TObjectPtr<UBoidFlightMovementComponent> FlightMovement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boid|Movement", meta = (ClampMin = "0.0"))
	float MaxSpeedCmPerSec = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boid|Movement", meta = (ClampMin = "0.0"))
	float MaxAccelerationCmPerSec2 = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boid|Movement", meta = (ClampMin = "0.0"))
	float BrakingAccelerationCmPerSec2 = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boid|Movement", meta = (ClampMin = "0.0"))
	float ArrivalRadiusCm = 20.0f;

	/** Reserved for later routed/combat states; Wander does not read this target. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Boid|Movement")
	TObjectPtr<AActor> MovementTarget;

private:
	FVector Velocity = FVector::ZeroVector;
	FVector FlightHeading = FVector::ForwardVector;
	FVector HeldAvoidanceDirection = FVector::ZeroVector;
	FVector LastValidWanderPosition = FVector::ZeroVector;
	bool bHasLastValidWanderPosition = false;
	float AvoidanceHoldSeconds = 0.0f;
	int32 AgentId = INDEX_NONE;
	int32 SimulationSeed = 0;
	bool bSteeringActive = false;
	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> ActiveSteeringProfile;
	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> RuntimeProfile;
	TArray<FBoidSteeringRuntimeState> SteeringRuntimeStates;
};
