#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/BoidAttackTypes.h"
#include "Engine/EngineTypes.h"
#include "BoidAttackComponent.generated.h"

class AFlyingBoidAttackProjectile;
class USceneComponent;
class ASwarmSimulationRunner;

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
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Attack") FBoidAttackFacts AttackFacts;

	bool ActivateCombatContext(uint64 OwnerToken);
	uint64 GetCombatContextOwner() const { return ContextOwnerToken; }
	void DeactivateCombatContext(uint64 OwnerToken);
	const FBoidAttackFacts& GetAttackFacts() const { return AttackFacts; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY(Transient)
	TSubclassOf<AFlyingBoidAttackProjectile> SnapshottedProjectileClass;
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
	TWeakObjectPtr<ASwarmSimulationRunner> TickRunner;
	double NextAttemptTime = 0.0;

	void ResetCadence(EBoidAttackRejection Reason);
	void RefreshTickPrerequisites();
	bool ValidateMuzzleAndLaunch(const FVector& MuzzleLocation, const FVector& Direction, AActor* Target) const;
};
