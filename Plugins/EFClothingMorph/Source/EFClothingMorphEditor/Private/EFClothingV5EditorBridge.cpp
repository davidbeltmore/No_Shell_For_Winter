#include "EFClothingV5EditorBridge.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EFClothingBodyProfile.h"
#include "EFClothingDefinition.h"
#include "EFClothingFitProfile.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingSurfaceBinding.h"
#include "EFClothingSystemManifest.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFClothingV5EditorBridge, Log, All);

namespace EFClothingV5EditorBridgePrivate
{
	constexpr TCHAR V5RegistryPackagePath[] =
		TEXT("/EFClothingMorph/_Internal/Compiled/V5");
	constexpr TCHAR V5RegistryAssetName[] = TEXT("DA_EFClothingFitRegistry");
	constexpr TCHAR BodyProfilePackagePath[] =
		TEXT("/EFClothingMorph/_Internal/Compiled/V5/BodyProfiles");
	constexpr TCHAR GarmentDefinitionPackagePath[] =
		TEXT("/EFClothingMorph/_Internal/Compiled/V5/Garments");
	constexpr TCHAR ManifestPackagePath[] =
		TEXT("/EFClothingMorph/_Internal/Compiled/V5");
	constexpr TCHAR ManifestAssetName[] = TEXT("DA_EFClothingSystemManifest");

	struct FBodyPlan
	{
		FSoftObjectPath BodyPath;
		FString AssetName;
		FName ProfileId = NAME_None;
		TArray<FName> SkinMaterialSlots;
	};

	struct FBindingPlan
	{
		FEFClothingV5StreamableBinding Record;
	};

	struct FGarmentPlan
	{
		FEFClothingGarmentRow Row;
		FSoftObjectPath BodyProfilePath;
		FString AssetName;
		TArray<FBindingPlan> Bindings;
	};

	struct FTrackedAsset
	{
		UObject* Asset = nullptr;
		bool bCreated = false;
		bool bChanged = false;
	};

