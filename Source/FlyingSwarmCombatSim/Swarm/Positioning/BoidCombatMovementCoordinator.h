#pragma once

#include "CoreMinimal.h"
#include "Swarm/Positioning/BoidCombatMovementTypes.h"
#include "Swarm/Routing/BoidNavigationTypes.h"

class AActor;
class AFlyingBoid;
class USwarmSimulationSubsystem;
class FBoidRangedPositionPlanner;
struct FBoidPlannerAgent;
struct FBoidRangedSelection;

/** Coordinates ranged selection, orbit, close-range recovery, and readiness per boid. */
class FBoidCombatMovementCoordinator
{
public:
	void Update(USwarmSimulationSubsystem& Runner, TConstArrayView<FBoidPlannerAgent> Agents,
		FBoidRangedPositionPlanner& Planner, float DeltaTime);
	FBoidNavigationGoal GetNavigation(int32 AgentId, const AActor* Target) const;
	void FinalizeNavigation(USwarmSimulationSubsystem& Runner, TConstArrayView<FBoidPlannerAgent> Agents,
		float DeltaTime);
	void ValidateReadinessAfterMovement(USwarmSimulationSubsystem& Runner,
		TConstArrayView<FBoidPlannerAgent> Agents, float DeltaTime);
	void ResetAgent(int32 AgentId);
	void ResetPosition(int32 AgentId);

private:
	struct FAgentState
	{
		struct FOrbitControlState
		{
			int32 SelectionVersion = INDEX_NONE;
			int32 LastDirection = 1;
			float StableSeconds = 0.f;
			float ClearProofAgeSeconds = 0.f;
			FVector ClearProofStart = FVector::ZeroVector;
			FVector ClearProofGoal = FVector::ZeroVector;
			FVector ClearProofCenter = FVector::ZeroVector;
			int32 ClearProofSelectionVersion = INDEX_NONE;
			bool bHasClearProof = false;
			bool bWasOrbiting = false;
		};
		struct FRangeRecoveryState
		{
			FVector Goal = FVector::ZeroVector;
			FVector Center = FVector::ZeroVector;
			FVector Direction = FVector::ZeroVector;
			bool bActive = false;
		};
		struct FReadinessState
		{
			float ElapsedSeconds = 0.f;
			bool bPendingBudget = false;
		};
		TWeakObjectPtr<AActor> Target;
		FBoidNavigationGoal Navigation;
		FBoidCombatMovementFacts Facts;
		FOrbitControlState Orbit;
		FRangeRecoveryState Recovery;
		FReadinessState Readiness;
		uint64 LastOwnerToken = 0;
		uint32 LastIntentRevision = 0;
		float HoldSeconds = 0.f;
		bool bHasTargetCenter = false;
		bool bWarnedHold = false;
		bool bTrackHistory = false;
		uint32 ControlSignature = 0;
	};

	bool HasBudget(const USwarmSimulationSubsystem& Runner) const;
	bool HasMovementBudget(const USwarmSimulationSubsystem& Runner) const;
	bool SweepClear(USwarmSimulationSubsystem& Runner, AFlyingBoid& Boid, AActor& Target,
		const FVector& Start, const FVector& End);
	bool FindSafeEscapeGoal(USwarmSimulationSubsystem& Runner, AFlyingBoid& Boid, AActor& Target,
		const FVector& Center, float EscapeRange, TConstArrayView<FVector> Directions,
		FVector& OutGoal, FVector& OutDirection, bool& bPendingBudget, int32& OutRejectedDirections);
	void Publish(USwarmSimulationSubsystem& Runner, AFlyingBoid& Boid, FAgentState& State,
		const FBoidRangedSelection* Selection, bool bReady);
	void ValidateReadinessForAgent(USwarmSimulationSubsystem& Runner, AFlyingBoid& Boid,
		FAgentState& State, float DeltaTime);
	void RecordChange(FAgentState& State, const FString& Message);
	void WarnIfHeld(const AFlyingBoid& Boid, FAgentState& State, bool bHeld, float DeltaTime);

	TMap<int32, FAgentState> RuntimeByAgent;
	int32 FairCursor = 0;
	int32 ReadinessCursor = 0;
	int32 ReadinessReserve = 0;
	TSet<FString> WarnedConfiguration;
};
