#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Swarm/Debug/SwarmDebugPresentation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmDebugBadgeScaleTest, "FlyingSwarm.Debug.BadgeReadability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmDebugBadgeScaleTest::RunTest(const FString& Parameters)
{
	FSwarmDebugSettings Settings;
	Settings.BadgeFullSizeDistanceCm = 1.f;
	Settings.BadgeFalloffEndDistanceCm = 2.f;
	TestEqual(TEXT("Authored one-centimetre start"), SwarmDebugPresentation::BadgeDistanceAlpha(Settings, 1.0), 0.f);
	TestEqual(TEXT("Midpoint at 1.5 centimetres"), SwarmDebugPresentation::BadgeDistanceAlpha(Settings, 1.5), .5f);
	TestEqual(TEXT("Authored two-centimetre end"), SwarmDebugPresentation::BadgeDistanceAlpha(Settings, 2.0), 1.f);
	TestEqual(TEXT("Beyond the interval uses the distant endpoint"), SwarmDebugPresentation::BadgeDistanceAlpha(Settings, 2000.0), 1.f);
	Settings.BadgeNearScale = 1.f;
	Settings.BadgeDistantScale = .5f;
	TestEqual(TEXT("Near badge scale"), SwarmDebugPresentation::CalcBadgeScale(Settings, 0.f), 1.f);
	TestEqual(TEXT("Midpoint badge scale"), SwarmDebugPresentation::CalcBadgeScale(Settings, .5f), .75f);
	TestEqual(TEXT("Distant badge scale"), SwarmDebugPresentation::CalcBadgeScale(Settings, 1.f), .5f);
	Settings.BadgeDistantScale = 0.f;
	TestEqual(TEXT("Zero scale stays positive"), SwarmDebugPresentation::CalcBadgeScale(Settings, 1.f), .01f);
	Settings.BadgeFalloffEndDistanceCm = 1.f;
	TestEqual(TEXT("Equal endpoints keep near size before the boundary"), SwarmDebugPresentation::BadgeDistanceAlpha(Settings, .5), 0.f);
	TestEqual(TEXT("Equal endpoints switch at the authored boundary"), SwarmDebugPresentation::BadgeDistanceAlpha(Settings, 1.0), 1.f);
	Settings.BadgeFullSizeDistanceCm = 2.f;
	TestEqual(TEXT("Reversed endpoints are used without reordering"), SwarmDebugPresentation::BadgeDistanceAlpha(Settings, 1.5), .5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmInspectorSelectionTest, "FlyingSwarm.Debug.InspectorSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmInspectorSelectionTest::RunTest(const FString& Parameters)
{
	using SwarmDebugPresentation::InspectorCandidateWins;
	TestFalse(TEXT("No candidate beyond the aim radius"), InspectorCandidateWins(6401., 10., 1, 6400., TNumericLimits<double>::Max(), INDEX_NONE));
	TestTrue(TEXT("A candidate on the radius is accepted"), InspectorCandidateWins(6400., 10., 1, 6400., TNumericLimits<double>::Max(), INDEX_NONE));
	TestTrue(TEXT("Aim proximity wins over camera distance"), InspectorCandidateWins(20., 900., 8, 25., 10., 2));
	TestTrue(TEXT("Camera proximity breaks equal aim distance"), InspectorCandidateWins(25., 10., 8, 25., 20., 2));
	TestTrue(TEXT("Stable identity breaks an exact tie"), InspectorCandidateWins(25., 20., 1, 25., 20., 2));
	TestFalse(TEXT("A farther candidate cannot replace a selection"), InspectorCandidateWins(26., 1., 1, 25., 20., 2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmMovementSampleTest, "FlyingSwarm.Debug.MovementSampleFreshness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmMovementSampleTest::RunTest(const FString& Parameters)
{
	FSwarmMovementDebugSnapshot Sample;
	TestFalse(TEXT("New selection waits for a real sample"), SwarmDebugPresentation::IsCurrentMovementSample(Sample, 100, true));
	Sample.bHasSample = true;
	Sample.FrameNumber = 100;
	Sample.RequestedPolicy = EBoidMovementPolicy::Cruise;
	Sample.ResolvedPolicy = EBoidMovementPolicy::LocationArrival;
	Sample.Commit.Result = ESwarmMovementCommitResult::FlightSpaceRejected;
	TestTrue(TEXT("Current movement is available"), SwarmDebugPresentation::IsCurrentMovementSample(Sample, 100, true));
	TestFalse(TEXT("An old step is not reported as current"), SwarmDebugPresentation::IsCurrentMovementSample(Sample, 101, true));
	TestFalse(TEXT("Suspended simulation invalidates current-frame data"), SwarmDebugPresentation::IsCurrentMovementSample(Sample, 100, false));
	TestEqual(TEXT("Movement rejection has an explicit label"), FString(SwarmDebugPresentation::MovementCommitLabel(Sample.Commit.Result)), FString(TEXT("FLIGHT SPACE REJECTED")));
	return true;
}

#endif
