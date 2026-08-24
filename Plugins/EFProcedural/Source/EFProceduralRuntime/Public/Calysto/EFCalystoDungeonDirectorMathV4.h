#pragma once

#include "Calysto/EFCalystoDungeonTypesV4.h"
#include "CoreMinimal.h"

class UEFCalystoDungeonDirectorPolicyV4;

/** Counter-RNG domain IDs are isolated so adding a draw in one domain cannot
 * perturb another. */
namespace EFCalystoDungeonDomainsV4 {
inline constexpr uint64 Style = 0x56345F5354594C45ULL;              // V4_STYLE
inline constexpr uint64 Theme = 0x56345F5448454D45ULL;              // V4_THEME
inline constexpr uint64 Shape = 0x56345F5348415045ULL;              // V4_SHAPE
inline constexpr uint64 AnchorDensity = 0x56345F414E44454EULL;      // V4_ANDEN
inline constexpr uint64 ThreatBudget = 0x56345F5448524541ULL;       // V4_THREA
inline constexpr uint64 CategoryBlend = 0x56345F434154424CULL;      // V4_CATBL
inline constexpr uint64 CategoryPresence = 0x56345F4341545052ULL;   // V4_CATPR
inline constexpr uint64 CategoryCount = 0x56345F434154434EULL;      // V4_CATCN
inline constexpr uint64 Tier = 0x56345F5449455253ULL;               // V4_TIERS
inline constexpr uint64 CatalogSource = 0x56345F4341545352ULL;      // V4_CATSR
inline constexpr uint64 CatalogEntry = 0x56345F434154454EULL;       // V4_CATEN
inline constexpr uint64 EnemyBundle = 0x56345F454E42554EULL;        // V4_ENBUN
inline constexpr uint64 EnemyBudgetBacktrack = 0x56345F454E4242ULL; // V4_ENBB
inline constexpr uint64 NPC = 0x56345F4E50435F5FULL;                // V4_NPC__
inline constexpr uint64 ChestContents = 0x56345F4348434F4EULL;      // V4_CHCON
inline constexpr uint64 Winter = 0x56345F57494E5445ULL;             // V4_WINTE
inline constexpr uint64 Anchors = 0x56345F414E434852ULL;            // V4_ANCHR
inline constexpr uint64 RunDNA = 0x56345F52554E444EULL;             // V4_RUNDN
inline constexpr uint64 SmoothFloorNoise = 0x56345F534D4F4F54ULL;   // V4_SMOOT
inline constexpr uint64 Jitter = 0x56345F4A49545452ULL;             // V4_JITTR
inline constexpr uint64 PerformanceOutcome = 0x56345F4F5554434FULL; // V4_OUTCO
inline constexpr uint64 NativeLighting = 0x56345F4C49474854ULL;     // V4_LIGHT
} // namespace EFCalystoDungeonDomainsV4

/** Pure deterministic V4 math. Every public helper rejects non-finite or
 * invalid input. */
class EFPROCEDURALRUNTIME_API FEFCalystoDungeonDirectorMathV4 final {
public:
  /** Stable UTF-8 SHA-256 used by all canonical V4 serializers. */
  static FString HashCanonicalText(const FString &Text);
  static bool ValidateDomainUniqueness(FString *OutError = nullptr);
  static FString
  GetCompanionSnapshotHash(const FEFCalystoCompanionSnapshotV4 &Snapshot);
  static uint64 DeriveEcologyCounterValue(int64 RunSeed, int64 FloorNumber,
                                          int64 GenerationSerial,
                                          const FString &PolicyHash,
                                          uint64 DomainId,
                                          uint64 StableTraitId = 0,
                                          uint32 DrawIndex = 0);
  static float ResolveEcologyTrait(float RunDNA, float SmoothFloorNoise,
                                   float Jitter, float AuthoredBias);
  static float ScoreFloorOutcome(const FEFCalystoFloorOutcomeV4 &Outcome);
  static float UpdatePerformanceEMA(float PreviousEMA,
                                    const FEFCalystoFloorOutcomeV4 &Outcome,
                                    float Alpha = 0.25f);
  static uint64 StableNameId(FName Name);
  static uint64 DeriveCounterValue(const FEFCalystoResolveContextV4 &Context,
                                   const FString &PolicyHash, uint64 DomainId,
                                   uint64 StableEntityId = 0,
                                   uint32 DrawIndex = 0);
  static double Uniform01(const FEFCalystoResolveContextV4 &Context,
                          const FString &PolicyHash, uint64 DomainId,
                          uint64 StableEntityId = 0, uint32 DrawIndex = 0);
  static bool Bernoulli(double Probability,
                        const FEFCalystoResolveContextV4 &Context,
                        const FString &PolicyHash, uint64 DomainId,
                        uint64 StableEntityId = 0, uint32 DrawIndex = 0);
  static float SamplePERT(const FEFCalystoPertRangeV4 &Distribution,
                          float Volatility,
                          const FEFCalystoResolveContextV4 &Context,
                          const FString &PolicyHash, uint64 DomainId,
                          uint64 StableEntityId = 0, uint32 FirstDrawIndex = 0);
  static int32 SampleCount(int32 MinimumWhenPresent, int32 MaximumPerFloor,
                           float EffectiveChance,
                           const FEFCalystoResolveContextV4 &Context,
                           const FString &PolicyHash, uint64 StableEntityId,
                           uint32 FirstDrawIndex = 0);
  static int32 SampleDiscretePERT(int32 Minimum, int32 Mode, int32 Maximum,
                                  float Concentration, float Volatility,
                                  const FEFCalystoResolveContextV4 &Context,
                                  const FString &PolicyHash, uint64 DomainId,
                                  uint64 StableEntityId = 0,
                                  uint32 FirstDrawIndex = 0);

