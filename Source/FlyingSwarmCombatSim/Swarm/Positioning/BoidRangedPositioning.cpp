#include "Swarm/Positioning/BoidRangedPositioning.h"

FBoidRangedSettings FBoidRangedSettings::Sanitized() const
{
	FBoidRangedSettings S = *this;
	auto Safe = [](float V, float Default, float Minimum) { return FMath::Max(Minimum, FMath::IsFinite(V) ? V : Default); };
	S.MinimumRangeCm = Safe(MinimumRangeCm, 300, 1);
	S.MaximumRangeCm = Safe(MaximumRangeCm, 800, S.MinimumRangeCm + 1);
	S.PreferredRangeCm = FMath::Clamp(Safe(PreferredRangeCm, 500, 1), S.MinimumRangeCm, S.MaximumRangeCm);
	S.AdaptiveMinimumRangeCm = FMath::Clamp(Safe(AdaptiveMinimumRangeCm, 200.f, 1.f), 1.f, S.MinimumRangeCm);
	S.AdaptiveShellCount = FMath::Clamp(AdaptiveShellCount, 1, 16);
	S.AdaptiveDistanceWeight = FMath::Clamp(Safe(AdaptiveDistanceWeight, .35f, 0.f), 0.f, 1.f);
	S.DirectionsPerSphere = FMath::Clamp(DirectionsPerSphere, 1, 128);
	S.CandidatesPerFrame = FMath::Clamp(CandidatesPerFrame, 1, 128);
	S.SearchIntervalSeconds = Safe(SearchIntervalSeconds, .5f, .01f);
	S.TargetMovementThresholdCm = Safe(TargetMovementThresholdCm, 250, 1);
	S.ClearanceProbeCm = Safe(ClearanceProbeCm, 600, 1);
	S.SpacingDistanceCm = Safe(SpacingDistanceCm, 300, 1);
	S.TravelScaleCm = Safe(TravelScaleCm, 3000, 1);
	S.SwitchScoreImprovement = FMath::Clamp(Safe(SwitchScoreImprovement, .1f, 0), 0.f, 1.f);
	S.OpennessWeight = Safe(OpennessWeight, .55f, 0);
	S.OpennessMeanWeight = Safe(OpennessMeanWeight, .6f, 0);
	S.OpennessMinimumWeight = Safe(OpennessMinimumWeight, .4f, 0);
	S.SpacingWeight = Safe(SpacingWeight, .2f, 0);
	S.TravelWeight = Safe(TravelWeight, .15f, 0);
	S.RangeWeight = Safe(RangeWeight, .1f, 0);
	S.SeedBiasWeight = FMath::Clamp(Safe(SeedBiasWeight, .05f, 0), 0.f, 1.f);
	S.AngularSpeedDegPerSec = Safe(AngularSpeedDegPerSec, 15, 0);
	S.OrbitLeadSeconds = Safe(OrbitLeadSeconds, .5f, .01f);
	S.ReadyDelaySeconds = Safe(ReadyDelaySeconds, .3f, 0);
	return S;
}

