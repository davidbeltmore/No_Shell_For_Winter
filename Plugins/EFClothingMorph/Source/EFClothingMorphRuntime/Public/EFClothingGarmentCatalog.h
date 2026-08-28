#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Misc/SecureHash.h"
#include "EFClothingGarmentCatalog.generated.h"

class USkeletalMesh;

/** Runtime backend selected per garment/body pair by the authored catalog. */
UENUM(BlueprintType)
enum class EEFClothingSurfaceBackend : uint8
{
	/** Geometry-compiled correspondence, certified weights and adaptive clearance. */
	GeometryFitFallback UMETA(DisplayName = "Geometry Fit Fallback"),

	/** Final GPU surface-wrap backend; rows can opt in as its cooked graph becomes available. */
	SurfaceWrapGPU UMETA(DisplayName = "Automatic GPU Surface Wrap (Recommended)"),

	Disabled UMETA(Hidden)
};

/** Authored override for the compiler's automatic tight/loose classification. */
UENUM(BlueprintType)
enum class EEFClothingFitPolicy : uint8
{
	Auto UMETA(DisplayName = "Automatic Region Classification (Recommended)"),
	Tight UMETA(DisplayName = "Tight / Follow Body Surface"),
	Hybrid UMETA(DisplayName = "Hybrid / Blend Authored Motion"),
	Loose UMETA(DisplayName = "Loose / Collision Only"),
	Rigid UMETA(DisplayName = "Rigid / Collision Only")
};

