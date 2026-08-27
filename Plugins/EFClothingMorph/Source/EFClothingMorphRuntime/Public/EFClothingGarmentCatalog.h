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
	GeometryFitFallback UMETA(DisplayName = "Ajuste geometrico de respaldo"),

	/** Final GPU surface-wrap backend; rows can opt in as its cooked graph becomes available. */
	SurfaceWrapGPU UMETA(DisplayName = "Ajuste automatico GPU (Recomendado)"),

	Disabled UMETA(Hidden)
};

/** Authored override for the compiler's automatic tight/loose classification. */
UENUM(BlueprintType)
enum class EEFClothingFitPolicy : uint8
{
	Auto UMETA(DisplayName = "Automatico (Recomendado)"),
	Tight UMETA(DisplayName = "Ajustado al cuerpo"),
	Hybrid UMETA(DisplayName = "Hibrido"),
	Loose UMETA(DisplayName = "Suelto"),
	Rigid UMETA(DisplayName = "Rigido")
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01 | Identidad de la prenda", meta = (DisplayName = "Indice / ID de la prenda", ToolTip = "Nombre unico y estable de esta entrada. No lo cambies despues de compilar la prenda."))
	FName GarmentId = NAME_None;

	/** Friendly name shown by the Clothing Director. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01 | Identidad de la prenda", meta = (DisplayName = "Nombre visible", ToolTip = "Nombre facil de reconocer dentro del Director."))
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01 | Identidad de la prenda", meta = (DisplayName = "Usar como prenda", ToolTip = "Activalo para que EF Clothing Morph compile y controle esta mesh como ropa."))
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01 | Identidad de la prenda", meta = (DisplayName = "Mesh original de la prenda", ToolTip = "Selecciona la mesh original usada por el inventario/ACF. Nunca selecciones una SK_ generada por EF Clothing Morph."))
	TSoftObjectPtr<USkeletalMesh> SourceGarment;

	/** Exact visible body surface for this compiled row (Female now, Male later). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "01 | Identidad de la prenda", meta = (DisplayName = "Cuerpo de referencia", ToolTip = "Cuerpo DAZ al que debe ajustarse esta prenda, por ejemplo Female o Male."))
	TSoftObjectPtr<USkeletalMesh> BodySurface;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02 | Ajuste automatico", meta = (DisplayName = "Metodo de ajuste", ToolTip = "Usa Ajuste automatico GPU para prendas nuevas. El respaldo geometrico se conserva solo por compatibilidad."))
	EEFClothingSurfaceBackend Backend = EEFClothingSurfaceBackend::SurfaceWrapGPU;

	/** Auto classifies connected garment regions; explicit modes remain available for authoring exceptions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02 | Ajuste automatico", meta = (DisplayName = "Tipo de ajuste", ToolTip = "Automatico detecta si cada zona debe seguir la piel o conservar movimiento. Cambialo solo si una prenda necesita una excepcion."))
	EEFClothingFitPolicy FitPolicy = EEFClothingFitPolicy::Auto;

	/** Semantic coverage used by gameplay/UI and future body-region proxy masks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03 | Cobertura corporal", meta = (DisplayName = "Zonas que cubre", ToolTip = "Describe las regiones del cuerpo cubiertas por esta prenda."))
	FGameplayTagContainer CoverageTags;

	/**
	 * Body material slots hidden only on the live body component while this garment
	 * is applied. Visibility is reference-counted and restored exactly on unequip.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03 | Cobertura corporal", meta = (DisplayName = "Partes del cuerpo a ocultar", ToolTip = "Materiales del cuerpo que deben ocultarse mientras esta prenda esta equipada. Se restauran al desequiparla."))
	TArray<FName> HiddenBodyMaterialSlots;

	/**
	 * Body sections that must not participate in the compiler's closest-surface,
	 * clearance or skin-weight projection. This is distinct from visual hiding:
	 * auxiliary anatomy can be hidden at runtime and also excluded from the
	 * mathematical body envelope without hard-coding a garment or DAZ product.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "99 | Opciones avanzadas", meta = (AdvancedDisplay, DisplayName = "Superficies excluidas del ajuste", ToolTip = "Materiales corporales que el solver no debe usar como superficie de referencia."))
	TArray<FName> ExcludedBodySurfaceMaterialSlots;

	/**
	 * Root bones of optional anatomy branches that must never drive this garment.
	 * The compiler redirects their influence to the nearest non-excluded,
	 * hierarchy-compatible ancestor on the generated EF_AutoFit profile only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "99 | Opciones avanzadas", meta = (AdvancedDisplay, DisplayName = "Ramas oseas excluidas", ToolTip = "Ramas de huesos auxiliares que nunca deben transferir peso a esta prenda."))
	TArray<FName> ExcludedBodyBoneBranches;

	/** Body morph namespaces belonging to an excluded accessory/graft. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "99 | Opciones avanzadas", meta = (AdvancedDisplay, DisplayName = "Prefijos de morph excluidos", ToolTip = "Morphs de anatomia auxiliar que no deben participar en el ajuste."))
	TArray<FString> ExcludedBodyMorphPrefixes;

	/** Optional authored floor; it is always rounded upward to a compiler-certified tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "99 | Opciones avanzadas", meta = (ClampMin = "1.0", ClampMax = "2.0", AdvancedDisplay, DisplayName = "Multiplicador minimo de separacion", ToolTip = "Piso heredado de separacion. Normalmente debe permanecer en 1.0."))
	float MinimumClearanceMultiplier = 1.0f;

	/** Negative means compiler-selected from geometry/fabric data; otherwise an absolute cm floor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02 | Ajuste automatico", meta = (ClampMin = "-1.0", ClampMax = "5.0", UIMin = "-1.0", UIMax = "2.0", Units = "cm", DisplayName = "Separacion de la tela (cm; -1 = Automatico)", ToolTip = "Separacion base de esta tela respecto a la piel. Deja -1 para que el compilador la calcule.", AdvancedDisplay))
	float FabricClearanceCm = -1.0f;

	/** Enables this garment index's topology-free runtime clearance override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04 | Offset de esta prenda", meta = (DisplayName = "Activar offset de esta prenda", ToolTip = "Afecta solamente este indice del catalogo. Si esta apagado, se ignora el offset extra de esta prenda."))
	bool bEnableRuntimeTuning = true;

	/** Extra outward clearance applied after skinning and morphs, in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04 | Offset de esta prenda", meta = (ClampMin = "0.0", ClampMax = "0.35", UIMin = "0.0", UIMax = "0.35", Units = "cm", DisplayName = "Offset extra hacia afuera (cm)", ToolTip = "Empuja solamente esta prenda hacia afuera despues de animaciones y morphs. Prueba primero 0.05 cm. No requiere recompilar.", EditCondition = "bEnableRuntimeTuning"))
	float AdditionalClearanceCm = 0.0f;

	/** Hidden schema-1 field retained only until legacy DataTables are retired. */
	UPROPERTY()
	float RuntimeOffsetCm = 0.0f;

