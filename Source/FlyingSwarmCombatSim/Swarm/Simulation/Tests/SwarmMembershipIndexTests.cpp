#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "Swarm/Simulation/SwarmSimulationBounds.h"
#include "Swarm/FlyingBoid.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmMembershipIndexTest, "FlyingSwarm.Simulation.MembershipIndex", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmMembershipIndexTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL());
	World->SpawnActor<ASwarmSimulationBounds>();
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	AFlyingBoid* First = World->SpawnActor<AFlyingBoid>();
	AFlyingBoid* Second = World->SpawnActor<AFlyingBoid>(FVector(100, 0, 0), FRotator::ZeroRotator);
	USwarmSimulationSubsystem* Simulation = World->GetSubsystem<USwarmSimulationSubsystem>();
	if (!TestNotNull(TEXT("First agent exists"), First) || !TestNotNull(TEXT("Second agent exists"), Second) || !TestNotNull(TEXT("Simulation exists"), Simulation)) return false;
	Simulation->ApplyQueuedRegistrations();
	const int32 FirstId = Simulation->GetAgentId(First);
	const int32 SecondId = Simulation->GetAgentId(Second);
	TestTrue(TEXT("Committed agents have distinct IDs"), FirstId != INDEX_NONE && SecondId != INDEX_NONE && FirstId != SecondId);
	First->Destroy();
	Simulation->ApplyQueuedRegistrations();
	TestNull(TEXT("Removed record is no longer indexed"), Simulation->FindAgentRecord(FirstId));
	TestEqual(TEXT("Survivor keeps its ID after compaction"), Simulation->GetAgentId(Second), SecondId);
	TestTrue(TEXT("Survivor maps to the compacted record"), Simulation->AgentRecordIndices.FindRef(SecondId) == 0 && Simulation->FindAgentRecord(SecondId) != nullptr);
	return true;
}

#endif
