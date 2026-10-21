#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/BoidHealthTypes.h"
#include "Combat/BoidAttackTypes.h"
#include "Swarm/BoidIntentTypes.h"
#include "Swarm/Positioning/BoidRangedPositioning.h"
#include "Swarm/Routing/BoidNavigationTypes.h"
#include "Swarm/Positioning/BoidCombatMovementTypes.h"
#include "Swarm/Steering/BoidSteeringBehavior.h"
#include "Engine/TimerHandle.h"
#include "FlyingBoid.generated.h"

class AFlyingCharacter;
class USwarmSimulationSubsystem;
class UBoxComponent;
class USphereComponent;
class UStaticMeshComponent;
class UStateTreeComponent;
class UBoidFlightMovementComponent;
class UBoidAttackComponent;
class UBoidSteeringProfile;

/** Public, reflected view of the boid's current player-sensing state. */
USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FBoidTargetFacts
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Targeting")
	TObjectPtr<AActor> TargetActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Targeting")
	FVector LastKnownPosition = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Targeting", meta = (Units = "cm"))
	float DistanceCm = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Targeting")
	bool bDetected = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Targeting")
	bool bHasSight = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Targeting")
	bool bAttackReady = false;
};

/** Physical flying agent. StateTree selects intent; the world simulation subsystem owns movement. */
UCLASS()
class FLYINGSWARMCOMBATSIM_API AFlyingBoid : public AActor
{
	GENERATED_BODY()

public:
	AFlyingBoid();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

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
	void SetWanderActive(bool bActive);
	bool IsWanderActive() const { return bSteeringActive; } // Kept for source/asset compatibility.
	bool IsSteeringActive() const { return bSteeringActive; }
	UBoidSteeringProfile* GetSteeringProfile() const { return ActiveSteeringProfile; }
	void PrepareSteeringRuntime(UBoidSteeringProfile* Profile);
	FBoidSteeringRuntimeState& GetSteeringRuntimeState(int32 BehaviorIndex);

	void UpdateTargetSensing(float ElapsedSeconds);
	UFUNCTION(BlueprintPure, Category = "Boid|Targeting")
	FBoidTargetFacts GetTargetFacts() const { return TargetFacts; }
	UFUNCTION(BlueprintPure, Category = "Boid|Positioning")
	FBoidPositioningFacts GetPositioningFacts() const { return PositioningFacts; }
	UFUNCTION(BlueprintPure, Category = "Boid|Attack")
	FBoidAttackFacts GetAttackFacts() const;
	bool RequestRangedPhase(EBoidRangedPhaseRequest Phase, uint64 OwnerToken, uint64 CombatToken);
	void ReleaseRangedPhase(uint64 OwnerToken);
	EBoidRangedPhaseRequest GetRequestedRangedPhase() const { return RequestedRangedPhase; }
	uint64 GetRangedPhaseOwner() const { return RangedPhaseOwner; }
	void PublishPositioning(const FBoidPositioningFacts& Facts, bool bReady);
	void PublishCombatMovement(const FBoidCombatMovementFacts& Facts) { CombatMovementFacts = IsAlive() ? Facts : FBoidCombatMovementFacts(); }
	UFUNCTION(BlueprintPure, Category="Boid|Combat Movement") FBoidCombatMovementFacts GetCombatMovementFacts() const { return CombatMovementFacts; }
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Boid|Combat Movement") FBoidCombatMovementFacts CombatMovementFacts;
	void PublishMovementConstraint(EBoidCombatMovementConstraint Constraint, float Speed, const FString& Reason);
	FString GetStateTreeAssetPath() const;
	int32 GetSimulationSeed() const { return SimulationSeed; }
	void PublishNavigation(const FBoidNavigationFacts& Facts) { NavigationFacts = IsAlive() ? Facts : FBoidNavigationFacts(); }
	UFUNCTION(BlueprintPure, Category="Boid|Navigation") FBoidNavigationFacts GetNavigationFacts() const { return NavigationFacts; }
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Boid|Navigation") FBoidNavigationFacts NavigationFacts;
	UFUNCTION(BlueprintPure, Category = "Boid|Health")
	bool IsAlive() const { return !bDead && CurrentHealth > 0.0f; }
	void SetMovementIntent(const FBoidMovementIntent& Intent, uint64 OwnerToken = 0);
	UFUNCTION(BlueprintCallable, Category = "Boid|Movement")
	void RequestMovementIntent(const FBoidMovementIntent& Intent) { SetMovementIntent(Intent, 0); }
	FBoidMovementIntent GetMovementIntent() const { return MovementIntent; }
	uint64 GetMovementIntentOwner() const { return MovementIntentOwnerToken; }
	uint64 AcquireIntentToken();
	void SetMovementTarget(AActor* Target, uint64 OwnerToken = 0);
	UFUNCTION(BlueprintSetter)
	void SetMovementTargetFromBlueprint(AActor* Target) { SetMovementTarget(Target, 0); }
	uint64 GetMovementTargetOwner() const { return MovementTargetOwnerToken; }
	UFUNCTION(BlueprintPure, Category="Boid|Simulation")
	USwarmSimulationSubsystem* GetSimulationSubsystem() const;

