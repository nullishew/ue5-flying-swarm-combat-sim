#include "Swarm/Routing/BoidFlightRouteService.h"
#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "Swarm/FlyingBoid.h"
#include "World_Navigation/RoomArea.h"
#include "World_Navigation/NavigationGraphAsset.h"
#include "World_Navigation/WorldNavigationSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Algo/Reverse.h"

struct FBoidFlightRouteService::FRequestState
{
	uint64 Handle = 0;
	TWeakObjectPtr<AFlyingBoid> Boid;
	TWeakObjectPtr<AActor> IgnoreTarget;
	int32 AgentId = INDEX_NONE;
	int32 GoalVersion = 0;
	uint32 Generation = 0;
	FVector Start = FVector::ZeroVector;
	FVector Goal = FVector::ZeroVector;
	float Radius = 0.f;
	bool bDirectChecked = false;
	bool bFinished = false;
	TSharedPtr<const FNavigationGraphSnapshot> Graph;
	TArray<int32> StartAttachments, GoalAttachments;
	TSet<int32> StartVisible, GoalVisible;
	int32 StartCursor = 0, GoalCursor = 0, AttachmentLimit = 4;
	bool bSearchInitialized = false;
	TArray<float> GScore;
	TArray<int32> CameFrom, Open;
	TSet<int32> Closed;
	TMap<int32, bool> ValidatedEdges; // Request-local only; never masks blockers on another request.
	int32 CurrentNode = INDEX_NONE, ArcCursor = 0;
};

void FBoidFlightRouteService::RefreshGraph(USwarmSimulationSubsystem* Runner)
{
	if (!Runner || !Runner->GetWorld()) return;
	auto* Navigation = Runner->GetWorld()->GetSubsystem<UWorldNavigationSubsystem>();
	TSharedPtr<const FNavigationGraphSnapshot> WorldGraph;
	if (Navigation) WorldGraph = Navigation->GetSnapshot();
	uint32 Hash = WorldGraph ? WorldGraph->Generation : 0;
	Hash = HashCombineFast(Hash, uint32(Runner->NavigationMode));
	Hash = HashCombineFast(Hash, GetTypeHash(Runner->Tuning.BoundsMarginCm));
	if (Runner->WanderBounds)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Runner->WanderBounds->GetComponentLocation()));
		Hash = HashCombineFast(Hash, GetTypeHash(Runner->WanderBounds->GetScaledBoxExtent()));
		const FRotator Rotation = Runner->WanderBounds->GetComponentRotation();
		Hash = HashCombineFast(Hash, GetTypeHash(Rotation.Pitch));
		Hash = HashCombineFast(Hash, GetTypeHash(Rotation.Yaw));
		Hash = HashCombineFast(Hash, GetTypeHash(Rotation.Roll));
	}
	ConfigurationFailure.Reset();
	const bool bUseWorld = WorldGraph && WorldGraph->bHasAuthoredData;
	if (!bUseWorld && Runner->NavigationGraph)
	{
		for (const auto& N : Runner->NavigationGraph->Nodes)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(N.NodeId));
			Hash = HashCombineFast(Hash, GetTypeHash(N.Position));
		}
		for (const auto& E : Runner->NavigationGraph->Edges)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(E.FromNodeId));
			Hash = HashCombineFast(Hash, GetTypeHash(E.ToNodeId));
			Hash = HashCombineFast(Hash, uint32(E.bBidirectional));
		}
		Runner->NavigationGraph->Validate(&ConfigurationFailure);
	}
	const FString Warning = bUseWorld && Runner->NavigationGraph
		? TEXT("World-authored rooms or waypoints take precedence; the legacy navigation graph is ignored.") : ConfigurationFailure;
	if (!Warning.IsEmpty() && Warning != LastLoggedConfigurationFailure)
		UE_LOG(LogTemp, Warning, TEXT("Boid navigation: %s"), *Warning);
	LastLoggedConfigurationFailure = Warning;
	if (!bHasGeometryHash || Hash != LastGeometryHash)
	{
		if (bHasGeometryHash) InvalidateGraph();
		if (bUseWorld) CurrentGraph = WorldGraph;
		else
		{
			auto Graph = MakeShared<FNavigationGraphSnapshot>();
			if (Runner->NavigationGraph && ConfigurationFailure.IsEmpty())
			{ Graph->Nodes = Runner->NavigationGraph->Nodes; Graph->Edges = Runner->NavigationGraph->Edges; }
			Graph->BuildAdjacency(); CurrentGraph = Graph;
		}
		LastGeometryHash = Hash; bHasGeometryHash = true;
	}
}

