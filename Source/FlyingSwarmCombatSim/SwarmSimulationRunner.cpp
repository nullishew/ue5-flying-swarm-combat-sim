#include "SwarmSimulationRunner.h"

#include "BoidFlightAlgorithms.h"
#include "BoidSteeringBehavior.h"
#include "BoidSteeringProfile.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StateTreeComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FlyingBoid.h"

namespace
{
	struct FBoidSnapshot
	{
		TWeakObjectPtr<AFlyingBoid> Boid;
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector Heading = FVector::ForwardVector;
		float Radius = 30.0f;
		int32 Id = INDEX_NONE;
		FVector TargetPosition = FVector::ZeroVector;
		bool bHasTarget = false;
		bool bSteeringActive = false;
	};

	struct FSteeringOutput
	{
		FVector ProfileIntent = FVector::ZeroVector;
		FVector Separation = FVector::ZeroVector;
		FVector Alignment = FVector::ZeroVector;
		FVector Cohesion = FVector::ZeroVector;
		FVector Avoidance = FVector::ZeroVector;
		FVector Heading = FVector::ZeroVector;
		FVector Acceleration = FVector::ZeroVector;
		FVector ProbeEnd = FVector::ZeroVector;
		float TargetSpeed = 0.0f;
		bool bThreatened = false;
		bool bSteeringActive = false;
	};

	FCollisionQueryParams ObstacleQuery(const FBoidSnapshot& Self)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BoidObstacleProbe), false);
		Params.AddIgnoredActor(Self.Boid.Get());
		Params.AddIgnoredActors(Self.Boid->CollisionSphere->GetMoveIgnoreActors());
		return Params;
	}

	FVector OverlapSeparationDirection(int32 SelfId, int32 NeighborId)
	{
		const uint32 LowId = static_cast<uint32>(FMath::Min(SelfId, NeighborId));
		const uint32 HighId = static_cast<uint32>(FMath::Max(SelfId, NeighborId));
		const uint32 Hash = LowId * 73856093u ^ HighId * 19349663u;
		const float Angle = 2.0f * PI * static_cast<float>(Hash % 1024u) / 1024.0f;
		const float Height = static_cast<float>((Hash >> 10) % 1024u) / 512.0f - 1.0f;
		const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), Height);
		return (SelfId < NeighborId ? Direction : -Direction).GetSafeNormal();
	}

	bool HasWallBetween(UWorld* World, const FBoidSnapshot& Self, const FBoidSnapshot& Other)
	{
		FCollisionQueryParams Params = ObstacleQuery(Self);
		Params.AddIgnoredActor(Other.Boid.Get());
		const USphereComponent* Sphere = Self.Boid->CollisionSphere;
		return World->LineTraceTestByChannel(Self.Position, Other.Position, Sphere->GetCollisionObjectType(),
			Params, FCollisionResponseParams(Sphere->GetCollisionResponseToChannels()));
	}

	void DrawDirection(UWorld* World, const FVector& Position, const FVector& Direction,
		float Length, const FColor& Color)
	{
		if (!Direction.IsNearlyZero())
		{
			DrawDebugDirectionalArrow(World, Position, Position + Direction.GetSafeNormal() * Length,
				16.0f, Color, false, 0.0f, 0, 2.0f);
		}
	}
}

ASwarmSimulationRunner::ASwarmSimulationRunner()
{
	PrimaryActorTick.bCanEverTick = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	WanderBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("WanderBounds"));
	WanderBounds->SetupAttachment(SceneRoot);
	WanderBounds->InitBoxExtent(FVector(2500.0f, 2500.0f, 1000.0f));
	WanderBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WanderBounds->SetCanEverAffectNavigation(false);
}