	static FString MakeObjectPath(const FString& PackagePath, const FString& AssetName)
	{
		return FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *AssetName, *AssetName);
	}

	static FString SanitizeAssetToken(const FString& Value)
	{
		FString Sanitized = Value;
		for (TCHAR& Character : Sanitized)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
			{
				Character = TEXT('_');
			}
		}
		while (Sanitized.Contains(TEXT("__")))
		{
			Sanitized.ReplaceInline(TEXT("__"), TEXT("_"));
		}
		Sanitized.RemoveFromStart(TEXT("_"));
		Sanitized.RemoveFromEnd(TEXT("_"));
		return Sanitized.IsEmpty() ? TEXT("Unnamed") : Sanitized;
	}

	static void SortUniqueNames(TArray<FName>& Values)
	{
		Values.RemoveAll([](const FName Value)
		{
			return Value.IsNone();
		});
		Values.Sort(FNameLexicalLess());
		for (int32 Index = Values.Num() - 1; Index > 0; --Index)
		{
			if (Values[Index] == Values[Index - 1])
			{
				Values.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
	}

	static FName MakeBodyProfileId(const FSoftObjectPath& BodyPath)
	{
		const FString BodyName = FPackageName::ObjectPathToObjectName(BodyPath.ToString());
		return FName(*SanitizeAssetToken(BodyName));
	}

	static FString MakeBodyProfileAssetName(const FSoftObjectPath& BodyPath)
	{
		return FString::Printf(
			TEXT("DA_EFBodyProfile_%s"),
			*SanitizeAssetToken(FPackageName::ObjectPathToObjectName(BodyPath.ToString())));
	}

	static FString MakeGarmentAssetName(const FName GarmentId)
	{
		return FString::Printf(
			TEXT("DA_EFGarment_%s"),
			*SanitizeAssetToken(GarmentId.ToString()));
	}

	static FName MakeStableBindingId(
		const FName GarmentId,
		const FSoftObjectPath& SourcePath,
		const FSoftObjectPath& BodyPath,
		const int32 LODIndex)
	{
		const FString Canonical = FString::Printf(
			TEXT("Garment=%s|Source=%s|Body=%s|LOD=%d"),
			*GarmentId.ToString(),
			*SourcePath.ToString(),
			*BodyPath.ToString(),
			LODIndex);
		return FName(*FString::Printf(
			TEXT("EFV5_%s_%s_LOD%d"),
			*SanitizeAssetToken(GarmentId.ToString()),
			*FMD5::HashAnsiString(*Canonical).Left(16),
			LODIndex));
	}

	static bool IsDefaultLayerRule(const FEFClothingLayerRule& Rule)
	{
		return Rule.Layer == EEFClothingLayer::Base
			&& Rule.Priority == 0
			&& Rule.OccupiedRegions.IsEmpty();
	}

	static FEFClothingLayerRule MakeMigratedLayerRule(const FEFClothingGarmentRow& Row)
	{
		FEFClothingLayerRule Result = Row.LayerRule;
		if (!IsDefaultLayerRule(Row.LayerRule))
		{
			return Result;
		}

		const FString Identity = FString::Printf(
			TEXT("%s|%s"),
			*Row.GarmentId.ToString(),
			*Row.SourceGarment.ToSoftObjectPath().ToString()).ToLower();
		if (Identity.Contains(TEXT("underwear")) || Identity.Contains(TEXT("bikini")))
		{
			Result.Layer = EEFClothingLayer::Underwear;
		}
		else if (Identity.Contains(TEXT("rag")))
		{
			Result.Layer = EEFClothingLayer::Base;
		}
		return Result;
	}

	static bool LayerRulesEqual(
		const FEFClothingLayerRule& A,
		const FEFClothingLayerRule& B)
	{
		if (A.Layer != B.Layer
			|| A.Priority != B.Priority
			|| A.OccupiedRegions.Num() != B.OccupiedRegions.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.OccupiedRegions.Num(); ++Index)
		{
			const FEFClothingRegionOccupancyRule& Left = A.OccupiedRegions[Index];
			const FEFClothingRegionOccupancyRule& Right = B.OccupiedRegions[Index];
			if (Left.RegionId != Right.RegionId
				|| Left.OccupiedThicknessCm != Right.OccupiedThicknessCm
				|| Left.MinimumOuterGapCm != Right.MinimumOuterGapCm
				|| Left.bExclusiveWithinLayer != Right.bExclusiveWithinLayer)
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * V5.0 inferred underwear layers only in generated definitions. V5.1 makes
	 * the single Director row authoritative by materializing that legacy default
	 * once, without changing any explicit author choice.
	 */
	static int32 MaterializeLegacyLayerDefaults(UEFClothingMorphDirectorPolicy* Director)
	{
		if (!IsValid(Director))
		{
			return 0;
		}

		bool bModified = false;
		int32 MigratedCount = 0;
		for (FEFClothingGarmentRow& Row : Director->Garments)
		{
			const FEFClothingLayerRule Desired = MakeMigratedLayerRule(Row);
			if (LayerRulesEqual(Row.LayerRule, Desired))
			{
				continue;
			}
			if (!bModified)
			{
				Director->Modify();
				bModified = true;
			}
			Row.LayerRule = Desired;
			++MigratedCount;
		}
		if (bModified)
		{
			Director->MarkPackageDirty();
		}
		return MigratedCount;
	}

	static bool LowerBodyMorphGuardsEqual(
		const FEFClothingLowerBodyMorphGuardRule& A,
		const FEFClothingLowerBodyMorphGuardRule& B)
	{
		return A.bEnabled == B.bEnabled
			&& A.ExactBodyMorphNames == B.ExactBodyMorphNames
			&& A.MaximumClearanceCm == B.MaximumClearanceCm;
	}

	static bool IsDefaultLowerBodyMorphGuard(
		const FEFClothingLowerBodyMorphGuardRule& Rule)
	{
		return !Rule.bEnabled
			&& Rule.ExactBodyMorphNames.IsEmpty()
			&& Rule.MaximumClearanceCm
				== EFClothingMorphV5::MaximumLowerBodyMorphGuardClearanceCm;
	}

	static bool MakeAutomaticLowerBodyMorphGuard(
		const FName GarmentId,
		FEFClothingLowerBodyMorphGuardRule& OutRule)
	{
		float MaximumClearanceCm = 0.0f;
		if (GarmentId == FName(TEXT("UnderWearPanty_Female"))
			|| GarmentId == FName(TEXT("UnderWearBikini_Female")))
		{
			MaximumClearanceCm =
				EFClothingMorphV5::MaximumLowerBodyMorphGuardClearanceCm;
		}
		else if (GarmentId == FName(TEXT("RagPants")))
		{
			MaximumClearanceCm = 0.20f;
		}
		else
		{
			return false;
		}

		OutRule.bEnabled = true;
		OutRule.ExactBodyMorphNames = {FName(TEXT("Body Voluptuous"))};
		OutRule.MaximumClearanceCm = MaximumClearanceCm;
		return true;
	}

	/**
	 * Seeds only the three known lower-garment identities. This deliberately
	 * avoids Layer and CoverageTags because Bra shares the underwear layer and
	 * older rows may carry broad legacy coverage.
	 */
	static int32 MaterializeAutomaticLowerBodyMorphGuards(
		UEFClothingMorphDirectorPolicy* Director)
	{
		if (!IsValid(Director))
		{
			return 0;
		}

		bool bModified = false;
		int32 MigratedCount = 0;
		for (FEFClothingGarmentRow& Row : Director->Garments)
		{
			if (!IsDefaultLowerBodyMorphGuard(Row.LowerBodyMorphGuard))
			{
				continue;
			}

			FEFClothingLowerBodyMorphGuardRule Desired;
			if (!MakeAutomaticLowerBodyMorphGuard(Row.GarmentId, Desired))
			{
				continue;
			}
			if (!bModified)
			{
				Director->Modify();
				bModified = true;
			}
			Row.LowerBodyMorphGuard = MoveTemp(Desired);
			++MigratedCount;
		}
		if (bModified)
		{
			Director->MarkPackageDirty();
		}
		return MigratedCount;
	}

	static bool MaterialPoliciesEqual(
		const FEFClothingMaterialPolicySet& A,
		const FEFClothingMaterialPolicySet& B)
	{
		if (A.DefaultPolicy != B.DefaultPolicy || A.Overrides.Num() != B.Overrides.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Overrides.Num(); ++Index)
		{
			const FEFClothingMaterialPolicyRule& Left = A.Overrides[Index];
			const FEFClothingMaterialPolicyRule& Right = B.Overrides[Index];
			if (Left.bEnabled != Right.bEnabled
				|| Left.MaterialSlot != Right.MaterialSlot
				|| Left.Material.ToSoftObjectPath() != Right.Material.ToSoftObjectPath()
				|| Left.Policy != Right.Policy
				|| Left.Priority != Right.Priority)
			{
				return false;
			}
		}
		return true;
	}

	static bool StreamableBindingsEqual(
		const TArray<FEFClothingV5StreamableBinding>& A,
		const TArray<FEFClothingV5StreamableBinding>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			const FEFClothingV5StreamableBinding& Left = A[Index];
			const FEFClothingV5StreamableBinding& Right = B[Index];
			if (Left.StableBindingId != Right.StableBindingId
				|| Left.GarmentId != Right.GarmentId
				|| Left.SourceGarment.ToSoftObjectPath() != Right.SourceGarment.ToSoftObjectPath()
				|| Left.BodySurface.ToSoftObjectPath() != Right.BodySurface.ToSoftObjectPath()
				|| Left.Binding.ToSoftObjectPath() != Right.Binding.ToSoftObjectPath()
				|| Left.LODIndex != Right.LODIndex
				|| Left.ContentHash != Right.ContentHash
				|| Left.SchemaVersion != Right.SchemaVersion)
			{
				return false;
			}
		}
		return true;
	}

	template <typename AssetType>
	static AssetType* FindOrCreateDataAsset(
		const FString& PackagePath,
		const FString& AssetName,
		bool& bOutCreated,
		FString& OutError)
	{
		bOutCreated = false;
		const FString ObjectPath = MakeObjectPath(PackagePath, AssetName);
		if (AssetType* Existing = LoadObject<AssetType>(nullptr, *ObjectPath))
		{
			return Existing;
		}

		if (UObject* WrongType = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath))
		{
			OutError = FString::Printf(
				TEXT("V5 asset path %s is occupied by incompatible class %s."),
				*ObjectPath,
				*WrongType->GetClass()->GetPathName());
			return nullptr;
		}

		const FString PackageName = FString::Printf(TEXT("%s/%s"), *PackagePath, *AssetName);
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("Could not create package %s."), *PackageName);
			return nullptr;
		}

		AssetType* Asset = NewObject<AssetType>(
			Package,
			*AssetName,
			RF_Public | RF_Standalone | RF_Transactional);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("Could not create asset %s."), *ObjectPath);
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Asset);
		bOutCreated = true;
		return Asset;
	}

	static void BeginChange(UObject* Asset, bool& bAssetChanged)
	{
		if (!bAssetChanged)
		{
			Asset->Modify();
			bAssetChanged = true;
		}
	}

	template <typename ValueType>
	static void AssignIfDifferent(
		UObject* Asset,
		ValueType& Current,
		const ValueType& Desired,
		bool& bAssetChanged)
	{
		if (!(Current == Desired))
		{
			BeginChange(Asset, bAssetChanged);
			Current = Desired;
		}
	}

	static bool SoftPathsEqual(
		const TArray<TSoftObjectPtr<UEFClothingBodyProfile>>& A,
		const TArray<TSoftObjectPtr<UEFClothingBodyProfile>>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].ToSoftObjectPath() != B[Index].ToSoftObjectPath())
			{
				return false;
			}
		}
		return true;
	}

	static bool SoftPathsEqual(
		const TArray<TSoftObjectPtr<UEFClothingDefinition>>& A,
		const TArray<TSoftObjectPtr<UEFClothingDefinition>>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].ToSoftObjectPath() != B[Index].ToSoftObjectPath())
			{
				return false;
			}
		}
		return true;
	}

	static bool SaveAsset(UObject* Asset, FString& OutError)
	{
		if (!IsValid(Asset) || !Asset->GetOutermost())
		{
			OutError = TEXT("Cannot save an invalid V5 generated asset.");
			return false;
		}

		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.Error = GError;
		if (!UPackage::SavePackage(Package, Asset, *Filename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("Failed to save V5 asset %s."), *Asset->GetPathName());
			return false;
		}
		return true;
	}

	static bool BuildPlans(
		const UEFClothingMorphDirectorPolicy* Director,
		const UEFClothingFitRegistry* V4Registry,
		TArray<FBodyPlan>& OutBodies,
		TArray<FGarmentPlan>& OutGarments,
		int32& OutSkippedDraftCount,
		FString& OutError)
	{
		OutBodies.Reset();
		OutGarments.Reset();
		OutSkippedDraftCount = 0;
		TMap<FSoftObjectPath, int32> BodyPlanByPath;
		TMap<FString, FSoftObjectPath> BodyAssetNameOwners;
		TSet<FName> GarmentIds;
		TSet<FString> GarmentAssetNames;

		for (const FEFClothingGarmentRow& Row : Director->BuildBodyVariants())
		{
			if (!Row.bEnabled)
			{
				continue;
			}
			if (Row.GarmentId.IsNone() || Row.SourceGarment.IsNull() || Row.BodySurface.IsNull())
			{
				++OutSkippedDraftCount;
				continue;
			}
			if (GarmentIds.Contains(Row.GarmentId))
			{
				OutError = FString::Printf(
					TEXT("V5 sync found duplicate enabled GarmentId '%s'."),
					*Row.GarmentId.ToString());
				return false;
			}
			GarmentIds.Add(Row.GarmentId);

			const FString GarmentAssetName = MakeGarmentAssetName(Row.GarmentId);
			if (GarmentAssetNames.Contains(GarmentAssetName))
			{
				OutError = FString::Printf(
					TEXT("Two garment identities sanitize to the same V5 asset name '%s'."),
					*GarmentAssetName);
				return false;
			}
			GarmentAssetNames.Add(GarmentAssetName);

			const FSoftObjectPath SourcePath = Row.SourceGarment.ToSoftObjectPath();
			const FSoftObjectPath BodyPath = Row.BodySurface.ToSoftObjectPath();
			int32* BodyPlanIndex = BodyPlanByPath.Find(BodyPath);
			if (!BodyPlanIndex)
			{
				USkeletalMesh* BodyMesh = Row.BodySurface.LoadSynchronous();
				if (!BodyMesh)
				{
					OutError = FString::Printf(
						TEXT("Could not load body mesh %s to migrate its material slots."),
						*BodyPath.ToString());
					return false;
				}

				FBodyPlan BodyPlan;
				BodyPlan.BodyPath = BodyPath;
				BodyPlan.ProfileId = MakeBodyProfileId(BodyPath);
				BodyPlan.AssetName = MakeBodyProfileAssetName(BodyPath);
				for (const FSkeletalMaterial& Material : BodyMesh->GetMaterials())
				{
					BodyPlan.SkinMaterialSlots.Add(Material.MaterialSlotName);
				}
				SortUniqueNames(BodyPlan.SkinMaterialSlots);

				if (const FSoftObjectPath* ExistingOwner = BodyAssetNameOwners.Find(BodyPlan.AssetName))
				{
					OutError = FString::Printf(
						TEXT("Bodies %s and %s sanitize to the same V5 profile asset '%s'."),
						*ExistingOwner->ToString(),
						*BodyPath.ToString(),
						*BodyPlan.AssetName);
					return false;
				}
				BodyAssetNameOwners.Add(BodyPlan.AssetName, BodyPath);
				const int32 NewIndex = OutBodies.Add(MoveTemp(BodyPlan));
				BodyPlanByPath.Add(BodyPath, NewIndex);
				BodyPlanIndex = BodyPlanByPath.Find(BodyPath);
			}

			FGarmentPlan GarmentPlan;
			GarmentPlan.Row = Row;
			GarmentPlan.AssetName = GarmentAssetName;
			GarmentPlan.BodyProfilePath = FSoftObjectPath(MakeObjectPath(
				BodyProfilePackagePath,
				OutBodies[*BodyPlanIndex].AssetName));

			TArray<const UEFClothingSurfaceBinding*> MatchingBindings;
			for (const UEFClothingSurfaceBinding* Binding : V4Registry->NativeSourceBindings)
			{
				if (IsValid(Binding)
					&& Binding->GarmentId == Row.GarmentId
					&& Binding->SourceGarment.ToSoftObjectPath() == SourcePath
					&& Binding->BodySurface.ToSoftObjectPath() == BodyPath)
				{
					MatchingBindings.Add(Binding);
				}
			}
			if (MatchingBindings.Num() != 1)
			{
				OutError = FString::Printf(
					TEXT("Garment '%s' requires exactly one matching V4 binding; found %d."),
					*Row.GarmentId.ToString(),
					MatchingBindings.Num());
				return false;
			}

			const UEFClothingSurfaceBinding* Binding = MatchingBindings[0];
			TSet<int32> PublishedLODs;
			for (const FEFClothingSurfaceLODPairBinding& LODPair : Binding->LODPairBindings)
			{
				const int32 LODIndex = LODPair.GarmentTopology.LODIndex;
				if (LODIndex < 0 || PublishedLODs.Contains(LODIndex))
				{
					OutError = FString::Printf(
						TEXT("Garment '%s' has an invalid or duplicate V4 garment LOD %d."),
						*Row.GarmentId.ToString(),
						LODIndex);
					return false;
				}
				PublishedLODs.Add(LODIndex);

				FBindingPlan BindingPlan;
				BindingPlan.Record.StableBindingId = MakeStableBindingId(
					Row.GarmentId,
					SourcePath,
					BodyPath,
					LODIndex);
				BindingPlan.Record.GarmentId = Row.GarmentId;
				BindingPlan.Record.SourceGarment = Row.SourceGarment;
				BindingPlan.Record.BodySurface = Row.BodySurface;
				BindingPlan.Record.Binding = TSoftObjectPtr<UEFClothingSurfaceBinding>(
					FSoftObjectPath(Binding));
				BindingPlan.Record.LODIndex = LODIndex;
				BindingPlan.Record.ContentHash =
					FEFClothingV5StreamableBinding::ComputePayloadContentHash(Binding, LODPair);
				BindingPlan.Record.SchemaVersion = Binding->SchemaVersion;
				if (!BindingPlan.Record.HasValidMetadata())
				{
					OutError = FString::Printf(
						TEXT("Garment '%s' produced invalid V5 streamable metadata for LOD %d."),
						*Row.GarmentId.ToString(),
						LODIndex);
					return false;
				}
				GarmentPlan.Bindings.Add(MoveTemp(BindingPlan));
			}
			if (GarmentPlan.Bindings.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Garment '%s' has no V4 LOD binding to publish into V5."),
					*Row.GarmentId.ToString());
				return false;
			}
			GarmentPlan.Bindings.Sort([](const FBindingPlan& A, const FBindingPlan& B)
			{
				return A.Record.LODIndex < B.Record.LODIndex;
			});
			OutGarments.Add(MoveTemp(GarmentPlan));
		}

		OutBodies.Sort([](const FBodyPlan& A, const FBodyPlan& B)
		{
			return A.BodyPath.ToString() < B.BodyPath.ToString();
		});
		OutGarments.Sort([](const FGarmentPlan& A, const FGarmentPlan& B)
		{
			return A.Row.GarmentId.LexicalLess(B.Row.GarmentId);
		});
		if (OutGarments.IsEmpty())
		{
			OutError = TEXT("V5 sync found no complete enabled V4 garment rows.");
			return false;
		}
		return true;
	}
}

