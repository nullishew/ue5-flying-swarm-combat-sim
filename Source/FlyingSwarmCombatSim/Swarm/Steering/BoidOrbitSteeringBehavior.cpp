#include "Swarm/Steering/BoidOrbitSteeringBehavior.h"

FVector UBoidOrbitSteeringBehavior::CalculateDirection(const FBoidSteeringInput& Input,
	FBoidSteeringRuntimeState& Runtime) const
{
	return CalcDesiredVelocity(Input).GetSafeNormal();
}

bool UBoidOrbitSteeringBehavior::CalculateDesiredSpeed(const FBoidSteeringInput& Input,
	float& OutSpeedCmPerSec) const
{
	const FVector Velocity = CalcDesiredVelocity(Input);
	if (Input.NavigationPhase != EBoidNavigationPhase::Orbit || !Input.OrbitContext.bActive) return false;
	OutSpeedCmPerSec = Velocity.Size();
	return FMath::IsFinite(OutSpeedCmPerSec);
}

FVector UBoidOrbitSteeringBehavior::CalcDesiredVelocity(const FBoidSteeringInput& Input) const
{
	const float Seconds = FMath::IsFinite(Input.DeltaTime) && Input.DeltaTime > UE_SMALL_NUMBER
		? Input.DeltaTime : 0.0001f;
	return (PredictPosition(Input, Seconds) - Input.Position) / Seconds;
}

FVector UBoidOrbitSteeringBehavior::PredictPosition(const FBoidSteeringInput& Input, float Seconds) const
{
	const FBoidOrbitContext& Orbit = Input.OrbitContext;
	if (Input.NavigationPhase != EBoidNavigationPhase::Orbit || !Orbit.bActive
		|| Input.Position.ContainsNaN() || Orbit.Center.ContainsNaN() || Orbit.Axis.ContainsNaN()
		|| !FMath::IsFinite(Orbit.RadiusCm) || !FMath::IsFinite(Orbit.HeightCm)
		|| !FMath::IsFinite(Orbit.AngularSpeedDegPerSec) || !FMath::IsFinite(Seconds) || Seconds <= 0.f)
	{
		return Input.Position;
	}
	const FVector Axis = Orbit.Axis.GetSafeNormal();
	if (Axis.IsNearlyZero()) return Input.Position;
	const FVector Offset = Input.Position - Orbit.Center;
	const FVector Radial = FVector::VectorPlaneProject(Offset, Axis);
	const float CurrentRadius = Radial.Size();
	const FVector RadialDirection = CurrentRadius > UE_SMALL_NUMBER
		? Radial / CurrentRadius
		: FVector::VectorPlaneProject(FVector::ForwardVector, Axis).GetSafeNormal();
	if (RadialDirection.IsNearlyZero()) return Input.Position;

	const float AngularSpeed = FMath::Clamp(FMath::Abs(Orbit.AngularSpeedDegPerSec), 0.f, 360.f);
	const float SignedSeconds = Orbit.Direction < 0 ? -Seconds : Seconds;
	const float Angle = FMath::DegreesToRadians(AngularSpeed * SignedSeconds);
	const FVector RotatedRadial = FQuat(Axis, Angle).RotateVector(RadialDirection).GetSafeNormal();
	if (RotatedRadial.IsNearlyZero()) return Input.Position;

	// Exponential corrections move toward the authored radius and height without
	// overshooting when a profile uses a large gain or a long prediction interval.
	const float RadialGain = FMath::IsFinite(RadialCorrectionGainPerSec)
		? FMath::Max(0.f, RadialCorrectionGainPerSec) : 0.f;
	const float ElevationGain = FMath::IsFinite(ElevationCorrectionGainPerSec)
		? FMath::Max(0.f, ElevationCorrectionGainPerSec) : 0.f;
	const float RadialFraction = 1.f - FMath::Exp(-RadialGain * Seconds);
	const float ElevationFraction = 1.f - FMath::Exp(-ElevationGain * Seconds);
	const float PredictedRadius = CurrentRadius + (FMath::Max(0.f, Orbit.RadiusCm) - CurrentRadius) * RadialFraction;
	const float CurrentHeight = FVector::DotProduct(Offset, Axis);
	const float PredictedHeight = CurrentHeight + (Orbit.HeightCm - CurrentHeight) * ElevationFraction;
	const FVector IdealPosition = Orbit.Center + RotatedRadial * PredictedRadius + Axis * PredictedHeight;
	FVector Displacement = IdealPosition - Input.Position;
	const float SpeedLimit = FMath::IsFinite(Input.FlightSpeedLimitCmPerSec)
		? FMath::Max(0.f, Input.FlightSpeedLimitCmPerSec) : 0.f;
	Displacement = Displacement.GetClampedToMaxSize(SpeedLimit * Seconds);
	return Input.Position + Displacement;
}

