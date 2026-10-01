#include "SwarmSimulationRunner.h"

#include "DrawDebugHelpers.h"
#include "FlyingBoid.h"

ASwarmSimulationRunner::ASwarmSimulationRunner()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASwarmSimulationRunner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!IsValid(TestBoid) || !IsValid(TargetActor))
	{
		return;
	}

	const FVector TargetLocation = TargetActor->GetActorLocation();
	if (bDrawTarget)
	{
		DrawDebugSphere(GetWorld(), TargetLocation, 20.0f, 12, FColor::Green, false, 0.0f);
		DrawDebugLine(GetWorld(), TestBoid->GetActorLocation(), TargetLocation, FColor::Cyan, false, 0.0f);
	}

	// Bound the update interval and split slow frames so the sweep sees short moves.
	float RemainingTime = FMath::Min(DeltaTime, 0.25f);
	for (int32 Step = 0; Step < 8 && RemainingTime > UE_SMALL_NUMBER; ++Step)
	{
		const float StepTime = FMath::Min(RemainingTime, 1.0f / 30.0f);
		TestBoid->StepTowards(TargetLocation, StepTime);
		RemainingTime -= StepTime;
	}
}
