#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BoidSteeringProfile.generated.h"

class UBoidSteeringBehavior;

USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FBoidSteeringBehaviorEntry
{
	GENERATED_BODY()

	/** Keep this identifier stable so agent-specific randomization stays stable when reordering entries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior")
	FName EntryId = NAME_None;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Behavior")
	TObjectPtr<UBoidSteeringBehavior> Behavior;
};

/** Reusable, designer-authored ordered stack selected by a StateTree state. */
UCLASS(BlueprintType)
class FLYINGSWARMCOMBATSIM_API UBoidSteeringProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (TitleProperty = "EntryId"))
	TArray<FBoidSteeringBehaviorEntry> Behaviors;
};
