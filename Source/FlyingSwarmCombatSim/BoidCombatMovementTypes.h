#pragma once
#include "CoreMinimal.h"
#include "BoidIntentTypes.h"
#include "BoidNavigationTypes.h"
#include "BoidCombatMovementTypes.generated.h"

UENUM(BlueprintType)
enum class EBoidCombatControlStatus : uint8 { Ready, PendingBudget, Blocked, NeedsApproach };
UENUM(BlueprintType)
enum class EBoidCombatHoldReason : uint8 { None, InvalidTarget, InvalidOwnership, SteeringDisabled, InvalidProfile, NoSelection, PendingRoute, PendingBudget, OrbitBlocked, RetreatBlocked, ZeroSpeed };
UENUM(BlueprintType)
enum class EBoidCombatMovementConstraint : uint8 { None, Arrival, Hold, SpeedLimit, Obstacle, EmergencySeparation };

/** Observations only. Controller, selection and steering runtime stay per agent. */
USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FBoidCombatMovementFacts
{
    GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") EBoidCombatControlStatus Status = EBoidCombatControlStatus::NeedsApproach;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") EBoidCombatHoldReason HoldReason = EBoidCombatHoldReason::None;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") EBoidCombatMovementConstraint Constraint = EBoidCombatMovementConstraint::None;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") EBoidRangedPhaseRequest RequestedPhase = EBoidRangedPhaseRequest::Approach;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") EBoidRangedPhaseControl PhaseControl = EBoidRangedPhaseControl::Automatic;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") EBoidNavigationPhase ExecutedPhase = EBoidNavigationPhase::None;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") bool bHasSelection = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") bool bCanOrbit = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") bool bOrbiting = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") bool bRetreating = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") bool bReadinessEligible = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") bool bAttackReady = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") FVector Goal = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") float TargetRangeCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") float DesiredRangeCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") float MinimumRangeCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") float MaximumRangeCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") float RangeErrorCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") float OrbitRadiusCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") float RadialErrorCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") float HeightErrorCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") float DesiredSpeedCmPerSec = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") float ActualSpeedCmPerSec = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") float HoldSeconds = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") float SearchAgeSeconds = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") int32 ControlQueriesUsed = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") int32 PredictionSegments = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") int32 SelectionVersion = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") int32 CandidatesEvaluated = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") int32 CandidateCount = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") int32 Rebases = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") int32 Rejections = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") int32 StaleResults = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") FString Detail;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") FString EffectiveProfile;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") FString BlueprintClass;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") FString StateTreeAsset;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") FString SelectionReason;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") FString OrbitReason;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") FString RouteReason;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") FString MovementReason;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat Movement") TArray<FString> RecentChanges;
};
