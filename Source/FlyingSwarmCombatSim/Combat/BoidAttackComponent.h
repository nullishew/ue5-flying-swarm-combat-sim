#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/BoidAttackTypes.h"
#include "Engine/EngineTypes.h"
#include "BoidAttackComponent.generated.h"

class AFlyingBoidAttackProjectile;
class USceneComponent;
class USwarmSimulationSubsystem;
struct FCollisionQueryParams;

/** Fires while enabled by the Combat StateTree task and ranged movement is active; never moves the boid or completes a StateTree state. */
UCLASS(ClassGroup=(Boid), meta=(BlueprintSpawnableComponent))
class FLYINGSWARMCOMBATSIM_API UBoidAttackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBoidAttackComponent();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack") TSubclassOf<AFlyingBoidAttackProjectile> ProjectileClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.01", Units="s")) float AttackIntervalSeconds = 0.75f;
	/** Fire during approach/retreat without waiting for orbit readiness; zero disables this branch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0", Units="cm")) float ApproachAttackDistanceCm = 2000.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0", Units="cm")) float CloseAttackDistanceCm = 100.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Muzzle", meta=(UseComponentPicker, AllowedClasses="/Script/Engine.SceneComponent")) FComponentReference MuzzleReference;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Muzzle") FName MuzzleSocketName;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Muzzle", meta=(ClampMin="0.01", Units="cm")) float LaunchClearanceDistanceCm = 30.f;
	/** Zero restores direct shots; one uses the full bounded intercept. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Prediction", meta=(ClampMin="0", ClampMax="1")) float PredictionStrength = 1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Prediction", meta=(ClampMin="0", Units="s")) float MaximumPredictionSeconds = 1.25f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Fan") bool bEnableFans = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Fan", meta=(ClampMin="3", ClampMax="9", Delta="2")) int32 FanProjectileCount = 5;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Fan", meta=(ClampMin="0", ClampMax="180", Units="deg")) float FanTotalAngleDegrees = 24.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Fan", meta=(ClampMin="0", Units="cm")) float MinimumFanDistanceCm = 400.f;
	/** Total fan damage relative to one normal bullet, divided among configured bullets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Fan", meta=(ClampMin="0", ClampMax="10")) float FanTotalDamageMultiplier = 1.5f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Fan", meta=(ClampMin="0", ClampMax="1")) float MinimumFanProbability = 0.10f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Fan", meta=(ClampMin="0", ClampMax="1")) float MaximumFanProbability = 0.30f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Timing", meta=(ClampMin="0", ClampMax="0.95")) float CadenceJitterFraction = 0.15f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Attack") FBoidAttackFacts AttackFacts;

	bool ActivateCombatContext(uint64 OwnerToken);
	uint64 GetCombatContextOwner() const { return ContextOwnerToken; }
	void DeactivateCombatContext(uint64 OwnerToken);
	const FBoidAttackFacts& GetAttackFacts() const { return AttackFacts; }
	void SuspendSimulation();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;

private:
	UPROPERTY(Transient)
	TSubclassOf<AFlyingBoidAttackProjectile> SnapshottedProjectileClass;
	float SnapshottedPredictionStrength = 1.f;
	float SnapshottedMaximumPredictionSeconds = 1.25f;
	bool bSnapshottedEnableFans = true;
	int32 SnapshottedFanCount = 5;
	float SnapshottedFanAngle = 24.f;
	float SnapshottedMinimumFanDistance = 400.f;
	float SnapshottedFanDamageMultiplier = 1.5f;
	float SnapshottedMinimumFanProbability = 0.10f;
	float SnapshottedMaximumFanProbability = 0.30f;
	float SnapshottedCadenceJitter = 0.15f;
	FRandomStream AttackRandom;
	bool bRandomInitialized = false;
	float AssignedFanProbability = 0.f;
	float SnapshottedInterval = 0.75f;
	float SnapshottedApproachDistance = 2000.f;
	float SnapshottedCloseDistance = 100.f;
	float SnapshottedClearance = 30.f;
	float SnapshottedProjectileRadius = 0.f;
	float SnapshottedProjectileSpeed = 0.f;
	float SnapshottedProjectileDamage = 0.f;
	float SnapshottedProjectileLifespan = 0.f;
	TWeakObjectPtr<USceneComponent> SnapshottedMuzzleComponent;
	FName SnapshottedMuzzleSocketName;
	bool bConfigurationValid = false;
	uint64 ContextOwnerToken = 0;
	TWeakObjectPtr<AActor> CadenceTarget;
	TWeakObjectPtr<USwarmSimulationSubsystem> SimulationSubscription;
	FDelegateHandle SimulationHandle;
	double NextAttemptTime = 0.0;

	bool InitializeRandomState();
	void ResetCadence(EBoidAttackRejection Reason);
	void RefreshSimulationSubscription();
	void OnSimulationFrame(float DeltaTime);
	void DetachSimulation();
	bool ValidateMuzzle(const FVector& MuzzleLocation, const FCollisionQueryParams& Params) const;
	bool ValidateLaunch(const FVector& MuzzleLocation, const FVector& Direction, AActor* Target, const FCollisionQueryParams& Params) const;
};
