#include "World_Navigation/NavigationGraphAsset.h"

bool UNavigationGraphAsset::Validate(FString* Error) const
{
	if (Nodes.IsEmpty()) { if (Error) *Error = TEXT("Graph has no nodes."); return false; }
	TSet<FName> Ids;
	TMap<FName,FVector> Positions;
	for (const FNavigationWaypointNode& Node : Nodes)
	{
		if (Node.NodeId.IsNone() || Ids.Contains(Node.NodeId)) { if (Error) *Error = TEXT("Graph contains an empty or duplicate node ID."); return false; }
		if (Node.Position.ContainsNaN()) { if (Error) *Error = FString::Printf(TEXT("Graph node '%s' has an invalid position."), *Node.NodeId.ToString()); return false; }
		Ids.Add(Node.NodeId);
		Positions.Add(Node.NodeId,Node.Position);
	}
	TSet<FString> EdgeIds;
	for (const FNavigationGraphEdge& Edge : Edges)
	{
		if (Edge.FromNodeId.IsNone() || Edge.ToNodeId.IsNone() || Edge.FromNodeId == Edge.ToNodeId || !Ids.Contains(Edge.FromNodeId) || !Ids.Contains(Edge.ToNodeId))
		{ if (Error) *Error = TEXT("Graph edge has an invalid endpoint or self-loop."); return false; }
		const FVector* From=Positions.Find(Edge.FromNodeId);
		const FVector* To=Positions.Find(Edge.ToNodeId);
		if (!From || !To || FVector::DistSquared(*From,*To)<=UE_SMALL_NUMBER)
		{ if (Error) *Error = TEXT("Graph edge has zero or invalid length."); return false; }
		const FString Key = Edge.FromNodeId.ToString() + TEXT("/") + Edge.ToNodeId.ToString();
		if (EdgeIds.Contains(Key)) { if (Error) *Error = TEXT("Graph contains a duplicate directed edge."); return false; }
		EdgeIds.Add(Key);
	}
	return true;
}
