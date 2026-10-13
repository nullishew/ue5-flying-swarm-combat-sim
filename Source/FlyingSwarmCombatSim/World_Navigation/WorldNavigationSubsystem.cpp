#include "World_Navigation/WorldNavigationSubsystem.h"
#include "World_Navigation/RoomArea.h"
#include "World_Navigation/NavigationWaypoint.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void UWorldNavigationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &ThisClass::LevelAdded);
	LevelRemovedHandle = FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &ThisClass::LevelRemoved);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It) RegisterActor(*It);
}

void UWorldNavigationSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	for (TActorIterator<AActor> It(&World); It; ++It) RegisterActor(*It);
}

void UWorldNavigationSubsystem::Deinitialize()
{
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedHandle);
	Rooms.Reset(); Waypoints.Reset(); Snapshot.Reset();
	Super::Deinitialize();
}

void UWorldNavigationSubsystem::RegisterActor(AActor* Actor)
{
	if (!IsValid(Actor) || Actor->GetWorld() != GetWorld()) return;
	if (auto* Room = Cast<ARoomArea>(Actor)) { Rooms.Add(Room); Invalidate(); }
	if (auto* Waypoint = Cast<ANavigationWaypoint>(Actor)) { Waypoints.Add(Waypoint); Invalidate(); }
}

void UWorldNavigationSubsystem::UnregisterActor(AActor* Actor)
{
	if (auto* Room = Cast<ARoomArea>(Actor)) { Rooms.Remove(Room); Invalidate(); }
	if (auto* Waypoint = Cast<ANavigationWaypoint>(Actor)) { Waypoints.Remove(Waypoint); Invalidate(); }
}

void UWorldNavigationSubsystem::LevelAdded(ULevel* Level, UWorld* World)
{
	if (World == GetWorld() && Level) for (AActor* Actor : Level->Actors) RegisterActor(Actor);
}

void UWorldNavigationSubsystem::LevelRemoved(ULevel* Level, UWorld* World)
{
	if (World != GetWorld()) return;
	if (Level) for (AActor* Actor : Level->Actors) UnregisterActor(Actor);
	else { Rooms.Reset(); Waypoints.Reset(); Invalidate(); }
}

void UWorldNavigationSubsystem::Invalidate() { ++Generation; bDirty = true; }

TArray<ARoomArea*> UWorldNavigationSubsystem::GetRooms() const
{
	TArray<ARoomArea*> Result;
	for (const auto& Weak : Rooms) if (auto* Room = Weak.Get()) if (!Room->IsActorBeingDestroyed()) Result.Add(Room);
	Result.Sort([](const ARoomArea& A, const ARoomArea& B)
	{
		if (A.TransitionPriority != B.TransitionPriority) return A.TransitionPriority > B.TransitionPriority;
		const FName AId = A.GetRoomId(), BId = B.GetRoomId();
		return AId == BId ? A.GetPathName() < B.GetPathName() : AId.LexicalLess(BId);
	});
	return Result;
}

TArray<ARoomArea*> UWorldNavigationSubsystem::GetContainingRooms(const FVector& Position) const
{
	TArray<ARoomArea*> Result;
	for (auto* Room : GetRooms()) if (Room->Contains(Position)) Result.Add(Room);
	return Result;
}

ARoomArea* UWorldNavigationSubsystem::GetPrimaryRoom(const FVector& Position) const
{
	const auto Containing = GetContainingRooms(Position);
	return Containing.IsEmpty() ? nullptr : Containing[0];
}

