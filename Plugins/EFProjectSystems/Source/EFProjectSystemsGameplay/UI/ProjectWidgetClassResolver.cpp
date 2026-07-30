#include "UI/ProjectWidgetClassResolver.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EFProjectUISettings.h"
#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectWidgetClassResolver, Log, All);

namespace ProjectWidgetClassResolverPrivate
{
	static constexpr TCHAR DefaultWidgetRootPath[] = TEXT("/Game/_Game/Widgets");

	struct FDiscoveredWidgetClass
	{
		FString PackageName;
		UClass* WidgetClass = nullptr;
		bool bDirectNativeParent = false;
		int32 PriorityScore = 0;
		FString PriorityReason;
	};

	static TMap<FString, TWeakObjectPtr<UClass>> GResolvedWidgetClassCache;

	static TArray<FName> ResolveWidgetSearchRoots()
	{
		TArray<FName> SearchRoots;

		if (const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get())
		{
			for (const FDirectoryPath& RootPath : UISettings->WidgetDiscoveryRootPaths)
			{
				const FString CleanPath = RootPath.Path.TrimStartAndEnd();
				if (!CleanPath.IsEmpty() && CleanPath.StartsWith(TEXT("/")))
				{
					SearchRoots.AddUnique(FName(*CleanPath));
				}
			}
		}

		if (SearchRoots.IsEmpty())
		{
			SearchRoots.Add(FName(DefaultWidgetRootPath));
		}

		return SearchRoots;
	}

	static bool GetGeneratedClassObjectPath(const FAssetData& AssetData, FString& OutClassObjectPath)
	{
		FString GeneratedClassExportPath;
		if (!AssetData.GetTagValue(FName(TEXT("GeneratedClass")), GeneratedClassExportPath)
			&& !AssetData.GetTagValue(FName(TEXT("GeneratedClassPath")), GeneratedClassExportPath))
		{
			return false;
		}

		OutClassObjectPath = FPackageName::ExportTextPathToObjectPath(GeneratedClassExportPath);
		return !OutClassObjectPath.IsEmpty();
	}

	static FString InferPreferredWidgetSystemFolder(const TCHAR* ContextName)
	{
		const FString Context = ContextName ? FString(ContextName) : FString();
		if (Context.Contains(TEXT("ProjectGameplayDebug")))
		{
			return TEXT("Debug");
		}
		if (Context.Contains(TEXT("ProjectEmote")))
		{
			return TEXT("Y");
		}
		if (Context.Contains(TEXT("ProjectSurvivalStatus")))
		{
			return TEXT("Status");
		}
		if (Context.Contains(TEXT("ProjectInnerState")) || Context.Contains(TEXT("ProjectSurvivalNeeds")))
		{
			return TEXT("InnerState");
		}
		const bool bInnerDoctrineContext =
			Context.Contains(TEXT("ProjectInnerDoctrine"))
			|| Context.Contains(TEXT("InnerDoctrine"));
		const bool bAltarExchangeContext =
			Context.Contains(TEXT("Exchange"))
			|| Context.Contains(TEXT("Altar"))
			|| Context.Contains(TEXT("DxpExchange"));
		if (bInnerDoctrineContext && bAltarExchangeContext)
		{
			return TEXT("InnerDoctrineAltar");
		}
		if (bInnerDoctrineContext || Context.Contains(TEXT("ProjectAttribute")))
		{
			return TEXT("Attributes");
		}
		if (Context.Contains(TEXT("ProjectChronicle")) || Context.Contains(TEXT("ActivityFeed")))
		{
			return TEXT("Chronicle");
		}
		if (Context.Contains(TEXT("Lockpick")))
		{
			return TEXT("LockPick");
		}

		return FString();
	}

	static bool PackageIsUnderSystemFolder(const FString& PackageName, const FString& SystemFolder)
	{
		return !SystemFolder.IsEmpty()
			&& PackageName.Contains(FString::Printf(TEXT("/%s/"), *SystemFolder));
	}

