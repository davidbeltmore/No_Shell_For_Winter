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
		if (!IsValid(Registry))
		{
			OutReport = TEXT("V3 orphan cleanup requires the active registry.");
			return false;
		}

		TSet<FName> ActivePackages;
		ActivePackages.Add(Registry->GetOutermost()->GetFName());
		for (const UEFClothingSurfaceBinding* Binding : Registry->NativeSourceBindings)
		{
			if (IsValid(Binding))
			{
				ActivePackages.Add(Binding->GetOutermost()->GetFName());
			}
		}

		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		const FString OutputRootString(EFClothingMorphV3::CompiledOutputRoot);
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

		if (!Blocked.IsEmpty())
		{
			OutReport = FString::Printf(
				TEXT("V3 orphan cleanup was blocked by referencers: %s"),
				*FString::Join(Blocked, TEXT("; ")));
			return false;
		}
		if (Orphans.IsEmpty())
		{
			OutReport = TEXT("V3 binding cleanup: no orphan bindings.");
			return true;
		}

		const int32 DeletedCount = ObjectTools::DeleteAssets(Orphans, false);
		if (DeletedCount != Orphans.Num())
		{
			OutReport = FString::Printf(
				TEXT("V3 binding cleanup deleted %d of %d audited orphan bindings."),
				DeletedCount,
				Orphans.Num());
			return false;
		}
		OutReport = FString::Printf(
			TEXT("V3 binding cleanup deleted %d audited orphan binding(s)."),
			DeletedCount);
		return true;
	}
}

FEFClothingNativeSourceCompileOptions
FEFClothingNativeSourceEditorGate::MakeCanonicalOptions()
{
	FEFClothingNativeSourceCompileOptions Options;
	Options.OutputRoot = EFClothingMorphV3::CompiledOutputRoot;
	Options.MinimumClearanceCm = EFClothingMorphV3::DefaultCollisionClearanceCm;
	Options.MaximumPushCm = 2.5f;
	Options.bOnlyStale = true;
	return Options;
}

FEFClothingNativeSourceEditorGateResult
FEFClothingNativeSourceEditorGate::ValidateOrRefresh(
	UEFClothingMorphDirectorPolicy* Director,
	UEFClothingFitRegistry* Registry,
	USkeletalMesh* CompatibilityReference,
	const bool bRefreshIfStale)
{
	FEFClothingNativeSourceEditorGateResult Result;
	Result.Registry = Registry;

	if (!IsValid(Director) || !IsValid(CompatibilityReference))
	{
		Result.Report = TEXT(
			"V3 native-source gate requires a valid Director and protected compatibility mesh.");
		return Result;
	}

	// Native Skeletal Mesh authoring can leave render/DDC work queued. Finish it
	// before topology fingerprints are compared; this does not resave any mesh.
	FAssetCompilingManager::Get().FinishAllCompilation();
	const FEFClothingNativeSourceCompileOptions Options = MakeCanonicalOptions();
	const FEFClothingNativeSourceFreshnessResult Freshness =
		UEFClothingFitCompilerLibrary::ValidateNativeSourceCatalogV3(
			Director,
			Registry,
			CompatibilityReference,
			Options);
	if (Freshness.bFresh)
	{
		FString CleanupReport;
		if (!EFClothingNativeSourceEditorGatePrivate::CleanupOrphanBindings(
			Registry,
			CleanupReport))
		{
			Result.Report = FString::Printf(
				TEXT("%s | %s"),
				*Freshness.Report,
				*CleanupReport);
			return Result;
		}
		Result.bSuccess = true;
		Result.bWasFresh = true;
		Result.Report = FString::Printf(
			TEXT("%s | %s"),
			*Freshness.Report,
			*CleanupReport);
		return Result;
	}

	Result.StaleReason = Freshness.Report;
	if (!bRefreshIfStale)
	{
		Result.Report = Freshness.Report;
		return Result;
	}

	const FEFClothingNativeSourceCatalogCompileResult CompileResult =
		UEFClothingFitCompilerLibrary::CompileNativeSourceCatalogV3(
			Director,
			CompatibilityReference,
			Options);
	Result.Registry = CompileResult.Registry;
	if (!CompileResult.bSuccess || !IsValid(CompileResult.Registry))
	{
		Result.Report = FString::Printf(
			TEXT("%s | Refresh failed: %s"),
			*Freshness.Report,
			*CompileResult.Report);
		return Result;
	}

	FAssetCompilingManager::Get().FinishAllCompilation();
	const FEFClothingNativeSourceFreshnessResult PostRefresh =
		UEFClothingFitCompilerLibrary::ValidateNativeSourceCatalogV3(
			Director,
			CompileResult.Registry,
			CompatibilityReference,
			Options);
	FString CleanupReport;
	const bool bCleanupSucceeded = PostRefresh.bFresh
		&& EFClothingNativeSourceEditorGatePrivate::CleanupOrphanBindings(
			CompileResult.Registry,
			CleanupReport);
	Result.bSuccess = PostRefresh.bFresh && bCleanupSucceeded;
	Result.bRefreshed = Result.bSuccess;
	Result.Report = Result.bSuccess
		? FString::Printf(
			TEXT("%s | %s | %s"),
			*CompileResult.Report,
			*PostRefresh.Report,
			*CleanupReport)
		: FString::Printf(
			TEXT("%s | Post-refresh validation failed: %s"),
			*CompileResult.Report,
			PostRefresh.bFresh ? *CleanupReport : *PostRefresh.Report);
	return Result;
}
