#include "NavigationVisualizer.h"
#include "World_Navigation/NavigationWaypoint.h"
#include "World_Navigation/RoomArea.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "CanvasItem.h"
#include "SceneManagement.h"
#include "PrimitiveDrawingUtils.h"
#include "SceneView.h"
#include "Engine/Canvas.h"

namespace BoidNavigationVisualization
{
	void DrawPoint(const ANavigationWaypoint* Point, FPrimitiveDrawInterface* PDI)
	{
		const FVector Position = Point->GetActorLocation();
		const FLinearColor Color = FLinearColor::Yellow;
		DrawWireSphere(PDI, Position, Color, 25.f, 16, SDPG_World, 2.f);
		for (const auto& Link : Point->Links)
		{
			if (!IsValid(Link.Destination)) continue;
			const FVector End = Link.Destination->GetActorLocation();
			const FVector Direction = (End - Position).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
			const FLinearColor LinkColor = Point == Link.Destination ? FLinearColor::Red : FLinearColor::Green;
			PDI->DrawLine(Position, End, LinkColor, SDPG_World, 2.f);
			auto Arrow = [&](const FVector& Tip, const FVector& Forward)
			{
				PDI->DrawLine(Tip, Tip - Forward * 35.f + Side * 15.f, LinkColor, SDPG_World, 2.f);
				PDI->DrawLine(Tip, Tip - Forward * 35.f - Side * 15.f, LinkColor, SDPG_World, 2.f);
			};
			Arrow(FMath::Lerp(Position, End, .65f), Direction);
			if (Link.bBidirectional) Arrow(FMath::Lerp(Position, End, .35f), -Direction);
		}
	}
}

void FNavigationVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	if (const auto* Point = Cast<ANavigationWaypoint>(Component->GetOwner())) BoidNavigationVisualization::DrawPoint(Point, PDI);
	if (const auto* Room = Cast<ARoomArea>(Component->GetOwner()))
	{
		TInlineComponentArray<UBoxComponent*> Boxes(Room);
		for (const auto* Box : Boxes) DrawWireBox(PDI, Box->GetComponentTransform().ToMatrixWithScale(), FBox(-Box->GetUnscaledBoxExtent(), Box->GetUnscaledBoxExtent()), FLinearColor(0.f, 1.f, 1.f), SDPG_World);
		for (TActorIterator<ANavigationWaypoint> It(Room->GetWorld()); It; ++It)
			if (It->Room == Room) BoidNavigationVisualization::DrawPoint(*It, PDI);
	}
}

void FNavigationVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	const AActor* Actor = Component->GetOwner();
	FVector2D Pixel;
	if (!Actor || !GEngine || !View->WorldToPixel(Actor->GetActorLocation(), Pixel)) return;
	FString Label = Actor->GetActorLabel();
	if (const auto* Room = Cast<ARoomArea>(Actor)) Label = (Room->RoomName.IsEmpty() ? Label : Room->RoomName) + TEXT(" [") + Room->GetRoomId().ToString() + TEXT("]");
	if (const auto* Point = Cast<ANavigationWaypoint>(Actor))
	{
		Label += IsValid(Point->Room) ? TEXT(" / ") + Point->Room->GetRoomId().ToString() : TEXT(" / Unassigned");
		for (const auto& Link : Point->Links) if (!IsValid(Link.Destination) || Link.Destination == Point) Label += TEXT(" / Invalid link");
	}
	FCanvasTextItem Text(Pixel + FVector2D(8, 8), FText::FromString(Label), GEngine->GetSmallFont(), FLinearColor::White);
	Text.EnableShadow(FLinearColor::Black); Canvas->DrawItem(Text);
}