uint64 FBoidFlightRouteService::Request(USwarmSimulationSubsystem* Runner, AFlyingBoid* Boid, int32 AgentId, int32 GoalVersion,
	const FVector& Start, const FVector& Goal, AActor* IgnoreTarget)
{
	if (!Runner || !Boid) return 0;
	RefreshGraph(Runner);
	auto State = MakeShared<FRequestState>();
	State->Handle = NextHandle++; State->Boid = Boid; State->IgnoreTarget = IgnoreTarget;
	State->AgentId = AgentId; State->GoalVersion = GoalVersion; State->Start = Start; State->Goal = Goal;
	State->Generation = GraphGeneration; State->Graph = CurrentGraph;
	State->Radius = (Boid->CollisionSphere ? Boid->CollisionSphere->GetScaledSphereRadius() : 0.f) + FMath::Max(0.f, Runner->Tuning.ObstacleMarginCm);
	if (State->Graph)
	{
		for (int32 I = 0; I < State->Graph->Nodes.Num(); ++I) { State->StartAttachments.Add(I); State->GoalAttachments.Add(I); }
		auto Sort = [&](TArray<int32>& List, const FVector& Origin)
		{
			List.Sort([&](int32 A, int32 B)
			{
				const double DA = FVector::DistSquared(Origin, State->Graph->Nodes[A].Position), DB = FVector::DistSquared(Origin, State->Graph->Nodes[B].Position);
				return DA == DB ? A < B : DA < DB;
			});
		};
		Sort(State->StartAttachments, Start); Sort(State->GoalAttachments, Goal);
	}
	Requests.Add(State->Handle, State); Queue.Add(State->Handle);
	FBoidFlightRouteResult Pending; Pending.GoalVersion = GoalVersion;
	Results.Add(State->Handle, MoveTemp(Pending)); ResultOwners.Add(State->Handle, AgentId);
	return State->Handle;
}

const FBoidFlightRouteResult* FBoidFlightRouteService::FindResult(uint64 Handle) const { return Results.Find(Handle); }
void FBoidFlightRouteService::Cancel(uint64 Handle) { Requests.Remove(Handle); Results.Remove(Handle); ResultOwners.Remove(Handle); Queue.Remove(Handle); }
void FBoidFlightRouteService::ResetAgent(int32 AgentId)
{
	for (auto It = Requests.CreateIterator(); It; ++It) if (It.Value()->AgentId == AgentId) { Results.Remove(It.Key()); Queue.Remove(It.Key()); It.RemoveCurrent(); }
	for (auto It = ResultOwners.CreateIterator(); It; ++It) if (It.Value() == AgentId) { Results.Remove(It.Key()); It.RemoveCurrent(); }
}
void FBoidFlightRouteService::InvalidateGraph()
{
	++GraphGeneration;
	for (auto& Pair : Results) if (Pair.Value.Status == EBoidRouteStatus::Ready)
	{
		Pair.Value.Status = EBoidRouteStatus::Failed; Pair.Value.Waypoints.Reset();
		Pair.Value.FailureReason = TEXT("Navigation changed before the completed route was consumed.");
	}
}

void FBoidFlightRouteService::BeginFrame(USwarmSimulationSubsystem* Runner, TConstArrayView<FBoidPlannerAgent> Agents)
{
	CurrentAgents.Reset(); for (const auto& Agent : Agents) CurrentAgents.Add(Agent);
	RefreshGraph(Runner);
	QueriesLeft = Runner ? FMath::Max(0, Runner->RouteQueriesPerFrame) : 32;
	ExpansionsLeft = Runner ? FMath::Max(0, Runner->RouteExpansionsPerFrame) : 64;
	// A one-query budget alternates search/active validation instead of starving either forever.
	if (QueriesLeft == 1) { bSingleQuerySearchFrame = !bSingleQuerySearchFrame; ActiveQueriesLeft = bSingleQuerySearchFrame ? 0 : 1; }
	else ActiveQueriesLeft = QueriesLeft / 2;
	SearchQueriesLeft = QueriesLeft - ActiveQueriesLeft;
	RequestQueryFloor = RequestExpansionFloor = 0;
	if (Runner) Runner->LastFrameRouteQueries = Runner->LastFrameRouteExpansions = 0;
}