	static int32 ScoreWidgetClassCandidate(
		const FString& PackageName,
		const UClass* WidgetClass,
		const UClass* NativeWidgetClass,
		const TCHAR* ContextName,
		const FName PriorityGroup,
		FString& OutReason)
	{
		int32 Score = 0;
		TArray<FString> Reasons;

		if (PackageName.StartsWith(DefaultWidgetRootPath))
		{
			Score += 1000;
			Reasons.Add(TEXT("under /Game/_Game/Widgets"));
		}

		const FString PreferredSystemFolder = InferPreferredWidgetSystemFolder(ContextName);
		if (!PreferredSystemFolder.IsEmpty())
		{
			if (PackageIsUnderSystemFolder(PackageName, PreferredSystemFolder))
			{
				Score += 250000;
				Reasons.Add(FString::Printf(TEXT("preferred widget system '%s'"), *PreferredSystemFolder));
			}
			else if (PackageName.StartsWith(DefaultWidgetRootPath))
			{
				Score -= 250000;
				Reasons.Add(FString::Printf(TEXT("not in preferred widget system '%s'"), *PreferredSystemFolder));
			}
		}

		if (PackageName.Contains(TEXT("/Global/")))
		{
			Score += 100000;
			Reasons.Add(TEXT("Global folder"));
		}
		if (PackageName.Contains(TEXT("/Globals/")))
		{
			Score += 100000;
			Reasons.Add(TEXT("current Globals folder"));
		}
		if (PackageName.Contains(TEXT("Global")))
		{
			Score += 10000;
			Reasons.Add(TEXT("global asset/class name"));
		}
		if (PackageName.Contains(TEXT("/Main/")))
		{
			Score += 1000;
			Reasons.Add(TEXT("Main folder"));
		}
		if (PriorityGroup != NAME_None && PackageName.Contains(PriorityGroup.ToString()))
		{
			Score += 5000;
			Reasons.Add(FString::Printf(TEXT("priority group '%s'"), *PriorityGroup.ToString()));
		}
		if (ContextName && ContextName[0] != 0 && PackageName.Contains(ContextName))
		{
			Score += 500;
			Reasons.Add(FString::Printf(TEXT("context '%s'"), ContextName));
		}
		if (WidgetClass && NativeWidgetClass && WidgetClass->GetSuperClass() == NativeWidgetClass)
		{
			Score += 250;
			Reasons.Add(TEXT("direct native parent"));
		}
		if (PackageName.StartsWith(TEXT("/Game/UI/")))
		{
			Score -= 1000000;
			Reasons.Add(TEXT("legacy /Game/UI penalty"));
		}

		OutReason = Reasons.IsEmpty() ? TEXT("default discovery score") : FString::Join(Reasons, TEXT(", "));
		return Score;
	}

	static FString MakeCacheKey(
		const FSoftClassPath& ConfiguredWidgetClass,
		const UClass* NativeWidgetClass,
		const TCHAR* ContextName,
		const FName PriorityGroup)
	{
		return FString::Printf(
			TEXT("%s|%s|%s|%s"),
			*ConfiguredWidgetClass.ToString(),
			*GetPathNameSafe(NativeWidgetClass),
			ContextName ? ContextName : TEXT(""),
			*PriorityGroup.ToString());
	}