FEFClothingV5DirectorPreparationResult FEFClothingV5EditorBridge::PrepareDirectorDefaults(
	UEFClothingMorphDirectorPolicy* Director)
{
	using namespace EFClothingV5EditorBridgePrivate;
	FEFClothingV5DirectorPreparationResult Result;
	Result.MigratedLayerRowCount = MaterializeLegacyLayerDefaults(Director);
	Result.MigratedLowerBodyGuardRowCount = MaterializeAutomaticLowerBodyMorphGuards(Director);
	Result.bChanged = Result.MigratedLayerRowCount > 0
		|| Result.MigratedLowerBodyGuardRowCount > 0;
	return Result;
}

FEFClothingV5EditorSyncResult FEFClothingV5EditorBridge::SyncFromV4(
	UEFClothingMorphDirectorPolicy* Director,
	UEFClothingFitRegistry* V4Registry,
	const bool bSaveAssets)
{
	using namespace EFClothingV5EditorBridgePrivate;
	FEFClothingV5EditorSyncResult Result;
	if (!IsValid(Director) || !IsValid(V4Registry))
	{
		Result.Report = TEXT("V5 sync requires a valid V4 Director and V4 binding registry.");
		return Result;
	}
	if (Director->GetPathName().StartsWith(TEXT("/EFClothingMorph/_Internal/Compiled/V5/"))
		|| V4Registry->GetPathName().StartsWith(TEXT("/EFClothingMorph/_Internal/Compiled/V5/")))
	{
		Result.Report = TEXT("V5 sync refused a V5 output asset as its read-only V4 input.");
		return Result;
	}

	FString IdentityError;
	if (!Director->ValidateIdentity(IdentityError))
	{
		Result.Report = FString::Printf(TEXT("V4 Director identity is invalid: %s"), *IdentityError);
		return Result;
	}
	const FEFClothingV5DirectorPreparationResult Preparation =
		PrepareDirectorDefaults(Director);
	Result.MigratedAuthoringRowCount = Preparation.MigratedLayerRowCount;
	Result.MigratedLowerBodyGuardRowCount = Preparation.MigratedLowerBodyGuardRowCount;

	TArray<FBodyPlan> BodyPlans;
	TArray<FGarmentPlan> GarmentPlans;
	FString Error;
	if (!BuildPlans(
		Director,
		V4Registry,
		BodyPlans,
		GarmentPlans,
		Result.SkippedDraftCount,
		Error))
	{
		Result.Report = Error;
		return Result;
	}

	TArray<FTrackedAsset> TrackedAssets;
	if (Preparation.bChanged)
	{
		TrackedAssets.Add({Director, false, true});
	}
	TArray<TSoftObjectPtr<UEFClothingBodyProfile>> ManifestProfiles;
	for (const FBodyPlan& Plan : BodyPlans)
	{
		bool bCreated = false;
		UEFClothingBodyProfile* Profile = FindOrCreateDataAsset<UEFClothingBodyProfile>(
			BodyProfilePackagePath,
			Plan.AssetName,
			bCreated,
			Error);
		if (!Profile)
		{
			Result.Report = Error;
			return Result;
		}

		bool bChanged = bCreated;
		if (bCreated)
		{
			Profile->Modify();
		}
		AssignIfDifferent(Profile, Profile->SchemaVersion, EFClothingMorphV5::BodyProfileSchemaVersion, bChanged);
		AssignIfDifferent(Profile, Profile->BodyProfileId, Plan.ProfileId, bChanged);
		const TSoftObjectPtr<USkeletalMesh> DesiredBody(Plan.BodyPath);
		if (Profile->BodySurface.ToSoftObjectPath() != DesiredBody.ToSoftObjectPath())
		{
			BeginChange(Profile, bChanged);
			Profile->BodySurface = DesiredBody;
		}
		AssignIfDifferent(Profile, Profile->SkinMaterialSlots, Plan.SkinMaterialSlots, bChanged);

		FString ValidationError;
		if (!Profile->ValidateProfile(ValidationError))
		{
			Result.Report = FString::Printf(
				TEXT("Generated body profile %s is invalid: %s"),
				*Profile->GetPathName(),
				*ValidationError);
			return Result;
		}

		ManifestProfiles.Add(Profile);
		TrackedAssets.Add({Profile, bCreated, bChanged});
	}

	TArray<TSoftObjectPtr<UEFClothingDefinition>> ManifestGarments;
	TArray<FEFClothingV5StreamableBinding> DesiredStreamableBindings;
	for (const FGarmentPlan& Plan : GarmentPlans)
	{
		const FEFClothingGarmentRow& Row = Plan.Row;
		bool bCreated = false;
		UEFClothingDefinition* Definition = FindOrCreateDataAsset<UEFClothingDefinition>(
			GarmentDefinitionPackagePath,
			Plan.AssetName,
			bCreated,
			Error);
		if (!Definition)
		{
			Result.Report = Error;
			return Result;
		}

		bool bChanged = bCreated;
		if (bCreated)
		{
			Definition->Modify();
		}
		AssignIfDifferent(
			Definition,
			Definition->SchemaVersion,
			EFClothingMorphV5::ClothingDefinitionSchemaVersion,
			bChanged);
		AssignIfDifferent(Definition, Definition->GarmentId, Row.GarmentId, bChanged);
		if (Definition->SourceGarment.ToSoftObjectPath() != Row.SourceGarment.ToSoftObjectPath())
		{
			BeginChange(Definition, bChanged);
			Definition->SourceGarment = Row.SourceGarment;
		}
		if (Definition->BodyProfile.ToSoftObjectPath() != Plan.BodyProfilePath)
		{
			BeginChange(Definition, bChanged);
			Definition->BodyProfile = TSoftObjectPtr<UEFClothingBodyProfile>(Plan.BodyProfilePath);
		}

		const FEFClothingLayerRule DesiredLayerRule = MakeMigratedLayerRule(Row);
		if (!LayerRulesEqual(Definition->LayerRule, DesiredLayerRule))
		{
			BeginChange(Definition, bChanged);
			Definition->LayerRule = DesiredLayerRule;
		}
		AssignIfDifferent(
			Definition,
			Definition->CoveredBodyRegionIds,
			Row.CoveredBodyRegionIds,
			bChanged);
		AssignIfDifferent(Definition, Definition->CoverageTags, Row.CoverageTags, bChanged);
		if (!MaterialPoliciesEqual(Definition->MaterialPolicy, Row.MaterialPolicy))
		{
			BeginChange(Definition, bChanged);
			Definition->MaterialPolicy = Row.MaterialPolicy;
		}
		AssignIfDifferent(
			Definition,
			Definition->AdditionalClearanceCm,
			FMath::Clamp(Row.AdditionalClearanceCm, 0.0f, EFClothingMorphV5::MaximumClearanceCm),
			bChanged);
		AssignIfDifferent(
			Definition,
			Definition->ShellThicknessCm,
			FMath::Clamp(Row.ShellThicknessCm, 0.0f, EFClothingMorphV5::MaximumShellThicknessCm),
			bChanged);
		if (!LowerBodyMorphGuardsEqual(
			Definition->LowerBodyMorphGuard,
			Row.LowerBodyMorphGuard))
		{
			BeginChange(Definition, bChanged);
			Definition->LowerBodyMorphGuard = Row.LowerBodyMorphGuard;
		}

		const FBindingPlan* PrimaryBinding = Plan.Bindings.FindByPredicate([](const FBindingPlan& Candidate)
		{
			return Candidate.Record.LODIndex == 0;
		});
		if (!PrimaryBinding)
		{
			PrimaryBinding = &Plan.Bindings[0];
		}
		AssignIfDifferent(
			Definition,
			Definition->BindingStableId,
			PrimaryBinding->Record.StableBindingId,
			bChanged);
		AssignIfDifferent(
			Definition,
			Definition->BindingVersion,
			PrimaryBinding->Record.SchemaVersion,
			bChanged);

		FString ValidationError;
		if (!Definition->ValidateDefinition(ValidationError))
		{
			Result.Report = FString::Printf(
				TEXT("Generated garment definition %s is invalid: %s"),
				*Definition->GetPathName(),
				*ValidationError);
			return Result;
		}

		for (const FBindingPlan& Binding : Plan.Bindings)
		{
			DesiredStreamableBindings.Add(Binding.Record);
		}
		ManifestGarments.Add(Definition);
		TrackedAssets.Add({Definition, bCreated, bChanged});
	}

	DesiredStreamableBindings.Sort([](
		const FEFClothingV5StreamableBinding& A,
		const FEFClothingV5StreamableBinding& B)
	{
		return A.StableBindingId.LexicalLess(B.StableBindingId);
	});

	bool bRegistryCreated = false;
	UEFClothingFitRegistry* V5Registry = FindOrCreateDataAsset<UEFClothingFitRegistry>(
		V5RegistryPackagePath,
		V5RegistryAssetName,
		bRegistryCreated,
		Error);
	if (!V5Registry)
	{
		Result.Report = Error;
		return Result;
	}
	bool bRegistryChanged = bRegistryCreated;
	if (bRegistryCreated)
	{
		V5Registry->Modify();
	}
	if (!V5Registry->Profiles.IsEmpty())
	{
		BeginChange(V5Registry, bRegistryChanged);
		V5Registry->Profiles.Reset();
	}
	if (!V5Registry->NativeSourceBindings.IsEmpty())
	{
		BeginChange(V5Registry, bRegistryChanged);
		V5Registry->NativeSourceBindings.Reset();
	}
	if (!StreamableBindingsEqual(V5Registry->V5StreamableBindings, DesiredStreamableBindings))
	{
		BeginChange(V5Registry, bRegistryChanged);
		V5Registry->V5StreamableBindings = DesiredStreamableBindings;
	}
	V5Registry->RebuildV5StreamableBindingIndex();
	TrackedAssets.Add({V5Registry, bRegistryCreated, bRegistryChanged});

	ManifestProfiles.Sort([](
		const TSoftObjectPtr<UEFClothingBodyProfile>& A,
		const TSoftObjectPtr<UEFClothingBodyProfile>& B)
	{
		return A.ToSoftObjectPath().ToString() < B.ToSoftObjectPath().ToString();
	});
	ManifestGarments.Sort([](
		const TSoftObjectPtr<UEFClothingDefinition>& A,
		const TSoftObjectPtr<UEFClothingDefinition>& B)
	{
		return A.ToSoftObjectPath().ToString() < B.ToSoftObjectPath().ToString();
	});

	bool bManifestCreated = false;
	UEFClothingSystemManifest* Manifest = FindOrCreateDataAsset<UEFClothingSystemManifest>(
		ManifestPackagePath,
		ManifestAssetName,
		bManifestCreated,
		Error);
	if (!Manifest)
	{
		Result.Report = Error;
		return Result;
	}
	bool bManifestChanged = bManifestCreated;
	if (bManifestCreated)
	{
		Manifest->Modify();
	}
	AssignIfDifferent(Manifest, Manifest->SchemaVersion, 1, bManifestChanged);
	AssignIfDifferent(Manifest, Manifest->SystemId, FName(TEXT("EFClothingMorphV5")), bManifestChanged);
	if (!SoftPathsEqual(Manifest->BodyProfiles, ManifestProfiles))
	{
		BeginChange(Manifest, bManifestChanged);
		Manifest->BodyProfiles = ManifestProfiles;
	}
	if (!SoftPathsEqual(Manifest->Garments, ManifestGarments))
	{
		BeginChange(Manifest, bManifestChanged);
		Manifest->Garments = ManifestGarments;
	}
	if (Manifest->BindingRegistry.ToSoftObjectPath() != FSoftObjectPath(V5Registry))
	{
		BeginChange(Manifest, bManifestChanged);
		Manifest->BindingRegistry = V5Registry;
	}

	FString ManifestValidationError;
	if (!Manifest->Validate(ManifestValidationError))
	{
		Result.Report = FString::Printf(
			TEXT("Generated V5 manifest %s is invalid: %s"),
			*Manifest->GetPathName(),
			*ManifestValidationError);
		return Result;
	}
	TrackedAssets.Add({Manifest, bManifestCreated, bManifestChanged});

	for (const FTrackedAsset& Tracked : TrackedAssets)
	{
		if (Tracked.bCreated)
		{
			++Result.CreatedAssetCount;
		}
		else if (Tracked.bChanged)
		{
			++Result.UpdatedAssetCount;
		}
		if (Tracked.bChanged)
		{
			Result.bChanged = true;
			Tracked.Asset->MarkPackageDirty();
			if (bSaveAssets)
			{
				if (!SaveAsset(Tracked.Asset, Error))
				{
					Result.Report = Error;
					return Result;
				}
				++Result.SavedAssetCount;
			}
		}
	}

	Result.BodyProfileCount = ManifestProfiles.Num();
	Result.GarmentDefinitionCount = ManifestGarments.Num();
	Result.StreamableBindingCount = DesiredStreamableBindings.Num();
	Result.Registry = V5Registry;
	Result.Manifest = Manifest;
	Result.bSuccess = true;
	Result.Report = FString::Printf(
		TEXT("EF Clothing Morph V5.1 single-table sync %s: bodies=%d garments=%d LOD bindings=%d created=%d updated=%d saved=%d drafts=%d migrated-rows=%d lower-body-guards=%d."),
		Result.bChanged ? TEXT("changed assets") : TEXT("was already current"),
		Result.BodyProfileCount,
		Result.GarmentDefinitionCount,
		Result.StreamableBindingCount,
		Result.CreatedAssetCount,
		Result.UpdatedAssetCount,
		Result.SavedAssetCount,
		Result.SkippedDraftCount,
		Result.MigratedAuthoringRowCount,
		Result.MigratedLowerBodyGuardRowCount);
	UE_LOG(LogEFClothingV5EditorBridge, Display, TEXT("%s"), *Result.Report);
	return Result;
}
