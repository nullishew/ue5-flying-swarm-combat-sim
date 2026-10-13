#pragma once

#include "CoreMinimal.h"
#include "BoidRangedPositioning.generated.h"

USTRUCT(BlueprintType)
struct FBoidRangedSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range", meta=(ClampMin="1"))
	float MinimumRangeCm = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range", meta=(ClampMin="1"))
	float PreferredRangeCm = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range", meta=(ClampMin="1"))
	float MaximumRangeCm = 800.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range|Adaptive")
	bool bAllowAdaptiveRange = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range|Adaptive", meta=(ClampMin="1"))
	float AdaptiveMinimumRangeCm = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range|Adaptive", meta=(ClampMin="1", ClampMax="16"))
	int32 AdaptiveShellCount = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Range|Adaptive", meta=(ClampMin="0", ClampMax="1"))
	float AdaptiveDistanceWeight = .35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Search", meta=(ClampMin="1", ClampMax="128"))
	int32 DirectionsPerSphere = 32;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Search", meta=(ClampMin="1", ClampMax="128"))
	int32 CandidatesPerFrame = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Search", meta=(ClampMin="0.01"))
	float SearchIntervalSeconds = .5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Search", meta=(ClampMin="1"))
	float TargetMovementThresholdCm = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Search", meta=(ClampMin="1"))
	float ClearanceProbeCm = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Search", meta=(ClampMin="1"))
	float SpacingDistanceCm = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Search", meta=(ClampMin="1"))
	float TravelScaleCm = 3000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Search", meta=(ClampMin="0", ClampMax="1"))
	float SwitchScoreImprovement = .10f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0"))
	float OpennessWeight = .55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0"))
	float OpennessMeanWeight = .6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0"))
	float OpennessMinimumWeight = .4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0"))
	float SpacingWeight = .20f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0"))
	float TravelWeight = .15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0"))
	float RangeWeight = .10f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scoring", meta=(ClampMin="0", ClampMax="1"))
	float SeedBiasWeight = .05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Orbit", meta=(ClampMin="0"))
	float AngularSpeedDegPerSec = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Orbit", meta=(ClampMin="1"))
	float OrbitLeadSeconds = .5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Readiness", meta=(ClampMin="0"))
	float ReadyDelaySeconds = .3f;
	FBoidRangedSettings Sanitized() const;
};

USTRUCT(BlueprintType)
struct FBoidPositioningFacts
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	bool bHasPosition = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	bool bOrbiting = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning") bool bCanOrbit = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	bool bUsingAdaptiveRange = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	float EffectiveMinimumRangeCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	float EffectivePreferredRangeCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	float EffectiveMaximumRangeCm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	FVector Destination = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	float Openness = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	float Score = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	FVector PreferredDirection = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	float SeedAffinity = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	int32 OrbitDirection = 1;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Positioning")
	float LastSearchSeconds = 0.f;
};

namespace BoidRanged
{
	uint32 ChoiceSeed(uint32 SimulationSeed, int32 AgentId);
	FVector PreferredDirection(uint32 Seed);
	int32 OrbitDirection(uint32 Seed);
	FVector SampleDirection(int32 Index, int32 Count, int32 Seed);
	FVector TargetRelativeOffset(int32 Index, int32 Count, uint32 Seed, float Radius);
	bool IsCandidateRankedAhead(bool bCandidateAdaptive, int32 CandidateRangeBucket, float CandidateScore, int32 CandidateIndex,
		bool bCurrentAdaptive, int32 CurrentRangeBucket, float CurrentScore, int32 CurrentIndex, float SwitchImprovement);
	void ClearanceDirections(TArray<FVector>& Out);
	float Openness(TConstArrayView<float> Clearances, float MeanWeight = .6f, float MinimumWeight = .4f);
	float Score(float Open, float SpacingCm, float TravelCm, float RangeCm, const FBoidRangedSettings& Settings);
	void AdaptiveRadii(const FBoidRangedSettings& Settings, TArray<float>& OutRadii);
	float ScoreAdaptive(float Open, float SpacingCm, float TravelCm, float RangeCm, const FBoidRangedSettings& Settings);
	float ScoreWithBias(float BaseScore, const FVector& CandidateDirection, uint32 Seed,
		float BiasWeight = .05f, float* OutSeedAffinity = nullptr);
	float BlendSeedBias(float BaseScore, float SeedAffinity, float BiasWeight = .05f);
	bool IsOrbitRangeStepValid(float PreviousRange, float NextRange, float MinimumRange, float MaximumRange, float EntryTolerance);
	bool ShouldSwitch(bool bCurrentValid, float CurrentScore, float NewScore, float Improvement);
	bool IsSearchStale(const FVector& Center, const FVector& Target, float Threshold);
	bool AdvanceReadiness(bool bEligible, float DeltaTime, float Delay, float& Elapsed);
	FVector OrbitAxis(const FVector& Offset);
}