	FVector GetFlightVelocity() const { return Velocity; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boid|Body")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boid|Body")
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boid|Behavior")
	TObjectPtr<UStateTreeComponent> StateTree;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boid|Movement")
	TObjectPtr<UBoidFlightMovementComponent> FlightMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boid|Attack")
	TObjectPtr<UBoidAttackComponent> AttackComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boid|Movement", meta = (ClampMin = "0.0"))
	float MaxSpeedCmPerSec = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boid|Movement", meta = (ClampMin = "0.0"))
	float MaxAccelerationCmPerSec2 = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boid|Movement", meta = (ClampMin = "0.0"))
	float BrakingAccelerationCmPerSec2 = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boid|Movement", meta = (ClampMin = "0.0"))
	float ArrivalRadiusCm = 20.0f;

	/** Actor pursued by Seek profiles; assigned per boid or through the StateTree Seek Target task. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, BlueprintSetter = SetMovementTargetFromBlueprint, Category = "Boid|Movement")
	TObjectPtr<AActor> MovementTarget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boid|Targeting", meta = (ClampMin = "0.0", Units = "cm"))
	float DetectionRadiusCm = 2000.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boid|Targeting", meta = (ClampMin = "0.0", Units = "cm"))
	float LoseRadiusCm = 2400.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boid|Targeting", meta = (ClampMin = "0.0", Units = "s"))
	float LostSightGraceSeconds = 1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boid|Targeting", Transient)
	FBoidTargetFacts TargetFacts;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boid|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 30.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boid|Health", Transient)
	float CurrentHealth = 30.0f;
	UPROPERTY(BlueprintAssignable, Category = "Boid|Health")
	FBoidHealthChangedDelegate OnHealthChanged;
	UPROPERTY(BlueprintAssignable, Category = "Boid|Health")
	FBoidDeathDelegate OnDeath;

private:
    friend class USwarmSimulationSubsystem;
    bool AssociateSimulationSubsystem(USwarmSimulationSubsystem* Simulation);
    void ClearSimulationSubsystem(USwarmSimulationSubsystem* Simulation);
	EBoidRangedPhaseRequest RequestedRangedPhase = EBoidRangedPhaseRequest::Approach;
	uint64 RangedPhaseOwner = 0;
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
	bool bDead = false;
	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> ActiveSteeringProfile;
	UPROPERTY(Transient)
	TObjectPtr<UBoidSteeringProfile> RuntimeProfile;
	TArray<FBoidSteeringRuntimeState> SteeringRuntimeStates;
	TWeakObjectPtr<AFlyingCharacter> SensedTarget;
	TWeakObjectPtr<USwarmSimulationSubsystem> SimulationSubsystem;
	FBoidMovementIntent MovementIntent;
	FBoidPositioningFacts PositioningFacts;
	uint64 MovementIntentOwnerToken = 0;
	uint64 MovementTargetOwnerToken = 0;
	uint64 NextIntentToken = 0;
	float SecondsSinceLastSight = 0.0f;
	bool bIsProcessingDamage = false;

	FTimerHandle DeathVisualTimer;
	void HideDeadVisuals();
	void HandleDeath();
};
