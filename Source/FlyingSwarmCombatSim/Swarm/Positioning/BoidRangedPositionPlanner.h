#pragma once

#include "CoreMinimal.h"
#include "Swarm/Positioning/BoidRangedPositioning.h"
#include "Swarm/Routing/BoidNavigationTypes.h"

class USwarmSimulationSubsystem;
class AFlyingBoid;
class AActor;

struct FBoidRangedSelection
{
    TWeakObjectPtr<AActor> Target;
    FVector Destination = FVector::ZeroVector;
    FVector Offset = FVector::ZeroVector;
    FVector Axis = FVector::UpVector;
    FVector ReservationPosition = FVector::ZeroVector;
    FVector PreferredDirection = FVector::ForwardVector;
    FBoidFlightRouteResult Route;
    uint32 RouteGeneration = 0;
    EBoidNavigationMode NavigationMode = EBoidNavigationMode::DirectOnly;
    uint32 ChoiceSeed = 0;
    uint32 IntentRevision = 0;
    int32 Version = 0;
    bool bHasPosition = false;
    bool bUsingAdaptiveRange = false;
    float Score = -1.f;
    float Open = 0.f;
    float SeedAffinity = 0.f;
    float LastSearchSeconds = 0.f;
    float SearchAgeSeconds = 0.f;
    int32 CandidatesEvaluated = 0;
    int32 CandidateCount = 0;
    int32 Rebases = 0;
    int32 Rejections = 0;
    int32 StaleResults = 0;
    FString FailureReason;
};

/** Selects safe ranged destinations. It does not steer boids or own readiness. */
class FBoidRangedPositionPlanner
{
public:
    void Update(USwarmSimulationSubsystem& Runner, TConstArrayView<FBoidPlannerAgent> Agents, float DeltaTime);
    const FBoidRangedSelection* FindSelection(int32 AgentId) const;
    void SetReservation(int32 AgentId, const FVector& Position);
    void RequestRefresh(int32 AgentId);
    void ResetPosition(int32 AgentId);
    void ResetAgent(int32 AgentId);

private:
    struct FCandidate
    {
        FVector Offset = FVector::ZeroVector;
        FVector Position = FVector::ZeroVector;
        float Open = 0.f;
        float Estimate = -1.f;
        float Score = -1.f;
        float LengthCm = 0.f;
        int32 Index = 0;
        int32 Version = 0;
        bool bAdaptive = false;
        bool bEvaluated = false;
        bool bResolved = false;
        uint64 RouteHandle = 0;
        FVector RequestedEndpoint = FVector::ZeroVector;
        TWeakObjectPtr<AActor> RequestedTarget;
        uint32 RequestedIntentRevision = 0;
        uint32 RequestedGraphGeneration = 0;
        FBoidFlightRouteResult Route;
        FString FailureReason;
    };
    struct FSearchState
    {
        FBoidRangedSelection Selection;
        TArray<FCandidate> Candidates;
        FVector SearchCenter = FVector::ZeroVector;
        double SearchStartedSeconds = 0.;
        float RetrySeconds = 0.f;
        int32 NextCandidate = 0;
        int32 NextVersion = 0;
        uint64 OwnerToken = 0;
        uint32 ConfigurationSignature = 0;
        bool bSearchActive = false;
        bool bForceRefresh = false;
        bool bExplicitRefresh = false;
    };
    bool ValidateRangedPosition(USwarmSimulationSubsystem& Runner, AFlyingBoid* Boid, const FVector& Goal,
        const FVector& Center, AActor* Target, FString& Rejection) const;
    float EvaluateRangedPosition(USwarmSimulationSubsystem& Runner, AFlyingBoid* Boid, const FVector& Goal,
        const FVector& Center, AActor* Target, const FBoidRangedSettings& Settings, float& Open, FString& Rejection) const;
    float ScorePosition(AFlyingBoid* Boid, const FVector& Goal, const FVector& Center, float Open,
        float TravelLength, const FBoidRangedSettings& Settings, float* OutSeedAffinity = nullptr) const;
    void CancelSearch(FSearchState& State);

    TMap<int32, FSearchState> RuntimeByAgent;
    TMap<int32, FVector> Reservations;
    TArray<FBoidPlannerAgent> ActiveAgents;
    USwarmSimulationSubsystem* Host = nullptr;
    int32 SearchCursor = 0;
};
