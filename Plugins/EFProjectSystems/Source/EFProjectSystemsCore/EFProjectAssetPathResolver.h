#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

struct FEFProjectAssetPathResolver
{
	static constexpr TCHAR LegacyAnimationRoot[] = TEXT("/Game/KawaiiAnimations/Exported2/");
	static constexpr TCHAR DeprecatedAnimationRoot[] = TEXT("/Game/ExportedAnimations/");
	static constexpr TCHAR OrganizedAnimationRoot[] = TEXT("/Game/_Game/Animations/");

	static FString ResolveOrganizedAnimationFolder(const FString& AssetName)
	{
		if (AssetName.Contains(TEXT("Crawl"), ESearchCase::IgnoreCase))
		{
			return FString(OrganizedAnimationRoot) + TEXT("Locomotion/Crawl/");
		}
		if (AssetName.Contains(TEXT("Swim"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Diving"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("FlutterKick"), ESearchCase::IgnoreCase))
		{
			return FString(OrganizedAnimationRoot) + TEXT("Locomotion/Swim/");
		}
		if (AssetName.Contains(TEXT("Walk"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Run"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Jog"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Pivot"), ESearchCase::IgnoreCase))
		{
			return FString(OrganizedAnimationRoot) + TEXT("Locomotion/Ground/");
		}
		if (AssetName.Contains(TEXT("Jump"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Fall"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Landing"), ESearchCase::IgnoreCase))
		{
			return FString(OrganizedAnimationRoot) + TEXT("Locomotion/Air/");
		}
		if (AssetName.Contains(TEXT("Dance"), ESearchCase::IgnoreCase))
		{
			return FString(OrganizedAnimationRoot) + TEXT("Emotes/Dance/");
		}
		if (AssetName.Contains(TEXT("Sit"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Seiza"), ESearchCase::IgnoreCase))
		{
			return FString(OrganizedAnimationRoot) + TEXT("Emotes/Sit/");
		}

		static constexpr TCHAR GeneralEmoteTokens[][10] =
		{
			TEXT("Idle"), TEXT("Look"), TEXT("Speak"), TEXT("Sleep"),
			TEXT("Lay"), TEXT("Lying"), TEXT("Bow"), TEXT("Clap"),
			TEXT("Wave"), TEXT("Cheer"), TEXT("Cry"), TEXT("Laugh"),
			TEXT("Angry"), TEXT("Happy"), TEXT("Sad")
		};
		for (const TCHAR* Token : GeneralEmoteTokens)
		{
			if (AssetName.Contains(Token, ESearchCase::IgnoreCase))
			{
				return FString(OrganizedAnimationRoot) + TEXT("Emotes/General/");
			}
		}

		return FString(OrganizedAnimationRoot) + TEXT("Kawaii/Female/");
	}

	static FSoftObjectPath BuildExportedAnimationPath(const FString& AssetName)
	{
		return AssetName.IsEmpty()
			? FSoftObjectPath()
			: FSoftObjectPath(FString::Printf(
				TEXT("%s%s.%s"),
				*ResolveOrganizedAnimationFolder(AssetName),
				*AssetName,
				*AssetName));
	}

	static FString RemapLegacyPathString(const FString& ObjectPath)
	{
		if (ObjectPath.IsEmpty()
			|| (!ObjectPath.StartsWith(LegacyAnimationRoot, ESearchCase::IgnoreCase)
				&& !ObjectPath.StartsWith(DeprecatedAnimationRoot, ESearchCase::IgnoreCase)))
		{
			return ObjectPath;
		}

		FString Leaf;
		if (!ObjectPath.Split(TEXT("/"), nullptr, &Leaf, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			return ObjectPath;
		}
		FString AssetName;
		if (!Leaf.Split(TEXT("."), &AssetName, nullptr) || AssetName.IsEmpty())
		{
			return ObjectPath;
		}
		return BuildExportedAnimationPath(AssetName).ToString();
	}

	static FSoftObjectPath RemapLegacyPath(const FSoftObjectPath& ObjectPath)
	{
		if (!ObjectPath.IsValid())
		{
			return ObjectPath;
		}

		const FString OriginalPath = ObjectPath.ToString();
		const FString RemappedPath = RemapLegacyPathString(OriginalPath);
		return RemappedPath == OriginalPath ? ObjectPath : FSoftObjectPath(RemappedPath);
	}

	template <typename TObjectType>
	static TObjectType* LoadObjectWithLegacyFallback(const FSoftObjectPath& AssetPath)
	{
		if (!AssetPath.IsValid())
		{
			return nullptr;
		}

		if (TObjectType* Loaded = Cast<TObjectType>(AssetPath.TryLoad()))
		{
			return Loaded;
		}

		const FSoftObjectPath RemappedPath = RemapLegacyPath(AssetPath);
		return RemappedPath == AssetPath ? nullptr : Cast<TObjectType>(RemappedPath.TryLoad());
	}
};
