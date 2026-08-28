#include "EFClothingNativeSourceEditorGate.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EFClothingFitProfile.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingSurfaceBinding.h"
#include "Engine/SkeletalMesh.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"

namespace EFClothingNativeSourceEditorGatePrivate
{
	static bool CleanupOrphanBindings(
		UEFClothingFitRegistry* Registry,
		FString& OutReport)
	{
		OutReport.Reset();
		TSet<FName> ActivePackages;
		if (IsValid(Registry))
		{
			ActivePackages.Add(Registry->GetOutermost()->GetFName());
			for (const UEFClothingSurfaceBinding* Binding : Registry->NativeSourceBindings)
			{
				if (IsValid(Binding))
				{
					ActivePackages.Add(Binding->GetOutermost()->GetFName());
				}
			}
		}

		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		const FString OutputRootString(EFClothingMorphV4::CompiledOutputRoot);
		const FName OutputRoot(*OutputRootString);
		AssetRegistry.ScanPathsSynchronous({OutputRootString}, true);

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(OutputRoot, Assets, true, false);
		TArray<FAssetData> Orphans;
		TArray<FString> Blocked;
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetClassPath != UEFClothingSurfaceBinding::StaticClass()->GetClassPathName()
				|| ActivePackages.Contains(Asset.PackageName))
			{
				continue;
			}

			TArray<FName> Referencers;
			AssetRegistry.GetReferencers(
				Asset.PackageName,
				Referencers,
				UE::AssetRegistry::EDependencyCategory::All);
			Referencers.Remove(Asset.PackageName);
			Referencers.Sort([](const FName A, const FName B)
			{
				return A.LexicalLess(B);
			});
			if (!Referencers.IsEmpty())
			{
				Blocked.Add(FString::Printf(
					TEXT("%s <- %s"),
					*Asset.PackageName.ToString(),
					*FString::JoinBy(
						Referencers,
						TEXT(", "),
						[](const FName Name) { return Name.ToString(); })));
				continue;
			}
			Orphans.Add(Asset);
		}

		if (Orphans.IsEmpty() && Blocked.IsEmpty())
		{
			OutReport = TEXT("V4 cleanup: no unused bindings.");
			return true;
		}

		const int32 DeletedCount = Orphans.IsEmpty()
			? 0
			: ObjectTools::DeleteAssets(Orphans, false);
		if (!Blocked.IsEmpty() || DeletedCount != Orphans.Num())
		{
			OutReport = FString::Printf(
				TEXT("V4 cleanup warning: removed %d of %d unused bindings; referenced items kept: %s"),
				DeletedCount,
				Orphans.Num(),
				Blocked.IsEmpty() ? TEXT("none") : *FString::Join(Blocked, TEXT("; ")));
			return false;
		}
		OutReport = FString::Printf(
			TEXT("V4 cleanup removed %d unused binding(s)."),
			DeletedCount);
		return true;
	}
}

FEFClothingNativeSourceCompileOptions
FEFClothingNativeSourceEditorGate::MakeCanonicalOptions()
{
	FEFClothingNativeSourceCompileOptions Options;
	Options.OutputRoot = EFClothingMorphV4::CompiledOutputRoot;
	Options.MinimumClearanceCm = EFClothingMorphV4::DefaultCollisionClearanceCm;
	Options.MaximumPushCm = 2.5f;
	Options.bOnlyStale = true;
	Options.TargetClothingName = NAME_None;
	Options.bStrictCatalogCertification = false;
	return Options;
}

