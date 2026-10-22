#include "Swarm/Debug/SwarmDebugPresenter.h"

#include "Swarm/Debug/SwarmDebugSettings.h"
#include "Swarm/FlyingBoid.h"
#include "Combat/BoidAttackComponent.h"
#include "Combat/BoidAttackProjectile.h"
#include "Player/FlyingCharacter.h"
#include "UI/SwarmDebugOverlayWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
	FString HoldLabel(EBoidCombatHoldReason Reason)
	{
		switch (Reason)
		{
		case EBoidCombatHoldReason::InvalidTarget: return TEXT("NO TARGET");
		case EBoidCombatHoldReason::InvalidOwnership: return TEXT("NO INTENT OWNER");
		case EBoidCombatHoldReason::SteeringDisabled: return TEXT("STEERING OFF");
		case EBoidCombatHoldReason::InvalidProfile: return TEXT("INVALID PROFILE");
		case EBoidCombatHoldReason::NoSelection: return TEXT("NO POSITION");
		case EBoidCombatHoldReason::PendingRoute: return TEXT("ROUTE WAIT");
		case EBoidCombatHoldReason::PendingBudget: return TEXT("BUDGET WAIT");
		case EBoidCombatHoldReason::OrbitBlocked: return TEXT("ORBIT BLOCKED");
		case EBoidCombatHoldReason::RetreatBlocked: return TEXT("RETREAT BLOCKED");
		case EBoidCombatHoldReason::ZeroSpeed: return TEXT("ZERO SPEED");
		default: return FString();
		}
	}

	FString PhaseLabel(const FBoidMovementIntent& Intent, EBoidNavigationPhase Phase)
	{
		switch (Phase)
		{
		case EBoidNavigationPhase::Transit: return TEXT("TRANSIT");
		case EBoidNavigationPhase::Approach: return TEXT("APPROACH");
		case EBoidNavigationPhase::Orbit: return TEXT("ORBIT");
		case EBoidNavigationPhase::Retreat: return TEXT("RETREAT");
		default: break;
		}
		switch (Intent.Policy)
		{
		case EBoidMovementPolicy::Cruise: return TEXT("CRUISE");
		case EBoidMovementPolicy::FollowStandOff: return TEXT("FOLLOW");
		case EBoidMovementPolicy::LocationArrival: return TEXT("LOCATION");
		default: return TEXT("RANGED");
		}
	}

	void DrawGoalLink(UWorld* World, const FVector& Start, const FVector& End, const FColor& Color, float Thickness)
	{
		// Bounded dash count, no retained line batches or diagnostic route queries.
		const int32 Count = FMath::Clamp(FMath::CeilToInt(FVector::Distance(Start, End) / 80.f), 1, 48);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			DrawDebugLine(World, FMath::Lerp(Start, End, Index / static_cast<float>(Count)), FMath::Lerp(Start, End, (Index + .55f) / Count), Color, false, 0.f, SDPG_Foreground, Thickness);
		}
	}

	void DrawProjectileRing(UWorld* World, const FVector& Center, double Radius, const FVector& AxisX, const FVector& AxisY, const FColor& Color, int32 Segments, float Thickness)
	{
		FVector Previous = Center + AxisX * Radius;
		for (int32 Index = 1; Index <= Segments; ++Index)
		{
			const double Angle = 2.0 * UE_PI * Index / Segments;
			const FVector Next = Center + (AxisX * FMath::Cos(Angle) + AxisY * FMath::Sin(Angle)) * Radius;
			DrawDebugLine(World, Previous, Next, Color, false, 0.f, SDPG_Foreground, Thickness);
			Previous = Next;
		}
	}

	void DrawProjectileBoundary(UWorld* World, const USphereComponent* Sphere, const FVector& ViewLocation, const FVector& ViewUp, const FVector& ViewRight, const FColor& Color, int32 Segments, float Thickness)
	{
		if (!IsValid(Sphere) || !Sphere->IsRegistered() || Sphere->GetWorld() != World || Sphere->GetCollisionEnabled() == ECollisionEnabled::NoCollision) return;
		const FVector Center = Sphere->GetComponentLocation();
		const double Radius = Sphere->GetScaledSphereRadius();
		if (Center.ContainsNaN() || ViewLocation.ContainsNaN() || !FMath::IsFinite(Radius) || Radius <= 0.0) return;
		const FVector Offset = ViewLocation - Center;
		const double DistanceSquared = Offset.SizeSquared();
		if (!FMath::IsFinite(DistanceSquared) || DistanceSquared <= Radius * Radius) return;
		const double Distance = FMath::Sqrt(DistanceSquared);
		const FVector ToCamera = Offset / Distance;
		FVector Right = FVector::CrossProduct(ViewUp, ToCamera).GetSafeNormal();
		if (Right.IsNearlyZero()) Right = FVector::CrossProduct(ViewRight, ToCamera).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(ToCamera, Right).GetSafeNormal();
		if (Right.IsNearlyZero() || Up.IsNearlyZero()) return;
		// The visible sphere edge lies on a tangent circle, not a centre-plane billboard.
		const FVector SilhouetteCenter = Center + ToCamera * (Radius * Radius / Distance);
		const double SilhouetteRadius = Radius * FMath::Sqrt(FMath::Max(0.0, 1.0 - Radius * Radius / DistanceSquared));
		DrawProjectileRing(World, SilhouetteCenter, SilhouetteRadius, Right, Up, Color, Segments, Thickness);
		DrawProjectileRing(World, Center, Radius, ToCamera, Up, Color, Segments, Thickness);
		DrawProjectileRing(World, Center, Radius, ToCamera, Right, Color, Segments, Thickness);
	}

	bool ProjectBadge(APlayerController& Controller, USwarmDebugOverlayWidget& Overlay, const FVector& Location, FVector2D& OutPosition)
	{
		FVector2D Screen;
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(&Controller, Location, Screen, true)) return false;
		const FGeometry PlayerGeometry = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(&Controller);
		const FGeometry& OverlayGeometry = Overlay.GetCachedGeometry();
		if (OverlayGeometry.GetLocalSize().X <= 0.f || OverlayGeometry.GetLocalSize().Y <= 0.f) return false;
		OutPosition = OverlayGeometry.AbsoluteToLocal(PlayerGeometry.LocalToAbsolute(Screen));
		return FMath::IsFinite(OutPosition.X) && FMath::IsFinite(OutPosition.Y);
	}
}

