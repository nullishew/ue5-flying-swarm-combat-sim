#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NavigationGraphAsset.generated.h"

USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FNavigationWaypointNode
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName NodeId = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector Position = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName RegionId = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bTransitAnchor = false;
};

USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FNavigationGraphEdge
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName FromNodeId = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ToNodeId = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bBidirectional = false;
};

UCLASS(BlueprintType, meta=(DisplayName="Navigation Graph"))
class FLYINGSWARMCOMBATSIM_API UNavigationGraphAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Navigation", meta=(TitleProperty="NodeId")) TArray<FNavigationWaypointNode> Nodes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Navigation") TArray<FNavigationGraphEdge> Edges;
	bool Validate(FString* Error = nullptr) const;
};
