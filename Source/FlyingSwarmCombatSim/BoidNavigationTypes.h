#pragma once
#include "CoreMinimal.h"
#include "BoidNavigationTypes.generated.h"

class AFlyingBoid;

UENUM(BlueprintType)
enum class EBoidNavigationMode : uint8 { DirectOnly, AuthoredRoutes };
UENUM(BlueprintType)
enum class EBoidNavigationPhase : uint8 { None, Transit, Approach, Orbit, Retreat };
UENUM(BlueprintType)
enum class EBoidRouteStatus : uint8 { Pending, Ready, Failed };
UENUM(BlueprintType)
enum class EBoidRouteTier : uint8 { None, Direct, Detour, Graph };

/** Immutable circle definition captured before steering. */
USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FBoidOrbitContext
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Orbit") bool bActive = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Orbit") FVector Center = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Orbit") FVector Axis = FVector::UpVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Orbit") float RadiusCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Orbit") float HeightCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Orbit") float AngularSpeedDegPerSec = 15.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Orbit") int32 Direction = 1;
};

USTRUCT(BlueprintType)
struct FLYINGSWARMCOMBATSIM_API FBoidNavigationFacts
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") EBoidNavigationPhase Phase = EBoidNavigationPhase::None;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") EBoidRouteStatus Status = EBoidRouteStatus::Pending;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") EBoidRouteTier Tier = EBoidRouteTier::None;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") FVector Destination = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") FVector Waypoint = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") TArray<FVector> Waypoints;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") float LengthCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") int32 GoalVersion = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") FString FailureReason;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") FName RoomId = NAME_None;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") bool bHasTransitGoal = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") FVector TransitGoal = FVector::ZeroVector;
};

struct FBoidPlannerAgent { AFlyingBoid* Boid = nullptr; int32 Id = INDEX_NONE; };
struct FBoidFlightRouteResult
{
	EBoidRouteStatus Status = EBoidRouteStatus::Pending;
	EBoidRouteTier Tier = EBoidRouteTier::None;
	TArray<FVector> Waypoints;
	float LengthCm = 0.f;
	FString FailureReason;
	int32 GoalVersion = 0;
};
struct FBoidNavigationGoal
{
	FVector Goal = FVector::ZeroVector;
	bool bHasGoal = false;
	bool bContinuousGoal = false;
	bool bPreferNavigationGoal = false;
	bool bHoldPosition = false;
	float DesiredSpeed = 0.f;
	EBoidNavigationPhase Phase = EBoidNavigationPhase::None;
	FBoidOrbitContext OrbitContext;
};
