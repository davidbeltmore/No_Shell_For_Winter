#pragma once

#include "CoreMinimal.h"
#include "EFCalystoDungeonTypesV4.generated.h"

class AActor;

/** Style identity in V4. Standard is an ordinary style, never a global fallback
 * profile. */
UENUM(BlueprintType)
enum class EEFCalystoStyleV4 : uint8 {
  Standard UMETA(DisplayName = "Standard"),
  Compact UMETA(DisplayName = "Compact"),
  Branching UMETA(DisplayName = "Branching")
};

/** Theme identity in V4. Default is an ordinary theme with neutral Calysto
 * topology. */
UENUM(BlueprintType)
enum class EEFCalystoThemeV4 : uint8 {
  Default UMETA(DisplayName = "Default"),
  Forge UMETA(DisplayName = "Forge"),
  Shrine UMETA(DisplayName = "Shrine")
};

UENUM(BlueprintType)
enum class EEFCalystoContentCategoryV4 : uint8 {
  Enemy UMETA(DisplayName = "Enemies"),
  NPC UMETA(DisplayName = "NPCs"),
  Food UMETA(DisplayName = "Food"),
  Chest UMETA(DisplayName = "Chests"),
  LooseLoot UMETA(DisplayName = "Loose Loot"),
  Clothing UMETA(DisplayName = "Clothing"),
  SpecialEvent UMETA(DisplayName = "Special Events"),
  Decoration UMETA(DisplayName = "Decoration"),
  Lighting UMETA(DisplayName = "Lighting")
};

UENUM(BlueprintType)
enum class EEFCalystoRarityTierV4 : uint8 {
  Common UMETA(DisplayName = "Common"),
  Uncommon UMETA(DisplayName = "Uncommon"),
  Rare UMETA(DisplayName = "Rare"),
  Epic UMETA(DisplayName = "Epic"),
  Winter UMETA(DisplayName = "Winter")
};

/** Source-local catalog rule. A floor first chooses its Style or Theme catalog;
 * inside that source Allow participates, Block excludes and Neutral contributes
 * no entry. Catalogs are never silently unioned or used as fallbacks. */
UENUM(BlueprintType)
enum class EEFCalystoCatalogRuleV4 : uint8 {
  Neutral UMETA(DisplayName = "Neutral"),
  Allow UMETA(DisplayName = "Allow"),
  Block UMETA(DisplayName = "Block")
};

UENUM(BlueprintType)
enum class EEFCalystoGenderV4 : uint8 {
  Any UMETA(DisplayName = "Any"),
  Female UMETA(DisplayName = "Female"),
  Male UMETA(DisplayName = "Male")
};

UENUM(BlueprintType)
enum class EEFCalystoLifecycleV4 : uint8 {
  FloorLocal UMETA(DisplayName = "Floor Local"),
  Recruitable UMETA(DisplayName = "Recruitable")
};

UENUM(BlueprintType)
enum class EEFCalystoCompanionRosterStateV4 : uint8 {
  ActiveParty UMETA(DisplayName = "Active Party"),
  RecruitedInactive UMETA(DisplayName = "Recruited Inactive"),
  Dead UMETA(DisplayName = "Confirmed Dead"),
  PendingDead UMETA(DisplayName = "Pending Death")
};

/**
 * One complete normal categorical tier roll. The four selectable tiers may
 * consume at most 90 percent. Nothing is derived as 1-sum and is intentionally
 * visible but not editable. Winter is a separate post-100 pool and never
 * competes inside this pre-Winter authoring surface.
 */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoTierMixV4 {
  GENERATED_BODY()

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Probability",
      meta = (ClampMin = "0.0", ClampMax = "0.90", DisplayName = "Common",
              ToolTip = "Common probability. The sum of Common, Uncommon, Rare, and Epic may never exceed 0.90."))
  float Common = 0.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Probability",
      meta =
          (ClampMin = "0.0", ClampMax = "0.90", DisplayName = "Uncommon",
           ToolTip = "Uncommon probability within the catalog roll."))
  float Uncommon = 0.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Probability",
      meta = (ClampMin = "0.0", ClampMax = "0.90", DisplayName = "Rare",
              ToolTip = "Rare probability within the catalog roll."))
  float Rare = 0.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Probability",
      meta = (ClampMin = "0.0", ClampMax = "0.90", DisplayName = "Epic",
              ToolTip = "Epic probability within the catalog roll."))
  float Epic = 0.0f;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Probability",
      meta = (DisplayName = "Nothing (Calculated)",
              ToolTip =
                  "Derived probability of selecting nothing: 1 minus the sum of all tiers. It may never be lower than 0.10."))
  float Nothing = 1.0f;

  float GetSelectableMass() const;
  float GetCalculatedNothing() const;
  float Get(EEFCalystoRarityTierV4 Tier) const;
  void Set(EEFCalystoRarityTierV4 Tier, float Value);
  void RefreshNothing();
};

