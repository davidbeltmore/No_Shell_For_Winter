#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFClothingFitProfile.generated.h"

class USkeletalMesh;

UENUM(BlueprintType)
enum class EEFClothingFitMode : uint8
{
	Tight,
	Hybrid,
	Loose,
	Rigid
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingMorphBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	FName BodyMorph = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	FName GarmentMorph = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	float Scale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	float Bias = 0.0f;
};

/**
 * Immutable runtime contract produced by the EF Clothing Morph V2 editor compiler.
 * It references a source garment but only ever writes to a generated derivative.
 */
UCLASS(BlueprintType)
class EFCLOTHINGMORPHRUNTIME_API UEFClothingFitProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> SourceGarment;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> FittedGarment;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> BodySurface;

	/** Multiple is compatibility evidence only. Runtime never assigns or mutates it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> CompatibilityReference;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FGuid BuildGuid;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 CompilerVersion = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EEFClothingFitMode FitMode = EEFClothingFitMode::Tight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skinning")
	FName SkinWeightProfileName = TEXT("EF_AutoFit");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	FName ClearanceMorphName = TEXT("EF_AutoFit_Clearance");

	/** Runtime value for the baked clearance morph. A value of 1 applies the compiled clearance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DefaultClearanceValue = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	float CompiledMinimumClearanceCm = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	float CompiledMaxPushCm = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	TArray<FEFClothingMorphBinding> MorphBindings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString SourceSkeletonFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString BodySkeletonFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString CompatibilitySkeletonFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 SourceVertexCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 AdjustedVertexCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 PenetratingVertexCountBefore = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 PenetratingVertexCountAfter = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	float MinimumSignedGapBeforeCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	float MinimumSignedGapAfterCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 TransferredMorphCount = 0;

	bool MatchesSource(const USkeletalMesh* Mesh) const;
};

UCLASS(BlueprintType)
class EFCLOTHINGMORPHRUNTIME_API UEFClothingFitRegistry : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EF Clothing Morph V2")
	TArray<TObjectPtr<UEFClothingFitProfile>> Profiles;

	const UEFClothingFitProfile* FindProfileForSource(const USkeletalMesh* SourceMesh) const;
};
