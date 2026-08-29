#include "Calysto/EFCalystoDungeonTypesV4.h"

namespace {
constexpr double MinimumNothingMass = 0.10;
}

float FEFCalystoTierMixV4::GetSelectableMass() const {
  return static_cast<float>(static_cast<double>(Common) +
                            static_cast<double>(Uncommon) +
                            static_cast<double>(Rare) +
                            static_cast<double>(Epic));
}

float FEFCalystoTierMixV4::GetCalculatedNothing() const {
  const double SelectableMass = static_cast<double>(Common) +
                                static_cast<double>(Uncommon) +
                                static_cast<double>(Rare) +
                                static_cast<double>(Epic);
  if (!FMath::IsFinite(SelectableMass)) {
    return 0.0f;
  }

  // Valid authored tier mixes reserve a permanent ten-percent Nothing result.
  // Keep the derived value canonical at the boundary instead of leaking the
  // accumulated IEEE-754 error from four independently-authored floats.
  return static_cast<float>(
      FMath::Clamp(1.0 - SelectableMass, MinimumNothingMass, 1.0));
}

float FEFCalystoTierMixV4::Get(const EEFCalystoRarityTierV4 Tier) const {
  switch (Tier) {
  case EEFCalystoRarityTierV4::Common:
    return Common;
  case EEFCalystoRarityTierV4::Uncommon:
    return Uncommon;
  case EEFCalystoRarityTierV4::Rare:
    return Rare;
  case EEFCalystoRarityTierV4::Epic:
    return Epic;
  case EEFCalystoRarityTierV4::Winter:
    return 0.0f;
  default:
    return 0.0f;
  }
}

void FEFCalystoTierMixV4::Set(const EEFCalystoRarityTierV4 Tier,
                              const float Value) {
  switch (Tier) {
  case EEFCalystoRarityTierV4::Common:
    Common = Value;
    break;
  case EEFCalystoRarityTierV4::Uncommon:
    Uncommon = Value;
    break;
  case EEFCalystoRarityTierV4::Rare:
    Rare = Value;
    break;
  case EEFCalystoRarityTierV4::Epic:
    Epic = Value;
    break;
  case EEFCalystoRarityTierV4::Winter:
    break;
  default:
    break;
  }
}

void FEFCalystoTierMixV4::RefreshNothing() {
  Nothing = GetCalculatedNothing();
}

void FEFCalystoTierCurveV4::RefreshNothing() {
  AtFloor1.RefreshNothing();
  AtFloor100.RefreshNothing();
}

void FEFCalystoCategoryProfileV4::RefreshNothing() { Tiers.RefreshNothing(); }

void FEFCalystoStyleProfileV4::RefreshNothing() {
  for (FEFCalystoCategoryProfileV4 &Category : Categories) {
    Category.RefreshNothing();
  }
}

FEFCalystoContextTraitsV4 FEFCalystoStyleProfileV4::GetAuthoredTraits() const {
  FEFCalystoContextTraitsV4 Result;
  Result.Mystery = Mystery;
  Result.Danger = Danger;
  Result.Safe = Safe;
  Result.Abundance = Abundance;
  Result.ClothingInfluence = ClothingInfluence;
  Result.Volatility = Volatility;
  return Result;
}

void FEFCalystoThemeProfileV4::RefreshNothing() {
  for (FEFCalystoCategoryProfileV4 &Category : Categories) {
    Category.RefreshNothing();
  }
}

FEFCalystoContextTraitsV4 FEFCalystoThemeProfileV4::GetAuthoredTraits() const {
  FEFCalystoContextTraitsV4 Result;
  Result.Mystery = Mystery;
  Result.Danger = Danger;
  Result.Safe = Safe;
  Result.Abundance = Abundance;
  Result.ClothingInfluence = ClothingInfluence;
  Result.Volatility = Volatility;
  return Result;
}
