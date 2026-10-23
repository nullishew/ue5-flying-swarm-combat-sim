#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Swarm/Steering/BoidLocalSteeringBehaviors.h"
#include "Swarm/Steering/BoidSteeringService.h"
#include "Swarm/Steering/BoidSteeringProfile.h"
#include "Swarm/Steering/BoidPathFollowingSteeringBehavior.h"
#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "Swarm/Simulation/SwarmSimulationBounds.h"
#include "Swarm/FlyingBoid.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/CustomVersion.h"
#include <limits>

namespace
{
	constexpr auto SteeringTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FSteeringFixture
	{
		FBoidSteeringService Service;
		float NeighborCellSizeCm = 900.f;
		UBoidSteeringProfile* Profile = NewObject<UBoidSteeringProfile>();
		TArray<FBoidSteeringRuntimeState> States;
		TArray<FBoidSteeringNeighbor> Agents = {{FVector::ZeroVector, FVector(100, 0, 0), 20.f, 1, true}, {FVector(100, 50, 0), FVector(0, 100, 0), 20.f, 2, true}};

		FSteeringFixture()
		{
			CastChecked<UBoidSeparationSteeringBehavior>(Profile->Behaviors[0].Behavior)->SeparationDistanceCm = 180.f;
			CastChecked<UBoidCohesionSteeringBehavior>(Profile->Behaviors[2].Behavior)->MinimumNeighborDistanceCm = 180.f;
			CastChecked<UBoidObstacleAvoidanceSteeringBehavior>(Profile->Behaviors[3].Behavior)->NeighborRadiusCm = 450.f;
		}

		FBoidSteeringServiceResult Evaluate(int32 Self = 0, bool bGrid = false, bool bWall = false)
		{
			States.SetNum(Profile ? Profile->Behaviors.Num() : 0);
			Service.BeginStep(Agents, NeighborCellSizeCm, bGrid);
			FBoidSteeringInput Input;
			Input.Position = Agents[Self].Position;
			Input.Velocity = Agents[Self].Velocity;
			Input.Radius = Agents[Self].Radius;
			Input.AgentId = Agents[Self].AgentId;
			Input.Heading = FVector::ForwardVector;
			Input.DeltaTime = .02f;
			Input.bHasGoal = true;
			Input.Goal = FVector(5000, 0, 0);
			return Service.Evaluate(Self, Input, Profile, [&](int32 Index) -> FBoidSteeringRuntimeState& { return States[Index]; }, [bWall](int32) { return bWall; });
		}
	};

	void SaveProfile(UBoidSteeringProfile* Profile, TArray<uint8>& Bytes, FCustomVersionContainer& Versions)
	{
		FMemoryWriter Writer(Bytes, true);
		FObjectAndNameAsStringProxyArchive Archive(Writer, false);
		Archive.SetIsPersistent(true);
		static_cast<UObject*>(Profile)->Serialize(Archive);
		Versions = Archive.GetCustomVersions();
	}

