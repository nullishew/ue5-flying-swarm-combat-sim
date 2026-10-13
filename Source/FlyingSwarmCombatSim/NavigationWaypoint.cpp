#include "NavigationWaypoint.h"
#include "WorldNavigationSubsystem.h"
#include "Engine/World.h"

ANavigationWaypoint::ANavigationWaypoint()
{
	PrimaryActorTick.bCanEverTick = false;
	Marker = CreateDefaultSubobject<UNavigationMarkerComponent>(TEXT("Marker"));
	SetRootComponent(Marker);
#if WITH_EDITORONLY_DATA
	Marker->bVisualizeComponent = true;
#endif
}

void ANavigationWaypoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (GetWorld()) if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>()) Navigation->RegisterActor(this);
}

void ANavigationWaypoint::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>()) Navigation->RegisterActor(this);
}

void ANavigationWaypoint::EndPlay(EEndPlayReason::Type Reason)
{
	if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>()) Navigation->UnregisterActor(this);
	Super::EndPlay(Reason);
}

#if WITH_EDITOR
void ANavigationWaypoint::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	if (GetWorld()) if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>()) Navigation->Invalidate();
}
#endif

#if WITH_EDITOR
void ANavigationWaypoint::PostEditUndo()
{
	Super::PostEditUndo();
	if (GetWorld()) if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>())
	{
		if (IsActorBeingDestroyed()) Navigation->UnregisterActor(this); else Navigation->RegisterActor(this);
	}
}
#endif

void ANavigationWaypoint::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	if (GetWorld()) if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>()) Navigation->RegisterActor(this);
}

void ANavigationWaypoint::PostUnregisterAllComponents()
{
	if (GetWorld()) if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>()) Navigation->UnregisterActor(this);
	Super::PostUnregisterAllComponents();
}