FName FBoidFlightRouteService::GetRegion(USwarmSimulationSubsystem* Runner, const FVector& Position, FName PreviousRegion) const
{
	(void)PreviousRegion;
	if (Runner && Runner->GetWorld()) if (auto* Navigation = Runner->GetWorld()->GetSubsystem<UWorldNavigationSubsystem>())
		if (auto* Room = Navigation->GetPrimaryRoom(Position)) return Room->GetRoomId();
	return NAME_None;
}

bool FBoidFlightRouteService::IsAllowedPosition(USwarmSimulationSubsystem* Runner, const FVector& Position, float Radius) const
{
	return Runner && FMath::IsFinite(Radius) && Radius >= 0.f && !Position.ContainsNaN() && Runner->IsInsideSafeBounds(Position, Radius);
}

bool FBoidFlightRouteService::IsAllowedSegment(USwarmSimulationSubsystem* Runner, const FVector& A, const FVector& B, float Radius) const
{
	// The outer box is convex. Collision checks belong to ValidateSegment and swept movement.
	return IsAllowedPosition(Runner, A, Radius) && IsAllowedPosition(Runner, B, Radius);
}

bool FBoidFlightRouteService::ValidateSegment(USwarmSimulationSubsystem& Runner, const FVector& A, const FVector& B, float Radius, const FRequestState& Request)
{
	if ((bValidatingActiveSegment && ActiveQueriesLeft <= 0) || (!bValidatingActiveSegment && SearchQueriesLeft <= RequestQueryFloor)) return false;
	if (!IsAllowedSegment(&Runner, A, B, Radius)) return false;
	const AFlyingBoid* Boid = Request.Boid.Get(); if (!Boid || !Boid->CollisionSphere) return false;
	--QueriesLeft; if (bValidatingActiveSegment) --ActiveQueriesLeft; else --SearchQueriesLeft; ++Runner.LastFrameRouteQueries;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BoidRouteSegment), false); Params.AddIgnoredActor(Boid); Params.AddIgnoredActor(Request.IgnoreTarget.Get());
	for (const auto& Agent : CurrentAgents) if (Agent.Boid) Params.AddIgnoredActor(Agent.Boid);
	const auto Shape = FCollisionShape::MakeSphere(Radius);
	const FCollisionResponseParams Responses(Boid->CollisionSphere->GetCollisionResponseToChannels());
	Params.bFindInitialOverlaps = true;
	if (A.Equals(B, UE_SMALL_NUMBER))
		return !Runner.GetWorld()->OverlapBlockingTestByChannel(A, FQuat::Identity, Boid->CollisionSphere->GetCollisionObjectType(), Shape, Params, Responses);
	FHitResult Hit;
	return !Runner.GetWorld()->SweepSingleByChannel(Hit, A, B, FQuat::Identity, Boid->CollisionSphere->GetCollisionObjectType(), Shape, Params, Responses);
}

