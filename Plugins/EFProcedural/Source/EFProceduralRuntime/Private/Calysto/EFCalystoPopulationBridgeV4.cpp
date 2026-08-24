#include "Calysto/EFCalystoPopulationBridgeV4.h"

#include "Features/IModularFeatures.h"

IEFCalystoPopulationBridgeV4::IEFCalystoPopulationBridgeV4() = default;

IEFCalystoPopulationBridgeV4::~IEFCalystoPopulationBridgeV4() = default;

FName IEFCalystoPopulationBridgeV4::GetModularFeatureName()
{
	static const FName FeatureName(TEXT("EFCalystoPopulationBridgeV4"));
	return FeatureName;
}

bool IEFCalystoPopulationBridgeV4::GatherRegisteredAdditionalPreloadPaths(
	const FEFCalystoResolvedFloorIntentV4& Intent,
	TArray<FSoftObjectPath>& OutAssetPaths,
	FString& OutError)
{
	OutAssetPaths.Reset();
	OutError.Reset();
	if (!Intent.bIsValid || Intent.GeneratorVersion != 4)
	{
		OutError = TEXT("Additional V4 preload paths require a valid, frozen GeneratorVersion 4 intent.");
		return false;
	}

	const TArray<IEFCalystoPopulationBridgeV4*> Registered =
		IModularFeatures::Get().GetModularFeatureImplementations<IEFCalystoPopulationBridgeV4>(
			GetModularFeatureName());
	TArray<IEFCalystoPopulationBridgeV4*> UniqueBridges;
	UniqueBridges.Reserve(Registered.Num());
	for (IEFCalystoPopulationBridgeV4* Bridge : Registered)
	{
		if (Bridge)
		{
			UniqueBridges.AddUnique(Bridge);
		}
	}

	TArray<EEFCalystoContentCategoryV4> RequiredCategories;
	for (const FEFCalystoSpawnInstanceDirectiveV4& Directive : Intent.SpawnDirectives)
	{
		if (Directive.Category == EEFCalystoContentCategoryV4::Enemy
			|| Directive.Category == EEFCalystoContentCategoryV4::NPC
			|| Directive.Category == EEFCalystoContentCategoryV4::Chest)
		{
			RequiredCategories.AddUnique(Directive.Category);
		}
	}
	for (const EEFCalystoContentCategoryV4 Category : RequiredCategories)
	{
		int32 OwnerCount = 0;
		for (const IEFCalystoPopulationBridgeV4* Bridge : UniqueBridges)
		{
			OwnerCount += Bridge->HandlesCategory(Category) ? 1 : 0;
		}
		if (OwnerCount != 1)
		{
			OutError = FString::Printf(
				TEXT("V4 preload requires exactly one population bridge for category %d; found %d."),
				static_cast<int32>(Category),
				OwnerCount);
			return false;
		}
	}

	TSet<FSoftObjectPath> UniquePaths;
	for (const IEFCalystoPopulationBridgeV4* Bridge : UniqueBridges)
	{
		TArray<FSoftObjectPath> BridgePaths;
		FString BridgeError;
		if (!Bridge->GatherAdditionalPreloadPaths(Intent, BridgePaths, BridgeError))
		{
			OutError = BridgeError.IsEmpty()
				? TEXT("A V4 population bridge rejected its additional preload contract without a diagnostic.")
				: BridgeError;
			return false;
		}
		for (const FSoftObjectPath& AssetPath : BridgePaths)
		{
			if (!AssetPath.IsValid())
			{
				OutError = TEXT("A V4 population bridge returned an invalid additional preload path.");
				return false;
			}
			UniquePaths.Add(AssetPath);
		}
	}

	OutAssetPaths = UniquePaths.Array();
	OutAssetPaths.Sort([](const FSoftObjectPath& Left, const FSoftObjectPath& Right)
	{
		return Left.ToString() < Right.ToString();
	});
	return true;
}
