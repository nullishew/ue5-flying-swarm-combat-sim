#include "Swarm/Positioning/BoidRangedPositionPlanner.h"

#include "Swarm/SwarmSimulationRunner.h"
#include "Swarm/Flight/BoidFlightAlgorithms.h"
#include "Swarm/Routing/BoidFlightRouteService.h"
#include "Swarm/BoidIntentTypes.h"
#include "Swarm/FlyingBoid.h"
#include "Player/FlyingCharacter.h"
#include "Components/SphereComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

namespace
{
	constexpr float RangeErrorBucketCm = 50.f;

	int32 RangeBucket(float Range, bool bAdaptive, const FBoidRangedSettings& Settings)
	{
		const float Error = bAdaptive
			? FMath::Max(0.f, Settings.MinimumRangeCm - Range)
			: FMath::Abs(Range - Settings.PreferredRangeCm);
		return FMath::FloorToInt(Error / RangeErrorBucketCm);
	}
}

bool FBoidRangedPositionPlanner::ValidateRangedPosition(ASwarmSimulationRunner& Runner, AFlyingBoid* Boid,
	const FVector& Goal, const FVector& Center, AActor* Target, FString& Rejection) const
{
	const USphereComponent* Sphere = Boid ? Boid->CollisionSphere : nullptr;
	if (!Sphere || Goal.ContainsNaN())
	{
		Rejection = TEXT("invalid position");
		return false;
	}
	const float Radius = Sphere->GetScaledSphereRadius() + FMath::Max(0.f, Runner.Tuning.ObstacleMarginCm);
	if (!Runner.IsInsideSafeBounds(Goal, Radius) || !Runner.IsAllowedFlightPosition(Goal, Radius))
	{
		Rejection = TEXT("bounds");
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BoidRangedPosition), false);
	Params.AddIgnoredActor(Boid);
	Params.AddIgnoredActor(Target);
	for (const FBoidPlannerAgent& Agent : ActiveAgents) Params.AddIgnoredActor(Agent.Boid);
	const FCollisionResponseParams Responses(Sphere->GetCollisionResponseToChannels());
	++Runner.LastFramePositioningQueries;
	if (Runner.GetWorld()->OverlapBlockingTestByChannel(Goal, FQuat::Identity, Sphere->GetCollisionObjectType(),
		FCollisionShape::MakeSphere(Radius), Params, Responses))
	{
		Rejection = TEXT("overlap");
		return false;
	}
	Params.bTraceComplex = true;
	++Runner.LastFramePositioningQueries;
	if (Runner.GetWorld()->LineTraceTestByChannel(Goal, Center, ECC_Visibility, Params))
	{
		Rejection = TEXT("sight");
		return false;
	}
	return true;
}