/**
 * Authored source of truth for meshes that EF Clothing Morph is allowed to manage.
 * A garment can have separate Female/Male rows without changing runtime code.
 */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingGarmentRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Immutable authoring/runtime identity. Array order is presentation-only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01 | Garment Identity", meta = (DisplayName = "Garment Index / ID", ToolTip = "Unique, stable identity for this entry. Do not rename it after compiling the garment."))
	FName GarmentId = NAME_None;

	/** Friendly name shown by the Clothing Director. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01 | Garment Identity", meta = (DisplayName = "Display Name", ToolTip = "Human-readable name shown in the Clothing Director."))
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01 | Garment Identity", meta = (DisplayName = "Enable Garment", ToolTip = "Enable only after the ID, original garment mesh, and reference body are assigned. New rows start disabled and do not block configured garments."))
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01 | Garment Identity", meta = (DisplayName = "Editable Source Garment Mesh", ToolTip = "This is the garment you may edit with Unreal's native Skeletal Mesh Deform tools. Save your changes and EF Clothing Morph refreshes its hidden runtime fit before Play or packaging. Never assign or edit an internal generated SK_ mesh."))
	TSoftObjectPtr<USkeletalMesh> SourceGarment;

	/** Exact visible body surface for this compiled row (Female now, Male later). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01 | Garment Identity", meta = (DisplayName = "Reference Body", ToolTip = "Exact DAZ body this garment must fit, for example Female or Male."))
	TSoftObjectPtr<USkeletalMesh> BodySurface;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02 | Automatic Fit", meta = (DisplayName = "Fit Backend", ToolTip = "Use Automatic GPU Surface Wrap for new garments. Geometry Fit Fallback remains for compatibility only."))
	EEFClothingSurfaceBackend Backend = EEFClothingSurfaceBackend::SurfaceWrapGPU;

	/** Auto classifies connected garment regions; explicit modes remain available for authoring exceptions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02 | Automatic Fit", meta = (DisplayName = "Runtime Fit Policy", ToolTip = "Automatic classifies each region after preserving your authored source shape. Tight follows the body most strongly; Hybrid blends body following with authored motion; Loose and Rigid preserve native skinning or Chaos motion and only prevent penetration."))
	EEFClothingFitPolicy FitPolicy = EEFClothingFitPolicy::Auto;

	/** Semantic coverage used by gameplay/UI and future body-region proxy masks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03 | Body Coverage", meta = (DisplayName = "Covered Body Regions", ToolTip = "Gameplay coverage tags for the body regions covered by this garment."))
	FGameplayTagContainer CoverageTags;

	/**
	 * Body material slots hidden only on the live body component while this garment
	 * is applied. Visibility is reference-counted and restored exactly on unequip.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03 | Body Coverage", meta = (DisplayName = "Body Material Slots to Hide", ToolTip = "Body material slots hidden while this garment is equipped. They are restored on unequip."))
	TArray<FName> HiddenBodyMaterialSlots;

	/**
	 * Body sections that must not participate in the compiler's closest-surface,
	 * clearance or skin-weight projection. This is distinct from visual hiding:
	 * auxiliary anatomy can be hidden at runtime and also excluded from the
	 * mathematical body envelope without hard-coding a garment or DAZ product.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "99 | Advanced", meta = (AdvancedDisplay, DisplayName = "Excluded Body Surfaces", ToolTip = "Body material slots the solver must not use as reference surfaces."))
	TArray<FName> ExcludedBodySurfaceMaterialSlots;

	/**
	 * Root bones of optional anatomy branches that must never drive this garment.
	 * The compiler redirects their influence to the nearest non-excluded,
	 * hierarchy-compatible ancestor on the generated EF_AutoFit profile only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "99 | Advanced", meta = (AdvancedDisplay, DisplayName = "Excluded Bone Branches", ToolTip = "Auxiliary bone branches that must never transfer skin weights to this garment."))
	TArray<FName> ExcludedBodyBoneBranches;

	/** Body morph namespaces belonging to an excluded accessory/graft. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "99 | Advanced", meta = (AdvancedDisplay, DisplayName = "Excluded Morph Prefixes", ToolTip = "Auxiliary-anatomy morph namespaces excluded from garment fitting."))
	TArray<FString> ExcludedBodyMorphPrefixes;

	/** Optional authored floor; it is always rounded upward to a compiler-certified tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "99 | Advanced", meta = (ClampMin = "1.0", ClampMax = "2.0", AdvancedDisplay, DisplayName = "Minimum Clearance Multiplier", ToolTip = "Legacy certified clearance floor. Normally leave this at 1.0."))
	float MinimumClearanceMultiplier = 1.0f;

	/** Negative means compiler-selected from geometry/fabric data; otherwise an absolute cm floor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02 | Automatic Fit", meta = (ClampMin = "-1.0", ClampMax = "5.0", UIMin = "-1.0", UIMax = "2.0", Units = "cm", DisplayName = "Compiled Base Clearance (cm; -1 = Automatic)", ToolTip = "Base skin clearance used during compilation. Leave at -1 for automatic selection. Changing this value requires Compile All Garments.", AdvancedDisplay))
	float FabricClearanceCm = -1.0f;

	/** Enables this garment index's topology-free runtime clearance override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04 | Per-Garment Runtime Clearance", meta = (DisplayName = "Enable Runtime Clearance", ToolTip = "Affects only this garment. Pushes the entire garment outward without creating real thickness. This setting is immediate and does not require recompilation."))
	bool bEnableRuntimeTuning = true;

	/** Extra outward clearance applied after skinning and morphs, in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04 | Per-Garment Runtime Clearance", meta = (ClampMin = "0.0", ClampMax = "0.35", UIMin = "0.0", UIMax = "0.35", Units = "cm", DisplayName = "Additional Skin Clearance (cm)", ToolTip = "Pushes this garment outward after animation and morphs. It does not add cloth thickness and takes effect without recompiling.", EditCondition = "bEnableRuntimeTuning"))
	float AdditionalClearanceCm = 0.0f;

	/** Generates an outward-only two-sided shell on the internal derived mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05 | Per-Garment Real Thickness", meta = (DisplayName = "Enable Adjustable Thickness (One-Time Compile)", ToolTip = "Creates the paired inner/outer topology required for runtime thickness. Compile once after enabling or disabling it. Changing Visible Thickness afterward is immediate and never rebuilds or hides the garment. The original mesh is never modified. Garments with authored Chaos Cloth need rebuilt Cloth mapping for this generated topology."))
	bool bCreateThicknessShell = false;

	/** Runtime distance between the fitted inner layer and its visible outer layer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05 | Per-Garment Real Thickness", meta = (ClampMin = "0.01", ClampMax = "0.35", UIMin = "0.01", UIMax = "0.35", Units = "cm", DisplayName = "Visible Thickness (cm, Runtime)", ToolTip = "Changes only this garment's visible geometric thickness. The fitted inner layer stays against the body while the outer layer and boundary walls expand outward. This takes effect at runtime without compiling, rebuilding, or hiding the garment. 0.05 cm is thin fabric; 0.20 cm is visibly thicker.", EditCondition = "bCreateThicknessShell"))
	float ShellThicknessCm = 0.05f;

	/** Same iterative solve control exposed by UE's native Modeling Offset tool. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05 | Per-Garment Real Thickness", meta = (ClampMin = "1", ClampMax = "100", UIMin = "1", UIMax = "32", DisplayName = "Iterative Offset Steps", ToolTip = "Number of Unreal Iterative Offset steps used to build the outer layer. More steps may follow curved surfaces better but increase compile time.", EditCondition = "bCreateThicknessShell", AdvancedDisplay))
	int32 ShellOffsetSteps = 10;

	/** Boundary offset is required by the certified paired-layer contract. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05 | Per-Garment Real Thickness", meta = (DisplayName = "Create Boundary Walls (Required)", ToolTip = "Creates visible walls at waistbands, cuffs, sleeves, and other openings. Keep this enabled: an open shell cannot be certified and is rejected safely.", EditCondition = "bCreateThicknessShell", AdvancedDisplay))
	bool bShellOffsetBoundaries = true;

	/** Per-step smoothing matching UE's iterative offset option. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05 | Per-Garment Real Thickness", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", DisplayName = "Smoothing Per Step", ToolTip = "Smooths the outer layer during each offset step. Leave at 0 to preserve the original shape and detail as closely as possible.", EditCondition = "bCreateThicknessShell", AdvancedDisplay))
	float ShellSmoothingPerStep = 0.0f;

	/** Optional native reproject-smooth behavior. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05 | Per-Garment Real Thickness", meta = (DisplayName = "Reproject After Smoothing", ToolTip = "Reprojects the smoothed result toward the offset surface. Use only when smoothing changes the silhouette too much.", EditCondition = "bCreateThicknessShell && ShellSmoothingPerStep > 0.0", AdvancedDisplay))
	bool bShellReprojectSmooth = false;

	/** Hidden schema-1 field retained only until legacy DataTables are retired. */
	UPROPERTY()
	float RuntimeOffsetCm = 0.0f;

	/** Optional human-facing note; it has no runtime or compiler effect. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "06 | Notes", meta = (MultiLine = "true", DisplayName = "Garment Notes", ToolTip = "Free-form authoring notes. This text has no runtime effect."))
	FText Notes;

	/** Negative means compiler-selected; otherwise the hard one-frame outward correction bound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "99 | Advanced", meta = (ClampMin = "-1.0", ClampMax = "10.0", UIMin = "-1.0", UIMax = "10.0", Units = "cm", DisplayName = "Maximum Correction (cm; -1 = Automatic)", ToolTip = "Maximum outward correction allowed per frame. Leave at -1 for automatic selection.", AdvancedDisplay))
	float MaximumCorrectionCm = -1.0f;

	/** Missing or stale body/garment LOD bindings must never expose the uncorrected garment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "99 | Advanced", meta = (AdvancedDisplay, DisplayName = "Hide When an LOD Binding Is Missing", ToolTip = "Prevents an unbound garment from becoming visible during LOD changes."))
	bool bFailClosedOnMissingLOD = true;

	/**
	 * An untouched array element created with the Details-panel plus button is a
	 * harmless authoring placeholder. It must not invalidate already configured
	 * garments until the user assigns an Id/source/body and explicitly enables it.
	 */
	bool IsDisabledEmptyPlaceholder() const
	{
		return !bEnabled
			&& GarmentId.IsNone()
			&& SourceGarment.IsNull()
			&& BodySurface.IsNull();
	}

	/** Hash of compile-relevant authoring only; runtime clearance, visible thickness and notes are excluded. */
	FString BuildCompileFingerprint() const
	{
		auto CanonicalNames = [](const TArray<FName>& Values)
		{
			TArray<FString> Result;
			for (const FName Value : Values)
			{
				if (!Value.IsNone())
				{
					Result.AddUnique(Value.ToString());
				}
			}
			Result.Sort();
			return FString::Join(Result, TEXT(","));
		};
		auto CanonicalStrings = [](const TArray<FString>& Values)
		{
			TArray<FString> Result;
			for (const FString& Value : Values)
			{
				if (!Value.IsEmpty())
				{
					Result.AddUnique(Value);
				}
			}
			Result.Sort();
			return FString::Join(Result, TEXT(","));
		};

		TArray<FGameplayTag> Tags;
		CoverageTags.GetGameplayTagArray(Tags);
		Tags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.GetTagName().LexicalLess(B.GetTagName());
		});
		TArray<FString> TagStrings;
		for (const FGameplayTag Tag : Tags)
		{
			TagStrings.Add(Tag.ToString());
		}

		// Keep the authored fingerprint in lockstep with the compiler's paired-layer
		// shell contract. Changing this value invalidates stale generated assets.
		constexpr int32 ShellAlgorithmVersion = 4;
		const bool bCanonicalShellEnabled = bCreateThicknessShell;
		const FString Canonical = FString::Printf(
			TEXT("Backend=%d|Fit=%d|Coverage=%s|Hidden=%s|ExcludedSurface=%s|ExcludedBones=%s|ExcludedMorphs=%s|MinMultiplier=%.6f|Fabric=%.6f|ShellAlgorithm=%d|Shell=%d|ShellSteps=%d|ShellBoundaries=%d|ShellSmooth=%.6f|ShellReproject=%d|MaxCorrection=%.6f|FailClosedLOD=%d"),
			static_cast<int32>(Backend),
			static_cast<int32>(FitPolicy),
			*FString::Join(TagStrings, TEXT(",")),
			*CanonicalNames(HiddenBodyMaterialSlots),
			*CanonicalNames(ExcludedBodySurfaceMaterialSlots),
			*CanonicalNames(ExcludedBodyBoneBranches),
			*CanonicalStrings(ExcludedBodyMorphPrefixes),
			MinimumClearanceMultiplier,
			FabricClearanceCm,
			ShellAlgorithmVersion,
			bCanonicalShellEnabled ? 1 : 0,
			bCanonicalShellEnabled ? ShellOffsetSteps : 0,
			bCanonicalShellEnabled && bShellOffsetBoundaries ? 1 : 0,
			bCanonicalShellEnabled ? ShellSmoothingPerStep : 0.0f,
			bCanonicalShellEnabled && bShellReprojectSmooth ? 1 : 0,
			MaximumCorrectionCm,
			bFailClosedOnMissingLOD ? 1 : 0);
		return FMD5::HashAnsiString(*Canonical);
	}
};

/**
 * Legacy read-only migration schema for DT_EFClothingGarmentTuning. Runtime no
 * longer consumes this struct; it remains registered so the one-time Director
 * migration can deserialize and retire the old table safely.
 */
USTRUCT()
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingGarmentTuningRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY()
	bool bEnableTuning = true;

	UPROPERTY()
	float AdditionalClearanceCm = 0.0f;

	UPROPERTY()
	FText Notes;
};