void FBoidFlightRouteService::ProcessOne(USwarmSimulationSubsystem& Runner, FRequestState& R)
{
	FBoidFlightRouteResult* Result = Results.Find(R.Handle); if (!Result || R.bFinished) return;
	auto Finish = [&](EBoidRouteStatus Status, EBoidRouteTier Tier, TArray<FVector> Points, const TCHAR* Why)
	{
		Result->Status = Status; Result->Tier = Tier; Result->Waypoints = MoveTemp(Points); Result->FailureReason = Why;
		Result->LengthCm = 0.f; FVector Previous = R.Start;
		for (const FVector& Point : Result->Waypoints) { Result->LengthCm += FVector::Distance(Previous, Point); Previous = Point; }
		R.bFinished = true;
	};
	if (R.Generation != GraphGeneration) { Finish(EBoidRouteStatus::Failed, EBoidRouteTier::None, {}, TEXT("Navigation changed; request a fresh route.")); return; }
	if (!R.bDirectChecked)
	{
		if (SearchQueriesLeft <= RequestQueryFloor) return;
		R.bDirectChecked = true;
		if (ValidateSegment(Runner, R.Start, R.Goal, R.Radius, R)) { Finish(EBoidRouteStatus::Ready, EBoidRouteTier::Direct, {R.Goal}, TEXT("")); return; }
	}
	if (Runner.NavigationMode == EBoidNavigationMode::DirectOnly || !R.Graph || R.Graph->Nodes.IsEmpty())
	{ Finish(EBoidRouteStatus::Failed, EBoidRouteTier::None, {}, TEXT("Direct route blocked; no authored waypoint route is available.")); return; }
	const auto& Nodes = R.Graph->Nodes;
	const int32 GoalNode = Nodes.Num(); // Virtual goal carries the final attachment cost.
	while (R.StartVisible.Num() < R.AttachmentLimit && R.StartCursor < R.StartAttachments.Num())
	{
		if (SearchQueriesLeft <= RequestQueryFloor) return;
		const int32 I = R.StartAttachments[R.StartCursor++];
		if (ValidateSegment(Runner, R.Start, Nodes[I].Position, R.Radius, R)) R.StartVisible.Add(I);
	}
	while (R.GoalVisible.Num() < R.AttachmentLimit && R.GoalCursor < R.GoalAttachments.Num())
	{
		if (SearchQueriesLeft <= RequestQueryFloor) return;
		const int32 I = R.GoalAttachments[R.GoalCursor++];
		if (ValidateSegment(Runner, Nodes[I].Position, R.Goal, R.Radius, R)) R.GoalVisible.Add(I);
	}
	if (!R.bSearchInitialized)
	{
		R.bSearchInitialized = true; R.GScore.Init(TNumericLimits<float>::Max(), GoalNode + 1);
		R.CameFrom.Init(INDEX_NONE, GoalNode + 1); R.Open.Reset(); R.Closed.Reset(); R.CurrentNode = INDEX_NONE;
		// Equivalent to expanding a temporary start node with body-clear attachment edges.
		for (int32 I : R.StartVisible) { R.GScore[I] = FVector::Distance(R.Start, Nodes[I].Position); R.Open.Add(I); }
	}
	auto Relax = [&](int32 From, int32 To, float Length)
	{
		if (R.Closed.Contains(To)) return;
		const float Cost = R.GScore[From] + Length;
		if (Cost < R.GScore[To]) { R.GScore[To] = Cost; R.CameFrom[To] = From; R.Open.AddUnique(To); }
	};
	while (R.CurrentNode != INDEX_NONE || !R.Open.IsEmpty())
	{
		if (R.CurrentNode == INDEX_NONE)
		{
			if (ExpansionsLeft <= RequestExpansionFloor) return;
			int32 BestSlot = 0; double BestF = TNumericLimits<double>::Max();
			for (int32 Slot = 0; Slot < R.Open.Num(); ++Slot)
			{
				const int32 I = R.Open[Slot]; const double F = R.GScore[I] + (I == GoalNode ? 0. : FVector::Distance(Nodes[I].Position, R.Goal));
				if (F < BestF || (F == BestF && I < R.Open[BestSlot])) { BestSlot = Slot; BestF = F; }
			}
			R.CurrentNode = R.Open[BestSlot]; R.Open.RemoveAtSwap(BestSlot, 1, EAllowShrinking::No);
			--ExpansionsLeft; ++Runner.LastFrameRouteExpansions;
			if (R.CurrentNode == GoalNode)
			{
				TArray<FVector> Points;
				for (int32 I = R.CameFrom[GoalNode]; I != INDEX_NONE; I = R.CameFrom[I]) Points.Add(Nodes[I].Position);
				Algo::Reverse(Points); Points.Add(R.Goal);
				Finish(EBoidRouteStatus::Ready, EBoidRouteTier::Graph, MoveTemp(Points), TEXT("")); return;
			}
			R.Closed.Add(R.CurrentNode); R.ArcCursor = 0;
			if (R.GoalVisible.Contains(R.CurrentNode)) Relax(R.CurrentNode, GoalNode, FVector::Distance(Nodes[R.CurrentNode].Position, R.Goal));
		}
		const auto& Outgoing = R.Graph->Outgoing[R.CurrentNode];
		while (R.ArcCursor < Outgoing.Num())
		{
			const int32 Index = Outgoing[R.ArcCursor]; const auto& Arc = R.Graph->Arcs[Index];
			if (R.Closed.Contains(Arc.To)) { ++R.ArcCursor; continue; }
			const bool* Valid = R.ValidatedEdges.Find(Index);
			if (!Valid)
			{
				if (SearchQueriesLeft <= RequestQueryFloor) return;
				R.ValidatedEdges.Add(Index, ValidateSegment(Runner, Nodes[Arc.From].Position, Nodes[Arc.To].Position, R.Radius, R));
				Valid = R.ValidatedEdges.Find(Index);
			}
			if (*Valid) Relax(Arc.From, Arc.To, Arc.Length);
			++R.ArcCursor;
		}
		R.CurrentNode = INDEX_NONE;
	}
	if (R.StartCursor < R.StartAttachments.Num() || R.GoalCursor < R.GoalAttachments.Num())
	{ R.AttachmentLimit += 4; R.bSearchInitialized = false; return; }
	Finish(EBoidRouteStatus::Failed, EBoidRouteTier::None, {}, TEXT("No clear connection through the authored waypoint graph."));
}