float FBoidRangedPositionPlanner::EvaluateRangedPosition(ASwarmSimulationRunner& Runner, AFlyingBoid* Boid,
	const FVector& Goal, const FVector& Center, AActor* Target, const FBoidRangedSettings& Settings,
	float& Open, FString& Rejection) const
{
	if (!ValidateRangedPosition(Runner, Boid, Goal, Center, Target, Rejection)) return -1.f;
	const USphereComponent* Sphere = Boid->CollisionSphere;
	if (Runner.NavigationMode == EBoidNavigationMode::DirectOnly)
	{
		const float Radius = Sphere->GetScaledSphereRadius() + FMath::Max(0.f, Runner.Tuning.ObstacleMarginCm);
		if (!Runner.IsAllowedFlightSegment(Boid->GetActorLocation(), Goal, Radius))
		{
			Rejection = TEXT("flight space");
			return -1.f;
		}
		FCollisionQueryParams RouteParams(SCENE_QUERY_STAT(BoidRangedDirectRoute), false);
		RouteParams.AddIgnoredActor(Boid);
		RouteParams.AddIgnoredActor(Target);
		for (const FBoidPlannerAgent& Agent : ActiveAgents) RouteParams.AddIgnoredActor(Agent.Boid);
		const FCollisionResponseParams RouteResponses(Sphere->GetCollisionResponseToChannels());
		++Runner.LastFramePositioningQueries;
		if (Runner.GetWorld()->SweepTestByChannel(Boid->GetActorLocation(), Goal, FQuat::Identity,
			Sphere->GetCollisionObjectType(), FCollisionShape::MakeSphere(Radius), RouteParams, RouteResponses))
		{
			Rejection = TEXT("route");
			return -1.f;
		}
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BoidRangedOpenness), false);
	Params.AddIgnoredActor(Target);
	Params.AddIgnoredActor(Boid);
	for (const FBoidPlannerAgent& Agent : ActiveAgents) Params.AddIgnoredActor(Agent.Boid);
	const FCollisionResponseParams Responses(Sphere->GetCollisionResponseToChannels());
	const float Radius = Sphere->GetScaledSphereRadius() + FMath::Max(0.f, Runner.Tuning.ObstacleMarginCm);
	FTransform Transform;
	FVector Extent;
	if (!BoidFlight::SafeBounds(Runner.WanderBounds, Radius, Runner.Tuning.BoundsMarginCm, Transform, Extent))
	{
		Rejection = TEXT("bounds configuration");
		return -1.f;
	}
	const FVector Local = Transform.InverseTransformPositionNoScale(Goal);
	TArray<FVector> Directions;
	BoidRanged::ClearanceDirections(Directions);
	TArray<float, TInlineAllocator<14>> Clearances;
	for (const FVector& Direction : Directions)
	{
		const FVector LocalDirection = Transform.InverseTransformVectorNoScale(Direction);
		float Distance = Settings.ClearanceProbeCm;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(LocalDirection[Axis]) > UE_SMALL_NUMBER)
			{
				const double Face = LocalDirection[Axis] > 0.f ? Extent[Axis] : -Extent[Axis];
				Distance = FMath::Min(Distance, static_cast<float>(FMath::Max(0., (Face - Local[Axis]) / LocalDirection[Axis])));
			}
		}
		FHitResult Hit;
		++Runner.LastFramePositioningQueries;
		if (Runner.GetWorld()->SweepSingleByChannel(Hit, Goal, Goal + Direction * Distance, FQuat::Identity,
			Sphere->GetCollisionObjectType(), FCollisionShape::MakeSphere(Radius), Params, Responses))
		{
			Distance = Hit.bStartPenetrating ? 0.f : FMath::Min(Distance, static_cast<float>(Hit.Distance));
		}
		Clearances.Add(Distance / Settings.ClearanceProbeCm);
	}
	Open = BoidRanged::Openness(Clearances, Settings.OpennessMeanWeight, Settings.OpennessMinimumWeight);
	return Open;
}

float FBoidRangedPositionPlanner::ScorePosition(AFlyingBoid* Boid, const FVector& Goal, const FVector& Center,
	float Open, float TravelLength, const FBoidRangedSettings& Settings, float* OutSeedAffinity) const
{
	float Spacing = Settings.SpacingDistanceCm;
	for (const FBoidPlannerAgent& Agent : ActiveAgents)
	{
		AFlyingBoid* Other = Agent.Boid;
		if (!IsValid(Other) || Other == Boid || !Other->IsAlive()) continue;
		Spacing = FMath::Min(Spacing, static_cast<float>(FVector::Distance(Goal, Other->GetActorLocation())));
		if (const FVector* Reservation = Reservations.Find(Agent.Id))
			Spacing = FMath::Min(Spacing, static_cast<float>(FVector::Distance(Goal, *Reservation)));
	}
	const uint32 Seed = BoidRanged::ChoiceSeed(static_cast<uint32>(Boid->GetSimulationSeed()), Host->GetAgentId(Boid));
	float Affinity = .5f;
	const float Range = FVector::Distance(Goal, Center);
	const float Base = Range < Settings.MinimumRangeCm - 1.f
		? BoidRanged::ScoreAdaptive(Open, Spacing, TravelLength, Range, Settings)
		: BoidRanged::Score(Open, Spacing, TravelLength, Range, Settings);
	const float Score = BoidRanged::ScoreWithBias(Base, Goal - Center, Seed, Settings.SeedBiasWeight, &Affinity);
	if (OutSeedAffinity) *OutSeedAffinity = Affinity;
	return Score;
}

void FBoidRangedPositionPlanner::CancelSearch(FSearchState& State)
{
	if (Host)
	{
		for (FCandidate& Candidate : State.Candidates)
		{
			if (Candidate.RouteHandle) Host->GetRouteService().Cancel(Candidate.RouteHandle);
			Candidate.RouteHandle = 0;
		}
	}
	State.Candidates.Reset();
	State.NextCandidate = 0;
	State.bSearchActive = false;
}

