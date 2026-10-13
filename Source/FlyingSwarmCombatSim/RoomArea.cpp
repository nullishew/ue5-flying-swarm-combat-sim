#include "RoomArea.h"
#include "NavigationWaypoint.h"
#include "WorldNavigationSubsystem.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"

ARoomArea::ARoomArea()
{
	PrimaryActorTick.bCanEverTick = false;
	RegionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("RegionBounds"));
	SetRootComponent(RegionBounds);
	RegionBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RegionBounds->SetGenerateOverlapEvents(false);
	RegionBounds->SetCanEverAffectNavigation(false);
	RegionBounds->SetBoxExtent(FVector(500.f));
	NavigationMarker = CreateDefaultSubobject<UNavigationMarkerComponent>(TEXT("NavigationMarker"));
	NavigationMarker->SetupAttachment(RegionBounds);
}

bool ARoomArea::Contains(const FVector& Position, float InsetCm) const
{
	if (Position.ContainsNaN()) return false;
	TInlineComponentArray<UBoxComponent*> Boxes(this);
	for (const UBoxComponent* Box : Boxes)
	{
		const FVector Local = Box->GetComponentTransform().InverseTransformPosition(Position);
		const FVector Scale = Box->GetComponentScale().GetAbs();
		if (Scale.ContainsNaN() || Scale.GetMin() <= UE_SMALL_NUMBER) continue;
		const FVector Extent = Box->GetUnscaledBoxExtent() - FVector(InsetCm / FMath::Max(.001f, Scale.X),
			InsetCm / FMath::Max(.001f, Scale.Y), InsetCm / FMath::Max(.001f, Scale.Z));
		if (Extent.GetMin() >= 0 && FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z) return true;
	}
	return false;
}

void ARoomArea::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (GetWorld()) if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>()) Navigation->RegisterActor(this);
}

void ARoomArea::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>()) Navigation->RegisterActor(this);
}

void ARoomArea::EndPlay(EEndPlayReason::Type Reason)
{
	if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>()) Navigation->UnregisterActor(this);
	Super::EndPlay(Reason);
}

#if WITH_EDITOR
void ARoomArea::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	if (GetWorld()) if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>()) Navigation->Invalidate();
}
#endif

#if WITH_EDITOR
void ARoomArea::PostEditUndo()
{
	Super::PostEditUndo();
	if (GetWorld()) if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>())
	{
		if (IsActorBeingDestroyed()) Navigation->UnregisterActor(this); else Navigation->RegisterActor(this);
	}
}
#endif

void ARoomArea::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	if (GetWorld()) if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>()) Navigation->RegisterActor(this);
}

void ARoomArea::PostUnregisterAllComponents()
{
	if (GetWorld()) if (auto* Navigation = GetWorld()->GetSubsystem<UWorldNavigationSubsystem>()) Navigation->UnregisterActor(this);
	Super::PostUnregisterAllComponents();
}
