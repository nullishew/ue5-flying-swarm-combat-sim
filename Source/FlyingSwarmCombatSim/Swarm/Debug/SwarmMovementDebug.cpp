#include "Swarm/Debug/SwarmMovementDebug.h"

#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "Swarm/FlyingBoid.h"
#include "Engine/World.h"
#include "EngineGlobals.h"

void USwarmSimulationSubsystem::SetDebugObservedAgent(UObject* Observer, AFlyingBoid* Boid)
{
	if (!IsValid(Observer)) return;
	if (IsValid(Boid) && Boid->IsAlive() && IsAgentRegistered(Boid)) DebugObservers.FindOrAdd(Observer) = Boid;
	else DebugObservers.Remove(Observer);
	PruneDebugObservations();
}

void USwarmSimulationSubsystem::PruneDebugObservations()
{
	TSet<int32> ObservedIds;
	for (auto It = DebugObservers.CreateIterator(); It; ++It)
	{
		AFlyingBoid* Boid = It.Value().Get();
		if (!It.Key().IsValid() || !IsValid(Boid) || !Boid->IsAlive() || !IsAgentRegistered(Boid)) It.RemoveCurrent();
		else ObservedIds.Add(GetAgentId(Boid));
	}
	for (auto It = DebugMovementSamples.CreateIterator(); It; ++It)
	{
		if (!ObservedIds.Contains(It.Key())) It.RemoveCurrent();
	}
	for (int32 Id : ObservedIds) DebugMovementSamples.FindOrAdd(Id);
}

bool USwarmSimulationSubsystem::GetDebugMovementSnapshot(const AFlyingBoid* Boid, FSwarmMovementDebugSnapshot& OutSnapshot) const
{
	OutSnapshot = FSwarmMovementDebugSnapshot();
	if (!IsValid(Boid) || !Boid->IsAlive() || !IsAgentRegistered(Boid)) return false;
	const FSwarmMovementDebugSnapshot* Sample = DebugMovementSamples.Find(GetAgentId(Boid));
	if (!Sample) return false;
	OutSnapshot = *Sample;
	return Sample->bHasSample;
}

void USwarmSimulationSubsystem::BeginDebugMovementStep(float StepTime)
{
	++DebugStepNumber;
	for (auto& Entry : DebugMovementSamples)
	{
		FSwarmMovementDebugSnapshot& Sample = Entry.Value;
		Sample = FSwarmMovementDebugSnapshot();
		FRegisteredAgent* Record = FindAgentRecord(Entry.Key);
		AFlyingBoid* Boid = Record ? Record->Boid.Get() : nullptr;
		if (!IsValid(Boid) || !Boid->IsAlive()) continue;
		Sample.bHasSample = true;
		Sample.FrameNumber = GFrameCounter;
		Sample.StepNumber = DebugStepNumber;
		Sample.WorldSeconds = GetWorld()->GetTimeSeconds();
		Sample.StepSeconds = StepTime;
		Sample.StartPosition = Boid->GetActorLocation();
		Sample.RequestedPolicy = Boid->GetMovementIntent().Policy;
		Sample.bSteeringActive = Boid->IsSteeringActive();
		Sample.Stage = Record->bLogicStarted ? TEXT("STEERING NOT EXECUTED") : TEXT("LOGIC NOT STARTED");
		Sample.SightLossAge = Boid->SecondsSinceLastSight;
		Sample.SightLossGrace = FMath::Max(0.f, Boid->LostSightGraceSeconds);
		Sample.RecoveryCountAtStart = Record->RecoveryCount;
		if (const FNavigationAgentState* Navigation = NavigationAgents.Find(Entry.Key))
		{
			if (Navigation->LastSeenAt >= 0.)
			{
				Sample.MemoryAge = FMath::Max(0.f, static_cast<float>(NavigationTime - Navigation->LastSeenAt));
				Sample.MemoryRemaining = FMath::Max(0.f, LastKnownPursuitSeconds - Sample.MemoryAge);
			}
		}
	}
}

void USwarmSimulationSubsystem::FinishDebugMovementStep()
{
	for (auto& Entry : DebugMovementSamples)
	{
		FSwarmMovementDebugSnapshot& Sample = Entry.Value;
		if (!Sample.bHasSample || Sample.StepNumber != DebugStepNumber) continue;
		const FRegisteredAgent* Record = FindAgentRecord(Entry.Key);
		AFlyingBoid* Boid = Record ? Record->Boid.Get() : nullptr;
		if (!IsValid(Boid) || !Boid->IsAlive() || !IsAgentRegistered(Boid))
		{
			Sample.bHasSample = false;
			continue;
		}
		Sample.ActualDelta = Boid->GetActorLocation() - Sample.StartPosition;
		Sample.ActualSpeed = Sample.ActualDelta.Size() / Sample.StepSeconds;
		Sample.FlightHeading = Boid->GetFlightHeading();
		Sample.StalledSeconds = Record->NoProgressSeconds;
		Sample.RecoveryCount = Record->RecoveryCount;
		Sample.bRecoveryAttempted = Record->RecoveryCount > Sample.RecoveryCountAtStart;
	}
}
