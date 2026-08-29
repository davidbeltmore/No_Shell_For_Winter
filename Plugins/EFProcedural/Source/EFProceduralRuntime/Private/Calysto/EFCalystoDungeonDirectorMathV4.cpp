#include "Calysto/EFCalystoDungeonDirectorMathV4.h"

#include "Calysto/EFCalystoDungeonDirectorPolicyV4.h"

#include <cmath>

namespace EFCalystoMathV4Private {
static constexpr float ProbabilityCap = 0.90f;
static constexpr double TinyProbability = 1.0e-9;
static constexpr double TierMassValidationTolerance = 1.0e-6;

static uint64 Mix64(uint64 Value) {
  Value += 0x9E3779B97F4A7C15ULL;
  Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ULL;
  Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBULL;
  return Value ^ (Value >> 31);
}

static uint64 HashString64(const FString &Text) {
  const FTCHARToUTF8 Utf8(*Text);
  uint64 Hash = 1469598103934665603ULL;
  for (int32 Index = 0; Index < Utf8.Length(); ++Index) {
    Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
    Hash *= 1099511628211ULL;
  }
  return Hash;
}

static FString HashText(const FString &Text) {
  const FTCHARToUTF8 Utf8(*Text);
  const int32 ByteLength = Utf8.Length();
  const int32 PaddedLength = ((ByteLength + 9 + 63) / 64) * 64;
  TArray<uint8> Message;
  Message.SetNumZeroed(PaddedLength);
  if (ByteLength > 0) {
    FMemory::Memcpy(Message.GetData(), Utf8.Get(), ByteLength);
  }
  Message[ByteLength] = 0x80;
  const uint64 BitLength = static_cast<uint64>(ByteLength) * 8ULL;
  for (int32 Index = 0; Index < 8; ++Index) {
    Message[PaddedLength - 1 - Index] =
        static_cast<uint8>(BitLength >> (Index * 8));
  }

  static constexpr uint32 K[64] = {
      0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU,
      0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U, 0xD807AA98U, 0x12835B01U,
      0x243185BEU, 0x550C7DC3U, 0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U,
      0xC19BF174U, 0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
      0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU, 0x983E5152U,
      0xA831C66DU, 0xB00327C8U, 0xBF597FC7U, 0xC6E00BF3U, 0xD5A79147U,
      0x06CA6351U, 0x14292967U, 0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU,
      0x53380D13U, 0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
      0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U, 0xD192E819U,
      0xD6990624U, 0xF40E3585U, 0x106AA070U, 0x19A4C116U, 0x1E376C08U,
      0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU,
      0x682E6FF3U, 0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
      0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U};
  auto Ror = [](const uint32 V, const uint32 S) {
    return (V >> S) | (V << (32U - S));
  };
  uint32 State[8] = {0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
                     0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U};
  for (int32 Block = 0; Block < PaddedLength; Block += 64) {
    uint32 W[64] = {};
    for (int32 I = 0; I < 16; ++I) {
      const int32 O = Block + I * 4;
      W[I] = (static_cast<uint32>(Message[O]) << 24U) |
             (static_cast<uint32>(Message[O + 1]) << 16U) |
             (static_cast<uint32>(Message[O + 2]) << 8U) |
             static_cast<uint32>(Message[O + 3]);
    }
    for (int32 I = 16; I < 64; ++I) {
      const uint32 S0 =
          Ror(W[I - 15], 7U) ^ Ror(W[I - 15], 18U) ^ (W[I - 15] >> 3U);
      const uint32 S1 =
          Ror(W[I - 2], 17U) ^ Ror(W[I - 2], 19U) ^ (W[I - 2] >> 10U);
      W[I] = W[I - 16] + S0 + W[I - 7] + S1;
    }
    uint32 A = State[0], B = State[1], C = State[2], D = State[3], E = State[4],
           F = State[5], G = State[6], H = State[7];
    for (int32 I = 0; I < 64; ++I) {
      const uint32 S1 = Ror(E, 6U) ^ Ror(E, 11U) ^ Ror(E, 25U);
      const uint32 T1 = H + S1 + ((E & F) ^ ((~E) & G)) + K[I] + W[I];
      const uint32 S0 = Ror(A, 2U) ^ Ror(A, 13U) ^ Ror(A, 22U);
      const uint32 T2 = S0 + ((A & B) ^ (A & C) ^ (B & C));
      H = G;
      G = F;
      F = E;
      E = D + T1;
      D = C;
      C = B;
      B = A;
      A = T1 + T2;
    }
    State[0] += A;
    State[1] += B;
    State[2] += C;
    State[3] += D;
    State[4] += E;
    State[5] += F;
    State[6] += G;
    State[7] += H;
  }
  FString Result;
  Result.Reserve(64);
  for (const uint32 Word : State) {
    Result += FString::Printf(TEXT("%08X"), Word);
  }
  return Result;
}

static FString FloatBits(const float Value) {
  uint32 Bits = 0;
  FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
  return FString::Printf(TEXT("%08X"), Bits);
}

static double SampleNormal(const FEFCalystoResolveContextV4 &Context,
                           const FString &PolicyHash, const uint64 Domain,
                           const uint64 StableId, uint32 &Draw) {
  const double U1 =
      FMath::Max(FEFCalystoDungeonDirectorMathV4::Uniform01(
                     Context, PolicyHash, Domain, StableId, Draw++),
                 1.0e-12);
  const double U2 = FEFCalystoDungeonDirectorMathV4::Uniform01(
      Context, PolicyHash, Domain, StableId, Draw++);
  return FMath::Sqrt(-2.0 * FMath::Loge(U1)) *
         FMath::Cos(2.0 * UE_DOUBLE_PI * U2);
}

static double SampleGamma(const double Shape,
                          const FEFCalystoResolveContextV4 &Context,
                          const FString &PolicyHash, const uint64 Domain,
                          const uint64 StableId, uint32 &Draw) {
  if (Shape <= 0.0) {
    return 0.0;
  }
  if (Shape < 1.0) {
    const double U =
        FMath::Max(FEFCalystoDungeonDirectorMathV4::Uniform01(
                       Context, PolicyHash, Domain, StableId, Draw++),
                   1.0e-12);
    return SampleGamma(Shape + 1.0, Context, PolicyHash, Domain, StableId,
                       Draw) *
           FMath::Pow(U, 1.0 / Shape);
  }
  const double D = Shape - 1.0 / 3.0;
  const double C = 1.0 / FMath::Sqrt(9.0 * D);
  for (int32 Attempt = 0; Attempt < 64; ++Attempt) {
    const double X = SampleNormal(Context, PolicyHash, Domain, StableId, Draw);
    const double Base = 1.0 + C * X;
    if (Base <= 0.0) {
      continue;
    }
    const double V = Base * Base * Base;
    const double U = FEFCalystoDungeonDirectorMathV4::Uniform01(
        Context, PolicyHash, Domain, StableId, Draw++);
    if (U < 1.0 - 0.0331 * X * X * X * X ||
        FMath::Loge(FMath::Max(U, 1.0e-12)) <
            0.5 * X * X + D * (1.0 - V + FMath::Loge(V))) {
      return D * V;
    }
  }
  return Shape;
}

static float EvaluateChance(const FEFCalystoChanceCurveV4 &Curve,
                            const int64 Floor) {
  return FMath::Lerp(
      Curve.ChanceAtFloor1, Curve.ChanceAtFloor100,
      static_cast<float>(
          FEFCalystoDungeonDirectorMathV4::Progression(Floor, Curve.Tau)));
}

static FEFCalystoTierMixV4 EvaluateTiers(const FEFCalystoTierCurveV4 &Curve,
                                         const int64 Floor, const double Tau) {
  const float G = static_cast<float>(
      FEFCalystoDungeonDirectorMathV4::Progression(Floor, Tau));
  FEFCalystoTierMixV4 Result;
  Result.Common =
      FMath::Lerp(Curve.AtFloor1.Common, Curve.AtFloor100.Common, G);
  Result.Uncommon =
      FMath::Lerp(Curve.AtFloor1.Uncommon, Curve.AtFloor100.Uncommon, G);
  Result.Rare = FMath::Lerp(Curve.AtFloor1.Rare, Curve.AtFloor100.Rare, G);
  Result.Epic = FMath::Lerp(Curve.AtFloor1.Epic, Curve.AtFloor100.Epic, G);
  Result.RefreshNothing();
  return Result;
}

static float LogOddsAdjusted(const float Base, const float Trait,
                             const float Intent, const float EMA) {
  if (!FMath::IsFinite(Base) || Base <= 0.0f) {
    return 0.0f;
  }
  const double P = FMath::Clamp(static_cast<double>(Base), TinyProbability,
                                1.0 - TinyProbability);
  const double L = FMath::Loge(P / (1.0 - P)) + FMath::Loge(2.0) * Trait +
                   FMath::Loge(1.5) * Intent + FMath::Loge(1.15) * EMA;
  return FMath::Min(ProbabilityCap,
                    static_cast<float>(1.0 / (1.0 + FMath::Exp(-L))));
}

static float SmoothRamp(const FEFCalystoCatalogEntryV4 &Entry,
                        const int64 Floor) {
  if (Floor < Entry.FirstEligibleFloor) {
    return 0.0f;
  }
  if (Entry.RampFloors <= 0) {
    return 1.0f;
  }
  const float T = FMath::Clamp(
      static_cast<float>(Floor - Entry.FirstEligibleFloor) / Entry.RampFloors,
      0.0f, 1.0f);
  return T * T * (3.0f - 2.0f * T);
}

static float EntryWeight(const FEFCalystoCatalogEntryV4 &Entry,
                         const int64 Floor) {
  if (Entry.Rule != EEFCalystoCatalogRuleV4::Allow ||
      Floor < Entry.FirstEligibleFloor) {
    return 0.0f;
  }
  return Entry.DeepShare *
         FMath::Lerp(Entry.InitialFraction, 1.0f, SmoothRamp(Entry, Floor));
}

static bool SupportsTier(const FEFCalystoCatalogEntryV4 &Entry,
                         const EEFCalystoRarityTierV4 Tier) {
  if (!Entry.bTierAgnostic) {
    return Entry.Tier == Tier;
  }
  return Entry.AllowedTiers.IsEmpty() || Entry.AllowedTiers.Contains(Tier);
}

static int32 SelectWeighted(const TArray<float> &Weights, const double Draw) {
  double Total = 0.0;
  for (const float W : Weights) {
    if (FMath::IsFinite(W) && W > 0.0f) {
      Total += W;
    }
  }
  if (Total <= 0.0) {
    return INDEX_NONE;
  }
  double Cursor = Draw * Total;
  for (int32 I = 0; I < Weights.Num(); ++I) {
    if (Weights[I] <= 0.0f) {
      continue;
    }
    if (Cursor < Weights[I]) {
      return I;
    }
    Cursor -= Weights[I];
  }
  for (int32 I = Weights.Num() - 1; I >= 0; --I) {
    if (Weights[I] > 0.0f) {
      return I;
    }
  }
  return INDEX_NONE;
}

static float TraitForCategory(const FEFCalystoContextTraitsV4 &T,
                              const EEFCalystoContentCategoryV4 C) {
  switch (C) {
  case EEFCalystoContentCategoryV4::Enemy:
    return T.Danger;
  case EEFCalystoContentCategoryV4::NPC:
    return T.Safe;
  case EEFCalystoContentCategoryV4::Food:
    return T.Abundance;
  case EEFCalystoContentCategoryV4::Chest:
    return T.Mystery;
  case EEFCalystoContentCategoryV4::Clothing:
    return T.ClothingInfluence;
  default:
    return 0.0f;
  }
}

static float IntentForCategory(const FEFCalystoDirectorIntentV4 &I,
                               const EEFCalystoContentCategoryV4 C) {
  switch (C) {
  case EEFCalystoContentCategoryV4::Enemy:
    return I.Danger;
  case EEFCalystoContentCategoryV4::NPC:
    return I.Safe;
  case EEFCalystoContentCategoryV4::Food:
    return I.Abundance;
  case EEFCalystoContentCategoryV4::Chest:
    return I.Mystery;
  case EEFCalystoContentCategoryV4::Clothing:
    return I.ClothingInfluence;
  default:
    return 0.0f;
  }
}

static float PerformanceDirection(const EEFCalystoContentCategoryV4 C) {
  if (C == EEFCalystoContentCategoryV4::Enemy) {
    return 1.0f;
  }
  if (C == EEFCalystoContentCategoryV4::NPC ||
      C == EEFCalystoContentCategoryV4::Food ||
      C == EEFCalystoContentCategoryV4::Chest ||
      C == EEFCalystoContentCategoryV4::Clothing) {
    return -1.0f;
  }
  return 0.0f;
}

static float ApplyPity(const float Chance, const int32 EmptyFloors,
                       const int32 Threshold) {
  if (Threshold <= 0 || EmptyFloors < Threshold || Chance <= 0.0f) {
    return Chance;
  }
  const int32 EligiblePitySteps = EmptyFloors - Threshold + 1;
  const float Strength =
      1.0f - FMath::Exp(-static_cast<float>(EligiblePitySteps) / Threshold);
  return FMath::Min(ProbabilityCap,
                    FMath::Lerp(Chance, ProbabilityCap, Strength));
}

static FName MakeStableInstanceId(const EEFCalystoContentCategoryV4 Category,
                                  const int32 Slot) {
  return FName(*FString::Printf(TEXT("V4.%02d.%04d"),
                                static_cast<int32>(Category), Slot));
}

static FString BuildOutcomeHash(const FEFCalystoResolveContextV4 &Context) {
  if (!Context.bHasFrozenOutcome) {
    return HashText(TEXT("V4_OUTCOME_NEUTRAL"));
  }
  return HashText(
      FString::Printf(TEXT("V4_OUTCOME|%s|%s|%s|%s|%s"),
                      *FloatBits(Context.FrozenOutcome.Combat),
                      *FloatBits(Context.FrozenOutcome.Survival),
                      *FloatBits(Context.FrozenOutcome.Resources),
                      *FloatBits(Context.FrozenOutcome.Pace),
                      *FloatBits(Context.FrozenOutcome.DeathsAndFailures)));
}

static FString
BuildCompanionSnapshotHash(const FEFCalystoCompanionSnapshotV4 &Snapshot) {
  TArray<FEFCalystoCompanionRecordV4> Records = Snapshot.Records;
  Records.Sort([](const FEFCalystoCompanionRecordV4 &A,
                  const FEFCalystoCompanionRecordV4 &B) {
    return A.StableCompanionId.ToString(EGuidFormats::Digits) <
           B.StableCompanionId.ToString(EGuidFormats::Digits);
  });
  FString C = FString::Printf(TEXT("V4_COMPANIONS|OWNS_RECALL:%d|"),
                              Snapshot.bPlayerOwnsWintersRecall ? 1 : 0);
  for (const FEFCalystoCompanionRecordV4 &R : Records) {
    C += FString::Printf(TEXT("R:%s:%s:%s:%s:%s:%s:%d:%d:%d|"),
                         *R.StableCompanionId.ToString(EGuidFormats::Digits),
                         *R.SourceSpawnId.ToString(),
                         *R.SourceCatalogId.ToString(),
                         *R.SourceVariantId.ToString(),
                         *R.ActorClass.ToSoftObjectPath().ToString(),
                         *R.Archetype.ToString(), static_cast<int32>(R.Gender),
                         static_cast<int32>(R.Grade),
                         static_cast<int32>(R.State));
  }
  return HashText(C);
}

static FString BuildIntentHash(const FEFCalystoResolvedFloorIntentV4 &I) {
  FString C = FString::Printf(
      TEXT("V4_INTENT|%lld|%lld|%lld|%s|%s|%s|%d|%d|%s|%s|%s|%d|%d|%d|%d|%s|%s|"
           "%s|%s|%s|"),
      I.RunSeed, I.FloorNumber, I.GenerationSerial, *I.PolicyHash,
      *I.EcologyHash, *I.CompanionSnapshotHash, static_cast<int32>(I.Style),
      static_cast<int32>(I.Theme), *FloatBits(I.StyleSelectionDraw),
      *FloatBits(I.ThemeSelectionDraw), *FloatBits(I.ShapeBlend),
      I.DungeonSize.X, I.DungeonSize.Y, I.DungeonSize.Z, I.PCGSeed,
      *FloatBits(I.CandidateAnchorDensity), *FloatBits(I.SidePathChance),
      *FloatBits(I.ThreatBudget), *FloatBits(I.ResourceBudget), *I.OutcomeHash);
  C += FString::Printf(TEXT("NATIVE_LIGHT:%s:%s:%d|"),
                       *FloatBits(I.LightingBlend),
                       *FloatBits(I.NativeWallLightHeight),
                       I.NativeWallLightTileDistance);
  C += FString::Printf(TEXT("DEV:%d:%s|"), I.DevelopmentForcedDungeonEdge,
                       *I.DevelopmentPopulationScenario.ToString());
  C += FString::Printf(
      TEXT("META:%d:%d:%d:%s|"), I.GeneratorVersion,
      I.bHasFrozenOutcome ? 1 : 0, I.DirectorLevel,
      *I.CalystoRoomType.ToSoftObjectPath().ToString());
  C += FString::Printf(TEXT("WINTER_LEVEL:%d|"), I.LogicalWinterLevel);
  C += FString::Printf(
      TEXT("TRAITS:%s:%s:%s:%s:%s:%s:%s:%s|"),
      *FloatBits(I.ResolvedTraits.Scale), *FloatBits(I.ResolvedTraits.Branching),
      *FloatBits(I.ResolvedTraits.Mystery), *FloatBits(I.ResolvedTraits.Danger),
      *FloatBits(I.ResolvedTraits.Safe), *FloatBits(I.ResolvedTraits.Abundance),
      *FloatBits(I.ResolvedTraits.ClothingInfluence),
      *FloatBits(I.ResolvedTraits.Volatility));
  C += FString::Printf(TEXT("COSTS:%s:%s|"),
                       *FloatBits(I.PlannedThreatCost),
                       *FloatBits(I.PlannedResourceCost));
  for (const FEFCalystoResolvedCategoryV4 &R : I.Categories) {
    C += FString::Printf(
        TEXT("C:%d:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%d:%d:%d:%d:%d:%d|"),
        static_cast<int32>(R.Category), *FloatBits(R.StyleThemeBlend),
        *FloatBits(R.ResolvedInfluence), *FloatBits(R.OpportunityChance),
        *FloatBits(R.SelectableTierMass), *FloatBits(R.WinterChance),
        *FloatBits(R.EffectiveChance), *FloatBits(R.ResolvedTiers.Common),
        *FloatBits(R.ResolvedTiers.Uncommon), *FloatBits(R.ResolvedTiers.Rare),
        *FloatBits(R.ResolvedTiers.Epic), R.MinimumWhenPresent,
        R.MaximumPerFloor, R.bPresent ? 1 : 0, R.AttemptCount, R.TargetCount,
        R.DirectiveCount);
    for (const FName CatalogId : R.EligibleCatalogIds) {
      C += FString::Printf(TEXT("CE:%d:%s|"), static_cast<int32>(R.Category),
                           *CatalogId.ToString());
    }
  }
  for (const FEFCalystoSpawnInstanceDirectiveV4 &D : I.SpawnDirectives) {
    C += FString::Printf(
        TEXT("D:%s:%s:%s:%s:%s:%d:%d:%d:%d:%d:%s:%d:%s:%d:%d:%d|"),
        *D.StableInstanceId.ToString(),
        *D.StableCompanionId.ToString(EGuidFormats::Digits),
        *D.CatalogId.ToString(), *D.VariantId.ToString(),
        *D.ActorClass.ToSoftObjectPath().ToString(),
        static_cast<int32>(D.Category), static_cast<int32>(D.Tier),
        D.LogicalLevel, D.PhysicalACFLevel, D.CategorySlotIndex,
        *FloatBits(D.EffectiveThreatCost), static_cast<int32>(D.Gender),
        *D.Archetype.ToString(), static_cast<int32>(D.Lifecycle),
        D.ChestContentAttemptCount, D.CooldownFloors);
  }
  for (const FEFCalystoChestContentDirectiveV4 &D : I.ChestContentDirectives) {
    C += FString::Printf(
        TEXT("CC:%s:%s:%s:%s:%d:%d|"), *D.ContainerInstanceId.ToString(),
        *D.StableAttemptId.ToString(), *D.ContentCatalogId.ToString(),
        *D.ContentClass.ToSoftObjectPath().ToString(),
        static_cast<int32>(D.Tier), D.CooldownFloors);
  }
  for (const FEFCalystoResolvedCompanionLevelV4 &L :
       I.ResolvedCompanionLevels) {
    C += FString::Printf(TEXT("CL:%s:%d:%d:%d|"),
                         *L.StableCompanionId.ToString(EGuidFormats::Digits),
                         static_cast<int32>(L.Grade), L.LogicalLevel,
                         L.PhysicalACFLevel);
  }
  return HashText(C);
}

static int32 BuildCalystoQualifiedPCGSeed(
    const FEFCalystoResolvedFloorIntentV4 &Intent,
    const FString &ProvisionalIntentHash) {
  const FString Hash = HashText(FString::Printf(
      TEXT("EFCalystoPCGSeedV4|%lld|%lld|%lld|%d|%s|%s|%s"),
      Intent.RunSeed, Intent.FloorNumber, Intent.GenerationSerial,
      Intent.GeneratorVersion, *Intent.PolicyHash, *Intent.EcologyHash,
      *ProvisionalIntentHash));
  if (Hash.Len() < 16) {
    return 0;
  }
  const uint64 Raw = FCString::Strtoui64(*Hash.Left(16), nullptr, 16);
  return static_cast<int32>((Raw % static_cast<uint64>(MAX_int32 - 1)) + 1ULL);
}

static bool IsFiniteUnit(const float V) {
  return FMath::IsFinite(V) && V >= 0.0f && V <= 1.0f;
}
static bool IsFiniteBias(const float V) {
  return FMath::IsFinite(V) && V >= -1.0f && V <= 1.0f;
}

static bool IsKnownTier(const EEFCalystoRarityTierV4 Tier) {
  switch (Tier) {
  case EEFCalystoRarityTierV4::Common:
  case EEFCalystoRarityTierV4::Uncommon:
  case EEFCalystoRarityTierV4::Rare:
  case EEFCalystoRarityTierV4::Epic:
  case EEFCalystoRarityTierV4::Winter:
    return true;
  default:
    return false;
  }
}

static bool IsTierUnlocked(const EEFCalystoRarityTierV4 Tier,
                           const int64 FloorNumber) {
  switch (Tier) {
  case EEFCalystoRarityTierV4::Common:
    return FloorNumber >= 1;
  case EEFCalystoRarityTierV4::Uncommon:
    return FloorNumber >= 2;
  case EEFCalystoRarityTierV4::Rare:
    return FloorNumber >= 5;
  case EEFCalystoRarityTierV4::Epic:
    return FloorNumber >= 10;
  case EEFCalystoRarityTierV4::Winter:
    return FloorNumber >= 101;
  default:
    return false;
  }
}

static bool IsKnownGender(const EEFCalystoGenderV4 Gender) {
  return Gender == EEFCalystoGenderV4::Any ||
         Gender == EEFCalystoGenderV4::Female ||
         Gender == EEFCalystoGenderV4::Male;
}

static bool
IsKnownCompanionState(const EEFCalystoCompanionRosterStateV4 State) {
  return State == EEFCalystoCompanionRosterStateV4::ActiveParty ||
         State == EEFCalystoCompanionRosterStateV4::RecruitedInactive ||
         State == EEFCalystoCompanionRosterStateV4::Dead ||
         State == EEFCalystoCompanionRosterStateV4::PendingDead;
}

static bool IsKnownDevelopmentScenario(const FName Scenario) {
  return Scenario.IsNone() || Scenario == FName(TEXT("Zero")) ||
         Scenario == FName(TEXT("ResourceMin")) ||
         Scenario == FName(TEXT("EnemyCap25")) ||
         Scenario == FName(TEXT("ResourceMax")) ||
         Scenario == FName(TEXT("NPCGeneralistFemale")) ||
         Scenario == FName(TEXT("NPCGeneralistMale")) ||
         Scenario == FName(TEXT("NPCMeleeFemale")) ||
         Scenario == FName(TEXT("NPCMeleeMale")) ||
         Scenario == FName(TEXT("NPCRangedFemale")) ||
         Scenario == FName(TEXT("NPCRangedMale")) ||
         Scenario == FName(TEXT("NPCTotal4")) ||
         Scenario == FName(TEXT("SpecialEvents6")) ||
         Scenario == FName(TEXT("CompanionRecallLifecycle"));
}

static bool IsDevelopmentZeroScenario(const FName Scenario) {
  return Scenario == FName(TEXT("Zero")) ||
         Scenario == FName(TEXT("ResourceMin"));
}

static bool IsDevelopmentNPCVariantScenario(const FName Scenario) {
  return Scenario == FName(TEXT("NPCGeneralistFemale")) ||
         Scenario == FName(TEXT("NPCGeneralistMale")) ||
         Scenario == FName(TEXT("NPCMeleeFemale")) ||
         Scenario == FName(TEXT("NPCMeleeMale")) ||
         Scenario == FName(TEXT("NPCRangedFemale")) ||
         Scenario == FName(TEXT("NPCRangedMale"));
}

static bool IsDevelopmentCompanionRecallScenario(const FName Scenario) {
  return Scenario == FName(TEXT("CompanionRecallLifecycle"));
}

struct FEnemyBundleCandidateV4 {
  const FEFCalystoCatalogEntryV4 *Entry = nullptr;
  EEFCalystoRarityTierV4 Tier = EEFCalystoRarityTierV4::Common;
  int32 LogicalLevel = 1;
  float EffectiveThreatCost = 0.0f;
  double Weight = 0.0;
  double Priority = 0.0;
};

struct FEnemyBundleSlotV4 {
  int32 CategorySlot = 0;
  uint64 StableId = 0;
  bool bMandatory = false;
  bool bWantsSpawn = false;
  TArray<FEnemyBundleCandidateV4> Candidates;
};

enum class EEnemyBundleSolveResultV4 : uint8 {
  Found,
  NoSolution,
  NodeLimit
};

/** Exact bounded search for a fixed set of non-Nothing enemy slots. Candidate
 * order is already a deterministic weighted permutation, so backtracking only
 * changes a draw when the preferred branch violates budget or variant caps. */
static EEnemyBundleSolveResultV4 SolveEnemyBundlesExact(
    const TArray<const FEnemyBundleSlotV4 *> &Slots, const float ThreatBudget,
    const TMap<FName, int32> &InitialVariantCounts,
    TArray<int32> &OutCandidateIndices, float &OutThreatCost) {
  static constexpr int32 MaximumSearchNodes = 131072;
  OutCandidateIndices.Init(INDEX_NONE, Slots.Num());
  OutThreatCost = 0.0f;
  if (Slots.IsEmpty()) {
    return EEnemyBundleSolveResultV4::Found;
  }

  TMap<FName, int32> VariantCounts = InitialVariantCounts;
  TArray<int32> Working;
  Working.Init(INDEX_NONE, Slots.Num());
  TArray<float> MinimumSuffixCost;
  MinimumSuffixCost.SetNumZeroed(Slots.Num() + 1);
  for (int32 SlotIndex = Slots.Num() - 1; SlotIndex >= 0; --SlotIndex) {
    float Cheapest = TNumericLimits<float>::Max();
    for (const FEnemyBundleCandidateV4 &Candidate :
         Slots[SlotIndex]->Candidates) {
      if (Candidate.Entry &&
          InitialVariantCounts.FindRef(Candidate.Entry->StableId) <
              Candidate.Entry->MaxPerVariant) {
        Cheapest = FMath::Min(Cheapest, Candidate.EffectiveThreatCost);
      }
    }
    if (Cheapest == TNumericLimits<float>::Max()) {
      return EEnemyBundleSolveResultV4::NoSolution;
    }
    MinimumSuffixCost[SlotIndex] =
        Cheapest + MinimumSuffixCost[SlotIndex + 1];
  }
  int32 VisitedNodes = 0;
  bool bNodeLimitReached = false;
  TFunction<bool(int32, float)> Search = [&](const int32 Depth,
                                             const float CostSoFar) {
    if (++VisitedNodes > MaximumSearchNodes) {
      bNodeLimitReached = true;
      return false;
    }
    if (Depth == Slots.Num()) {
      OutCandidateIndices = Working;
      OutThreatCost = CostSoFar;
      return true;
    }

    // An admissible precomputed lower bound rejects impossible budget branches
    // without changing candidate order or consuming additional RNG draws.
    if (CostSoFar + MinimumSuffixCost[Depth] > ThreatBudget + .0001f) {
      return false;
    }

    const FEnemyBundleSlotV4 &Slot = *Slots[Depth];
    for (int32 CandidateIndex = 0;
         CandidateIndex < Slot.Candidates.Num(); ++CandidateIndex) {
      const FEnemyBundleCandidateV4 &Candidate =
          Slot.Candidates[CandidateIndex];
      if (!Candidate.Entry ||
          VariantCounts.FindRef(Candidate.Entry->StableId) >=
              Candidate.Entry->MaxPerVariant ||
          CostSoFar + Candidate.EffectiveThreatCost >
              ThreatBudget + .0001f) {
        continue;
      }
      ++VariantCounts.FindOrAdd(Candidate.Entry->StableId);
      Working[Depth] = CandidateIndex;
      if (Search(Depth + 1,
                 CostSoFar + Candidate.EffectiveThreatCost)) {
        return true;
      }
      --VariantCounts.FindChecked(Candidate.Entry->StableId);
      Working[Depth] = INDEX_NONE;
      if (bNodeLimitReached) {
        return false;
      }
    }
    return false;
  };

  if (Search(0, 0.0f)) {
    return EEnemyBundleSolveResultV4::Found;
  }
  return bNodeLimitReached ? EEnemyBundleSolveResultV4::NodeLimit
                           : EEnemyBundleSolveResultV4::NoSolution;
}
} // namespace EFCalystoMathV4Private

