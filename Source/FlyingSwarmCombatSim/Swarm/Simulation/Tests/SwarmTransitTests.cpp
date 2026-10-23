#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Swarm/Simulation/SwarmTransit.h"
#include "Swarm/Simulation/SwarmSimulationSubsystem.h"
#include "Swarm/Simulation/SwarmSimulationBounds.h"
#include "Swarm/Routing/BoidFlightRouteService.h"
#include "Swarm/Steering/BoidSteeringProfile.h"
#include "Swarm/Steering/BoidPathFollowingSteeringBehavior.h"
#include "Swarm/FlyingBoid.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmTransitGeometryTest, "FlyingSwarm.Navigation.TransitArrivalGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmTransitGeometryTest::RunTest(const FString& Parameters)
{
	const FVector Goal(100.f, 0.f, 0.f);
	TestTrue(TEXT("An endpoint inside the radius arrives"), SwarmTransit::ReachedGoal(true, true, FVector::ZeroVector, FVector(95.f, 0.f, 0.f), Goal, 10.f));
	TestTrue(TEXT("A full pass through the region is not missed"), SwarmTransit::ReachedGoal(true, true, FVector::ZeroVector, FVector(200.f, 0.f, 0.f), Goal, 10.f));
	TestFalse(TEXT("An intermediate or unavailable route cannot complete"), SwarmTransit::ReachedGoal(false, true, FVector::ZeroVector, FVector(200.f, 0.f, 0.f), Goal, 10.f));
	TestFalse(TEXT("Clipped motion cannot manufacture arrival through a wall"), SwarmTransit::ReachedGoal(true, false, FVector::ZeroVector, FVector(200.f, 0.f, 0.f), Goal, 10.f));
	TestFalse(TEXT("Passing outside the radius is not arrival"), SwarmTransit::ReachedGoal(true, true, FVector(0.f, 20.f, 0.f), FVector(200.f, 20.f, 0.f), Goal, 10.f));
	TestTrue(TEXT("Already inside a ready final goal can finish"), SwarmTransit::ReachedGoal(true, true, Goal, Goal, Goal, 10.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwarmTransitCompletionTest, "FlyingSwarm.Navigation.TransitCompletion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwarmTransitCompletionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL());
	ASwarmSimulationBounds* Bounds = World->SpawnActor<ASwarmSimulationBounds>();
	if (!TestNotNull(TEXT("Simulation bounds exist"), Bounds)) return false;
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	AFlyingBoid* Boid = World->SpawnActor<AFlyingBoid>();
	AActor* Target = World->SpawnActor<AActor>();
	USwarmSimulationSubsystem* Simulation = World->GetSubsystem<USwarmSimulationSubsystem>();
	if (!TestNotNull(TEXT("Boid exists"), Boid) || !TestNotNull(TEXT("Memory target exists"), Target) || !TestNotNull(TEXT("Simulation exists"), Simulation)) return false;
	Simulation->ApplyQueuedRegistrations();
	if (!TestTrue(TEXT("Boid is registered"), Simulation->IsAgentRegistered(Boid))) return false;
	Simulation->NavigationMode = EBoidNavigationMode::DirectOnly;
	Simulation->PlannerAgents = {{Boid, Simulation->GetAgentId(Boid)}};
	const FVector Goal(100.f, 0.f, 0.f);
	const uint64 Owner = Boid->AcquireIntentToken();
	FBoidMovementIntent Intent;
	Intent.Policy = EBoidMovementPolicy::LocationArrival;
	Intent.bIsTransit = true;
	Intent.bHasGoal = true;
	Intent.Goal = Goal;
	Intent.ArrivalRadiusCm = 10.f;
	Boid->SetMovementIntent(Intent, Owner);
	Boid->SetWanderActive(true);
	auto& State = Simulation->NavigationAgents.FindOrAdd(Simulation->GetAgentId(Boid));
	State.LastSeenTarget = Target;
	State.LastSeenPosition = Goal;
	State.LastSeenAt = Simulation->NavigationTime;
	State.TransitMemoryRevision = 1;
	State.bHasDestination = true;
	State.Destination = Goal;
	State.Status = EBoidRouteStatus::Ready;
	State.Waypoints = {Goal};
	State.IntentOwner = Owner;
	State.IntentRevision = Boid->GetMovementIntent().Revision;
	Simulation->GetRouteService().BeginFrame(Simulation, Simulation->PlannerAgents);
	Simulation->UpdateNavigationGoals(0.f);
	TestTrue(TEXT("Final Transit waypoint remains continuous instead of arrival braking"), Simulation->GetNavigationGoal(Boid).bContinuousGoal);
	TestTrue(TEXT("Unconsumed memory is available"), Boid->GetNavigationFacts().bHasTransitGoal);
	const uint32 IntentRevision = Boid->GetMovementIntent().Revision;

	Boid->SetActorLocation(FVector(200.f, 0.f, 0.f));
	State.Waypoints = {FVector(50.f, 0.f, 0.f), Goal};
	State.WaypointIndex = 0;
	Simulation->RecordTransitArrival(Boid, Owner, IntentRevision, 1, FVector::ZeroVector, true);
	TestFalse(TEXT("Crossing the destination before the final route segment cannot finish"), Simulation->TryCompleteTransitGoal(Boid, Owner, 1));
	State.WaypointIndex = 1;
	Simulation->RecordTransitArrival(Boid, Owner, IntentRevision, 1, FVector::ZeroVector, false);
	TestFalse(TEXT("A blocked or bounds-clamped move does not finish Transit"), Simulation->TryCompleteTransitGoal(Boid, Owner, 1));
	State.Status = EBoidRouteStatus::Pending;
	Simulation->RecordTransitArrival(Boid, Owner, IntentRevision, 1, FVector::ZeroVector, true);
	TestFalse(TEXT("A pending route cannot finish Transit"), Simulation->TryCompleteTransitGoal(Boid, Owner, 1));
	State.Status = EBoidRouteStatus::Ready;
	Simulation->RecordTransitArrival(Boid, Owner, IntentRevision, 1, FVector::ZeroVector, true);
	TestFalse(TEXT("A different task cannot consume the arrival"), Simulation->TryCompleteTransitGoal(Boid, Owner + 1, 1));
	TestFalse(TEXT("A different memory revision cannot be consumed"), Simulation->TryCompleteTransitGoal(Boid, Owner, 2));
	const uint64 ReplacementOwner = Boid->AcquireIntentToken();
	Boid->SetMovementIntent(Boid->GetMovementIntent(), ReplacementOwner);
	TestFalse(TEXT("A task which actually lost ownership cannot consume arrival"), Simulation->TryCompleteTransitGoal(Boid, Owner, 1));
	TestFalse(TEXT("A new owner cannot inherit the old owner's arrival"), Simulation->TryCompleteTransitGoal(Boid, ReplacementOwner, 1));
	Boid->SetMovementIntent(Boid->GetMovementIntent(), Owner);
	Boid->ApplyWanderAcceleration(FVector(100.f, 0.f, 0.f), .01f, 500.f, Simulation->WanderBounds, Simulation->Tuning.BoundsMarginCm, 0.f);
	const FVector VelocityBeforeCompletion = Boid->GetFlightVelocity();
	TestFalse(TEXT("Moving handoff fixture has nonzero velocity"), VelocityBeforeCompletion.IsNearlyZero());
	TestTrue(TEXT("The owning task consumes the recorded final arrival"), Simulation->TryCompleteTransitGoal(Boid, Owner, 1));
	TestTrue(TEXT("Completion preserves velocity"), Boid->GetFlightVelocity().Equals(VelocityBeforeCompletion));
	TestFalse(TEXT("Completion publishes unavailable Transit memory immediately"), Boid->GetNavigationFacts().bHasTransitGoal);
	TestEqual(TEXT("Handled memory cannot be selected again"), Simulation->GetTransitGoalRevision(Boid), uint64(0));
	TestFalse(TEXT("Completion cannot be consumed twice"), Simulation->TryCompleteTransitGoal(Boid, Owner, 1));

	Boid->TargetFacts.TargetActor = Target;
	Boid->TargetFacts.LastKnownPosition = Goal;
	Boid->TargetFacts.bHasSight = true;
	Boid->TargetFacts.bDetected = true;
	Simulation->UpdateNavigationGoals(.01f);
	TestEqual(TEXT("A new detected sighting refreshes the remembered-goal revision"), Simulation->GetTransitGoalRevision(Boid), uint64(2));
	TestTrue(TEXT("New sighting makes a handled goal available again"), Boid->GetNavigationFacts().bHasTransitGoal);
	TestFalse(TEXT("The old task cannot consume refreshed memory"), Simulation->TryCompleteTransitGoal(Boid, Owner, 1));
	Boid->TargetFacts.bHasSight = false;
	Boid->TargetFacts.bDetected = false;
	Simulation->RecordTransitArrival(Boid, Owner, IntentRevision, 2, FVector::ZeroVector, true);
	FBoidMovementIntent NewIntent = Boid->GetMovementIntent();
	NewIntent.ArrivalRadiusCm += 1.f;
	Boid->SetMovementIntent(NewIntent, Owner);
	TestFalse(TEXT("An arrival for an old intent revision cannot complete a changed intent"), Simulation->TryCompleteTransitGoal(Boid, Owner, 2));
	NewIntent.bIsTransit = false;
	Boid->SetMovementIntent(NewIntent, Owner);
	TestFalse(TEXT("Ordinary location arrival cannot consume remembered Transit"), Simulation->TryCompleteTransitGoal(Boid, Owner, 2));
	Simulation->UpdateNavigationGoals(0.f);
	TestFalse(TEXT("Ordinary direct location arrival is not made continuous"), Simulation->GetNavigationGoal(Boid).bContinuousGoal);

	// Exercise the production movement result: neither collision nor bounds clipping is a clear fly-through.
	AActor* WallActor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Wall actor exists"), WallActor)) return false;
	UBoxComponent* Wall = NewObject<UBoxComponent>(WallActor);
	WallActor->SetRootComponent(Wall);
	Wall->InitBoxExtent(FVector(10.f, 200.f, 200.f));
	Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Wall->SetCollisionResponseToAllChannels(ECR_Block);
	Wall->RegisterComponent();
	Wall->SetWorldLocation(FVector(100.f, 0.f, 0.f));
	Boid->SetActorLocation(FVector::ZeroVector);
	bool bClearMove = true;
	Boid->ApplyWanderAcceleration(FVector(10000.f, 0.f, 0.f), .2f, 500.f, Simulation->WanderBounds, Simulation->Tuning.BoundsMarginCm, 0.f, nullptr, &bClearMove);
	TestFalse(TEXT("A real swept wall contact rejects fly-through completion"), bClearMove);
	Boid->SetActorLocation(Simulation->WanderBounds->GetComponentLocation() + FVector(Simulation->WanderBounds->GetScaledBoxExtent().X + 100.f, 0.f, 0.f));
	bClearMove = true;
	Boid->ApplyWanderAcceleration(FVector(10000.f, 0.f, 0.f), .2f, 500.f, Simulation->WanderBounds, Simulation->Tuning.BoundsMarginCm, 0.f, nullptr, &bClearMove);
	TestFalse(TEXT("A real bounds clamp rejects fly-through completion"), bClearMove);

	// A failed full corridor can still be pursued with local avoidance, without inventing a ready route.
	const FVector LocalGoal(600.f, 0.f, 0.f);
	Boid->SetActorLocation(FVector::ZeroVector);
	Boid->ApplyWanderAcceleration(-Boid->GetFlightVelocity() / .01f, .01f, 500.f, Simulation->WanderBounds, Simulation->Tuning.BoundsMarginCm, 0.f);
	Wall->SetWorldLocation(FVector(300.f, 0.f, 0.f));
	State.LastSeenPosition = LocalGoal;
	NewIntent.bIsTransit = true;
	NewIntent.Goal = LocalGoal;
	Boid->SetMovementIntent(NewIntent, Owner);
	UBoidSteeringProfile* LocalProfile = NewObject<UBoidSteeringProfile>(Boid);
	FBoidSteeringBehaviorEntry& Entry = LocalProfile->Behaviors.AddDefaulted_GetRef();
	Entry.Behavior = NewObject<UBoidPathFollowingSteeringBehavior>(LocalProfile);
	Boid->SetSteeringProfile(LocalProfile);
	Simulation->GetRouteService().BeginFrame(Simulation, Simulation->PlannerAgents);
	Simulation->UpdateNavigationGoals(0.f);
	TestTrue(TEXT("Initial pending route allows local Transit"), State.bLocalTransitFallback && State.Navigation.bHasGoal && !State.Navigation.bHoldPosition && State.Navigation.bContinuousGoal);
	Simulation->GetRouteService().Tick(Simulation, Simulation->PlannerAgents);
	Simulation->UpdateNavigationGoals(0.f);
	TestTrue(TEXT("Blocked route still reports failure honestly"), Boid->GetNavigationFacts().Status == EBoidRouteStatus::Failed);
	TestTrue(TEXT("Failed route retains the remembered destination for local steering"), State.bLocalTransitFallback && State.Navigation.Goal.Equals(LocalGoal) && !State.Navigation.bHoldPosition);
	TestTrue(TEXT("Inspector identifies local fallback"), Boid->GetNavigationFacts().FailureReason.Contains(TEXT("Local Transit steering active")));
	Simulation->AdvanceStep(.02f);
	TestFalse(TEXT("Blocked full corridor does not force a stationary Transit hold"), Boid->GetFlightVelocity().IsNearlyZero());
	const uint32 LocalRevision = Boid->GetMovementIntent().Revision;
	Boid->SetActorLocation(FVector(700.f, 0.f, 0.f));
	Simulation->RecordTransitArrival(Boid, Owner, LocalRevision, 2, FVector(500.f, 0.f, 0.f), false);
	TestFalse(TEXT("Clipped local movement cannot complete"), Simulation->TryCompleteTransitGoal(Boid, Owner, 2));
	// Even an unclipped step inside a large arrival radius cannot complete across a wall.
	Wall->SetWorldLocation(LocalGoal - FVector(100.f, 0.f, 0.f));
	FBoidMovementIntent WideArrival = Boid->GetMovementIntent();
	WideArrival.ArrivalRadiusCm = 200.f;
	Boid->SetMovementIntent(WideArrival, Owner);
	Simulation->UpdateNavigationGoals(0.f);
	Boid->SetActorLocation(LocalGoal - FVector(180.f, 0.f, 0.f));
	Simulation->RecordTransitArrival(Boid, Owner, Boid->GetMovementIntent().Revision, 2, LocalGoal - FVector(190.f, 0.f, 0.f), true);
	TestFalse(TEXT("Local radius overlap through a wall cannot consume memory"), Simulation->TryCompleteTransitGoal(Boid, Owner, 2));
	WallActor->Destroy();
	Boid->SetActorLocation(FVector(700.f, 0.f, 0.f));
	Simulation->RecordTransitArrival(Boid, Owner, Boid->GetMovementIntent().Revision, 2, FVector(500.f, 0.f, 0.f), true);
	TestTrue(TEXT("Clear local fly-through completes without a ready route"), Simulation->TryCompleteTransitGoal(Boid, Owner, 2));
	Simulation->UpdateNavigationGoals(0.f);
	TestFalse(TEXT("Handled memory cannot keep local fallback active"), State.bLocalTransitFallback);
	State.bTransitGoalHandled = false;
	State.bHasDestination = true;
	State.Status = EBoidRouteStatus::Ready;
	State.Waypoints = {LocalGoal};
	State.WaypointIndex = 0;
	State.SegmentValidatedAt = -1.;
	Simulation->GetRouteService().BeginFrame(Simulation, Simulation->PlannerAgents);
	Simulation->UpdateNavigationGoals(0.f);
	TestTrue(TEXT("A recovered ready route replaces local fallback"), !State.bLocalTransitFallback && State.Navigation.bHasGoal && !State.Navigation.bHoldPosition);
	Simulation->RouteQueriesPerFrame = 0;
	Simulation->GetRouteService().BeginFrame(Simulation, Simulation->PlannerAgents);
	State.SegmentValidatedAt = -1.;
	Simulation->UpdateNavigationGoals(0.f);
	TestTrue(TEXT("Validation budget exhaustion keeps local pursuit moving"), State.bLocalTransitFallback && !State.Navigation.bHoldPosition && Boid->GetNavigationFacts().Status == EBoidRouteStatus::Pending);
	TestTrue(TEXT("Local fallback does not publish a fictional waypoint route"), Boid->GetNavigationFacts().Waypoints.IsEmpty());
	Simulation->RouteQueriesPerFrame = 32;
	Simulation->GetRouteService().BeginFrame(Simulation, Simulation->PlannerAgents);
	Simulation->UpdateNavigationGoals(0.f);
	TestFalse(TEXT("Ready route resumes once validation budget returns"), State.bLocalTransitFallback);
	Bounds->NavigationMode = EBoidNavigationMode::AuthoredRoutes;
	Simulation->CanSimulate();
	Simulation->GetRouteService().BeginFrame(Simulation, Simulation->PlannerAgents);
	Simulation->UpdateNavigationGoals(0.f);
	TestTrue(TEXT("AuthoredRoutes also permits local fallback without usable waypoints"), State.bLocalTransitFallback && !State.Navigation.bHoldPosition);
	Bounds->NavigationMode = EBoidNavigationMode::DirectOnly;
	Simulation->CanSimulate();
	State.LastSeenPosition = FVector(10000.f, 0.f, 0.f);
	NewIntent.Goal = State.LastSeenPosition;
	Boid->SetMovementIntent(NewIntent, Owner);
	Simulation->UpdateNavigationGoals(0.f);
	TestFalse(TEXT("An out-of-bounds destination cannot enable local Transit"), State.bLocalTransitFallback);
	NewIntent.bIsTransit = false;
	Boid->SetMovementIntent(NewIntent, Owner);
	Simulation->UpdateNavigationGoals(0.f);
	TestFalse(TEXT("Ordinary location tasks never acquire local Transit fallback"), State.bLocalTransitFallback);
	State.LastSeenAt = Simulation->NavigationTime - Simulation->LastKnownPursuitSeconds - .1;
	TestEqual(TEXT("Memory expiry still removes Transit eligibility"), Simulation->GetTransitGoalRevision(Boid), uint64(0));
	NewIntent.bIsTransit = true;
	NewIntent.Goal = LocalGoal;
	State.LastSeenPosition = LocalGoal;
	Boid->SetMovementIntent(NewIntent, Owner);
	Simulation->UpdateNavigationGoals(0.f);
	TestTrue(TEXT("Expired memory holds rather than steering toward a stale destination"), !State.bLocalTransitFallback && State.Navigation.bHoldPosition);
	Boid->Destroy();
	Simulation->ApplyQueuedRegistrations();
	TestFalse(TEXT("A removed agent cannot complete an old pursuit"), Simulation->TryCompleteTransitGoal(Boid, Owner, 2));
	return true;
}

#endif
