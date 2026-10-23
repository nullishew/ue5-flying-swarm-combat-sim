#include "Swarm/Debug/SwarmDebugPresenter.h"

#include "Swarm/Debug/SwarmDebugSettings.h"
#include "Swarm/Debug/SwarmDebugPresentation.h"
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
#include "EngineGlobals.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/PlayerController.h"

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
			DrawDebugLine(World, FMath::Lerp(Start, End, Index / static_cast<float>(Count)),
			              FMath::Lerp(Start, End, (Index + .55f) / Count), Color, false, 0.f, SDPG_Foreground,
			              Thickness);
		}
	}

	void DrawProjectileRing(UWorld* World, const FVector& Center, double Radius, const FVector& AxisX,
	                        const FVector& AxisY, const FColor& Color, int32 Segments, float Thickness)
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

	void DrawProjectileBoundary(UWorld* World, const USphereComponent* Sphere, const FVector& ViewLocation,
	                            const FVector& ViewUp, const FVector& ViewRight, const FColor& Color, int32 Segments,
	                            float Thickness)
	{
		if (!IsValid(Sphere) || !Sphere->IsRegistered() || Sphere->GetWorld() != World || Sphere->GetCollisionEnabled()
			== ECollisionEnabled::NoCollision) return;
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

	bool ProjectBadge(APlayerController& Controller, USwarmDebugOverlayWidget& Overlay, const FVector& Location,
	                  FVector2D& OutPosition)
	{
		FVector2D Screen;
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(&Controller, Location, Screen, true)) return
			false;
		const FGeometry PlayerGeometry = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(&Controller);
		const FGeometry& OverlayGeometry = Overlay.GetCachedGeometry();
		if (OverlayGeometry.GetLocalSize().X <= 0.f || OverlayGeometry.GetLocalSize().Y <= 0.f) return false;
		OutPosition = OverlayGeometry.AbsoluteToLocal(PlayerGeometry.LocalToAbsolute(Screen));
		return FMath::IsFinite(OutPosition.X) && FMath::IsFinite(OutPosition.Y);
	}

	bool ProjectBadgeBounds(APlayerController& Controller, USwarmDebugOverlayWidget& Overlay, const FVector& Origin,
	                        const FVector& Extent, const FVector& ViewLocation, const FVector& ViewForward,
	                        float Height, float Gap, FVector2D& OutPosition)
	{
		if (Origin.ContainsNaN() || Extent.ContainsNaN() || !ProjectBadge(Controller, Overlay, Origin, OutPosition))
			return false;
		double Top = OutPosition.Y;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point = Origin + FVector((Corner & 1) ? Extent.X : -Extent.X,
			                                       (Corner & 2) ? Extent.Y : -Extent.Y,
			                                       (Corner & 4) ? Extent.Z : -Extent.Z);
			FVector2D Projected;
			// A box crossing the camera plane has no useful above-body card placement.
			if (FVector::DotProduct(Point - ViewLocation, ViewForward) <= 1.f || !ProjectBadge(
				Controller, Overlay, Point, Projected)) return false;
			Top = FMath::Min(Top, Projected.Y);
		}
		FVector2D RaisedAnchor;
		if (ProjectBadge(Controller, Overlay, Origin + FVector(0.f, 0.f, Extent.Z + Height), RaisedAnchor)) Top =
			FMath::Min(Top, RaisedAnchor.Y);
		OutPosition.Y = Top - Gap;
		return true;
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
	FrameLabels.Reset();
	RemainingRoute.Reset();
	InspectorLines.Reset();
	InspectorAgentId = INDEX_NONE;
	NextInspectorRefresh = 0.;
	bInspectorSampleFresh = false;
}

