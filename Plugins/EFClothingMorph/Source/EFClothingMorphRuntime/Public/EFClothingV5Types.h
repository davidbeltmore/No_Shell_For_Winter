#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "EFClothingV5Types.generated.h"

class UMaterialInterface;

namespace EFClothingMorphV5
{
	constexpr int32 BodyProfileSchemaVersion = 1;
	constexpr int32 ClothingDefinitionSchemaVersion = 2;
	constexpr float MaximumClearanceCm = 2.0f;
	constexpr float MaximumShellThicknessCm = 2.0f;
	constexpr float MaximumLowerBodyMorphGuardClearanceCm = 0.35f;
	constexpr float LowerBodyMorphGuardActivityEpsilon = 1.0e-4f;
}

/** Ordered semantic layer used to resolve garment stacking without asset-path heuristics. */
UENUM(BlueprintType)
enum class EEFClothingLayer : uint8
{
	Underwear UMETA(DisplayName = "Underwear"),
	Base UMETA(DisplayName = "Base Layer"),
	Outer UMETA(DisplayName = "Outer Layer"),
	Armor UMETA(DisplayName = "Armor"),
	Accessory UMETA(DisplayName = "Accessory")
};

/**
 * Explicit render policy for clothing materials. ForceOpaque is intentionally
 * the safe default: skin decals and tattoos must not be visible through cloth.
 */
UENUM(BlueprintType)
enum class EEFClothingMaterialPolicy : uint8
{
	ForceOpaque UMETA(DisplayName = "Force Opaque"),
	PreserveMasked UMETA(DisplayName = "Preserve Masked Cutouts"),
	AllowTranslucent UMETA(DisplayName = "Allow Translucent (Explicit Exception)")
};

/** A slot and/or material-specific exception to a clothing material policy. */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingMaterialPolicyRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Policy")
	bool bEnabled = true;

	/** Optional slot filter. NAME_None means every slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Policy", meta = (DisplayName = "Material Slot"))
	FName MaterialSlot = NAME_None;

	/** Optional exact material filter. A rule with both filters set must match both. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Policy")
	TSoftObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Policy")
	EEFClothingMaterialPolicy Policy = EEFClothingMaterialPolicy::ForceOpaque;

	/** Larger values win when more than one enabled rule matches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Policy", meta = (ClampMin = "-1000", ClampMax = "1000"))
	int32 Priority = 0;

	bool HasFilter() const
	{
		return !MaterialSlot.IsNone() || !Material.IsNull();
	}
};

/** Default material behavior plus narrowly scoped authoring exceptions. */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingMaterialPolicySet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Policy")
	EEFClothingMaterialPolicy DefaultPolicy = EEFClothingMaterialPolicy::ForceOpaque;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Policy", meta = (TitleProperty = "MaterialSlot"))
	TArray<FEFClothingMaterialPolicyRule> Overrides;
};

