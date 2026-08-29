#include "Calysto/EFCalystoDungeonDirectorPolicyV4.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/BlueprintSupport.h"
#include "Calysto/EFCalystoDungeonDirectorMathV4.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoPolicyV4, Log, All);

namespace EFCalystoPolicyV4Private {
static constexpr float ProbabilityTolerance = 0.0001f;
static const FName WintersRecallStableId(
    TEXT("Item.CompanionRevival.WintersRecall"));
static const FSoftObjectPath WintersRecallClassPath(
    TEXT("/Game/_Game/Items/Companions/"
         "BP_Item_WintersRecall.BP_Item_WintersRecall_C"));

static bool IsFiniteUnit(const float Value) {
  return FMath::IsFinite(Value) && Value >= 0.0f && Value <= 1.0f;
}

static bool IsFiniteBias(const float Value) {
  return FMath::IsFinite(Value) && Value >= -1.0f && Value <= 1.0f;
}

static FString FloatBits(const float Value) {
  uint32 Bits = 0;
  FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
  return FString::Printf(TEXT("%08X"), Bits);
}

static FEFCalystoTierMixV4 MakeTierMix(const float Common, const float Uncommon,
                                       const float Rare, const float Epic) {
  FEFCalystoTierMixV4 Result;
  Result.Common = Common;
  Result.Uncommon = Uncommon;
  Result.Rare = Rare;
  Result.Epic = Epic;
  Result.RefreshNothing();
  return Result;
}

static FEFCalystoTierCurveV4
MakeTierCurve(const FEFCalystoTierMixV4 &Floor1,
              const FEFCalystoTierMixV4 &Floor100) {
  FEFCalystoTierCurveV4 Result;
  Result.AtFloor1 = Floor1;
  Result.AtFloor100 = Floor100;
  Result.RefreshNothing();
  return Result;
}

static FEFCalystoChanceCurveV4
MakeChance(const float Floor1, const float Floor100, const float Tau) {
  FEFCalystoChanceCurveV4 Result;
  Result.ChanceAtFloor1 = Floor1;
  Result.ChanceAtFloor100 = Floor100;
  Result.Tau = Tau;
  return Result;
}

static FEFCalystoCatalogEntryV4 MakeCatalogEntry(
    const TCHAR *StableId, const TCHAR *ClassPath, const TCHAR *Archetype,
    const EEFCalystoGenderV4 Gender, const EEFCalystoRarityTierV4 Tier,
    const float InitialFraction, const float DeepShare, const int32 RampFloors,
    const float ThreatCost, const int64 FirstEligibleFloor,
    const int32 MaxPerVariant, const int32 CooldownFloors,
    const EEFCalystoLifecycleV4 Lifecycle, const bool bTierAgnostic) {
  FEFCalystoCatalogEntryV4 Result;
  Result.Name = StableId;
  Result.StableId = FName(StableId);
  Result.Rule = EEFCalystoCatalogRuleV4::Allow;
  Result.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(ClassPath));
  Result.Tier = Tier;
  Result.bTierAgnostic = bTierAgnostic;
  Result.Archetype = FName(Archetype);
  Result.Gender = Gender;
  Result.InitialFraction = InitialFraction;
  Result.DeepShare = DeepShare;
  Result.RampFloors = RampFloors;
  Result.BaseThreatCost = ThreatCost;
  Result.ReferenceLevel = 50;
  Result.FirstEligibleFloor = FirstEligibleFloor;
  Result.MaxPerVariant = MaxPerVariant;
  Result.CooldownFloors = CooldownFloors;
  Result.Lifecycle = Lifecycle;
  return Result;
}

static TArray<FEFCalystoCatalogEntryV4> MakeEnemyCatalog() {
  TArray<FEFCalystoCatalogEntryV4> Result;
  auto Add = [&Result](const TCHAR *Id, const TCHAR *Path,
                       const TCHAR *Archetype, const EEFCalystoGenderV4 Gender,
                       const float DeepShare, const float InitialFraction,
                       const int32 RampFloors, const int64 FirstFloor,
                       const float ThreatCost, const int32 MaxPerVariant,
                       const int32 Cooldown) {
    Result.Add(MakeCatalogEntry(
        Id, Path, Archetype, Gender, EEFCalystoRarityTierV4::Common,
        InitialFraction, DeepShare, RampFloors, ThreatCost, FirstFloor,
        MaxPerVariant, Cooldown, EEFCalystoLifecycleV4::FloorLocal, true));
  };

  Add(TEXT("Enemy.Melee.Female"),
      TEXT("/Game/_Game/Characters/Female/"
           "ACFMeleeEnemyBPFemale.ACFMeleeEnemyBPFemale_C"),
      TEXT("Melee"), EEFCalystoGenderV4::Female, .25f, 1.0f, 0, 1, 2.0f, 8, 0);
  Add(TEXT("Enemy.Melee.Male"),
      TEXT("/Game/_Game/Characters/Male/"
           "ACFMeleeEnemyBPMale.ACFMeleeEnemyBPMale_C"),
      TEXT("Melee"), EEFCalystoGenderV4::Male, .25f, 1.0f, 0, 1, 2.0f, 8, 0);
  Add(TEXT("Enemy.MM.Female"),
      TEXT("/Game/_Game/Characters/Female/"
           "ACFMMEnemyBPFemale.ACFMMEnemyBPFemale_C"),
      TEXT("MM"), EEFCalystoGenderV4::Female, .0333333f, .25f, 3, 1, 1.0f, 12,
      0);
  Add(TEXT("Enemy.MM.Male"),
      TEXT("/Game/_Game/Characters/Male/ACFMMEnemyBPMale.ACFMMEnemyBPMale_C"),
      TEXT("MM"), EEFCalystoGenderV4::Male, .0666667f, .25f, 3, 1, 1.0f, 12, 0);
  Add(TEXT("Enemy.Defender.Female"),
      TEXT("/Game/_Game/Characters/Female/"
           "ACFDefenderEnemyBPFemale.ACFDefenderEnemyBPFemale_C"),
      TEXT("Defender"), EEFCalystoGenderV4::Female, .025f, .15f, 4, 1, 3.0f, 6,
      0);
  Add(TEXT("Enemy.Defender.Male"),
      TEXT("/Game/_Game/Characters/Male/"
           "ACFDefenderEnemyBPMale.ACFDefenderEnemyBPMale_C"),
      TEXT("Defender"), EEFCalystoGenderV4::Male, .025f, .15f, 4, 1, 3.0f, 6,
      0);
  Add(TEXT("Enemy.Ranged.Female"),
      TEXT("/Game/_Game/Characters/Female/"
           "ACFRangedEnemyBPFemale.ACFRangedEnemyBPFemale_C"),
      TEXT("Ranged"), EEFCalystoGenderV4::Female, .12f, .15f, 4, 2, 3.0f, 6, 0);
  Add(TEXT("Enemy.Ranged.Male"),
      TEXT("/Game/_Game/Characters/Male/"
           "ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C"),
      TEXT("Ranged"), EEFCalystoGenderV4::Male, .08f, .15f, 4, 2, 3.0f, 6, 0);
  Add(TEXT("Enemy.Gun.Female"),
      TEXT("/Game/_Game/Characters/Female/"
           "ACFGunEnemyBPFemale.ACFGunEnemyBPFemale_C"),
      TEXT("Gun"), EEFCalystoGenderV4::Female, .025f, .10f, 5, 3, 3.0f, 5, 1);
  Add(TEXT("Enemy.Gun.Male"),
      TEXT("/Game/_Game/Characters/Male/ACFGunEnemyBPMale.ACFGunEnemyBPMale_C"),
      TEXT("Gun"), EEFCalystoGenderV4::Male, .025f, .10f, 5, 3, 3.0f, 5, 1);
  Add(TEXT("Enemy.Mage.Female"),
      TEXT("/Game/_Game/Characters/Female/"
           "ACFMageEnemyBPFemale.ACFMageEnemyBPFemale_C"),
      TEXT("Mage"), EEFCalystoGenderV4::Female, .05f, .10f, 6, 3, 4.0f, 4, 2);
  Add(TEXT("Enemy.Mage.Male"),
      TEXT("/Game/_Game/Characters/Male/"
           "ACFMageEnemyBPMale.ACFMageEnemyBPMale_C"),
      TEXT("Mage"), EEFCalystoGenderV4::Male, .05f, .10f, 6, 3, 4.0f, 4, 2);
  return Result;
}

static TArray<FEFCalystoCatalogEntryV4> MakeCompanionCatalog() {
  return {
      MakeCatalogEntry(
          TEXT("NPC.Companion.Generalist.Female"),
          TEXT("/Game/_Game/Characters/Female/"
               "ACFBaseCompanionBPFemale.ACFBaseCompanionBPFemale_C"),
          TEXT("Generalist"), EEFCalystoGenderV4::Female,
          EEFCalystoRarityTierV4::Common, 1.0f, .25f, 0, 1.0f, 1, 4, 0,
          EEFCalystoLifecycleV4::Recruitable, true),
      MakeCatalogEntry(TEXT("NPC.Companion.Generalist.Male"),
                       TEXT("/Game/_Game/Characters/Male/"
                            "ACFBaseCompanionBPMale.ACFBaseCompanionBPMale_C"),
                       TEXT("Generalist"), EEFCalystoGenderV4::Male,
                       EEFCalystoRarityTierV4::Common, 1.0f, .25f, 0, 1.0f, 1,
                       4, 0, EEFCalystoLifecycleV4::Recruitable, true),
      MakeCatalogEntry(
          TEXT("NPC.Companion.Melee.Female"),
          TEXT("/Game/_Game/Characters/Female/"
               "ACFMeleeCompanionBPFemale.ACFMeleeCompanionBPFemale_C"),
          TEXT("Melee"), EEFCalystoGenderV4::Female,
          EEFCalystoRarityTierV4::Common, .20f, .175f, 5, 1.0f, 3, 4, 0,
          EEFCalystoLifecycleV4::Recruitable, true),
      MakeCatalogEntry(
          TEXT("NPC.Companion.Melee.Male"),
          TEXT("/Game/_Game/Characters/Male/"
               "ACFMeleeCompanionBPMale.ACFMeleeCompanionBPMale_C"),
          TEXT("Melee"), EEFCalystoGenderV4::Male,
          EEFCalystoRarityTierV4::Common, .20f, .175f, 5, 1.0f, 3, 4, 0,
          EEFCalystoLifecycleV4::Recruitable, true),
      MakeCatalogEntry(
          TEXT("NPC.Companion.Ranged.Female"),
          TEXT("/Game/_Game/Characters/Female/"
               "ACFRangedCompanionBPFemale.ACFRangedCompanionBPFemale_C"),
          TEXT("Ranged"), EEFCalystoGenderV4::Female,
          EEFCalystoRarityTierV4::Common, .10f, .075f, 6, 1.0f, 5, 4, 0,
          EEFCalystoLifecycleV4::Recruitable, true),
      MakeCatalogEntry(
          TEXT("NPC.Companion.Ranged.Male"),
          TEXT("/Game/_Game/Characters/Male/"
               "ACFRangedCompanionBPMale.ACFRangedCompanionBPMale_C"),
          TEXT("Ranged"), EEFCalystoGenderV4::Male,
          EEFCalystoRarityTierV4::Common, .10f, .075f, 6, 1.0f, 5, 4, 0,
          EEFCalystoLifecycleV4::Recruitable, true)};
}

