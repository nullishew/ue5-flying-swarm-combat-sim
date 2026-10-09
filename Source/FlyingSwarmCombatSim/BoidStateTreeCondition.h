#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "BoidStateTreeCondition.generated.h"

UENUM()
enum class EBoidStateTreeFact : uint8
{
	Detected,
	HasSight,
	AttackReady,
	Dead
};

USTRUCT()
struct FBoidStateTreeConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Condition")
	EBoidStateTreeFact Fact = EBoidStateTreeFact::Detected;

	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bInvert = false;
};

/** Tests one read-only boid fact for StateTree selection and transitions. */
USTRUCT(meta = (DisplayName = "Boid Fact", Category = "Boid"))
struct FBoidStateTreeCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoidStateTreeConditionInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