	/** Optional human-facing note; it has no runtime or compiler effect. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "05 | Notas", meta = (MultiLine = "true", DisplayName = "Notas de esta prenda", ToolTip = "Texto libre para recordar decisiones o excepciones. No afecta el runtime."))
	FText Notes;

	/** Negative means compiler-selected; otherwise the hard one-frame outward correction bound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "99 | Opciones avanzadas", meta = (ClampMin = "-1.0", ClampMax = "10.0", UIMin = "-1.0", UIMax = "10.0", Units = "cm", DisplayName = "Correccion maxima (cm; -1 = Automatico)", ToolTip = "Limite de correccion por frame. Deja -1 para que el sistema lo calcule.", AdvancedDisplay))
	float MaximumCorrectionCm = -1.0f;

	/** Missing or stale body/garment LOD bindings must never expose the uncorrected garment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "99 | Opciones avanzadas", meta = (AdvancedDisplay, DisplayName = "Ocultar si falta ajuste de un LOD", ToolTip = "Evita mostrar una prenda sin binding valido durante cambios de LOD."))
	bool bFailClosedOnMissingLOD = true;

	/** Hash of compile-relevant authoring only; runtime offsets and notes are excluded. */
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

		const FString Canonical = FString::Printf(
			TEXT("Backend=%d|Fit=%d|Coverage=%s|Hidden=%s|ExcludedSurface=%s|ExcludedBones=%s|ExcludedMorphs=%s|MinMultiplier=%.6f|Fabric=%.6f|MaxCorrection=%.6f|FailClosedLOD=%d"),
			static_cast<int32>(Backend),
			static_cast<int32>(FitPolicy),
			*FString::Join(TagStrings, TEXT(",")),
			*CanonicalNames(HiddenBodyMaterialSlots),
			*CanonicalNames(ExcludedBodySurfaceMaterialSlots),
			*CanonicalNames(ExcludedBodyBoneBranches),
			*CanonicalStrings(ExcludedBodyMorphPrefixes),
			MinimumClearanceMultiplier,
			FabricClearanceCm,
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
