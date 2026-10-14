#pragma once
#include "Swarm/AI/BoidTargetSteeringStateTreeTask.h"
#include "BoidFollowStateTreeTask.generated.h"

USTRUCT()
struct FLYINGSWARMCOMBATSIM_API FBoidFollowTaskInstanceData : public FBoidTargetSteeringTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float StandOffDistanceCm = 600.0f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumTargetDistanceCm = 300.0f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float ArrivalRadiusCm = 80.0f;

};

/** Selects stand-off pursuit; target/profile ownership is shared with other target tasks. */
USTRUCT(meta=(DisplayName="Follow Detected Target", Category="Boid"))
struct FLYINGSWARMCOMBATSIM_API FBoidFollowStateTreeTask : public FBoidTargetSteeringStateTreeTask
{
	GENERATED_BODY()
	virtual const UStruct* GetInstanceDataType() const override { return FBoidFollowTaskInstanceData::StaticStruct(); }
	virtual FBoidMovementIntent MakeIntent(const FBoidTargetSteeringTaskInstanceData& Data) const override;
	virtual bool SupportsProfile(const UBoidSteeringProfile* Profile) const override;
};
