#pragma once
#include "BoidTargetSteeringStateTreeTask.h"
#include "BoidRangedOrbitStateTreeTask.generated.h"

USTRUCT()
struct FLYINGSWARMCOMBATSIM_API FBoidRangedOrbitTaskInstanceData : public FBoidTargetSteeringTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin="0", Units="cm"))
	float ArrivalRadiusCm = 80.f;
	UPROPERTY(EditAnywhere, Category="Movement") EBoidRangedPhaseControl PhaseControl = EBoidRangedPhaseControl::Automatic;
};

/** Activates ranged planning and a composable steering profile; range settings live on the planner host. */
USTRUCT(meta=(DisplayName="Orbit Detected Target at Range", Category="Boid"))
struct FLYINGSWARMCOMBATSIM_API FBoidRangedOrbitStateTreeTask : public FBoidTargetSteeringStateTreeTask
{
	GENERATED_BODY()
	virtual const UStruct* GetInstanceDataType() const override { return FBoidRangedOrbitTaskInstanceData::StaticStruct(); }
	virtual FBoidMovementIntent MakeIntent(const FBoidTargetSteeringTaskInstanceData& Data) const override;
	virtual bool SupportsProfile(const UBoidSteeringProfile* Profile) const override;
};