void FSwarmDebugPresenter::RestoreOutlines()
{
	for (const auto& Entry : OriginalMeshes)
	{
		if (UMeshComponent* Mesh = Entry.Key.Get())
		{
			Mesh->SetRenderCustomDepth(Entry.Value.bCustomDepth);
			Mesh->SetCustomDepthStencilValue(Entry.Value.Stencil);
			Mesh->SetCustomDepthStencilWriteMask(Entry.Value.WriteMask);
		}
	}
	OriginalMeshes.Reset();
}

void FSwarmDebugPresenter::DisableCollision()
{
	RestoreOutlines();
}

void FSwarmDebugPresenter::Shutdown()
{
	RestoreOutlines();
	if (USwarmSimulationSubsystem* Simulation = SuppressedSimulation.Get()) Simulation->ReleaseDebugPresentation();
	SuppressedSimulation.Reset();
	Agents.Reset();
	FrameBadges.Reset();
	RemainingRoute.Reset();
}

void FSwarmDebugPresenter::Update(APlayerController& Controller, USwarmDebugOverlayWidget& Overlay, const FSwarmDebugSettings& Settings, bool bCollision, bool bAgentState, bool bSpatial, bool bOutlineReady, const FString& OutlineFailure)
{
	UWorld* World = Controller.GetWorld();
	USwarmSimulationSubsystem* Simulation = World ? World->GetSubsystem<USwarmSimulationSubsystem>() : nullptr;
	if (SuppressedSimulation.Get() != Simulation)
	{
		Shutdown();
		SuppressedSimulation = Simulation;
		if (Simulation) Simulation->AcquireDebugPresentation();
	}
	const bool bDrawOutlines = bCollision && Settings.bOutlines && bOutlineReady;
	const bool bDrawState = bAgentState && Settings.bBadges;
	if (Simulation && (bDrawOutlines || bDrawState || bSpatial)) Simulation->GetDebugAgents(Agents);
	else Agents.Reset();
	if (!bDrawOutlines) RestoreOutlines();
	TSet<TWeakObjectPtr<UMeshComponent>> CurrentMeshes;
	FrameBadges.Reset();
	TArray<FSwarmDebugBadge>& Badges = FrameBadges;
	Badges.Reserve(Agents.Num());
	const bool bCanSimulate = Simulation && Simulation->IsDebugSimulationReady();
	const float MarkerSize = FMath::IsFinite(Settings.MarkerSizeCm) ? FMath::Clamp(Settings.MarkerSizeCm, 1.f, 200.f) : 18.f;
	const float ArrowSeconds = FMath::IsFinite(Settings.VelocitySeconds) ? FMath::Clamp(Settings.VelocitySeconds, .001f, 5.f) : .5f;
	const float ArrowCap = FMath::IsFinite(Settings.MaxVelocityArrowCm) ? FMath::Clamp(Settings.MaxVelocityArrowCm, 1.f, 10000.f) : 500.f;
	const float Thickness = FMath::IsFinite(Settings.LineThickness) ? FMath::Clamp(Settings.LineThickness, 1.f, 8.f) : 3.f;
	const float ReferenceDepth = FMath::IsFinite(Settings.BadgeReferenceDepthCm) ? FMath::Max(1.f, Settings.BadgeReferenceDepthCm) : 1000.f;
	const float MinScale = FMath::IsFinite(Settings.BadgeMinScale) ? FMath::Clamp(Settings.BadgeMinScale, .1f, 1.f) : .55f;
	const float BadgeHeight = FMath::IsFinite(Settings.BadgeHeightCm) ? FMath::Max(0.f, Settings.BadgeHeightCm) : 20.f;
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller.GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewForward = ViewRotation.Vector();
	const float Fov = Controller.PlayerCameraManager ? Controller.PlayerCameraManager->GetFOVAngle() : 90.f;
	const float FovScale = 1.f / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Fov, 5.f, 170.f) * .5f));
	for (const FSwarmDebugAgent& Agent : Agents)
	{
		AFlyingBoid* Boid = Agent.Boid.Get();
		if (!Simulation || !IsValid(Boid) || !Simulation->IsAgentRegistered(Boid)) continue;
		const FVector Position = Boid->GetActorLocation();
		if (Position.ContainsNaN()) continue;
		const FBoidTargetFacts Target = Boid->GetTargetFacts();
		const FBoidMovementIntent Intent = Boid->GetMovementIntent();
		const FBoidNavigationFacts Navigation = Boid->GetNavigationFacts();
		const FBoidCombatMovementFacts& Movement = Boid->CombatMovementFacts;
		FBoidNavigationGoal Goal;
		if (bSpatial || bDrawState) Simulation->GetDebugMovementView(Boid, Goal, RemainingRoute);
		const FBoidDebugAttackState Attack = (bDrawState || bDrawOutlines) && IsValid(Boid->AttackComponent) ? Boid->AttackComponent->GetDebugAttackState() : FBoidDebugAttackState();
		const AFlyingCharacter* PlayerTarget = Cast<AFlyingCharacter>(Target.TargetActor.Get());
		const bool bLiveTarget = IsValid(PlayerTarget) && PlayerTarget->IsAlive();
		const bool bPursuing = bLiveTarget && (Target.bDetected || (Intent.bIsTransit && Navigation.bHasTransitGoal));
		const int32 Category = bCanSimulate && Attack.bCanEngage ? 3 : (bPursuing ? 2 : 1);
		const FLinearColor Color = Category == 3 ? Settings.EngagingColor : (Category == 2 ? Settings.AwareColor : Settings.UnawareColor);
		if (bDrawOutlines)
		{
			TInlineComponentArray<UMeshComponent*> Meshes(Boid);
			for (UMeshComponent* Mesh : Meshes)
			{
				if (!IsValid(Mesh) || !Mesh->IsRegistered()) continue;
				const TWeakObjectPtr<UMeshComponent> Key(Mesh);
				CurrentMeshes.Add(Key);
				if (!OriginalMeshes.Contains(Key)) OriginalMeshes.Add(Key, {Mesh->bRenderCustomDepth != 0, Mesh->CustomDepthStencilValue, Mesh->CustomDepthStencilWriteMask});
				if (!Mesh->bRenderCustomDepth) Mesh->SetRenderCustomDepth(true);
				if (Mesh->CustomDepthStencilValue != Category) Mesh->SetCustomDepthStencilValue(Category);
				if (Mesh->CustomDepthStencilWriteMask != ERendererStencilMask::ERSM_Default) Mesh->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
			}
		}
		const bool bInFront = FVector::DotProduct(Position - ViewLocation, ViewForward) > 0.f;
		if (bDrawState && bInFront)
		{
			FSwarmDebugBadge Badge;
			FVector BoundsOrigin, BoundsExtent;
			if (IsValid(Boid->Visual))
			{
				BoundsOrigin = Boid->Visual->Bounds.Origin;
				BoundsExtent = Boid->Visual->Bounds.BoxExtent;
			}
			else Boid->GetActorBounds(true, BoundsOrigin, BoundsExtent);
			const FVector Anchor(BoundsOrigin.X, BoundsOrigin.Y, BoundsOrigin.Z + BoundsExtent.Z + BadgeHeight);
			if (!Anchor.ContainsNaN() && ProjectBadge(Controller, Overlay, Anchor, Badge.Position))
			{
				const float Depth = FVector::DotProduct(Anchor - ViewLocation, ViewForward);
				Badge.Scale = FMath::Clamp(ReferenceDepth * FovScale / FMath::Max(1.f, Depth), MinScale, 1.f);
				const bool bHolding = Movement.HoldReason != EBoidCombatHoldReason::None;
				const EBoidNavigationPhase Phase = Movement.ExecutedPhase != EBoidNavigationPhase::None ? Movement.ExecutedPhase : Navigation.Phase;
				Badge.Text = FString::Printf(TEXT("#%d %s"), Agent.Id, bCanSimulate ? (bHolding ? TEXT("HOLD") : *PhaseLabel(Intent, Phase)) : TEXT("SUSPENDED"));
				if (bCanSimulate && bHolding && FMath::IsFinite(Movement.HoldSeconds)) Badge.Text += FString::Printf(TEXT("\nHold %.1fs"), Movement.HoldSeconds);
				else if (bCanSimulate && (Phase == EBoidNavigationPhase::Orbit || Phase == EBoidNavigationPhase::Retreat) && bLiveTarget && FMath::IsFinite(Target.DistanceCm)) Badge.Text += FString::Printf(TEXT("\nRange %.1fm"), Target.DistanceCm / 100.f);
				else if (bCanSimulate && (Phase == EBoidNavigationPhase::Approach || Phase == EBoidNavigationPhase::Transit))
				{
					if (Goal.bHasGoal && !Goal.Goal.ContainsNaN()) Badge.Text += FString::Printf(TEXT("\nGoal %.1fm"), FVector::Distance(Position, Goal.Goal) / 100.f);
				}
				FString Reason = bCanSimulate ? HoldLabel(Movement.HoldReason) : TEXT("CHECK BOUNDS/SETUP");
				if (Reason.IsEmpty() && Navigation.Status == EBoidRouteStatus::Pending && Navigation.Phase != EBoidNavigationPhase::None) Reason = TEXT("ROUTE WAIT");
				if (Reason.IsEmpty() && Navigation.Status == EBoidRouteStatus::Failed && Navigation.Phase != EBoidNavigationPhase::None) Reason = TEXT("ROUTE FAILED");
				if (Reason.IsEmpty() && Target.bDetected && !Target.bHasSight) Reason = TEXT("NO SIGHT");
				if (Reason.IsEmpty() && Movement.Constraint == EBoidCombatMovementConstraint::Arrival) Reason = TEXT("ARRIVED");
				if (Reason.IsEmpty() && Movement.Constraint == EBoidCombatMovementConstraint::Obstacle) Reason = TEXT("OBSTACLE LIMIT");
				if (Reason.IsEmpty() && Movement.Constraint == EBoidCombatMovementConstraint::EmergencySeparation) Reason = TEXT("BODY SEPARATION");
				if (!Reason.IsEmpty())
				{
					int32 FirstBreak;
					if (Badge.Text.FindChar(TEXT('\n'), FirstBreak)) Badge.Text.InsertAt(FirstBreak, TEXT("\n") + Reason);
					else Badge.Text += TEXT("\n") + Reason;
				}
				if (bCanSimulate && Attack.bHasCadence) Badge.Text += FString::Printf(TEXT("\nAttempt %.1fs"), Attack.SecondsUntilAttempt);
				Badge.Color = Color;
				Badges.Add(MoveTemp(Badge));
			}
		}
		if (!bSpatial || !bCanSimulate) continue;
		const bool bHolding = Goal.bHoldPosition || Movement.HoldReason != EBoidCombatHoldReason::None;
		const EBoidNavigationPhase Phase = Movement.ExecutedPhase != EBoidNavigationPhase::None ? Movement.ExecutedPhase : Goal.Phase;
		if (Settings.bDestinations && bHolding)
		{
			DrawDebugPoint(World, Position, MarkerSize * .5f, Settings.WaypointColor.ToFColor(true), false, 0.f, SDPG_Foreground);
		}
		else if (Settings.bDestinations && Phase != EBoidNavigationPhase::Orbit && Goal.bHasGoal && !Goal.Goal.ContainsNaN())
		{
			DrawGoalLink(World, Position, Goal.Goal, Settings.DestinationColor.ToFColor(true), Thickness);
			DrawDebugPoint(World, Goal.Goal, MarkerSize * .5f, Settings.DestinationColor.ToFColor(true), false, 0.f, SDPG_Foreground);
		}
		if (Settings.bWaypoints && !bHolding && Phase != EBoidNavigationPhase::Orbit)
		{
			const FColor RouteColor = (Settings.WaypointColor * .55f).ToFColor(true);
			for (int32 Index = 1; Index < RemainingRoute.Num(); ++Index)
			{
				DrawDebugLine(World, RemainingRoute[Index - 1], RemainingRoute[Index], RouteColor, false, 0.f, SDPG_Foreground, FMath::Max(1.f, Thickness * .5f));
			}
		}
		const FVector Velocity = Boid->GetFlightVelocity();
		if (Settings.bVelocity && !Velocity.ContainsNaN() && !Velocity.IsNearlyZero()) DrawDebugDirectionalArrow(World, Position, Position + Velocity.GetClampedToMaxSize(ArrowCap / ArrowSeconds) * ArrowSeconds, MarkerSize, Settings.VelocityColor.ToFColor(true), false, 0.f, SDPG_Foreground, Thickness);
	}
	// Enemy shots remain observable without a ready simulation, living shooter or outline material.
	if (bCollision && Settings.bProjectileShapes && World)
	{
		const int32 Segments = FMath::Clamp(Settings.ProjectileRingSegments, 8, 64);
		const float RingThickness = FMath::IsFinite(Settings.ProjectileLineThickness) ? FMath::Clamp(Settings.ProjectileLineThickness, .5f, 4.f) : 1.5f;
		const FColor RingColor = Settings.ProjectileColor.ToFColor(true);
		const FVector ViewUp = ViewRotation.RotateVector(FVector::UpVector);
		const FVector ViewRight = ViewRotation.RotateVector(FVector::RightVector);
		for (TActorIterator<AFlyingBoidAttackProjectile> It(World); It; ++It)
		{
			AFlyingBoidAttackProjectile* Projectile = *It;
			if (IsValid(Projectile) && Projectile->HasActorBegunPlay() && Projectile->GetActorEnableCollision() && !Projectile->IsActorBeingDestroyed())
			{
				DrawProjectileBoundary(World, Projectile->CollisionComponent.Get(), ViewLocation, ViewUp, ViewRight, RingColor, Segments, RingThickness);
			}
		}
	}

	for (auto It = OriginalMeshes.CreateIterator(); It; ++It)
	{
		if (CurrentMeshes.Contains(It.Key())) continue;
		if (UMeshComponent* Mesh = It.Key().Get())
		{
			Mesh->SetRenderCustomDepth(It.Value().bCustomDepth);
			Mesh->SetCustomDepthStencilValue(It.Value().Stencil);
			Mesh->SetCustomDepthStencilWriteMask(It.Value().WriteMask);
		}
		It.RemoveCurrent();
	}
	FString Legend = FString::Printf(TEXT("Collision [%s] | Agent State [%s] | Spatial [%s]"), bCollision ? TEXT("ON") : TEXT("OFF"), bAgentState ? TEXT("ON") : TEXT("OFF"), bSpatial ? TEXT("ON") : TEXT("OFF"));
	if (bCollision && Settings.bOutlines) Legend += TEXT("\nAgent outlines: white unaware | Amber aware | Red attack conditions");
	if (bCollision && Settings.bProjectileShapes) Legend += TEXT("\nMagenta rings: enemy projectile collider | X-ray, true radius");
	if (bAgentState) Legend += TEXT("\nAttempt = next opportunity, not a guaranteed shot");
	if (bSpatial) Legend += FString::Printf(TEXT("\nGold dashed goal | Cyan remaining route | Green velocity\nX-ray graphics | Velocity: %.2fs, capped %.0fcm"), ArrowSeconds, ArrowCap);
	if (bCollision && Settings.bOutlines && !bOutlineReady) Legend += TEXT("\nOutlines unavailable: ") + OutlineFailure;
	Overlay.SetFrame(MoveTemp(Badges), Legend, Settings.FontSize, Settings.BadgeBackground);
	// The widget swapped its previous allocation back; clear old values while reusing capacity.
	FrameBadges.Reset();
}