FEFClothingNativeSourceEditorGateResult
FEFClothingNativeSourceEditorGate::ValidateOrRefresh(
	UEFClothingMorphDirectorPolicy* Director,
	UEFClothingFitRegistry* Registry,
	USkeletalMesh* CompatibilityReference,
	const bool bRefreshIfStale,
	const FName TargetClothingName,
	const bool bStrictCatalogCertification)
{
	FEFClothingNativeSourceEditorGateResult Result;
	Result.Registry = Registry;

	if (!IsValid(Director) || !IsValid(CompatibilityReference))
	{
		Result.Report = TEXT(
			"V4 clothing gate requires a valid Director and protected compatibility mesh.");
		return Result;
	}

	// Native Skeletal Mesh authoring can leave render/DDC work queued. Finish it
	// before topology fingerprints are compared; this does not resave any mesh.
	FAssetCompilingManager::Get().FinishAllCompilation();
	FEFClothingNativeSourceCompileOptions Options = MakeCanonicalOptions();
	Options.TargetClothingName = TargetClothingName;
	Options.bStrictCatalogCertification = bStrictCatalogCertification;
	const FEFClothingNativeSourceFreshnessResult Freshness =
		UEFClothingFitCompilerLibrary::ValidateNativeSourceCatalogV4(
			Director,
			Registry,
			CompatibilityReference,
			Options);
	if (Freshness.bFresh)
	{
		FString CleanupReport;
		const bool bCleanupSucceeded =
			EFClothingNativeSourceEditorGatePrivate::CleanupOrphanBindings(
			Registry,
			CleanupReport);
		Result.bSuccess = true;
		Result.bWasFresh = true;
		Result.bDegraded = Freshness.DraftRowCount > 0
			|| Freshness.InvalidRowCount > 0
			|| !bCleanupSucceeded;
		if (!bCleanupSucceeded)
		{
			Result.WarningReport = CleanupReport;
		}
		Result.Report = FString::Printf(
			TEXT("%s | %s"),
			*Freshness.Report,
			*CleanupReport);
		return Result;
	}

	Result.StaleReason = Freshness.Report;
	if (!bRefreshIfStale)
	{
		if (!bStrictCatalogCertification && Freshness.ValidBindingCount > 0)
		{
			Result.bSuccess = true;
			Result.bDegraded = true;
			Result.WarningReport = Freshness.Report;
		}
		Result.Report = Freshness.Report;
		return Result;
	}

	const FEFClothingNativeSourceCatalogCompileResult CompileResult =
		UEFClothingFitCompilerLibrary::CompileNativeSourceCatalogV4(
			Director,
			CompatibilityReference,
			Options);
	Result.Registry = CompileResult.Registry;
	if (!CompileResult.bSuccess || !IsValid(CompileResult.Registry))
	{
		FString CleanupReport;
		const bool bCleanupNeeded = IsValid(CompileResult.Registry)
			|| !CompileResult.Rows.IsEmpty();
		const bool bCleanupSucceeded = bCleanupNeeded
			? EFClothingNativeSourceEditorGatePrivate::CleanupOrphanBindings(
				CompileResult.Registry,
				CleanupReport)
			: true;
		if (!bCleanupNeeded)
		{
			CleanupReport = TEXT("V4 cleanup was not needed because no registry transaction started.");
		}
		if (!bCleanupSucceeded)
		{
			Result.bDegraded = true;
			Result.WarningReport = CleanupReport;
		}
		Result.Report = FString::Printf(
			TEXT("%s | Refresh failed: %s | %s"),
			*Freshness.Report,
			*CompileResult.Report,
			*CleanupReport);
		return Result;
	}

	FAssetCompilingManager::Get().FinishAllCompilation();
	const FEFClothingNativeSourceFreshnessResult PostRefresh =
		UEFClothingFitCompilerLibrary::ValidateNativeSourceCatalogV4(
			Director,
			CompileResult.Registry,
			CompatibilityReference,
			Options);
	FString CleanupReport;
	const bool bCleanupSucceeded =
		EFClothingNativeSourceEditorGatePrivate::CleanupOrphanBindings(
			CompileResult.Registry,
			CleanupReport);
	Result.bSuccess = true;
	Result.bRefreshed = CompileResult.PublishedRowCount > 0;
	Result.bDegraded = CompileResult.DraftRowCount > 0
		|| CompileResult.FailedRowCount > 0
		|| PostRefresh.InvalidRowCount > 0
		|| PostRefresh.StaleRowCount > 0
		|| !bCleanupSucceeded;
	if (!bCleanupSucceeded)
	{
		Result.WarningReport = CleanupReport;
	}
	Result.Report = FString::Printf(
		TEXT("%s | %s | %s"),
		*CompileResult.Report,
		*PostRefresh.Report,
		*CleanupReport);
	return Result;
}
