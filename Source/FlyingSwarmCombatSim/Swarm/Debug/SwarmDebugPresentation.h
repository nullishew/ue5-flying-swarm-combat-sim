#pragma once

#include "Swarm/Debug/SwarmDebugSettings.h"
#include "Swarm/Debug/SwarmMovementDebug.h"

namespace SwarmDebugPresentation
{
	inline bool InspectorCandidateWins(double AimDistanceSquared, double WorldDistanceSquared, int32 Id, double BestAimDistanceSquared, double BestWorldDistanceSquared, int32 BestId)
	{
		if (!FMath::IsFinite(AimDistanceSquared) || !FMath::IsFinite(WorldDistanceSquared) || AimDistanceSquared > BestAimDistanceSquared) return false;
		if (BestId == INDEX_NONE || AimDistanceSquared < BestAimDistanceSquared) return true;
		return WorldDistanceSquared < BestWorldDistanceSquared || (WorldDistanceSquared == BestWorldDistanceSquared && Id < BestId);
	}

	inline bool IsCurrentMovementSample(const FSwarmMovementDebugSnapshot& Sample, uint64 Frame, bool bSimulationReady)
	{
		return Sample.bHasSample && Sample.FrameNumber == Frame && bSimulationReady;
	}

	inline const TCHAR* MovementCommitLabel(ESwarmMovementCommitResult Result)
	{
		switch (Result)
		{
		case ESwarmMovementCommitResult::InvalidStep: return TEXT("INVALID MOVEMENT STEP");
		case ESwarmMovementCommitResult::InvalidBounds: return TEXT("INVALID BOUNDS");
		case ESwarmMovementCommitResult::FlightSpaceRejected: return TEXT("FLIGHT SPACE REJECTED");
		case ESwarmMovementCommitResult::SweepBlocked: return TEXT("SWEEP CONTACT (may slide)");
		case ESwarmMovementCommitResult::StartedPenetrating: return TEXT("STARTED PENETRATING");
		case ESwarmMovementCommitResult::Completed: return TEXT("MOVE CALL COMPLETED");
		case ESwarmMovementCommitResult::Interrupted: return TEXT("MOVEMENT INTERRUPTED");
		default: return TEXT("NOT ATTEMPTED");
		}
	}

	inline float BadgeDistanceAlpha(const FSwarmDebugSettings& Settings, double CameraDistanceCm)
	{
		if (Settings.BadgeFalloffEndDistanceCm == Settings.BadgeFullSizeDistanceCm) return CameraDistanceCm < Settings.BadgeFullSizeDistanceCm ? 0.f : 1.f;
		return static_cast<float>(FMath::Clamp((CameraDistanceCm - Settings.BadgeFullSizeDistanceCm) / (Settings.BadgeFalloffEndDistanceCm - Settings.BadgeFullSizeDistanceCm), 0.0, 1.0));
	}

	inline float CalcBadgeScale(const FSwarmDebugSettings& Settings, float Alpha)
	{
		const float NearScale = FMath::IsFinite(Settings.BadgeNearScale) ? FMath::Max(.01f, Settings.BadgeNearScale) : 1.f;
		const float DistantScale = FMath::IsFinite(Settings.BadgeDistantScale) ? FMath::Max(.01f, Settings.BadgeDistantScale) : .67f;
		return FMath::Lerp(NearScale, DistantScale, FMath::Clamp(Alpha, 0.f, 1.f));
	}
}