static TArray<FEFCalystoCatalogEntryV4> MakeFoodCatalog() {
  return {
      MakeCatalogEntry(
          TEXT("Apple"),
          TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/"
               "BP_Pickup_Food_Apple01.BP_Pickup_Food_Apple01_C"),
          TEXT("Food"), EEFCalystoGenderV4::Any, EEFCalystoRarityTierV4::Common,
          .40f, .40f, 0, 1.0f, 1, 30, 0, EEFCalystoLifecycleV4::FloorLocal,
          false),
      MakeCatalogEntry(
          TEXT("Bread"),
          TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/"
               "BP_Pickup_Food_Bread01.BP_Pickup_Food_Bread01_C"),
          TEXT("Food"), EEFCalystoGenderV4::Any, EEFCalystoRarityTierV4::Common,
          .30f, .30f, 0, 1.0f, 1, 30, 0, EEFCalystoLifecycleV4::FloorLocal,
          false),
      MakeCatalogEntry(
          TEXT("Water"),
          TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Drink/"
               "BP_Pickup_Drink_WaterBottle01.BP_Pickup_Drink_WaterBottle01_C"),
          TEXT("Drink"), EEFCalystoGenderV4::Any,
          EEFCalystoRarityTierV4::Common, .30f, .30f, 0, 1.0f, 1, 30, 0,
          EEFCalystoLifecycleV4::FloorLocal, false),
      MakeCatalogEntry(
          TEXT("CookedMeat"),
          TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/"
               "BP_Pickup_Food_CookedMeat01.BP_Pickup_Food_CookedMeat01_C"),
          TEXT("Food"), EEFCalystoGenderV4::Any,
          EEFCalystoRarityTierV4::Uncommon, 1.0f, 1.0f, 0, 1.0f, 2, 30, 1,
          EEFCalystoLifecycleV4::FloorLocal, false)};
}

static TArray<FEFCalystoCatalogEntryV4> MakeChestContainerCatalog() {
  return {MakeCatalogEntry(
              TEXT("Chest.Locked.V4"),
              TEXT("/Game/_Game/Items/Chests/"
                   "BP_CalystoLockedChestV4.BP_CalystoLockedChestV4_C"),
              TEXT("Chest"), EEFCalystoGenderV4::Any,
              EEFCalystoRarityTierV4::Common, 1.0f, .50f, 0, 1.0f, 1, 10, 0,
              EEFCalystoLifecycleV4::FloorLocal, true),
          MakeCatalogEntry(
              TEXT("Chest.LockPick.V4"),
              TEXT("/Game/_Game/Items/Chests/"
                   "BP_CalystoLockPickChestV4.BP_CalystoLockPickChestV4_C"),
              TEXT("Chest"), EEFCalystoGenderV4::Any,
              EEFCalystoRarityTierV4::Common, 1.0f, .50f, 0, 1.0f, 1, 10, 0,
              EEFCalystoLifecycleV4::FloorLocal, true)};
}

static TArray<FEFCalystoChestContentEntryV4> MakeChestContentsCatalog() {
  FEFCalystoChestContentEntryV4 Health;
  Health.Name = TEXT("ACF Health Potion");
  Health.StableId = TEXT("ChestContent.HealthPotion");
  Health.Tier = EEFCalystoRarityTierV4::Common;
  Health.ContentClass = TSoftClassPtr<UObject>(
      FSoftObjectPath(TEXT("/Game/FullSample/Blueprints/Items/Consumable/"
                           "ACFHealthPotionBP.ACFHealthPotionBP_C")));

  FEFCalystoChestContentEntryV4 Mana;
  Mana.Name = TEXT("ACF Mana Potion");
  Mana.StableId = TEXT("ChestContent.ManaPotion");
  Mana.Tier = EEFCalystoRarityTierV4::Uncommon;
  Mana.ContentClass = TSoftClassPtr<UObject>(
      FSoftObjectPath(TEXT("/Game/FullSample/Blueprints/Items/Consumable/"
                           "ACFManaPotionBP.ACFManaPotionBP_C")));

  FEFCalystoChestContentEntryV4 WinterRecall;
  WinterRecall.Name = TEXT("Winter's Recall");
  WinterRecall.StableId = WintersRecallStableId;
  WinterRecall.Tier = EEFCalystoRarityTierV4::Epic;
  WinterRecall.ContentClass =
      TSoftClassPtr<UObject>(WintersRecallClassPath);
  WinterRecall.FirstEligibleFloor = 1;
  WinterRecall.CooldownFloors = 8;
  WinterRecall.MaxPerFloor = 1;
  WinterRecall.bRequiresGraveyardEligibility = true;
  return {Health, Mana, WinterRecall};
}

static TArray<FEFCalystoCatalogEntryV4> MakeLooseLootCatalog() {
  return {MakeCatalogEntry(
      TEXT("RareAlcohol07"),
      TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Drink/"
           "BP_Pickup_Drink_AlcoholBottle07.BP_Pickup_Drink_AlcoholBottle07_C"),
      TEXT("RareDrink"), EEFCalystoGenderV4::Any, EEFCalystoRarityTierV4::Rare,
      1.0f, 1.0f, 0, 1.0f, 4, 4, 4, EEFCalystoLifecycleV4::FloorLocal, false)};
}

static TArray<FEFCalystoCatalogEntryV4> MakeClothingCatalog() {
  return {MakeCatalogEntry(
      TEXT("Clothing.Armor.ACF.Default"),
      TEXT("/Game/_Game/Items/Clothing/"
           "BP_CalystoArmorPickupV4.BP_CalystoArmorPickupV4_C"),
      TEXT("Armor"), EEFCalystoGenderV4::Any,
      EEFCalystoRarityTierV4::Common, 1.0f, 1.0f, 0, 1.0f, 1, 10, 0,
      EEFCalystoLifecycleV4::FloorLocal, true)};
}

static FEFCalystoTierCurveV4 EnemyTiers(const EEFCalystoStyleV4 Style,
                                        const bool bTheme,
                                        const EEFCalystoThemeV4 Theme) {
  if (bTheme) {
    switch (Theme) {
    case EEFCalystoThemeV4::Forge:
      return MakeTierCurve(MakeTierMix(.58f, .25f, .06f, .01f),
                           MakeTierMix(.22f, .30f, .26f, .12f));
    case EEFCalystoThemeV4::Shrine:
      return MakeTierCurve(MakeTierMix(.72f, .16f, .02f, 0.0f),
                           MakeTierMix(.38f, .26f, .18f, .08f));
    case EEFCalystoThemeV4::Default:
    default:
      return MakeTierCurve(MakeTierMix(.70f, .18f, .02f, 0.0f),
                           MakeTierMix(.35f, .28f, .18f, .09f));
    }
  }
  switch (Style) {
  case EEFCalystoStyleV4::Compact:
    return MakeTierCurve(MakeTierMix(.75f, .14f, .01f, 0.0f),
                         MakeTierMix(.42f, .27f, .15f, .06f));
  case EEFCalystoStyleV4::Branching:
    return MakeTierCurve(MakeTierMix(.62f, .22f, .05f, .01f),
                         MakeTierMix(.28f, .29f, .22f, .11f));
  case EEFCalystoStyleV4::Standard:
  default:
    return MakeTierCurve(MakeTierMix(.70f, .18f, .02f, 0.0f),
                         MakeTierMix(.35f, .28f, .18f, .09f));
  }
}

static FEFCalystoTierCurveV4
GenericTiers(const EEFCalystoContentCategoryV4 Category,
             const EEFCalystoStyleV4 Style, const bool bTheme,
             const EEFCalystoThemeV4 Theme) {
  if (Category == EEFCalystoContentCategoryV4::Enemy) {
    return EnemyTiers(Style, bTheme, Theme);
  }
  if (Category == EEFCalystoContentCategoryV4::NPC) {
    return MakeTierCurve(MakeTierMix(.75f, .13f, .02f, 0.0f),
                         MakeTierMix(.40f, .28f, .15f, .07f));
  }
  if (Category == EEFCalystoContentCategoryV4::Food) {
    return MakeTierCurve(MakeTierMix(.60f, .22f, .07f, .01f),
                         MakeTierMix(.45f, .28f, .13f, .04f));
  }
  if (Category == EEFCalystoContentCategoryV4::Chest) {
    return MakeTierCurve(MakeTierMix(.55f, .25f, .08f, .02f),
                         MakeTierMix(.35f, .30f, .17f, .08f));
  }
  if (Category == EEFCalystoContentCategoryV4::LooseLoot ||
      Category == EEFCalystoContentCategoryV4::Clothing) {
    return MakeTierCurve(MakeTierMix(.65f, .20f, .05f, 0.0f),
                         MakeTierMix(.40f, .28f, .16f, .06f));
  }
  const FEFCalystoTierMixV4 Empty = MakeTierMix(0.0f, 0.0f, 0.0f, 0.0f);
  return MakeTierCurve(Empty, Empty);
}

