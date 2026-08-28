#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Misc/SecureHash.h"
#include "EFClothingGarmentCatalog.generated.h"

class USkeletalMesh;

/** Internal runtime backend. V3 selects the safe automatic backend for new rows. */
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Native UE Offset", meta = (DisplayName = "Offset Type", ToolTip = "Uses the same iterative surface-offset concept as Unreal Engine's Modeling tools. More methods can be added later without changing the garment catalog."))
	EEFClothingNativeOffsetType OffsetType = EEFClothingNativeOffsetType::Iterative;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Native UE Offset", meta = (ClampMin = "-5.0", ClampMax = "5.0", UIMin = "-1.0", UIMax = "1.0", Units = "cm", DisplayName = "Distance (cm)", ToolTip = "Distance used when Apply Native Offset to Editable Mesh is pressed. Positive values move the mesh outward; negative values move it inward. Editing this value alone does not change any mesh."))
	float DistanceCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Native UE Offset", meta = (ClampMin = "1", ClampMax = "100", UIMin = "1", UIMax = "32", DisplayName = "Steps", ToolTip = "Number of iterative offset passes. More steps can follow curved surfaces more closely, but the explicit Apply operation takes longer."))
	int32 Steps = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Native UE Offset", meta = (DisplayName = "Offset Boundaries", ToolTip = "Offsets open boundaries such as waistbands, cuffs, and hems together with the rest of the surface."))
	bool bOffsetBoundaries = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Native UE Offset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", DisplayName = "Smoothing Per Step", ToolTip = "Smooths the offset result during each pass. Leave at zero to preserve authored seams and silhouette."))
	float SmoothingPerStep = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Native UE Offset", meta = (DisplayName = "Reproject After Smoothing", ToolTip = "Reprojects smoothed vertices toward the offset surface. Use this only when smoothing changes the intended silhouette."))
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Garment", meta = (DisplayName = "Use as Garment", ToolTip = "Enables automatic fitting for this entry. Keep it disabled while the garment mesh or reference body is not assigned."))
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Garment", meta = (DisplayName = "Garment ID / Index", ToolTip = "Unique and stable ID used by gameplay and saved data. Array order is only for organization; do not rename an ID after release."))
	FName GarmentId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Garment", meta = (DisplayName = "Editable Garment Mesh", ToolTip = "The authoritative garment mesh. You may edit and save this mesh with Unreal Engine's native Skeletal Mesh tools. EF Clothing Morph only rebuilds or updates hidden runtime data and never overwrites this source."))
	TSoftObjectPtr<USkeletalMesh> SourceGarment;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Garment", meta = (DisplayName = "Reference Body Mesh", ToolTip = "The exact visible DAZ body this garment must fit, for example Female or Male. The body mesh, its weights, and the shared skeleton are never modified."))
	TSoftObjectPtr<USkeletalMesh> BodySurface;

	/** Internal/developer-only profile selector retained for compiler compatibility. */
	UPROPERTY()
	FName NativeSkinWeightProfile = NAME_None;

	/** Legacy V2 classifier input. V3 is always native deformation plus a collision-only surface guard. */
	UPROPERTY()
	EEFClothingFitPolicy FitPolicy = EEFClothingFitPolicy::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runtime Fit", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "0.5", Units = "cm", DisplayName = "Skin Clearance (cm, Runtime)", ToolTip = "Moves only this garment outward from the final animated skin. It updates immediately, requires no rebuild, and does not add material thickness. Start with small values such as 0.02 to 0.10 cm."))
	float AdditionalClearanceCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runtime Fit", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "0.5", Units = "cm", DisplayName = "Surface Inflate (cm, Runtime)", ToolTip = "Moves this garment's rendered surface outward along the animated body normal. It updates immediately, but it does not create geometry, an inner layer, or side walls. If Create Shell is pressed explicitly, this value is also used as the requested real shell thickness."))
	float ShellThicknessCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Native UE Offset", meta = (DisplayName = "Native UE Offset", ShowOnlyInnerProperties, ToolTip = "Parameters for an explicit native mesh-editing action. Changing these values alone has no effect. Press Apply Native Offset to Editable Mesh when ready, then refresh the binding. Use Skin Clearance or Surface Inflate for non-destructive runtime tuning."))
	FEFClothingNativeUEOffsetSettings NativeUEOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Coverage", meta = (DisplayName = "Body Sections to Exclude", ToolTip = "Body material-slot names that are hidden while this garment is equipped and excluded from fitting and collision. Use this for covered auxiliary anatomy such as graft sections. The body asset itself is not changed."))
	TArray<FName> BodySectionsToExclude;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Coverage", meta = (DisplayName = "Covered Body Regions", ToolTip = "Optional gameplay tags describing the regions covered by this garment. These tags do not deform the garment."))
	FGameplayTagContainer CoverageTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notes", meta = (MultiLine = "true", DisplayName = "Garment Notes", ToolTip = "Optional authoring notes for this garment. Notes have no compiler or runtime effect."))
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
