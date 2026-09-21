#include "Calysto/EFCalystoPopulationBridgeV6.h"

#include "Engine/World.h"
#include "Features/IModularFeatures.h"

namespace EFCalystoPopulationBridgeV6Private
{
	FString CanonicalPath(const FSoftObjectPath& Path)
	{
		FString Result = Path.ToString();
		Result.TrimStartAndEndInline();
		Result.ToLowerInline();
		return Result;
	}
}

FName IEFCalystoPopulationBridgeV6::GetModularFeatureName()
{
	static const FName FeatureName(TEXT("EFCalystoPopulationBridgeV6"));
	return FeatureName;
}

bool IEFCalystoPopulationBridgeV6::CategoryRequiresBridge(const FName CategoryId)
{
	static const FName EnemyId(TEXT("Enemy"));
	static const FName NpcId(TEXT("NPC"));
	static const FName ChestId(TEXT("Chest"));
	return CategoryId.IsEqual(EnemyId, ENameCase::IgnoreCase)
		|| CategoryId.IsEqual(NpcId, ENameCase::IgnoreCase)
		|| CategoryId.IsEqual(ChestId, ENameCase::IgnoreCase);
}

IEFCalystoPopulationBridgeV6* IEFCalystoPopulationBridgeV6::ResolveUniqueBridge(
	const FName CategoryId,
	FString& OutError)
{
	OutError.Reset();
	if (!CategoryRequiresBridge(CategoryId))
	{
		OutError = FString::Printf(
			TEXT("Calysto category '%s' does not require a gameplay population bridge."),
			*CategoryId.ToString());
		return nullptr;
	}

	TArray<IEFCalystoPopulationBridgeV6*> Matching;
	const TArray<IEFCalystoPopulationBridgeV6*> Implementations =
		IModularFeatures::Get().GetModularFeatureImplementations<IEFCalystoPopulationBridgeV6>(
			GetModularFeatureName());
	for (IEFCalystoPopulationBridgeV6* Implementation : Implementations)
	{
		if (Implementation && Implementation->HandlesCategory(CategoryId))
		{
			Matching.Add(Implementation);
		}
	}
	if (Matching.Num() != 1)
	{
		OutError = FString::Printf(
			TEXT("Calysto V6 category '%s' requires exactly one gameplay population bridge; found %d."),
			*CategoryId.ToString(),
			Matching.Num());
		return nullptr;
	}
	return Matching[0];
}

bool IEFCalystoPopulationBridgeV6::GatherRegisteredAdditionalPreloadPaths(
	const FEFCalystoPopulationPlanV6& Plan,
	TArray<FSoftObjectPath>& OutAssetPaths,
	FString& OutError)
{
	using namespace EFCalystoPopulationBridgeV6Private;
	OutAssetPaths.Reset();
	OutError.Reset();

	TSet<IEFCalystoPopulationBridgeV6*> UniqueBridges;
	for (const FEFCalystoRoomPopulationPlanV6& Room : Plan.Rooms)
	{
		for (const FEFCalystoPopulationDecisionV6& Decision : Room.Decisions)
		{
			if (Decision.Kind != EEFCalystoPopulationDecisionKindV6::Actor
				|| !CategoryRequiresBridge(Decision.CategoryId))
			{
				continue;
			}
			FString ResolveError;
			IEFCalystoPopulationBridgeV6* Bridge = ResolveUniqueBridge(
				Decision.CategoryId, ResolveError);
			if (!Bridge)
			{
				OutError = MoveTemp(ResolveError);
				return false;
			}
			UniqueBridges.Add(Bridge);
		}
	}

	for (IEFCalystoPopulationBridgeV6* Bridge : UniqueBridges)
	{
		TArray<FSoftObjectPath> BridgePaths;
		FString BridgeError;
		if (!Bridge->GatherAdditionalPreloadPaths(Plan, BridgePaths, BridgeError))
		{
			OutError = BridgeError.IsEmpty()
				? TEXT("A Calysto V6 gameplay population bridge rejected its preload contract.")
				: MoveTemp(BridgeError);
			return false;
		}
		for (const FSoftObjectPath& Path : BridgePaths)
		{
			if (!Path.IsValid())
			{
				OutError = TEXT("A Calysto V6 gameplay population bridge returned an invalid preload path.");
				return false;
			}
			OutAssetPaths.AddUnique(Path);
		}
	}

	OutAssetPaths.Sort([](const FSoftObjectPath& Left, const FSoftObjectPath& Right)
	{
		return CanonicalPath(Left) < CanonicalPath(Right);
	});
	return true;
}

bool IEFCalystoPopulationBridgeV6::ValidateRegisteredCompanionRosterReady(
	UWorld* World,
	const FEFCalystoPopulationPlanV6& Plan,
	const FString& ExpectedSnapshotHash,
	FString& OutError)
{
	OutError.Reset();
	if (!IsValid(World) || !World->IsGameWorld()
		|| Plan.PopulationHash.IsEmpty() || ExpectedSnapshotHash.IsEmpty())
	{
		OutError = TEXT("Calysto V6 companion readiness requires a valid game world, population plan, and expected snapshot hash.");
		return false;
	}

	TArray<IEFCalystoPopulationBridgeV6*> Providers;
	const TArray<IEFCalystoPopulationBridgeV6*> Implementations =
		IModularFeatures::Get().GetModularFeatureImplementations<IEFCalystoPopulationBridgeV6>(
			GetModularFeatureName());
	for (IEFCalystoPopulationBridgeV6* Implementation : Implementations)
	{
		if (Implementation && Implementation->ProvidesCompanionRosterReadiness())
		{
			Providers.Add(Implementation);
		}
	}
	if (Providers.Num() != 1)
	{
		OutError = FString::Printf(
			TEXT("Calysto V6 requires exactly one companion-roster readiness provider; found %d."),
			Providers.Num());
		return false;
	}
	if (!Providers[0]->ValidateCompanionRosterReady(
			World,
			Plan,
			ExpectedSnapshotHash,
			OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("The Calysto V6 companion-roster readiness provider rejected the committed population plan.");
		}
		return false;
	}
	return true;
}