static FEFCalystoCategoryProfileV4
MakeCategory(const EEFCalystoContentCategoryV4 Category,
             const float Floor1Chance, const float Floor100Chance,
             const int32 MaxPerFloor, const float Tau,
             const EEFCalystoStyleV4 Style, const bool bTheme,
             const EEFCalystoThemeV4 Theme,
             const TArray<FEFCalystoCatalogEntryV4> &EnemyCatalog,
             const TArray<FEFCalystoCatalogEntryV4> &NPCCatalog,
             const TArray<FEFCalystoCatalogEntryV4> &FoodCatalog,
             const TArray<FEFCalystoCatalogEntryV4> &ChestCatalog,
             const TArray<FEFCalystoCatalogEntryV4> &LootCatalog,
             const TArray<FEFCalystoCatalogEntryV4> &ClothingCatalog) {
  FEFCalystoCategoryProfileV4 Result;
  Result.Category = Category;
  Result.bEnabled = Category != EEFCalystoContentCategoryV4::Decoration &&
                    Category != EEFCalystoContentCategoryV4::Lighting;
  Result.Chance = MakeChance(Floor1Chance, Floor100Chance, Tau);
  Result.Limits.MinimumWhenPresent =
      (Category == EEFCalystoContentCategoryV4::Enemy ||
       Category == EEFCalystoContentCategoryV4::Food ||
       Category == EEFCalystoContentCategoryV4::Chest)
          ? 1
          : 0;
  Result.Limits.MaximumPerFloor = MaxPerFloor;
  Result.PityAfterEmptyFloors =
      Category == EEFCalystoContentCategoryV4::Food
          ? 2
          : (Category == EEFCalystoContentCategoryV4::Chest ? 4 : 0);
  Result.MinimumChestContentAttempts = 0;
  Result.MaximumChestContentAttempts = 0;
  Result.Tiers = GenericTiers(Category, Style, bTheme, Theme);
  switch (Category) {
  case EEFCalystoContentCategoryV4::Enemy:
    Result.Catalog = EnemyCatalog;
    break;
  case EEFCalystoContentCategoryV4::NPC:
    Result.Catalog = NPCCatalog;
    break;
  case EEFCalystoContentCategoryV4::Food:
    Result.Catalog = FoodCatalog;
    break;
  case EEFCalystoContentCategoryV4::Chest:
    Result.Catalog = ChestCatalog;
    break;
  case EEFCalystoContentCategoryV4::LooseLoot:
    Result.Catalog = LootCatalog;
    break;
  case EEFCalystoContentCategoryV4::Clothing:
    Result.Catalog = ClothingCatalog;
    break;
  default:
    break;
  }
  if (Category == EEFCalystoContentCategoryV4::Chest) {
    Result.ChestContentsCatalog = MakeChestContentsCatalog();
    Result.MaximumChestContentAttempts = 3;
  }
  return Result;
}

static void ConfigureLighting(FEFCalystoLightingPolicyV4 &Lighting) {
  Lighting.Mode = EEFCalystoLightingModeV4::CalystoNative;
  Lighting.TorchClass = TSoftClassPtr<AActor>(
      FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Blueprint/Lightning/"
                           "BP_WallTorch.BP_WallTorch_C")));
  Lighting.Height = 200.0f;
  Lighting.TileDistance = 10;
}

static bool IsExactRegisteredActorClass(const FSoftObjectPath &ClassPath,
                                        const UClass *RequiredBaseClass) {
  if (!ClassPath.IsValid() || !IsValid(RequiredBaseClass)) {
    return false;
  }
  if (ClassPath.GetLongPackageName().StartsWith(TEXT("/Script/"))) {
    const UClass *NativeClass =
        FindObject<UClass>(nullptr, *ClassPath.ToString());
    return NativeClass && NativeClass->IsChildOf(RequiredBaseClass);
  }
  const IAssetRegistry *Registry = IAssetRegistry::Get();
  if (!Registry) {
    return false;
  }
  TArray<FTopLevelAssetPath> RequiredBases = {
      RequiredBaseClass->GetClassPathName()};
  TSet<FTopLevelAssetPath> Excluded;
  TSet<FTopLevelAssetPath> DerivedClasses;
  Registry->GetDerivedClassNames(RequiredBases, Excluded, DerivedClasses);
  if (DerivedClasses.Contains(ClassPath.GetAssetPath())) {
    return true;
  }
  const auto IsRegisteredDerivedPath =
      [&](const FString &TaggedClassPath) -> bool {
    FString ObjectPath =
        FPackageName::ExportTextPathToObjectPath(TaggedClassPath);
    if (ObjectPath.IsEmpty()) {
      ObjectPath = TaggedClassPath;
    }
    const FSoftObjectPath ParentPath(ObjectPath);
    if (!ParentPath.IsValid()) {
      return false;
    }
    const FTopLevelAssetPath ParentAssetPath = ParentPath.GetAssetPath();
    if (ParentAssetPath == RequiredBaseClass->GetClassPathName() ||
        DerivedClasses.Contains(ParentAssetPath)) {
      return true;
    }
    const UClass *RegisteredClass =
        FindObject<UClass>(nullptr, *ParentPath.ToString());
    return RegisteredClass && RegisteredClass->IsChildOf(RequiredBaseClass);
  };
  TArray<FAssetData> Assets;
  if (!Registry->GetAssetsByPackageName(FName(*ClassPath.GetLongPackageName()),
                                        Assets, true)) {
    return false;
  }
  for (const FAssetData &Asset : Assets) {
    FString GeneratedClassPath;
    if (!Asset.GetTagValue(FBlueprintTags::GeneratedClassPath,
                           GeneratedClassPath) ||
        FPackageName::ExportTextPathToObjectPath(GeneratedClassPath) !=
            ClassPath.ToString()) {
      continue;
    }
    FString NativeParentClassPath;
    if (Asset.GetTagValue(FBlueprintTags::NativeParentClassPath,
                          NativeParentClassPath) &&
        IsRegisteredDerivedPath(NativeParentClassPath)) {
      return true;
    }
    FString ParentClassPath;
    if (Asset.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPath) &&
        IsRegisteredDerivedPath(ParentClassPath)) {
      return true;
    }
    return false;
  }
  return false;
}

static int32 CeilingForCategory(const FEFCalystoSafetyCeilingsV4 &Limits,
                                const EEFCalystoContentCategoryV4 Category) {
  switch (Category) {
  case EEFCalystoContentCategoryV4::Enemy:
    return Limits.MaximumEnemies;
  case EEFCalystoContentCategoryV4::NPC:
    return Limits.MaximumNPCs;
  case EEFCalystoContentCategoryV4::Food:
    return Limits.MaximumFood;
  case EEFCalystoContentCategoryV4::Chest:
    return Limits.MaximumChests;
  case EEFCalystoContentCategoryV4::LooseLoot:
    return Limits.MaximumLooseLoot;
  case EEFCalystoContentCategoryV4::Clothing:
    return Limits.MaximumClothing;
  case EEFCalystoContentCategoryV4::SpecialEvent:
    return Limits.MaximumSpecialEvents;
  case EEFCalystoContentCategoryV4::Decoration:
  case EEFCalystoContentCategoryV4::Lighting:
  default:
    return 0;
  }
}

static bool IsKnownCategory(const EEFCalystoContentCategoryV4 Category) {
  switch (Category) {
  case EEFCalystoContentCategoryV4::Enemy:
  case EEFCalystoContentCategoryV4::NPC:
  case EEFCalystoContentCategoryV4::Food:
  case EEFCalystoContentCategoryV4::Chest:
  case EEFCalystoContentCategoryV4::LooseLoot:
  case EEFCalystoContentCategoryV4::Clothing:
  case EEFCalystoContentCategoryV4::SpecialEvent:
  case EEFCalystoContentCategoryV4::Decoration:
  case EEFCalystoContentCategoryV4::Lighting:
    return true;
  default:
    return false;
  }
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

static bool IsKnownRule(const EEFCalystoCatalogRuleV4 Rule) {
  return Rule == EEFCalystoCatalogRuleV4::Neutral ||
         Rule == EEFCalystoCatalogRuleV4::Allow ||
         Rule == EEFCalystoCatalogRuleV4::Block;
}

static bool IsKnownGender(const EEFCalystoGenderV4 Gender) {
  return Gender == EEFCalystoGenderV4::Any ||
         Gender == EEFCalystoGenderV4::Female ||
         Gender == EEFCalystoGenderV4::Male;
}

static bool IsKnownLifecycle(const EEFCalystoLifecycleV4 Lifecycle) {
  return Lifecycle == EEFCalystoLifecycleV4::FloorLocal ||
         Lifecycle == EEFCalystoLifecycleV4::Recruitable;
}
} // namespace EFCalystoPolicyV4Private

