#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Misc/SecureHash.h"
#include "EFClothingGarmentCatalog.generated.h"

class USkeletalMesh;

/** Internal runtime backend. V4 selects the safe automatic backend for new rows. */
UENUM(BlueprintType)
enum class EEFClothingSurfaceBackend : uint8
{
	GeometryFitFallback UMETA(DisplayName = "Geometry Fit Fallback"),
	SurfaceWrapGPU UMETA(DisplayName = "Automatic GPU Surface Wrap"),
	Disabled UMETA(Hidden)
};

/** Optional authoring hint for the automatic surface classifier. */
UENUM(BlueprintType)
enum class EEFClothingFitPolicy : uint8
{
	Auto UMETA(DisplayName = "Automatic (Recommended)"),
	Tight UMETA(DisplayName = "Close Fitting"),
	Hybrid UMETA(DisplayName = "Hybrid"),
	Loose UMETA(DisplayName = "Loose"),
	Rigid UMETA(DisplayName = "Rigid")
};

/** Method used by the source-preserving, Unreal-style structural offset. */
UENUM(BlueprintType)
enum class EEFClothingNativeOffsetType : uint8
{
	Iterative UMETA(DisplayName = "Iterative")
};

/**
 * Parameters for an explicit Unreal-style authoring operation. Editing these
 * values alone changes nothing; the user must press the corresponding button.
 */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingNativeUEOffsetSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced Mesh Edit", meta = (DisplayName = "Offset Type", ToolTip = "Uses the same iterative surface-offset idea as Unreal Engine's Modeling tools."))
	EEFClothingNativeOffsetType OffsetType = EEFClothingNativeOffsetType::Iterative;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced Mesh Edit", meta = (ClampMin = "-5.0", ClampMax = "5.0", UIMin = "-1.0", UIMax = "1.0", Units = "cm", DisplayName = "Distance (cm)", ToolTip = "Distance used when Apply Native Offset to Clothing Mesh is pressed. Positive values move the mesh outward; negative values move it inward. Editing this value alone does not change any mesh."))
	float DistanceCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced Mesh Edit", meta = (ClampMin = "1", ClampMax = "100", UIMin = "1", UIMax = "32", DisplayName = "Steps", ToolTip = "Number of offset passes. More steps can follow curved surfaces more closely, but the Apply action takes longer."))
	int32 Steps = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced Mesh Edit", meta = (DisplayName = "Offset Boundaries", ToolTip = "Moves open edges such as waistbands, cuffs, and hems with the rest of the surface."))
	bool bOffsetBoundaries = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced Mesh Edit", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", DisplayName = "Smoothing Per Step", ToolTip = "Smooths the result during each pass. Leave this at zero to preserve seams and silhouette."))
	float SmoothingPerStep = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced Mesh Edit", meta = (DisplayName = "Reproject After Smoothing", ToolTip = "Moves smoothed vertices back toward the offset surface. Use this only when smoothing changes the intended shape."))
	bool bReprojectAfterSmoothing = false;
};

