#pragma once

#include "CoreMinimal.h"
#include "EFClothingFitCompilerLibrary.h"

class UEFClothingFitRegistry;
class UEFClothingMorphDirectorPolicy;
class USkeletalMesh;

/** Result consumed by the editor module's pre-PIE authorization path. */
struct FEFClothingNativeSourceEditorGateResult
{
	bool bSuccess = false;
	bool bWasFresh = false;
	bool bRefreshed = false;
	bool bDegraded = false;
	UEFClothingFitRegistry* Registry = nullptr;
	FString StaleReason;
	FString WarningReport;
	FString Report;
};

/**
 * V4 editor freshness helper kept separate from module lifecycle code.
 * It reads source meshes and may create only project-owned binding assets.
 */
class FEFClothingNativeSourceEditorGate
{
public:
	static FEFClothingNativeSourceCompileOptions MakeCanonicalOptions();

	static FEFClothingNativeSourceEditorGateResult ValidateOrRefresh(
		UEFClothingMorphDirectorPolicy* Director,
		UEFClothingFitRegistry* Registry,
		USkeletalMesh* CompatibilityReference,
		bool bRefreshIfStale,
		FName TargetClothingName = NAME_None,
		bool bStrictCatalogCertification = false);
};
