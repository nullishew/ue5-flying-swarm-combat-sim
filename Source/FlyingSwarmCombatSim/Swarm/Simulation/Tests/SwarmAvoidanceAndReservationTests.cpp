#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Swarm/Flight/SwarmAvoidanceCandidates.h"
#include "Swarm/Positioning/SwarmPositionReservationGrid.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmAvoidanceAndReservationTest, "FlyingSwarm.Simulation.AvoidanceAndReservationHelpers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmAvoidanceAndReservationTest::RunTest(const FString& Parameters)
{
	TArray<FSwarmAvoidanceCandidate> Candidates;
	SwarmAvoidanceCandidates::AddDirection(Candidates, FVector(1, 0, 0), 0);
	SwarmAvoidanceCandidates::AddDirection(Candidates, FVector(0, 1, 0), 2);
	SwarmAvoidanceCandidates::AddDirection(Candidates, FVector(0, -1, 0), 1);
	SwarmAvoidanceCandidates::AddDirection(Candidates, FVector(0, 0, 1));
	SwarmAvoidanceCandidates::AddDirection(Candidates, FVector(0, 0, -1));
	SwarmAvoidanceCandidates::AddDirection(Candidates, FVector(1, 0, 0));
	TArray<int32> Selected;
	SwarmAvoidanceCandidates::Select(Candidates, 4, Selected);
	TestTrue(TEXT("Bounded selection preserves priority and deterministic ties"), Candidates.Num() == 5 && Selected == TArray<int32>({0, 2, 1, 3}));
	FSwarmPositionReservationGrid Grid;
	Grid.Configure(100.0);
	Grid.Set(7, FVector(-1, 0, 0));
	Grid.Set(8, FVector(1000, 0, 0));
	Grid.Query(FVector::ZeroVector, 10.0, Selected);
	TestTrue(TEXT("Reservation query excludes distant cells"), Selected == TArray<int32>({7}));
	Grid.Set(7, FVector(1000, 0, 0));
	Grid.Remove(8);
	Grid.Query(FVector(1000, 0, 0), 10.0, Selected);
	TestTrue(TEXT("Reservation move and removal apply immediately"), Selected == TArray<int32>({7}));
	return true;
}

#endif