/** Normal-tier probabilities at Floors 1 and 100. Convex interpolation
 * preserves the 90 percent invariant. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoTierCurveV4 {
  GENERATED_BODY()

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Tiers",
      meta =
          (DisplayName = "Floor 1",
           ToolTip =
               "Normal-tier distribution at the beginning of progression."))
  FEFCalystoTierMixV4 AtFloor1;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiers",
            meta = (DisplayName = "Floor 100",
                    ToolTip = "Normal-tier distribution at the standard ACF level limit of 100."))
  FEFCalystoTierMixV4 AtFloor100;

  void RefreshNothing();
};

/** Per-context probability curve. There is no global probability authority in
 * V4. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoChanceCurveV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability",
            meta = (ClampMin = "0.0", ClampMax = "0.90",
                    DisplayName = "Chance at Floor 1",
                    ToolTip = "Opportunity probability at Floor 1, before applying tiers and Nothing."))
  float ChanceAtFloor1 = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability",
            meta = (ClampMin = "0.0", ClampMax = "0.90",
                    DisplayName = "Chance at Floor 100",
                    ToolTip = "Opportunity probability at deep floors, before applying tiers and Nothing."))
  float ChanceAtFloor100 = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probability",
            meta = (ClampMin = "0.1", ClampMax = "100.0",
                    DisplayName = "Progression Speed",
                    ToolTip = "Tau of the saturating curve. A lower value reaches the deep-floor chance sooner."))
  float Tau = 12.0f;
};

/** Limits are conditional safety constraints; zero content is produced by the
 * probability roll. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCategoryLimitsV4 {
  GENERATED_BODY()

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Limits",
      meta =
          (ClampMin = "0", ClampMax = "30",
            DisplayName = "Minimum When Active",
           ToolTip =
                "Minimum amount only after the category passes its effective presence roll. This does not remove the possibility of zero."))
  int32 MinimumWhenPresent = 1;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits",
            meta = (ClampMin = "0", ClampMax = "30",
                    DisplayName = "Maximum Per Floor",
                    ToolTip = "Exact actor ceiling for this category in this Style or Theme."))
  int32 MaximumPerFloor = 1;
};

/** Project-owned catalog rule stored inside one Style or Theme category. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCatalogEntryV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog",
            meta = (DisplayName = "Name",
                    ToolTip = "Human-readable authoring name. The Stable ID remains the deterministic identity."))
  FString Name;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Catalog",
      meta = (DisplayName = "Stable ID",
              ToolTip = "Canonical identifier. It does not depend on array order and participates in every deterministic hash."))
  FName StableId = NAME_None;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog",
            meta = (DisplayName = "Rule",
                    ToolTip = "Within this catalog: Allow contributes the variant, Block excludes it, and Neutral contributes nothing. This does not veto the other catalog."))
  EEFCalystoCatalogRuleV4 Rule = EEFCalystoCatalogRuleV4::Allow;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog",
            meta = (AssetBundles = "CalystoFloorV4",
                    DisplayName = "Actor Class",
                    ToolTip = "Project-owned soft class loaded only when the floor selects it."))
  TSoftClassPtr<AActor> ActorClass;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Catalog",
      meta = (DisplayName = "Tier",
              ToolTip = "Fixed tier for this variant when it is not tier-agnostic."))
  EEFCalystoRarityTierV4 Tier = EEFCalystoRarityTierV4::Common;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite,
      Category = "Catalog|Advanced Director Behavior",
      meta = (DisplayName = "Tier Agnostic",
              ToolTip = "Enemy and NPC entries allow the tier to resolve difficulty and level before choosing archetype and gender.",
              AdvancedDisplay))
  bool bTierAgnostic = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog",
            meta = (DisplayName = "Allowed Tiers",
                    ToolTip = "An empty list means all tiers when Tier Agnostic is enabled."))
  TArray<EEFCalystoRarityTierV4> AllowedTiers;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog",
            meta = (DisplayName = "Archetype",
                    ToolTip = "Stable family used for ratios, caps, and telemetry. It does not depend on the class name."))
  FName Archetype = NAME_None;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog",
            meta = (DisplayName = "Gender",
                    ToolTip = "Authoritative gender for this variant."))
  EEFCalystoGenderV4 Gender = EEFCalystoGenderV4::Any;

  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            Category = "Catalog|Advanced Director Behavior",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    DisplayName = "Initial Multiplier",
                    ToolTip = "Multiplies the deep share when the variant becomes eligible, then progresses to 1 over Ramp Floors.",
                    AdvancedDisplay))
  float InitialFraction = 1.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            Category = "Catalog|Advanced Director Behavior",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    DisplayName = "Deep Share",
                    ToolTip = "Final weight of this variant within its tier after the ramp completes.",
                    AdvancedDisplay))
  float DeepShare = 1.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            Category = "Catalog|Advanced Director Behavior",
            meta = (ClampMin = "0", ClampMax = "1000",
                    DisplayName = "Ramp Floors",
                    ToolTip = "Number of floors used to move from the initial multiplier to the deep share.",
                    AdvancedDisplay))
  int32 RampFloors = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog|Threat",
            meta = (ClampMin = "0.01", ClampMax = "100.0",
                    DisplayName = "Base Threat Cost",
                    ToolTip = "Cost calibrated at the Reference Level. The tier does not multiply it again."))
  float BaseThreatCost = 1.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite,
      Category = "Catalog|Advanced Director Behavior",
      meta = (ClampMin = "1", ClampMax = "100",
              DisplayName = "Reference Level",
              ToolTip = "Level used only for calibration and evidence; V4 resolves cost against the floor level.",
              AdvancedDisplay))
  int32 ReferenceLevel = 50;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Catalog|Rules",
      meta = (ClampMin = "1", ClampMax = "1000000",
              DisplayName = "First Eligible Floor",
              ToolTip =
                  "Inclusive floor gate. Before this floor, the item has zero selection probability; this gate is not multiplied as another probability."))
  int64 FirstEligibleFloor = 1;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog|Rules",
            meta = (ClampMin = "1", ClampMax = "30",
                    DisplayName = "Maximum Per Variant",
                    ToolTip = "Per-floor cap for this Stable ID; it does not increase its probability."))
  int32 MaxPerVariant = 1;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catalog|Rules",
            meta = (ClampMin = "0", ClampMax = "100",
                    DisplayName = "Cooldown Floors",
                    ToolTip = "Confirmed floors that must pass before this variant may be selected again."))
  int32 CooldownFloors = 0;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Catalog|Rules",
      meta = (DisplayName = "Lifecycle",
              ToolTip = "Floor Local actors are destroyed with the floor; Recruitable actors may enter the persistent roster."))
  EEFCalystoLifecycleV4 Lifecycle = EEFCalystoLifecycleV4::FloorLocal;
};

/** Content selected for a chest container. It is not materialized as a loose
 * actor by the Director. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoChestContentEntryV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Content",
            meta = (DisplayName = "Name",
                    ToolTip = "Human-readable content name. It does not replace the Stable ID used for determinism."))
  FString Name;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Content",
            meta = (DisplayName = "Stable ID",
                    ToolTip = "Canonical identifier for the chest item."))
  FName StableId = NAME_None;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Chest Content",
      meta =
          (DisplayName = "Rule",
           ToolTip =
               "Allow contributes, Block excludes, and Neutral does not contribute this content."))
  EEFCalystoCatalogRuleV4 Rule = EEFCalystoCatalogRuleV4::Allow;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Content",
            meta = (AssetBundles = "CalystoFloorV4",
                    DisplayName = "Item Class",
                    ToolTip = "Soft item class; the bridge validates UACFItem without coupling EFProcedural to ACF."))
  TSoftClassPtr<UObject> ContentClass;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Content",
            meta = (DisplayName = "Tier",
                    ToolTip = "Exact tier that must win the roll to make this content eligible."))
  EEFCalystoRarityTierV4 Tier = EEFCalystoRarityTierV4::Common;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite,
      Category = "Chest Content|Advanced Director Behavior",
      meta = (ClampMin = "0.0", ClampMax = "1.0",
              DisplayName = "Initial Multiplier",
              ToolTip = "Weight multiplier when this entry first becomes eligible.",
              AdvancedDisplay))
  float InitialFraction = 1.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            Category = "Chest Content|Advanced Director Behavior",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    DisplayName = "Deep Share",
                    ToolTip = "Final conditional weight within its tier.",
                    AdvancedDisplay))
  float DeepShare = 1.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            Category = "Chest Content|Advanced Director Behavior",
            meta = (ClampMin = "0", ClampMax = "1000",
                    DisplayName = "Ramp Floors",
                    ToolTip = "Floors required to reach the deep share.",
                    AdvancedDisplay))
  int32 RampFloors = 0;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Chest Content",
      meta =
          (ClampMin = "1", ClampMax = "1000000",
           DisplayName = "First Eligible Floor",
           ToolTip = "Inclusive deterministic gate; this is not another probability."))
  int64 FirstEligibleFloor = 1;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Content",
            meta = (ClampMin = "0", ClampMax = "100",
                    DisplayName = "Cooldown Floors",
                    ToolTip = "Confirmed floors required before this entry may repeat."))
  int32 CooldownFloors = 0;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Chest Content",
      meta =
          (ClampMin = "1", ClampMax = "3", DisplayName = "Maximum Per Floor",
           ToolTip = "Frozen cap per Stable ID; Winter's Recall uses one."))
  int32 MaxPerFloor = 3;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest Content",
            meta = (DisplayName = "Requires Graveyard Eligibility",
                    ToolTip = "The resolver excludes this entry when the frozen snapshot has no eligible companion to rescue."))
  bool bRequiresGraveyardEligibility = false;
};

/** Shared authoring for one content family inside one Style or Theme. Category
 * activation is Theme-owned; bEnabled is hidden and ignored for Style entries. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCategoryProfileV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category",
            meta = (DisplayName = "Category",
                    ToolTip = "Content family controlled by this block. Every V4 profile must contain it exactly once."))
  EEFCalystoContentCategoryV4 Category = EEFCalystoContentCategoryV4::Enemy;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category",
            meta = (DisplayName = "Enabled",
                    ToolTip = "Theme-only activation gate. A disabled Theme category performs no rolls and materializes no generated actors; the legacy Style value is hidden and ignored."))
  bool bEnabled = true;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category",
            meta = (DisplayName = "Block Combination",
                    ToolTip = "Explicit veto. If either the Style or Theme blocks this category, the combined result is zero."))
  bool bBlocked = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category",
            meta = (DisplayName = "Chance",
                    ToolTip = "Activation probability for this category from Floor 1 into deep progression."))
  FEFCalystoChanceCurveV4 Chance;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category",
            meta = (DisplayName = "Limits",
                    ToolTip = "Conditional minimum and exact maximum authored for this Style or Theme."))
  FEFCalystoCategoryLimitsV4 Limits;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Category|Probability",
      meta =
          (ClampMin = "0", ClampMax = "20",
           DisplayName = "Pity After Empty Floors",
           ToolTip = "0 disables pity. Food defaults to 2 and Chest to 4 within each Style and Theme; pity never raises chance above 0.90."))
  int32 PityAfterEmptyFloors = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category",
            meta = (DisplayName = "Tier Probabilities",
                    ToolTip = "Common, Uncommon, Rare, and Epic; Nothing is calculated automatically."))
  FEFCalystoTierCurveV4 Tiers;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category",
            meta = (TitleProperty = "Name", AssetBundles = "CalystoFloorV4",
                    DisplayName = "Catalog",
                    ToolTip = "Catalog entries owned by this category and profile."))
  TArray<FEFCalystoCatalogEntryV4> Catalog;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Category",
      meta = (TitleProperty = "Name", AssetBundles = "CalystoFloorV4",
              DisplayName = "Chest Contents",
              ToolTip = "Conditional catalog separate from the container actor. Valid only for the Chests category."))
  TArray<FEFCalystoChestContentEntryV4> ChestContentsCatalog;

  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            Category = "Category|Chest Content",
            meta = (ClampMin = "0", ClampMax = "3",
                    DisplayName = "Minimum Content Attempts",
                    ToolTip = "Minimum internal rolls per chest. This may be zero and never materializes loose loot."))
  int32 MinimumChestContentAttempts = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            Category = "Category|Chest Content",
            meta = (ClampMin = "0", ClampMax = "3",
                    DisplayName = "Maximum Content Attempts",
                    ToolTip = "Maximum internal rolls per chest. Every roll preserves its Nothing probability."))
  int32 MaximumChestContentAttempts = 0;

  void RefreshNothing();
};

/** Macro traits have disjoint responsibilities and never duplicate category
 * chance. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoContextTraitsV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits",
            meta = (ClampMin = "-1.0", ClampMax = "1.0", DisplayName = "Scale",
                    ToolTip = "Normalized personality contribution to dungeon size."))
  float Scale = 0.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Traits",
      meta =
          (ClampMin = "-1.0", ClampMax = "1.0", DisplayName = "Branching",
           ToolTip =
               "Normalized personality contribution to side paths."))
  float Branching = 0.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Traits",
      meta =
          (ClampMin = "-1.0", ClampMax = "1.0", DisplayName = "Mystery",
            ToolTip = "Modifies chests and redistributes existing mass toward higher or lower tiers without changing total selectable mass."))
  float Mystery = 0.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Traits",
      meta = (ClampMin = "-1.0", ClampMax = "1.0", DisplayName = "Danger",
              ToolTip =
                  "Coherently controls enemy presence and difficulty through one resolved influence."))
  float Danger = 0.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Traits",
      meta =
          (ClampMin = "-1.0", ClampMax = "1.0", DisplayName = "Safe",
           ToolTip = "Applies a bounded change to safe-NPC opportunity."))
  float Safe = 0.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Traits",
      meta = (ClampMin = "-1.0", ClampMax = "1.0", DisplayName = "Abundance",
              ToolTip = "Applies a bounded change to food opportunity."))
  float Abundance = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits",
            meta = (ClampMin = "-1.0", ClampMax = "1.0",
                    DisplayName = "Clothing Influence",
                    ToolTip = "Applies a bounded change to clothing opportunity."))
  float ClothingInfluence = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traits",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    DisplayName = "Volatility",
                    ToolTip = "Controls how strongly each floor may lean toward the Style or Theme. It does not alter limits."))
  float Volatility = 0.5f;
};

UENUM(BlueprintType)
enum class EEFCalystoLightingModeV4 : uint8 {
  CalystoNative UMETA(DisplayName = "Calysto Native")
};

/**
 * Lighting remains Calysto-native and is never materialized as population by
 * the Director.  V4 resolves these values per floor and applies them only to
 * the transient BP_MassiveDungeon runtime instance, before Calysto's single
 * native generation pass.
 */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoLightingPolicyV4 {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lighting",
            meta = (DisplayName = "Mode",
                    ToolTip = "Lighting is generated only by Calysto. V4 resolves native settings for the transient runtime dungeon; it never spawns a second torch population."))
  EEFCalystoLightingModeV4 Mode = EEFCalystoLightingModeV4::CalystoNative;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lighting",
            meta = (DisplayName = "Torch Class",
                    ToolTip = "The native Calysto torch class. It is displayed for auditing and is not replaced by V4.",
                    AssetBundles = "CalystoFloorV4"))
  TSoftClassPtr<AActor> TorchClass;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting",
            meta = (DisplayName = "Native Wall Light Height", ClampMin = "0.0",
                    ClampMax = "1000.0",
                    ToolTip = "Written to Calysto's native Wall Light Height on the transient runtime dungeon. Style and Theme are blended deterministically for each floor."))
  float Height = 200.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting",
            meta = (DisplayName = "Native Torch Tile Interval", ClampMin = "1",
                    ClampMax = "100",
                    ToolTip = "Written to Calysto's native Wall Light Tile Distance before its single PCG pass. Lower values create more native torch opportunities; 5 is valid. Style and Theme produce a deterministic per-floor interval between their values."))
  int32 TileDistance = 10;
};