FString FEFCalystoDungeonDirectorMathV4::HashCanonicalText(
    const FString &Text) {
  return EFCalystoMathV4Private::HashText(Text);
}

bool FEFCalystoDungeonDirectorMathV4::ValidateDomainUniqueness(
    FString *OutError) {
  const uint64 Values[] = {EFCalystoDungeonDomainsV4::Style,
                           EFCalystoDungeonDomainsV4::Theme,
                           EFCalystoDungeonDomainsV4::Shape,
                           EFCalystoDungeonDomainsV4::AnchorDensity,
                           EFCalystoDungeonDomainsV4::ThreatBudget,
                           EFCalystoDungeonDomainsV4::CategoryBlend,
                           EFCalystoDungeonDomainsV4::CategoryPresence,
                           EFCalystoDungeonDomainsV4::CategoryCount,
                           EFCalystoDungeonDomainsV4::Tier,
                           EFCalystoDungeonDomainsV4::CatalogSource,
                           EFCalystoDungeonDomainsV4::CatalogEntry,
                           EFCalystoDungeonDomainsV4::EnemyBundle,
                           EFCalystoDungeonDomainsV4::EnemyBudgetBacktrack,
                           EFCalystoDungeonDomainsV4::NPC,
                           EFCalystoDungeonDomainsV4::ChestContents,
                           EFCalystoDungeonDomainsV4::Winter,
                           EFCalystoDungeonDomainsV4::Anchors,
                           EFCalystoDungeonDomainsV4::RunDNA,
                           EFCalystoDungeonDomainsV4::SmoothFloorNoise,
                           EFCalystoDungeonDomainsV4::Jitter,
                           EFCalystoDungeonDomainsV4::PerformanceOutcome,
                           EFCalystoDungeonDomainsV4::NativeLighting};
  TSet<uint64> Unique;
  for (const uint64 Value : Values) {
    if (Value == 0 || Unique.Contains(Value)) {
      if (OutError) {
        *OutError = TEXT("V4 RNG domain IDs contain zero or a collision.");
      }
      return false;
    }
    Unique.Add(Value);
  }
  return true;
}

FString FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(
    const FEFCalystoCompanionSnapshotV4 &Snapshot) {
  return EFCalystoMathV4Private::BuildCompanionSnapshotHash(Snapshot);
}

uint64 FEFCalystoDungeonDirectorMathV4::DeriveEcologyCounterValue(
    const int64 RunSeed, const int64 FloorNumber, const int64 GenerationSerial,
    const FString &PolicyHash, const uint64 DomainId,
    const uint64 StableTraitId, const uint32 DrawIndex) {
  using namespace EFCalystoMathV4Private;
  const bool bUsesFloor = DomainId != EFCalystoDungeonDomainsV4::RunDNA;
  const bool bUsesSerial =
      DomainId == EFCalystoDungeonDomainsV4::Jitter ||
      DomainId == EFCalystoDungeonDomainsV4::PerformanceOutcome;
  if (RunSeed <= 0 || PolicyHash.IsEmpty() || (bUsesFloor && FloorNumber < 1) ||
      (bUsesSerial && GenerationSerial < 1) ||
      (DomainId != EFCalystoDungeonDomainsV4::RunDNA &&
       DomainId != EFCalystoDungeonDomainsV4::SmoothFloorNoise &&
       DomainId != EFCalystoDungeonDomainsV4::Jitter &&
       DomainId != EFCalystoDungeonDomainsV4::PerformanceOutcome)) {
    return 0;
  }
  uint64 V = Mix64(static_cast<uint64>(RunSeed));
  V = Mix64(V ^ 4ULL);
  V = Mix64(V ^ HashString64(PolicyHash));
  V = Mix64(V ^ DomainId);
  if (bUsesFloor) {
    V = Mix64(V ^ static_cast<uint64>(FloorNumber));
  }
  if (bUsesSerial) {
    V = Mix64(V ^ static_cast<uint64>(GenerationSerial));
  }
  V = Mix64(V ^ StableTraitId);
  return Mix64(V ^ static_cast<uint64>(DrawIndex));
}

float FEFCalystoDungeonDirectorMathV4::ResolveEcologyTrait(
    const float RunDNA, const float SmoothFloorNoise, const float Jitter,
    const float AuthoredBias) {
  if (!FMath::IsFinite(RunDNA) || !FMath::IsFinite(SmoothFloorNoise) ||
      !FMath::IsFinite(Jitter) || !FMath::IsFinite(AuthoredBias)) {
    return 0.0f;
  }
  return FMath::Clamp(.45f * RunDNA + .35f * SmoothFloorNoise + .20f * Jitter +
                          AuthoredBias,
                      -1.0f, 1.0f);
}