	void LoadProfile(UBoidSteeringProfile* Profile, const TArray<uint8>& Bytes, const FCustomVersionContainer& Versions)
	{
		FMemoryReader Reader(Bytes, true);
		FObjectAndNameAsStringProxyArchive Archive(Reader, false);
		Archive.SetIsPersistent(true);
		Archive.SetCustomVersions(Versions);
		static_cast<UObject*>(Profile)->Serialize(Archive);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoidSeparationBehaviorTest, "FlyingSwarm.Steering.SeparationMagnitudeAndPrediction", SteeringTestFlags)

bool FBoidSeparationBehaviorTest::RunTest(const FString& Parameters)
{
	UBoidSeparationSteeringBehavior* Behavior = NewObject<UBoidSeparationSteeringBehavior>();
	Behavior->SeparationDistanceCm = 200.f;
	Behavior->PredictionSeconds = 0.f;
	Behavior->PredictedClearanceCm = 0.f;
	FBoidSteeringRuntimeState State;
	TArray<FBoidSteeringNeighbor> Neighbors = {{FVector(100, 0, 0), FVector::ZeroVector, 10.f, 2, true}};
	FBoidSteeringInput Input;
	Input.AgentId = 1;
	Input.Radius = 10.f;
	Input.Neighbors = Neighbors;
	const auto Near = Behavior->EvaluateWithResult(Input, State);
	TestTrue(TEXT("Separation retains squared falloff rather than normalizing to one"), Near.Direction.Equals(FVector(-.25, 0, 0), .0001));
	Neighbors[0].Position.X = 150.f;
	const auto Far = Behavior->EvaluateWithResult(Input, State);
	TestTrue(TEXT("Farther neighbors exert weaker separation"), Far.Direction.Size() < Near.Direction.Size());
	Behavior->Weight = 12.f;
	TestTrue(TEXT("Large authored weights scale the preserved magnitude"), FMath::IsNearlyEqual(Behavior->EvaluateWithResult(Input, State).Direction.Size(), Far.Direction.Size() * 12., .0001));
	Behavior->Weight = 0.f;
	TestTrue(TEXT("Zero weight removes optional separation and predictive override"), Behavior->EvaluateWithResult(Input, State).Direction.IsNearlyZero() && !Behavior->EvaluateWithResult(Input, State).bCriticalSeparation);
	Behavior->Weight = 1.f;
	Behavior->PredictionSeconds = 1.f;
	Behavior->PredictedClearanceCm = 50.f;
	Neighbors[0].Position.X = 300.f;
	Neighbors[0].Velocity.X = -400.f;
	TestTrue(TEXT("Converging nonoverlapping neighbors trigger predictive spacing"), Behavior->EvaluateWithResult(Input, State).bCriticalSeparation);
	Neighbors[0].Position = FVector::ZeroVector;
	TestFalse(TEXT("Coincident-agent direction is finite"), Behavior->EvaluateWithResult(Input, State).Direction.ContainsNaN());
	TestTrue(TEXT("Coincident-agent tie direction is antisymmetric"), UBoidSeparationSteeringBehavior::OverlapDirection(1, 2).Equals(-UBoidSeparationSteeringBehavior::OverlapDirection(2, 1)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoidOptionalSteeringTest, "FlyingSwarm.Steering.OptionalContributionsAndPhysicalProtection", SteeringTestFlags)

bool FBoidOptionalSteeringTest::RunTest(const FString& Parameters)
{
	FSteeringFixture Fixture;
	TestFalse(TEXT("Configured separation contributes"), Fixture.Evaluate().Separation.IsNearlyZero());
	TestFalse(TEXT("Configured Alignment contributes"), Fixture.Evaluate().Alignment.IsNearlyZero());
	TestTrue(TEXT("Walls exclude optional neighbors and physical overlap checks"), Fixture.Evaluate(0, false, true).AcceptedNeighbors == 0);
	Fixture.Agents[1].Position = FVector(300, 0, 0);
	TestFalse(TEXT("Configured Cohesion contributes outside its minimum distance"), Fixture.Evaluate().Cohesion.IsNearlyZero());
	Fixture.Agents[1].bSteeringActive = false;
	const auto Inactive = Fixture.Evaluate();
	TestTrue(TEXT("Inactive neighbors do not contribute alignment or cohesion"), Inactive.Alignment.IsNearlyZero() && Inactive.Cohesion.IsNearlyZero());
	Fixture.Agents[1].bSteeringActive = true;
	Fixture.Profile->Behaviors.RemoveAt(0);
	TestTrue(TEXT("Removing Separation removes its influence"), Fixture.Evaluate().Separation.IsNearlyZero());
	Fixture.Profile->Behaviors.Empty();
	const auto Empty = Fixture.Evaluate();
	TestTrue(TEXT("Empty explicit profiles have no hidden global flocking or avoidance"), Empty.Separation.IsNearlyZero() && Empty.Alignment.IsNearlyZero() && Empty.Cohesion.IsNearlyZero() && Empty.Avoidance == nullptr);
	Fixture.Agents[1].Position = FVector(5, 0, 0);
	const auto Overlap = Fixture.Evaluate();
	TestTrue(TEXT("Physical protection remains with every optional behavior removed"), Overlap.bPhysicalOverlap && Overlap.bCriticalSeparation && Overlap.Desired.X < 0.f && Overlap.Separation.IsNearlyZero());
	Fixture.Profile = nullptr;
	const auto Missing = Fixture.Evaluate();
	TestTrue(TEXT("Missing profiles report a configuration error and request zero speed"), !Missing.ConfigurationProblem.IsEmpty() && Missing.bHasDesiredSpeed && Missing.DesiredSpeed == 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoidSteeringBaselineTest, "FlyingSwarm.Steering.ProfileSpatialEquivalence", SteeringTestFlags)

bool FBoidSteeringBaselineTest::RunTest(const FString& Parameters)
{
	FSteeringFixture Fixture;
	CastChecked<UBoidSeparationSteeringBehavior>(Fixture.Profile->Behaviors[0].Behavior)->SeparationDistanceCm = 500.f;
	Fixture.Agents.Reset();
	for (int32 Index = 0; Index < 40; ++Index) Fixture.Agents.Add({FVector((Index % 5) * 210., ((Index / 5) % 4) * 200., (Index / 20) * 250.), FVector(100, 20, 10), 20.f, Index + 1, true});
	for (int32 Index = 0; Index < Fixture.Agents.Num(); ++Index)
	{
		const auto Explicit = Fixture.Evaluate(Index, false);
		const auto Grid = Fixture.Evaluate(Index, true);
		TestTrue(TEXT("Grid and exhaustive evaluations agree"), Explicit.Desired.Equals(Grid.Desired, .0001) && Explicit.AcceptedNeighbors == Grid.AcceptedNeighbors);
	}
	Fixture.Profile->Behaviors.RemoveAll([](const FBoidSteeringBehaviorEntry& Entry) { return !Entry.Behavior->IsA<UBoidSeparationSteeringBehavior>(); });
	auto* Separation = CastChecked<UBoidSeparationSteeringBehavior>(Fixture.Profile->Behaviors[0].Behavior);
	Separation->PredictionSeconds = 0.f;
	Separation->PredictedClearanceCm = 0.f;
	Separation->SeparationDistanceCm = 1200.f;
	for (int32 Index = 1; Index < Fixture.Agents.Num(); ++Index) Fixture.Agents[Index].Position = FVector(10000. + Index * 500., 0, 0);
	Fixture.Agents[1].Position = FVector(1000, 0, 0);
	const auto Broad = Fixture.Evaluate(0, true);
	TestEqual(TEXT("A broad profile gathers a neighbor beyond the global radius"), Broad.AcceptedNeighbors, static_cast<int64>(1));
	Separation->SeparationDistanceCm = 100.f;
	const auto Narrow = Fixture.Evaluate(0, true);
	TestEqual(TEXT("A narrow profile in the same captured layout excludes that neighbor"), Narrow.AcceptedNeighbors, static_cast<int64>(0));
	TestTrue(TEXT("Mixed query radii retain grid/exhaustive equivalence"), Narrow.Desired.Equals(Fixture.Evaluate(0, false).Desired));
	Separation->Weight = 0.f;
	const auto Disabled = Fixture.Evaluate();
	TestTrue(TEXT("Disabled separation no longer expands optional neighbor gathering"), Disabled.Separation.IsNearlyZero() && Disabled.Avoidance == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoidAvoidanceBehaviorTest, "FlyingSwarm.Steering.AvoidanceModifierAndRuntimeIsolation", SteeringTestFlags)

bool FBoidAvoidanceBehaviorTest::RunTest(const FString& Parameters)
{
	FSteeringFixture Fixture;
	Fixture.Agents.SetNum(1);
	Fixture.Profile->Behaviors.RemoveAll([](const FBoidSteeringBehaviorEntry& Entry) { return !Entry.Behavior->IsA<UBoidObstacleAvoidanceSteeringBehavior>(); });
	const auto Evaluation = Fixture.Evaluate();
	if (!TestNotNull(TEXT("Avoidance resolves as a modifier"), Evaluation.Avoidance)) return false;
	auto Probe = [](const FVector& Direction) { return Direction.X > .5f ? 0.f : 700.f; };
	FBoidSteeringRuntimeState AgentA, AgentB;
	const auto Avoidance = Fixture.Service.ApplyAvoidance(Evaluation, AgentA, FVector::ForwardVector, FVector::ForwardVector, 670.f, 700.f, 8, Probe);
	TestTrue(TEXT("Avoidance selects a usable direction within the candidate budget"), !Avoidance.bBlocked && Avoidance.Candidates <= 8 && Probe(Avoidance.Direction) > 0.f);
	TestTrue(TEXT("Hold state belongs to the agent and entry"), AgentA.AvoidanceHoldSeconds > 0.f && AgentB.AvoidanceHoldSeconds == 0.f);
	const auto Blocked = Fixture.Service.ApplyAvoidance(Evaluation, AgentB, FVector::ForwardVector, FVector::ForwardVector, 670.f, 700.f, 8, [](const FVector&) { return 0.f; });
	TestTrue(TEXT("Fully blocked avoidance reports a stop"), Blocked.bBlocked);
	auto* Behavior = CastChecked<UBoidObstacleAvoidanceSteeringBehavior>(Fixture.Profile->Behaviors[0].Behavior);
	Behavior->Weight = 12.f;
	const auto SaturatedEvaluation = Fixture.Evaluate();
	AgentB = FBoidSteeringRuntimeState();
	const auto Saturated = Fixture.Service.ApplyAvoidance(SaturatedEvaluation, AgentB, FVector::ForwardVector, FVector::ForwardVector, 670.f, 700.f, 8, Probe);
	TestTrue(TEXT("Avoidance weights above one saturate at full influence"), Saturated.Direction.Equals(Avoidance.Direction));
	Behavior->Weight = .25f;
	const auto PartialEvaluation = Fixture.Evaluate();
	AgentB = FBoidSteeringRuntimeState();
	const auto Partial = Fixture.Service.ApplyAvoidance(PartialEvaluation, AgentB, FVector::ForwardVector, FVector::ForwardVector, 670.f, 700.f, 8, Probe);
	TestTrue(TEXT("Fractional avoidance blends toward the chosen direction"), Partial.Direction.X > Avoidance.Direction.X);
	Behavior->Weight = 0.f;
	TestTrue(TEXT("Zero avoidance weight removes modifier and hold influence"), Fixture.Evaluate().Avoidance == nullptr);
	Behavior->Weight = 1.f;
	Fixture.States[0].AvoidanceHoldSeconds = 1.f;
	Fixture.Evaluate();
	TestTrue(TEXT("Entry hold countdown advances with simulation time"), FMath::IsNearlyEqual(Fixture.States[0].AvoidanceHoldSeconds, .98f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoidSteeringProfileLifecycleTest, "FlyingSwarm.Steering.ProfileDefaultsSerializationAndValidation", SteeringTestFlags)

bool FBoidSteeringProfileLifecycleTest::RunTest(const FString& Parameters)
{
	UBoidSteeringProfile* Profile = NewObject<UBoidSteeringProfile>();
	TestEqual(TEXT("New profiles contain four ordinary entries"), Profile->Behaviors.Num(), 4);
	FString Reason;
	TestTrue(TEXT("New defaults validate"), Profile->ValidateProfile(Reason));
	TestTrue(TEXT("Saved profile package version remains recognized without a conversion path"), FCurrentCustomVersions::Get(FGuid(0x833E4A14, 0xB8964A01, 0x984B4DD2, 0xBF97A387)).IsSet());
	TestEqual(TEXT("New profiles preserve the authored separation preset"), CastChecked<UBoidSeparationSteeringBehavior>(Profile->Behaviors[0].Behavior)->SeparationDistanceCm, 500.f);
	TestEqual(TEXT("New profiles preserve the authored cohesion exclusion"), CastChecked<UBoidCohesionSteeringBehavior>(Profile->Behaviors[2].Behavior)->MinimumNeighborDistanceCm, 500.f);
	TestEqual(TEXT("New profiles preserve the authored avoidance neighborhood"), CastChecked<UBoidObstacleAvoidanceSteeringBehavior>(Profile->Behaviors[3].Behavior)->NeighborRadiusCm, 500.f);
	UBoidSteeringProfile* Duplicate = DuplicateObject<UBoidSteeringProfile>(Profile, GetTransientPackage());
	TestTrue(TEXT("Duplication instances behavior objects independently"), Duplicate->Behaviors[0].Behavior != Profile->Behaviors[0].Behavior && Duplicate->Behaviors[0].Behavior->GetOuter() == Duplicate);
	Profile->Behaviors.Add(Profile->Behaviors[0]);
	TestFalse(TEXT("Duplicate entry IDs are rejected"), Profile->ValidateProfile(Reason));
	Profile->Behaviors.Last().EntryId = TEXT("SecondSeparation");
	TestFalse(TEXT("Duplicate extracted behavior types are rejected"), Profile->ValidateProfile(Reason));
	Profile->Behaviors.Pop();
	Profile->Behaviors[0].Behavior->Weight = -1.f;
	TestFalse(TEXT("Negative weights are rejected"), Profile->ValidateProfile(Reason));
	Profile->Behaviors[0].Behavior->Weight = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("Nonfinite weights are rejected"), Profile->ValidateProfile(Reason));
	Profile->Behaviors.Empty();
	Duplicate = DuplicateObject<UBoidSteeringProfile>(Profile, GetTransientPackage());
	TestEqual(TEXT("Duplicating an intentionally empty array does not restore defaults"), Duplicate->Behaviors.Num(), 0);
	TArray<uint8> Bytes;
	FCustomVersionContainer Versions;
	SaveProfile(Profile, Bytes, Versions);
	UBoidSteeringProfile* Reloaded = NewObject<UBoidSteeringProfile>();
	LoadProfile(Reloaded, Bytes, Versions);
	TestTrue(TEXT("Persistent reload preserves an intentionally empty profile"), Reloaded->Behaviors.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoidSteeringSafetyAndSwitchTest, "FlyingSwarm.Steering.SafetyAndProfileSwitching", SteeringTestFlags)

bool FBoidSteeringSafetyAndSwitchTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL());
	ASwarmSimulationBounds* Bounds = World->SpawnActor<ASwarmSimulationBounds>();
	if (!TestNotNull(TEXT("Bounds exist"), Bounds)) return false;
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	AFlyingBoid* Boid = World->SpawnActor<AFlyingBoid>();
	USwarmSimulationSubsystem* Simulation = World->GetSubsystem<USwarmSimulationSubsystem>();
	if (!TestNotNull(TEXT("Boid exists"), Boid) || !TestNotNull(TEXT("Simulation exists"), Simulation)) return false;
	Simulation->ApplyQueuedRegistrations();
	auto* Record = Simulation->FindAgentRecord(Simulation->GetAgentId(Boid));
	if (!TestNotNull(TEXT("Boid is registered for stepping"), Record)) return false;
	Record->bLogicStarted = true;
	UBoidSteeringProfile* First = NewObject<UBoidSteeringProfile>(Boid);
	UBoidSteeringProfile* Second = NewObject<UBoidSteeringProfile>(Boid);
	Boid->SetSteeringProfile(First);
	Boid->GetSteeringRuntimeState(3).AvoidanceHoldSeconds = 5.f;
	Boid->SetSteeringProfile(Second);
	TestEqual(TEXT("Switching profiles clears previous avoidance holds"), Boid->GetSteeringRuntimeState(3).AvoidanceHoldSeconds, 0.f);
	Boid->GetSteeringRuntimeState(3).AvoidanceHoldSeconds = 5.f;
	Boid->ResetSteeringAvoidanceRuntime();
	TestEqual(TEXT("Recovery reset clears entry-owned avoidance holds"), Boid->GetSteeringRuntimeState(3).AvoidanceHoldSeconds, 0.f);
	Boid->GetSteeringRuntimeState(3).AvoidanceHoldSeconds = 5.f;
	Second->Behaviors[3].Behavior = NewObject<UBoidObstacleAvoidanceSteeringBehavior>(Second);
	Boid->PrepareSteeringRuntime(Second);
	TestEqual(TEXT("Replacing an entry reinitializes its runtime"), Boid->GetSteeringRuntimeState(3).AvoidanceHoldSeconds, 0.f);
	Second->Behaviors.Empty();
	Boid->SetSteeringProfile(Second);
	for (int32 Step = 0; Step < 10; ++Step) Simulation->AdvanceStep(.02f);
	TestTrue(TEXT("The safety scenario actually steps a moving agent"), Boid->GetActorLocation().X > 0.f);
	AActor* WallActor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Wall actor exists"), WallActor)) return false;
	UBoxComponent* Wall = NewObject<UBoxComponent>(WallActor);
	WallActor->SetRootComponent(Wall);
	Wall->SetBoxExtent(FVector(20.f, 1000.f, 1000.f));
	Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Wall->SetCollisionResponseToAllChannels(ECR_Block);
	Wall->RegisterComponent();
	Wall->SetWorldLocation(FVector(200.f, 0.f, 0.f));
	for (int32 Step = 0; Step < 60; ++Step) Simulation->AdvanceStep(.02f);
	TestTrue(TEXT("Mandatory geometry protection remains without optional avoidance"), Boid->GetActorLocation().X < 180.f);
	TestTrue(TEXT("Mandatory bounds still contain the body"), Simulation->IsInsideSafeBounds(Boid->GetActorLocation(), Boid->CollisionSphere->GetScaledSphereRadius()));
	Simulation->DefaultSteeringProfile = nullptr;
	Boid->SetSteeringProfile(nullptr);
	const int32 RecoveriesBeforeMissingProfile = Record->RecoveryCount;
	AddExpectedError(TEXT("No steering profile is assigned."), EAutomationExpectedErrorFlags::Contains, 1);
	for (int32 Step = 0; Step < 100; ++Step) Simulation->AdvanceStep(.02f);
	TestEqual(TEXT("Missing profiles hold instead of triggering implicit wander recovery"), Record->RecoveryCount, RecoveriesBeforeMissingProfile);
	TestTrue(TEXT("Missing profiles brake to a stop"), Boid->GetFlightVelocity().IsNearlyZero());
	return true;
}

#endif
