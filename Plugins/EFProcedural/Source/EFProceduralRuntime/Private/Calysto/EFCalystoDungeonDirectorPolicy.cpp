#include "Calysto/EFCalystoDungeonDirectorPolicy.h"

#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/BlueprintSupport.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

namespace EFCalystoDirectorPolicyPrivate
{
	static FEFCalystoPopulationCatalogEntry MakePopulation(
		const TCHAR* Id,
		const TCHAR* ClassPath,
		const int32 Weight,
		const float Cost,
		const int32 MaxPerFloor,
		const bool bEnabled = true,
		const int64 MinimumFloor = 1,
		const float Rarity = 0.0f,
		const int32 CooldownFloors = 0)
	{
		FEFCalystoPopulationCatalogEntry Entry;
		Entry.StableId = FName(Id);
		Entry.bEnabled = bEnabled;
		Entry.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(ClassPath));
		Entry.BaseWeight = Weight;
		Entry.Cost = Cost;
		Entry.MinimumFloor = MinimumFloor;
		Entry.MaxPerFloor = MaxPerFloor;
		Entry.Rarity = Rarity;
		Entry.CooldownFloors = CooldownFloors;
		return Entry;
	}

	static bool IsFiniteDistribution(const FEFCalystoFloatDistribution& Distribution)
	{
		return FMath::IsFinite(Distribution.Min)
			&& FMath::IsFinite(Distribution.Mode)
			&& FMath::IsFinite(Distribution.Max)
			&& FMath::IsFinite(Distribution.Concentration)
			&& Distribution.Min <= Distribution.Mode
			&& Distribution.Mode <= Distribution.Max
			&& Distribution.Concentration >= 2.0f
			&& Distribution.Concentration <= 8.0f;
	}

	static bool IsValidIntDistribution(const FEFCalystoIntDistribution& Distribution)
	{
		return Distribution.Min <= Distribution.Mode
			&& Distribution.Mode <= Distribution.Max
			&& FMath::IsFinite(Distribution.Concentration)
			&& Distribution.Concentration >= 2.0f
			&& Distribution.Concentration <= 8.0f;
	}

	static bool IsProbability(const float Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0f && Value <= 1.0f;
	}

	static bool IsBias(const float Value)
	{
		return FMath::IsFinite(Value) && Value >= -1.0f && Value <= 1.0f;
	}

	static FString FloatBits(const float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return FString::Printf(TEXT("%08X"), Bits);
	}

	static bool IsExactRegisteredAsset(const FSoftObjectPath& Path)
	{
		const IAssetRegistry* Registry = IAssetRegistry::Get();
		return Registry && Path.IsValid() && Registry->GetAssetByObjectPath(Path, true).IsValid();
	}

	static bool IsExactRegisteredActorClass(const FSoftObjectPath& ClassPath, const UClass* RequiredBaseClass)
	{
		const IAssetRegistry* Registry = IAssetRegistry::Get();
		if (!Registry || !ClassPath.IsValid() || !IsValid(RequiredBaseClass))
		{
			return false;
		}

		TArray<FAssetData> PackageAssets;
		if (!Registry->GetAssetsByPackageName(
				FName(*ClassPath.GetLongPackageName()), PackageAssets, true)
			|| PackageAssets.IsEmpty())
		{
			return false;
		}

		for (const FAssetData& AssetData : PackageAssets)
		{
			FString GeneratedClassExportPath;
			if (!AssetData.GetTagValue(FBlueprintTags::GeneratedClassPath, GeneratedClassExportPath)
				|| FPackageName::ExportTextPathToObjectPath(GeneratedClassExportPath) != ClassPath.ToString())
			{
				continue;
			}

			FString NativeParentExportPath;
			if (!AssetData.GetTagValue(FBlueprintTags::NativeParentClassPath, NativeParentExportPath))
			{
				AssetData.GetTagValue(FBlueprintTags::ParentClassPath, NativeParentExportPath);
			}
			const FString NativeParentPath = FPackageName::ExportTextPathToObjectPath(NativeParentExportPath);
			const UClass* NativeParentClass = FindObject<UClass>(nullptr, *NativeParentPath);
			return NativeParentClass && NativeParentClass->IsChildOf(RequiredBaseClass);
		}

		return false;
	}
}