UEFCalystoDungeonDirectorPolicyV4::UEFCalystoDungeonDirectorPolicyV4() {
  using namespace EFCalystoPolicyV4Private;
  ValidatedDungeonSizes = {26, 27, 28, 29, 30};

  const TArray<FEFCalystoCatalogEntryV4> Enemies = MakeEnemyCatalog();
  const TArray<FEFCalystoCatalogEntryV4> NPCs = MakeCompanionCatalog();
  const TArray<FEFCalystoCatalogEntryV4> Food = MakeFoodCatalog();
  const TArray<FEFCalystoCatalogEntryV4> Chests = MakeChestContainerCatalog();
  const TArray<FEFCalystoCatalogEntryV4> Loot = MakeLooseLootCatalog();
  const TArray<FEFCalystoCatalogEntryV4> Clothing = MakeClothingCatalog();

  auto AddStyle =
      [&](const EEFCalystoStyleV4 Id, const float SelectionProbability,
          const int32 MinSize, const int32 MaxSize, const float Size1,
          const float Size100, const float Branch1, const float Branch100,
          const FEFCalystoPertRangeV4 &Anchors,
          const FEFCalystoContextTraitsV4 &Traits, const float WinterTau,
          const float Threat1, const float Threat100,
          const TArray<TPair<EEFCalystoContentCategoryV4, FVector>>
              &ChancesAndMax) {
        FEFCalystoStyleProfileV4 Profile;
        Profile.Style = Id;
        Profile.SelectionProbability = SelectionProbability;
        Profile.Volatility = Traits.Volatility;
        Profile.Mystery = Traits.Mystery;
        Profile.Danger = Traits.Danger;
        Profile.Safe = Traits.Safe;
        Profile.Abundance = Traits.Abundance;
        Profile.ClothingInfluence = Traits.ClothingInfluence;
        Profile.WinterTau = WinterTau;
        Profile.Layout.MinimumDungeonEdge = MinSize;
        Profile.Layout.MaximumDungeonEdge = MaxSize;
        Profile.Layout.EarlySizeMode = Size1;
        Profile.Layout.DeepSizeMode = Size100;
        Profile.Layout.SizeTau =
            Id == EEFCalystoStyleV4::Compact
                ? 10.0f
                : (Id == EEFCalystoStyleV4::Branching ? 14.0f : 12.0f);
        Profile.Layout.CandidateAnchorDensity = Anchors;
        Profile.Layout.BranchingChance =
            MakeChance(Branch1, Branch100, Profile.Layout.SizeTau);
        Profile.Threat.EarlyBudget = Threat1;
        Profile.Threat.DeepBudget = Threat100;
        Profile.Threat.Tau =
            Id == EEFCalystoStyleV4::Compact
                ? 9.0f
                : (Id == EEFCalystoStyleV4::Branching ? 11.0f : 10.0f);
        ConfigureLighting(Profile.Lighting);
        for (const TPair<EEFCalystoContentCategoryV4, FVector> &Item :
             ChancesAndMax) {
          Profile.Categories.Add(MakeCategory(
              Item.Key, Item.Value.X, Item.Value.Y,
              FMath::RoundToInt(Item.Value.Z), 12.0f, Id, false,
              EEFCalystoThemeV4::Default, Enemies, NPCs, Food, Chests, Loot,
              Clothing));
        }
        Profile.RefreshNothing();
        Styles.Add(MoveTemp(Profile));
      };

  FEFCalystoContextTraitsV4 StandardTraits;
  StandardTraits.Volatility = .67f;
  FEFCalystoContextTraitsV4 CompactTraits;
  CompactTraits.Danger = .10f;
  CompactTraits.Safe = .05f;
  CompactTraits.Abundance = -.10f;
  CompactTraits.Mystery = -.10f;
  CompactTraits.Volatility = .45f;
  FEFCalystoContextTraitsV4 BranchingTraits;
  BranchingTraits.Danger = .05f;
  BranchingTraits.Safe = .10f;
  BranchingTraits.Abundance = .10f;
  BranchingTraits.Mystery = .25f;
  BranchingTraits.ClothingInfluence = .10f;
  BranchingTraits.Volatility = .80f;

  const TArray<TPair<EEFCalystoContentCategoryV4, FVector>> StandardCategories =
      {{EEFCalystoContentCategoryV4::Enemy, FVector(.70f, .90f, 25)},
       {EEFCalystoContentCategoryV4::NPC, FVector(.08f, .15f, 4)},
       {EEFCalystoContentCategoryV4::Food, FVector(.75f, .55f, 30)},
       {EEFCalystoContentCategoryV4::Chest, FVector(.25f, .45f, 10)},
       {EEFCalystoContentCategoryV4::LooseLoot, FVector(.05f, .20f, 4)},
       {EEFCalystoContentCategoryV4::Clothing, FVector(.08f, .18f, 10)},
       {EEFCalystoContentCategoryV4::SpecialEvent, FVector(0, 0, 6)},
       {EEFCalystoContentCategoryV4::Decoration, FVector(0, 0, 0)},
       {EEFCalystoContentCategoryV4::Lighting, FVector(0, 0, 0)}};
  const TArray<TPair<EEFCalystoContentCategoryV4, FVector>> CompactCategories =
      {{EEFCalystoContentCategoryV4::Enemy, FVector(.65f, .88f, 20)},
       {EEFCalystoContentCategoryV4::NPC, FVector(.06f, .12f, 3)},
       {EEFCalystoContentCategoryV4::Food, FVector(.65f, .45f, 20)},
       {EEFCalystoContentCategoryV4::Chest, FVector(.20f, .35f, 6)},
       {EEFCalystoContentCategoryV4::LooseLoot, FVector(.04f, .14f, 3)},
       {EEFCalystoContentCategoryV4::Clothing, FVector(.06f, .14f, 6)},
       {EEFCalystoContentCategoryV4::SpecialEvent, FVector(0, 0, 4)},
       {EEFCalystoContentCategoryV4::Decoration, FVector(0, 0, 0)},
       {EEFCalystoContentCategoryV4::Lighting, FVector(0, 0, 0)}};
  const TArray<TPair<EEFCalystoContentCategoryV4, FVector>>
      BranchingCategories = {
          {EEFCalystoContentCategoryV4::Enemy, FVector(.75f, .90f, 25)},
          {EEFCalystoContentCategoryV4::NPC, FVector(.10f, .20f, 4)},
          {EEFCalystoContentCategoryV4::Food, FVector(.80f, .62f, 30)},
          {EEFCalystoContentCategoryV4::Chest, FVector(.30f, .55f, 10)},
          {EEFCalystoContentCategoryV4::LooseLoot, FVector(.08f, .25f, 4)},
          {EEFCalystoContentCategoryV4::Clothing, FVector(.12f, .25f, 10)},
          {EEFCalystoContentCategoryV4::SpecialEvent, FVector(0, 0, 6)},
          {EEFCalystoContentCategoryV4::Decoration, FVector(0, 0, 0)},
          {EEFCalystoContentCategoryV4::Lighting, FVector(0, 0, 0)}};

  AddStyle(EEFCalystoStyleV4::Standard, .50f, 18, 30, 20, 30, .45f, .55f,
           {.25f, .32f, .45f, 4.0f}, StandardTraits, 100.0f, 6, 50,
           StandardCategories);
  AddStyle(EEFCalystoStyleV4::Compact, .25f, 18, 26, 19, 25, .30f, .40f,
           {.25f, .30f, .40f, 4.0f}, CompactTraits, 130.0f, 5, 44,
           CompactCategories);
  AddStyle(EEFCalystoStyleV4::Branching, .25f, 20, 30, 21, 30, .60f, .70f,
           {.30f, .38f, .50f, 4.0f}, BranchingTraits, 75.0f, 7, 55,
           BranchingCategories);

  auto AddTheme =
      [&](const EEFCalystoThemeV4 Id, const float SelectionProbability,
          const TCHAR *RoomType, const int32 MinSize, const int32 MaxSize,
          const float Size1, const float Size100, const float Branch1,
          const float Branch100, const FEFCalystoPertRangeV4 &Anchors,
          const FEFCalystoContextTraitsV4 &Traits, const float WinterTau,
          const float Threat1, const float Threat100,
          const TArray<TPair<EEFCalystoContentCategoryV4, FVector>>
              &ChancesAndMax) {
        FEFCalystoThemeProfileV4 Profile;
        Profile.Theme = Id;
        Profile.SelectionProbability = SelectionProbability;
        if (RoomType && *RoomType) {
          Profile.RoomType = TSoftObjectPtr<UObject>(FSoftObjectPath(RoomType));
        }
        Profile.Volatility = Traits.Volatility;
        Profile.Mystery = Traits.Mystery;
        Profile.Danger = Traits.Danger;
        Profile.Safe = Traits.Safe;
        Profile.Abundance = Traits.Abundance;
        Profile.ClothingInfluence = Traits.ClothingInfluence;
        Profile.WinterTau = WinterTau;
        Profile.Layout.MinimumDungeonEdge = MinSize;
        Profile.Layout.MaximumDungeonEdge = MaxSize;
        Profile.Layout.EarlySizeMode = Size1;
        Profile.Layout.DeepSizeMode = Size100;
        Profile.Layout.SizeTau =
            Id == EEFCalystoThemeV4::Forge
                ? 9.0f
                : (Id == EEFCalystoThemeV4::Shrine ? 14.0f : 12.0f);
        Profile.Layout.CandidateAnchorDensity = Anchors;
        Profile.Layout.BranchingChance =
            MakeChance(Branch1, Branch100, Profile.Layout.SizeTau);
        Profile.Threat.EarlyBudget = Threat1;
        Profile.Threat.DeepBudget = Threat100;
        Profile.Threat.Tau =
            Id == EEFCalystoThemeV4::Forge
                ? 8.0f
                : (Id == EEFCalystoThemeV4::Shrine ? 12.0f : 10.0f);
        ConfigureLighting(Profile.Lighting);
        for (const TPair<EEFCalystoContentCategoryV4, FVector> &Item :
             ChancesAndMax) {
          Profile.Categories.Add(
              MakeCategory(Item.Key, Item.Value.X, Item.Value.Y,
                           FMath::RoundToInt(Item.Value.Z), 12.0f,
                           EEFCalystoStyleV4::Standard, true, Id, Enemies, NPCs,
                            Food, Chests, Loot, Clothing));
        }
        Profile.RefreshNothing();
        Themes.Add(MoveTemp(Profile));
      };

  FEFCalystoContextTraitsV4 DefaultTraits;
  DefaultTraits.Volatility = .67f;
  FEFCalystoContextTraitsV4 ForgeTraits;
  ForgeTraits.Danger = .30f;
  ForgeTraits.Safe = -.25f;
  ForgeTraits.Abundance = -.20f;
  ForgeTraits.Mystery = .20f;
  ForgeTraits.ClothingInfluence = .10f;
  ForgeTraits.Volatility = .75f;
  FEFCalystoContextTraitsV4 ShrineTraits;
  ShrineTraits.Danger = -.20f;
  ShrineTraits.Safe = .30f;
  ShrineTraits.Abundance = .20f;
  ShrineTraits.Mystery = .35f;
  ShrineTraits.ClothingInfluence = .05f;
  ShrineTraits.Volatility = .55f;

  const TArray<TPair<EEFCalystoContentCategoryV4, FVector>> ForgeCategories = {
      {EEFCalystoContentCategoryV4::Enemy, FVector(.80f, .90f, 25)},
      {EEFCalystoContentCategoryV4::NPC, FVector(.03f, .08f, 2)},
      {EEFCalystoContentCategoryV4::Food, FVector(.55f, .35f, 18)},
      {EEFCalystoContentCategoryV4::Chest, FVector(.30f, .55f, 10)},
      {EEFCalystoContentCategoryV4::LooseLoot, FVector(.08f, .22f, 4)},
      {EEFCalystoContentCategoryV4::Clothing, FVector(.10f, .20f, 8)},
      {EEFCalystoContentCategoryV4::SpecialEvent, FVector(0, 0, 6)},
      {EEFCalystoContentCategoryV4::Decoration, FVector(0, 0, 0)},
      {EEFCalystoContentCategoryV4::Lighting, FVector(0, 0, 0)}};
  const TArray<TPair<EEFCalystoContentCategoryV4, FVector>> ShrineCategories = {
      {EEFCalystoContentCategoryV4::Enemy, FVector(.55f, .78f, 18)},
      {EEFCalystoContentCategoryV4::NPC, FVector(.15f, .30f, 4)},
      {EEFCalystoContentCategoryV4::Food, FVector(.80f, .65f, 30)},
      {EEFCalystoContentCategoryV4::Chest, FVector(.35f, .60f, 8)},
      {EEFCalystoContentCategoryV4::LooseLoot, FVector(.05f, .18f, 4)},
      {EEFCalystoContentCategoryV4::Clothing, FVector(.12f, .22f, 10)},
      {EEFCalystoContentCategoryV4::SpecialEvent, FVector(0, 0, 6)},
      {EEFCalystoContentCategoryV4::Decoration, FVector(0, 0, 0)},
      {EEFCalystoContentCategoryV4::Lighting, FVector(0, 0, 0)}};

  AddTheme(EEFCalystoThemeV4::Default, .60f, TEXT(""), 18, 30, 20, 30, .45f,
           .55f, {.25f, .32f, .45f, 4.0f}, DefaultTraits, 100.0f, 6, 50,
           StandardCategories);
  AddTheme(EEFCalystoThemeV4::Forge, .25f,
           TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Props/"
                "DA_RoomForge.DA_RoomForge"),
           20, 30, 22, 30, .35f, .55f, {.30f, .38f, .50f, 4.0f}, ForgeTraits,
           60.0f, 8, 60, ForgeCategories);
  AddTheme(EEFCalystoThemeV4::Shrine, .15f,
           TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Props/"
                "DA_RoomShrine.DA_RoomShrine"),
           18, 28, 19, 27, .45f, .65f, {.25f, .34f, .45f, 4.0f}, ShrineTraits,
           120.0f, 4, 42, ShrineCategories);
  SynchronizeDerivedTierNothing();
}