void FSwarmDebugPresenter::Update(APlayerController& Controller, USwarmDebugOverlayWidget& Overlay,
                                  const FSwarmDebugSettings& Settings, bool bCollision, bool bAgentState, bool bSpatial,
                                  bool bOutlineReady, const FString& OutlineFailure, AFlyingBoid* SelectedAgent,
                                  int32 SelectedAgentId)
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
	FrameLabels.Reset();
	TArray<FSwarmDebugBadge>& Badges = FrameBadges;
	Badges.Reserve(Agents.Num());
	const bool bCanSimulate = Simulation && Simulation->IsDebugSimulationReady();
	const float MarkerSize = FMath::IsFinite(Settings.MarkerSizeCm)
		                         ? FMath::Clamp(Settings.MarkerSizeCm, 1.f, 200.f)
		                         : 18.f;
	const float ArrowSeconds = FMath::IsFinite(Settings.VelocitySeconds)
		                           ? FMath::Clamp(Settings.VelocitySeconds, .001f, 5.f)
		                           : .5f;
	const float ArrowCap = FMath::IsFinite(Settings.MaxVelocityArrowCm)
		                       ? FMath::Clamp(Settings.MaxVelocityArrowCm, 1.f, 10000.f)
		                       : 500.f;
	const float Thickness = FMath::IsFinite(Settings.LineThickness)
		                        ? FMath::Clamp(Settings.LineThickness, 1.f, 8.f)
		                        : 3.f;
	const float BadgeHeight = Settings.BadgeHeightCm;
	const float BadgeGap = Settings.BadgeScreenGap;
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller.GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewForward = ViewRotation.Vector();
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
		if (bSpatial) Simulation->GetDebugMovementView(Boid, Goal, RemainingRoute);
		const FBoidDebugAttackState Attack = (bDrawState || bDrawOutlines) && IsValid(Boid->AttackComponent)
			                                     ? Boid->AttackComponent->GetDebugAttackState()
			                                     : FBoidDebugAttackState();
		const AFlyingCharacter* PlayerTarget = Cast<AFlyingCharacter>(Target.TargetActor.Get());
		const bool bLiveTarget = IsValid(PlayerTarget) && PlayerTarget->IsAlive();
		const bool bPursuing = bLiveTarget && (Target.bDetected || (Intent.bIsTransit && Navigation.bHasTransitGoal));
		const int32 Category = bCanSimulate && Attack.bCanEngage ? 3 : (bPursuing ? 2 : 1);
		const FLinearColor Color = Category == 3
			                           ? Settings.EngagingColor
			                           : (Category == 2 ? Settings.AwareColor : Settings.UnawareColor);
		if (bDrawOutlines)
		{
			TInlineComponentArray<UMeshComponent*> Meshes(Boid);
			for (UMeshComponent* Mesh : Meshes)
			{
				if (!IsValid(Mesh) || !Mesh->IsRegistered()) continue;
				const TWeakObjectPtr<UMeshComponent> Key(Mesh);
				CurrentMeshes.Add(Key);
				if (!OriginalMeshes.Contains(Key)) OriginalMeshes.Add(Key, {
					                                                      Mesh->bRenderCustomDepth != 0,
					                                                      Mesh->CustomDepthStencilValue,
					                                                      Mesh->CustomDepthStencilWriteMask
				                                                      });
				if (!Mesh->bRenderCustomDepth) Mesh->SetRenderCustomDepth(true);
				if (Mesh->CustomDepthStencilValue != Category) Mesh->SetCustomDepthStencilValue(Category);
				if (Mesh->CustomDepthStencilWriteMask != ERendererStencilMask::ERSM_Default) Mesh->
					SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
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
			if (ProjectBadgeBounds(Controller, Overlay, BoundsOrigin, BoundsExtent, ViewLocation, ViewForward,
			                       BadgeHeight, BadgeGap, Badge.Position))
			{
				Badge.DistanceAlpha = SwarmDebugPresentation::BadgeDistanceAlpha(
					Settings, FVector::Distance(Position, ViewLocation));
				Badge.HealthFraction = FMath::IsFinite(Boid->CurrentHealth) && FMath::IsFinite(Boid->MaxHealth) && Boid
				                       ->MaxHealth > 0.f
					                       ? FMath::Clamp(Boid->CurrentHealth / Boid->MaxHealth, 0.f, 1.f)
					                       : 0.f;
				Badge.bHasCadence = bCanSimulate && Attack.bHasCadence && FMath::IsFinite(Attack.AttemptIntervalSeconds)
					&& Attack.AttemptIntervalSeconds > 0.f && FMath::IsFinite(Attack.SecondsUntilAttempt);
				Badge.SecondsUntilAttempt = Badge.bHasCadence ? FMath::Max(0.f, Attack.SecondsUntilAttempt) : 0.f;
				Badge.AttackFraction = Badge.bHasCadence
					                       ? FMath::Clamp(Badge.SecondsUntilAttempt / Attack.AttemptIntervalSeconds,
					                                      0.f, 1.f)
					                       : 0.f;
				const bool bHolding = Movement.HoldReason != EBoidCombatHoldReason::None;
				const EBoidNavigationPhase Phase = Movement.ExecutedPhase != EBoidNavigationPhase::None
					                                   ? Movement.ExecutedPhase
					                                   : Navigation.Phase;
				Badge.Text = FString::Printf(
					TEXT("#%d %s"), Agent.Id,
					bCanSimulate ? (bHolding ? TEXT("HOLD") : *PhaseLabel(Intent, Phase)) : TEXT("SUSPENDED"));
				FString Reason = bCanSimulate ? HoldLabel(Movement.HoldReason) : TEXT("CHECK BOUNDS/SETUP");
				if (Reason.IsEmpty() && Navigation.Status == EBoidRouteStatus::Pending && Navigation.Phase !=
					EBoidNavigationPhase::None) Reason = TEXT("ROUTE WAIT");
				if (Reason.IsEmpty() && Navigation.Status == EBoidRouteStatus::Failed && Navigation.Phase !=
					EBoidNavigationPhase::None) Reason = TEXT("ROUTE FAILED");
				if (Reason.IsEmpty() && Target.bDetected && !Target.bHasSight) Reason = TEXT("NO SIGHT");
				if (Reason.IsEmpty() && Movement.Constraint == EBoidCombatMovementConstraint::Arrival) Reason = TEXT(
					"ARRIVED");
				if (Reason.IsEmpty() && Movement.Constraint == EBoidCombatMovementConstraint::Obstacle) Reason = TEXT(
					"OBSTACLE LIMIT");
				if (Reason.IsEmpty() && Movement.Constraint == EBoidCombatMovementConstraint::EmergencySeparation)
					Reason = TEXT("BODY SEPARATION");
				Badge.Reason = MoveTemp(Reason);
				Badge.Color = Color;
				Badges.Add(MoveTemp(Badge));
			}
		}
		if (!bSpatial || !bCanSimulate) continue;
		const bool bHolding = Goal.bHoldPosition || Movement.HoldReason != EBoidCombatHoldReason::None;
		const EBoidNavigationPhase Phase = Movement.ExecutedPhase != EBoidNavigationPhase::None
			                                   ? Movement.ExecutedPhase
			                                   : Goal.Phase;
		if (Settings.bDestinations && bHolding)
		{
			DrawDebugPoint(World, Position, MarkerSize * .5f, Settings.WaypointColor.ToFColor(true), false, 0.f,
			               SDPG_Foreground);
		}
		else if (Settings.bDestinations && Phase != EBoidNavigationPhase::Orbit && Goal.bHasGoal && !Goal.Goal.
			ContainsNaN())
		{
			DrawGoalLink(World, Position, Goal.Goal, Settings.DestinationColor.ToFColor(true), Thickness);
			DrawDebugPoint(World, Goal.Goal, MarkerSize * .5f, Settings.DestinationColor.ToFColor(true), false, 0.f,
			               SDPG_Foreground);
			FVector2D StartScreen, EndScreen;
			if (FVector::DotProduct(Position - ViewLocation, ViewForward) > 1.f && FVector::DotProduct(
					Goal.Goal - ViewLocation, ViewForward) > 1.f
				&& ProjectBadge(Controller, Overlay, Position, StartScreen) && ProjectBadge(
					Controller, Overlay, Goal.Goal, EndScreen))
			{
				FSwarmDebugLabel Label;
				Label.Position = (StartScreen + EndScreen) * .5f + FVector2D(0.f, -8.f);
				Label.Text = FString::Printf(
					TEXT("#%d  %.1f m"), Agent.Id, FVector::Distance(Position, Goal.Goal) / 100.f);
				FrameLabels.Add(MoveTemp(Label));
			}
		}
		if (Settings.bWaypoints && !bHolding && Phase != EBoidNavigationPhase::Orbit)
		{
			const FColor RouteColor = (Settings.WaypointColor * .55f).ToFColor(true);
			for (int32 Index = 1; Index < RemainingRoute.Num(); ++Index)
			{
				DrawDebugLine(World, RemainingRoute[Index - 1], RemainingRoute[Index], RouteColor, false, 0.f,
				              SDPG_Foreground, FMath::Max(1.f, Thickness * .5f));
			}
		}
		const FVector Velocity = Boid->GetFlightVelocity();
		if (Settings.bVelocity && !Velocity.ContainsNaN() && !Velocity.IsNearlyZero()) DrawDebugDirectionalArrow(
			World, Position, Position + Velocity.GetClampedToMaxSize(ArrowCap / ArrowSeconds) * ArrowSeconds,
			MarkerSize, Settings.VelocityColor.ToFColor(true), false, 0.f, SDPG_Foreground, Thickness);
	}
	// Enemy shots remain observable without a ready simulation, living shooter or outline material.
	if (bCollision && Settings.bProjectileShapes && World)
	{
		const int32 Segments = FMath::Clamp(Settings.ProjectileRingSegments, 8, 64);
		const float RingThickness = FMath::IsFinite(Settings.ProjectileLineThickness)
			                            ? FMath::Clamp(Settings.ProjectileLineThickness, .5f, 4.f)
			                            : 1.5f;
		const FColor RingColor = Settings.ProjectileColor.ToFColor(true);
		const FVector ViewUp = ViewRotation.RotateVector(FVector::UpVector);
		const FVector ViewRight = ViewRotation.RotateVector(FVector::RightVector);
		for (TActorIterator<AFlyingBoidAttackProjectile> It(World); It; ++It)
		{
			AFlyingBoidAttackProjectile* Projectile = *It;
			if (IsValid(Projectile) && Projectile->HasActorBegunPlay() && Projectile->GetActorEnableCollision() && !
				Projectile->IsActorBeingDestroyed())
			{
				DrawProjectileBoundary(World, Projectile->CollisionComponent.Get(), ViewLocation, ViewUp, ViewRight,
				                       RingColor, Segments, RingThickness);
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
	FSwarmDebugInspectorView Inspector;
	if (Simulation && IsValid(SelectedAgent) && SelectedAgent->IsAlive() && Simulation->IsAgentRegistered(SelectedAgent) && Simulation->GetAgentId(SelectedAgent) == SelectedAgentId)
	{
		Inspector.AgentId = SelectedAgentId;
		FSwarmMovementDebugSnapshot Sample;
		Simulation->GetDebugMovementSnapshot(SelectedAgent, Sample);
		const bool bFresh = SwarmDebugPresentation::IsCurrentMovementSample(Sample, GFrameCounter, bCanSimulate);
		const double Now = FPlatformTime::Seconds();
		if (InspectorAgentId != SelectedAgentId || Now >= NextInspectorRefresh || bInspectorSampleFresh != bFresh)
		{
			InspectorAgentId = SelectedAgentId;
			bInspectorSampleFresh = bFresh;
			NextInspectorRefresh = Now + .1;
			InspectorLines.Reset();
			const FBoidMovementIntent Intent = SelectedAgent->GetMovementIntent();
			const FBoidTargetFacts Target = SelectedAgent->GetTargetFacts();
			const FBoidNavigationFacts Navigation = SelectedAgent->GetNavigationFacts();
			const FBoidCombatMovementFacts Movement = SelectedAgent->GetCombatMovementFacts();
			InspectorLines.Add(FString::Printf(TEXT("Live intent: %s | transit %s | steering %s"), *StaticEnum<EBoidMovementPolicy>()->GetNameStringByValue(static_cast<int64>(Intent.Policy)), Intent.bIsTransit ? TEXT("YES") : TEXT("NO"), SelectedAgent->IsSteeringActive() ? TEXT("ON") : TEXT("OFF")));
			if (bFresh)
			{
				const FString RequestedPolicy = StaticEnum<EBoidMovementPolicy>()->GetNameStringByValue(static_cast<int64>(Sample.RequestedPolicy));
				const FString ResolvedPolicy = Sample.bHasResolvedPolicy ? StaticEnum<EBoidMovementPolicy>()->GetNameStringByValue(static_cast<int64>(Sample.ResolvedPolicy)) : TEXT("NOT RESOLVED");
				InspectorLines.Add(FString::Printf(TEXT("Step policy: %s -> %s"), *RequestedPolicy, *ResolvedPolicy));
				InspectorLines.Add(FString::Printf(TEXT("Move: %s | %s"), SwarmDebugPresentation::MovementCommitLabel(Sample.Commit.Result), *Sample.Stage));
				InspectorLines.Add(FString::Printf(TEXT("Speed: requested %.1f / displaced %.1f cm/s"), Sample.RequestedSpeed, Sample.ActualSpeed));
				InspectorLines.Add(FString::Printf(TEXT("Steering: %s | intentional stop %s"), *StaticEnum<EBoidCombatMovementConstraint>()->GetNameStringByValue(static_cast<int64>(Sample.Constraint)), Sample.bIntentionalStop ? TEXT("YES") : TEXT("NO")));
				InspectorLines.Add(FString::Printf(TEXT("Step %.4fs | move %.2f / clamp %.2f / actual %.2f cm"), Sample.StepSeconds, Sample.Commit.RequestedDelta.Size(), Sample.Commit.ClampedDelta.Size(), Sample.ActualDelta.Size()));
				InspectorLines.Add(FString::Printf(TEXT("Acceleration: %s cm/s2"), *Sample.Acceleration.ToCompactString()));
				InspectorLines.Add(FString::Printf(TEXT("Heading: steering %s / flight %s"), *Sample.SteeringHeading.ToCompactString(), *Sample.FlightHeading.ToCompactString()));
				InspectorLines.Add(FString::Printf(TEXT("Displacement: %s cm | SafeMove returned %s"), *Sample.ActualDelta.ToCompactString(), Sample.Commit.bMoveReturned ? TEXT("YES") : TEXT("NO")));
				InspectorLines.Add(FString::Printf(TEXT("Bounds: clamped %s | correction %.2f cm"), Sample.Commit.bBoundsClamped ? TEXT("YES") : TEXT("NO"), Sample.BoundsCorrection.Size()));
				if (Sample.Commit.bBlockingHit || Sample.Commit.bStartedPenetrating)
				{
					InspectorLines.Add(FString::Printf(TEXT("Hit: %s | penetration %s | normal %s"), *Sample.Commit.HitComponent, Sample.Commit.bStartedPenetrating ? TEXT("YES") : TEXT("NO"), *Sample.Commit.HitNormal.ToCompactString()));
				}
				InspectorLines.Add(FString::Printf(TEXT("Stall counter %.2fs | recoveries %d%s"), Sample.StalledSeconds, Sample.RecoveryCount, Sample.bRecoveryAttempted ? TEXT(" (THIS STEP)") : TEXT("")));
				InspectorLines.Add(FString::Printf(TEXT("Sight: %s | detected %s | loss %.2f / grace %.2fs"), Target.bHasSight ? TEXT("YES") : TEXT("NO"), Target.bDetected ? TEXT("YES") : TEXT("NO"), Sample.SightLossAge, Sample.SightLossGrace));
				InspectorLines.Add(Sample.MemoryAge >= 0.f ? FString::Printf(TEXT("Memory: age %.2fs / remaining %.2fs | valid %s"), Sample.MemoryAge, Sample.MemoryRemaining, Navigation.bHasTransitGoal ? TEXT("YES") : TEXT("NO")) : TEXT("Memory: no remembered sighting"));
				InspectorLines.Add(Sample.bHasGoal ? FString::Printf(TEXT("Step goal: %.1f cm | arrival radius %.1f cm"), Sample.GoalDistance, Sample.ArrivalRadius) : TEXT("Step goal: NONE"));
				InspectorLines.Add(FString::Printf(TEXT("Evaluated profile: %s"), Sample.EffectiveProfile.IsEmpty() ? TEXT("NOT EVALUATED") : *Sample.EffectiveProfile));
			}
			else
			{
				InspectorLines.Add(!bCanSimulate ? TEXT("SIMULATION SUSPENDED - no current movement sample") : (Sample.bHasSample ? TEXT("NO CURRENT STEP - previous movement sample is stale") : TEXT("WAITING FOR SAMPLE")));
				InspectorLines.Add(FString::Printf(TEXT("Sight: %s | detected %s"), Target.bHasSight ? TEXT("YES") : TEXT("NO"), Target.bDetected ? TEXT("YES") : TEXT("NO")));
			}
			const FString NavigationPhase = StaticEnum<EBoidNavigationPhase>()->GetNameStringByValue(static_cast<int64>(Navigation.Phase));
			const FString RouteStatus = Navigation.Phase == EBoidNavigationPhase::None ? TEXT("N/A") : StaticEnum<EBoidRouteStatus>()->GetNameStringByValue(static_cast<int64>(Navigation.Status));
			const FString Mode = StaticEnum<EBoidNavigationMode>()->GetNameStringByValue(static_cast<int64>(Simulation->NavigationMode));
			InspectorLines.Add(FString::Printf(TEXT("Navigation: %s | %s | mode %s"), *NavigationPhase, *RouteStatus, *Mode));
			InspectorLines.Add(FString::Printf(TEXT("Combat hold (reported): %s | %.2fs"), *StaticEnum<EBoidCombatHoldReason>()->GetNameStringByValue(static_cast<int64>(Movement.HoldReason)), Movement.HoldSeconds));
			if (!Navigation.FailureReason.IsEmpty()) InspectorLines.Add(TEXT("Route: ") + Navigation.FailureReason);
			InspectorLines.Add(FString::Printf(TEXT("Intent owner %llu | revision %u"), static_cast<unsigned long long>(SelectedAgent->GetMovementIntentOwner()), Intent.Revision));
			InspectorLines.Add(TEXT("Actor: ") + SelectedAgent->GetActorNameOrLabel());
			InspectorLines.Add(TEXT("Object: ") + SelectedAgent->GetName());
			InspectorLines.Add(TEXT("Class: ") + SelectedAgent->GetClass()->GetName());
			InspectorLines.Add(TEXT("Assigned tree: ") + SelectedAgent->GetStateTreeAssetPath());
		}
		Inspector.Lines = InspectorLines;
		FVector Origin, Extent;
		if (IsValid(SelectedAgent->Visual))
		{
			Origin = SelectedAgent->Visual->Bounds.Origin;
			Extent = SelectedAgent->Visual->Bounds.BoxExtent;
		}
		else SelectedAgent->GetActorBounds(true, Origin, Extent);
		bool bProjected = !Origin.ContainsNaN() && !Extent.ContainsNaN();
		FVector2D Minimum(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2D Maximum(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
		for (int32 Corner = 0; Corner < 8 && bProjected; ++Corner)
		{
			const FVector Point = Origin + FVector((Corner & 1) ? Extent.X : -Extent.X, (Corner & 2) ? Extent.Y : -Extent.Y, (Corner & 4) ? Extent.Z : -Extent.Z);
			FVector2D Screen;
			bProjected = FVector::DotProduct(Point - ViewLocation, ViewForward) > 1.f && ProjectBadge(Controller, Overlay, Point, Screen);
			if (!bProjected) break;
			Minimum.X = FMath::Min(Minimum.X, Screen.X);
			Minimum.Y = FMath::Min(Minimum.Y, Screen.Y);
			Maximum.X = FMath::Max(Maximum.X, Screen.X);
			Maximum.Y = FMath::Max(Maximum.Y, Screen.Y);
		}
		const FVector2D ViewSize = Overlay.GetCachedGeometry().GetLocalSize();
		Inspector.bHasScreenBounds = bProjected && Maximum.X >= 0. && Maximum.Y >= 0. && Minimum.X < ViewSize.X && Minimum.Y < ViewSize.Y;
		if (Inspector.bHasScreenBounds)
		{
			Inspector.ScreenMinimum = FVector2D(FMath::Max(0., Minimum.X - 5.), FMath::Max(0., Minimum.Y - 5.));
			Inspector.ScreenMaximum = FVector2D(FMath::Min(ViewSize.X, Maximum.X + 5.), FMath::Min(ViewSize.Y, Maximum.Y + 5.));
		}
	}
	else
	{
		InspectorLines.Reset();
		InspectorAgentId = INDEX_NONE;
		NextInspectorRefresh = 0.;
	}
	FString Legend = FString::Printf(
		TEXT("Collision [%s] | Agent State [%s] | Spatial [%s]"), bCollision ? TEXT("ON") : TEXT("OFF"),
		bAgentState ? TEXT("ON") : TEXT("OFF"), bSpatial ? TEXT("ON") : TEXT("OFF"));
	if (bCollision && Settings.bOutlines) Legend += TEXT(
		"\nAgent outlines: white unaware | Amber aware | Red attack conditions");
	if (bCollision && Settings.bProjectileShapes) Legend += TEXT(
		"\nMagenta rings: enemy projectile collider | X-ray, true radius");
	if (bAgentState) Legend += TEXT("\nHP = health | ATK = next attack check; launch depends on attack conditions");
	if (bSpatial) Legend += FString::Printf(
		TEXT(
			"\nGold dashed goal | Cyan remaining route | Green velocity\nX-ray graphics | Velocity: %.2fs, capped %.0fcm"),
		ArrowSeconds, ArrowCap);
	if (bCollision && Settings.bOutlines && !bOutlineReady) Legend += TEXT("\nOutlines unavailable: ") + OutlineFailure;
	Overlay.SetFrame(MoveTemp(Badges), MoveTemp(FrameLabels), Legend, Settings, Inspector);
	// The widget swapped its previous allocation back; clear old values while reusing capacity.
	FrameBadges.Reset();
	FrameLabels.Reset();
}