void FBoidFlightRouteService::Tick(USwarmSimulationSubsystem* Runner, TConstArrayView<FBoidPlannerAgent>)
{
	if (!Runner || Queue.IsEmpty()) return;
	int32 Stalled = 0;
	while (Stalled < Queue.Num() && (SearchQueriesLeft > 0 || ExpansionsLeft > 0))
	{
		if (Cursor >= Queue.Num()) Cursor = 0;
		const uint64 Handle = Queue[Cursor++]; auto* State = Requests.Find(Handle);
		if (!State || !State->IsValid()) { ++Stalled; continue; }
		const int32 Count = FMath::Max(1, Requests.Num());
		RequestQueryFloor = FMath::Max(0, SearchQueriesLeft - FMath::Max(1, FMath::DivideAndRoundUp(SearchQueriesLeft, Count)));
		RequestExpansionFloor = FMath::Max(0, ExpansionsLeft - FMath::Max(1, FMath::DivideAndRoundUp(ExpansionsLeft, Count)));
		const int32 BeforeQueries = SearchQueriesLeft, BeforeExpansions = ExpansionsLeft;
		const int32 BeforeLimit = (*State)->AttachmentLimit;
		ProcessOne(*Runner, **State); RequestQueryFloor = RequestExpansionFloor = 0;
		if ((*State)->bFinished) { Requests.Remove(Handle); Queue.Remove(Handle); Cursor = 0; Stalled = 0; }
		else if (BeforeQueries == SearchQueriesLeft && BeforeExpansions == ExpansionsLeft && BeforeLimit == (*State)->AttachmentLimit) ++Stalled;
		else Stalled = 0;
	}
}

bool FBoidFlightRouteService::FindTransitAnchor(USwarmSimulationSubsystem*, const FVector&, FName, FVector&) const
{
	// Compatibility API only. Pursuit must retain the actual remembered destination.
	return false;
}

EBoidRouteStatus FBoidFlightRouteService::ValidateActiveSegment(USwarmSimulationSubsystem* Runner, AFlyingBoid* Boid,
	const FVector& Start, const FVector& Goal, AActor* IgnoreTarget)
{
	if (!Runner || !Boid) return EBoidRouteStatus::Failed;
	if (QueriesLeft <= 0 || ActiveQueriesLeft <= 0) return EBoidRouteStatus::Pending;
	FRequestState State; State.Boid = Boid; State.IgnoreTarget = IgnoreTarget;
	State.Radius = (Boid->CollisionSphere ? Boid->CollisionSphere->GetScaledSphereRadius() : 0.f) + FMath::Max(0.f, Runner->Tuning.ObstacleMarginCm);
	bValidatingActiveSegment = true;
	const bool bValid = ValidateSegment(*Runner, Start, Goal, State.Radius, State);
	bValidatingActiveSegment = false;
	return bValid ? EBoidRouteStatus::Ready : EBoidRouteStatus::Failed;
}