float FEFCalystoDungeonDirectorMathV4::ScoreFloorOutcome(
    const FEFCalystoFloorOutcomeV4 &O) {
  if (!FMath::IsFinite(O.Combat) || !FMath::IsFinite(O.Survival) ||
      !FMath::IsFinite(O.Resources) || !FMath::IsFinite(O.Pace) ||
      !FMath::IsFinite(O.DeathsAndFailures)) {
    return .5f;
  }
  const float Score = .40f * FMath::Clamp(O.Combat, 0.0f, 1.0f) +
                      .25f * FMath::Clamp(O.Survival, 0.0f, 1.0f) +
                      .20f * FMath::Clamp(O.Resources, 0.0f, 1.0f) +
                      .15f * FMath::Clamp(O.Pace, 0.0f, 1.0f) -
                      .10f * FMath::Clamp(O.DeathsAndFailures, 0.0f, 1.0f);
  return FMath::Clamp(Score, 0.0f, 1.0f);
}

float FEFCalystoDungeonDirectorMathV4::UpdatePerformanceEMA(
    const float PreviousEMA, const FEFCalystoFloorOutcomeV4 &Outcome,
    const float Alpha) {
  if (!FMath::IsFinite(PreviousEMA) || !FMath::IsFinite(Alpha)) {
    return .5f;
  }
  return FMath::Lerp(FMath::Clamp(PreviousEMA, 0.0f, 1.0f),
                     ScoreFloorOutcome(Outcome),
                     FMath::Clamp(Alpha, 0.0f, 1.0f));
}

uint64 FEFCalystoDungeonDirectorMathV4::StableNameId(const FName Name) {
  return EFCalystoMathV4Private::HashString64(Name.ToString());
}

uint64 FEFCalystoDungeonDirectorMathV4::DeriveCounterValue(
    const FEFCalystoResolveContextV4 &Context, const FString &PolicyHash,
    const uint64 DomainId, const uint64 StableEntityId,
    const uint32 DrawIndex) {
  using namespace EFCalystoMathV4Private;
  if (Context.RunSeed <= 0 || Context.FloorNumber < 1 ||
      Context.GenerationSerial < 1 || PolicyHash.IsEmpty() || DomainId == 0) {
    return 0;
  }
  uint64 V = Mix64(static_cast<uint64>(Context.RunSeed));
  V = Mix64(V ^ static_cast<uint64>(Context.FloorNumber));
  V = Mix64(V ^ static_cast<uint64>(Context.GenerationSerial));
  V = Mix64(V ^ 4ULL);
  V = Mix64(V ^ HashString64(PolicyHash));
  V = Mix64(V ^ HashString64(Context.EcologyHash));
  V = Mix64(V ^ HashString64(Context.CompanionSnapshotHash));
  V = Mix64(V ^ HashString64(BuildOutcomeHash(Context)));
  V = Mix64(V ^ DomainId);
  V = Mix64(V ^ StableEntityId);
  return Mix64(V ^ static_cast<uint64>(DrawIndex));
}

double FEFCalystoDungeonDirectorMathV4::Uniform01(
    const FEFCalystoResolveContextV4 &Context, const FString &PolicyHash,
    const uint64 DomainId, const uint64 StableEntityId,
    const uint32 DrawIndex) {
  return static_cast<double>(DeriveCounterValue(Context, PolicyHash, DomainId,
                                                StableEntityId, DrawIndex) >>
                             11) *
         (1.0 / 9007199254740992.0);
}

bool FEFCalystoDungeonDirectorMathV4::Bernoulli(
    const double Probability, const FEFCalystoResolveContextV4 &Context,
    const FString &PolicyHash, const uint64 DomainId,
    const uint64 StableEntityId, const uint32 DrawIndex) {
  return FMath::IsFinite(Probability) &&
         Uniform01(Context, PolicyHash, DomainId, StableEntityId, DrawIndex) <
             FMath::Clamp(Probability, 0.0, 1.0);
}

float FEFCalystoDungeonDirectorMathV4::SamplePERT(
    const FEFCalystoPertRangeV4 &D, const float Volatility,
    const FEFCalystoResolveContextV4 &Context, const FString &PolicyHash,
    const uint64 DomainId, const uint64 StableEntityId,
    const uint32 FirstDrawIndex) {
  using namespace EFCalystoMathV4Private;
  if (!FMath::IsFinite(D.Min) || !FMath::IsFinite(D.Mode) ||
      !FMath::IsFinite(D.Max) || !FMath::IsFinite(D.Concentration) ||
      D.Min > D.Mode || D.Mode > D.Max || D.Concentration < 2.0f ||
      D.Concentration > 8.0f) {
    return D.Min;
  }
  if (FMath::IsNearlyEqual(D.Min, D.Max)) {
    return D.Min;
  }
  const float V = FMath::Clamp(Volatility, 0.0f, 1.0f);
  const double Concentration =
      V <= .5f ? FMath::Lerp(8.0f, D.Concentration, V * 2.0f)
               : FMath::Lerp(D.Concentration, 2.0f, (V - .5f) * 2.0f);
  const double Range = D.Max - D.Min;
  const double Alpha = 1.0 + Concentration * (D.Mode - D.Min) / Range;
  const double Beta = 1.0 + Concentration * (D.Max - D.Mode) / Range;
  uint32 Draw = FirstDrawIndex;
  const double X =
      SampleGamma(Alpha, Context, PolicyHash, DomainId, StableEntityId, Draw);
  const double Y =
      SampleGamma(Beta, Context, PolicyHash, DomainId, StableEntityId, Draw);
  const double Unit =
      X + Y > UE_DOUBLE_SMALL_NUMBER ? X / (X + Y) : Alpha / (Alpha + Beta);
  return FMath::Clamp(static_cast<float>(D.Min + Unit * Range), D.Min, D.Max);
}

int32 FEFCalystoDungeonDirectorMathV4::SampleDiscretePERT(
    const int32 Minimum, const int32 Mode, const int32 Maximum,
    const float Concentration, const float Volatility,
    const FEFCalystoResolveContextV4 &Context, const FString &PolicyHash,
    const uint64 DomainId, const uint64 StableEntityId,
    const uint32 FirstDrawIndex) {
  using namespace EFCalystoMathV4Private;
  if (Minimum > Mode || Mode > Maximum || !FMath::IsFinite(Concentration) ||
      !FMath::IsFinite(Volatility) || Concentration < 2.0f ||
      Concentration > 8.0f) {
    return Minimum;
  }
  if (Minimum == Maximum) {
    return Minimum;
  }
  const double V = FMath::Clamp(static_cast<double>(Volatility), 0.0, 1.0);
  const double C =
      V <= .5 ? FMath::Lerp(8.0, static_cast<double>(Concentration), V * 2.0)
              : FMath::Lerp(static_cast<double>(Concentration), 2.0,
                            (V - .5) * 2.0);
  const double Range = static_cast<double>(static_cast<int64>(Maximum) -
                                           static_cast<int64>(Minimum));
  const double Alpha =
      1.0 + C *
                static_cast<double>(static_cast<int64>(Mode) -
                                    static_cast<int64>(Minimum)) /
                Range;
  const double Beta =
      1.0 + C *
                static_cast<double>(static_cast<int64>(Maximum) -
                                    static_cast<int64>(Mode)) /
                Range;
  uint32 Draw = FirstDrawIndex;
  const double X =
      SampleGamma(Alpha, Context, PolicyHash, DomainId, StableEntityId, Draw);
  const double Y =
      SampleGamma(Beta, Context, PolicyHash, DomainId, StableEntityId, Draw);
  const double Unit =
      X + Y > UE_DOUBLE_SMALL_NUMBER ? X / (X + Y) : Alpha / (Alpha + Beta);
  const double Sample = static_cast<double>(Minimum) + Unit * Range;
  if (!FMath::IsFinite(Sample) || Sample < static_cast<double>(MIN_int32) ||
      Sample > static_cast<double>(MAX_int32)) {
    return Minimum;
  }
  return FMath::Clamp(FMath::RoundToInt(Sample), Minimum, Maximum);
}

int32 FEFCalystoDungeonDirectorMathV4::SampleCount(
    const int32 MinimumWhenPresent, const int32 MaximumPerFloor,
    const float EffectiveChance, const FEFCalystoResolveContextV4 &Context,
    const FString &PolicyHash, const uint64 StableEntityId,
    const uint32 FirstDrawIndex) {
  if (MaximumPerFloor <= 0 || !FMath::IsFinite(EffectiveChance) ||
      EffectiveChance <= 0.0f) {
    return 0;
  }
  const int32 Minimum = FMath::Clamp(MinimumWhenPresent, 0, MaximumPerFloor);
  const double P = FMath::Pow(
      FMath::Clamp(static_cast<double>(EffectiveChance), 0.0, 0.90), 4.0);
  int32 Count = Minimum;
  for (int32 I = 0; I < MaximumPerFloor - Minimum; ++I) {
    if (Bernoulli(P, Context, PolicyHash,
                  EFCalystoDungeonDomainsV4::CategoryCount, StableEntityId,
                  FirstDrawIndex + static_cast<uint32>(I))) {
      ++Count;
    }
  }
  return Count;
}

double FEFCalystoDungeonDirectorMathV4::Progression(const int64 FloorNumber,
                                                    const double Tau) {
  if (FloorNumber < 1 || !FMath::IsFinite(Tau) || Tau <= 0.0) {
    return 0.0;
  }

  const double Raw =
      1.0 - FMath::Exp(-static_cast<double>(FloorNumber - 1) / Tau);
  // At great depths the exponential is smaller than double precision and the
  // subtraction rounds to exactly 1.0. Preserve the mathematical contract of
  // an asymptote that is approached but never reached.
  return FMath::Clamp(Raw, 0.0, std::nextafter(1.0, 0.0));
}

float FEFCalystoDungeonDirectorMathV4::BlendProbability(const float StyleChance,
                                                        const float ThemeChance,
                                                        const float Blend) {
  if (!FMath::IsFinite(StyleChance) || !FMath::IsFinite(ThemeChance) ||
      !FMath::IsFinite(Blend)) {
    return 0.0f;
  }
  return FMath::Lerp(StyleChance, ThemeChance, FMath::Clamp(Blend, 0.0f, 1.0f));
}

float FEFCalystoDungeonDirectorMathV4::LogOddsBlend(const float A,
                                                    const float B,
                                                    const float Blend) {
  if (A <= 0.0f && B <= 0.0f) {
    return 0.0f;
  }
  const double PA = FMath::Clamp(static_cast<double>(A),
                                 EFCalystoMathV4Private::TinyProbability,
                                 1.0 - EFCalystoMathV4Private::TinyProbability);
  const double PB = FMath::Clamp(static_cast<double>(B),
                                 EFCalystoMathV4Private::TinyProbability,
                                 1.0 - EFCalystoMathV4Private::TinyProbability);
  const double L =
      FMath::Lerp(FMath::Loge(PA / (1.0 - PA)), FMath::Loge(PB / (1.0 - PB)),
                  FMath::Clamp(static_cast<double>(Blend), 0.0, 1.0));
  return static_cast<float>(1.0 / (1.0 + FMath::Exp(-L)));
}

FEFCalystoTierMixV4
FEFCalystoDungeonDirectorMathV4::BlendTiers(const FEFCalystoTierMixV4 &S,
                                            const FEFCalystoTierMixV4 &T,
                                            const float Blend) {
  FEFCalystoTierMixV4 R;
  R.Common = BlendProbability(S.Common, T.Common, Blend);
  R.Uncommon = BlendProbability(S.Uncommon, T.Uncommon, Blend);
  R.Rare = BlendProbability(S.Rare, T.Rare, Blend);
  R.Epic = BlendProbability(S.Epic, T.Epic, Blend);
  const float Sum = R.GetSelectableMass();
  if (Sum > .90f) {
    const float Scale = .90f / Sum;
    R.Common *= Scale;
    R.Uncommon *= Scale;
    R.Rare *= Scale;
    R.Epic *= Scale;
  }
  R.RefreshNothing();
  return R;
}

FEFCalystoTierMixV4 FEFCalystoDungeonDirectorMathV4::ApplyMystery(
    const FEFCalystoTierMixV4 &Input, const float Mystery,
    const TSet<EEFCalystoRarityTierV4> &EligibleTiers) {
  const EEFCalystoRarityTierV4 Tiers[] = {
      EEFCalystoRarityTierV4::Common, EEFCalystoRarityTierV4::Uncommon,
      EEFCalystoRarityTierV4::Rare, EEFCalystoRarityTierV4::Epic};
  const float Scores[] = {-1.5f, -.5f, .5f, 1.5f};
  FEFCalystoTierMixV4 R;
  float OriginalEligibleMass = 0.0f;
  double WeightedTotal = 0.0;
  for (int32 I = 0; I < 4; ++I) {
    if (!EligibleTiers.Contains(Tiers[I])) {
      continue;
    }
    OriginalEligibleMass += Input.Get(Tiers[I]);
    WeightedTotal += Input.Get(Tiers[I]) *
                     FMath::Exp(FMath::Clamp(Mystery, -1.0f, 1.0f) *
                                Scores[I] * (FMath::Loge(3.0) / 3.0));
  }
  if (WeightedTotal > 0.0) {
    for (int32 I = 0; I < 4; ++I) {
      if (!EligibleTiers.Contains(Tiers[I])) {
        continue;
      }
      const double W = Input.Get(Tiers[I]) *
                       FMath::Exp(FMath::Clamp(Mystery, -1.0f, 1.0f) *
                                  Scores[I] * (FMath::Loge(3.0) / 3.0));
      R.Set(Tiers[I],
            static_cast<float>(OriginalEligibleMass * W / WeightedTotal));
    }
  }
  R.RefreshNothing();
  return R;
}

bool FEFCalystoDungeonDirectorMathV4::ValidateTierMix(
    const FEFCalystoTierMixV4 &M, FString *OutError) {
  const double SelectableMass =
      static_cast<double>(M.Common) + static_cast<double>(M.Uncommon) +
      static_cast<double>(M.Rare) + static_cast<double>(M.Epic);
  const bool bValid =
      FMath::IsFinite(M.Common) && FMath::IsFinite(M.Uncommon) &&
      FMath::IsFinite(M.Rare) && FMath::IsFinite(M.Epic) && M.Common >= 0.0f &&
      M.Uncommon >= 0.0f && M.Rare >= 0.0f && M.Epic >= 0.0f &&
      FMath::IsFinite(SelectableMass) &&
      SelectableMass <=
          static_cast<double>(EFCalystoMathV4Private::ProbabilityCap) +
              EFCalystoMathV4Private::TierMassValidationTolerance &&
      M.GetCalculatedNothing() >= 0.10f;
  if (!bValid && OutError) {
    *OutError = TEXT(
        "Common+Uncommon+Rare+Epic must be finite, non-negative and <= 0.90.");
  }
  return bValid;
}

FEFCalystoEnemyLevelBandV4
FEFCalystoDungeonDirectorMathV4::ResolveEnemyLevelBand(
    const EEFCalystoRarityTierV4 Tier, const int32 DirectorLevel,
    const int32 LogicalWinterLevel) {
  FEFCalystoEnemyLevelBandV4 R;
  const int32 A = FMath::Clamp(DirectorLevel, 1, 100);
  switch (Tier) {
  case EEFCalystoRarityTierV4::Common:
    R.Min = A - 3;
    R.Mode = A;
    R.Max = A + 2;
    break;
  case EEFCalystoRarityTierV4::Uncommon:
    R.Min = A - 1;
    R.Mode = A + 2;
    R.Max = A + 5;
    break;
  case EEFCalystoRarityTierV4::Rare:
    R.Min = A + 2;
    R.Mode = A + 5;
    R.Max = A + 10;
    break;
  case EEFCalystoRarityTierV4::Epic:
    R.Min = A + 6;
    R.Mode = A + 10;
    R.Max = A + 16;
    break;
  case EEFCalystoRarityTierV4::Winter:
    R.Min = LogicalWinterLevel - 2;
    R.Mode = LogicalWinterLevel + 2;
    R.Max = LogicalWinterLevel + 6;
    break;
  default:
    break;
  }
  if (Tier == EEFCalystoRarityTierV4::Winter) {
    R.Min = FMath::Max(101, R.Min);
    R.Mode = FMath::Max(R.Min, R.Mode);
    R.Max = FMath::Max(R.Mode, R.Max);
  } else {
    R.Min = FMath::Clamp(R.Min, 1, 100);
    R.Mode = FMath::Clamp(R.Mode, R.Min, 100);
    R.Max = FMath::Clamp(R.Max, R.Mode, 100);
  }
  return R;
}

float FEFCalystoDungeonDirectorMathV4::LevelCostMultiplier(const int32 L) {
  const int32 Level = FMath::Max(L, 1);
  const double Offset = static_cast<double>(Level - 1);
  const double Product =
      (1.0 + .12 * Offset) * (1.0 + .08 * Offset) * (1.0 + .06 * Offset);
  const double Power = FMath::Pow(Product, 1.0 / 3.0);
  return FMath::IsFinite(Power) ? static_cast<float>(Power) : 0.0f;
}