void ASwarmSimulationRunner::BeginPlay()
{
	Super::BeginPlay();
	if (!DefaultSteeringProfile)
	{
		// Keeps older StateTree assets functional until a designer assigns a
		// reusable profile. This transient profile is never shared between agents
		// for mutable state; each boid owns its runtime state array.
		FallbackSteeringProfile = NewObject<UBoidSteeringProfile>(this, TEXT("RuntimeFallbackWanderProfile"));
		FBoidSteeringBehaviorEntry& Entry = FallbackSteeringProfile->Behaviors.AddDefaulted_GetRef();
		Entry.EntryId = TEXT("FallbackWander");
		UBoidWanderSteeringBehavior* Wander = NewObject<UBoidWanderSteeringBehavior>(FallbackSteeringProfile);
		Wander->DriftChangeSeconds = Tuning.DriftChangeSeconds;
		Wander->MaxDriftYawRateDeg = Tuning.MaxDriftYawRateDeg;
		Wander->MaxDriftPitchRateDeg = Tuning.MaxDriftPitchRateDeg;
		Entry.Behavior = Wander;
	}
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
	if (!Boid)
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

bool ASwarmSimulationRunner::IsInsideSafeBounds(const FVector& Position, float BoidRadius) const
{
	const FVector SafeExtent = WanderBounds->GetScaledBoxExtent()
		- FVector(BoidRadius + FMath::Max(0.0f, Tuning.BoundsMarginCm));
	if (SafeExtent.GetMin() <= 0.0f)
	{
		return false;
	}
	const FVector Local = WanderBounds->GetComponentTransform().InverseTransformPositionNoScale(Position);
	return FMath::Abs(Local.X) <= SafeExtent.X && FMath::Abs(Local.Y) <= SafeExtent.Y
		&& FMath::Abs(Local.Z) <= SafeExtent.Z;
}

void ASwarmSimulationRunner::ApplyQueuedRegistrations()
{
	for (const TWeakObjectPtr<AFlyingBoid>& Removed : PendingRemovals)
	{
		if (AFlyingBoid* RemovedBoid = Removed.Get())
		{
			for (const FRegisteredAgent& Other : Agents)
			{
				if (AFlyingBoid* OtherBoid = Other.Boid.Get(); OtherBoid && OtherBoid != RemovedBoid)
				{
					OtherBoid->CollisionSphere->IgnoreActorWhenMoving(RemovedBoid, false);
					RemovedBoid->CollisionSphere->IgnoreActorWhenMoving(OtherBoid, false);
				}
			}
		}
	}
	Agents.RemoveAll([this](const FRegisteredAgent& Agent)
	{
		if (!Agent.Boid.IsValid() || PendingRemovals.Contains(Agent.Boid))
		{
			if (AFlyingBoid* Boid = Agent.Boid.Get())
			{
				if (IsValid(Boid->StateTree) && Boid->StateTree->IsRunning())
				{
					Boid->StateTree->StopLogic(TEXT("Unregistered"));
				}
				Boid->SetWanderActive(false);
			}
			return true;
		}
		return false;
	});
	PendingRemovals.Reset();
	for (const TWeakObjectPtr<AFlyingBoid>& WeakBoid : PendingAdditions)
	{
		AFlyingBoid* Boid = WeakBoid.Get();
		if (!IsValid(Boid) || GetAgentId(Boid) != INDEX_NONE)
		{
			continue;
		}
		FRegisteredAgent& Agent = Agents.AddDefaulted_GetRef();
		Agent.Boid = WeakBoid;
		Agent.Id = NextAgentId++;
		Boid->InitializeWander(Agent.Id, Tuning.InitialSeed);
		if (Tuning.CruiseSpeedCmPerSec <= 0.0f || Boid->MaxSpeedCmPerSec <= 0.0f
			|| Boid->MaxAccelerationCmPerSec2 * Tuning.AccelerationScale <= 0.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("Boid %s cannot cruise with zero speed or acceleration settings."), *Boid->GetName());
		}
		for (const FRegisteredAgent& Other : Agents)
		{
			if (AFlyingBoid* OtherBoid = Other.Boid.Get(); OtherBoid && OtherBoid != Boid)
			{
				Boid->CollisionSphere->IgnoreActorWhenMoving(OtherBoid, true);
				OtherBoid->CollisionSphere->IgnoreActorWhenMoving(Boid, true);
			}
		}
		const FVector SafeExtent = WanderBounds->GetScaledBoxExtent()
			- FVector(Boid->CollisionSphere->GetScaledSphereRadius() + FMath::Max(0.0f, Tuning.BoundsMarginCm));
		if (SafeExtent.GetMin() <= BoidFlight::ContactSkinCm)
		{
			UE_LOG(LogTemp, Error, TEXT("WanderBounds cannot contain boid %s; enlarge the bounds or reduce its radius."),
				*Boid->GetName());
		}
		else if (!IsInsideSafeBounds(Boid->GetActorLocation(), Boid->CollisionSphere->GetScaledSphereRadius()))
		{
			UE_LOG(LogTemp, Warning, TEXT("Boid %s starts outside WanderBounds; it will be pulled back inside."), *Boid->GetName());
		}
		if (IsValid(Boid->StateTree))
		{
			Boid->StateTree->StartLogic();
		}
		if (!Boid->IsSteeringActive())
		{
			UE_LOG(LogTemp, Warning, TEXT("Boid %s needs a StateTree steering-profile task to activate movement."), *Boid->GetName());
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
	if (bDrawBounds)
	{
		DrawDebugBox(GetWorld(), WanderBounds->GetComponentLocation(), WanderBounds->GetScaledBoxExtent(),
			WanderBounds->GetComponentQuat(), FColor::Cyan, false, 0.0f, 0, 1.0f);
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
		if (Boid->IsSteeringActive())
		{
			Boid->ConstrainWanderToBounds(WanderBounds, Tuning.BoundsMarginCm);
		}
		FBoidSnapshot& Snapshot = Snapshots.AddDefaulted_GetRef();
		Snapshot.Boid = Boid;
		Snapshot.Position = Boid->GetActorLocation();
		Snapshot.Velocity = Boid->GetFlightVelocity();
		Snapshot.Heading = Boid->GetFlightHeading();
		Snapshot.Radius = Boid->CollisionSphere->GetScaledSphereRadius();
		Snapshot.Id = Agent.Id;
		Snapshot.bSteeringActive = Boid->IsSteeringActive();
		if (IsValid(Boid->MovementTarget))
		{
			Snapshot.TargetPosition = Boid->MovementTarget->GetActorLocation();
			Snapshot.bHasTarget = true;
		}
	}

	TArray<FSteeringOutput> Outputs;
	Outputs.Reserve(Snapshots.Num());
	const float SeparationRadius = FMath::Max(0.0f, Tuning.SeparationDistanceCm);
	const float NeighborRadius = FMath::Max(0.0f, Tuning.NeighborRadiusCm);
	const float PredictionSeconds = FMath::Max(0.0f, Tuning.PredictedSeparationSeconds);
	const float PredictionClearance = FMath::Max(0.0f, Tuning.PredictedSeparationClearanceCm);
	for (const FBoidSnapshot& Self : Snapshots)
	{
		FSteeringOutput& Output = Outputs.AddDefaulted_GetRef();
		if (!Self.bSteeringActive)
		{
			continue;
		}
		AFlyingBoid* Boid = Self.Boid.Get();
		Output.bSteeringActive = true;
		UBoidSteeringProfile* Profile = Boid->GetSteeringProfile();
		if (!Profile)
		{
			Profile = DefaultSteeringProfile ? DefaultSteeringProfile.Get() : FallbackSteeringProfile.Get();
		}
		Boid->PrepareSteeringRuntime(Profile);
		if (Profile)
		{
			FBoidSteeringInput SteeringInput;
			SteeringInput.Position = Self.Position;
			SteeringInput.Velocity = Self.Velocity;
			SteeringInput.Heading = Self.Heading;
			SteeringInput.TargetPosition = Self.TargetPosition;
			SteeringInput.bHasTarget = Self.bHasTarget;
			SteeringInput.DeltaTime = StepTime;
			SteeringInput.AgentId = Self.Id;
			FVector ProfileIntent = FVector::ZeroVector;
			for (int32 BehaviorIndex = 0; BehaviorIndex < Profile->Behaviors.Num(); ++BehaviorIndex)
			{
				const UBoidSteeringBehavior* Behavior = Profile->Behaviors[BehaviorIndex].Behavior;
				if (Behavior)
				{
					ProfileIntent += Behavior->Evaluate(SteeringInput, Boid->GetSteeringRuntimeState(BehaviorIndex));
				}
			}
			Output.ProfileIntent = ProfileIntent;
		}
		FVector SeparationSum = FVector::ZeroVector;
		FVector AlignmentSum = FVector::ZeroVector;
		FVector CohesionPositionSum = FVector::ZeroVector;
		FVector CriticalDirection = FVector::ZeroVector;
		float NearestCriticalDistance = TNumericLimits<float>::Max();
		int32 AlignmentCount = 0;
		int32 CohesionCount = 0;
		bool bCloseApproach = false;
		bool bOverlappingNeighbor = false;
		TArray<const FBoidSnapshot*> VisibleNeighbors;
		for (const FBoidSnapshot& Neighbor : Snapshots)
		{
			if (Neighbor.Id == Self.Id)
			{
				continue;
			}
			const FVector Offset = Self.Position - Neighbor.Position;
			const float Distance = Offset.Size();
			const float PhysicalClearance = Self.Radius + Neighbor.Radius + BoidFlight::ContactSkinCm;
			const float PairSeparationRadius = FMath::Max(SeparationRadius, PhysicalClearance);
			const FVector RelativeVelocity = Self.Velocity - Neighbor.Velocity;
			const float SearchRadius = FMath::Max(NeighborRadius,
				PairSeparationRadius + static_cast<float>(RelativeVelocity.Size()) * PredictionSeconds);
			if (Distance > SearchRadius || HasWallBetween(GetWorld(), Self, Neighbor))
			{
				continue;
			}
			VisibleNeighbors.Add(&Neighbor);
			const FVector Away = Distance > UE_SMALL_NUMBER ? Offset / Distance
				: OverlapSeparationDirection(Self.Id, Neighbor.Id);
			if (Distance < PairSeparationRadius)
			{
				const float Strength = 1.0f - Distance / PairSeparationRadius;
				SeparationSum += Away * Strength * Strength;
			}
			bOverlappingNeighbor |= Distance < PhysicalClearance;
			bool bPairCritical = Distance < FMath::Max(PredictionClearance, PhysicalClearance);
			if (PredictionSeconds > UE_SMALL_NUMBER && FVector::DotProduct(Offset, RelativeVelocity) < 0.0f)
			{
				const float Time = FMath::Clamp(static_cast<float>(-FVector::DotProduct(Offset, RelativeVelocity)
					/ FMath::Max(RelativeVelocity.SizeSquared(), static_cast<double>(UE_SMALL_NUMBER))), 0.0f, PredictionSeconds);
				const FVector PredictedOffset = Offset + RelativeVelocity * Time;
				const float PredictedDistance = PredictedOffset.Size();
				const float PairClearance = FMath::Max(PredictionClearance, PhysicalClearance);
				if (PredictedDistance < PairClearance)
				{
					const FVector PredictedAway = PredictedDistance > UE_SMALL_NUMBER ? PredictedOffset / PredictedDistance : Away;
					SeparationSum += PredictedAway * 2.0f * (1.0f - PredictedDistance / PairClearance);
					bPairCritical = true;
				}
			}
			if (bPairCritical)
			{
				bCloseApproach = true;
				if (Distance < NearestCriticalDistance)
				{
					NearestCriticalDistance = Distance;
					CriticalDirection = Away;
				}
			}
			if (Neighbor.bSteeringActive && Distance < NeighborRadius)
			{
				if (!Neighbor.Velocity.IsNearlyZero())
				{
					AlignmentSum += Neighbor.Velocity.GetSafeNormal();
					++AlignmentCount;
				}
				if (Distance >= PairSeparationRadius)
				{
					CohesionPositionSum += Neighbor.Position;
					++CohesionCount;
				}
			}
		}
		Output.Separation = SeparationSum.GetClampedToMaxSize(1.0f);
		if (bCloseApproach && Output.Separation.IsNearlyZero())
		{
			Output.Separation = CriticalDirection;
		}
		Output.Alignment = AlignmentCount ? AlignmentSum.GetSafeNormal() : FVector::ZeroVector;
		Output.Cohesion = CohesionCount
			? (CohesionPositionSum / CohesionCount - Self.Position).GetSafeNormal() : FVector::ZeroVector;
		const bool bCriticalSeparation = bOverlappingNeighbor || (bCloseApproach && Tuning.SeparationWeight > 0.0f);
		FVector Desired = (Output.ProfileIntent
			+ Output.Separation * FMath::Max(0.0f, Tuning.SeparationWeight) * (bCloseApproach ? 3.0f : 1.0f)
			+ Output.Alignment * FMath::Max(0.0f, Tuning.AlignmentWeight)
			+ Output.Cohesion * FMath::Max(0.0f, Tuning.CohesionWeight)).GetSafeNormal();
		if (bCriticalSeparation && !Output.Separation.IsNearlyZero())
		{
			Desired = Output.Separation.GetSafeNormal();
		}
		if (Desired.IsNearlyZero())
		{
			Desired = Self.Heading;
		}

		const float CruiseSpeed = FMath::Min(FMath::Max(0.0f, Tuning.CruiseSpeedCmPerSec),
			FMath::Max(0.0f, Boid->MaxSpeedCmPerSec));
		const float Brake = FMath::Max(1.0f, Boid->BrakingAccelerationCmPerSec2 * Tuning.BrakingScale);
		const float Speed = Self.Velocity.Size();
		const float MinLookahead = FMath::Max(1.0f, FMath::Min(Tuning.MinLookaheadCm, Tuning.MaxLookaheadCm));
		const float MaxLookahead = FMath::Max(MinLookahead, FMath::Max(Tuning.MinLookaheadCm, Tuning.MaxLookaheadCm));
		const float Lookahead = FMath::Clamp(Speed * 0.6f + Speed * Speed / (2.0f * Brake),
			MinLookahead, MaxLookahead);
		const FCollisionQueryParams QueryParams = ObstacleQuery(Self);
		const USphereComponent* Sphere = Self.Boid->CollisionSphere;
		const FCollisionResponseParams Responses(Sphere->GetCollisionResponseToChannels());
		auto Probe = [&](const FVector& Direction)
		{
			return BoidFlight::Clearance(GetWorld(), WanderBounds, Self.Position, Self.Radius,
				Direction, Lookahead, Tuning.BoundsMarginCm, Tuning.ObstacleMarginCm,
				Sphere->GetCollisionObjectType(), QueryParams, Responses);
		};
		const FVector Forward = Speed > 1.0f ? Self.Velocity / Speed : Self.Heading;
		const float ForwardClearance = Probe(Forward);
		const float IntentClearance = Probe(Desired);
		Output.ProbeEnd = Self.Position + Forward * ForwardClearance;
		Output.bThreatened = FMath::Min(ForwardClearance, IntentClearance) < Lookahead - BoidFlight::ContactSkinCm;
		float ChosenClearance = IntentClearance;
		if (Output.bThreatened || bCriticalSeparation)
		{
			TArray<FVector> Candidates;
			Candidates.Add(Desired);
			Candidates.Add(Forward);
			Candidates.Add(-Forward);
			if (Boid->GetAvoidanceHoldSeconds() > 0.0f)
			{
				Candidates.Add(Boid->GetHeldAvoidanceDirection());
			}
			BoidFlight::AppendEscapeDirections(Candidates);
			FVector BestDirection = Desired;
			float BestScore = -TNumericLimits<float>::Max();
			bool bFoundUsable = false;
			ChosenClearance = 0.0f;
			for (const FVector& Candidate : Candidates)
			{
				const float Clearance = Probe(Candidate);
				const bool bUsable = Clearance > BoidFlight::ContactSkinCm;
				if (bFoundUsable && !bUsable)
				{
					continue;
				}
				float Crowding = 0.0f;
				const FVector CandidateVelocity = Candidate * CruiseSpeed;
				for (const FBoidSnapshot* Neighbor : VisibleNeighbors)
				{
					const FVector Offset = Self.Position - Neighbor->Position;
					const FVector Relative = CandidateVelocity - Neighbor->Velocity;
					const float Time = FMath::Clamp(static_cast<float>(-FVector::DotProduct(Offset, Relative)
						/ FMath::Max(Relative.SizeSquared(), static_cast<double>(UE_SMALL_NUMBER))), 0.0f, PredictionSeconds);
					const float PredictedDistance = (Offset + Relative * Time).Size();
					const float ClearanceNeeded = FMath::Max(PredictionClearance, Self.Radius + Neighbor->Radius);
					Crowding += FMath::Max(0.0f, 1.0f - PredictedDistance / FMath::Max(1.0f, ClearanceNeeded));
				}
				const float Continuity = Boid->GetAvoidanceHoldSeconds() > 0.0f
					? static_cast<float>(FVector::DotProduct(Candidate, Boid->GetHeldAvoidanceDirection())) : 0.0f;
				const float Score = 3.0f * Clearance / Lookahead + 0.8f * FVector::DotProduct(Candidate, Desired)
					+ 0.6f * Continuity - 2.0f * FMath::Min(Crowding, 2.0f);
				if ((bUsable && !bFoundUsable) || Score > BestScore)
				{
					bFoundUsable = bUsable;
					BestScore = Score;
					BestDirection = Candidate;
					ChosenClearance = Clearance;
				}
			}
			Desired = BestDirection;
			Output.Avoidance = BestDirection;
			if (Boid->GetAvoidanceHoldSeconds() <= 0.0f
				|| FVector::DotProduct(BestDirection, Boid->GetHeldAvoidanceDirection()) < 0.98f)
			{
				Boid->HoldAvoidanceDirection(BestDirection, Tuning.AvoidanceHoldSeconds);
			}
		}

		Output.Heading = Boid->TurnFlightHeading(Desired, Tuning.MaxTurnRateDegPerSec, StepTime);
		const bool bEmergency = bCriticalSeparation || (Output.bThreatened
			&& (Speed < 20.0f || ForwardClearance < FMath::Max(Self.Radius + Tuning.ObstacleMarginCm,
				Speed * Tuning.SteeringResponseSeconds)));
		const FVector SteeringDirection = bEmergency ? Desired : Output.Heading;
		const float SteeringClearance = Probe(SteeringDirection);
		// Clearance is sphere-center travel; the probe already accounts for radius.
		const float SafeSpeed = FMath::Sqrt(2.0f * Brake * FMath::Max(0.0f,
			SteeringClearance - BoidFlight::ContactSkinCm));
		Output.TargetSpeed = FMath::Min(CruiseSpeed, SafeSpeed);
		if (Output.bThreatened)
		{
			Output.TargetSpeed = FMath::Min(Output.TargetSpeed,
				CruiseSpeed * FMath::Clamp(ChosenClearance / Lookahead, 0.25f, 1.0f));
		}
		const float Accel = FMath::Max(0.0f, Boid->MaxAccelerationCmPerSec2 * Tuning.AccelerationScale);
		const float Limit = Output.TargetSpeed < Speed ? FMath::Max(Accel, Brake) : Accel;
		Output.Acceleration = ((SteeringDirection * Output.TargetSpeed - Self.Velocity)
			/ FMath::Max(0.01f, Tuning.SteeringResponseSeconds)).GetClampedToMaxSize(Limit);
	}

	const AFlyingBoid* Selected = IsValid(DebugBoid) ? DebugBoid.Get() : TestBoid.Get();
	for (int32 Index = 0; Index < Snapshots.Num(); ++Index)
	{
		AFlyingBoid* Boid = Snapshots[Index].Boid.Get();
		if (!IsValid(Boid) || !Outputs[Index].bSteeringActive)
		{
			continue;
		}
		const float SpeedLimit = FMath::Min(FMath::Max(0.0f, Tuning.CruiseSpeedCmPerSec),
			FMath::Max(0.0f, Boid->MaxSpeedCmPerSec));
		Boid->ApplyWanderAcceleration(Outputs[Index].Acceleration, StepTime, SpeedLimit,
			WanderBounds, Tuning.BoundsMarginCm, Tuning.VisualTurnSpeed);
		float StalledSeconds = 0.0f;
		int32 RecoveryCount = 0;
		for (FRegisteredAgent& Agent : Agents)
		{
			if (Agent.Id != Snapshots[Index].Id)
			{
				continue;
			}
			if (SpeedLimit > 0.0f && Boid->MaxAccelerationCmPerSec2 * Tuning.AccelerationScale > 0.0f)
			{
				if (Agent.ProgressWindowSeconds <= 0.0f)
				{
					Agent.ProgressWindowStart = Snapshots[Index].Position;
				}
				Agent.ProgressDistanceCm += FVector::Distance(Snapshots[Index].Position, Boid->GetActorLocation());
				Agent.ProgressWindowSeconds += StepTime;
				if (Agent.ProgressWindowSeconds >= FMath::Max(0.1f, Tuning.ProgressWindowSeconds))
				{
					const float MinimumSpeed = FMath::Min(FMath::Max(0.0f, Tuning.MinimumProgressSpeedCmPerSec), SpeedLimit * 0.1f);
					// Net displacement also catches rapid back-and-forth jitter at a
					// contact, which can accumulate path length without escaping it.
					const float NetDistance = FVector::Distance(Agent.ProgressWindowStart, Boid->GetActorLocation());
					const float ProgressSpeed = FMath::Min(Agent.ProgressDistanceCm, NetDistance) / Agent.ProgressWindowSeconds;
					Agent.NoProgressSeconds = ProgressSpeed < MinimumSpeed
						? Agent.NoProgressSeconds + Agent.ProgressWindowSeconds : 0.0f;
					Agent.ProgressWindowSeconds = 0.0f;
					Agent.ProgressDistanceCm = 0.0f;
				}
				if (Agent.NoProgressSeconds >= FMath::Max(0.1f, Tuning.StuckProgressSeconds))
				{
					Boid->HoldAvoidanceDirection(FVector::ZeroVector, 0.0f);
					++Agent.RecoveryCount;
					if (!Boid->RecoverWanderFromStall(WanderBounds, Tuning.BoundsMarginCm, SpeedLimit))
					{
						UE_LOG(LogTemp, Warning, TEXT("Boid %s cannot recover: no free escape inside WanderBounds."), *Boid->GetName());
					}
					Agent.NoProgressSeconds = 0.0f;
				}
			}
			else
			{
				Agent.NoProgressSeconds = Agent.ProgressWindowSeconds = Agent.ProgressDistanceCm = 0.0f;
			}
			StalledSeconds = Agent.NoProgressSeconds;
			RecoveryCount = Agent.RecoveryCount;
			break;
		}
		if (bDrawSteering && Boid == Selected)
		{
			const FVector Position = Snapshots[Index].Position;
			DrawDirection(GetWorld(), Position, Outputs[Index].ProfileIntent, 180.0f, FColor::Blue);
			DrawDirection(GetWorld(), Position, Outputs[Index].Separation, 180.0f, FColor::Red);
			DrawDirection(GetWorld(), Position, Outputs[Index].Alignment, 180.0f, FColor::Yellow);
			DrawDirection(GetWorld(), Position, Outputs[Index].Cohesion, 180.0f, FColor::Magenta);
			DrawDirection(GetWorld(), Position, Outputs[Index].Avoidance, 250.0f, FColor::Orange);
			DrawDirection(GetWorld(), Position, Outputs[Index].Heading, 250.0f, FColor::White);
			DrawDebugLine(GetWorld(), Position, Outputs[Index].ProbeEnd,
				Outputs[Index].bThreatened ? FColor::Red : FColor::Green, false, 0.0f, 0, 2.0f);
			DrawDebugString(GetWorld(), Position + FVector(0.0f, 0.0f, 75.0f),
				FString::Printf(TEXT("Steering %.0f cm/s | stalled %.1fs | recovery %d"),
					Boid->GetFlightVelocity().Size(), StalledSeconds, RecoveryCount),
				nullptr, FColor::White, 0.0f);
		}
	}
}