void UEFCalystoDungeonDirectorPolicyV4::PostLoad() {
  Super::PostLoad();
  SynchronizeDerivedTierNothing();
}

#if WITH_EDITOR
void UEFCalystoDungeonDirectorPolicyV4::PostEditChangeProperty(
    FPropertyChangedEvent &PropertyChangedEvent) {
  const FName ChangedProperty = PropertyChangedEvent.GetPropertyName();
  const bool bChangedTierProbability =
      ChangedProperty == GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Common) ||
      ChangedProperty ==
          GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Uncommon) ||
      ChangedProperty == GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Rare) ||
      ChangedProperty == GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Epic);
  if (bChangedTierProbability) {
    auto ClampChangedTier = [ChangedProperty](FEFCalystoTierMixV4 &Mix) {
      float *ChangedValue = nullptr;
      if (ChangedProperty ==
          GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Common)) {
        ChangedValue = &Mix.Common;
      } else if (ChangedProperty ==
                 GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Uncommon)) {
        ChangedValue = &Mix.Uncommon;
      } else if (ChangedProperty ==
                 GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Rare)) {
        ChangedValue = &Mix.Rare;
      } else if (ChangedProperty ==
                 GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Epic)) {
        ChangedValue = &Mix.Epic;
      }
      if (!ChangedValue) {
        return;
      }
      const float OtherMass = Mix.GetSelectableMass() - *ChangedValue;
      *ChangedValue =
          FMath::Clamp(*ChangedValue, 0.0f, FMath::Max(0.0f, .90f - OtherMass));
    };
    auto ClampCategories =
        [&ClampChangedTier](TArray<FEFCalystoCategoryProfileV4> &Categories) {
          for (FEFCalystoCategoryProfileV4 &Category : Categories) {
            ClampChangedTier(Category.Tiers.AtFloor1);
            ClampChangedTier(Category.Tiers.AtFloor100);
          }
        };
    for (FEFCalystoStyleProfileV4 &Style : Styles) {
      ClampCategories(Style.Categories);
    }
    for (FEFCalystoThemeProfileV4 &Theme : Themes) {
      ClampCategories(Theme.Categories);
    }
  }
  SynchronizeDerivedTierNothing();
  Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UEFCalystoDungeonDirectorPolicyV4::SynchronizeDerivedTierNothing() {
  for (FEFCalystoStyleProfileV4 &Style : Styles) {
    Style.RefreshNothing();
  }
  for (FEFCalystoThemeProfileV4 &Theme : Themes) {
    Theme.RefreshNothing();
  }
}

const FEFCalystoStyleProfileV4 *UEFCalystoDungeonDirectorPolicyV4::FindStyle(
    const EEFCalystoStyleV4 Style) const {
  return Styles.FindByPredicate(
      [Style](const FEFCalystoStyleProfileV4 &Candidate) {
        return Candidate.Style == Style;
      });
}

const FEFCalystoThemeProfileV4 *UEFCalystoDungeonDirectorPolicyV4::FindTheme(
    const EEFCalystoThemeV4 Theme) const {
  return Themes.FindByPredicate(
      [Theme](const FEFCalystoThemeProfileV4 &Candidate) {
        return Candidate.Theme == Theme;
      });
}

const FEFCalystoCategoryProfileV4 *
UEFCalystoDungeonDirectorPolicyV4::FindCategory(
    const TArray<FEFCalystoCategoryProfileV4> &Categories,
    const EEFCalystoContentCategoryV4 Category) {
  return Categories.FindByPredicate(
      [Category](const FEFCalystoCategoryProfileV4 &Candidate) {
        return Candidate.Category == Category;
      });
}

bool UEFCalystoDungeonDirectorPolicyV4::ValidatePolicy() const {
  FString Error;
  const bool bValid = Validate(Error);
  if (!bValid) {
    UE_LOG(LogEFCalystoPolicyV4, Warning, TEXT("V4 policy validation failed: %s"),
           *Error);
  }
  return bValid;
}

FString UEFCalystoDungeonDirectorPolicyV4::GetPolicyHash() const {
  return FEFCalystoDungeonDirectorResolverV4::GetPolicyHash(this);
}