/**
 * One self-contained garment definition in the EF Clothing Morph Director.
 * SourceGarment is the only author-edited mesh; generated runtime artifacts are
 * deliberately absent from this public schema.
 */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingGarmentRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clothing Setup", meta = (DisplayName = "Use This Clothing", ToolTip = "Turns automatic fitting on for this clothing. An unfinished entry stays a draft and cannot disable other clothes."))
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clothing Setup", meta = (DisplayName = "Clothing Name", ToolTip = "A unique, stable name used by gameplay and saved data. A name is created automatically after both meshes are assigned, and you may edit it at any time before release."))
	FName GarmentId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clothing Setup", meta = (DisplayName = "Clothing Mesh", ToolTip = "The clothing mesh shown in game. You may edit and save it with Unreal Engine's native Skeletal Mesh tools. EF Clothing Morph never replaces it."))
	TSoftObjectPtr<USkeletalMesh> SourceGarment;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clothing Setup", meta = (DisplayName = "Body Mesh", ToolTip = "The exact visible body this clothing must follow, for example Female or Male. The body mesh, skin weights, and shared skeleton are never modified."))
	TSoftObjectPtr<USkeletalMesh> BodySurface;

	/** Internal/developer-only profile selector retained for compiler compatibility. */
	UPROPERTY()
	FName NativeSkinWeightProfile = NAME_None;

	/** Legacy V2 classifier input. V3 is always native deformation plus a collision-only surface guard. */
	UPROPERTY()
	EEFClothingFitPolicy FitPolicy = EEFClothingFitPolicy::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Live Fit", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "0.5", Units = "cm", DisplayName = "Skin Gap (cm)", ToolTip = "Adds space between this clothing and the animated skin. It updates immediately and affects only this entry. Start with 0.02 to 0.10 cm."))
	float AdditionalClearanceCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Live Fit", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "0.5", Units = "cm", DisplayName = "Surface Volume (cm)", ToolTip = "Makes this clothing look fuller by moving its visible surface outward. It updates immediately and does not create new geometry."))
	float ShellThicknessCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced Mesh Edit", meta = (DisplayName = "Native Offset Settings", ShowOnlyInnerProperties, ToolTip = "Settings for an explicit Unreal mesh edit. Changing these values alone does nothing. Use Skin Gap or Surface Volume for immediate, non-destructive tuning."))
	FEFClothingNativeUEOffsetSettings NativeUEOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Hiding", meta = (DisplayName = "Body Sections to Hide", ToolTip = "Body material-slot names hidden while this clothing is equipped. Use this for covered auxiliary anatomy. The body asset itself is not changed."))
	TArray<FName> BodySectionsToExclude;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Hiding", meta = (DisplayName = "Covered Body Areas", ToolTip = "Optional gameplay tags that describe which body areas this clothing covers. These tags do not change its shape."))
	FGameplayTagContainer CoverageTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notes", meta = (MultiLine = "true", DisplayName = "Notes", ToolTip = "Optional notes for this clothing. Notes do not affect fitting or gameplay."))
	FText Notes;

	// ---------------------------------------------------------------------
	// Legacy/internal serialized compatibility. These fields intentionally
	// remain reflected for existing assets and current runtime/compiler code,
	// but are not exposed in the V3 Director Details panel.
	// ---------------------------------------------------------------------

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use GarmentId as the stable visible identity."))
	FText DisplayName;

	UPROPERTY()
	EEFClothingSurfaceBackend Backend = EEFClothingSurfaceBackend::SurfaceWrapGPU;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use BodySectionsToExclude."))
	TArray<FName> HiddenBodyMaterialSlots;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use BodySectionsToExclude."))
	TArray<FName> ExcludedBodySurfaceMaterialSlots;

	UPROPERTY()
	TArray<FName> ExcludedBodyBoneBranches;

	UPROPERTY()
	TArray<FString> ExcludedBodyMorphPrefixes;

	UPROPERTY()
	float MinimumClearanceMultiplier = 1.0f;

	UPROPERTY()
	float FabricClearanceCm = -1.0f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Skin Clearance is always an immediate per-garment runtime value in V3."))
	bool bEnableRuntimeTuning = true;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "V3 never creates generated shell meshes automatically. Use the explicit native authoring action."))
	bool bCreateThicknessShell = false;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use NativeUEOffset.Steps for an explicit authoring action."))
	int32 ShellOffsetSteps = 10;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use NativeUEOffset.bOffsetBoundaries for an explicit authoring action."))
	bool bShellOffsetBoundaries = true;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use NativeUEOffset.SmoothingPerStep for an explicit authoring action."))
	float ShellSmoothingPerStep = 0.0f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use NativeUEOffset.bReprojectAfterSmoothing for an explicit authoring action."))
	bool bShellReprojectSmooth = false;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use AdditionalClearanceCm."))
	float RuntimeOffsetCm = 0.0f;

	UPROPERTY()
	float MaximumCorrectionCm = -1.0f;

	UPROPERTY()
	bool bFailClosedOnMissingLOD = true;

	/** Returns the V3 exclusion list plus any values serialized by older schemas. */
	TArray<FName> GetEffectiveBodySectionsToExclude() const
	{
		TArray<FName> Result;
		auto AppendUniqueValid = [&Result](const TArray<FName>& Values)
		{
			for (const FName Value : Values)
			{
				if (!Value.IsNone())
				{
					Result.AddUnique(Value);
				}
			}
		};
		AppendUniqueValid(BodySectionsToExclude);
		AppendUniqueValid(HiddenBodyMaterialSlots);
		AppendUniqueValid(ExcludedBodySurfaceMaterialSlots);
		return Result;
	}

	bool IsDisabledEmptyPlaceholder() const
	{
		return !bEnabled
			&& GarmentId.IsNone()
			&& SourceGarment.IsNull()
			&& BodySurface.IsNull();
	}

	/** True once this entry has the minimum identity and mesh pair required for fitting. */
	bool HasCompleteClothingSetup() const
	{
		return !GarmentId.IsNone()
			&& !SourceGarment.IsNull()
			&& !BodySurface.IsNull();
	}

	/** Disabled or incomplete entries are harmless authoring drafts. */
	bool IsClothingDraft() const
	{
		return !bEnabled || !HasCompleteClothingSetup();
	}

	/** Row-local validation used by V4 runtime and compiler consumers. */
	bool ValidateClothingForUse(FString& OutError) const;

	/**
	 * Hash of compile-relevant data. Runtime sliders, notes, and unapplied native
	 * authoring parameters are excluded; applying an authoring action changes the
	 * source mesh content fingerprint instead.
	 */
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

		const TArray<FName> EffectiveExcludedSections = GetEffectiveBodySectionsToExclude();
		const FString Canonical = FString::Printf(
			TEXT("Backend=%d|Fit=%d|NativeSkinProfile=%s|Coverage=%s|BodySections=%s|ExcludedBones=%s|ExcludedMorphs=%s|MinMultiplier=%.6f|Fabric=%.6f|MaxCorrection=%.6f"),
			static_cast<int32>(Backend),
			static_cast<int32>(FitPolicy),
			*NativeSkinWeightProfile.ToString(),
			*FString::Join(TagStrings, TEXT(",")),
			*CanonicalNames(EffectiveExcludedSections),
			*CanonicalNames(ExcludedBodyBoneBranches),
			*CanonicalStrings(ExcludedBodyMorphPrefixes),
			MinimumClearanceMultiplier,
			FabricClearanceCm,
			MaximumCorrectionCm);
		return FMD5::HashAnsiString(*Canonical);
	}
};

/** Legacy migration schema retained so old tuning tables can still deserialize. */
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