UEFCalystoDungeonDirectorPolicy::UEFCalystoDungeonDirectorPolicy()
{
	// Intentionally empty. Runtime sizes become authoritative only after an
	// explicit certification receipt promotes them into the authored asset.

	FEFCalystoStylePolicy Standard;
	Standard.Style = EEFCalystoDungeonStyle::Standard;
	Standard.SelectionWeight = 5;
	Styles.Add(Standard);

	FEFCalystoStylePolicy Compact;
	Compact.Style = EEFCalystoDungeonStyle::Compact;
	Compact.SelectionWeight = 3;
	Compact.ScaleBias = -0.65f;
	Compact.BranchingBias = -0.30f;
	Compact.ThreatBias = 0.10f;
	Styles.Add(Compact);

	FEFCalystoStylePolicy Branching;
	Branching.Style = EEFCalystoDungeonStyle::Branching;
	Branching.SelectionWeight = 3;
	Branching.ScaleBias = 0.10f;
	Branching.BranchingBias = 0.70f;
	Branching.MysteryBias = 0.25f;
	Styles.Add(Branching);

	using namespace EFCalystoDirectorPolicyPrivate;
	EnemyCatalog = {
		MakePopulation(TEXT("FemaleDummyAmbush"), TEXT("/Game/_Game/Characters/Female/ACFDummyAmbushEnemyBPFemale.ACFDummyAmbushEnemyBPFemale_C"), 0, 1.0f, 1, false),
		MakePopulation(TEXT("FemaleDummy"), TEXT("/Game/_Game/Characters/Female/ACFDummyEnemyBPFemale.ACFDummyEnemyBPFemale_C"), 0, 1.0f, 1, false),
		MakePopulation(TEXT("FemaleDefender"), TEXT("/Game/_Game/Characters/Female/ACFDefenderEnemyBPFemale.ACFDefenderEnemyBPFemale_C"), 2, 3.0f, 6, true, 1, 0.25f),
		MakePopulation(TEXT("FemaleGun"), TEXT("/Game/_Game/Characters/Female/ACFGunEnemyBPFemale.ACFGunEnemyBPFemale_C"), 1, 3.0f, 5, true, 3, 0.45f, 1),
		MakePopulation(TEXT("FemaleMage"), TEXT("/Game/_Game/Characters/Female/ACFMageEnemyBPFemale.ACFMageEnemyBPFemale_C"), 2, 4.0f, 4, true, 4, 0.70f, 2),
		MakePopulation(TEXT("FemaleMelee"), TEXT("/Game/_Game/Characters/Female/ACFMeleeEnemyBPFemale.ACFMeleeEnemyBPFemale_C"), 1, 2.0f, 8, true, 1, 0.10f),
		MakePopulation(TEXT("FemaleMM"), TEXT("/Game/_Game/Characters/Female/ACFMMEnemyBPFemale.ACFMMEnemyBPFemale_C"), 2, 1.0f, 12),
		MakePopulation(TEXT("FemaleRanged"), TEXT("/Game/_Game/Characters/Female/ACFRangedEnemyBPFemale.ACFRangedEnemyBPFemale_C"), 3, 3.0f, 6, true, 2, 0.30f),
		MakePopulation(TEXT("MaleDummyAmbush"), TEXT("/Game/_Game/Characters/Male/ACFDummyAmbushEnemyBPMale.ACFDummyAmbushEnemyBPMale_C"), 0, 1.0f, 1, false),
		MakePopulation(TEXT("MaleDummy"), TEXT("/Game/_Game/Characters/Male/ACFDummyEnemyBPMale.ACFDummyEnemyBPMale_C"), 0, 1.0f, 1, false),
		MakePopulation(TEXT("MaleDefender"), TEXT("/Game/_Game/Characters/Male/ACFDefenderEnemyBPMale.ACFDefenderEnemyBPMale_C"), 2, 3.0f, 6, true, 1, 0.25f),
		MakePopulation(TEXT("MaleGun"), TEXT("/Game/_Game/Characters/Male/ACFGunEnemyBPMale.ACFGunEnemyBPMale_C"), 1, 3.0f, 5, true, 3, 0.45f, 1),
		MakePopulation(TEXT("MaleMage"), TEXT("/Game/_Game/Characters/Male/ACFMageEnemyBPMale.ACFMageEnemyBPMale_C"), 2, 4.0f, 4, true, 4, 0.70f, 2),
		MakePopulation(TEXT("MaleMelee"), TEXT("/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale.ACFMeleeEnemyBPMale_C"), 1, 2.0f, 8, true, 1, 0.10f),
		MakePopulation(TEXT("MaleMM"), TEXT("/Game/_Game/Characters/Male/ACFMMEnemyBPMale.ACFMMEnemyBPMale_C"), 4, 1.0f, 12),
		MakePopulation(TEXT("MaleRanged"), TEXT("/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C"), 2, 3.0f, 6, true, 2, 0.30f)
	};

	FoodCatalog = {
		MakePopulation(TEXT("Apple"), TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_Apple01.BP_Pickup_Food_Apple01_C"), 4, 1.0f, 4),
		MakePopulation(TEXT("Bread"), TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_Bread01.BP_Pickup_Food_Bread01_C"), 3, 1.0f, 4, true, 1, 0.10f),
		MakePopulation(TEXT("CookedMeat"), TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_CookedMeat01.BP_Pickup_Food_CookedMeat01_C"), 2, 1.0f, 3, true, 2, 0.45f, 1),
		MakePopulation(TEXT("Water"), TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Drink/BP_Pickup_Drink_WaterBottle01.BP_Pickup_Drink_WaterBottle01_C"), 4, 1.0f, 4)
	};

	ChestCatalog = {
		MakePopulation(TEXT("LockedChest"), TEXT("/Game/_Game/Lockpicking/LockedChest.LockedChest_C"), 3, 1.0f, 2, true, 1, 0.25f),
		MakePopulation(TEXT("LockPickChest"), TEXT("/Game/_Game/Lockpicking/LockPickChest.LockPickChest_C"), 2, 1.0f, 1, true, 3, 0.65f, 2)
	};

	LootCatalog = {
		MakePopulation(TEXT("RareAlcohol07"), TEXT("/Game/_Game/FoodSystem/Food/Items/PickuableItems/Drink/BP_Pickup_Drink_AlcoholBottle07.BP_Pickup_Drink_AlcoholBottle07_C"), 1, 1.0f, 1, true, 4, 1.0f, 4)
	};

	FEFCalystoThemeCatalogEntry Forge;
	Forge.StableId = TEXT("Forge");
	Forge.RoomType = TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomForge.DA_RoomForge")));
	Forge.BaseWeight = 5;
	Forge.BiasAxis = -1.0f;
	ThemeCatalog.Add(Forge);

	FEFCalystoThemeCatalogEntry Shrine;
	Shrine.StableId = TEXT("Shrine");
	Shrine.RoomType = TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomShrine.DA_RoomShrine")));
	Shrine.BaseWeight = 5;
	Shrine.BiasAxis = 1.0f;
	ThemeCatalog.Add(Shrine);
}

FString UEFCalystoDungeonDirectorPolicy::GetPolicyHash() const
{
	return UEFCalystoDungeonSubsystem::ComputePolicyHash(this);
}

FString UEFCalystoDungeonDirectorPolicy::GetPolicyHashWithValidatedDungeonSizes(
	const TArray<int32>& CandidateSizes) const
{
	UEFCalystoDungeonDirectorPolicy* Candidate = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(
		this, GetTransientPackage());
	if (!Candidate)
	{
		return FString();
	}
	Candidate->SetFlags(RF_Transient);
	Candidate->ValidatedDungeonSizes = CandidateSizes;
	FString ValidationError;
	if (!Candidate->Validate(ValidationError))
	{
		return FString();
	}
	return UEFCalystoDungeonSubsystem::ComputePolicyHash(Candidate);
}

bool UEFCalystoDungeonDirectorPolicy::Validate(FString& OutError) const
{
	using namespace EFCalystoDirectorPolicyPrivate;
	OutError.Reset();
	if (SchemaVersion != 3 || GeneratorVersion != 3 || PolicyId.IsNone())
	{
		OutError = TEXT("V3 policy identity must use schema 3, generator 3, and a non-empty PolicyId.");
		return false;
	}
	if (Limits.MinDungeonEdge < 18 || Limits.MaxDungeonEdge > 30 || Limits.MinDungeonEdge > Limits.MaxDungeonEdge
		|| Limits.MaxEnemies < 0 || Limits.MaxEnemies > 25
		|| Limits.MaxFood < 0 || Limits.MaxFood > 8
		|| Limits.MaxChests < 0 || Limits.MaxChests > 3
		|| Limits.MaxLoot < 0 || Limits.MaxLoot > 4
		|| Limits.MaxSpecialEvents < 0 || Limits.MaxSpecialEvents > 4
		|| Limits.MaxDirectorActors < 1 || Limits.MaxDirectorActors > 36
		|| Limits.MaxDirectorActors < Limits.MaxEnemies
		|| !FMath::IsFinite(Limits.MinCandidateAnchorDensity) || !FMath::IsFinite(Limits.MaxCandidateAnchorDensity)
		|| Limits.MinCandidateAnchorDensity < 0.20f || Limits.MaxCandidateAnchorDensity > 0.50f
		|| Limits.MinCandidateAnchorDensity > Limits.MaxCandidateAnchorDensity
		|| !FMath::IsFinite(Limits.MinSidePathChance) || !FMath::IsFinite(Limits.MaxSidePathChance)
		|| Limits.MinSidePathChance < 0.30f || Limits.MaxSidePathChance > 0.70f
		|| Limits.MinSidePathChance > Limits.MaxSidePathChance
		|| Limits.RoomMinSize != 4 || Limits.RoomMaxSize != 8)
	{
		OutError = TEXT("V3 hard safety limits are invalid.");
		return false;
	}

	if (!FMath::IsFinite(Progression.LayoutTau) || Progression.LayoutTau <= 0.0f
		|| !FMath::IsFinite(Progression.ThreatTau) || Progression.ThreatTau <= 0.0f
		|| !FMath::IsFinite(Progression.StartSizeMode) || !FMath::IsFinite(Progression.EndSizeMode)
		|| Progression.StartSizeMode < Limits.MinDungeonEdge || Progression.StartSizeMode > Limits.MaxDungeonEdge
		|| Progression.EndSizeMode < Limits.MinDungeonEdge || Progression.EndSizeMode > Limits.MaxDungeonEdge
		|| !FMath::IsFinite(Progression.SizeHalfRange) || Progression.SizeHalfRange < 0.0f
		|| !IsProbability(Progression.StartEnemyPresence) || !IsProbability(Progression.EndEnemyPresence)
		|| !FMath::IsFinite(Progression.StartEnemyCountMode) || !FMath::IsFinite(Progression.EndEnemyCountMode)
		|| Progression.StartEnemyCountMode < 1.0f || Progression.StartEnemyCountMode > Limits.MaxEnemies
		|| Progression.EndEnemyCountMode < 1.0f || Progression.EndEnemyCountMode > Limits.MaxEnemies
		|| !FMath::IsFinite(Progression.EnemyCountLowerOffset) || Progression.EnemyCountLowerOffset < 0.0f
		|| !FMath::IsFinite(Progression.EnemyCountUpperOffset) || Progression.EnemyCountUpperOffset < 0.0f
		|| !FMath::IsFinite(Progression.StartThreatBudget) || Progression.StartThreatBudget < 0.0f
		|| !FMath::IsFinite(Progression.EndThreatBudget) || Progression.EndThreatBudget < Progression.StartThreatBudget
		|| !FMath::IsFinite(Progression.ThreatBudgetRelativeRange)
		|| Progression.ThreatBudgetRelativeRange < 0.0f || Progression.ThreatBudgetRelativeRange > 0.50f
		|| !FMath::IsFinite(Progression.PacingAmplitude)
		|| Progression.PacingAmplitude < 0.0f || Progression.PacingAmplitude > 0.25f
		|| !IsProbability(Progression.StartFoodPresence) || !IsProbability(Progression.EndFoodPresence)
		|| !IsProbability(Progression.StartChestPresence) || !IsProbability(Progression.EndChestPresence)
		|| !IsProbability(Progression.StartLootPresence) || !IsProbability(Progression.EndLootPresence)
		|| !IsProbability(Progression.StartSpecialEventPresence) || !IsProbability(Progression.EndSpecialEventPresence)
		|| !IsFiniteDistribution(Progression.CandidateAnchorDensity)
		|| !IsFiniteDistribution(Progression.SidePathChance)
		|| !IsValidIntDistribution(Progression.FoodCount)
		|| !IsValidIntDistribution(Progression.ChestCount)
		|| !IsValidIntDistribution(Progression.LootCount)
		|| !IsValidIntDistribution(Progression.SpecialEventCount)
		|| Progression.CandidateAnchorDensity.Min < Limits.MinCandidateAnchorDensity
		|| Progression.CandidateAnchorDensity.Max > Limits.MaxCandidateAnchorDensity
		|| Progression.SidePathChance.Min < Limits.MinSidePathChance
		|| Progression.SidePathChance.Max > Limits.MaxSidePathChance
		|| Progression.FoodCount.Min < 1 || Progression.FoodCount.Max > Limits.MaxFood
		|| Progression.ChestCount.Min < 1 || Progression.ChestCount.Max > Limits.MaxChests
		|| Progression.LootCount.Min < 1 || Progression.LootCount.Max > Limits.MaxLoot
		|| Progression.SpecialEventCount.Min < 1 || Progression.SpecialEventCount.Max > Limits.MaxSpecialEvents)
	{
		OutError = TEXT("V3 progression contains an invalid distribution.");
		return false;
	}

	const float EcologyWeightSum = Ecology.RunDNAWeight + Ecology.SmoothNoiseWeight + Ecology.JitterWeight;
	if (!FMath::IsFinite(Ecology.RunDNAWeight) || Ecology.RunDNAWeight < 0.0f
		|| !FMath::IsFinite(Ecology.SmoothNoiseWeight) || Ecology.SmoothNoiseWeight < 0.0f
		|| !FMath::IsFinite(Ecology.JitterWeight) || Ecology.JitterWeight < 0.0f
		|| !FMath::IsNearlyEqual(EcologyWeightSum, 1.0f, 0.001f)
		|| Ecology.SmoothNoisePeriod < 2 || Ecology.MaxConsecutiveStyle < 1
		|| Ecology.MaxConsecutiveDominantTheme < 1 || Ecology.FoodPityAfterEmptyFloors < 1
		|| Ecology.ChestPityAfterEmptyFloors < 1
		|| !IsProbability(Ecology.RaritySelectionStrength))
	{
		OutError = TEXT("V3 ecology weights or pacing constraints are invalid.");
		return false;
	}

	const float AdaptationWeightSum = Adaptation.CombatWeight + Adaptation.SurvivalWeight
		+ Adaptation.ResourcesWeight + Adaptation.PaceWeight;
	if (!FMath::IsFinite(Adaptation.CombatWeight) || Adaptation.CombatWeight < 0.0f
		|| !FMath::IsFinite(Adaptation.SurvivalWeight) || Adaptation.SurvivalWeight < 0.0f
		|| !FMath::IsFinite(Adaptation.ResourcesWeight) || Adaptation.ResourcesWeight < 0.0f
		|| !FMath::IsFinite(Adaptation.PaceWeight) || Adaptation.PaceWeight < 0.0f
		|| !FMath::IsNearlyEqual(AdaptationWeightSum, 1.0f, 0.001f)
		|| !IsProbability(Adaptation.EMAAlpha)
		|| !FMath::IsFinite(Adaptation.MaximumInfluence) || Adaptation.MaximumInfluence < 0.0f || Adaptation.MaximumInfluence > 0.15f
		|| !IsProbability(Adaptation.DeathPenalty))
	{
		OutError = TEXT("V3 adaptation weights or clamps are invalid.");
		return false;
	}

	TSet<int32> UniqueSizes;
	for (const int32 Size : ValidatedDungeonSizes)
	{
		if (Size < Limits.MinDungeonEdge || Size > Limits.MaxDungeonEdge || UniqueSizes.Contains(Size))
		{
			OutError = FString::Printf(TEXT("Validated dungeon size %d is invalid or duplicated."), Size);
			return false;
		}
		UniqueSizes.Add(Size);
	}
	if (UniqueSizes.IsEmpty())
	{
		OutError = TEXT("At least one validated dungeon size is required.");
		return false;
	}

	TSet<EEFCalystoDungeonStyle> UniqueStyles;
	for (const FEFCalystoStylePolicy& Style : Styles)
	{
		if (Style.Style == EEFCalystoDungeonStyle::Auto || Style.SelectionWeight <= 0 || Style.SelectionWeight > 100
			|| UniqueStyles.Contains(Style.Style) || !IsBias(Style.ScaleBias) || !IsBias(Style.BranchingBias)
			|| !IsBias(Style.ThreatBias) || !IsBias(Style.AbundanceBias) || !IsBias(Style.MysteryBias))
		{
			OutError = TEXT("Styles must be unique, non-Auto, and have a positive selection weight.");
			return false;
		}
		UniqueStyles.Add(Style.Style);
	}
	if (!UniqueStyles.Contains(EEFCalystoDungeonStyle::Standard)
		|| !UniqueStyles.Contains(EEFCalystoDungeonStyle::Compact)
		|| !UniqueStyles.Contains(EEFCalystoDungeonStyle::Branching))
	{
		OutError = TEXT("Standard, Compact, and Branching styles are required.");
		return false;
	}

	TSet<FName> GlobalIds;
	TSet<FSoftObjectPath> GlobalClasses;
	auto ValidateCatalog = [&OutError, &GlobalIds, &GlobalClasses](
		const TArray<FEFCalystoPopulationCatalogEntry>& Catalog,
		const TCHAR* Label,
		const bool bRequireEnabled,
		const UClass* RequiredBaseClass) -> bool
	{
		bool bHasEnabled = false;
		for (const FEFCalystoPopulationCatalogEntry& Entry : Catalog)
		{
			const FSoftObjectPath Path = Entry.ActorClass.ToSoftObjectPath();
			const FString PackageName = Path.GetLongPackageName();
			if (Entry.StableId.IsNone() || GlobalIds.Contains(Entry.StableId) || !Path.IsValid() || GlobalClasses.Contains(Path)
				|| Entry.BaseWeight < 0 || Entry.BaseWeight > 100 || !FMath::IsFinite(Entry.Cost) || Entry.Cost <= 0.0f
				|| Entry.MinimumFloor <= 0 || Entry.MaxPerFloor <= 0 || Entry.MaxPerFloor > 25
				|| !IsProbability(Entry.Rarity) || Entry.CooldownFloors < 0 || Entry.CooldownFloors > 100
				|| (Entry.bEnabled && Entry.BaseWeight <= 0)
				|| !FPackageName::IsValidLongPackageName(PackageName) || !FPackageName::DoesPackageExist(PackageName)
				|| !IsExactRegisteredActorClass(Path, RequiredBaseClass))
			{
				OutError = FString::Printf(TEXT("%s catalog entry %s is invalid or duplicated."), Label, *Entry.StableId.ToString());
				return false;
			}
			GlobalIds.Add(Entry.StableId);
			GlobalClasses.Add(Path);
			bHasEnabled |= Entry.bEnabled;
		}
		if (bRequireEnabled && !bHasEnabled)
		{
			OutError = FString::Printf(TEXT("%s catalog requires at least one enabled entry."), Label);
			return false;
		}
		return true;
	};

	if (!ValidateCatalog(EnemyCatalog, TEXT("Enemy"), true, APawn::StaticClass())
		|| !ValidateCatalog(FoodCatalog, TEXT("Food"), true, AActor::StaticClass())
		|| !ValidateCatalog(ChestCatalog, TEXT("Chest"), true, AActor::StaticClass())
		|| !ValidateCatalog(LootCatalog, TEXT("Loot"), true, AActor::StaticClass())
		|| !ValidateCatalog(SpecialEventCatalog, TEXT("SpecialEvent"), false, AActor::StaticClass()))
	{
		return false;
	}
	static const TSet<FName> RequiredEnemyIds = {
		TEXT("FemaleDummyAmbush"), TEXT("FemaleDummy"), TEXT("FemaleDefender"), TEXT("FemaleGun"),
		TEXT("FemaleMage"), TEXT("FemaleMelee"), TEXT("FemaleMM"), TEXT("FemaleRanged"),
		TEXT("MaleDummyAmbush"), TEXT("MaleDummy"), TEXT("MaleDefender"), TEXT("MaleGun"),
		TEXT("MaleMage"), TEXT("MaleMelee"), TEXT("MaleMM"), TEXT("MaleRanged")
	};
	TSet<FName> ActualEnemyIds;
	for (const FEFCalystoPopulationCatalogEntry& Entry : EnemyCatalog)
	{
		ActualEnemyIds.Add(Entry.StableId);
		if (Entry.StableId.ToString().Contains(TEXT("Dummy")) && Entry.bEnabled)
		{
			OutError = FString::Printf(TEXT("Audited dummy entry %s must remain disabled until calibration."), *Entry.StableId.ToString());
			return false;
		}
	}
	bool bHasAllRequiredEnemyIds = true;
	for (const FName RequiredId : RequiredEnemyIds)
	{
		bHasAllRequiredEnemyIds &= ActualEnemyIds.Contains(RequiredId);
	}
	if (!bHasAllRequiredEnemyIds)
	{
		OutError = TEXT("The V3 enemy catalog is missing one or more of the 16 audited entries.");
		return false;
	}

	static const TMap<FName, FSoftObjectPath> RequiredThemes = {
		{TEXT("Forge"), FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomForge.DA_RoomForge"))},
		{TEXT("Shrine"), FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomShrine.DA_RoomShrine"))}
	};
	if (ThemeCatalog.Num() != RequiredThemes.Num())
	{
		OutError = TEXT("V3 requires the exact Forge/Shrine theme topology.");
		return false;
	}
	for (const FEFCalystoThemeCatalogEntry& Entry : ThemeCatalog)
	{
		const FSoftObjectPath* RequiredPath = RequiredThemes.Find(Entry.StableId);
		if (GlobalIds.Contains(Entry.StableId) || !RequiredPath || Entry.RoomType.ToSoftObjectPath() != *RequiredPath
			|| !FPackageName::DoesPackageExist(Entry.RoomType.ToSoftObjectPath().GetLongPackageName())
			|| !IsExactRegisteredAsset(Entry.RoomType.ToSoftObjectPath())
			|| Entry.BaseWeight <= 0 || Entry.BaseWeight > 100
			|| !FMath::IsFinite(Entry.BiasAxis) || Entry.BiasAxis < -1.0f || Entry.BiasAxis > 1.0f)
		{
			OutError = FString::Printf(TEXT("Theme %s violates the Forge/Shrine contract."), *Entry.StableId.ToString());
			return false;
		}
		GlobalIds.Add(Entry.StableId);
	}
	return true;
}

FString UEFCalystoDungeonDirectorPolicy::BuildCanonicalString() const
{
	using namespace EFCalystoDirectorPolicyPrivate;
	FString Canonical = FString::Printf(
		TEXT("EFCalystoDirectorPolicyV3|%d|%d|%s|L:%d:%d:%d:%d:%d:%d:%d:%d:%s:%s:%s:%s:%d:%d|"),
		SchemaVersion,
		GeneratorVersion,
		*PolicyId.ToString(),
		Limits.MinDungeonEdge,
		Limits.MaxDungeonEdge,
		Limits.MaxEnemies,
		Limits.MaxFood,
		Limits.MaxChests,
		Limits.MaxLoot,
		Limits.MaxSpecialEvents,
		Limits.MaxDirectorActors,
		*FloatBits(Limits.MinCandidateAnchorDensity),
		*FloatBits(Limits.MaxCandidateAnchorDensity),
		*FloatBits(Limits.MinSidePathChance),
		*FloatBits(Limits.MaxSidePathChance),
		Limits.RoomMinSize,
		Limits.RoomMaxSize);

	auto AppendFloat = [&Canonical](const float Value) { Canonical += FloatBits(Value) + TEXT("|"); };
	auto AppendFloatDistribution = [&Canonical](const TCHAR* Label, const FEFCalystoFloatDistribution& Distribution)
	{
		Canonical += FString::Printf(TEXT("FD:%s:%s:%s:%s:%s|"), Label, *FloatBits(Distribution.Min),
			*FloatBits(Distribution.Mode), *FloatBits(Distribution.Max), *FloatBits(Distribution.Concentration));
	};
	auto AppendIntDistribution = [&Canonical](const TCHAR* Label, const FEFCalystoIntDistribution& Distribution)
	{
		Canonical += FString::Printf(TEXT("ID:%s:%d:%d:%d:%s|"), Label, Distribution.Min, Distribution.Mode,
			Distribution.Max, *FloatBits(Distribution.Concentration));
	};
	AppendFloat(Progression.LayoutTau);
	AppendFloat(Progression.ThreatTau);
	AppendFloat(Progression.StartSizeMode);
	AppendFloat(Progression.EndSizeMode);
	AppendFloat(Progression.SizeHalfRange);
	AppendFloat(Progression.StartEnemyPresence);
	AppendFloat(Progression.EndEnemyPresence);
	AppendFloat(Progression.StartEnemyCountMode);
	AppendFloat(Progression.EndEnemyCountMode);
	AppendFloat(Progression.EnemyCountLowerOffset);
	AppendFloat(Progression.EnemyCountUpperOffset);
	AppendFloat(Progression.StartThreatBudget);
	AppendFloat(Progression.EndThreatBudget);
	AppendFloat(Progression.ThreatBudgetRelativeRange);
	AppendFloat(Progression.PacingAmplitude);
	AppendFloat(Progression.StartFoodPresence);
	AppendFloat(Progression.EndFoodPresence);
	AppendFloat(Progression.StartChestPresence);
	AppendFloat(Progression.EndChestPresence);
	AppendFloat(Progression.StartLootPresence);
	AppendFloat(Progression.EndLootPresence);
	AppendFloat(Progression.StartSpecialEventPresence);
	AppendFloat(Progression.EndSpecialEventPresence);
	AppendFloatDistribution(TEXT("AnchorDensity"), Progression.CandidateAnchorDensity);
	AppendFloatDistribution(TEXT("SidePath"), Progression.SidePathChance);
	AppendIntDistribution(TEXT("FoodCount"), Progression.FoodCount);
	AppendIntDistribution(TEXT("ChestCount"), Progression.ChestCount);
	AppendIntDistribution(TEXT("LootCount"), Progression.LootCount);
	AppendIntDistribution(TEXT("SpecialEventCount"), Progression.SpecialEventCount);
	Canonical += FString::Printf(TEXT("ECO:%s:%s:%s:%d:%d:%d:%d:%d:%s|"), *FloatBits(Ecology.RunDNAWeight),
		*FloatBits(Ecology.SmoothNoiseWeight), *FloatBits(Ecology.JitterWeight), Ecology.SmoothNoisePeriod,
		Ecology.MaxConsecutiveStyle, Ecology.MaxConsecutiveDominantTheme, Ecology.FoodPityAfterEmptyFloors,
		Ecology.ChestPityAfterEmptyFloors, *FloatBits(Ecology.RaritySelectionStrength));
	Canonical += FString::Printf(TEXT("ADAPT:%s:%s:%s:%s:%s:%s:%s|"), *FloatBits(Adaptation.CombatWeight),
		*FloatBits(Adaptation.SurvivalWeight), *FloatBits(Adaptation.ResourcesWeight), *FloatBits(Adaptation.PaceWeight),
		*FloatBits(Adaptation.EMAAlpha), *FloatBits(Adaptation.MaximumInfluence), *FloatBits(Adaptation.DeathPenalty));

	TArray<int32> Sizes = ValidatedDungeonSizes;
	Sizes.Sort();
	for (const int32 Size : Sizes) { Canonical += FString::Printf(TEXT("Z:%d|"), Size); }

	TArray<FEFCalystoStylePolicy> SortedStyles = Styles;
	SortedStyles.Sort([](const FEFCalystoStylePolicy& A, const FEFCalystoStylePolicy& B) { return static_cast<uint8>(A.Style) < static_cast<uint8>(B.Style); });
	for (const FEFCalystoStylePolicy& Style : SortedStyles)
	{
		Canonical += FString::Printf(TEXT("S:%d:%d:%s:%s:%s:%s:%s|"), static_cast<int32>(Style.Style), Style.SelectionWeight,
			*FloatBits(Style.ScaleBias), *FloatBits(Style.BranchingBias), *FloatBits(Style.ThreatBias),
			*FloatBits(Style.AbundanceBias), *FloatBits(Style.MysteryBias));
	}

	auto AppendCatalog = [&Canonical](const TCHAR* Prefix, TArray<FEFCalystoPopulationCatalogEntry> Catalog)
	{
		Catalog.Sort([](const FEFCalystoPopulationCatalogEntry& A, const FEFCalystoPopulationCatalogEntry& B) { return A.StableId.LexicalLess(B.StableId); });
		for (const FEFCalystoPopulationCatalogEntry& Entry : Catalog)
		{
			Canonical += FString::Printf(TEXT("%s:%s:%d:%s:%d:%s:%lld:%d:%s:%d|"), Prefix,
				*Entry.StableId.ToString(), Entry.bEnabled ? 1 : 0,
				*Entry.ActorClass.ToSoftObjectPath().ToString(), Entry.BaseWeight, *FloatBits(Entry.Cost),
				Entry.MinimumFloor, Entry.MaxPerFloor, *FloatBits(Entry.Rarity), Entry.CooldownFloors);
		}
	};
	AppendCatalog(TEXT("E"), EnemyCatalog);
	AppendCatalog(TEXT("F"), FoodCatalog);
	AppendCatalog(TEXT("C"), ChestCatalog);
	AppendCatalog(TEXT("R"), LootCatalog);
	AppendCatalog(TEXT("V"), SpecialEventCatalog);

	TArray<FEFCalystoThemeCatalogEntry> Themes = ThemeCatalog;
	Themes.Sort([](const FEFCalystoThemeCatalogEntry& A, const FEFCalystoThemeCatalogEntry& B) { return A.StableId.LexicalLess(B.StableId); });
	for (const FEFCalystoThemeCatalogEntry& Entry : Themes)
	{
		Canonical += FString::Printf(TEXT("T:%s:%s:%d:%s|"), *Entry.StableId.ToString(), *Entry.RoomType.ToSoftObjectPath().ToString(),
			Entry.BaseWeight, *FloatBits(Entry.BiasAxis));
	}
	return Canonical;
}