double FEFCalystoDungeonDirectorMathV4::WeightEnemyBundleForDanger(
    const double BaseWeight, const float Danger,
    const float EffectiveThreatCost, const float MaximumThreatCost,
    const int32 LogicalLevel, const int32 MaximumLogicalLevel) {
  if (!FMath::IsFinite(BaseWeight) || BaseWeight <= 0.0 ||
      !FMath::IsFinite(Danger) || Danger < -1.0f || Danger > 1.0f ||
      !FMath::IsFinite(EffectiveThreatCost) || EffectiveThreatCost < 0.0f ||
      !FMath::IsFinite(MaximumThreatCost) || MaximumThreatCost < 0.0f ||
      LogicalLevel < 1 || MaximumLogicalLevel < LogicalLevel) {
    return 0.0;
  }
  const double ThreatNorm =
      MaximumThreatCost > 0.0f
          ? FMath::Clamp(static_cast<double>(EffectiveThreatCost) /
                             static_cast<double>(MaximumThreatCost),
                         0.0, 1.0)
          : 0.0;
  const double LevelNorm = FMath::Clamp(
      static_cast<double>(LogicalLevel) /
          static_cast<double>(FMath::Max(1, MaximumLogicalLevel)),
      0.0, 1.0);
  const double DangerScore = .60 * ThreatNorm + .40 * LevelNorm;
  const double Result =
      BaseWeight * FMath::Exp((FMath::Loge(3.0) / 2.0) *
                              static_cast<double>(Danger) * DangerScore);
  return FMath::IsFinite(Result) && Result > 0.0 ? Result : 0.0;
}

bool FEFCalystoDungeonDirectorResolverV4::Validate(
    const UEFCalystoDungeonDirectorPolicyV4 *Policy, FString &OutError) {
  if (!IsValid(Policy)) {
    OutError = TEXT("V4 policy is null.");
    return false;
  }
  if (!FEFCalystoDungeonDirectorMathV4::ValidateDomainUniqueness(&OutError)) {
    return false;
  }
  return Policy->Validate(OutError);
}

FString FEFCalystoDungeonDirectorResolverV4::GetPolicyHash(
    const UEFCalystoDungeonDirectorPolicyV4 *Policy) {
  FString Error;
  return Validate(Policy, Error)
             ? EFCalystoMathV4Private::HashText(Policy->BuildCanonicalString())
             : FString();
}

bool FEFCalystoDungeonDirectorResolverV4::Resolve(
    const UEFCalystoDungeonDirectorPolicyV4 *Policy,
    const FEFCalystoResolveContextV4 &Context,
    FEFCalystoResolvedFloorIntentV4 &OutIntent, FString &OutError) {
  return ResolveInternal(Policy, nullptr, Context, OutIntent, OutError);
}

#if WITH_DEV_AUTOMATION_TESTS
bool FEFCalystoDungeonDirectorResolverV4::ResolvePrevalidatedForTesting(
    const UEFCalystoDungeonDirectorPolicyV4 *Policy,
    const FString &PrevalidatedPolicyHash,
    const FEFCalystoResolveContextV4 &Context,
    FEFCalystoResolvedFloorIntentV4 &OutIntent, FString &OutError) {
  return ResolveInternal(Policy, &PrevalidatedPolicyHash, Context, OutIntent,
                         OutError);
}
#endif