void FBoidRangedPositionPlanner::Update(ASwarmSimulationRunner& Runner,
	TConstArrayView<FBoidPlannerAgent> Agents, float DeltaTime)
{
	Host = &Runner;
	const double Started = FPlatformTime::Seconds();
	ActiveAgents.Reset(Agents.Num());
	for (const FBoidPlannerAgent& Agent : Agents) ActiveAgents.Add(Agent);
	ActiveAgents.Sort([](const FBoidPlannerAgent& A, const FBoidPlannerAgent& B) { return A.Id < B.Id; });
	for (const FBoidPlannerAgent& Agent : ActiveAgents) RuntimeByAgent.FindOrAdd(Agent.Id);
	Runner.LastFrameCandidates = Runner.LastFramePositioningQueries = 0;
	const FBoidRangedSettings Settings = Runner.RangedSettings.Sanitized();
	const float FrameDelta = FMath::Max(0.f, FMath::IsFinite(DeltaTime) ? DeltaTime : 0.f);

	for (const FBoidPlannerAgent& Agent : ActiveAgents)
	{
		AFlyingBoid* Boid = Agent.Boid;
		FSearchState& State = RuntimeByAgent.FindChecked(Agent.Id);
		FBoidRangedSelection& Selection = State.Selection;
		if (!IsValid(Boid)) { CancelSearch(State); continue; }
		AActor* Target = Boid->MovementTarget;
		const FBoidTargetFacts Facts = Boid->GetTargetFacts();
		const FBoidMovementIntent Intent = Boid->GetMovementIntent();
		const uint64 OwnerToken = Boid->GetMovementIntentOwner();
		const AFlyingCharacter* Player = Cast<AFlyingCharacter>(Target);
		const bool bEligible = Boid->IsAlive() && Boid->IsSteeringActive()
			&& Intent.Policy == EBoidMovementPolicy::RangedOrbit && IsValid(Target)
			&& (!Player || Player->IsAlive()) && Facts.bDetected && Facts.TargetActor == Target;
		if (!bEligible)
		{
			CancelSearch(State);
			State = FSearchState();
			Reservations.Remove(Agent.Id);
			continue;
		}

		const uint32 Seed = BoidRanged::ChoiceSeed(static_cast<uint32>(Boid->GetSimulationSeed()), Agent.Id);
		if (Selection.Target.Get() != Target || Selection.IntentRevision != Intent.Revision
			|| Selection.ChoiceSeed != Seed || State.OwnerToken != OwnerToken)
		{
			CancelSearch(State);
			State = FSearchState();
			Selection.Target = Target;
			Selection.IntentRevision = Intent.Revision;
			Selection.ChoiceSeed = Seed;
			Selection.PreferredDirection = BoidRanged::PreferredDirection(Seed);
			State.OwnerToken = OwnerToken;
			Reservations.Remove(Agent.Id);
		}
		uint32 ConfigurationSignature = GetTypeHash(Settings.bAllowAdaptiveRange);
		for (float Value : { Settings.MinimumRangeCm, Settings.PreferredRangeCm, Settings.MaximumRangeCm,
			Settings.AdaptiveMinimumRangeCm, Settings.SwitchScoreImprovement })
			ConfigurationSignature = HashCombineFast(ConfigurationSignature, GetTypeHash(Value));
		ConfigurationSignature = HashCombineFast(ConfigurationSignature, GetTypeHash(Settings.DirectionsPerSphere));
		ConfigurationSignature = HashCombineFast(ConfigurationSignature, GetTypeHash(Settings.AdaptiveShellCount));
		if (State.ConfigurationSignature != ConfigurationSignature)
		{
			CancelSearch(State);
			State.ConfigurationSignature = ConfigurationSignature;
			State.RetrySeconds = 0.f;
			State.bForceRefresh = true;
			State.bExplicitRefresh = Selection.bHasPosition;
		}
		const FVector Center = Target->GetActorLocation();
		Selection.SearchAgeSeconds = State.bSearchActive && State.SearchStartedSeconds > 0.
			? static_cast<float>(FPlatformTime::Seconds() - State.SearchStartedSeconds) : 0.f;
		State.RetrySeconds = FMath::Max(0.f, State.RetrySeconds - FrameDelta);

		if (Selection.bHasPosition)
		{
			if (Selection.bUsingAdaptiveRange && !Settings.bAllowAdaptiveRange)
			{
				Selection.bHasPosition = false;
				Selection.Score = -1.f;
				Selection.FailureReason = TEXT("adaptive range disabled");
				Reservations.Remove(Agent.Id);
			}
		}
		if (Selection.bHasPosition)
		{
			const bool bOrbiting = Boid->GetPositioningFacts().bOrbiting;
			const FVector ValidationPoint = bOrbiting ? Boid->GetActorLocation() : Selection.Destination;
			const float Range = FVector::Distance(ValidationPoint, Center);
			const float Min = Selection.bUsingAdaptiveRange ? Settings.AdaptiveMinimumRangeCm : Settings.MinimumRangeCm;
			const float Max = Selection.bUsingAdaptiveRange ? Settings.MinimumRangeCm : Settings.MaximumRangeCm;
			FString Rejection;
			const bool bPositionValid = Range >= Min - 1.f && Range <= Max + 1.f
				&& ValidateRangedPosition(Runner, Boid, ValidationPoint, Center, Target, Rejection);
			if (!bPositionValid)
			{
				Selection.bHasPosition = false;
				Selection.Score = -1.f;
				Selection.FailureReason = Rejection.IsEmpty() ? TEXT("selected range no longer valid") : Rejection;
				Reservations.Remove(Agent.Id);
			}
			else
			{
				Selection.Score = ScorePosition(Boid, Selection.Destination, Center, Selection.Open,
					Selection.Route.LengthCm, Settings, &Selection.SeedAffinity);
			}
		}

		if (State.bSearchActive && BoidRanged::IsSearchStale(State.SearchCenter, Center, Settings.TargetMovementThresholdCm))
		{
			State.SearchCenter = Center;
			++Selection.Rebases;
		}

		if (!State.bSearchActive && (State.bForceRefresh || State.RetrySeconds <= 0.f))
		{
			State.bForceRefresh = false;
			State.SearchCenter = Center;
			State.SearchStartedSeconds = FPlatformTime::Seconds();
			State.NextCandidate = 0;
			State.Candidates.Reset();
			TArray<float> AdaptiveRadii;
			if (Settings.bAllowAdaptiveRange) BoidRanged::AdaptiveRadii(Settings, AdaptiveRadii);
			const int32 Directions = Settings.DirectionsPerSphere;
			int32 Index = 0;
			// A nearby preferred-range point is usable before the wider search completes.
			// The small seeded offset preserves variety without selecting the opposite
			// side of the player and repeatedly fighting close-range recovery.
			FVector NearbyDirection = (Boid->GetActorLocation() - Center).GetSafeNormal();
			if (NearbyDirection.IsNearlyZero()) NearbyDirection = Selection.PreferredDirection;
			NearbyDirection = (NearbyDirection + Selection.PreferredDirection * .15f).GetSafeNormal();
			FCandidate& Nearby = State.Candidates.AddDefaulted_GetRef();
			Nearby.Index = Index++;
			Nearby.Offset = NearbyDirection * Settings.PreferredRangeCm;
			Nearby.Position = Center + Nearby.Offset;
			for (int32 Direction = 0; Direction < Directions; ++Direction)
			{
				auto Add = [&](float Radius, bool bAdaptive)
				{
					FCandidate& Candidate = State.Candidates.AddDefaulted_GetRef();
					Candidate.Index = Index++;
					Candidate.bAdaptive = bAdaptive;
					Candidate.Offset = BoidRanged::TargetRelativeOffset(Direction, Directions, Seed, Radius);
					Candidate.Position = Center + Candidate.Offset;
				};
				Add(Settings.PreferredRangeCm, false);
				for (int32 Shell = 0; Shell < AdaptiveRadii.Num(); ++Shell)
					Add(AdaptiveRadii[Shell], true);
				Add(Settings.MinimumRangeCm, false);
				Add(Settings.MaximumRangeCm, false);
			}
			State.bSearchActive = true;
			Selection.CandidateCount = State.Candidates.Num();
			Selection.CandidatesEvaluated = 0;
		}

		Selection.ReservationPosition = Reservations.FindRef(Agent.Id);
	}

	int32 EvaluatedThisFrame = 0;
	int32 IdleAgents = 0;
	while (!ActiveAgents.IsEmpty() && EvaluatedThisFrame < Settings.CandidatesPerFrame
		&& IdleAgents < ActiveAgents.Num())
	{
		SearchCursor %= ActiveAgents.Num();
		const FBoidPlannerAgent& Agent = ActiveAgents[SearchCursor];
		SearchCursor = (SearchCursor + 1) % ActiveAgents.Num();
		FSearchState& State = RuntimeByAgent.FindChecked(Agent.Id);
		AFlyingBoid* Boid = Agent.Boid;
		if (!IsValid(Boid) || !State.bSearchActive || State.NextCandidate >= State.Candidates.Num())
		{
			++IdleAgents;
			continue;
		}
		IdleAgents = 0;
		FCandidate& Candidate = State.Candidates[State.NextCandidate++];
		AActor* Target = State.Selection.Target.Get();
		if (!IsValid(Target)) continue;
		if (Candidate.bAdaptive && !Settings.bAllowAdaptiveRange)
		{
			Candidate.bEvaluated = true;
			Candidate.Estimate = -1.f;
			Candidate.FailureReason = TEXT("adaptive range disabled");
			++EvaluatedThisFrame;
			++Runner.LastFrameCandidates;
			++State.Selection.CandidatesEvaluated;
			++State.Selection.Rejections;
			continue;
		}
		Candidate.Position = Target->GetActorLocation() + Candidate.Offset;
		FString Rejection;
		const float Range = FVector::Distance(Candidate.Position, Target->GetActorLocation());
		const float MinRange = Candidate.bAdaptive ? Settings.AdaptiveMinimumRangeCm : Settings.MinimumRangeCm;
		const float MaxRange = Candidate.bAdaptive ? Settings.MinimumRangeCm : Settings.MaximumRangeCm;
		if (Range < MinRange - 1.f || Range > MaxRange + 1.f)
		{
			Rejection = TEXT("range");
			Candidate.Estimate = -1.f;
		}
		else
		{
			Candidate.Estimate = EvaluateRangedPosition(Runner, Boid, Candidate.Position,
				Target->GetActorLocation(), Target, Settings, Candidate.Open, Rejection);
		}
		Candidate.FailureReason = Rejection;
		Candidate.bEvaluated = true;
		++EvaluatedThisFrame;
		++Runner.LastFrameCandidates;
		++State.Selection.CandidatesEvaluated;
		if (Candidate.Estimate < 0.f)
		{
			++State.Selection.Rejections;
			State.Selection.FailureReason = Rejection;
			continue;
		}

		if (Runner.NavigationMode == EBoidNavigationMode::DirectOnly)
		{
			Candidate.Route.Status = EBoidRouteStatus::Ready;
			Candidate.Route.Tier = EBoidRouteTier::Direct;
			Candidate.Route.Waypoints.Add(Candidate.Position);
			Candidate.Route.LengthCm = FVector::Distance(Boid->GetActorLocation(), Candidate.Position);
			Candidate.LengthCm = Candidate.Route.LengthCm;
			Candidate.bResolved = true;
			Candidate.Score = ScorePosition(Boid, Candidate.Position, Target->GetActorLocation(), Candidate.Open,
				Candidate.LengthCm, Settings);
		}
		else
		{
			Candidate.Version = ++State.NextVersion;
			Candidate.RequestedEndpoint = Candidate.Position;
			Candidate.RequestedTarget = Target;
			Candidate.RequestedIntentRevision = State.Selection.IntentRevision;
			Candidate.RouteHandle = Runner.GetRouteService().Request(&Runner, Boid, Agent.Id, Candidate.Version,
				Boid->GetActorLocation(), Candidate.Position, Target);
			Candidate.RequestedGraphGeneration = Runner.GetRouteService().GetGeneration();
		}
	}

	for (const FBoidPlannerAgent& Agent : ActiveAgents)
	{
		AFlyingBoid* Boid = Agent.Boid;
		if (!IsValid(Boid)) continue;
		FSearchState& State = RuntimeByAgent.FindChecked(Agent.Id);
		FBoidRangedSelection& Selection = State.Selection;
		AActor* Target = Selection.Target.Get();
		if (!IsValid(Target) || !State.bSearchActive) continue;
		const FVector Center = Target->GetActorLocation();
		bool bPending = false;
		for (FCandidate& Candidate : State.Candidates)
		{
			if (Candidate.bAdaptive && !Settings.bAllowAdaptiveRange)
			{
				if (Candidate.RouteHandle) Runner.GetRouteService().Cancel(Candidate.RouteHandle);
				Candidate.RouteHandle = 0;
				Candidate.bResolved = false;
				Candidate.Score = -1.f;
				continue;
			}
			const bool bGraphStale = Runner.NavigationMode == EBoidNavigationMode::AuthoredRoutes
				&& Candidate.bEvaluated && Candidate.RequestedGraphGeneration != Runner.GetRouteService().GetGeneration();
			if (bGraphStale)
			{
				if (Candidate.RouteHandle) Runner.GetRouteService().Cancel(Candidate.RouteHandle);
				Candidate.RouteHandle = 0;
				Candidate.bEvaluated = true;
				Candidate.bResolved = false;
				Candidate.Score = Candidate.Estimate = -1.f;
				++Candidate.Version;
				++Selection.StaleResults;
				Candidate.FailureReason = TEXT("navigation changed; retrying with next search");
				continue;
			}
			if (!Candidate.RouteHandle || !Candidate.bEvaluated) continue;
			if (Candidate.RequestedTarget.Get() != Target
				|| Candidate.RequestedIntentRevision != Selection.IntentRevision
				|| Candidate.RequestedGraphGeneration != Runner.GetRouteService().GetGeneration()
				|| Candidate.Version <= 0)
			{
				Runner.GetRouteService().Cancel(Candidate.RouteHandle);
				Candidate.RouteHandle = 0;
				Candidate.bResolved = false;
				Candidate.Score = Candidate.Estimate = -1.f;
				Candidate.FailureReason = TEXT("stale route identity; retrying next search");
				++Candidate.Version;
				++Selection.StaleResults;
				continue;
			}
			const float CandidateRange = FVector::Distance(Candidate.Position, Center);
			const float CandidateMinimum = Candidate.bAdaptive ? Settings.AdaptiveMinimumRangeCm : Settings.MinimumRangeCm;
			const float CandidateMaximum = Candidate.bAdaptive ? Settings.MinimumRangeCm : Settings.MaximumRangeCm;
			if (CandidateRange < CandidateMinimum - 1.f || CandidateRange > CandidateMaximum + 1.f)
			{
				Runner.GetRouteService().Cancel(Candidate.RouteHandle);
				Candidate.RouteHandle = 0;
				Candidate.bResolved = false;
				Candidate.Score = -1.f;
				Candidate.FailureReason = TEXT("candidate endpoint left live range");
				++Selection.StaleResults;
				++Selection.Rejections;
				continue;
			}
			const FBoidFlightRouteResult* Result = Runner.GetRouteService().FindResult(Candidate.RouteHandle);
			if (!Result || Result->Status == EBoidRouteStatus::Pending)
			{
				bPending = true;
				continue;
			}
			if (Result->GoalVersion != Candidate.Version
				|| Candidate.RequestedGraphGeneration != Runner.GetRouteService().GetGeneration())
			{
				Runner.GetRouteService().Cancel(Candidate.RouteHandle);
				Candidate.RouteHandle = 0;
				Candidate.bResolved = false;
				Candidate.Score = -1.f;
				Candidate.FailureReason = TEXT("stale route version; retrying next search");
				++Selection.StaleResults;
				continue;
			}
			const FBoidFlightRouteResult CompletedRoute = *Result;
			Runner.GetRouteService().Cancel(Candidate.RouteHandle);
			Candidate.RouteHandle = 0;
			Candidate.Route = CompletedRoute;
			Candidate.bResolved = CompletedRoute.Status == EBoidRouteStatus::Ready;
			Candidate.LengthCm = CompletedRoute.LengthCm;
			if (!Candidate.bResolved)
			{
				Candidate.Score = -1.f;
				Candidate.FailureReason = CompletedRoute.FailureReason;
				++Selection.Rejections;
				continue;
			}
			FString LiveRejection;
			if (!ValidateRangedPosition(Runner, Boid, Candidate.Position, Center, Target, LiveRejection))
			{
				Candidate.bResolved = false;
				Candidate.Score = -1.f;
				Candidate.FailureReason = LiveRejection.IsEmpty() ? TEXT("candidate endpoint is no longer valid") : LiveRejection;
				++Selection.StaleResults;
				++Selection.Rejections;
				continue;
			}
			Candidate.Score = ScorePosition(Boid, Candidate.Position, Center, Candidate.Open, Candidate.LengthCm, Settings);
		}

		FCandidate* Best = nullptr;
		int32 BestBucket = MAX_int32;
		for (FCandidate& Candidate : State.Candidates)
		{
			if (!Candidate.bResolved || Candidate.Score < 0.f) continue;
			if (State.bExplicitRefresh && Selection.bHasPosition
				&& FVector::Distance(Candidate.Position, Selection.Destination)
					<= FMath::Max(1.f, Boid->GetMovementIntent().ArrivalRadiusCm)) continue;
			const int32 Bucket = RangeBucket(FVector::Distance(Candidate.Position, Center), Candidate.bAdaptive, Settings);
			const bool bAhead = Best && BoidRanged::IsCandidateRankedAhead(Candidate.bAdaptive, Bucket,
				Candidate.Score, Candidate.Index, Best->bAdaptive, BestBucket, Best->Score, Best->Index, Settings.SwitchScoreImprovement);
			if (!Best || bAhead)
			{
				Best = &Candidate;
				BestBucket = Bucket;
			}
		}
		if (Best)
		{
			const bool bOrbiting = Boid->GetPositioningFacts().bOrbiting;
			const float ActualRange = FVector::Distance(Boid->GetActorLocation(), Center);
			const float CurrentMinimum = Selection.bUsingAdaptiveRange
				? Settings.AdaptiveMinimumRangeCm : Settings.MinimumRangeCm;
			const float CurrentMaximum = Selection.bUsingAdaptiveRange
				? Settings.MinimumRangeCm : Settings.MaximumRangeCm;
			FString CurrentGeometryFailure;
			const bool bCurrentGeometryValid = Selection.bHasPosition
				&& ActualRange >= CurrentMinimum - 1.f && ActualRange <= CurrentMaximum + 1.f
				&& ValidateRangedPosition(Runner, Boid, Boid->GetActorLocation(), Center, Target, CurrentGeometryFailure);
			const bool bMayReplaceSelection = !Selection.bHasPosition || !bOrbiting || !bCurrentGeometryValid || State.bExplicitRefresh;
			const float CurrentRange = FVector::Distance(Selection.Destination, Center);
			const int32 CurrentBucket = RangeBucket(CurrentRange, Selection.bUsingAdaptiveRange, Settings);
			const bool bSameSelection = Selection.bHasPosition && Selection.bUsingAdaptiveRange == Best->bAdaptive
				&& Selection.Destination.Equals(Best->Position, 1.f);
			if (bSameSelection)
			{
				Selection.Open = Best->Open;
				Selection.Score = ScorePosition(Boid, Best->Position, Center, Best->Open, Best->LengthCm,
					Settings, &Selection.SeedAffinity);
				Selection.Route = Best->Route;
				Selection.RouteGeneration = Runner.GetRouteService().GetGeneration();
				Selection.NavigationMode = Runner.NavigationMode;
			}
			if (bMayReplaceSelection && !bSameSelection && (!Selection.bHasPosition
				|| State.bExplicitRefresh
				|| BoidRanged::IsCandidateRankedAhead(Best->bAdaptive, BestBucket, Best->Score, Best->Index,
					Selection.bUsingAdaptiveRange, CurrentBucket, Selection.Score, MAX_int32, Settings.SwitchScoreImprovement)))
			{
				bool bBestSegmentPending = false;
				const float BestRange = FVector::Distance(Best->Position, Center);
				const float BestMinimum = Best->bAdaptive ? Settings.AdaptiveMinimumRangeCm : Settings.MinimumRangeCm;
				const float BestMaximum = Best->bAdaptive ? Settings.MinimumRangeCm : Settings.MaximumRangeCm;
				FString BestRejection;
				const bool bBestEndpointValid = BestRange >= BestMinimum - 1.f && BestRange <= BestMaximum + 1.f
					&& ValidateRangedPosition(Runner, Boid, Best->Position, Center, Target, BestRejection);
				if (!bBestEndpointValid)
				{
					Best->Score = -1.f;
					Best->FailureReason = BestRejection.IsEmpty() ? TEXT("candidate range changed") : BestRejection;
					++Selection.Rejections;
					bPending = true;
				}
				else if (Runner.NavigationMode == EBoidNavigationMode::AuthoredRoutes)
				{
					if (Best->Route.Waypoints.IsEmpty())
					{
						++Selection.Rejections;
						Selection.FailureReason = TEXT("route has no first segment");
						Best->Score = -1.f;
						bPending = true;
					}
					else
					{
						const EBoidRouteStatus Segment = Runner.GetRouteService().ValidateActiveSegment(&Runner, Boid,
							Boid->GetActorLocation(), Best->Route.Waypoints[0], Target);
						if (Segment == EBoidRouteStatus::Pending) bBestSegmentPending = true;
						else if (Segment == EBoidRouteStatus::Failed)
						{
							Best->Score = -1.f;
							Best->FailureReason = TEXT("route first segment blocked");
							++Selection.Rejections;
							bPending = true;
						}
					}
				}
				else
				{
					const float Radius = Boid->CollisionSphere->GetScaledSphereRadius()
						+ FMath::Max(0.f, Runner.Tuning.ObstacleMarginCm);
					if (!Runner.IsAllowedFlightSegment(Boid->GetActorLocation(), Best->Position, Radius))
					{
						Best->Score = -1.f;
						Best->FailureReason = TEXT("flight space changed");
						++Selection.Rejections;
						bPending = true;
					}
					else
					{
						FCollisionQueryParams RouteParams(SCENE_QUERY_STAT(BoidRangedDirectCommit), false);
						RouteParams.AddIgnoredActor(Boid);
						RouteParams.AddIgnoredActor(Target);
						for (const FBoidPlannerAgent& Other : ActiveAgents) RouteParams.AddIgnoredActor(Other.Boid);
						const FCollisionResponseParams RouteResponses(Boid->CollisionSphere->GetCollisionResponseToChannels());
						++Runner.LastFramePositioningQueries;
						if (Runner.GetWorld()->SweepTestByChannel(Boid->GetActorLocation(), Best->Position,
							FQuat::Identity, Boid->CollisionSphere->GetCollisionObjectType(),
							FCollisionShape::MakeSphere(Radius), RouteParams, RouteResponses))
						{
							Best->Score = -1.f;
							Best->FailureReason = TEXT("route changed");
							++Selection.Rejections;
							bPending = true;
						}
					}
				}
				bPending |= bBestSegmentPending;
				if (Best->Score >= 0.f && !bBestSegmentPending)
				{
					Selection.Destination = Best->Position;
					Selection.Offset = Best->Position - Center;
					Selection.Axis = BoidRanged::OrbitAxis(Selection.Offset);
					Selection.Route = Best->Route;
					Selection.RouteGeneration = Runner.GetRouteService().GetGeneration();
					Selection.NavigationMode = Runner.NavigationMode;
					Selection.bHasPosition = true;
					Selection.bUsingAdaptiveRange = Best->bAdaptive;
					Selection.Open = Best->Open;
					Selection.Score = ScorePosition(Boid, Best->Position, Center, Best->Open, Best->LengthCm,
						Settings, &Selection.SeedAffinity);
					if (!bSameSelection) ++Selection.Version;
					Selection.FailureReason.Reset();
					State.bExplicitRefresh = false;
				}
			}
		}

		if (State.NextCandidate >= State.Candidates.Num())
		{
			for (const FCandidate& Candidate : State.Candidates)
				if (Candidate.RouteHandle) bPending = true;
			if (!bPending)
			{
				Selection.LastSearchSeconds = static_cast<float>(FPlatformTime::Seconds() - State.SearchStartedSeconds);
				State.RetrySeconds = Settings.SearchIntervalSeconds;
				CancelSearch(State);
				State.SearchStartedSeconds = 0.;
				State.bExplicitRefresh = false;
			}
		}
		Selection.SearchAgeSeconds = State.bSearchActive
			? static_cast<float>(FPlatformTime::Seconds() - State.SearchStartedSeconds) : 0.f;
	}
	Runner.LastFramePositioningMilliseconds = static_cast<float>((FPlatformTime::Seconds() - Started) * 1000.);
}