bool UEFCalystoDungeonDirectorPolicyV4::Validate(FString &OutError) const {
  using namespace EFCalystoPolicyV4Private;
  OutError.Reset();
  if (SchemaVersion != 4 || GeneratorVersion != 4 ||
      PolicyId != FName(TEXT("CalystoDungeonDirectorV4"))) {
    OutError = TEXT("V4 policy identity must be schema 4, generator 4, "
                    "PolicyId CalystoDungeonDirectorV4.");
    return false;
  }
  if (SafetyCeilings.MaximumEnemies != 25 || SafetyCeilings.MaximumNPCs != 4 ||
      SafetyCeilings.MaximumFood != 30 || SafetyCeilings.MaximumChests != 10 ||
      SafetyCeilings.MaximumLooseLoot != 4 ||
      SafetyCeilings.MaximumClothing != 10 ||
      SafetyCeilings.MaximumSpecialEvents != 6 ||
      SafetyCeilings.MaximumDirectorActors != 89 ||
      !FMath::IsNearlyEqual(SafetyCeilings.MaximumThreatBudget, 60.0f)) {
    OutError = TEXT("V4 immutable technical ceilings have drifted.");
    return false;
  }
  TSet<int32> Sizes;
  for (const int32 Size : ValidatedDungeonSizes) {
    if (Size < 18 || Size > 30 || Sizes.Contains(Size)) {
      OutError = FString::Printf(
          TEXT("Invalid or duplicated certified size %d."), Size);
      return false;
    }
    Sizes.Add(Size);
  }
  if (Sizes.IsEmpty()) {
    OutError = TEXT("V4 requires at least one certified dungeon size.");
    return false;
  }

  auto ValidateLayout =
      [&OutError](const FEFCalystoLayoutProfileV4 &Layout) -> bool {
    if (Layout.MinimumDungeonEdge < 18 || Layout.MaximumDungeonEdge > 30 ||
        Layout.MinimumDungeonEdge > Layout.MaximumDungeonEdge ||
        !FMath::IsFinite(Layout.EarlySizeMode) ||
        !FMath::IsFinite(Layout.DeepSizeMode) ||
        Layout.EarlySizeMode < Layout.MinimumDungeonEdge ||
        Layout.EarlySizeMode > Layout.MaximumDungeonEdge ||
        Layout.DeepSizeMode < Layout.MinimumDungeonEdge ||
        Layout.DeepSizeMode > Layout.MaximumDungeonEdge ||
        !FMath::IsFinite(Layout.SizeHalfRange) || Layout.SizeHalfRange < 0.0f ||
        Layout.SizeHalfRange > 6.0f || !FMath::IsFinite(Layout.SizeTau) ||
        Layout.SizeTau <= 0.0f ||
        !FMath::IsFinite(Layout.CandidateAnchorDensity.Min) ||
        !FMath::IsFinite(Layout.CandidateAnchorDensity.Mode) ||
        !FMath::IsFinite(Layout.CandidateAnchorDensity.Max) ||
        !FMath::IsFinite(Layout.CandidateAnchorDensity.Concentration) ||
        Layout.CandidateAnchorDensity.Min < .20f ||
        Layout.CandidateAnchorDensity.Max > .50f ||
        Layout.CandidateAnchorDensity.Min >
            Layout.CandidateAnchorDensity.Mode ||
        Layout.CandidateAnchorDensity.Mode >
            Layout.CandidateAnchorDensity.Max ||
        Layout.CandidateAnchorDensity.Concentration < 2.0f ||
        Layout.CandidateAnchorDensity.Concentration > 8.0f ||
        !IsFiniteUnit(Layout.BranchingChance.ChanceAtFloor1) ||
        !IsFiniteUnit(Layout.BranchingChance.ChanceAtFloor100) ||
        Layout.BranchingChance.ChanceAtFloor1 < .30f ||
        Layout.BranchingChance.ChanceAtFloor1 > .70f ||
        Layout.BranchingChance.ChanceAtFloor100 < .30f ||
        Layout.BranchingChance.ChanceAtFloor100 > .70f ||
        !FMath::IsFinite(Layout.BranchingChance.Tau) ||
        Layout.BranchingChance.Tau <= 0.0f) {
      OutError = TEXT("A V4 layout profile violates Calysto-safe ranges.");
      return false;
    }
    return true;
  };

  auto ValidateLighting =
      [&OutError](const FEFCalystoLightingPolicyV4 &Lighting) -> bool {
    const FSoftObjectPath Expected(
        TEXT("/Game/Calysto/Dungeon/Blueprint/Lightning/"
             "BP_WallTorch.BP_WallTorch_C"));
    if (Lighting.Mode != EEFCalystoLightingModeV4::CalystoNative ||
        Lighting.TorchClass.ToSoftObjectPath() != Expected ||
        !FPackageName::DoesPackageExist(Expected.GetLongPackageName()) ||
        !FMath::IsFinite(Lighting.Height) || Lighting.Height < 0.0f ||
        Lighting.Height > 1000.0f || Lighting.TileDistance < 1 ||
        Lighting.TileDistance > 100) {
      OutError = TEXT("V4 native lighting requires WallTorch, a finite native "
                      "height in 0..1000, and a native tile interval in 1..100.");
      return false;
    }
    return true;
  };

  auto ValidateThreat = [this](const FEFCalystoThreatCurveV4 &Threat) {
    return FMath::IsFinite(Threat.EarlyBudget) &&
           FMath::IsFinite(Threat.DeepBudget) && FMath::IsFinite(Threat.Tau) &&
           FMath::IsFinite(Threat.RelativeRange) &&
           Threat.EarlyBudget >= 0.0f &&
           Threat.EarlyBudget <= SafetyCeilings.MaximumThreatBudget &&
           Threat.DeepBudget >= 0.0f &&
           Threat.DeepBudget <= SafetyCeilings.MaximumThreatBudget &&
           Threat.Tau > 0.0f && Threat.RelativeRange >= 0.0f &&
           Threat.RelativeRange <= .50f;
  };

  auto ValidateDecoration = [](const FEFCalystoDecorationPolicyV4 &Decoration) {
    if (!Decoration.bUseCalystoNativeArrays) {
      return false;
    }
    TSet<FName> Ids;
    for (const FName Id : Decoration.TransientAllowlistedArrayIds) {
      if (Id.IsNone() || Ids.Contains(Id)) {
        return false;
      }
      Ids.Add(Id);
    }
    return true;
  };

  auto ValidateCategory =
      [this, &OutError](const FEFCalystoCategoryProfileV4 &Category,
                        const TCHAR *Owner,
                        const bool bThemeOwnsEnabled) -> bool {
    if (!IsKnownCategory(Category.Category) ||
        !IsFiniteUnit(Category.Chance.ChanceAtFloor1) ||
        !IsFiniteUnit(Category.Chance.ChanceAtFloor100) ||
        Category.Chance.ChanceAtFloor1 > .90f ||
        Category.Chance.ChanceAtFloor100 > .90f ||
        !FMath::IsFinite(Category.Chance.Tau) || Category.Chance.Tau <= 0.0f) {
      OutError =
          FString::Printf(TEXT("%s category %d has invalid chance values."),
                          Owner, static_cast<int32>(Category.Category));
      return false;
    }
    const int32 Ceiling = CeilingForCategory(SafetyCeilings, Category.Category);
    if (Category.Limits.MinimumWhenPresent < 0 ||
        Category.Limits.MaximumPerFloor < Category.Limits.MinimumWhenPresent ||
        Category.Limits.MaximumPerFloor > Ceiling ||
        Category.MinimumChestContentAttempts < 0 ||
        Category.MaximumChestContentAttempts <
            Category.MinimumChestContentAttempts ||
        Category.MaximumChestContentAttempts > 3 ||
        Category.PityAfterEmptyFloors < 0 ||
        Category.PityAfterEmptyFloors > 20 ||
        ((Category.Category != EEFCalystoContentCategoryV4::Food &&
          Category.Category != EEFCalystoContentCategoryV4::Chest) &&
         Category.PityAfterEmptyFloors != 0) ||
        (Category.Category != EEFCalystoContentCategoryV4::Chest &&
         (Category.MinimumChestContentAttempts != 0 ||
          Category.MaximumChestContentAttempts != 0))) {
      OutError =
          FString::Printf(TEXT("%s category %d violates its exact limits."),
                          Owner, static_cast<int32>(Category.Category));
      return false;
    }
    FString TierError;
    if (!FEFCalystoDungeonDirectorMathV4::ValidateTierMix(
            Category.Tiers.AtFloor1, &TierError) ||
        !FEFCalystoDungeonDirectorMathV4::ValidateTierMix(
            Category.Tiers.AtFloor100, &TierError)) {
      OutError =
          FString::Printf(TEXT("%s category %d tier mix is invalid: %s"), Owner,
                          static_cast<int32>(Category.Category), *TierError);
      return false;
    }
    TSet<FName> Ids;
    for (const FEFCalystoCatalogEntryV4 &Entry : Category.Catalog) {
      const FSoftObjectPath Path = Entry.ActorClass.ToSoftObjectPath();
      if (Entry.StableId == WintersRecallStableId ||
          Path == WintersRecallClassPath) {
        OutError = FString::Printf(
            TEXT("%s category %d aliases Winter's Recall outside the Chest "
                 "Contents catalog."),
            Owner, static_cast<int32>(Category.Category));
        return false;
      }
      TSet<EEFCalystoRarityTierV4> AllowedTiers;
      bool bInvalidAllowedTier = false;
      for (const EEFCalystoRarityTierV4 Tier : Entry.AllowedTiers) {
        if (!IsKnownTier(Tier) || AllowedTiers.Contains(Tier)) {
          bInvalidAllowedTier = true;
          break;
        }
        AllowedTiers.Add(Tier);
      }
      if (Entry.Name.TrimStartAndEnd().IsEmpty() || Entry.StableId.IsNone() ||
          Ids.Contains(Entry.StableId) ||
          Entry.StableId.ToString().Contains(TEXT("Dummy"),
                                             ESearchCase::IgnoreCase) ||
          !IsKnownRule(Entry.Rule) || !IsKnownTier(Entry.Tier) ||
          !IsKnownGender(Entry.Gender) || !IsKnownLifecycle(Entry.Lifecycle) ||
          bInvalidAllowedTier ||
          (!Entry.bTierAgnostic && !Entry.AllowedTiers.IsEmpty()) ||
          Entry.Archetype.IsNone() || !IsFiniteUnit(Entry.InitialFraction) ||
          !IsFiniteUnit(Entry.DeepShare) || Entry.RampFloors < 0 ||
          !FMath::IsFinite(Entry.BaseThreatCost) ||
          Entry.BaseThreatCost <= 0.0f || Entry.ReferenceLevel < 1 ||
          Entry.ReferenceLevel > 100 || Entry.FirstEligibleFloor < 1 ||
          Entry.MaxPerVariant < 1 ||
          Entry.MaxPerVariant > FMath::Max(1, Ceiling) ||
          Entry.CooldownFloors < 0) {
        OutError = FString::Printf(
            TEXT("%s catalog entry %s is invalid or duplicated."), Owner,
            *Entry.StableId.ToString());
        return false;
      }
      Ids.Add(Entry.StableId);
      if (Entry.Rule == EEFCalystoCatalogRuleV4::Allow) {
        const UClass *RequiredClass =
            (Category.Category == EEFCalystoContentCategoryV4::Enemy ||
             Category.Category == EEFCalystoContentCategoryV4::NPC)
                ? APawn::StaticClass()
                : AActor::StaticClass();
        const bool bNativeClass =
            Path.GetLongPackageName().StartsWith(TEXT("/Script/"));
        if (!Path.IsValid() ||
            (!bNativeClass &&
             !FPackageName::DoesPackageExist(Path.GetLongPackageName())) ||
            !IsExactRegisteredActorClass(Path, RequiredClass)) {
          OutError = FString::Printf(TEXT("%s catalog class %s is missing or "
                                          "has the wrong native base."),
                                     Owner, *Path.ToString());
          return false;
        }
      }
    }
    if (Category.Category == EEFCalystoContentCategoryV4::Enemy &&
        Category.Catalog.Num() != 12) {
      OutError = FString::Printf(
          TEXT("%s must contain exactly the 12 non-Dummy enemy variants."),
          Owner);
      return false;
    }
    if (Category.Category == EEFCalystoContentCategoryV4::NPC &&
        Category.Catalog.Num() != 6) {
      OutError = FString::Printf(
          TEXT("%s must contain exactly the six companion variants."), Owner);
      return false;
    }
    if (Category.Category == EEFCalystoContentCategoryV4::Clothing &&
        (!bThemeOwnsEnabled || Category.bEnabled) && !Category.bBlocked &&
        (Category.Chance.ChanceAtFloor1 > 0.0f ||
         Category.Chance.ChanceAtFloor100 > 0.0f) &&
        !Category.Catalog.ContainsByPredicate(
            [](const FEFCalystoCatalogEntryV4 &Entry) {
              return Entry.Rule == EEFCalystoCatalogRuleV4::Allow;
            })) {
      OutError = FString::Printf(
          TEXT("%s enables Clothing probability without an allowed pickup "
               "catalog entry."),
          Owner);
      return false;
    }
    if (Category.Category != EEFCalystoContentCategoryV4::Chest &&
        !Category.ChestContentsCatalog.IsEmpty()) {
      OutError = FString::Printf(
          TEXT("%s stores chest contents outside the Chest category."), Owner);
      return false;
    }
    TSet<FName> ContentIds;
    int32 WintersRecallEntryCount = 0;
    for (const FEFCalystoChestContentEntryV4 &Entry :
         Category.ChestContentsCatalog) {
      const FSoftObjectPath Path = Entry.ContentClass.ToSoftObjectPath();
      const bool bNativeClass =
          Path.GetLongPackageName().StartsWith(TEXT("/Script/"));
      const bool bUsesWintersRecallId =
          Entry.StableId == WintersRecallStableId;
      const bool bUsesWintersRecallClass = Path == WintersRecallClassPath;
      if (bUsesWintersRecallId != bUsesWintersRecallClass) {
        OutError = FString::Printf(
            TEXT("%s chest content %s violates the exact Winter's Recall "
                 "Stable ID <-> class mapping."),
            Owner, *Entry.StableId.ToString());
        return false;
      }
      if (bUsesWintersRecallId) {
        ++WintersRecallEntryCount;
        // Floor 1 keeps the authored floor gate neutral. Confirmed graveyard
        // eligibility remains the sole progression gate (and normally makes
        // the first effective opportunity Floor 2 after an Advance commit).
        if (Category.Category != EEFCalystoContentCategoryV4::Chest ||
            Entry.Rule != EEFCalystoCatalogRuleV4::Allow ||
            Entry.Tier != EEFCalystoRarityTierV4::Epic ||
            Entry.FirstEligibleFloor != 1 || Entry.MaxPerFloor != 1 ||
            Entry.CooldownFloors != 8 ||
            !Entry.bRequiresGraveyardEligibility) {
          OutError = FString::Printf(
              TEXT("%s Winter's Recall must be one allowed Epic Chest "
                   "Contents entry, graveyard-gated from Floor 1, capped at "
                   "one per floor and cooled down for eight floors."),
              Owner);
          return false;
        }
      }
      if (Entry.Name.TrimStartAndEnd().IsEmpty() || Entry.StableId.IsNone() ||
          ContentIds.Contains(Entry.StableId) ||
          !IsKnownRule(Entry.Rule) || !IsKnownTier(Entry.Tier) ||
          Entry.Tier == EEFCalystoRarityTierV4::Winter ||
          !IsFiniteUnit(Entry.InitialFraction) ||
          !IsFiniteUnit(Entry.DeepShare) || Entry.RampFloors < 0 ||
          Entry.FirstEligibleFloor < 1 || Entry.CooldownFloors < 0 ||
          Entry.MaxPerFloor < 1 || Entry.MaxPerFloor > 3 ||
          (Entry.Rule == EEFCalystoCatalogRuleV4::Allow &&
           (!Path.IsValid() ||
            (!bNativeClass &&
             !FPackageName::DoesPackageExist(Path.GetLongPackageName())) ||
            !IsExactRegisteredActorClass(Path, UObject::StaticClass())))) {
        OutError = FString::Printf(
            TEXT("%s chest content %s is invalid or duplicated."), Owner,
            *Entry.StableId.ToString());
        return false;
      }
      ContentIds.Add(Entry.StableId);
    }
    if (Category.Category == EEFCalystoContentCategoryV4::Chest &&
        WintersRecallEntryCount != 1) {
      OutError = FString::Printf(
          TEXT("%s Chest Contents must contain Winter's Recall exactly once."),
          Owner);
      return false;
    }
    return true;
  };

  float StyleProbabilitySum = 0.0f;
  TSet<EEFCalystoStyleV4> StyleIds;
  for (const FEFCalystoStyleProfileV4 &Style : Styles) {
    StyleProbabilitySum += Style.SelectionProbability;
    if (StyleIds.Contains(Style.Style) ||
        !IsFiniteUnit(Style.SelectionProbability) ||
        Style.SelectionProbability <= 0.0f ||
        !IsFiniteBias(Style.Mystery) ||
        !IsFiniteBias(Style.Danger) ||
        !IsFiniteBias(Style.Safe) ||
        !IsFiniteBias(Style.Abundance) ||
        !IsFiniteBias(Style.ClothingInfluence) ||
        !IsFiniteUnit(Style.Volatility) ||
        !IsFiniteUnit(Style.PlayerAdaptationStrength) ||
        !FMath::IsFinite(Style.WinterTau) || Style.WinterTau <= 0.0f ||
        !ValidateLayout(Style.Layout) || !ValidateLighting(Style.Lighting) ||
        !ValidateDecoration(Style.Decoration) ||
        !ValidateThreat(Style.Threat)) {
      if (OutError.IsEmpty()) {
        OutError = TEXT("A V4 Style profile is invalid or duplicated.");
      }
      return false;
    }
    StyleIds.Add(Style.Style);
    TSet<EEFCalystoContentCategoryV4> Categories;
    TSet<FName> SelectableIds;
    for (const FEFCalystoCategoryProfileV4 &Category : Style.Categories) {
      if (Categories.Contains(Category.Category) ||
          !ValidateCategory(Category, TEXT("Style"), false)) {
        return false;
      }
      Categories.Add(Category.Category);
      for (const FEFCalystoCatalogEntryV4 &Entry : Category.Catalog) {
        if (SelectableIds.Contains(Entry.StableId)) {
          OutError = TEXT("A V4 Style reuses one Stable ID across categories.");
          return false;
        }
        SelectableIds.Add(Entry.StableId);
      }
      for (const FEFCalystoChestContentEntryV4 &Entry :
           Category.ChestContentsCatalog) {
        if (SelectableIds.Contains(Entry.StableId)) {
          OutError = TEXT("A V4 Style reuses one Stable ID across categories.");
          return false;
        }
        SelectableIds.Add(Entry.StableId);
      }
    }
    if (Categories.Num() != 9) {
      OutError =
          TEXT("Every V4 Style must define all nine categories exactly once.");
      return false;
    }
  }
  if (StyleIds.Num() != 3 || !StyleIds.Contains(EEFCalystoStyleV4::Standard) ||
      !StyleIds.Contains(EEFCalystoStyleV4::Compact) ||
      !StyleIds.Contains(EEFCalystoStyleV4::Branching) ||
      !FMath::IsNearlyEqual(StyleProbabilitySum, 1.0f, ProbabilityTolerance)) {
    OutError = TEXT("V4 requires Standard/Compact/Branching and their "
                    "selection probabilities must sum exactly 1.0.");
    return false;
  }

  float ThemeProbabilitySum = 0.0f;
  TSet<EEFCalystoThemeV4> ThemeIds;
  for (const FEFCalystoThemeProfileV4 &Theme : Themes) {
    ThemeProbabilitySum += Theme.SelectionProbability;
    const FSoftObjectPath RoomTypePath = Theme.RoomType.ToSoftObjectPath();
    const bool bRoomTypeRequired = Theme.Theme != EEFCalystoThemeV4::Default;
    const bool bRoomTypeValid =
        Theme.RoomType.IsNull()
            ? !bRoomTypeRequired
            : RoomTypePath.IsValid() && FPackageName::DoesPackageExist(
                                            RoomTypePath.GetLongPackageName());
    if (ThemeIds.Contains(Theme.Theme) || !bRoomTypeValid ||
        !IsFiniteUnit(Theme.SelectionProbability) ||
        Theme.SelectionProbability <= 0.0f ||
        !IsFiniteBias(Theme.Mystery) ||
        !IsFiniteBias(Theme.Danger) ||
        !IsFiniteBias(Theme.Safe) ||
        !IsFiniteBias(Theme.Abundance) ||
        !IsFiniteBias(Theme.ClothingInfluence) ||
        !IsFiniteUnit(Theme.Volatility) ||
        !IsFiniteUnit(Theme.PlayerAdaptationStrength) ||
        !FMath::IsFinite(Theme.WinterTau) || Theme.WinterTau <= 0.0f ||
        !ValidateLayout(Theme.Layout) || !ValidateLighting(Theme.Lighting) ||
        !ValidateDecoration(Theme.Decoration) ||
        !ValidateThreat(Theme.Threat)) {
      if (OutError.IsEmpty()) {
        OutError = TEXT("A V4 Theme profile is invalid or duplicated.");
      }
      return false;
    }
    ThemeIds.Add(Theme.Theme);
    TSet<EEFCalystoContentCategoryV4> Categories;
    TSet<FName> SelectableIds;
    for (const FEFCalystoCategoryProfileV4 &Category : Theme.Categories) {
      if (Categories.Contains(Category.Category) ||
          !ValidateCategory(Category, TEXT("Theme"), true)) {
        return false;
      }
      Categories.Add(Category.Category);
      for (const FEFCalystoCatalogEntryV4 &Entry : Category.Catalog) {
        if (SelectableIds.Contains(Entry.StableId)) {
          OutError = TEXT("A V4 Theme reuses one Stable ID across categories.");
          return false;
        }
        SelectableIds.Add(Entry.StableId);
      }
      for (const FEFCalystoChestContentEntryV4 &Entry :
           Category.ChestContentsCatalog) {
        if (SelectableIds.Contains(Entry.StableId)) {
          OutError = TEXT("A V4 Theme reuses one Stable ID across categories.");
          return false;
        }
        SelectableIds.Add(Entry.StableId);
      }
    }
    if (Categories.Num() != 9) {
      OutError =
          TEXT("Every V4 Theme must define all nine categories exactly once.");
      return false;
    }
  }
  if (ThemeIds.Num() != 3 || !ThemeIds.Contains(EEFCalystoThemeV4::Default) ||
      !ThemeIds.Contains(EEFCalystoThemeV4::Forge) ||
      !ThemeIds.Contains(EEFCalystoThemeV4::Shrine) ||
      !FMath::IsNearlyEqual(ThemeProbabilitySum, 1.0f, ProbabilityTolerance)) {
    OutError = TEXT("V4 requires Default/Forge/Shrine and their selection "
                    "probabilities must sum exactly 1.0.");
    return false;
  }

  for (const FEFCalystoStyleProfileV4 &Style : Styles) {
    for (const FEFCalystoThemeProfileV4 &Theme : Themes) {
      const int32 MinimumEdge = FMath::Max(Style.Layout.MinimumDungeonEdge,
                                           Theme.Layout.MinimumDungeonEdge);
      const int32 MaximumEdge = FMath::Min(Style.Layout.MaximumDungeonEdge,
                                           Theme.Layout.MaximumDungeonEdge);
      const bool bHasCertifiedSize = ValidatedDungeonSizes.ContainsByPredicate(
          [MinimumEdge, MaximumEdge](const int32 Size) {
            return Size >= MinimumEdge && Size <= MaximumEdge;
          });
      if (!bHasCertifiedSize) {
        OutError = TEXT("A V4 Style/Theme pair has no certified Calysto size "
                        "intersection.");
        return false;
      }
      int32 PairMaximum = 0;
      for (const FEFCalystoCategoryProfileV4 &StyleCategory :
           Style.Categories) {
        const FEFCalystoCategoryProfileV4 *ThemeCategory =
            FindCategory(Theme.Categories, StyleCategory.Category);
        if (!ThemeCategory) {
          OutError = TEXT("Style/Theme category surfaces do not match.");
          return false;
        }
        const int32 EffectiveMin =
            FMath::Max(StyleCategory.Limits.MinimumWhenPresent,
                       ThemeCategory->Limits.MinimumWhenPresent);
        const int32 EffectiveMax =
            FMath::Min(StyleCategory.Limits.MaximumPerFloor,
                       ThemeCategory->Limits.MaximumPerFloor);
        if (EffectiveMin > EffectiveMax) {
          OutError =
              TEXT("A Style/Theme category limit intersection is empty.");
          return false;
        }
        PairMaximum += EffectiveMax;
      }
      if (PairMaximum > SafetyCeilings.MaximumDirectorActors) {
        OutError =
            TEXT("A Style/Theme pair exceeds the 89 initial-actor ceiling.");
        return false;
      }
    }
  }
  return true;
}