bool FEFCalystoDungeonDirectorResolverV4::ResolveInternal(
    const UEFCalystoDungeonDirectorPolicyV4 *Policy,
    const FString *PrevalidatedPolicyHash,
    const FEFCalystoResolveContextV4 &Context,
    FEFCalystoResolvedFloorIntentV4 &OutIntent, FString &OutError) {
  using namespace EFCalystoMathV4Private;
  OutIntent = {};
  if (PrevalidatedPolicyHash) {
#if WITH_DEV_AUTOMATION_TESTS
    bool bHashIsCanonicalSHA256 = PrevalidatedPolicyHash->Len() == 64;
    for (const TCHAR Character : *PrevalidatedPolicyHash) {
      bHashIsCanonicalSHA256 &= FChar::IsHexDigit(Character);
    }
    if (!IsValid(Policy) || Policy->SchemaVersion != 4 ||
        Policy->GeneratorVersion != 4 ||
        Policy->PolicyId != FName(TEXT("CalystoDungeonDirectorV4")) ||
        !bHashIsCanonicalSHA256) {
      OutError = TEXT("Prevalidated V4 Automation resolution requires an "
                      "immutable schema-4 policy and canonical SHA-256.");
      return false;
    }
#else
    OutError = TEXT("Prevalidated V4 Automation resolution is unavailable in "
                    "this build.");
    return false;
#endif
  } else if (!Validate(Policy, OutError)) {
    return false;
  }
  if (Context.RunSeed <= 0 || Context.FloorNumber < 1 ||
      Context.GenerationSerial < 1 || Context.EcologyHash.IsEmpty() ||
      !IsFiniteUnit(Context.PerformanceEMA) ||
      !IsFiniteBias(Context.DirectorIntent.Scale) ||
      !IsFiniteBias(Context.DirectorIntent.Branching) ||
      !IsFiniteBias(Context.DirectorIntent.Danger) ||
      !IsFiniteBias(Context.DirectorIntent.Safe) ||
      !IsFiniteBias(Context.DirectorIntent.Abundance) ||
      !IsFiniteBias(Context.DirectorIntent.Mystery) ||
      !IsFiniteBias(Context.DirectorIntent.ClothingInfluence) ||
      !IsFiniteBias(Context.DirectorIntent.Volatility) ||
      !IsFiniteBias(Context.ResolvedRunTraits.Scale) ||
      !IsFiniteBias(Context.ResolvedRunTraits.Branching) ||
      !IsFiniteBias(Context.ResolvedRunTraits.Danger) ||
      !IsFiniteBias(Context.ResolvedRunTraits.Safe) ||
      !IsFiniteBias(Context.ResolvedRunTraits.Abundance) ||
      !IsFiniteBias(Context.ResolvedRunTraits.Mystery) ||
      !IsFiniteBias(Context.ResolvedRunTraits.ClothingInfluence) ||
      !IsFiniteUnit(Context.ResolvedRunTraits.Volatility) ||
      Context.ConsecutiveStyleCount < 0 || Context.ConsecutiveThemeCount < 0 ||
      Context.MaximumConsecutiveStyle < 1 ||
      Context.MaximumConsecutiveTheme < 1 ||
      Context.ConsecutiveFloorsWithoutFood < 0 ||
      Context.ConsecutiveFloorsWithoutChest < 0) {
    OutError = TEXT("V4 resolve context contains invalid ranges, NaN, infinity "
                    "or missing frozen identity.");
    return false;
  }
  if (!IsKnownDevelopmentScenario(Context.DevelopmentPopulationScenario) ||
      (Context.DevelopmentForcedDungeonEdge != 0 &&
       (Context.DevelopmentForcedDungeonEdge < 18 ||
        Context.DevelopmentForcedDungeonEdge > 30))) {
    OutError = TEXT("V4 Development automation contains an unknown scenario "
                    "or an out-of-range dungeon edge.");
    return false;
  }
#if UE_BUILD_SHIPPING
  if (Context.DevelopmentForcedDungeonEdge != 0 ||
      !Context.DevelopmentPopulationScenario.IsNone()) {
    OutError =
        TEXT("Shipping rejects every V4 Development automation override.");
    return false;
  }
#endif
  if (Context.FloorNumber > static_cast<int64>(MAX_int32) - 6) {
    OutError = TEXT("V4 floor number cannot be represented safely by per-slot "
                    "logical levels.");
    return false;
  }
  if (Context.bHasFrozenOutcome) {
    const FEFCalystoFloorOutcomeV4 &O = Context.FrozenOutcome;
    if (!IsFiniteUnit(O.Combat) || !IsFiniteUnit(O.Survival) ||
        !IsFiniteUnit(O.Resources) || !IsFiniteUnit(O.Pace) ||
        !IsFiniteUnit(O.DeathsAndFailures)) {
      OutError = TEXT("Frozen V4 floor outcome contains invalid values.");
      return false;
    }
  }
  const FString PolicyHash = PrevalidatedPolicyHash
                                 ? *PrevalidatedPolicyHash
                                 : GetPolicyHash(Policy);
  const FString ComputedOutcomeHash = BuildOutcomeHash(Context);
  if (!Context.OutcomeHash.IsEmpty() &&
      !Context.OutcomeHash.Equals(ComputedOutcomeHash,
                                  ESearchCase::IgnoreCase)) {
    OutError =
        TEXT("Frozen V4 outcome hash does not match its canonical payload.");
    return false;
  }
  const FString ComputedCompanionHash =
      BuildCompanionSnapshotHash(Context.CompanionSnapshot);
  if (Context.CompanionSnapshotHash.IsEmpty() ||
      !Context.CompanionSnapshotHash.Equals(ComputedCompanionHash,
                                            ESearchCase::IgnoreCase)) {
    OutError = TEXT("Frozen V4 companion snapshot hash does not match its "
                    "canonical payload.");
    return false;
  }
  TSet<FGuid> CompanionIds;
  int32 ActivePartyCount = 0;
  const bool bHasRosterHistory = !Context.CompanionSnapshot.Records.IsEmpty();
  bool bHasConfirmedDeadCompanion = false;
  for (const FEFCalystoCompanionRecordV4 &Record :
       Context.CompanionSnapshot.Records) {
    if (!Record.StableCompanionId.IsValid() ||
        CompanionIds.Contains(Record.StableCompanionId) ||
        Record.SourceSpawnId.IsNone() || Record.SourceCatalogId.IsNone() ||
        Record.SourceVariantId.IsNone() || Record.Archetype.IsNone() ||
        Record.ActorClass.IsNull() || !IsKnownGender(Record.Gender) ||
        !IsKnownTier(Record.Grade) || !IsKnownCompanionState(Record.State)) {
      OutError = TEXT("Frozen V4 companion snapshot contains an invalid or "
                      "duplicated record.");
      return false;
    }
    CompanionIds.Add(Record.StableCompanionId);
    if (Record.State == EEFCalystoCompanionRosterStateV4::ActiveParty) {
      ++ActivePartyCount;
    }
    if (Record.State == EEFCalystoCompanionRosterStateV4::Dead) {
      bHasConfirmedDeadCompanion = true;
    }
  }
  if (ActivePartyCount > 2) {
    OutError = TEXT(
        "Frozen V4 companion snapshot exceeds the active-party cap of two.");
    return false;
  }
#if !UE_BUILD_SHIPPING
  if (IsDevelopmentCompanionRecallScenario(
          Context.DevelopmentPopulationScenario)) {
    const bool bFloorOneContract =
        Context.FloorNumber == 1 &&
        Context.CompanionSnapshot.Records.IsEmpty() &&
        !Context.CompanionSnapshot.bPlayerOwnsWintersRecall;
    const bool bFloorTwoContract =
        Context.FloorNumber == 2 &&
        Context.CompanionSnapshot.Records.Num() == 1 &&
        Context.CompanionSnapshot.Records[0].State ==
            EEFCalystoCompanionRosterStateV4::Dead &&
        Context.CompanionSnapshot.Records[0].SourceCatalogId ==
            FName(TEXT("NPC.Companion.Generalist.Female")) &&
        ActivePartyCount == 0 && bHasConfirmedDeadCompanion &&
        !Context.CompanionSnapshot.bPlayerOwnsWintersRecall;
    if (!bFloorOneContract && !bFloorTwoContract) {
      OutError = TEXT(
          "CompanionRecallLifecycle only accepts an empty Floor 1 snapshot "
          "or exactly one confirmed-dead Generalist Female on Floor 2, and "
          "never accepts an already-owned Winter's Recall.");
      return false;
    }
  }
#endif
  if ((Context.ConsecutiveStyleCount > 0 &&
       !Policy->FindStyle(Context.PreviousStyle)) ||
      (Context.ConsecutiveThemeCount > 0 &&
       !Policy->FindTheme(Context.PreviousTheme))) {
    OutError =
        TEXT("V4 anti-streak memory references an unknown Style or Theme.");
    return false;
  }

  OutIntent.GeneratorVersion = 4;
  OutIntent.RunSeed = Context.RunSeed;
  OutIntent.FloorNumber = Context.FloorNumber;
  OutIntent.GenerationSerial = Context.GenerationSerial;
  OutIntent.PolicyHash = PolicyHash;
  OutIntent.EcologyHash = Context.EcologyHash;
  OutIntent.CompanionSnapshotHash = ComputedCompanionHash;
  OutIntent.CompanionSnapshot = Context.CompanionSnapshot;
  OutIntent.DevelopmentForcedDungeonEdge = Context.DevelopmentForcedDungeonEdge;
  OutIntent.DevelopmentPopulationScenario =
      Context.DevelopmentPopulationScenario;
  OutIntent.OutcomeHash = ComputedOutcomeHash;
  OutIntent.bHasFrozenOutcome = Context.bHasFrozenOutcome;
  OutIntent.FrozenOutcome = Context.FrozenOutcome;
  OutIntent.StyleSelectionDraw =
      static_cast<float>(FEFCalystoDungeonDirectorMathV4::Uniform01(
          Context, PolicyHash, EFCalystoDungeonDomainsV4::Style));
  OutIntent.ThemeSelectionDraw =
      static_cast<float>(FEFCalystoDungeonDirectorMathV4::Uniform01(
          Context, PolicyHash, EFCalystoDungeonDomainsV4::Theme));

  if (Context.DirectorIntent.bHasPreferredStyle &&
      !Policy->FindStyle(Context.DirectorIntent.PreferredStyle)) {
    OutError = TEXT("Preferred V4 Style does not exist.");
    return false;
  }
#if !UE_BUILD_SHIPPING
  const auto SupportsDevelopmentEdge =
      [&Context, Policy](const FEFCalystoLayoutProfileV4 &Layout) {
        const int32 Edge = Context.DevelopmentForcedDungeonEdge;
        return Edge == 0 ||
               (Policy->ValidatedDungeonSizes.Contains(Edge) &&
                Edge >= Layout.MinimumDungeonEdge &&
                Edge <= Layout.MaximumDungeonEdge);
      };
#endif
  TArray<float> StyleWeights;
  for (const auto &Profile : Policy->Styles) {
    float Weight =
        Context.ConsecutiveStyleCount >= Context.MaximumConsecutiveStyle &&
                Profile.Style == Context.PreviousStyle
            ? 0.0f
            : Profile.SelectionProbability;
#if !UE_BUILD_SHIPPING
    // An exact Development edge certifies geometry, not failure handling. Keep
    // the ordinary weighted Style draw, but remove profiles whose authored
    // limits cannot legally realize that edge. Normal runtime selection is
    // byte-for-byte unchanged because the override is zero outside automation.
    if (Weight > 0.0f && !SupportsDevelopmentEdge(Profile.Layout)) {
      Weight = 0.0f;
    }
    if (Weight > 0.0f && Context.DevelopmentForcedDungeonEdge != 0) {
      const bool bAnyCompatibleTheme = Policy->Themes.ContainsByPredicate(
          [&SupportsDevelopmentEdge](const FEFCalystoThemeProfileV4 &Theme) {
            return Theme.SelectionProbability > 0.0f &&
                   SupportsDevelopmentEdge(Theme.Layout);
          });
      if (!bAnyCompatibleTheme) {
        Weight = 0.0f;
      }
    }
#endif
    if (Weight > 0.0f && Context.DirectorIntent.bHasPreferredStyle &&
        Profile.Style == Context.DirectorIntent.PreferredStyle) {
      Weight *= 3.0f;
    }
    StyleWeights.Add(Weight);
  }
  int32 StyleIndex =
      SelectWeighted(StyleWeights, OutIntent.StyleSelectionDraw);
#if !UE_BUILD_SHIPPING
  // The explicit unattended population fixtures validate exact actor caps at
  // the certified 30x30 edge. Pin only those transient Development scenarios
  // to the neutral Standard profile so a legitimate Compact limit (26) cannot
  // make an otherwise unrelated population acceptance test nondeterministic.
  if (!Context.DevelopmentPopulationScenario.IsNone()) {
    StyleIndex = Policy->Styles.IndexOfByPredicate(
        [](const FEFCalystoStyleProfileV4 &Profile) {
          return Profile.Style == EEFCalystoStyleV4::Standard;
        });
  }
#endif
  if (!Policy->Styles.IsValidIndex(StyleIndex)) {
    OutError = TEXT("V4 Style selection has no eligible weight.");
    return false;
  }
  OutIntent.Style = Policy->Styles[StyleIndex].Style;
  if (Context.DirectorIntent.bHasPreferredTheme &&
      !Policy->FindTheme(Context.DirectorIntent.PreferredTheme)) {
    OutError = TEXT("Preferred V4 Theme does not exist.");
    return false;
  }
  TArray<float> ThemeWeights;
  for (const auto &Profile : Policy->Themes) {
    float Weight =
        Context.ConsecutiveThemeCount >= Context.MaximumConsecutiveTheme &&
                Profile.Theme == Context.PreviousTheme
            ? 0.0f
            : Profile.SelectionProbability;
#if !UE_BUILD_SHIPPING
    if (Weight > 0.0f && !SupportsDevelopmentEdge(Profile.Layout)) {
      Weight = 0.0f;
    }
#endif
    if (Weight > 0.0f && Context.DirectorIntent.bHasPreferredTheme &&
        Profile.Theme == Context.DirectorIntent.PreferredTheme) {
      Weight *= 3.0f;
    }
    ThemeWeights.Add(Weight);
  }
  int32 ThemeIndex =
      SelectWeighted(ThemeWeights, OutIntent.ThemeSelectionDraw);
#if !UE_BUILD_SHIPPING
  // See the Style pin above. Default is the neutral Theme and supports the
  // certified 30x30 acceptance edge without changing normal V4 selection.
  if (!Context.DevelopmentPopulationScenario.IsNone()) {
    ThemeIndex = Policy->Themes.IndexOfByPredicate(
        [](const FEFCalystoThemeProfileV4 &Profile) {
          return Profile.Theme == EEFCalystoThemeV4::Default;
        });
  }
#endif
  if (!Policy->Themes.IsValidIndex(ThemeIndex)) {
    OutError = TEXT("V4 Theme selection has no eligible weight.");
    return false;
  }
  OutIntent.Theme = Policy->Themes[ThemeIndex].Theme;
  const FEFCalystoStyleProfileV4 *S = Policy->FindStyle(OutIntent.Style);
  const FEFCalystoThemeProfileV4 *T = Policy->FindTheme(OutIntent.Theme);
  if (!S || !T) {
    OutError = TEXT("Resolved V4 Style/Theme disappeared.");
    return false;
  }
  OutIntent.CalystoRoomType = T->RoomType;
  const float Volatility =
      FMath::Clamp((S->Volatility + T->Volatility) / 2.0f +
                       .25f * Context.DirectorIntent.Volatility,
                   0.0f, 1.0f);
  FEFCalystoPertRangeV4 BlendD{0.0f, .5f, 1.0f, 8.0f - 6.0f * Volatility};
  OutIntent.ShapeBlend = FEFCalystoDungeonDirectorMathV4::SamplePERT(
      BlendD, .5f, Context, PolicyHash, EFCalystoDungeonDomainsV4::Shape);
  auto RT = [&](float SV, float TV, float RV) {
    return FMath::Clamp(FMath::Lerp(SV, TV, OutIntent.ShapeBlend) + RV, -1.0f,
                        1.0f);
  };
  OutIntent.ResolvedTraits.Scale =
      RT(0.0f, 0.0f, Context.ResolvedRunTraits.Scale);
  OutIntent.ResolvedTraits.Branching =
      RT(0.0f, 0.0f, Context.ResolvedRunTraits.Branching);
  OutIntent.ResolvedTraits.Mystery = RT(S->Mystery, T->Mystery,
                                        Context.ResolvedRunTraits.Mystery);
  OutIntent.ResolvedTraits.Danger =
      RT(S->Danger, T->Danger, Context.ResolvedRunTraits.Danger);
  OutIntent.ResolvedTraits.Safe =
      RT(S->Safe, T->Safe, Context.ResolvedRunTraits.Safe);
  OutIntent.ResolvedTraits.Abundance =
      RT(S->Abundance, T->Abundance,
         Context.ResolvedRunTraits.Abundance);
  OutIntent.ResolvedTraits.ClothingInfluence =
      RT(S->ClothingInfluence, T->ClothingInfluence,
         Context.ResolvedRunTraits.ClothingInfluence);
  OutIntent.ResolvedTraits.Volatility = Volatility;

  const float SG =
      static_cast<float>(FEFCalystoDungeonDirectorMathV4::Progression(
          Context.FloorNumber, S->Layout.SizeTau));
  const float TG =
      static_cast<float>(FEFCalystoDungeonDirectorMathV4::Progression(
          Context.FloorNumber, T->Layout.SizeTau));
  float Mode = FMath::Lerp(
      FMath::Lerp(S->Layout.EarlySizeMode, S->Layout.DeepSizeMode, SG),
      FMath::Lerp(T->Layout.EarlySizeMode, T->Layout.DeepSizeMode, TG),
      OutIntent.ShapeBlend);
  Mode += 2.0f * OutIntent.ResolvedTraits.Scale +
          2.0f * Context.DirectorIntent.Scale;
  const int32 MinEdge =
      FMath::Max(S->Layout.MinimumDungeonEdge, T->Layout.MinimumDungeonEdge);
  const int32 MaxEdge =
      FMath::Min(S->Layout.MaximumDungeonEdge, T->Layout.MaximumDungeonEdge);
  Mode = FMath::Clamp(Mode, static_cast<float>(MinEdge),
                      static_cast<float>(MaxEdge));
  const float Half = FMath::Lerp(S->Layout.SizeHalfRange,
                                 T->Layout.SizeHalfRange, OutIntent.ShapeBlend);
  FEFCalystoPertRangeV4 SizeD{
      FMath::Max(static_cast<float>(MinEdge), Mode - Half), Mode,
      FMath::Min(static_cast<float>(MaxEdge), Mode + Half), 4.0f};
  int32 Edge = FMath::RoundToInt(FEFCalystoDungeonDirectorMathV4::SamplePERT(
      SizeD, Volatility, Context, PolicyHash, EFCalystoDungeonDomainsV4::Shape,
      1));
  int32 Best = INDEX_NONE, BestDistance = MAX_int32;
  for (const int32 Candidate : Policy->ValidatedDungeonSizes) {
    if (Candidate < MinEdge || Candidate > MaxEdge) {
      continue;
    }
    const int32 D = FMath::Abs(Candidate - Edge);
    if (D < BestDistance || (D == BestDistance && Candidate < Best)) {
      Best = Candidate;
      BestDistance = D;
    }
  }
  if (Best == INDEX_NONE) {
    OutError =
        TEXT("Selected V4 Style/Theme has no certified size intersection.");
    return false;
  }
#if !UE_BUILD_SHIPPING
  if (Context.DevelopmentForcedDungeonEdge != 0) {
    const int32 ForcedEdge = Context.DevelopmentForcedDungeonEdge;
    if (ForcedEdge < MinEdge || ForcedEdge > MaxEdge ||
        !Policy->ValidatedDungeonSizes.Contains(ForcedEdge)) {
      OutError = TEXT("DevelopmentForcedDungeonEdge is not certified for the "
                      "resolved V4 Style/Theme intersection.");
      return false;
    }
    Best = ForcedEdge;
  }
#endif
  OutIntent.DungeonSize = FIntVector(Best, Best, 1);
  FEFCalystoPertRangeV4 AD;
  AD.Min =
      FMath::Lerp(S->Layout.CandidateAnchorDensity.Min,
                  T->Layout.CandidateAnchorDensity.Min, OutIntent.ShapeBlend);
  AD.Mode =
      FMath::Lerp(S->Layout.CandidateAnchorDensity.Mode,
                  T->Layout.CandidateAnchorDensity.Mode, OutIntent.ShapeBlend);
  AD.Max =
      FMath::Lerp(S->Layout.CandidateAnchorDensity.Max,
                  T->Layout.CandidateAnchorDensity.Max, OutIntent.ShapeBlend);
  AD.Concentration = FMath::Lerp(S->Layout.CandidateAnchorDensity.Concentration,
                                 T->Layout.CandidateAnchorDensity.Concentration,
                                 OutIntent.ShapeBlend);
  OutIntent.CandidateAnchorDensity =
      FEFCalystoDungeonDirectorMathV4::SamplePERT(
          AD, Volatility, Context, PolicyHash,
          EFCalystoDungeonDomainsV4::AnchorDensity);
  const float BaseBranch = FEFCalystoDungeonDirectorMathV4::BlendProbability(
      EvaluateChance(S->Layout.BranchingChance, Context.FloorNumber),
      EvaluateChance(T->Layout.BranchingChance, Context.FloorNumber),
      OutIntent.ShapeBlend);
  OutIntent.SidePathChance = FMath::Clamp(
      LogOddsAdjusted(BaseBranch, OutIntent.ResolvedTraits.Branching,
                      Context.DirectorIntent.Branching, 0.0f),
      .30f, .70f);
  // Lighting owns a dedicated counter-RNG domain.  It varies only the two
  // native Calysto inputs, never the population or PCG seed domains.
  OutIntent.LightingBlend = FEFCalystoDungeonDirectorMathV4::SamplePERT(
      BlendD, Volatility, Context, PolicyHash,
      EFCalystoDungeonDomainsV4::NativeLighting,
      FEFCalystoDungeonDirectorMathV4::StableNameId(TEXT("NativeWallLight")));
  OutIntent.NativeWallLightHeight = FMath::Lerp(
      S->Lighting.Height, T->Lighting.Height, OutIntent.LightingBlend);
  OutIntent.NativeWallLightTileDistance = FMath::Clamp(
      FMath::RoundToInt(FMath::Lerp(
          static_cast<float>(S->Lighting.TileDistance),
          static_cast<float>(T->Lighting.TileDistance),
          OutIntent.LightingBlend)),
      1, 100);
  OutIntent.PCGSeed = static_cast<int32>(
      1ULL +
      (FEFCalystoDungeonDirectorMathV4::DeriveCounterValue(
           Context, PolicyHash, EFCalystoDungeonDomainsV4::Shape,
           FEFCalystoDungeonDirectorMathV4::StableNameId(TEXT("PCGSeed"))) %
       2147483646ULL));
  OutIntent.DirectorLevel =
      FMath::Clamp(static_cast<int32>(Context.FloorNumber), 1, 100);
  OutIntent.LogicalWinterLevel =
      Context.FloorNumber <= 100 ? 0 : static_cast<int32>(Context.FloorNumber);
  for (const FEFCalystoCompanionRecordV4 &Record :
       Context.CompanionSnapshot.Records) {
    const FEFCalystoEnemyLevelBandV4 Band =
        FEFCalystoDungeonDirectorMathV4::ResolveEnemyLevelBand(
            Record.Grade, OutIntent.DirectorLevel,
            OutIntent.LogicalWinterLevel);
    const uint64 Stable = FEFCalystoDungeonDirectorMathV4::StableNameId(
        FName(*Record.StableCompanionId.ToString(EGuidFormats::Digits)));
    FEFCalystoResolvedCompanionLevelV4 Level;
    Level.StableCompanionId = Record.StableCompanionId;
    Level.Grade = Record.Grade;
    Level.LogicalLevel = FEFCalystoDungeonDirectorMathV4::SampleDiscretePERT(
        Band.Min, Band.Mode, Band.Max, 4.0f, Volatility, Context, PolicyHash,
        EFCalystoDungeonDomainsV4::NPC, Stable, 0);
    Level.PhysicalACFLevel = FMath::Min(Level.LogicalLevel, 100);
    OutIntent.ResolvedCompanionLevels.Add(MoveTemp(Level));
  }
  OutIntent.ResolvedCompanionLevels.Sort([](const auto &A, const auto &B) {
    return A.StableCompanionId.ToString(EGuidFormats::Digits) <
           B.StableCompanionId.ToString(EGuidFormats::Digits);
  });
  const float SB = FMath::Lerp(
      S->Threat.EarlyBudget, S->Threat.DeepBudget,
      static_cast<float>(FEFCalystoDungeonDirectorMathV4::Progression(
          Context.FloorNumber, S->Threat.Tau)));
  const float TB = FMath::Lerp(
      T->Threat.EarlyBudget, T->Threat.DeepBudget,
      static_cast<float>(FEFCalystoDungeonDirectorMathV4::Progression(
          Context.FloorNumber, T->Threat.Tau)));
  const float BM = FEFCalystoDungeonDirectorMathV4::BlendProbability(
      SB, TB, OutIntent.ShapeBlend);
  const float BR = FEFCalystoDungeonDirectorMathV4::BlendProbability(
      S->Threat.RelativeRange, T->Threat.RelativeRange, OutIntent.ShapeBlend);
  OutIntent.ThreatBudget = FMath::Min(
      Policy->SafetyCeilings.MaximumThreatBudget,
      FEFCalystoDungeonDirectorMathV4::SamplePERT(
          {BM * (1 - BR), BM, BM * (1 + BR), 4.0f}, Volatility, Context,
          PolicyHash, EFCalystoDungeonDomainsV4::ThreatBudget));

  const EEFCalystoContentCategoryV4 Categories[] = {
      EEFCalystoContentCategoryV4::Enemy,
      EEFCalystoContentCategoryV4::NPC,
      EEFCalystoContentCategoryV4::Food,
      EEFCalystoContentCategoryV4::Chest,
      EEFCalystoContentCategoryV4::LooseLoot,
      EEFCalystoContentCategoryV4::Clothing,
      EEFCalystoContentCategoryV4::SpecialEvent,
      EEFCalystoContentCategoryV4::Decoration,
      EEFCalystoContentCategoryV4::Lighting};
  TSet<EEFCalystoContentCategoryV4> DisabledGenerationCategories;
  for (const EEFCalystoContentCategoryV4 C : Categories) {
    FEFCalystoResolvedCategoryV4 R;
    R.Category = C;
    const auto *SC = Policy->FindCategory(S->Categories, C);
    const auto *TC = Policy->FindCategory(T->Categories, C);
    if (!SC || !TC) {
      OutError = TEXT("V4 Style/Theme category contract is incomplete.");
      return false;
    }
    // Theme is the sole authoring authority for category activation. Style may
    // still veto a combination through bBlocked, but its legacy serialized
    // bEnabled value is intentionally ignored and hidden by the editor module.
    const bool bCategoryGenerationEnabled =
        TC->bEnabled && !SC->bBlocked && !TC->bBlocked;
    if (!bCategoryGenerationEnabled) {
      DisabledGenerationCategories.Add(C);
    }
    const uint64 Stable = static_cast<uint64>(C) + 1;
    R.StyleThemeBlend = FEFCalystoDungeonDirectorMathV4::SamplePERT(
        BlendD, .5f, Context, PolicyHash,
        EFCalystoDungeonDomainsV4::CategoryBlend, Stable);
    const FEFCalystoContextTraitsV4 StyleTraits = S->GetAuthoredTraits();
    const FEFCalystoContextTraitsV4 ThemeTraits = T->GetAuthoredTraits();
    R.ResolvedInfluence = FMath::Clamp(
        FMath::Lerp(TraitForCategory(StyleTraits, C),
                    TraitForCategory(ThemeTraits, C), R.StyleThemeBlend) +
            TraitForCategory(Context.ResolvedRunTraits, C),
        -1.0f, 1.0f);
    R.MinimumWhenPresent = FMath::Max(SC->Limits.MinimumWhenPresent,
                                      TC->Limits.MinimumWhenPresent);
    R.MaximumPerFloor =
        FMath::Min(SC->Limits.MaximumPerFloor, TC->Limits.MaximumPerFloor);
    int32 MaximumAttempts = R.MaximumPerFloor;
    if (C == EEFCalystoContentCategoryV4::NPC) {
      MaximumAttempts = FMath::Min(
          R.MaximumPerFloor,
          FMath::Max(0, Policy->SafetyCeilings.MaximumNPCs - ActivePartyCount));
      R.MinimumWhenPresent = FMath::Min(R.MinimumWhenPresent, MaximumAttempts);
    }
    TSet<EEFCalystoRarityTierV4> Eligible;
    for (const EEFCalystoRarityTierV4 CandidateTier : {
             EEFCalystoRarityTierV4::Common,
             EEFCalystoRarityTierV4::Uncommon,
             EEFCalystoRarityTierV4::Rare,
             EEFCalystoRarityTierV4::Epic}) {
      if (EFCalystoMathV4Private::IsTierUnlocked(CandidateTier,
                                                 Context.FloorNumber)) {
        Eligible.Add(CandidateTier);
      }
    }
    const FEFCalystoTierMixV4 ST =
        EvaluateTiers(SC->Tiers, Context.FloorNumber, SC->Chance.Tau);
    const FEFCalystoTierMixV4 TT =
        EvaluateTiers(TC->Tiers, Context.FloorNumber, TC->Chance.Tau);
    const bool bInfluenceTiers =
        C == EEFCalystoContentCategoryV4::NPC ||
        C == EEFCalystoContentCategoryV4::Food ||
        C == EEFCalystoContentCategoryV4::Chest ||
        C == EEFCalystoContentCategoryV4::Clothing;
    R.ResolvedTiers = FEFCalystoDungeonDirectorMathV4::ApplyMystery(
        FEFCalystoDungeonDirectorMathV4::BlendTiers(ST, TT, R.StyleThemeBlend),
        bInfluenceTiers ? R.ResolvedInfluence : 0.0f, Eligible);
    R.SelectableTierMass = R.ResolvedTiers.GetSelectableMass();
    const float Base = FEFCalystoDungeonDirectorMathV4::BlendProbability(
        EvaluateChance(SC->Chance, Context.FloorNumber),
        EvaluateChance(TC->Chance, Context.FloorNumber), R.StyleThemeBlend);
    const float Adapt =
        FMath::Lerp(S->PlayerAdaptationStrength, T->PlayerAdaptationStrength,
                    R.StyleThemeBlend);
    const float SignedEMA =
        (Context.PerformanceEMA - .5f) * 2.0f * Adapt * PerformanceDirection(C);
    R.OpportunityChance = bCategoryGenerationEnabled
                              ? LogOddsAdjusted(
                                    Base, R.ResolvedInfluence,
                                    IntentForCategory(Context.DirectorIntent, C),
                                    SignedEMA)
                              : 0.0f;
    const int32 Pity =
        FMath::RoundToInt(FEFCalystoDungeonDirectorMathV4::BlendProbability(
            static_cast<float>(SC->PityAfterEmptyFloors),
            static_cast<float>(TC->PityAfterEmptyFloors), R.StyleThemeBlend));
    if (bCategoryGenerationEnabled &&
        C == EEFCalystoContentCategoryV4::Food) {
      R.OpportunityChance = ApplyPity(
          R.OpportunityChance, Context.ConsecutiveFloorsWithoutFood, Pity);
    }
    if (bCategoryGenerationEnabled &&
        C == EEFCalystoContentCategoryV4::Chest) {
      R.OpportunityChance = ApplyPity(
          R.OpportunityChance, Context.ConsecutiveFloorsWithoutChest, Pity);
    }
    const float SW =
        Context.FloorNumber <= 100
            ? 0.0f
            : .90f * (1.0f - FMath::Exp(-static_cast<float>(
                                            Context.FloorNumber - 100) /
                                        S->WinterTau));
    const float TW =
        Context.FloorNumber <= 100
            ? 0.0f
            : .90f * (1.0f - FMath::Exp(-static_cast<float>(
                                            Context.FloorNumber - 100) /
                                        T->WinterTau));
    R.WinterChance =
        bCategoryGenerationEnabled && C == EEFCalystoContentCategoryV4::Enemy
            ? FEFCalystoDungeonDirectorMathV4::BlendProbability(
                  SW, TW, R.StyleThemeBlend)
            : 0.0f;
    R.EffectiveChance = R.OpportunityChance;
    R.bPresent = FEFCalystoDungeonDirectorMathV4::Bernoulli(
        R.OpportunityChance, Context, PolicyHash,
        EFCalystoDungeonDomainsV4::CategoryPresence, Stable);
    // Presence opens at least one catalog attempt even for categories authored
    // with MinimumWhenPresent=0. Such an attempt remains optional (and may
    // resolve to the permanent Nothing mass); only authored minimum slots are
    // conditioned on a non-empty tier. Chest-content attempts intentionally do
    // not use this base because their separate contract is explicitly 0..3.
    const int32 BaseCategoryAttempts =
        FMath::Min(MaximumAttempts, FMath::Max(1, R.MinimumWhenPresent));
    R.TargetCount = R.bPresent
                        ? FEFCalystoDungeonDirectorMathV4::SampleCount(
                              BaseCategoryAttempts, MaximumAttempts,
                              R.OpportunityChance, Context, PolicyHash, Stable)
                        : 0;
#if !UE_BUILD_SHIPPING
    if (IsDevelopmentZeroScenario(Context.DevelopmentPopulationScenario)) {
      R.bPresent = false;
      R.TargetCount = 0;
    } else if (Context.DevelopmentPopulationScenario ==
                   FName(TEXT("EnemyCap25")) &&
               C == EEFCalystoContentCategoryV4::Enemy) {
      if (R.MinimumWhenPresent != 25 || R.MaximumPerFloor != 25 ||
          MaximumAttempts != 25) {
        OutError = TEXT("EnemyCap25 requires the transient V4 Style and Theme "
                        "enemy limits to be exactly min=max=25.");
        OutIntent = {};
        return false;
      }
      R.bPresent = true;
      R.TargetCount = 25;
    } else if (Context.DevelopmentPopulationScenario ==
                   FName(TEXT("ResourceMax")) &&
               (C == EEFCalystoContentCategoryV4::Food ||
                C == EEFCalystoContentCategoryV4::Chest)) {
      const int32 ExpectedMaximum =
          C == EEFCalystoContentCategoryV4::Food ? 30 : 10;
      if (R.MinimumWhenPresent != ExpectedMaximum ||
          R.MaximumPerFloor != ExpectedMaximum ||
          MaximumAttempts != ExpectedMaximum) {
        OutError = TEXT("ResourceMax requires transient V4 Food min=max=30 "
                        "and Chest min=max=10 in both Style and Theme.");
        OutIntent = {};
        return false;
      }
      R.bPresent = true;
      R.TargetCount = ExpectedMaximum;
    } else if (Context.DevelopmentPopulationScenario ==
                   FName(TEXT("NPCTotal4")) &&
               C == EEFCalystoContentCategoryV4::NPC) {
      if (R.MinimumWhenPresent != 4 || R.MaximumPerFloor != 4 ||
          MaximumAttempts != 4 || ActivePartyCount != 0 || bHasRosterHistory) {
        OutError = TEXT(
            "NPCTotal4 requires an empty frozen roster and transient NPC "
            "limits exactly min=max=4.");
        OutIntent = {};
        return false;
      }
      R.bPresent = true;
      R.TargetCount = 4;
    } else if (Context.DevelopmentPopulationScenario ==
                   FName(TEXT("SpecialEvents6")) &&
               C == EEFCalystoContentCategoryV4::SpecialEvent) {
      if (R.MinimumWhenPresent != 6 || R.MaximumPerFloor != 6 ||
          MaximumAttempts != 6) {
        OutError = TEXT(
            "SpecialEvents6 requires transient Special Event limits exactly "
            "min=max=6.");
        OutIntent = {};
        return false;
      }
      R.bPresent = true;
      R.TargetCount = 6;
    } else if (IsDevelopmentCompanionRecallScenario(
                   Context.DevelopmentPopulationScenario) &&
               (C == EEFCalystoContentCategoryV4::NPC ||
                C == EEFCalystoContentCategoryV4::Chest)) {
      if (R.MinimumWhenPresent != 1 || R.MaximumPerFloor != 1 ||
          MaximumAttempts != 1) {
        OutError = TEXT(
            "CompanionRecallLifecycle requires transient NPC and Chest "
            "limits to remain exactly min=max=1.");
        OutIntent = {};
        return false;
      }
      const bool bEnableForFloor =
          C == EEFCalystoContentCategoryV4::NPC || Context.FloorNumber == 2;
      R.bPresent = bEnableForFloor;
      R.TargetCount = bEnableForFloor ? 1 : 0;
    } else if (IsDevelopmentNPCVariantScenario(
                   Context.DevelopmentPopulationScenario) &&
               C == EEFCalystoContentCategoryV4::NPC) {
      if (R.MinimumWhenPresent != 1 || R.MaximumPerFloor != 1 ||
          MaximumAttempts != 1) {
        OutError = TEXT("An exact V4 NPC variant fixture requires min=max=1 "
                        "and at most one eligible catalog entry.");
        OutIntent = {};
        return false;
      }
      // The exact NPC fixture creates one recruitable local companion only
      // before this run has companion history. Once recruited, the frozen
      // roster is the sole authority: an active companion is projected below,
      // while an inactive or dead companion must not be silently replaced by
      // another catalog roll. This is Development-only fixture behavior.
      R.bPresent = !bHasRosterHistory || ActivePartyCount > 0;
      R.TargetCount = bHasRosterHistory ? 0 : 1;
    }
#endif
    R.AttemptCount = R.TargetCount;
    for (const auto &E : SC->Catalog) {
      if (E.Rule == EEFCalystoCatalogRuleV4::Allow) {
        R.EligibleCatalogIds.AddUnique(E.StableId);
      }
    }
    for (const auto &E : TC->Catalog) {
      if (E.Rule == EEFCalystoCatalogRuleV4::Allow) {
        R.EligibleCatalogIds.AddUnique(E.StableId);
      }
    }
    R.EligibleCatalogIds.Sort(FNameLexicalLess());
#if !UE_BUILD_SHIPPING
    if (IsDevelopmentNPCVariantScenario(
            Context.DevelopmentPopulationScenario) &&
        C == EEFCalystoContentCategoryV4::NPC &&
        R.EligibleCatalogIds.Num() != 1) {
      OutError = TEXT("An exact V4 NPC variant fixture must expose one and only one eligible catalog ID.");
      OutIntent = {};
      return false;
    }
    if (IsDevelopmentCompanionRecallScenario(
            Context.DevelopmentPopulationScenario) &&
        C == EEFCalystoContentCategoryV4::NPC &&
        R.EligibleCatalogIds.Num() != 2) {
      OutError = TEXT(
          "CompanionRecallLifecycle must expose exactly the Generalist "
          "Female and Generalist Male NPC catalog IDs.");
      OutIntent = {};
      return false;
    }
#endif
    if (C != EEFCalystoContentCategoryV4::Enemy &&
        C != EEFCalystoContentCategoryV4::NPC &&
        C != EEFCalystoContentCategoryV4::Decoration &&
        C != EEFCalystoContentCategoryV4::Lighting) {
      OutIntent.ResourceBudget += R.TargetCount;
    }
    OutIntent.Categories.Add(MoveTemp(R));
  }

  TMap<FName, int32> VariantCounts;
  TArray<FEFCalystoCompanionRecordV4> ActiveParty;
  for (const FEFCalystoCompanionRecordV4 &Record :
       Context.CompanionSnapshot.Records) {
    if (Record.State == EEFCalystoCompanionRosterStateV4::ActiveParty) {
      ActiveParty.Add(Record);
    }
  }
  ActiveParty.Sort([](const auto &A, const auto &B) {
    return A.StableCompanionId.ToString(EGuidFormats::Digits) <
           B.StableCompanionId.ToString(EGuidFormats::Digits);
  });
  FEFCalystoResolvedCategoryV4 *NPCResult =
      OutIntent.Categories.FindByPredicate([](const auto &R) {
        return R.Category == EEFCalystoContentCategoryV4::NPC;
      });
  for (int32 Index = 0; Index < ActiveParty.Num(); ++Index) {
    const auto &Record = ActiveParty[Index];
    FEFCalystoSpawnInstanceDirectiveV4 D;
    D.StableInstanceId = FName(*FString::Printf(
        TEXT("V4.Party.%s"),
        *Record.StableCompanionId.ToString(EGuidFormats::Digits)));
    D.StableCompanionId = Record.StableCompanionId;
    D.CatalogId = Record.SourceCatalogId;
    D.VariantId = Record.SourceVariantId;
    D.Archetype = Record.Archetype;
    D.Gender = Record.Gender;
    D.Lifecycle = EEFCalystoLifecycleV4::Recruitable;
    D.Category = EEFCalystoContentCategoryV4::NPC;
    D.ActorClass = Record.ActorClass;
    D.Tier = Record.Grade;
    D.CategorySlotIndex = Index;
    const FEFCalystoResolvedCompanionLevelV4 *Level =
        OutIntent.ResolvedCompanionLevels.FindByPredicate(
            [&](const FEFCalystoResolvedCompanionLevelV4 &Candidate) {
              return Candidate.StableCompanionId == Record.StableCompanionId;
            });
    if (!Level) {
      OutError = TEXT("V4 active-party record has no frozen companion level.");
      return false;
    }
    D.LogicalLevel = Level->LogicalLevel;
    D.PhysicalACFLevel = Level->PhysicalACFLevel;
    OutIntent.SpawnDirectives.Add(MoveTemp(D));
    if (NPCResult) {
      ++NPCResult->DirectiveCount;
    }
  }
  const FEFCalystoResolvedCategoryV4 *ResolvedEnemyCategory =
      OutIntent.Categories.FindByPredicate([](const auto &Category) {
        return Category.Category == EEFCalystoContentCategoryV4::Enemy;
      });
  const float ResolvedDanger = FMath::Clamp(
      (ResolvedEnemyCategory ? ResolvedEnemyCategory->ResolvedInfluence : 0.0f) +
          Context.DirectorIntent.Danger,
      -1.0f, 1.0f);
  auto ResolveEnemyBundles =
      [&](FEFCalystoResolvedCategoryV4 &R,
          const FEFCalystoCategoryProfileV4 &StyleCategory,
          const FEFCalystoCategoryProfileV4 &ThemeCategory) {
        TArray<FEnemyBundleSlotV4> Slots;
        Slots.Reserve(R.TargetCount);
        for (int32 SlotIndex = 0; SlotIndex < R.TargetCount; ++SlotIndex) {
          FEnemyBundleSlotV4 Slot;
          Slot.CategorySlot = SlotIndex;
          Slot.StableId =
              (static_cast<uint64>(R.Category) + 1) * 100000ULL +
              static_cast<uint64>(SlotIndex);
          Slot.bMandatory = SlotIndex < R.MinimumWhenPresent;

          const bool bThemeSource =
              FEFCalystoDungeonDirectorMathV4::Bernoulli(
                  R.StyleThemeBlend, Context, PolicyHash,
                  EFCalystoDungeonDomainsV4::CatalogSource, Slot.StableId);
          const TArray<FEFCalystoCatalogEntryV4> &Source =
              bThemeSource ? ThemeCategory.Catalog : StyleCategory.Catalog;

          TArray<float> TierMasses = {
              R.ResolvedTiers.Common, R.ResolvedTiers.Uncommon,
              R.ResolvedTiers.Rare, R.ResolvedTiers.Epic,
              FMath::Clamp(R.WinterChance, 0.0f, .90f)};
          const float OriginalNormalMass =
              TierMasses[0] + TierMasses[1] + TierMasses[2] + TierMasses[3];
          const float TargetNormalMass =
              TierMasses[4] > 0.0f
                  ? FMath::Max(0.0f, .90f - TierMasses[4])
                  : OriginalNormalMass;
          if (OriginalNormalMass > UE_SMALL_NUMBER) {
            const float Scale = TargetNormalMass / OriginalNormalMass;
            for (int32 TierIndex = 0; TierIndex < 4; ++TierIndex) {
              TierMasses[TierIndex] =
                  FMath::Max(0.0f, TierMasses[TierIndex]) * Scale;
            }
          } else {
            for (int32 TierIndex = 0; TierIndex < 4; ++TierIndex) {
              TierMasses[TierIndex] = 0.0f;
            }
          }

          double AvailableMass = 0.0;
          for (int32 TierIndex = 0; TierIndex < TierMasses.Num(); ++TierIndex) {
            const float TierMass = TierMasses[TierIndex];
            if (TierMass <= 0.0f) {
              continue;
            }
            const EEFCalystoRarityTierV4 Tier =
                TierIndex == 4
                    ? EEFCalystoRarityTierV4::Winter
                    : static_cast<EEFCalystoRarityTierV4>(TierIndex);
            const FEFCalystoEnemyLevelBandV4 Band =
                FEFCalystoDungeonDirectorMathV4::ResolveEnemyLevelBand(
                    Tier, OutIntent.DirectorLevel,
                    OutIntent.LogicalWinterLevel);
            const uint64 LevelStable =
                Slot.StableId ^
                (0x9E3779B97F4A7C15ULL *
                 static_cast<uint64>(TierIndex + 1));
            const int32 LogicalLevel =
                FEFCalystoDungeonDirectorMathV4::SampleDiscretePERT(
                    Band.Min, Band.Mode, Band.Max, 4.0f, Volatility, Context,
                    PolicyHash, EFCalystoDungeonDomainsV4::EnemyBundle,
                    LevelStable, 2);

            TArray<float> EntryWeights;
            EntryWeights.Init(0.0f, Source.Num());
            double EntryWeightTotal = 0.0;
            for (int32 EntryIndex = 0; EntryIndex < Source.Num(); ++EntryIndex) {
              const FEFCalystoCatalogEntryV4 &Entry = Source[EntryIndex];
              const bool bEligible =
                  Entry.Rule == EEFCalystoCatalogRuleV4::Allow &&
                  SupportsTier(Entry, Tier) &&
                  Context.FloorNumber >= Entry.FirstEligibleFloor &&
                  !Context.CooldownBlockedCatalogIds.Contains(Entry.StableId) &&
                  VariantCounts.FindRef(Entry.StableId) < Entry.MaxPerVariant;
              const float ResolvedEntryWeight =
                  bEligible ? EntryWeight(Entry, Context.FloorNumber) : 0.0f;
              EntryWeights[EntryIndex] = ResolvedEntryWeight;
              EntryWeightTotal += static_cast<double>(ResolvedEntryWeight);
            }
            if (EntryWeightTotal <= 0.0) {
              // The authored tier mass becomes Nothing for this selected source;
              // there is deliberately no cross-profile or cross-tier fallback.
              continue;
            }
            AvailableMass += static_cast<double>(TierMass);
            for (int32 EntryIndex = 0; EntryIndex < Source.Num(); ++EntryIndex) {
              if (EntryWeights[EntryIndex] <= 0.0f) {
                continue;
              }
              const FEFCalystoCatalogEntryV4 &Entry = Source[EntryIndex];
              FEnemyBundleCandidateV4 Candidate;
              Candidate.Entry = &Entry;
              Candidate.Tier = Tier;
              Candidate.LogicalLevel = LogicalLevel;
              Candidate.EffectiveThreatCost =
                  Entry.BaseThreatCost *
                  FEFCalystoDungeonDirectorMathV4::LevelCostMultiplier(
                      LogicalLevel) /
                  FEFCalystoDungeonDirectorMathV4::LevelCostMultiplier(
                      static_cast<int32>(Context.FloorNumber));
              if (!FMath::IsFinite(Candidate.EffectiveThreatCost) ||
                  Candidate.EffectiveThreatCost <= 0.0f) {
                OutError = TEXT("V4 enemy bundle produced an invalid effective "
                                "threat cost.");
                return false;
              }
              Candidate.Weight =
                  static_cast<double>(TierMass) *
                  static_cast<double>(EntryWeights[EntryIndex]) /
                  EntryWeightTotal;
              Slot.Candidates.Add(MoveTemp(Candidate));
            }
          }

          float MaximumThreatCost = 0.0f;
          int32 MaximumLogicalLevel = 1;
          for (const FEnemyBundleCandidateV4 &Candidate : Slot.Candidates) {
            MaximumThreatCost =
                FMath::Max(MaximumThreatCost, Candidate.EffectiveThreatCost);
            MaximumLogicalLevel =
                FMath::Max(MaximumLogicalLevel, Candidate.LogicalLevel);
          }
          double TiltedWeightTotal = 0.0;
          for (FEnemyBundleCandidateV4 &Candidate : Slot.Candidates) {
            Candidate.Weight =
                FEFCalystoDungeonDirectorMathV4::WeightEnemyBundleForDanger(
                    Candidate.Weight, ResolvedDanger,
                    Candidate.EffectiveThreatCost, MaximumThreatCost,
                    Candidate.LogicalLevel, MaximumLogicalLevel);
            TiltedWeightTotal += Candidate.Weight;
          }
          if (TiltedWeightTotal > 0.0 && AvailableMass > 0.0) {
            const double PreserveSelectableMass =
                AvailableMass / TiltedWeightTotal;
            for (FEnemyBundleCandidateV4 &Candidate : Slot.Candidates) {
              Candidate.Weight *= PreserveSelectableMass;
              const uint64 CandidateStable =
                  Slot.StableId ^
                  FEFCalystoDungeonDirectorMathV4::StableNameId(
                      Candidate.Entry->StableId) ^
                  (0xD6E8FEB86659FD93ULL *
                   (static_cast<uint64>(Candidate.Tier) + 1ULL));
              const double Draw = FMath::Max(
                  FEFCalystoDungeonDirectorMathV4::Uniform01(
                      Context, PolicyHash,
                      EFCalystoDungeonDomainsV4::EnemyBudgetBacktrack,
                      CandidateStable),
                  1.0e-12);
              Candidate.Priority = -FMath::Loge(Draw) / Candidate.Weight;
            }
          }
          Slot.Candidates.Sort([](const FEnemyBundleCandidateV4 &A,
                                  const FEnemyBundleCandidateV4 &B) {
            if (A.Priority != B.Priority) {
              return A.Priority < B.Priority;
            }
            const FString AId = A.Entry ? A.Entry->StableId.ToString() : FString();
            const FString BId = B.Entry ? B.Entry->StableId.ToString() : FString();
            if (AId != BId) {
              return AId < BId;
            }
            if (A.Tier != B.Tier) {
              return static_cast<uint8>(A.Tier) < static_cast<uint8>(B.Tier);
            }
            return A.LogicalLevel < B.LogicalLevel;
          });

          Slot.bWantsSpawn =
              Slot.bMandatory ||
              (AvailableMass > 0.0 &&
               FEFCalystoDungeonDirectorMathV4::Uniform01(
                   Context, PolicyHash, EFCalystoDungeonDomainsV4::Tier,
                   Slot.StableId, 1) < AvailableMass);
          if (Slot.bMandatory && Slot.Candidates.IsEmpty()) {
            OutError = TEXT(
                "V4 enemy Minimum when Active has no eligible bundle in the "
                "selected Style/Theme catalog source.");
            return false;
          }
          Slots.Add(MoveTemp(Slot));
        }

        TArray<const FEnemyBundleSlotV4 *> AcceptedSlots;
        for (const FEnemyBundleSlotV4 &Slot : Slots) {
          if (Slot.bMandatory) {
            AcceptedSlots.Add(&Slot);
          }
        }
        TArray<int32> ChosenCandidates;
        float SolvedThreatCost = 0.0f;
        EEnemyBundleSolveResultV4 SolveResult = SolveEnemyBundlesExact(
            AcceptedSlots, OutIntent.ThreatBudget, VariantCounts,
            ChosenCandidates, SolvedThreatCost);
        if (SolveResult != EEnemyBundleSolveResultV4::Found) {
          OutError =
              SolveResult == EEnemyBundleSolveResultV4::NodeLimit
                  ? TEXT("V4 enemy mandatory bundle backtracking reached its "
                         "deterministic node limit; generation failed closed.")
                  : TEXT("V4 enemy Minimum when Active cannot satisfy its "
                         "resolved threat budget and variant caps.");
          return false;
        }

        // Mandatory slots use exact bounded backtracking. Optional slots are
        // intentionally allowed to become Nothing, so they use their existing
        // deterministic weighted order and accept the first candidate that
        // still satisfies the resolved budget and per-variant cap. Re-solving
        // the complete mandatory prefix for every optional attempt would be
        // quadratic in slots and potentially exponential in candidate count.
        TMap<FName, int32> AcceptedVariantCounts = VariantCounts;
        for (int32 Index = 0; Index < AcceptedSlots.Num(); ++Index) {
          const FEnemyBundleSlotV4 &AcceptedSlot = *AcceptedSlots[Index];
          if (AcceptedSlot.Candidates.IsValidIndex(ChosenCandidates[Index]) &&
              AcceptedSlot.Candidates[ChosenCandidates[Index]].Entry) {
            ++AcceptedVariantCounts.FindOrAdd(
                AcceptedSlot.Candidates[ChosenCandidates[Index]].Entry->StableId);
          }
        }
        for (const FEnemyBundleSlotV4 &Slot : Slots) {
          if (Slot.bMandatory || !Slot.bWantsSpawn ||
              Slot.Candidates.IsEmpty()) {
            continue;
          }
          for (int32 CandidateIndex = 0;
               CandidateIndex < Slot.Candidates.Num(); ++CandidateIndex) {
            const FEnemyBundleCandidateV4 &Candidate =
                Slot.Candidates[CandidateIndex];
            if (!Candidate.Entry ||
                AcceptedVariantCounts.FindRef(Candidate.Entry->StableId) >=
                    Candidate.Entry->MaxPerVariant ||
                SolvedThreatCost + Candidate.EffectiveThreatCost >
                    OutIntent.ThreatBudget + .0001f) {
              continue;
            }
            AcceptedSlots.Add(&Slot);
            ChosenCandidates.Add(CandidateIndex);
            SolvedThreatCost += Candidate.EffectiveThreatCost;
            ++AcceptedVariantCounts.FindOrAdd(Candidate.Entry->StableId);
            break;
          }
        }

        if (R.bPresent &&
            (AcceptedSlots.Num() < R.MinimumWhenPresent ||
             ChosenCandidates.Num() != AcceptedSlots.Num())) {
          OutError = TEXT("V4 enemy Minimum when Active was not fully resolved; "
                          "generation failed closed.");
          return false;
        }
        for (int32 Index = 0; Index < AcceptedSlots.Num(); ++Index) {
          const FEnemyBundleSlotV4 &Slot = *AcceptedSlots[Index];
          if (!Slot.Candidates.IsValidIndex(ChosenCandidates[Index])) {
            OutError = TEXT("V4 enemy backtracking returned an invalid bundle "
                            "selection.");
            return false;
          }
          const FEnemyBundleCandidateV4 &Candidate =
              Slot.Candidates[ChosenCandidates[Index]];
          const FEFCalystoCatalogEntryV4 &Entry = *Candidate.Entry;
          ++VariantCounts.FindOrAdd(Entry.StableId);
          FEFCalystoSpawnInstanceDirectiveV4 Directive;
          Directive.StableInstanceId =
              MakeStableInstanceId(R.Category, Slot.CategorySlot);
          Directive.CatalogId = Entry.StableId;
          Directive.VariantId = Entry.StableId;
          Directive.Archetype = Entry.Archetype;
          Directive.Gender = Entry.Gender;
          Directive.Lifecycle = Entry.Lifecycle;
          Directive.Category = R.Category;
          Directive.ActorClass = Entry.ActorClass;
          Directive.Tier = Candidate.Tier;
          Directive.LogicalLevel = Candidate.LogicalLevel;
          Directive.PhysicalACFLevel =
              FMath::Min(Candidate.LogicalLevel, 100);
          Directive.CategorySlotIndex = Slot.CategorySlot;
          Directive.CooldownFloors = Entry.CooldownFloors;
          Directive.EffectiveThreatCost = Candidate.EffectiveThreatCost;
          OutIntent.PlannedThreatCost += Directive.EffectiveThreatCost;
          OutIntent.SpawnDirectives.Add(MoveTemp(Directive));
          ++R.DirectiveCount;
        }
        if (!FMath::IsNearlyEqual(OutIntent.PlannedThreatCost,
                                  SolvedThreatCost, .001f) ||
            OutIntent.PlannedThreatCost > OutIntent.ThreatBudget + .0001f) {
          OutError = TEXT("V4 enemy backtracking threat-cost accounting did "
                          "not match its solved bundle set.");
          return false;
        }
        return true;
      };
  for (FEFCalystoResolvedCategoryV4 &R : OutIntent.Categories) {
    if (R.Category == EEFCalystoContentCategoryV4::Decoration ||
        R.Category == EEFCalystoContentCategoryV4::Lighting) {
      continue;
    }
    const auto *SC = Policy->FindCategory(S->Categories, R.Category);
    const auto *TC = Policy->FindCategory(T->Categories, R.Category);
    if (!SC || !TC) {
      continue;
    }
    if (R.Category == EEFCalystoContentCategoryV4::Enemy) {
      if (!ResolveEnemyBundles(R, *SC, *TC)) {
        OutIntent = {};
        return false;
      }
      continue;
    }
    for (int32 Slot = 0; Slot < R.TargetCount; ++Slot) {
      const uint64 Stable = (static_cast<uint64>(R.Category) + 1) * 100000ULL +
                            static_cast<uint64>(Slot);
      const bool bThemeSource = FEFCalystoDungeonDirectorMathV4::Bernoulli(
          R.StyleThemeBlend, Context, PolicyHash,
          EFCalystoDungeonDomainsV4::CatalogSource, Stable);
      const TArray<FEFCalystoCatalogEntryV4> &Source =
          bThemeSource ? TC->Catalog : SC->Catalog;
#if !UE_BUILD_SHIPPING
      const FName RequiredRecallLifecycleNPC =
          IsDevelopmentCompanionRecallScenario(
              Context.DevelopmentPopulationScenario) &&
                  R.Category == EEFCalystoContentCategoryV4::NPC
              ? (Context.FloorNumber == 1
                     ? FName(TEXT("NPC.Companion.Generalist.Female"))
                     : FName(TEXT("NPC.Companion.Generalist.Male")))
              : NAME_None;
#endif
      auto HasEligibleTier = [&](const EEFCalystoRarityTierV4 CandidateTier) {
        return Source.ContainsByPredicate(
            [&](const FEFCalystoCatalogEntryV4 &E) {
              bool bEligible =
                  E.Rule == EEFCalystoCatalogRuleV4::Allow &&
                  SupportsTier(E, CandidateTier) &&
                  Context.FloorNumber >= E.FirstEligibleFloor &&
                  !Context.CooldownBlockedCatalogIds.Contains(E.StableId) &&
                  VariantCounts.FindRef(E.StableId) < E.MaxPerVariant;
#if !UE_BUILD_SHIPPING
              bEligible = bEligible &&
                          (RequiredRecallLifecycleNPC.IsNone() ||
                           E.StableId == RequiredRecallLifecycleNPC);
#endif
              return bEligible;
            });
      };
      const bool bMandatory = Slot < R.MinimumWhenPresent;
      EEFCalystoRarityTierV4 Tier = EEFCalystoRarityTierV4::Common;
      TArray<float> TWs = {R.ResolvedTiers.Common, R.ResolvedTiers.Uncommon,
                           R.ResolvedTiers.Rare, R.ResolvedTiers.Epic};
      if (bMandatory) {
        TArray<float> MandatoryWeights;
        for (int32 TierIndex = 0; TierIndex < 4; ++TierIndex) {
          const auto CandidateTier =
              static_cast<EEFCalystoRarityTierV4>(TierIndex);
          MandatoryWeights.Add(HasEligibleTier(CandidateTier)
                                   ? TWs[TierIndex]
                                   : 0.0f);
        }
        const int32 ChosenTier = SelectWeighted(
            MandatoryWeights, FEFCalystoDungeonDirectorMathV4::Uniform01(
                                  Context, PolicyHash,
                                  EFCalystoDungeonDomainsV4::Tier, Stable, 1));
        if (ChosenTier == INDEX_NONE) {
          continue;
        }
        Tier = static_cast<EEFCalystoRarityTierV4>(ChosenTier);
      } else {
        // The draw spans the complete probability interval. Authored Nothing
        // and locked/ineligible tier mass remain empty; they are not rerouted.
        double Cursor = FEFCalystoDungeonDirectorMathV4::Uniform01(
            Context, PolicyHash, EFCalystoDungeonDomainsV4::Tier, Stable, 1);
        int32 ChosenTier = INDEX_NONE;
        for (int32 TierIndex = 0; TierIndex < TWs.Num(); ++TierIndex) {
          if (Cursor < static_cast<double>(TWs[TierIndex])) {
            ChosenTier = TierIndex;
            break;
          }
          Cursor -= static_cast<double>(TWs[TierIndex]);
        }
        if (ChosenTier == INDEX_NONE) {
          continue;
        }
        Tier = static_cast<EEFCalystoRarityTierV4>(ChosenTier);
        if (!HasEligibleTier(Tier)) {
          continue;
        }
      }
      const FEFCalystoEnemyLevelBandV4 Band =
          FEFCalystoDungeonDirectorMathV4::ResolveEnemyLevelBand(
              Tier, OutIntent.DirectorLevel, OutIntent.LogicalWinterLevel);
      const int32 Logical =
          R.Category == EEFCalystoContentCategoryV4::NPC
              ? FEFCalystoDungeonDirectorMathV4::SampleDiscretePERT(
                    Band.Min, Band.Mode, Band.Max, 4.0f, Volatility, Context,
                    PolicyHash, EFCalystoDungeonDomainsV4::NPC, Stable, 2)
              : 0;
      TArray<float> Weights;
      for (const FEFCalystoCatalogEntryV4 &E : Source) {
        float W =
            E.Rule == EEFCalystoCatalogRuleV4::Allow && SupportsTier(E, Tier) &&
                    Context.FloorNumber >= E.FirstEligibleFloor &&
                    !Context.CooldownBlockedCatalogIds.Contains(E.StableId) &&
                    VariantCounts.FindRef(E.StableId) < E.MaxPerVariant
                ? EntryWeight(E, Context.FloorNumber)
                : 0.0f;
#if !UE_BUILD_SHIPPING
        if (!RequiredRecallLifecycleNPC.IsNone() &&
            E.StableId != RequiredRecallLifecycleNPC) {
          W = 0.0f;
        }
#endif
        Weights.Add(W);
      }
      const uint64 Domain =
          R.Category == EEFCalystoContentCategoryV4::NPC
              ? EFCalystoDungeonDomainsV4::NPC
              : EFCalystoDungeonDomainsV4::CatalogEntry;
      const int32 EI =
          SelectWeighted(Weights, FEFCalystoDungeonDirectorMathV4::Uniform01(
                                      Context, PolicyHash, Domain, Stable));
      if (!Source.IsValidIndex(EI)) {
        continue;
      }
      const FEFCalystoCatalogEntryV4 &E = Source[EI];
      ++VariantCounts.FindOrAdd(E.StableId);
      const int32 ActualSlot = R.Category == EEFCalystoContentCategoryV4::NPC
                                   ? Slot + ActivePartyCount
                                   : Slot;
      FEFCalystoSpawnInstanceDirectiveV4 D;
      D.StableInstanceId = MakeStableInstanceId(R.Category, ActualSlot);
      D.CatalogId = E.StableId;
      D.VariantId = E.StableId;
      D.Archetype = E.Archetype;
      D.Gender = E.Gender;
      D.Lifecycle = E.Lifecycle;
      D.Category = R.Category;
      D.ActorClass = E.ActorClass;
      D.Tier = Tier;
      D.LogicalLevel = Logical;
      D.PhysicalACFLevel = Logical > 0 ? FMath::Min(Logical, 100) : 0;
      D.CategorySlotIndex = ActualSlot;
      D.CooldownFloors = E.CooldownFloors;
      if (R.Category != EEFCalystoContentCategoryV4::NPC) {
        OutIntent.PlannedResourceCost += 1.0f;
      }
      OutIntent.SpawnDirectives.Add(MoveTemp(D));
      ++R.DirectiveCount;
    }
  }
  for (const FEFCalystoResolvedCategoryV4 &R : OutIntent.Categories) {
    if (R.bPresent && R.DirectiveCount < R.MinimumWhenPresent) {
      OutError = FString::Printf(
          TEXT("V4 category %d Minimum when Active resolved %d of %d required "
               "directives; generation failed closed."),
          static_cast<int32>(R.Category), R.DirectiveCount,
          R.MinimumWhenPresent);
      OutIntent = {};
      return false;
    }
  }
  TMap<FName, int32> ContentCounts;
  for (FEFCalystoSpawnInstanceDirectiveV4 &Chest : OutIntent.SpawnDirectives) {
    if (Chest.Category != EEFCalystoContentCategoryV4::Chest) {
      continue;
    }
    const auto *SC =
        Policy->FindCategory(S->Categories, EEFCalystoContentCategoryV4::Chest);
    const auto *TC =
        Policy->FindCategory(T->Categories, EEFCalystoContentCategoryV4::Chest);
    if (!SC || !TC) {
      continue;
    }
    const int32 MinAttempts = FMath::Max(SC->MinimumChestContentAttempts,
                                         TC->MinimumChestContentAttempts);
    const int32 MaxAttempts = FMath::Min(SC->MaximumChestContentAttempts,
                                         TC->MaximumChestContentAttempts);
    const uint64 Stable =
        FEFCalystoDungeonDirectorMathV4::StableNameId(Chest.StableInstanceId);
    const FEFCalystoResolvedCategoryV4 *ChestResult =
        OutIntent.Categories.FindByPredicate([](const auto &R) {
          return R.Category == EEFCalystoContentCategoryV4::Chest;
        });
    if (!ChestResult) {
      continue;
    }
#if !UE_BUILD_SHIPPING
    if (IsDevelopmentCompanionRecallScenario(
            Context.DevelopmentPopulationScenario)) {
      if (Context.FloorNumber != 2 || !bHasConfirmedDeadCompanion ||
          Context.CompanionSnapshot.bPlayerOwnsWintersRecall ||
          MinAttempts != 1 || MaxAttempts != 1) {
        OutError = TEXT(
            "CompanionRecallLifecycle reached a chest without its exact "
            "confirmed-dead, ownership and one-attempt contract.");
        OutIntent = {};
        return false;
      }
      Chest.ChestContentAttemptCount = 1;
      const bool bThemeSource = FEFCalystoDungeonDirectorMathV4::Bernoulli(
          ChestResult->StyleThemeBlend, Context, PolicyHash,
          EFCalystoDungeonDomainsV4::CatalogSource, Stable, 1);
      const TArray<FEFCalystoChestContentEntryV4> &Source =
          bThemeSource ? TC->ChestContentsCatalog : SC->ChestContentsCatalog;
      const FName RecallId(TEXT("Item.CompanionRevival.WintersRecall"));
      const FEFCalystoChestContentEntryV4 *Recall = nullptr;
      int32 RecallMatches = 0;
      for (const FEFCalystoChestContentEntryV4 &Entry : Source) {
        if (Entry.StableId == RecallId) {
          Recall = &Entry;
          ++RecallMatches;
        }
      }
      if (RecallMatches != 1 || !Recall ||
          Recall->Rule != EEFCalystoCatalogRuleV4::Allow ||
          Recall->Tier != EEFCalystoRarityTierV4::Epic ||
          Recall->ContentClass.IsNull() ||
          Context.FloorNumber < Recall->FirstEligibleFloor ||
          Context.CooldownBlockedCatalogIds.Contains(Recall->StableId) ||
          ContentCounts.FindRef(Recall->StableId) >= Recall->MaxPerFloor ||
          Recall->MaxPerFloor != 1 || Recall->CooldownFloors != 8 ||
          !Recall->bRequiresGraveyardEligibility) {
        OutError = TEXT(
            "CompanionRecallLifecycle selected catalog source has no single "
            "eligible Epic Winter's Recall; no cross-profile fallback is "
            "permitted.");
        OutIntent = {};
        return false;
      }
      ++ContentCounts.FindOrAdd(Recall->StableId);
      FEFCalystoChestContentDirectiveV4 Directive;
      Directive.ContainerInstanceId = Chest.StableInstanceId;
      Directive.StableAttemptId = FName(*FString::Printf(
          TEXT("%s.Content.0"), *Chest.StableInstanceId.ToString()));
      Directive.ContentCatalogId = Recall->StableId;
      Directive.ContentClass = Recall->ContentClass;
      Directive.Tier = Recall->Tier;
      Directive.CooldownFloors = Recall->CooldownFloors;
      OutIntent.ChestContentDirectives.Add(MoveTemp(Directive));
      continue;
    }
#endif
    Chest.ChestContentAttemptCount =
        FEFCalystoDungeonDirectorMathV4::SampleCount(
            MinAttempts, MaxAttempts, ChestResult->OpportunityChance, Context,
            PolicyHash, Stable);
    for (int32 Attempt = 0; Attempt < Chest.ChestContentAttemptCount;
         ++Attempt) {
      TArray<float> TierWeights = {ChestResult->ResolvedTiers.Common,
                                   ChestResult->ResolvedTiers.Uncommon,
                                   ChestResult->ResolvedTiers.Rare,
                                   ChestResult->ResolvedTiers.Epic};
      const double TierGate = FEFCalystoDungeonDirectorMathV4::Uniform01(
          Context, PolicyHash, EFCalystoDungeonDomainsV4::Tier, Stable,
          Attempt * 3 + 10);
      if (TierGate >= ChestResult->ResolvedTiers.GetSelectableMass()) {
        continue;
      }
      const int32 TierIndex = SelectWeighted(
          TierWeights, FEFCalystoDungeonDirectorMathV4::Uniform01(
                           Context, PolicyHash, EFCalystoDungeonDomainsV4::Tier,
                           Stable, Attempt * 3 + 11));
      if (TierIndex == INDEX_NONE) {
        continue;
      }
      const EEFCalystoRarityTierV4 ContentTier =
          static_cast<EEFCalystoRarityTierV4>(TierIndex);
      const bool bThemeSource = FEFCalystoDungeonDirectorMathV4::Bernoulli(
          ChestResult->StyleThemeBlend, Context, PolicyHash,
          EFCalystoDungeonDomainsV4::CatalogSource, Stable, Attempt + 1);
      const TArray<FEFCalystoChestContentEntryV4> &Source =
          bThemeSource ? TC->ChestContentsCatalog : SC->ChestContentsCatalog;
      TArray<const FEFCalystoChestContentEntryV4 *> Entries;
      TArray<float> Weights;
      for (const auto &E : Source) {
        if (E.Rule != EEFCalystoCatalogRuleV4::Allow || E.Tier != ContentTier ||
            Context.FloorNumber < E.FirstEligibleFloor ||
            Context.CooldownBlockedCatalogIds.Contains(E.StableId) ||
            ContentCounts.FindRef(E.StableId) >= E.MaxPerFloor ||
            (E.bRequiresGraveyardEligibility &&
             (!bHasConfirmedDeadCompanion ||
              Context.CompanionSnapshot.bPlayerOwnsWintersRecall))) {
          continue;
        }
        const float Ramp =
            E.RampFloors <= 0
                ? 1.0f
                : FMath::Clamp(static_cast<float>(Context.FloorNumber -
                                                  E.FirstEligibleFloor) /
                                   E.RampFloors,
                               0.0f, 1.0f);
        Entries.Add(&E);
        Weights.Add(E.DeepShare * FMath::Lerp(E.InitialFraction, 1.0f,
                                              Ramp * Ramp * (3 - 2 * Ramp)));
      }
      const int32 EI =
          SelectWeighted(Weights, FEFCalystoDungeonDirectorMathV4::Uniform01(
                                      Context, PolicyHash,
                                      EFCalystoDungeonDomainsV4::ChestContents,
                                      Stable, Attempt + 1));
      if (!Entries.IsValidIndex(EI)) {
        continue;
      }
      const auto &E = *Entries[EI];
      ++ContentCounts.FindOrAdd(E.StableId);
      FEFCalystoChestContentDirectiveV4 D;
      D.ContainerInstanceId = Chest.StableInstanceId;
      D.StableAttemptId = FName(*FString::Printf(
          TEXT("%s.Content.%d"), *Chest.StableInstanceId.ToString(), Attempt));
      D.ContentCatalogId = E.StableId;
      D.ContentClass = E.ContentClass;
      D.Tier = E.Tier;
      D.CooldownFloors = E.CooldownFloors;
      OutIntent.ChestContentDirectives.Add(MoveTemp(D));
    }
  }
  for (FEFCalystoResolvedCategoryV4 &R : OutIntent.Categories) {
    R.TargetCount = R.DirectiveCount;
    if (R.TargetCount > R.MaximumPerFloor) {
      OutError = TEXT("V4 realized directive count exceeds its resolved "
                      "Style/Theme category cap.");
      OutIntent = {};
      return false;
    }
  }
#if !UE_BUILD_SHIPPING
  if (!Context.DevelopmentPopulationScenario.IsNone()) {
    auto DirectiveCountFor = [&OutIntent](
                                 const EEFCalystoContentCategoryV4 Category) {
      int32 Count = 0;
      for (const FEFCalystoSpawnInstanceDirectiveV4 &Directive :
           OutIntent.SpawnDirectives) {
        Count += Directive.Category == Category ? 1 : 0;
      }
      return Count;
    };
    bool bExactFixtureSatisfied = true;
    FString FixtureDiagnostic;
    if (Context.DevelopmentPopulationScenario == FName(TEXT("EnemyCap25"))) {
      bExactFixtureSatisfied = DirectiveCountFor(
                                   EEFCalystoContentCategoryV4::Enemy) == 25;
      FixtureDiagnostic = TEXT("EnemyCap25 did not resolve exactly 25 enemy directives.");
    } else if (Context.DevelopmentPopulationScenario ==
               FName(TEXT("ResourceMax"))) {
      bExactFixtureSatisfied =
          DirectiveCountFor(EEFCalystoContentCategoryV4::Food) == 30 &&
          DirectiveCountFor(EEFCalystoContentCategoryV4::Chest) == 10;
      FixtureDiagnostic = TEXT("ResourceMax did not resolve exactly 30 Food and 10 Chest directives.");
    } else if (Context.DevelopmentPopulationScenario ==
                   FName(TEXT("NPCTotal4"))) {
      int32 ValidLocalRecruitableNPCs = 0;
      for (const FEFCalystoSpawnInstanceDirectiveV4 &Directive :
           OutIntent.SpawnDirectives) {
        if (Directive.Category == EEFCalystoContentCategoryV4::NPC &&
            Directive.Lifecycle == EEFCalystoLifecycleV4::Recruitable &&
            !Directive.StableCompanionId.IsValid() &&
            !Directive.ActorClass.IsNull() && Directive.LogicalLevel >= 1 &&
            Directive.PhysicalACFLevel ==
                FMath::Min(Directive.LogicalLevel, 100)) {
          ++ValidLocalRecruitableNPCs;
        }
      }
      bExactFixtureSatisfied =
          OutIntent.SpawnDirectives.Num() == 4 &&
          DirectiveCountFor(EEFCalystoContentCategoryV4::NPC) == 4 &&
          ValidLocalRecruitableNPCs == 4;
      FixtureDiagnostic = TEXT(
          "NPCTotal4 did not resolve exactly four valid local Recruitable "
          "NPC directives inside the total hard cap.");
    } else if (Context.DevelopmentPopulationScenario ==
                   FName(TEXT("SpecialEvents6"))) {
      int32 ValidProbeEvents = 0;
      const FSoftObjectPath ProbeClassPath(
          TEXT("/Script/EFProceduralPCGRuntime.EFCalystoSpecialEventProbe"));
      for (const FEFCalystoSpawnInstanceDirectiveV4 &Directive :
           OutIntent.SpawnDirectives) {
        if (Directive.Category == EEFCalystoContentCategoryV4::SpecialEvent &&
            Directive.CatalogId ==
                FName(TEXT("Event.Automation.SpecialEventProbe")) &&
            Directive.ActorClass.ToSoftObjectPath() == ProbeClassPath &&
            Directive.Tier == EEFCalystoRarityTierV4::Common &&
            Directive.Lifecycle == EEFCalystoLifecycleV4::FloorLocal) {
          ++ValidProbeEvents;
        }
      }
      bExactFixtureSatisfied =
          OutIntent.SpawnDirectives.Num() == 6 &&
          DirectiveCountFor(EEFCalystoContentCategoryV4::SpecialEvent) == 6 &&
          ValidProbeEvents == 6;
      FixtureDiagnostic = TEXT(
          "SpecialEvents6 did not resolve exactly six project-owned Common "
          "probe directives and no other actors.");
    } else if (IsDevelopmentCompanionRecallScenario(
                   Context.DevelopmentPopulationScenario)) {
      const FName ExpectedNPC = Context.FloorNumber == 1
                                    ? FName(TEXT("NPC.Companion.Generalist.Female"))
                                    : FName(TEXT("NPC.Companion.Generalist.Male"));
      int32 ExpectedNPCCount = 0;
      int32 OtherActorCount = 0;
      FName ChestInstanceId = NAME_None;
      for (const FEFCalystoSpawnInstanceDirectiveV4 &Directive :
           OutIntent.SpawnDirectives) {
        if (Directive.Category == EEFCalystoContentCategoryV4::NPC &&
            Directive.CatalogId == ExpectedNPC &&
            !Directive.StableCompanionId.IsValid() &&
            Directive.Lifecycle == EEFCalystoLifecycleV4::Recruitable) {
          ++ExpectedNPCCount;
        } else if (Context.FloorNumber == 2 &&
                   Directive.Category ==
                       EEFCalystoContentCategoryV4::Chest &&
                   ChestInstanceId.IsNone()) {
          ChestInstanceId = Directive.StableInstanceId;
        } else {
          ++OtherActorCount;
        }
      }
      const bool bFloorOne =
          Context.FloorNumber == 1 && ExpectedNPCCount == 1 &&
          ChestInstanceId.IsNone() && OtherActorCount == 0 &&
          OutIntent.SpawnDirectives.Num() == 1 &&
          OutIntent.ChestContentDirectives.IsEmpty();
      bool bExactRecall = false;
      if (Context.FloorNumber == 2 &&
          OutIntent.ChestContentDirectives.Num() == 1) {
        const FEFCalystoChestContentDirectiveV4 &Content =
            OutIntent.ChestContentDirectives[0];
        bExactRecall =
            !ChestInstanceId.IsNone() &&
            Content.ContainerInstanceId == ChestInstanceId &&
            Content.ContentCatalogId ==
                FName(TEXT("Item.CompanionRevival.WintersRecall")) &&
            Content.Tier == EEFCalystoRarityTierV4::Epic &&
            !Content.ContentClass.IsNull() && Content.CooldownFloors == 8;
      }
      const bool bFloorTwo =
          Context.FloorNumber == 2 && ExpectedNPCCount == 1 &&
          !ChestInstanceId.IsNone() && OtherActorCount == 0 &&
          OutIntent.SpawnDirectives.Num() == 2 && bExactRecall;
      bExactFixtureSatisfied = bFloorOne || bFloorTwo;
      FixtureDiagnostic = TEXT(
          "CompanionRecallLifecycle violated its exact per-floor NPC, chest "
          "or Winter's Recall postcondition.");
    } else if (IsDevelopmentNPCVariantScenario(
                   Context.DevelopmentPopulationScenario)) {
      const int32 ExpectedNPCDirectives =
          bHasRosterHistory ? ActivePartyCount : 1;
      bExactFixtureSatisfied =
          DirectiveCountFor(EEFCalystoContentCategoryV4::NPC) ==
          ExpectedNPCDirectives;
      FixtureDiagnostic = FString::Printf(
          TEXT("The selected NPC variant fixture resolved an unexpected NPC "
               "directive count (expected %d)."),
          ExpectedNPCDirectives);
    }
    if (!bExactFixtureSatisfied) {
      OutError = MoveTemp(FixtureDiagnostic);
      OutIntent = {};
      return false;
    }
  }
#endif
  for (const FEFCalystoSpawnInstanceDirectiveV4 &Directive :
       OutIntent.SpawnDirectives) {
    if (!DisabledGenerationCategories.Contains(Directive.Category)) {
      continue;
    }
    const bool bFrozenActiveCompanion =
        Directive.Category == EEFCalystoContentCategoryV4::NPC &&
        Directive.StableCompanionId.IsValid();
    if (!bFrozenActiveCompanion) {
      OutError = FString::Printf(
          TEXT("V4 disabled category %d produced a spawn directive; generation "
               "failed closed."),
          static_cast<int32>(Directive.Category));
      OutIntent = {};
      return false;
    }
  }
  int32 PlannedActors = OutIntent.SpawnDirectives.Num();
  int32 PlannedNPCs = 0;
  for (const FEFCalystoSpawnInstanceDirectiveV4 &Directive :
       OutIntent.SpawnDirectives) {
    PlannedNPCs +=
        Directive.Category == EEFCalystoContentCategoryV4::NPC ? 1 : 0;
  }
  if (PlannedNPCs > Policy->SafetyCeilings.MaximumNPCs) {
    OutError = TEXT("V4 resolved intent exceeds the total NPC cap, including "
                    "active party.");
    OutIntent = {};
    return false;
  }
  if (PlannedActors > Policy->SafetyCeilings.MaximumDirectorActors) {
    OutError = TEXT("V4 resolved intent exceeds the immutable actor ceiling.");
    OutIntent = {};
    return false;
  }
  // The first hash freezes every resolved decision while retaining the pure Shape
  // domain seed. Calysto then receives a deterministic qualification pass over
  // that frozen document. Store the qualified value back in FloorIntent before
  // producing the authoritative hash so runtime, replay, manifests and telemetry
  // all refer to the exact seed consumed by the vendor PCG graph.
  const FString ProvisionalIntentHash = BuildIntentHash(OutIntent);
  OutIntent.PCGSeed = BuildCalystoQualifiedPCGSeed(OutIntent, ProvisionalIntentHash);
  OutIntent.IntentHash = BuildIntentHash(OutIntent);
  OutIntent.bIsValid = !OutIntent.IntentHash.IsEmpty();
  if (OutIntent.PCGSeed <= 0 || !OutIntent.bIsValid) {
    OutError = TEXT("V4 canonical intent hashing failed.");
    return false;
  }
  return true;
}
