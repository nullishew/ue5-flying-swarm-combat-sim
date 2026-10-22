#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "UI/SwarmDebugOverlayWidget.h"
#include "Swarm/Simulation/SwarmSimulationSubsystem.h"

class APlayerController;
class UMeshComponent;
struct FSwarmDebugSettings;

/** Non-ticking, controller-owned view. Only current frame data and reversible mesh settings are stored. */
class FSwarmDebugPresenter
{
public:
	void Update(APlayerController& Controller, USwarmDebugOverlayWidget& Overlay, const FSwarmDebugSettings& Settings, bool bCollision, bool bAgentState, bool bSpatial, bool bOutlineReady, const FString& OutlineFailure);
	void Shutdown();
	/** Restore agent rendering without stopping the independent badge/spatial views. */
	void DisableCollision();

private:
	struct FOriginalMeshState
	{
		bool bCustomDepth = false;
		int32 Stencil = 0;
		ERendererStencilMask WriteMask = ERendererStencilMask::ERSM_Default;
	};
	void RestoreOutlines();
	TWeakObjectPtr<USwarmSimulationSubsystem> SuppressedSimulation;
	TMap<TWeakObjectPtr<UMeshComponent>, FOriginalMeshState> OriginalMeshes;
	TArray<FSwarmDebugAgent> Agents;
	TArray<FSwarmDebugBadge> FrameBadges;
	TArray<FVector> RemainingRoute;
};
