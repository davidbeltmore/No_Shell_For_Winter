#include "Calysto/EFCalystoV6EditorValidationLibrary.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"

namespace EFCalystoV6EditorValidationPrivate
{
	static const FAssetBundleData* FindSerializedBundles(
		const FName PackageName, TArray<FAssetData>& OutRows)
	{
		IAssetRegistry& Registry = IAssetRegistry::GetChecked();
		Registry.GetAssetsByPackageName(
			PackageName, OutRows, true, true);
		if (OutRows.Num() != 1 || !OutRows[0].TaggedAssetBundles.IsValid())
		{
			return nullptr;
		}
		return OutRows[0].TaggedAssetBundles.Get();
	}
}

TArray<FString>
UEFCalystoV6EditorValidationLibrary::GetSerializedAssetBundleNames(
	const FName PackageName)
{
	TArray<FAssetData> Rows;
	const FAssetBundleData* Bundles =
		EFCalystoV6EditorValidationPrivate::FindSerializedBundles(
			PackageName, Rows);
	TArray<FString> Result;
	if (!Bundles)
	{
		return Result;
	}
	Result.Reserve(Bundles->Bundles.Num());
	for (const FAssetBundleEntry& Entry : Bundles->Bundles)
	{
		Result.Add(Entry.BundleName.ToString());
	}
	Result.Sort([](const FString& A, const FString& B)
	{
		return A.Compare(B, ESearchCase::IgnoreCase) < 0;
	});
	return Result;
}

TArray<FString>
UEFCalystoV6EditorValidationLibrary::GetSerializedAssetBundleAssetPaths(
	const FName PackageName, const FName BundleName)
{
	TArray<FAssetData> Rows;
	const FAssetBundleData* Bundles =
		EFCalystoV6EditorValidationPrivate::FindSerializedBundles(
			PackageName, Rows);
	TArray<FString> Result;
	if (!Bundles)
	{
		return Result;
	}
	const FAssetBundleEntry* Entry = Bundles->Bundles.FindByPredicate(
		[BundleName](const FAssetBundleEntry& Candidate)
		{
			return Candidate.BundleName == BundleName;
		});
	if (!Entry)
	{
		return Result;
	}
	Result.Reserve(Entry->AssetPaths.Num());
	for (const FTopLevelAssetPath& Path : Entry->AssetPaths)
	{
		Result.Add(Path.ToString());
	}
	Result.Sort([](const FString& A, const FString& B)
	{
		return A.Compare(B, ESearchCase::IgnoreCase) < 0;
	});
	return Result;
}