float UBoidOrbitSteeringBehavior::MinimumChordRange(const FVector& Center, const FVector& Start, const FVector& End)
{
	if (Center.ContainsNaN() || Start.ContainsNaN() || End.ContainsNaN()) return 0.f;
	const FVector Segment = End - Start;
	const double LengthSquared = Segment.SizeSquared();
	const double Alpha = LengthSquared > UE_SMALL_NUMBER
		? FMath::Clamp(static_cast<double>(FVector::DotProduct(Center - Start, Segment)) / LengthSquared, 0.0, 1.0)
		: 0.0;
	return static_cast<float>(FVector::Distance(Center, Start + Segment * Alpha));
}

bool UBoidOrbitSteeringBehavior::IsRangeStepValid(const FVector& Center, const FVector& Start, const FVector& End,
	float MinimumRange, float MaximumRange, float DesiredRange, float DriftAllowance)
{
	if (Center.ContainsNaN() || Start.ContainsNaN() || End.ContainsNaN()
		|| !FMath::IsFinite(MinimumRange) || !FMath::IsFinite(MaximumRange) || MinimumRange > MaximumRange
		|| !FMath::IsFinite(DesiredRange) || !FMath::IsFinite(DriftAllowance)
		|| DesiredRange < MinimumRange - 1.f || DesiredRange > MaximumRange + 1.f) return false;
	const float ChordRange = MinimumChordRange(Center, Start, End);
	const float StartRange = FVector::Distance(Center, Start);
	const float EndRange = FVector::Distance(Center, End);
	const float MaximumEndpointRange = FMath::Max(StartRange, EndRange);
	if (ChordRange >= MinimumRange - 1.f && MaximumEndpointRange <= MaximumRange + 1.f) return true;
	const float StartError = FMath::Abs(StartRange - DesiredRange);
	const float EndError = FMath::Abs(EndRange - DesiredRange);
	return ChordRange >= MinimumRange - FMath::Max(0.f, DriftAllowance)
		&& MaximumEndpointRange <= MaximumRange + FMath::Max(0.f, DriftAllowance)
		&& EndError + 0.001f < StartError;

}

FBoidSteeringResult UBoidOrbitSteeringBehavior::EvaluateWithResult(const FBoidSteeringInput& Input,
	FBoidSteeringRuntimeState& Runtime) const
{
	FBoidSteeringResult Result;
	if (Input.NavigationPhase != EBoidNavigationPhase::Orbit || !Input.OrbitContext.bActive) return Result;
	const FVector Desired = CalcDesiredVelocity(Input);
	Result.Direction = Desired.GetSafeNormal() * FMath::Max(0.f, Weight) * Runtime.WeightScale;
	const float EffectiveSpeedScale = FMath::Max(0.f, Runtime.WeightScale);
	Result.bHasDesiredSpeed = FMath::Max(0.f, Weight) > 0.f && EffectiveSpeedScale > UE_SMALL_NUMBER
		&& FMath::IsFinite(Desired.Size());
	Result.DesiredSpeedCmPerSec = Desired.Size() * EffectiveSpeedScale;
	return Result;
}