const FBoidRangedSelection* FBoidRangedPositionPlanner::FindSelection(int32 AgentId) const
{
	const FSearchState* State = RuntimeByAgent.Find(AgentId);
	return State ? &State->Selection : nullptr;
}

void FBoidRangedPositionPlanner::SetReservation(int32 AgentId, const FVector& Position)
{
	if (Position.ContainsNaN()) Reservations.Remove(AgentId);
	else Reservations.Add(AgentId, Position);
	if (FSearchState* State = RuntimeByAgent.Find(AgentId)) State->Selection.ReservationPosition = Position;
}

void FBoidRangedPositionPlanner::RequestRefresh(int32 AgentId)
{
	if (FSearchState* State = RuntimeByAgent.Find(AgentId))
	{
		State->bExplicitRefresh = true;
		State->RetrySeconds = 0.f;
		if (!State->bSearchActive) State->bForceRefresh = true;
	}
}

void FBoidRangedPositionPlanner::ResetPosition(int32 AgentId)
{
	if (FSearchState* State = RuntimeByAgent.Find(AgentId))
	{
		CancelSearch(*State);
		State->Selection.bHasPosition = false;
		State->Selection.Score = -1.f;
		State->Selection.FailureReason = TEXT("position reset");
		State->RetrySeconds = 0.f;
		State->bForceRefresh = true;
		State->bExplicitRefresh = true;
	}
	Reservations.Remove(AgentId);
}

void FBoidRangedPositionPlanner::ResetAgent(int32 AgentId)
{
	if (FSearchState* State = RuntimeByAgent.Find(AgentId)) CancelSearch(*State);
	RuntimeByAgent.Remove(AgentId);
	Reservations.Remove(AgentId);
}