TSharedRef<const FNavigationGraphSnapshot> UWorldNavigationSubsystem::GetSnapshot()
{
	if (!bDirty && Snapshot) return Snapshot.ToSharedRef();
	auto New = MakeShared<FNavigationGraphSnapshot>();
	New->Generation = Generation;
	const auto RegisteredRooms = GetRooms();
	TSet<FName> RoomIds;
	for (auto* Room : RegisteredRooms)
	{
		if (RoomIds.Contains(Room->GetRoomId())) New->Diagnostics.Add(FString::Printf(TEXT("Duplicate room ID '%s'."), *Room->GetRoomId().ToString()));
		RoomIds.Add(Room->GetRoomId());
	}
	TArray<ANavigationWaypoint*> Points;
	for (const auto& Weak : Waypoints)
		if (auto* Point = Weak.Get()) if (!Point->IsActorBeingDestroyed()) Points.Add(Point);
	New->bHasAuthoredData = !RegisteredRooms.IsEmpty() || !Points.IsEmpty();
	Points.Sort([](const ANavigationWaypoint& A, const ANavigationWaypoint& B) { return A.GetPathName() < B.GetPathName(); });
	TMap<ANavigationWaypoint*, FName> Ids;
	for (auto* Point : Points)
	{
		if (Point->GetActorLocation().ContainsNaN())
		{ New->Diagnostics.Add(FString::Printf(TEXT("Waypoint '%s' has an invalid position."), *Point->GetName())); continue; }
		FNavigationWaypointNode Node;
		Node.NodeId = FName(*Point->GetPathName()); Node.Position = Point->GetActorLocation();
		if (IsValid(Point->Room) && RegisteredRooms.Contains(Point->Room.Get())) Node.RegionId = Point->Room->GetRoomId();
		else if (Point->Room != nullptr)
			New->Diagnostics.Add(FString::Printf(TEXT("Waypoint '%s' has an invalid or foreign-world room reference; room metadata is ignored."), *Point->GetName()));
		New->Nodes.Add(Node); Ids.Add(Point, Node.NodeId);
	}
	TSet<FString> DirectedEdges;
	for (auto* Point : Points)
	{
		const FName* From = Ids.Find(Point); if (!From) continue;
		for (const auto& Link : Point->Links)
		{
			const FName* To = Ids.Find(Link.Destination.Get());
			if (!To || Point == Link.Destination || FVector::DistSquared(Point->GetActorLocation(), Link.Destination->GetActorLocation()) <= UE_SMALL_NUMBER)
			{ New->Diagnostics.Add(FString::Printf(TEXT("Waypoint '%s' has an invalid/self/zero-length link."), *Point->GetName())); continue; }
			auto Add = [&](FName A, FName B)
			{
				const FString Key = A.ToString() + TEXT("|") + B.ToString();
				if (DirectedEdges.Contains(Key)) return;
				DirectedEdges.Add(Key); FNavigationGraphEdge Edge; Edge.FromNodeId = A; Edge.ToNodeId = B; New->Edges.Add(Edge);
			};
			Add(*From, *To); if (Link.bBidirectional) Add(*To, *From);
		}
	}
	for (const FString& Diagnostic : New->Diagnostics) UE_LOG(LogTemp, Warning, TEXT("World navigation: %s"), *Diagnostic);
	New->BuildAdjacency();
	Snapshot = New; bDirty = false;
	return Snapshot.ToSharedRef();
}

void FNavigationGraphSnapshot::BuildAdjacency()
{
	Arcs.Reset(); Outgoing.SetNum(Nodes.Num());
	for (auto& List : Outgoing) List.Reset();
	TMap<FName, int32> Indices;
	for (int32 I = 0; I < Nodes.Num(); ++I) Indices.Add(Nodes[I].NodeId, I);
	TSet<uint64> Seen;
	for (const auto& Edge : Edges)
	{
		const int32* From = Indices.Find(Edge.FromNodeId), *To = Indices.Find(Edge.ToNodeId);
		if (!From || !To || *From == *To) continue;
		auto Add = [&](int32 A, int32 B)
		{
			const uint64 Key = (uint64(uint32(A)) << 32) | uint32(B);
			if (Seen.Contains(Key)) return;
			Seen.Add(Key); const int32 Index = Arcs.Add({A, B, float(FVector::Distance(Nodes[A].Position, Nodes[B].Position))});
			Outgoing[A].Add(Index);
		};
		Add(*From, *To); if (Edge.bBidirectional) Add(*To, *From);
	}
}
