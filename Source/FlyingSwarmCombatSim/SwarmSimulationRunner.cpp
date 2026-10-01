#include "SwarmSimulationRunner.h"

#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "FlyingBoid.h"

namespace
{
	struct FBoidSnapshot
	{
		TWeakObjectPtr<AFlyingBoid> Boid;
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector Target = FVector::ZeroVector;
		bool bHasTarget = false;
	};
}

ASwarmSimulationRunner::ASwarmSimulationRunner()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASwarmSimulationRunner::BeginPlay()
{
	Super::BeginPlay();
	RandomStream.Initialize(Tuning.InitialSeed);

	// Sort placed agents so their IDs do not depend on actor iteration order.
	TArray<AFlyingBoid*> PlacedBoids;
	for (TActorIterator<AFlyingBoid> It(GetWorld()); It; ++It)
	{
		PlacedBoids.Add(*It);
	}
	PlacedBoids.Sort([](const AFlyingBoid& Left, const AFlyingBoid& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});
	for (AFlyingBoid* Boid : PlacedBoids)
	{
		RegisterBoid(Boid);
	}
	RegisterBoid(TestBoid);
}

void ASwarmSimulationRunner::RegisterBoid(AFlyingBoid* Boid)
{
	if (!IsValid(Boid))
	{
		return;
	}
	const TWeakObjectPtr<AFlyingBoid> WeakBoid(Boid);
	PendingRemovals.Remove(WeakBoid);
	if (GetAgentId(Boid) == INDEX_NONE && !PendingAdditions.Contains(WeakBoid))
	{
		PendingAdditions.Add(WeakBoid);
	}
}

void ASwarmSimulationRunner::UnregisterBoid(AFlyingBoid* Boid)
{
	if (Boid == nullptr)
	{
		return;
	}
	const TWeakObjectPtr<AFlyingBoid> WeakBoid(Boid);
	PendingAdditions.Remove(WeakBoid);
	if (!PendingRemovals.Contains(WeakBoid))
	{
		PendingRemovals.Add(WeakBoid);
	}
}

int32 ASwarmSimulationRunner::GetAgentId(AFlyingBoid* Boid) const
{
	for (const FRegisteredAgent& Agent : Agents)
	{
		if (Agent.Boid.Get() == Boid)
		{
			return Agent.Id;
		}
	}
	return INDEX_NONE;
}

void ASwarmSimulationRunner::ApplyQueuedRegistrations()
{
	Agents.RemoveAll([this](const FRegisteredAgent& Agent)
	{
		return !Agent.Boid.IsValid() || PendingRemovals.Contains(Agent.Boid);
	});
	PendingRemovals.Reset();

	for (const TWeakObjectPtr<AFlyingBoid>& Boid : PendingAdditions)
	{
		if (Boid.IsValid() && GetAgentId(Boid.Get()) == INDEX_NONE)
		{
			FRegisteredAgent& Agent = Agents.AddDefaulted_GetRef();
			Agent.Boid = Boid;
			Agent.Id = NextAgentId++;
		}
	}
	PendingAdditions.Reset();
}

void ASwarmSimulationRunner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (DeltaTime <= 0.0f || !FMath::IsFinite(DeltaTime))
	{
		return;
	}

	const float MaxStep = FMath::Max(0.001f, Tuning.MaxStepSeconds);
	float RemainingTime = FMath::Min(DeltaTime, FMath::Max(0.001f, Tuning.MaxFrameSeconds));
	const int32 MaxSteps = FMath::Clamp(Tuning.MaxSubsteps, 1, 16);
	for (int32 Step = 0; Step < MaxSteps && RemainingTime > UE_SMALL_NUMBER; ++Step)
	{
		const float StepTime = FMath::Min(RemainingTime, MaxStep);
		AdvanceStep(StepTime);
		RemainingTime -= StepTime;
	}
}

void ASwarmSimulationRunner::AdvanceStep(float StepTime)
{
	ApplyQueuedRegistrations();
	TArray<FBoidSnapshot> Snapshots;
	Snapshots.Reserve(Agents.Num());
	for (const FRegisteredAgent& Agent : Agents)
	{
		AFlyingBoid* Boid = Agent.Boid.Get();
		if (!IsValid(Boid))
		{
			continue;
		}
		AActor* Target = IsValid(Boid->MovementTarget) ? Boid->MovementTarget.Get() : TargetActor.Get();
		FBoidSnapshot& Snapshot = Snapshots.AddDefaulted_GetRef();
		Snapshot.Boid = Boid;
		Snapshot.Position = Boid->GetActorLocation();
		Snapshot.Velocity = Boid->GetFlightVelocity();
		Snapshot.bHasTarget = IsValid(Target);
		Snapshot.Target = Snapshot.bHasTarget ? Target->GetActorLocation() : Snapshot.Position;
	}

	// All outputs are calculated before the first actor moves.
	TArray<FVector> Accelerations;
	Accelerations.Reserve(Snapshots.Num());
	for (const FBoidSnapshot& Snapshot : Snapshots)
	{
		const AFlyingBoid* Boid = Snapshot.Boid.Get();
		Accelerations.Add(Boid->CalculateArrivalAcceleration(Snapshot.Position, Snapshot.Velocity,
			Snapshot.Target, StepTime, Tuning.SpeedScale, Tuning.AccelerationScale, Tuning.BrakingScale));
	}
	for (int32 Index = 0; Index < Snapshots.Num(); ++Index)
	{
		AFlyingBoid* Boid = Snapshots[Index].Boid.Get();
		if (!IsValid(Boid))
		{
			continue;
		}
		Boid->ApplyAcceleration(Accelerations[Index], StepTime, Tuning.SpeedScale);
		if (bDrawTarget && Snapshots[Index].bHasTarget)
		{
			DrawDebugSphere(GetWorld(), Snapshots[Index].Target, 20.0f, 12, FColor::Green, false, 0.0f);
			DrawDebugLine(GetWorld(), Boid->GetActorLocation(), Snapshots[Index].Target,
				FColor::Cyan, false, 0.0f);
		}
	}
}