namespace BoidRanged
{
	uint32 ChoiceSeed(uint32 SimulationSeed, int32 AgentId)
	{
		uint32 Value = SimulationSeed ^ (static_cast<uint32>(AgentId) + 0x9e3779b9u);
		Value ^= Value >> 16; Value *= 0x7feb352du;
		Value ^= Value >> 15; Value *= 0x846ca68bu;
		Value ^= Value >> 16;
		return Value;
	}
	FVector PreferredDirection(uint32 Seed)
	{
		FRandomStream Random(static_cast<int32>(Seed ^ 0x85ebca6bu));
		const float Z = Random.FRandRange(-1.f, 1.f);
		const float Angle = Random.FRandRange(0.f, 2.f * PI);
		const float R = FMath::Sqrt(FMath::Max(0.f, 1.f - Z * Z));
		return FVector(R * FMath::Cos(Angle), R * FMath::Sin(Angle), Z);
	}
	int32 OrbitDirection(uint32 Seed)
	{
		return (ChoiceSeed(Seed, 0x2c1b3c6d) & 1u) ? 1 : -1;
	}
	FVector SampleDirection(int32 Index, int32 Count, int32 Seed)
	{
		const double Z = 1. - 2. * (Index + .5) / FMath::Max(1, Count);
		const double Radius = FMath::Sqrt(FMath::Max(0., 1. - Z * Z));
		const double Angle = Index * PI * (3. - FMath::Sqrt(5.));
		// Rotate the full Fibonacci lattice once per agent seed. Including Index here
		// would rotate every point independently and destroy the even distribution.
		FRandomStream Random(static_cast<int32>(ChoiceSeed(static_cast<uint32>(Seed), 0x6d2b79f5)));
		const FQuat Rotation = FRotator(Random.FRandRange(-180, 180), Random.FRandRange(-180, 180), Random.FRandRange(-180, 180)).Quaternion();
		return Rotation.RotateVector(FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), Z));
	}
	FVector TargetRelativeOffset(int32 Index, int32 Count, uint32 Seed, float Radius)
	{
		return SampleDirection(Index, Count, static_cast<int32>(Seed)) * FMath::Max(0.f, Radius);
	}
	bool IsCandidateRankedAhead(bool bCandidateAdaptive, int32 CandidateRangeBucket, float CandidateScore, int32 CandidateIndex,
		bool bCurrentAdaptive, int32 CurrentRangeBucket, float CurrentScore, int32 CurrentIndex, float SwitchImprovement)
	{
		if (!FMath::IsFinite(CandidateScore) || CandidateScore < 0.f) return false;
		if (!FMath::IsFinite(CurrentScore) || CurrentScore < 0.f) return true;
		if (bCandidateAdaptive != bCurrentAdaptive) return !bCandidateAdaptive;
		if (CandidateRangeBucket != CurrentRangeBucket) return CandidateRangeBucket < CurrentRangeBucket;
		const float Improvement = FMath::Max(0.f, FMath::IsFinite(SwitchImprovement) ? SwitchImprovement : 0.f);
		if (CandidateScore >= CurrentScore + Improvement) return true;
		if (CurrentScore >= CandidateScore + Improvement) return false;
		return CandidateIndex < CurrentIndex;
	}
	void ClearanceDirections(TArray<FVector>& Out)
	{
		Out = { FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector, FVector::UpVector, -FVector::UpVector };
		for (int X : {-1, 1}) for (int Y : {-1, 1}) for (int Z : {-1, 1}) Out.Add(FVector(X,Y,Z).GetSafeNormal());
	}
	float Openness(TConstArrayView<float> Clearances, float MeanWeight, float MinimumWeight)
	{
		if (Clearances.IsEmpty()) return 0;
		float Sum = 0, Minimum = 1;
		for (float C : Clearances) { C = FMath::Clamp(C, 0.f, 1.f); Sum += C; Minimum = FMath::Min(Minimum, C); }
		return (MeanWeight * Sum / Clearances.Num() + MinimumWeight * Minimum) / FMath::Max(UE_SMALL_NUMBER, MeanWeight + MinimumWeight);
	}
	float Score(float Open, float SpacingCm, float TravelCm, float RangeCm, const FBoidRangedSettings& S)
	{
		const float Side = RangeCm < S.PreferredRangeCm ? S.PreferredRangeCm - S.MinimumRangeCm : S.MaximumRangeCm - S.PreferredRangeCm;
		const float Range = 1 - FMath::Clamp(FMath::Abs(RangeCm - S.PreferredRangeCm) / FMath::Max(1.f, Side), 0.f, 1.f);
		const float Weight = S.OpennessWeight + S.SpacingWeight + S.TravelWeight + S.RangeWeight;
		return (S.OpennessWeight * Open + S.SpacingWeight * FMath::Clamp(SpacingCm / S.SpacingDistanceCm, 0.f, 1.f)
			+ S.TravelWeight * (1 - FMath::Clamp(TravelCm / S.TravelScaleCm, 0.f, 1.f)) + S.RangeWeight * Range) / FMath::Max(UE_SMALL_NUMBER, Weight);
	}
	void AdaptiveRadii(const FBoidRangedSettings& Settings, TArray<float>& OutRadii)
	{
		OutRadii.Reset();
		const FBoidRangedSettings S = Settings.Sanitized();
		const float Floor = FMath::Clamp(S.AdaptiveMinimumRangeCm, 1.f, S.MinimumRangeCm);
		if (!S.bAllowAdaptiveRange || Floor >= S.MinimumRangeCm) return;

		const float Step = (S.MinimumRangeCm - Floor) / S.AdaptiveShellCount;
		OutRadii.Reserve(S.AdaptiveShellCount);
		for (int32 Shell = 1; Shell <= S.AdaptiveShellCount; ++Shell)
		{
			OutRadii.Add(S.MinimumRangeCm - Step * Shell);
		}
	}
	float ScoreAdaptive(float Open, float SpacingCm, float TravelCm, float RangeCm, const FBoidRangedSettings& Settings)
	{
		if (!FMath::IsFinite(Open) || !FMath::IsFinite(SpacingCm) || !FMath::IsFinite(TravelCm) || !FMath::IsFinite(RangeCm))
		{
			return -1.f;
		}
		const FBoidRangedSettings S = Settings.Sanitized();
		const float QualityWeight = S.OpennessWeight + S.SpacingWeight + S.TravelWeight;
		const float Quality = (S.OpennessWeight * FMath::Clamp(Open, 0.f, 1.f)
			+ S.SpacingWeight * FMath::Clamp(SpacingCm / S.SpacingDistanceCm, 0.f, 1.f)
			+ S.TravelWeight * (1.f - FMath::Clamp(TravelCm / S.TravelScaleCm, 0.f, 1.f)))
			/ FMath::Max(UE_SMALL_NUMBER, QualityWeight);
		const float Floor = FMath::Clamp(S.AdaptiveMinimumRangeCm, 1.f, S.MinimumRangeCm);
		const float Distance = FMath::Clamp((RangeCm - Floor) / FMath::Max(1.f, S.MinimumRangeCm - Floor), 0.f, 1.f);
		return (1.f - S.AdaptiveDistanceWeight) * Quality + S.AdaptiveDistanceWeight * Distance;
	}
	float ScoreWithBias(float BaseScore, const FVector& CandidateDirection, uint32 Seed, float BiasWeight, float* OutSeedAffinity)
	{
		float Affinity = 0.5f;
		if (!CandidateDirection.ContainsNaN() && FMath::IsFinite(CandidateDirection.SizeSquared())
			&& !CandidateDirection.IsNearlyZero())
		{
			const float CandidateAffinity = 0.5f + 0.5f * FVector::DotProduct(CandidateDirection.GetSafeNormal(), PreferredDirection(Seed));
			if (FMath::IsFinite(CandidateAffinity)) Affinity = FMath::Clamp(CandidateAffinity, 0.f, 1.f);
		}
		if (OutSeedAffinity) *OutSeedAffinity = Affinity;
		return BlendSeedBias(BaseScore, Affinity, BiasWeight);
	}
	float BlendSeedBias(float BaseScore, float SeedAffinity, float BiasWeight)
	{
		if(!FMath::IsFinite(BaseScore)||BaseScore<0.f)return -1.f;
		const float Bias = FMath::Clamp(FMath::IsFinite(BiasWeight)?BiasWeight:.05f, 0.f, 1.f);
		const float SafeAffinity = FMath::IsFinite(SeedAffinity) ? FMath::Clamp(SeedAffinity, 0.f, 1.f) : .5f;
		return (1.f - Bias) * BaseScore + Bias * SafeAffinity;
	}
	bool IsOrbitRangeStepValid(float PreviousRange, float NextRange, float MinimumRange, float MaximumRange, float EntryTolerance)
	{
		if(!FMath::IsFinite(PreviousRange)||!FMath::IsFinite(NextRange)||!FMath::IsFinite(MinimumRange)
			||!FMath::IsFinite(MaximumRange)||!FMath::IsFinite(EntryTolerance)||MinimumRange>MaximumRange)return false;
		auto Error=[&](float Range){return FMath::Max(0.f,FMath::Max(MinimumRange-Range,Range-MaximumRange));};
		const float NextError=Error(NextRange);
		if(NextError<=1.f)return true;
		const float PreviousError=Error(PreviousRange);
		// Arrival can start orbit just outside the band. Permit only a short step
		// correcting toward it; readiness still requires actual in-band distance.
		return PreviousError>0.f&&PreviousError<=FMath::Max(0.f,EntryTolerance)+1.f
			&&NextError<PreviousError-UE_SMALL_NUMBER;
	}
	bool ShouldSwitch(bool bCurrentValid, float CurrentScore, float NewScore, float Improvement)
	{
		return NewScore >= 0 && (!bCurrentValid || (NewScore > CurrentScore && NewScore >= CurrentScore + Improvement));
	}
	bool IsSearchStale(const FVector& Center, const FVector& Target, float Threshold) { return FVector::DistSquared(Center, Target) > FMath::Square(Threshold); }
	bool AdvanceReadiness(bool bEligible, float DeltaTime, float Delay, float& Elapsed)
	{
		Elapsed = bEligible ? Elapsed + FMath::Max(0.f, DeltaTime) : 0.f;
		return bEligible && Elapsed >= Delay;
	}
	FVector OrbitAxis(const FVector& Offset)
	{
		const FVector Unit = Offset.GetSafeNormal();
		return FMath::Abs(FVector::DotProduct(Unit, FVector::UpVector)) > .98
			? FVector::VectorPlaneProject(FVector::RightVector, Unit).GetSafeNormal() : FVector::UpVector;
	}
}