	static TArray<FDiscoveredWidgetClass> DiscoverWidgetClasses(UClass* NativeWidgetClass, const TCHAR* ContextName, const FName PriorityGroup)
	{
		TArray<FDiscoveredWidgetClass> Matches;
		if (!NativeWidgetClass)
		{
			return Matches;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		TArray<FString> RootPathStrings;
		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		Filter.bRecursivePaths = true;

		for (const FName& SearchRoot : ResolveWidgetSearchRoots())
		{
			Filter.PackagePaths.Add(SearchRoot);
			RootPathStrings.Add(SearchRoot.ToString());
		}

		if (!RootPathStrings.IsEmpty())
		{
			AssetRegistry.ScanPathsSynchronous(RootPathStrings, true);
		}

		TArray<FAssetData> WidgetBlueprintAssets;
		AssetRegistry.GetAssets(Filter, WidgetBlueprintAssets);

		for (const FAssetData& AssetData : WidgetBlueprintAssets)
		{
			FString GeneratedClassObjectPath;
			if (!GetGeneratedClassObjectPath(AssetData, GeneratedClassObjectPath))
			{
				continue;
			}

			UClass* GeneratedWidgetClass = LoadObject<UClass>(nullptr, *GeneratedClassObjectPath);
			if (!GeneratedWidgetClass || !GeneratedWidgetClass->IsChildOf(NativeWidgetClass) || GeneratedWidgetClass == NativeWidgetClass)
			{
				continue;
			}

			FDiscoveredWidgetClass Match;
			Match.PackageName = AssetData.PackageName.ToString();
			Match.WidgetClass = GeneratedWidgetClass;
			Match.bDirectNativeParent = GeneratedWidgetClass->GetSuperClass() == NativeWidgetClass;
			Match.PriorityScore = ScoreWidgetClassCandidate(
				Match.PackageName,
				GeneratedWidgetClass,
				NativeWidgetClass,
				ContextName,
				PriorityGroup,
				Match.PriorityReason);
			Matches.Add(Match);
		}

		Matches.Sort([](const FDiscoveredWidgetClass& Left, const FDiscoveredWidgetClass& Right)
		{
			if (Left.PriorityScore != Right.PriorityScore)
			{
				return Left.PriorityScore > Right.PriorityScore;
			}

			if (Left.bDirectNativeParent != Right.bDirectNativeParent)
			{
				return Left.bDirectNativeParent && !Right.bDirectNativeParent;
			}

			return Left.PackageName < Right.PackageName;
		});

		return Matches;
	}
}

UClass* ProjectWidgetClassResolver::ResolveWidgetClass(
	const FSoftClassPath& ConfiguredWidgetClass,
	UClass* NativeWidgetClass,
	const TCHAR* ContextName)
{
	return ResolveWidgetClassWithPriority(ConfiguredWidgetClass, NativeWidgetClass, ContextName, NAME_None);
}

UClass* ProjectWidgetClassResolver::ResolveWidgetClassWithPriority(
	const FSoftClassPath& ConfiguredWidgetClass,
	UClass* NativeWidgetClass,
	const TCHAR* ContextName,
	const FName PriorityGroup)
{
	if (!NativeWidgetClass)
	{
		return nullptr;
	}

	if (UClass* LoadedConfiguredClass = ConfiguredWidgetClass.TryLoadClass<UUserWidget>())
	{
		if (LoadedConfiguredClass->IsChildOf(NativeWidgetClass))
		{
			return LoadedConfiguredClass;
		}

		UE_LOG(
			LogProjectWidgetClassResolver,
			Warning,
			TEXT("[%s] Configured widget class '%s' does not derive from '%s'. Falling back to discovery."),
			ContextName ? ContextName : TEXT("Widget"),
			*LoadedConfiguredClass->GetName(),
			*NativeWidgetClass->GetName());
	}

	const FString CacheKey = ProjectWidgetClassResolverPrivate::MakeCacheKey(ConfiguredWidgetClass, NativeWidgetClass, ContextName, PriorityGroup);
	if (TWeakObjectPtr<UClass>* CachedClassPtr = ProjectWidgetClassResolverPrivate::GResolvedWidgetClassCache.Find(CacheKey))
	{
		if (CachedClassPtr->IsValid())
		{
			return CachedClassPtr->Get();
		}
	}

	const TArray<ProjectWidgetClassResolverPrivate::FDiscoveredWidgetClass> Matches =
		ProjectWidgetClassResolverPrivate::DiscoverWidgetClasses(NativeWidgetClass, ContextName, PriorityGroup);

	if (!Matches.IsEmpty())
	{
		if (Matches.Num() > 1)
		{
			UE_LOG(
				LogProjectWidgetClassResolver,
				Warning,
				TEXT("[%s] Found %d Widget Blueprints derived from '%s'; using '%s' score=%d reason=%s. Configure the exact class in EF Project UI settings if this is ambiguous."),
				ContextName ? ContextName : TEXT("Widget"),
				Matches.Num(),
				*NativeWidgetClass->GetName(),
				*GetNameSafe(Matches[0].WidgetClass),
				Matches[0].PriorityScore,
				*Matches[0].PriorityReason);
		}
		else
		{
			UE_LOG(
				LogProjectWidgetClassResolver,
				Verbose,
				TEXT("[%s] Resolved Widget Blueprint '%s' for native '%s' score=%d reason=%s."),
				ContextName ? ContextName : TEXT("Widget"),
				*GetNameSafe(Matches[0].WidgetClass),
				*NativeWidgetClass->GetName(),
				Matches[0].PriorityScore,
				*Matches[0].PriorityReason);
		}

		ProjectWidgetClassResolverPrivate::GResolvedWidgetClassCache.Add(CacheKey, Matches[0].WidgetClass);
		return Matches[0].WidgetClass;
	}

	ProjectWidgetClassResolverPrivate::GResolvedWidgetClassCache.Add(CacheKey, NativeWidgetClass);
	return NativeWidgetClass;
}