FString UEFCalystoDungeonDirectorPolicyV4::BuildCanonicalString() const {
  using namespace EFCalystoPolicyV4Private;
  FString Result = FString::Printf(
      TEXT("EFCalystoDirectorPolicyV4|%d|%d|%s|SAFE:%d:%d:%d:%d:%d:%d:%d:%d:%"
           "s|"),
      SchemaVersion, GeneratorVersion, *PolicyId.ToString(),
      SafetyCeilings.MaximumEnemies, SafetyCeilings.MaximumNPCs,
      SafetyCeilings.MaximumFood, SafetyCeilings.MaximumChests,
      SafetyCeilings.MaximumLooseLoot, SafetyCeilings.MaximumClothing,
      SafetyCeilings.MaximumSpecialEvents, SafetyCeilings.MaximumDirectorActors,
      *FloatBits(SafetyCeilings.MaximumThreatBudget));
  TArray<int32> Sizes = ValidatedDungeonSizes;
  Sizes.Sort();
  for (const int32 Size : Sizes) {
    Result += FString::Printf(TEXT("SIZE:%d|"), Size);
  }

  auto AppendTierMix = [&Result](const TCHAR *Prefix,
                                 const FEFCalystoTierMixV4 &Mix) {
    Result += FString::Printf(TEXT("%s:%s:%s:%s:%s:%s|"), Prefix,
                              *FloatBits(Mix.Common), *FloatBits(Mix.Uncommon),
                              *FloatBits(Mix.Rare), *FloatBits(Mix.Epic),
                              *FloatBits(Mix.GetCalculatedNothing()));
  };
  auto AppendCategory = [&Result, &AppendTierMix](
                            const FEFCalystoCategoryProfileV4 &Category,
                            const bool bThemeOwnsEnabled) {
    Result += FString::Printf(
        TEXT("CAT:%d:%d:%d:%s:%s:%s:%d:%d:%d:%d:%d|"),
        static_cast<int32>(Category.Category),
        !bThemeOwnsEnabled || Category.bEnabled ? 1 : 0,
        Category.bBlocked ? 1 : 0, *FloatBits(Category.Chance.ChanceAtFloor1),
        *FloatBits(Category.Chance.ChanceAtFloor100),
        *FloatBits(Category.Chance.Tau), Category.Limits.MinimumWhenPresent,
        Category.Limits.MaximumPerFloor, Category.PityAfterEmptyFloors,
        Category.MinimumChestContentAttempts,
        Category.MaximumChestContentAttempts);
    AppendTierMix(TEXT("T1"), Category.Tiers.AtFloor1);
    AppendTierMix(TEXT("T100"), Category.Tiers.AtFloor100);
    TArray<FEFCalystoCatalogEntryV4> Catalog = Category.Catalog;
    Catalog.Sort([](const FEFCalystoCatalogEntryV4 &A,
                    const FEFCalystoCatalogEntryV4 &B) {
      return A.StableId.LexicalLess(B.StableId);
    });
    for (const FEFCalystoCatalogEntryV4 &Entry : Catalog) {
      Result += FString::Printf(
          TEXT("ENTRY:%s:%s:%d:%s:%d:%d:%s:%d:%s:%s:%d:%s:%d:%lld:%d:%d:%d|"),
          *FEFCalystoDungeonDirectorMathV4::HashCanonicalText(Entry.Name),
          *Entry.StableId.ToString(),
          static_cast<int32>(Entry.Rule),
          *Entry.ActorClass.ToSoftObjectPath().ToString(),
          static_cast<int32>(Entry.Tier), Entry.bTierAgnostic ? 1 : 0,
          *Entry.Archetype.ToString(), static_cast<int32>(Entry.Gender),
          *FloatBits(Entry.InitialFraction), *FloatBits(Entry.DeepShare),
          Entry.RampFloors, *FloatBits(Entry.BaseThreatCost),
          Entry.ReferenceLevel, Entry.FirstEligibleFloor, Entry.MaxPerVariant,
          Entry.CooldownFloors, static_cast<int32>(Entry.Lifecycle));
      TArray<EEFCalystoRarityTierV4> Allowed = Entry.AllowedTiers;
      Allowed.Sort(
          [](const EEFCalystoRarityTierV4 A, const EEFCalystoRarityTierV4 B) {
            return static_cast<uint8>(A) < static_cast<uint8>(B);
          });
      for (const EEFCalystoRarityTierV4 Tier : Allowed) {
        Result += FString::Printf(TEXT("ALLOW:%d|"), static_cast<int32>(Tier));
      }
    }
    TArray<FEFCalystoChestContentEntryV4> Contents =
        Category.ChestContentsCatalog;
    Contents.Sort([](const FEFCalystoChestContentEntryV4 &A,
                     const FEFCalystoChestContentEntryV4 &B) {
      return A.StableId.LexicalLess(B.StableId);
    });
    for (const FEFCalystoChestContentEntryV4 &Entry : Contents) {
      Result += FString::Printf(
          TEXT("CHESTCONTENT:%s:%s:%d:%s:%d:%s:%s:%d:%lld:%d:%d:%d|"),
          *FEFCalystoDungeonDirectorMathV4::HashCanonicalText(Entry.Name),
          *Entry.StableId.ToString(),
          static_cast<int32>(Entry.Rule),
          *Entry.ContentClass.ToSoftObjectPath().ToString(),
          static_cast<int32>(Entry.Tier), *FloatBits(Entry.InitialFraction),
          *FloatBits(Entry.DeepShare), Entry.RampFloors,
          Entry.FirstEligibleFloor, Entry.CooldownFloors, Entry.MaxPerFloor,
          Entry.bRequiresGraveyardEligibility ? 1 : 0);
    }
  };
  auto AppendCalystoNativeSurface =
      [&Result](const FEFCalystoLightingPolicyV4 &L,
                const FEFCalystoDecorationPolicyV4 &D) {
        Result += FString::Printf(
            TEXT("LIGHT:%d:%s:%s:%d:%d|DECO:%d|"), static_cast<int32>(L.Mode),
            *L.TorchClass.ToSoftObjectPath().ToString(), *FloatBits(L.Height),
            L.TileDistance, 1,
            D.bUseCalystoNativeArrays ? 1 : 0);
        TArray<FName> Arrays = D.TransientAllowlistedArrayIds;
        Arrays.Sort(FNameLexicalLess());
        for (const FName ArrayId : Arrays) {
          Result += FString::Printf(TEXT("DECOARRAY:%s|"), *ArrayId.ToString());
        }
      };
  auto AppendTraits = [&Result](const FEFCalystoContextTraitsV4 &T) {
    Result += FString::Printf(
        TEXT("TRAIT:%s:%s:%s:%s:%s:%s:%s:%s|"), *FloatBits(T.Scale),
        *FloatBits(T.Branching), *FloatBits(T.Mystery), *FloatBits(T.Danger),
        *FloatBits(T.Safe), *FloatBits(T.Abundance),
        *FloatBits(T.ClothingInfluence), *FloatBits(T.Volatility));
  };
  auto AppendLayout = [&Result](const FEFCalystoLayoutProfileV4 &L) {
    Result += FString::Printf(
        TEXT("LAY:%d:%d:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s|"),
        L.MinimumDungeonEdge, L.MaximumDungeonEdge, *FloatBits(L.EarlySizeMode),
        *FloatBits(L.DeepSizeMode), *FloatBits(L.SizeHalfRange),
        *FloatBits(L.SizeTau), *FloatBits(L.CandidateAnchorDensity.Min),
        *FloatBits(L.CandidateAnchorDensity.Mode),
        *FloatBits(L.CandidateAnchorDensity.Max),
        *FloatBits(L.CandidateAnchorDensity.Concentration),
        *FloatBits(L.BranchingChance.ChanceAtFloor1),
        *FloatBits(L.BranchingChance.ChanceAtFloor100),
        *FloatBits(L.BranchingChance.Tau));
  };

  TArray<FEFCalystoStyleProfileV4> SortedStyles = Styles;
  SortedStyles.Sort([](const auto &A, const auto &B) {
    return static_cast<uint8>(A.Style) < static_cast<uint8>(B.Style);
  });
  for (const FEFCalystoStyleProfileV4 &Style : SortedStyles) {
    Result += FString::Printf(
        TEXT("STYLE:%d:%s:%s:%s:%s:%s:%s:%s|"), static_cast<int32>(Style.Style),
        *FloatBits(Style.SelectionProbability),
        *FloatBits(Style.PlayerAdaptationStrength), *FloatBits(Style.WinterTau),
        *FloatBits(Style.Threat.EarlyBudget),
        *FloatBits(Style.Threat.DeepBudget), *FloatBits(Style.Threat.Tau),
        *FloatBits(Style.Threat.RelativeRange));
    AppendTraits(Style.GetAuthoredTraits());
    AppendLayout(Style.Layout);
    AppendCalystoNativeSurface(Style.Lighting, Style.Decoration);
    TArray<FEFCalystoCategoryProfileV4> Categories = Style.Categories;
    Categories.Sort([](const auto &A, const auto &B) {
      return static_cast<uint8>(A.Category) < static_cast<uint8>(B.Category);
    });
    for (const FEFCalystoCategoryProfileV4 &Category : Categories) {
      AppendCategory(Category, false);
    }
  }
  TArray<FEFCalystoThemeProfileV4> SortedThemes = Themes;
  SortedThemes.Sort([](const auto &A, const auto &B) {
    return static_cast<uint8>(A.Theme) < static_cast<uint8>(B.Theme);
  });
  for (const FEFCalystoThemeProfileV4 &Theme : SortedThemes) {
    Result += FString::Printf(
        TEXT("THEME:%d:%s:%s:%s:%s:%s:%s:%s:%s|"),
        static_cast<int32>(Theme.Theme), *FloatBits(Theme.SelectionProbability),
        *Theme.RoomType.ToSoftObjectPath().ToString(),
        *FloatBits(Theme.PlayerAdaptationStrength), *FloatBits(Theme.WinterTau),
        *FloatBits(Theme.Threat.EarlyBudget),
        *FloatBits(Theme.Threat.DeepBudget), *FloatBits(Theme.Threat.Tau),
        *FloatBits(Theme.Threat.RelativeRange));
    AppendTraits(Theme.GetAuthoredTraits());
    AppendLayout(Theme.Layout);
    AppendCalystoNativeSurface(Theme.Lighting, Theme.Decoration);
    TArray<FEFCalystoCategoryProfileV4> Categories = Theme.Categories;
    Categories.Sort([](const auto &A, const auto &B) {
      return static_cast<uint8>(A.Category) < static_cast<uint8>(B.Category);
    });
    for (const FEFCalystoCategoryProfileV4 &Category : Categories) {
      AppendCategory(Category, true);
    }
  }
  return Result;
}