  static double Progression(int64 FloorNumber, double Tau);
  static float BlendProbability(float StyleChance, float ThemeChance,
                                float Blend);
  static float LogOddsBlend(float A, float B, float Blend);
  static FEFCalystoTierMixV4 BlendTiers(const FEFCalystoTierMixV4 &StyleTiers,
                                        const FEFCalystoTierMixV4 &ThemeTiers,
                                        float Blend);
  static FEFCalystoTierMixV4
  ApplyMystery(const FEFCalystoTierMixV4 &Input, float Mystery,
               const TSet<EEFCalystoRarityTierV4> &EligibleTiers);
  static bool ValidateTierMix(const FEFCalystoTierMixV4 &Mix,
                              FString *OutError = nullptr);
  static FEFCalystoEnemyLevelBandV4
  ResolveEnemyLevelBand(EEFCalystoRarityTierV4 Tier, int32 DirectorLevel,
                        int32 LogicalWinterLevel);
  static float LevelCostMultiplier(int32 LogicalLevel);
  /** Applies Danger exactly once to a complete enemy bundle. Threat and level
   * inputs are normalized against the candidate set for the same slot. */
  static double WeightEnemyBundleForDanger(double BaseWeight, float Danger,
                                            float EffectiveThreatCost,
                                            float MaximumThreatCost,
                                            int32 LogicalLevel,
                                            int32 MaximumLogicalLevel);
};

/** Static, world-free compiler from one V4 policy/context to an immutable
 * pre-PCG intent. */
class EFPROCEDURALRUNTIME_API FEFCalystoDungeonDirectorResolverV4 final {
public:
  static bool Validate(const UEFCalystoDungeonDirectorPolicyV4 *Policy,
                       FString &OutError);
  static FString GetPolicyHash(const UEFCalystoDungeonDirectorPolicyV4 *Policy);
  static bool Resolve(const UEFCalystoDungeonDirectorPolicyV4 *Policy,
                      const FEFCalystoResolveContextV4 &Context,
                      FEFCalystoResolvedFloorIntentV4 &OutIntent,
                      FString &OutError);
#if WITH_DEV_AUTOMATION_TESTS
  /**
   * High-volume, world-free Automation path. The caller must validate the
   * immutable policy once and pass its canonical SHA-256. This deliberately
   * skips repeating policy validation/canonical serialization for every sample;
   * normal runtime and all fail-closed tests continue to use Resolve().
   */
  static bool ResolvePrevalidatedForTesting(
      const UEFCalystoDungeonDirectorPolicyV4 *Policy,
      const FString &PrevalidatedPolicyHash,
      const FEFCalystoResolveContextV4 &Context,
      FEFCalystoResolvedFloorIntentV4 &OutIntent, FString &OutError);
#endif

private:
  static bool ResolveInternal(
      const UEFCalystoDungeonDirectorPolicyV4 *Policy,
      const FString *PrevalidatedPolicyHash,
      const FEFCalystoResolveContextV4 &Context,
      FEFCalystoResolvedFloorIntentV4 &OutIntent, FString &OutError);
};
