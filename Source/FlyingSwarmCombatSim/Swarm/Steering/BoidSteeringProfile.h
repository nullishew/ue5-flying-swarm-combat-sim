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
	UBoidSteeringProfile();
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steering", meta = (TitleProperty = "EntryId"))
	TArray<FBoidSteeringBehaviorEntry> Behaviors;
	bool ValidateProfile(FString& OutReason) const;
	/** One primary per phase; configuration only, no mutation or movement. */
	bool ValidateCombatProfile(FString& OutReason) const;
};