/** Decoration only selects allowlisted arrays on transient Calysto clones. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDecorationPolicyV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decoration",
            meta = (DisplayName = "Use Calysto Native Arrays",
                    ToolTip = "Keeps decoration within the arrays Calysto already processes on its transient clone."))
  bool bUseCalystoNativeArrays = true;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decoration",
            meta = (DisplayName = "Allowed Transient Arrays",
                    ToolTip = "Project-owned IDs authorized to vary arrays on the transient clone; this never modifies a Calysto Data Asset."))
  TArray<FName> TransientAllowlistedArrayIds;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoPertRangeV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution",
            meta = (DisplayName = "Minimum",
                    ToolTip = "Inclusive lower endpoint of the PERT distribution."))
  float Min = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution",
            meta = (DisplayName = "Most Likely",
                    ToolTip = "Modal value favored by the distribution."))
  float Mode = 0.5f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution",
            meta = (DisplayName = "Maximum",
                    ToolTip = "Inclusive upper endpoint of the PERT distribution."))
  float Max = 1.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution",
            meta = (ClampMin = "2.0", ClampMax = "8.0",
                    DisplayName = "Predictability",
                    ToolTip = "8 concentrates results near the most likely value; 2 produces greater dispersion."))
  float Concentration = 4.0f;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoLayoutProfileV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout",
            meta = (ClampMin = "18", ClampMax = "30",
                    DisplayName = "Minimum Size",
                    ToolTip = "Lower bound for this profile; only entries in Validated Dungeon Sizes may resolve."))
  int32 MinimumDungeonEdge = 18;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout",
            meta = (ClampMin = "18", ClampMax = "30",
                    DisplayName = "Maximum Size",
                    ToolTip = "Upper bound for this profile, never greater than 30."))
  int32 MaximumDungeonEdge = 30;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout",
            meta = (ClampMin = "18.0", ClampMax = "30.0",
                    DisplayName = "Typical Size at Floor 1",
                    ToolTip = "Modal size at Floor 1; this does not force an exact value."))
  float EarlySizeMode = 20.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout",
            meta = (ClampMin = "18.0", ClampMax = "30.0",
                    DisplayName = "Typical Size at Floor 100",
                    ToolTip = "Modal size toward Floor 100; progression is saturating."))
  float DeepSizeMode = 30.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            Category = "Advanced Director Behavior",
            meta = (ClampMin = "0.0", ClampMax = "6.0",
                    DisplayName = "Size Half Range",
                    ToolTip = "Half-width of the PERT distribution around the typical size.",
                    AdvancedDisplay))
  float SizeHalfRange = 2.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Advanced Director Behavior",
      meta =
          (ClampMin = "0.1", ClampMax = "100.0",
           DisplayName = "Scale Progression Speed",
           ToolTip =
               "Saturating tau: a lower value reaches the deep-floor size sooner.",
           AdvancedDisplay))
  float SizeTau = 12.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout",
            meta = (DisplayName = "Anchor Density",
                    ToolTip = "PERT distribution for structural population anchors. This is not an enemy count and may never be zero."))
  FEFCalystoPertRangeV4 CandidateAnchorDensity = {0.25f, 0.32f, 0.45f, 4.0f};

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout",
            meta = (DisplayName = "Branching Chance",
                    ToolTip = "Branching probability from Floor 1 through Floor 100."))
  FEFCalystoChanceCurveV4 BranchingChance = {0.45f, 0.55f, 12.0f};
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoThreatCurveV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat",
            meta = (ClampMin = "0.0", ClampMax = "60.0",
                    DisplayName = "Threat Budget at Floor 1",
                    ToolTip = "Modal threat budget at Floor 1."))
  float EarlyBudget = 6.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat",
            meta = (ClampMin = "0.0", ClampMax = "60.0",
                    DisplayName = "Threat Budget at Floor 100",
                    ToolTip = "Modal threat budget toward Floor 100."))
  float DeepBudget = 50.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat",
            meta = (ClampMin = "0.1", ClampMax = "100.0",
                    DisplayName = "Threat Progression Speed",
                    ToolTip = "Saturating progression tau for the threat budget."))
  float Tau = 10.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Advanced Director Behavior",
      meta = (ClampMin = "0.0", ClampMax = "0.50",
              DisplayName = "Relative Range",
              ToolTip = "Relative PERT dispersion around the modal budget.",
              AdvancedDisplay))
  float RelativeRange = 0.15f;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoStyleProfileV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (DisplayName = "Style",
                    ToolTip = "Unique identity of this profile. Standard is an ordinary Style, not a global layer."))
  EEFCalystoStyleV4 Style = EEFCalystoStyleV4::Standard;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    DisplayName = "Selection Probability",
                    ToolTip = "Selection probabilities across all Styles must total exactly 1.0."))
  float SelectionProbability = 0.5f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    DisplayName = "Volatility",
                    ToolTip = "Controls probabilistic dispersion between this Style and the Theme. 0 is stable and 1 is highly variable; limits are never altered."))
  float Volatility = 0.5f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    DisplayName = "Player Adaptation Strength",
                    ToolTip = "Scales EMA influence for this Style without exceeding the V4 cap."))
  float PlayerAdaptationStrength = 1.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (ClampMin = "1.0", ClampMax = "1000.0",
                    DisplayName = "Tau Winter",
                    ToolTip = "Winter uses 0.90*(1-exp(-(Floor-100)/TauWinter)) after Floor 100."))
  float WinterTau = 100.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Style",
      meta =
          (DisplayName = "Layout",
            ToolTip = "Probabilistic shape ranges owned by this Style."))
  FEFCalystoLayoutProfileV4 Layout;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Style",
      meta = (DisplayName = "Threat",
              ToolTip = "Threat-budget curve owned by this Style."))
  FEFCalystoThreatCurveV4 Threat;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (TitleProperty = "Category", DisplayName = "Categories",
                    ToolTip = "Probability, tiers, limits, and catalog per category for this Style. Enabled is Theme-owned and is hidden here."))
  TArray<FEFCalystoCategoryProfileV4> Categories;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (DisplayName = "Lighting",
                    ToolTip = "Calysto-native lighting configuration for this Style."))
  FEFCalystoLightingPolicyV4 Lighting;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (DisplayName = "Decoration",
                    ToolTip = "Native decoration selection for this Style."))
  FEFCalystoDecorationPolicyV4 Decoration;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (ClampMin = "-1.0", ClampMax = "1.0",
                    DisplayName = "Mystery",
                    ToolTip = "Profile-wide influence of this Style on chests and their tiers while preserving Nothing."))
  float Mystery = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (ClampMin = "-1.0", ClampMax = "1.0",
                    DisplayName = "Danger",
                    ToolTip = "Profile-wide influence of this Style on enemy presence and difficulty."))
  float Danger = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (ClampMin = "-1.0", ClampMax = "1.0",
                    DisplayName = "Safe",
                    ToolTip = "Profile-wide influence of this Style on safe-NPC opportunity."))
  float Safe = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (ClampMin = "-1.0", ClampMax = "1.0",
                    DisplayName = "Abundance",
                    ToolTip = "Profile-wide influence of this Style on food opportunity."))
  float Abundance = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style",
            meta = (ClampMin = "-1.0", ClampMax = "1.0",
                    DisplayName = "Clothing Influence",
                    ToolTip = "Profile-wide influence of this Style on clothing opportunity and tiers."))
  float ClothingInfluence = 0.0f;

  void RefreshNothing();
  FEFCalystoContextTraitsV4 GetAuthoredTraits() const;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoThemeProfileV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (DisplayName = "Theme",
                    ToolTip = "Unique identity of this profile. Default is an ordinary Theme, not a global layer."))
  EEFCalystoThemeV4 Theme = EEFCalystoThemeV4::Default;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    DisplayName = "Selection Probability",
                    ToolTip = "Selection probabilities across all Themes must total exactly 1.0."))
  float SelectionProbability = 0.6f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Theme",
      meta =
          (AssetBundles = "CalystoFloorV4",
           DisplayName = "Calysto Room Type",
           ToolTip =
               "Default may remain empty as neutral topology. Forge and Shrine use their vendor Room Types without modifying them."))
  TSoftObjectPtr<UObject> RoomType;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    DisplayName = "Volatility",
                    ToolTip = "Controls probabilistic dispersion between this Theme and the Style. 0 is stable and 1 is highly variable; limits are never altered."))
  float Volatility = 0.5f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    DisplayName = "Player Adaptation Strength",
                    ToolTip = "Scales EMA influence for this Theme without exceeding the V4 cap."))
  float PlayerAdaptationStrength = 1.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (ClampMin = "1.0", ClampMax = "1000.0",
                    DisplayName = "Tau Winter",
                    ToolTip = "Winter uses 0.90*(1-exp(-(Floor-100)/TauWinter)) after Floor 100."))
  float WinterTau = 100.0f;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Theme",
      meta =
          (DisplayName = "Layout",
            ToolTip = "Probabilistic shape ranges owned by this Theme."))
  FEFCalystoLayoutProfileV4 Layout;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Theme",
      meta = (DisplayName = "Threat",
              ToolTip = "Threat-budget curve owned by this Theme."))
  FEFCalystoThreatCurveV4 Threat;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (TitleProperty = "Category", DisplayName = "Categories",
                    ToolTip = "Probability, tiers, limits, catalog, and the sole Enabled gate per category for this Theme."))
  TArray<FEFCalystoCategoryProfileV4> Categories;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (DisplayName = "Lighting",
                    ToolTip = "Calysto-native lighting configuration for this Theme."))
  FEFCalystoLightingPolicyV4 Lighting;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (DisplayName = "Decoration",
                    ToolTip = "Native decoration selection for this Theme."))
  FEFCalystoDecorationPolicyV4 Decoration;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (ClampMin = "-1.0", ClampMax = "1.0",
                    DisplayName = "Mystery",
                    ToolTip = "Profile-wide influence of this Theme on chests and their tiers while preserving Nothing."))
  float Mystery = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (ClampMin = "-1.0", ClampMax = "1.0",
                    DisplayName = "Danger",
                    ToolTip = "Profile-wide influence of this Theme on enemy presence and difficulty."))
  float Danger = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (ClampMin = "-1.0", ClampMax = "1.0",
                    DisplayName = "Safe",
                    ToolTip = "Profile-wide influence of this Theme on safe-NPC opportunity."))
  float Safe = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (ClampMin = "-1.0", ClampMax = "1.0",
                    DisplayName = "Abundance",
                    ToolTip = "Profile-wide influence of this Theme on food opportunity."))
  float Abundance = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme",
            meta = (ClampMin = "-1.0", ClampMax = "1.0",
                    DisplayName = "Clothing Influence",
                    ToolTip = "Profile-wide influence of this Theme on clothing opportunity and tiers."))
  float ClothingInfluence = 0.0f;

  void RefreshNothing();
  FEFCalystoContextTraitsV4 GetAuthoredTraits() const;
};

/** Immutable technical ceilings. They protect Calysto but do not contribute
 * gameplay probability. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoSafetyCeilingsV4 {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safety")
  int32 MaximumEnemies = 25;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safety")
  int32 MaximumNPCs = 4;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safety")
  int32 MaximumFood = 30;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safety")
  int32 MaximumChests = 10;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safety")
  int32 MaximumLooseLoot = 4;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safety")
  int32 MaximumClothing = 10;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safety")
  int32 MaximumSpecialEvents = 6;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safety")
  int32 MaximumDirectorActors = 89;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safety")
  float MaximumThreatBudget = 60.0f;
};

/** Normalized V4 intent. Preferred Style/Theme only receive a bounded
 * probabilistic boost; anti-streak remains authoritative. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDirectorIntentV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent",
            meta = (ClampMin = "-1.0", ClampMax = "1.0"))
  float Scale = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent",
            meta = (ClampMin = "-1.0", ClampMax = "1.0"))
  float Branching = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent")
  bool bHasPreferredStyle = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent")
  EEFCalystoStyleV4 PreferredStyle = EEFCalystoStyleV4::Standard;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent")
  bool bHasPreferredTheme = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent")
  EEFCalystoThemeV4 PreferredTheme = EEFCalystoThemeV4::Default;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent",
            meta = (ClampMin = "-1.0", ClampMax = "1.0"))
  float Danger = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent",
            meta = (ClampMin = "-1.0", ClampMax = "1.0"))
  float Safe = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent",
            meta = (ClampMin = "-1.0", ClampMax = "1.0"))
  float Abundance = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent",
            meta = (ClampMin = "-1.0", ClampMax = "1.0"))
  float Mystery = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent",
            meta = (ClampMin = "-1.0", ClampMax = "1.0"))
  float ClothingInfluence = 0.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent",
            meta = (ClampMin = "-1.0", ClampMax = "1.0",
                    ToolTip = "Bounded volatility bias. -1 reduces and +1 increases combined Style/Theme volatility by up to 0.25; limits are never altered."))
  float Volatility = 0.0f;
};

/** Frozen inputs for one isolated V4 resolution. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCompanionRecordV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
  FGuid StableCompanionId;

  /** Deterministic per-instance spawn identity that originally produced this
   * recruit. It is not a catalog/content identifier and may be reused only by
   * an exact replay of the same frozen floor. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
  FName SourceSpawnId = NAME_None;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
  FName SourceCatalogId = NAME_None;

  /** Concrete catalog variant selected for the originating spawn. Multiple
   * companions may legitimately share this value. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
  FName SourceVariantId = NAME_None;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
  TSoftClassPtr<AActor> ActorClass;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
  FName Archetype = NAME_None;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
  EEFCalystoGenderV4 Gender = EEFCalystoGenderV4::Any;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
  EEFCalystoRarityTierV4 Grade = EEFCalystoRarityTierV4::Common;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
  EEFCalystoCompanionRosterStateV4 State =
      EEFCalystoCompanionRosterStateV4::RecruitedInactive;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCompanionSnapshotV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
  TArray<FEFCalystoCompanionRecordV4> Records;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
  bool bPlayerOwnsWintersRecall = false;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoFloorOutcomeV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outcome",
            meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float Combat = 0.5f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outcome",
            meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float Survival = 0.5f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outcome",
            meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float Resources = 0.5f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outcome",
            meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float Pace = 0.5f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outcome",
            meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float DeathsAndFailures = 0.0f;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoResolveContextV4 {
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  int64 RunSeed = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  int64 FloorNumber = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  int64 GenerationSerial = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  FString EcologyHash;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  FEFCalystoDirectorIntentV4 DirectorIntent;

  /** Frozen ecology contribution already resolved from Run DNA, smooth noise
   * and jitter. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  FEFCalystoContextTraitsV4 ResolvedRunTraits;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver",
            meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float PerformanceEMA = 0.5f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  bool bHasFrozenOutcome = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  FEFCalystoFloorOutcomeV4 FrozenOutcome;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  FString OutcomeHash;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  EEFCalystoStyleV4 PreviousStyle = EEFCalystoStyleV4::Standard;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver",
            meta = (ClampMin = "0"))
  int32 ConsecutiveStyleCount = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  EEFCalystoThemeV4 PreviousTheme = EEFCalystoThemeV4::Default;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver",
            meta = (ClampMin = "0"))
  int32 ConsecutiveThemeCount = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver",
            meta = (ClampMin = "1"))
  int32 MaximumConsecutiveStyle = 2;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver",
            meta = (ClampMin = "1"))
  int32 MaximumConsecutiveTheme = 3;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver",
            meta = (ClampMin = "0"))
  int32 ConsecutiveFloorsWithoutFood = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver",
            meta = (ClampMin = "0"))
  int32 ConsecutiveFloorsWithoutChest = 0;

  /** Stable catalog IDs unavailable because of frozen run cooldown state. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  TArray<FName> CooldownBlockedCatalogIds;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  FString CompanionSnapshotHash;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resolver")
  FEFCalystoCompanionSnapshotV4 CompanionSnapshot;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Advanced|Automation",
      meta = (ClampMin = "0", ClampMax = "30", AdvancedDisplay,
              DisplayName = "Development Forced Dungeon Edge",
              ToolTip = "0 disables the override. Development may force only a certified 18..30 size valid for the resolved Style/Theme pair."))
  int32 DevelopmentForcedDungeonEdge = 0;

  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Advanced|Automation",
      meta = (AdvancedDisplay, DisplayName = "Development Population Scenario",
              ToolTip = "Development only: exact population fixtures, NPCTotal4, SpecialEvents6, NPC variants, and CompanionRecallLifecycle. Shipping rejects every scenario."))
  FName DevelopmentPopulationScenario = NAME_None;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoResolvedCategoryV4 {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  EEFCalystoContentCategoryV4 Category = EEFCalystoContentCategoryV4::Enemy;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  float StyleThemeBlend = 0.5f;

  /** Category-specific Style/Theme influence resolved with the exact same
   * blend draw used by Chance, tiers and catalog source. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  float ResolvedInfluence = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  float OpportunityChance = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  float SelectableTierMass = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  float WinterChance = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  float EffectiveChance = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  FEFCalystoTierMixV4 ResolvedTiers;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  int32 MinimumWhenPresent = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  int32 MaximumPerFloor = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  bool bPresent = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  int32 TargetCount = 0;

  /** Attempts before Nothing/catalog gates. TargetCount is replaced by the
   * final actor count. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  int32 AttemptCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  int32 DirectiveCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
  TArray<FName> EligibleCatalogIds;
};

/** Inclusive logical-level band selected by an enemy rarity tier. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoEnemyLevelBandV4 {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Level")
  int32 Min = 1;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Level")
  int32 Mode = 1;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Level")
  int32 Max = 1;
};

/** One preselected, immutable actor slot. PCG may place it but must never
 * reroll its content. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoSpawnInstanceDirectiveV4 {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  FName StableInstanceId = NAME_None;

  /** Valid only for a frozen roster projection; invalid identifies a newly
   * generated NPC. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  FGuid StableCompanionId;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  FName CatalogId = NAME_None;

  /** Concrete variant selected inside the merged Style/Theme catalog. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  FName VariantId = NAME_None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  FName Archetype = NAME_None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  EEFCalystoGenderV4 Gender = EEFCalystoGenderV4::Any;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  EEFCalystoLifecycleV4 Lifecycle = EEFCalystoLifecycleV4::FloorLocal;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  EEFCalystoContentCategoryV4 Category = EEFCalystoContentCategoryV4::Enemy;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  TSoftClassPtr<AActor> ActorClass;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  EEFCalystoRarityTierV4 Tier = EEFCalystoRarityTierV4::Common;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  int32 LogicalLevel = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  int32 PhysicalACFLevel = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  float EffectiveThreatCost = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  int32 CategorySlotIndex = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  int32 ChestContentAttemptCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Directive")
  int32 CooldownFloors = 0;
};

/** Frozen content selection for one chest container directive. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoChestContentDirectiveV4 {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest Content")
  FName ContainerInstanceId = NAME_None;

  /** Canonical per-attempt identity; repeated content entries remain
   * unambiguous. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest Content")
  FName StableAttemptId = NAME_None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest Content")
  FName ContentCatalogId = NAME_None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest Content")
  TSoftClassPtr<UObject> ContentClass;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest Content")
  EEFCalystoRarityTierV4 Tier = EEFCalystoRarityTierV4::Common;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest Content")
  int32 CooldownFloors = 0;
};

/** Frozen level for every roster record, including records not materialized on
 * this floor. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoResolvedCompanionLevelV4 {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion",
            meta = (DisplayName = "Stable ID",
                    ToolTip = "Persistent identity of the companion."))
  FGuid StableCompanionId;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Companion",
      meta = (DisplayName = "Grade",
              ToolTip = "Frozen tier/grade used to resolve the level band."))
  EEFCalystoRarityTierV4 Grade = EEFCalystoRarityTierV4::Common;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Companion",
      meta = (DisplayName = "Logical Level",
              ToolTip = "Deterministic discrete PERT result for this floor."))
  int32 LogicalLevel = 1;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion",
            meta = (DisplayName = "Physical ACF Level",
                    ToolTip = "The lower of Logical Level and 100."))
  int32 PhysicalACFLevel = 1;
};

/** Isolated V4 pre-PCG contract. It intentionally does not reuse or mutate V3
 * intent types. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoResolvedFloorIntentV4 {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  bool bIsValid = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  int32 GeneratorVersion = 4;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  int64 RunSeed = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  int64 FloorNumber = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  int64 GenerationSerial = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  FString PolicyHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  FString EcologyHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  FString CompanionSnapshotHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  FEFCalystoCompanionSnapshotV4 CompanionSnapshot;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  FString IntentHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  FString OutcomeHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  bool bHasFrozenOutcome = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  FEFCalystoFloorOutcomeV4 FrozenOutcome;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  EEFCalystoStyleV4 Style = EEFCalystoStyleV4::Standard;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  EEFCalystoThemeV4 Theme = EEFCalystoThemeV4::Default;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  float StyleSelectionDraw = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  float ThemeSelectionDraw = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  float ShapeBlend = 0.5f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  TSoftObjectPtr<UObject> CalystoRoomType;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  FEFCalystoContextTraitsV4 ResolvedTraits;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  int32 DirectorLevel = 1;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  int32 LogicalWinterLevel = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  FIntVector DungeonSize = FIntVector(26, 26, 1);

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
            Category = "Advanced|Automation")
  int32 DevelopmentForcedDungeonEdge = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
            Category = "Advanced|Automation")
  FName DevelopmentPopulationScenario = NAME_None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  int32 PCGSeed = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  float CandidateAnchorDensity = 0.32f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  float SidePathChance = 0.50f;

  /** Deterministic Style/Theme draw used exclusively by the native-lighting
   * domain. It is recorded so Replay applies the exact same native settings. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  float LightingBlend = 0.5f;

  /** Final values written to the transient BP_MassiveDungeon runtime actor
   * before Calysto's native PCG execution. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  float NativeWallLightHeight = 200.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  int32 NativeWallLightTileDistance = 10;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  float ThreatBudget = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  float PlannedThreatCost = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  float ResourceBudget = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  float PlannedResourceCost = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  TArray<FEFCalystoResolvedCategoryV4> Categories;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  TArray<FEFCalystoSpawnInstanceDirectiveV4> SpawnDirectives;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  TArray<FEFCalystoChestContentDirectiveV4> ChestContentDirectives;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "V4")
  TArray<FEFCalystoResolvedCompanionLevelV4> ResolvedCompanionLevels;
};

/** One post-placement V4 instance. The manifest records what was actually
 * materialized, never a reroll. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRealizedInstanceV4 {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  FName StableInstanceId = NAME_None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  FGuid StableCompanionId;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  FName CatalogId = NAME_None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  FName VariantId = NAME_None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  FName Archetype = NAME_None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  EEFCalystoGenderV4 Gender = EEFCalystoGenderV4::Any;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  EEFCalystoLifecycleV4 Lifecycle = EEFCalystoLifecycleV4::FloorLocal;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  EEFCalystoContentCategoryV4 Category = EEFCalystoContentCategoryV4::Enemy;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  TSoftClassPtr<AActor> ActorClass;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  FTransform Transform;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  EEFCalystoRarityTierV4 Tier = EEFCalystoRarityTierV4::Common;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  int32 LogicalLevel = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  float EffectiveThreatCost = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  int32 CooldownFloors = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  TArray<FName> VerifiedChestContentIds;

  /** Verified per-attempt payload, including StableAttemptId and the resolved
   * item class. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Instance")
  TArray<FEFCalystoChestContentDirectiveV4> VerifiedChestContents;
};

/** Immutable V4 post-placement evidence. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRealizedFloorManifestV4 {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  bool bIsValid = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  int64 RunSeed = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  int64 FloorNumber = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  int64 GenerationSerial = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  FString IntentHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  FString AnchorTopologyHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  FString PopulationHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  FString ResourceHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  FString CompanionSnapshotHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  FString ManifestHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  int32 CandidateAnchorCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  int32 EnemyCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  int32 NPCCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  int32 FoodCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  int32 ChestCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  int32 LooseLootCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  int32 ClothingCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  int32 SpecialEventCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  int32 SpawnedActorCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  float RealizedThreatCost = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  float RealizedResourceCost = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
  TArray<FEFCalystoRealizedInstanceV4> Instances;
};

/** Confirmed cooldown state; the policy is not queried again during Replay or
 * Advance. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCooldownStateV4 {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
            meta = (DisplayName = "Stable ID",
                    ToolTip = "Catalog or content ID subject to cooldown."))
  FName StableId = NAME_None;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
      meta = (DisplayName = "Last Selected Floor",
              ToolTip = "Confirmed floor on which this entry was last selected."))
  int64 LastSelectedFloor = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
            meta = (DisplayName = "Frozen Duration",
                    ToolTip = "Number of floors frozen in the directive that originated this cooldown."))
  int32 CooldownFloors = 0;
};

/** V4 run memory. It neither reuses nor adapts V3 ecology. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRunEcologyStateV4 {
  GENERATED_BODY()

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
      meta =
          (DisplayName = "Initialized",
           ToolTip =
               "True after deriving Run DNA and its hash for the current run."))
  bool bInitialized = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
            meta = (DisplayName = "Run DNA Hash",
                    ToolTip = "Canonical identity of the persistent DNA; independent of floor and serial."))
  FString RunDNAHash;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
      meta =
          (DisplayName = "Run DNA",
           ToolTip =
               "Persistent traits derived only from RunSeed and PolicyHash."))
  FEFCalystoContextTraitsV4 RunDNATraits;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
      meta = (DisplayName = "Performance EMA",
              ToolTip = "Frozen, bounded adaptation; 0.5 is neutral."))
  float PerformanceEMA = 0.5f;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
      meta = (DisplayName = "Last Committed Floor",
              ToolTip = "Advance may commit each floor only once."))
  int64 LastCommittedFloor = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
            meta = (DisplayName = "Revision",
                    ToolTip = "Increases once per ecology commit."))
  int64 EcologyRevision = 0;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
      meta =
          (DisplayName = "Floors Without Food",
           ToolTip =
               "Counter used by Food pity for the next Style/Theme."))
  int32 ConsecutiveFloorsWithoutFood = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
            meta = (DisplayName = "Floors Without Chests",
                    ToolTip = "Counter used by Chest pity for the next Style/Theme."))
  int32 ConsecutiveFloorsWithoutChest = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
            meta = (DisplayName = "Recent Styles",
                    ToolTip = "Canonical history for the anti-streak rule."))
  TArray<EEFCalystoStyleV4> RecentStyles;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
            meta = (DisplayName = "Recent Themes",
                    ToolTip = "Canonical history for the anti-streak rule."))
  TArray<EEFCalystoThemeV4> RecentThemes;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
      meta = (DisplayName = "Cooldowns",
              ToolTip =
                  "Cooldowns confirmed from materialized directives."))
  TArray<FEFCalystoCooldownStateV4> Cooldowns;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
      meta =
          (DisplayName = "Companion Snapshot",
           ToolTip = "Frozen roster excluding RunEpoch from the canonical hash."))
  FEFCalystoCompanionSnapshotV4 CompanionSnapshot;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
      meta = (DisplayName = "Ecology Hash",
              ToolTip = "Canonical SHA-256 of all confirmed memory."))
  FString EcologyHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology",
            meta = (DisplayName = "Synthetic History",
                    ToolTip = "True only for Development Jump; it is not equivalent to traversing the run."))
  bool bDevelopmentSyntheticHistory = false;
};

UENUM(BlueprintType)
enum class EEFCalystoDungeonRunStateV4 : uint8 {
  Idle UMETA(DisplayName = "Inactive"),
  Traveling UMETA(DisplayName = "Traveling"),
  Generating UMETA(DisplayName = "Generating"),
  Ready UMETA(DisplayName = "Ready"),
  Failed UMETA(DisplayName = "Failed")
};

UENUM(BlueprintType)
enum class EEFCalystoDungeonTravelKindV4 : uint8 {
  None UMETA(DisplayName = "None"),
  NewRun UMETA(DisplayName = "New Run"),
  Advance UMETA(DisplayName = "Advance"),
  Reroll UMETA(DisplayName = "Reroll"),
  Replay UMETA(DisplayName = "Replay"),
  DebugJump UMETA(DisplayName = "Development Jump")
};

/** Public V4 DTO for UI, AI, and automation; it neither exposes nor depends on
 * V3 snapshots. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDungeonSnapshotV4 {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "State",
                    ToolTip = "Current V4 travel and generation state."))
  EEFCalystoDungeonRunStateV4 State = EEFCalystoDungeonRunStateV4::Idle;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta = (DisplayName = "Active Run",
              ToolTip =
                  "True from New Run until final closure or recovery."))
  bool bHasActiveRun = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Run Epoch",
                    ToolTip = "Monotonic session identity; it does not participate in CompanionSnapshotHash."))
  int64 RunEpoch = 0;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta = (DisplayName = "Valid Policy",
              ToolTip = "Cached result of fail-closed V4 policy validation."))
  bool bPolicyValid = false;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta = (DisplayName = "Policy Error",
              ToolTip =
                  "Validation detail when the policy is unusable."))
  FString PolicyError;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Queued Intent",
                    ToolTip = "True when a normalized preference is queued for the next floor."))
  bool bHasQueuedDirectorIntent = false;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta = (DisplayName = "Travel Kind",
              ToolTip =
                  "Diagnostic identity of the requested operation; it never drives RNG."))
  EEFCalystoDungeonTravelKindV4 TravelKind =
      EEFCalystoDungeonTravelKindV4::None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Run Seed",
                    ToolTip = "Authoritative seed for the run."))
  int64 RunSeed = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Floor",
                    ToolTip = "Requested or currently ready floor."))
  int64 FloorNumber = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Generation Serial",
                    ToolTip = "Advance, Reroll, and Development Jump increment it; Replay and Retry preserve it."))
  int64 GenerationSerial = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Style",
                    ToolTip = "Probabilistic Style resolved for the floor."))
  EEFCalystoStyleV4 Style = EEFCalystoStyleV4::Standard;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Theme",
                    ToolTip = "Probabilistic Theme resolved for the floor."))
  EEFCalystoThemeV4 Theme = EEFCalystoThemeV4::Default;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Size",
                    ToolTip = "Certified X/Y size with Z fixed at one."))
  FIntVector DungeonSize = FIntVector(26, 26, 1);

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
            Category = "Snapshot|Advanced Automation",
            meta = (DisplayName = "Development Forced Dungeon Edge"))
  int32 DevelopmentForcedDungeonEdge = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
            Category = "Snapshot|Advanced Automation",
            meta = (DisplayName = "Development Population Scenario"))
  FName DevelopmentPopulationScenario = NAME_None;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta = (DisplayName = "PCG Seed",
              ToolTip = "Deterministic seed delivered to Calysto exactly once."))
  int32 PCGSeed = 0;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta =
          (DisplayName = "Anchor Density",
           ToolTip =
               "Candidate structural density, never enemy density."))
  float CandidateAnchorDensity = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Side Path Chance",
                    ToolTip = "Resolved chance bounded between 0.30 and 0.70."))
  float SidePathChance = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Resolved Traits",
                    ToolTip = "Final floor personality after DNA, noise, jitter, Style, and Theme."))
  FEFCalystoContextTraitsV4 ResolvedTraits;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Categories",
                    ToolTip = "Resolved chances, limits, presence, and counts per category."))
  TArray<FEFCalystoResolvedCategoryV4> Categories;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Threat Budget",
                    ToolTip = "Resolved threat ceiling for the floor."))
  float ThreatBudget = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Planned Threat",
                    ToolTip = "Frozen total cost of enemy directives."))
  float PlannedThreatCost = 0.0f;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta =
          (DisplayName = "Resource Budget",
           ToolTip =
               "Frozen number of resource attempts before Nothing."))
  float ResourceBudget = 0.0f;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta = (DisplayName = "Planned Resources",
              ToolTip = "Total cost of resources actually selected."))
  float PlannedResourceCost = 0.0f;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta = (DisplayName = "Realized Threat",
              ToolTip = "Cost verified in the post-placement manifest."))
  float RealizedThreatCost = 0.0f;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta =
          (DisplayName = "Realized Resources",
           ToolTip =
               "Resource cost verified in the post-placement manifest."))
  float RealizedResourceCost = 0.0f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Policy Hash",
                    ToolTip = "Hash of the V4 policy used."))
  FString PolicyHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Ecology Hash",
                    ToolTip = "Hash of the confirmed memory used."))
  FString EcologyHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Intent Hash",
                    ToolTip = "Hash of the pre-PCG intent."))
  FString IntentHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Manifest Hash",
                    ToolTip = "Hash of the content actually materialized."))
  FString ManifestHash;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Companion Snapshot Hash",
                    ToolTip = "Canonical hash of the frozen roster."))
  FString CompanionSnapshotHash;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta = (DisplayName = "Companions Ready",
              ToolTip =
                  "True when the frozen party has been restored and validated."))
  bool bCompanionReady = false;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta =
          (DisplayName = "Door Ready",
           ToolTip =
               "True only after PCG, navigation, manifest, and population are ready."))
  bool bDoorReady = false;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta = (DisplayName = "Failure Code",
              ToolTip = "Stable code for automation and telemetry."))
  FName FailureCode = NAME_None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Failure Message",
                    ToolTip = "Human-readable description of the fail-closed failure."))
  FString FailureMessage;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta =
          (DisplayName = "Current Attempt",
           ToolTip = "Current recovery/generation attempt number."))
  int32 CurrentAttempt = 0;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta = (DisplayName = "Maximum Attempts",
              ToolTip = "Attempt ceiling before returning safely."))
  int32 MaximumAttempts = 2;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Pending Floor",
                    ToolTip = "Frozen floor awaiting travel or bootstrap."))
  int64 PendingFloorNumber = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
            meta = (DisplayName = "Pending Serial",
                    ToolTip = "Frozen serial awaiting travel or bootstrap."))
  int64 PendingGenerationSerial = 0;

  UPROPERTY(
      VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot",
      meta =
          (DisplayName = "Return Map",
           ToolTip =
               "Project-owned package used for safe recovery to the HUB."))
  FString ReturnMapPackage;
};
