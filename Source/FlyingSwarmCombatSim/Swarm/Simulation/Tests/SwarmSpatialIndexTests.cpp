#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Swarm/Simulation/SwarmSpatialIndex.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmSpatialIndexTest, "FlyingSwarm.Simulation.SpatialIndex", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmSpatialIndexTest::RunTest(const FString& Parameters)
{
	FSwarmSpatialIndex Index;
	TArray<FSwarmSpatialSample> Samples = { { FVector(-1, 0, 0), 20 }, { FVector(101, 0, 0), 10 }, { FVector(1000, 0, 0), 30 } };
	TArray<int32> Neighbors;
	Index.Rebuild(Samples, 100.0);
	const FSwarmSpatialQueryStats Stats = Index.QueryRadius(FVector::ZeroVector, 102.0, Neighbors);
	TestTrue(TEXT("Grid covers negative and multiple cells without falling back"), Stats.CellsVisited > 0);
	TestTrue(TEXT("Nearby samples are ordered by ID and distant samples excluded"), Neighbors == TArray<int32>({1, 0}));
	Samples[0].Position = Samples[1].Position = FVector(1000, 0, 0);
	Index.Rebuild(Samples, 100.0);
	Index.QueryRadius(FVector::ZeroVector, 102.0, Neighbors);
	TestTrue(TEXT("Rebuild removes previous cell membership"), Neighbors.IsEmpty());
	return true;
}

#endif