/**
 * Per-garment guard for body morphs that need a small, conditional lower-body
 * reserve. The rule is authored in the single Director row and names body
 * morphs explicitly, so upper garments can never inherit it from a shared
 * layer or coverage tag.
 */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingLowerBodyMorphGuardRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lower Body Morph Guard", meta = (DisplayName = "Enable Guard", ToolTip = "Adds a bounded reserve only while one of the exact body morphs below is active."))
	bool bEnabled = false;

	/** Exact body morph names that activate this guard. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lower Body Morph Guard", meta = (DisplayName = "Exact Body Morph Names", ToolTip = "Exact body curve names. This rule never infers morphs from clothing Layer or Coverage."))
	TArray<FName> ExactBodyMorphNames;

	/** Maximum extra clearance requested when any named body morph reaches weight 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lower Body Morph Guard", meta = (ClampMin = "0.0", ClampMax = "0.35", UIMin = "0.0", UIMax = "0.35", Units = "cm", DisplayName = "Maximum Clearance (cm)", ToolTip = "Extra lower-body reserve at morph weight 1. Values above 0.35 cm are intentionally unsupported."))
	float MaximumClearanceCm = 0.35f;

	/** Asset-only validation; it never loads a mesh or reads live curves. */
	bool Validate(FString& OutError) const
	{
		OutError.Reset();
		if (!FMath::IsFinite(MaximumClearanceCm)
			|| MaximumClearanceCm < 0.0f
			|| MaximumClearanceCm > EFClothingMorphV5::MaximumLowerBodyMorphGuardClearanceCm)
		{
			OutError = TEXT("Lower Body Morph Guard clearance is outside the supported V5 range.");
			return false;
		}

		TSet<FName> UniqueNames;
		for (const FName MorphName : ExactBodyMorphNames)
		{
			if (MorphName.IsNone() || UniqueNames.Contains(MorphName))
			{
				OutError = TEXT("Lower Body Morph Guard contains an empty or duplicate body morph name.");
				return false;
			}
			UniqueNames.Add(MorphName);
		}
		if (bEnabled && UniqueNames.IsEmpty())
		{
			OutError = TEXT("An enabled Lower Body Morph Guard needs at least one exact body morph name.");
			return false;
		}
		if (bEnabled && MaximumClearanceCm <= 0.0f)
		{
			OutError = TEXT("An enabled Lower Body Morph Guard needs a positive clearance reserve.");
			return false;
		}
		return true;
	}

	/**
	 * Returns the strongest positive named morph weight in [0, 1]. Invalid,
	 * negative, unrelated, or disabled inputs are deliberately inert.
	 */
	float EvaluateActivity(const TMap<FName, float>& BodyMorphWeights) const
	{
		if (!bEnabled)
		{
			return 0.0f;
		}

		float Activity = 0.0f;
		for (const FName MorphName : ExactBodyMorphNames)
		{
			const float* Weight = BodyMorphWeights.Find(MorphName);
			if (Weight
				&& FMath::IsFinite(*Weight)
				&& *Weight > EFClothingMorphV5::LowerBodyMorphGuardActivityEpsilon)
			{
				Activity = FMath::Max(Activity, FMath::Clamp(*Weight, 0.0f, 1.0f));
			}
		}
		return Activity;
	}
};

/** Space claimed by one garment inside a named body region. */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingRegionOccupancyRule
{
	GENERATED_BODY()

	/** Stable region identifier declared by the selected body profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layering")
	FName RegionId = NAME_None;

	/** Approximate fabric volume reserved before a higher layer is fitted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layering", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "1.0", Units = "cm"))
	float OccupiedThicknessCm = 0.0f;

	/** Minimum separation requested between this garment and the next outer layer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layering", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "0.5", Units = "cm"))
	float MinimumOuterGapCm = 0.0f;

	/** Prevents another garment on the same semantic layer from claiming this region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layering")
	bool bExclusiveWithinLayer = true;
};

/** Layer ordering and region occupancy for one garment. */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingLayerRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layering")
	EEFClothingLayer Layer = EEFClothingLayer::Base;

	/** Deterministic tie-breaker inside the same semantic layer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layering", meta = (ClampMin = "-1000", ClampMax = "1000"))
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layering", meta = (TitleProperty = "RegionId"))
	TArray<FEFClothingRegionOccupancyRule> OccupiedRegions;
};

/** A named body region and the skin material slots that visually represent it. */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingBodyRegionDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Region")
	FName RegionId = NAME_None;

	/** Optional semantic tags used by equipment/gameplay adapters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Region")
	FGameplayTagContainer RegionTags;

	/** Material slots hidden when an equipped definition fully covers this region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Region")
	TArray<FName> SkinMaterialSlots;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Region")
	bool bHideSkinWhenCovered = true;
};

/** Declarative body morph family; no anatomy names are hard-coded in runtime code. */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingMorphGroupDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Group")
	FName MorphGroupId = NAME_None;

	/** Exact curve/morph names belonging to this family. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Group")
	TArray<FName> ExactMorphNames;

	/** Optional case-sensitive prefixes for generated DAZ/JCM morph families. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Group")
	TArray<FString> MorphNamePrefixes;

	/** Body profile region identifiers influenced by this morph family. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Group")
	TArray<FName> AffectedRegionIds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Group", meta = (ClampMin = "-10.0", ClampMax = "10.0"))
	float MinimumValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Group", meta = (ClampMin = "-10.0", ClampMax = "10.0"))
	float MaximumValue = 1.0f;
};
