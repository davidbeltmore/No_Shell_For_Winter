#include "EFClothingGarmentRowCustomization.h"
#include "EFClothingMorphDirectorDetails.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingFitProfile.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingMorphV2Settings.h"
#include "EFClothingNativeSourceEditorGate.h"
#include "EFClothingV5EditorBridge.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Features/IModularFeatures.h"
#include "IPIEAuthorizer.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "FEFClothingMorphEditorModule"

DEFINE_LOG_CATEGORY_STATIC(LogEFClothingMorphEditor, Log, All);

namespace EFClothingMorphEditorModulePrivate
{
	constexpr TCHAR CompatibilityReferencePath[] = TEXT("/Game/DazToUnreal/Multiple/Multiple.Multiple");
	constexpr TCHAR ManagedMaterialRoot[] = TEXT("/Game/_Game/Generated/EFClothingMorph/V5/Materials/");

	struct FV5GarmentMaterialPolicyResult
	{
		int32 ChangedMaterialCount = 0;
		int32 AlreadyOverriddenCount = 0;
		int32 ExplicitExceptionCount = 0;
		int32 ManagedMaterialCount = 0;
		int32 UnsupportedMaterialCount = 0;
		int32 ProtectedMaterialCount = 0;
		int32 MissingGarmentCount = 0;
		bool bSaved = true;
	};

	static bool DoesMaterialRuleMatch(
		const FEFClothingMaterialPolicyRule& Rule,
		const FSkeletalMaterial& SkeletalMaterial)
	{
		if (!Rule.bEnabled || !Rule.HasFilter())
		{
			return false;
		}
		const bool bSlotMatches = Rule.MaterialSlot.IsNone()
			|| Rule.MaterialSlot == SkeletalMaterial.MaterialSlotName
#if WITH_EDITORONLY_DATA
			|| Rule.MaterialSlot == SkeletalMaterial.ImportedMaterialSlotName
#endif
			;
		bool bMaterialMatches = Rule.Material.IsNull()
			|| Rule.Material.ToSoftObjectPath()
				== FSoftObjectPath(SkeletalMaterial.MaterialInterface);
		// A managed V5 child must keep matching an exception authored against
		// the original slot material; otherwise the first policy application
		// would silently invalidate that rule on the next refresh.
		const UMaterialInstanceConstant* ManagedInstance =
			Cast<UMaterialInstanceConstant>(SkeletalMaterial.MaterialInterface);
		if (!bMaterialMatches
			&& !Rule.Material.IsNull()
			&& IsValid(ManagedInstance)
			&& ManagedInstance->GetOutermost()->GetName().StartsWith(ManagedMaterialRoot)
			&& IsValid(ManagedInstance->Parent))
		{
			bMaterialMatches = Rule.Material.ToSoftObjectPath()
				== FSoftObjectPath(ManagedInstance->Parent);
		}
		return bSlotMatches && bMaterialMatches;
	}

	static EEFClothingMaterialPolicy ResolveMaterialPolicy(
		const FEFClothingMaterialPolicySet& PolicySet,
		const FSkeletalMaterial& SkeletalMaterial)
	{
		EEFClothingMaterialPolicy Result = PolicySet.DefaultPolicy;
		int32 WinningPriority = TNumericLimits<int32>::Lowest();
		for (const FEFClothingMaterialPolicyRule& Rule : PolicySet.Overrides)
		{
			if (DoesMaterialRuleMatch(Rule, SkeletalMaterial)
				&& Rule.Priority >= WinningPriority)
			{
				WinningPriority = Rule.Priority;
				Result = Rule.Policy;
			}
		}
		return Result;
	}

	static bool SetBlendModeOverride(
		UMaterialInstanceConstant* MaterialInstance,
		const bool bOverrideBlendMode,
		const EBlendMode BlendMode,
		TArray<UObject*>& ChangedAssets)
	{
		if (!IsValid(MaterialInstance))
		{
			return false;
		}

		const FMaterialInstanceBasePropertyOverrides& CurrentOverrides =
			MaterialInstance->BasePropertyOverrides;
		if (CurrentOverrides.bOverride_BlendMode == bOverrideBlendMode
			&& (!bOverrideBlendMode || CurrentOverrides.BlendMode == BlendMode))
		{
			return false;
		}

		FMaterialInstanceBasePropertyOverrides NewOverrides = CurrentOverrides;
		NewOverrides.bOverride_BlendMode = bOverrideBlendMode;
		if (bOverrideBlendMode)
		{
			NewOverrides.BlendMode = BlendMode;
		}

		MaterialInstance->Modify();
		{
			FMaterialInstanceParameterUpdateContext UpdateContext(MaterialInstance);
			UpdateContext.SetBasePropertyOverrides(NewOverrides);
		}
		MaterialInstance->MarkPackageDirty();
		ChangedAssets.AddUnique(MaterialInstance);
		return true;
	}

	static FString MakeManagedMaterialAssetName(
		const USkeletalMesh* GarmentMesh,
		const FSkeletalMaterial& SkeletalMaterial,
		const int32 MaterialIndex)
	{
		FString SlotName = SkeletalMaterial.MaterialSlotName.IsNone()
			? FString::Printf(TEXT("Slot%d"), MaterialIndex)
			: SkeletalMaterial.MaterialSlotName.ToString();
		SlotName.ReplaceInline(TEXT(" "), TEXT("_"));
		SlotName.ReplaceInline(TEXT("-"), TEXT("_"));
		const FString Identity = GetPathNameSafe(GarmentMesh)
			+ TEXT("|") + SlotName
			+ TEXT("|") + GetPathNameSafe(SkeletalMaterial.MaterialInterface);
		return FString::Printf(
			TEXT("MI_EF_%s_%s_%s"),
			*GarmentMesh->GetName(),
			*SlotName,
			*FMD5::HashAnsiString(*Identity).Left(8));
	}

	static EBlendMode ResolveAuthoredBlendMode(const UMaterialInterface* Material)
	{
		const UMaterialInterface* Candidate = Material;
		for (int32 Depth = 0; IsValid(Candidate) && Depth < 8; ++Depth)
		{
			const UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Candidate);
			if (!IsValid(Instance)
				|| !Instance->BasePropertyOverrides.bOverride_BlendMode
				|| Instance->BasePropertyOverrides.BlendMode != BLEND_Opaque
				|| !IsValid(Instance->Parent))
			{
				return Candidate->GetBlendMode();
			}
			// Explicit AllowTranslucent/PreserveMasked opts out of the earlier
			// opaque safety override, so walk through consecutive opaque MICs.
			Candidate = Instance->Parent;
		}
		return IsValid(Candidate) ? Candidate->GetBlendMode() : BLEND_Opaque;
	}

	static UMaterialInstanceConstant* EnsureManagedMaterialInstance(
		USkeletalMesh* GarmentMesh,
		FSkeletalMaterial& SkeletalMaterial,
		const int32 MaterialIndex,
		FV5GarmentMaterialPolicyResult& Result,
		TArray<UObject*>& ChangedAssets)
	{
		UMaterialInterface* SourceMaterial = SkeletalMaterial.MaterialInterface;
		if (!IsValid(GarmentMesh) || !IsValid(SourceMaterial))
		{
			return nullptr;
		}
		if (UMaterialInstanceConstant* ExistingManaged = Cast<UMaterialInstanceConstant>(SourceMaterial))
		{
			if (ExistingManaged->GetOutermost()->GetName().StartsWith(ManagedMaterialRoot))
			{
				return ExistingManaged;
			}
		}
		if (!GarmentMesh->GetOutermost()->GetName().StartsWith(TEXT("/Game/")))
		{
			++Result.ProtectedMaterialCount;
			UE_LOG(
				LogEFClothingMorphEditor,
				Warning,
				TEXT("EF Clothing Morph V5.1 left protected garment '%s' unchanged; a project-owned garment is required for a managed material override."),
				*GarmentMesh->GetPathName());
			return nullptr;
		}

		const FString ManagedAssetName = MakeManagedMaterialAssetName(
			GarmentMesh,
			SkeletalMaterial,
			MaterialIndex);
		const FString ManagedPackageName = FString(ManagedMaterialRoot) + ManagedAssetName;
		const FString ManagedObjectPath = ManagedPackageName + TEXT(".") + ManagedAssetName;
		UMaterialInstanceConstant* ManagedInstance = LoadObject<UMaterialInstanceConstant>(
			nullptr,
			*ManagedObjectPath);
		if (!IsValid(ManagedInstance))
		{
			UPackage* ManagedPackage = CreatePackage(*ManagedPackageName);
			ManagedInstance = NewObject<UMaterialInstanceConstant>(
				ManagedPackage,
				*ManagedAssetName,
				RF_Public | RF_Standalone | RF_Transactional);
			if (!IsValid(ManagedInstance))
			{
				++Result.UnsupportedMaterialCount;
				return nullptr;
			}
			FAssetRegistryModule::AssetCreated(ManagedInstance);
			++Result.ManagedMaterialCount;
		}
		if (ManagedInstance->Parent != SourceMaterial)
		{
			ManagedInstance->Modify();
			ManagedInstance->SetParentEditorOnly(SourceMaterial);
			ManagedInstance->MarkPackageDirty();
			ChangedAssets.AddUnique(ManagedInstance);
		}
		if (SkeletalMaterial.MaterialInterface != ManagedInstance)
		{
			GarmentMesh->Modify();
			SkeletalMaterial.MaterialInterface = ManagedInstance;
			GarmentMesh->MarkPackageDirty();
			ChangedAssets.AddUnique(GarmentMesh);
		}
		return ManagedInstance;
	}

	/**
	 * Applies the V5 declarative material contract for every Director garment.
	 * ForceOpaque remains the fail-safe default. Masked and translucent rendering
	 * require explicit policy. Third-party/direct materials are never edited:
	 * they receive a project-owned managed instance assigned to the garment slot.
	 */
	static FV5GarmentMaterialPolicyResult ApplyV5GarmentMaterialPolicy(
		const UEFClothingMorphDirectorPolicy* Director)
	{
		FV5GarmentMaterialPolicyResult Result;
		if (!IsValid(Director) || IsRunningCommandlet())
		{
			return Result;
		}

		TArray<UObject*> ChangedAssets;
		for (const FEFClothingGarmentRow& Clothing : Director->BuildBodyVariants())
		{
			if (!Clothing.bEnabled || Clothing.SourceGarment.IsNull())
			{
				continue;
			}

			USkeletalMesh* GarmentMesh = Clothing.SourceGarment.LoadSynchronous();
			if (!IsValid(GarmentMesh))
			{
				++Result.MissingGarmentCount;
				UE_LOG(
					LogEFClothingMorphEditor,
					Warning,
					TEXT("EF Clothing Morph V5.1 could not load clothing mesh '%s' while applying its material policy."),
					*Clothing.SourceGarment.ToSoftObjectPath().ToString());
				continue;
			}

			TArray<FSkeletalMaterial>& SkeletalMaterials = GarmentMesh->GetMaterials();
			for (int32 MaterialIndex = 0; MaterialIndex < SkeletalMaterials.Num(); ++MaterialIndex)
			{
				FSkeletalMaterial& SkeletalMaterial = SkeletalMaterials[MaterialIndex];
				UMaterialInterface* Material = SkeletalMaterial.MaterialInterface;
				if (!IsValid(Material))
				{
					continue;
				}

				const EEFClothingMaterialPolicy EffectivePolicy = ResolveMaterialPolicy(
					Clothing.MaterialPolicy,
					SkeletalMaterial);
				const EBlendMode AuthoredBlendMode = ResolveAuthoredBlendMode(Material);
				if (EffectivePolicy == EEFClothingMaterialPolicy::AllowTranslucent)
				{
					// AllowTranslucent means that an earlier V4.5/V5 opaque safety
					// override must no longer win over the authored parent material.
					// Non-opaque authored overrides are intentionally preserved.
					if (Material->GetBlendMode() == BLEND_Opaque
						&& AuthoredBlendMode != BLEND_Opaque)
					{
						UMaterialInstanceConstant* ManagedInstance = EnsureManagedMaterialInstance(
							GarmentMesh,
							SkeletalMaterial,
							MaterialIndex,
							Result,
							ChangedAssets);
						if (SetBlendModeOverride(
							ManagedInstance,
							true,
							AuthoredBlendMode,
							ChangedAssets))
						{
							++Result.ChangedMaterialCount;
						}
					}
					++Result.ExplicitExceptionCount;
					continue;
				}
				if (EffectivePolicy == EEFClothingMaterialPolicy::PreserveMasked)
				{
					if (AuthoredBlendMode == BLEND_Masked)
					{
						if (Material->GetBlendMode() != BLEND_Masked)
						{
							UMaterialInstanceConstant* ManagedInstance = EnsureManagedMaterialInstance(
								GarmentMesh,
								SkeletalMaterial,
								MaterialIndex,
								Result,
								ChangedAssets);
							if (SetBlendModeOverride(
								ManagedInstance,
								true,
								BLEND_Masked,
								ChangedAssets))
							{
								++Result.ChangedMaterialCount;
							}
						}
						++Result.ExplicitExceptionCount;
						continue;
					}
				}

				UMaterialInstanceConstant* MaterialInstance = EnsureManagedMaterialInstance(
					GarmentMesh,
					SkeletalMaterial,
					MaterialIndex,
					Result,
					ChangedAssets);
				if (!IsValid(MaterialInstance))
				{
					continue;
				}

				if (!SetBlendModeOverride(
					MaterialInstance,
					true,
					BLEND_Opaque,
					ChangedAssets))
				{
					++Result.AlreadyOverriddenCount;
					continue;
				}
				++Result.ChangedMaterialCount;
			}
		}

		if (!ChangedAssets.IsEmpty())
		{
			UEditorAssetSubsystem* AssetSubsystem = GEditor
				? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
				: nullptr;
			Result.bSaved = IsValid(AssetSubsystem)
				&& AssetSubsystem->SaveLoadedAssets(ChangedAssets, true);
		}
		return Result;
	}

	static bool SavePreparedDirector(
		UEFClothingMorphDirectorPolicy* Director,
		const FEFClothingV5DirectorPreparationResult& Preparation)
	{
		if (!Preparation.bChanged)
		{
			return true;
		}
		UEditorAssetSubsystem* AssetSubsystem = GEditor
			? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
			: nullptr;
		TArray<UObject*> AssetsToSave = {Director};
		return IsValid(AssetSubsystem)
			&& AssetSubsystem->SaveLoadedAssets(AssetsToSave, true);
	}
}

class FEFClothingMorphEditorModule final : public IModuleInterface, public IPIEAuthorizer
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyEditor =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyEditor.RegisterCustomClassLayout(
			TEXT("EFClothingMorphDirectorPolicy"),
			FOnGetDetailCustomizationInstance::CreateStatic(
				&FEFClothingMorphDirectorDetails::MakeInstance));
		PropertyEditor.RegisterCustomPropertyTypeLayout(
			TEXT("EFClothingGarmentRow"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(
				&FEFClothingGarmentRowCustomization::MakeInstance));
		PropertyEditor.NotifyCustomizationModuleChanged();

		ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(
			this,
			&FEFClothingMorphEditorModule::OnObjectPropertyChanged);
		PackageSavedHandle = UPackage::PackageSavedWithContextEvent.AddRaw(
			this,
			&FEFClothingMorphEditorModule::OnPackageSaved);

		IModularFeatures::Get().RegisterModularFeature(IPIEAuthorizer::GetModularFeatureName(), this);

		// Run the same catalog refresh once on editor startup so existing database
		// entries also receive the invariant without requiring a manual re-save.
		const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
		if (Settings && Settings->bEnabled)
		{
			QueueAutomaticRefresh(Settings->DirectorPolicy.LoadSynchronous());
		}
	}

	virtual void ShutdownModule() override
	{
		IModularFeatures::Get().UnregisterModularFeature(IPIEAuthorizer::GetModularFeatureName(), this);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
		UPackage::PackageSavedWithContextEvent.Remove(PackageSavedHandle);
		if (AutomaticRefreshTickerHandle.IsValid())
		{
			FTSTicker::RemoveTicker(AutomaticRefreshTickerHandle);
			AutomaticRefreshTickerHandle.Reset();
		}
		PendingAutomaticRefresh.Reset();

		if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
		{
			FPropertyEditorModule& PropertyEditor =
				FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			PropertyEditor.UnregisterCustomPropertyTypeLayout(TEXT("EFClothingGarmentRow"));
			PropertyEditor.UnregisterCustomClassLayout(TEXT("EFClothingMorphDirectorPolicy"));
			PropertyEditor.NotifyCustomizationModuleChanged();
		}
	}

private:
	bool IsConfiguredDirector(const UEFClothingMorphDirectorPolicy* Director) const
	{
		if (!IsValid(Director) || Director->HasAnyFlags(RF_ClassDefaultObject))
		{
			return false;
		}
		const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
		return Settings
			&& Settings->bEnabled
			&& Settings->DirectorPolicy.ToSoftObjectPath()
				== FSoftObjectPath(Director);
	}

	void QueueAutomaticRefresh(UEFClothingMorphDirectorPolicy* Director)
	{
		if (IsRunningCommandlet()
			|| bAutomaticRefreshInProgress
			|| !IsConfiguredDirector(Director))
		{
			return;
		}
		PendingAutomaticRefresh = Director;
		if (AutomaticRefreshTickerHandle.IsValid())
		{
			FTSTicker::RemoveTicker(AutomaticRefreshTickerHandle);
			AutomaticRefreshTickerHandle.Reset();
		}
		// Property editing can emit several nested events while the user assigns a
		// mesh pair. Debounce them so one complete row produces one atomic refresh.
		AutomaticRefreshTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(
				this,
				&FEFClothingMorphEditorModule::RunAutomaticRefresh),
			0.75f);
	}

	void OnObjectPropertyChanged(
		UObject* Object,
		FPropertyChangedEvent& PropertyChangedEvent)
	{
		UEFClothingMorphDirectorPolicy* Director =
			Cast<UEFClothingMorphDirectorPolicy>(Object);
		if (!IsConfiguredDirector(Director))
		{
			return;
		}

		const FName PropertyName = PropertyChangedEvent.GetPropertyName();
		const FName MemberPropertyName = PropertyChangedEvent.GetMemberPropertyName();
		const FName ClothesPropertyName = GET_MEMBER_NAME_CHECKED(
			UEFClothingMorphDirectorPolicy,
			Garments);
		if (PropertyName == ClothesPropertyName
			|| MemberPropertyName == GET_MEMBER_NAME_CHECKED(UEFClothingMorphDirectorPolicy, Bodies)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UEFClothingMorphDirectorPolicy, Bodies))
		{
			QueueAutomaticRefresh(Director);
			return;
		}
		if (MemberPropertyName != ClothesPropertyName)
		{
			return;
		}

		// Runtime-only controls, notes, native edit parameters, and visual body
		// hiding never invalidate surface geometry. Avoid a synchronous catalog
		// fingerprint pass for those frequent edits.
		static const TSet<FName> CompileRelevantClothingProperties = {
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, bEnabled),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, GarmentId),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, SourceGarment),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, BodySurface),
			// V5 metadata must run the debounced publication transaction. The
			// lower-body guard additionally participates in the binding fingerprint.
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, LayerRule),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, CoveredBodyRegionIds),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, MaterialPolicy),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, LowerBodyMorphGuard),
			GET_MEMBER_NAME_CHECKED(FEFClothingLowerBodyMorphGuardRule, ExactBodyMorphNames),
			GET_MEMBER_NAME_CHECKED(FEFClothingLowerBodyMorphGuardRule, MaximumClearanceCm),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, NativeSkinWeightProfile),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, FitPolicy),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, CoverageTags),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, ExcludedBodySurfaceMaterialSlots),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, Backend),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, ExcludedBodyBoneBranches),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, ExcludedBodyMorphPrefixes),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, MinimumClearanceMultiplier),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, FabricClearanceCm),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, MaximumCorrectionCm)
		};
		if (PropertyName.IsNone()
			|| CompileRelevantClothingProperties.Contains(PropertyName))
		{
			QueueAutomaticRefresh(Director);
		}
	}

	void OnPackageSaved(
		const FString& PackagePath,
		UPackage* Package,
		FObjectPostSaveContext SaveContext)
	{
		if (IsRunningCommandlet()
			|| !IsValid(Package)
			|| SaveContext.IsProceduralSave()
			|| SaveContext.IsFromAutoSave())
		{
			return;
		}

		const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
		UEFClothingMorphDirectorPolicy* Director = Settings
			? Settings->DirectorPolicy.LoadSynchronous()
			: nullptr;
		if (!IsConfiguredDirector(Director))
		{
			return;
		}

		if (Director->GetOutermost() == Package)
		{
			QueueAutomaticRefresh(Director);
			return;
		}

		// Native Unreal edits remain authoritative. Saving a clothing or reference
		// body mesh automatically invalidates only affected fingerprints; the gate
		// then rebuilds stale rows while retaining every already-valid binding.
		const FString SavedPackageName = Package->GetName();
		for (const FEFClothingGarmentRow& Clothing : Director->BuildBodyVariants())
		{
			if (Clothing.SourceGarment.ToSoftObjectPath().GetLongPackageName()
					== SavedPackageName
				|| Clothing.BodySurface.ToSoftObjectPath().GetLongPackageName()
					== SavedPackageName)
			{
				QueueAutomaticRefresh(Director);
				return;
			}
		}
	}

	bool RunAutomaticRefresh(float DeltaTime)
	{
		AutomaticRefreshTickerHandle.Reset();
		UEFClothingMorphDirectorPolicy* Director = PendingAutomaticRefresh.Get();
		PendingAutomaticRefresh.Reset();
		if (!IsConfiguredDirector(Director) || bAutomaticRefreshInProgress)
		{
			return false;
		}

		using namespace EFClothingMorphEditorModulePrivate;
		TGuardValue<bool> RefreshGuard(bAutomaticRefreshInProgress, true);
		const FEFClothingV5DirectorPreparationResult Preparation =
			FEFClothingV5EditorBridge::PrepareDirectorDefaults(Director);
		if (Preparation.bChanged)
		{
			const bool bDirectorSaved = SavePreparedDirector(Director, Preparation);
			if (bDirectorSaved)
			{
				UE_LOG(
					LogEFClothingMorphEditor,
					Display,
					TEXT("EF Clothing Morph V5.1 prepared and saved the single Director before binding refresh: layer-rows=%d lower-body-guards=%d."),
					Preparation.MigratedLayerRowCount,
					Preparation.MigratedLowerBodyGuardRowCount);
			}
			else
			{
				UE_LOG(
					LogEFClothingMorphEditor,
					Warning,
					TEXT("EF Clothing Morph V5.1 prepared the single Director before binding refresh but could not save it: layer-rows=%d lower-body-guards=%d."),
					Preparation.MigratedLayerRowCount,
					Preparation.MigratedLowerBodyGuardRowCount);
			}
		}
		const FV5GarmentMaterialPolicyResult MaterialResult =
			ApplyV5GarmentMaterialPolicy(Director);
		if (MaterialResult.ChangedMaterialCount > 0)
		{
			if (MaterialResult.bSaved)
			{
				UE_LOG(
					LogEFClothingMorphEditor,
					Display,
					TEXT("EF Clothing Morph V5.1 applied %d safe clothing material override(s); managed=%d explicit-exceptions=%d."),
					MaterialResult.ChangedMaterialCount,
					MaterialResult.ManagedMaterialCount,
					MaterialResult.ExplicitExceptionCount);
			}
			else
			{
				UE_LOG(
					LogEFClothingMorphEditor,
					Warning,
					TEXT("EF Clothing Morph V5.1 applied %d safe clothing material override(s), but one or more assets could not be saved and remain dirty."),
					MaterialResult.ChangedMaterialCount);
			}
		}

		USkeletalMesh* CompatibilityReference = LoadObject<USkeletalMesh>(
			nullptr,
			CompatibilityReferencePath);
		const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
		UEFClothingFitRegistry* Registry = Settings
			? Settings->Registry.LoadSynchronous()
			: nullptr;
		if (!IsValid(CompatibilityReference))
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Warning,
				TEXT("EF Clothing Morph V4 automatic binding refresh could not load the protected compatibility reference."));
			return false;
		}

		const FEFClothingNativeSourceEditorGateResult Result =
			FEFClothingNativeSourceEditorGate::ValidateOrRefresh(
				Director,
				Registry,
				CompatibilityReference,
				true);
		if (!Result.bSuccess)
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Warning,
				TEXT("EF Clothing Morph V4 automatic binding refresh failed safely; existing ready clothes remain registered: %s"),
				*Result.Report);
		}
		else if (Result.bRefreshed)
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Display,
				TEXT("EF Clothing Morph V4 automatically created or updated stale clothing bindings: %s"),
				*Result.Report);
		}
		else
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Display,
				TEXT("EF Clothing Morph V4 automatic binding check found no stale clothes: %s"),
				*Result.Report);
		}

		if (Result.bSuccess && IsValid(Result.Registry))
		{
			const FEFClothingV5EditorSyncResult V5Result =
				FEFClothingV5EditorBridge::SyncFromV4(
					Director,
					Result.Registry,
					true);
			if (V5Result.bSuccess)
			{
				UE_LOG(LogEFClothingMorphEditor, Display, TEXT("%s"), *V5Result.Report);
			}
			else
			{
				UE_LOG(LogEFClothingMorphEditor, Warning, TEXT("%s"), *V5Result.Report);
			}
		}
		return false;
	}

	FDelegateHandle ObjectPropertyChangedHandle;
	FDelegateHandle PackageSavedHandle;
	FTSTicker::FDelegateHandle AutomaticRefreshTickerHandle;
	TWeakObjectPtr<UEFClothingMorphDirectorPolicy> PendingAutomaticRefresh;
	bool bAutomaticRefreshInProgress = false;

protected:
	virtual TValueOrError<bool, FText> IsPIEAuthorizedInternal(bool bIsSimulateInEditor) const override
	{
		// Silent authorization queries stay non-blocking. Exact validation and any
		// binding refresh happen only after the user has actually requested PIE.
		return MakeValue(true);
	}

	virtual TValueOrError<bool, FText> RequestPIEPermissionInternal(bool bIsSimulateInEditor) const override
	{
		using namespace EFClothingMorphEditorModulePrivate;

		const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
		if (!Settings || !Settings->bEnabled)
		{
			return MakeValue(true);
		}

		// Native Skeletal Mesh edits can leave render/DDC work queued. The V4 gate
		// reads the authoritative source and can publish bindings only, never meshes.
		FAssetCompilingManager::Get().FinishAllCompilation();

		UEFClothingMorphDirectorPolicy* Director = Settings->DirectorPolicy.LoadSynchronous();
		USkeletalMesh* CompatibilityReference = LoadObject<USkeletalMesh>(nullptr, CompatibilityReferencePath);
		if (!IsValid(Director))
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Warning,
				TEXT("EF Clothing Morph V4 could not load the configured Clothing Director. Play is allowed and every clothing mesh will use its untouched Unreal output."));
			return MakeValue(true);
		}
		if (!IsValid(CompatibilityReference))
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Warning,
				TEXT("EF Clothing Morph V4 could not load its protected compatibility reference. Play is allowed in visible passthrough; no protected asset was modified."));
			return MakeValue(true);
		}

		FString PolicyError;
		if (!Director->ValidateIdentity(PolicyError))
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Warning,
				TEXT("EF Clothing Morph V4 Director identity warning: %s. Play is allowed in visible passthrough."),
				*PolicyError);
			return MakeValue(true);
		}

		const FEFClothingV5DirectorPreparationResult Preparation =
			FEFClothingV5EditorBridge::PrepareDirectorDefaults(Director);
		if (Preparation.bChanged && !SavePreparedDirector(Director, Preparation))
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Warning,
				TEXT("EF Clothing Morph V5.1 prepared lower-body guards before PIE binding refresh, but the Director could not be saved and remains dirty."));
		}

		UEFClothingFitRegistry* Registry = Settings->Registry.LoadSynchronous();
		const FV5GarmentMaterialPolicyResult MaterialResult =
			ApplyV5GarmentMaterialPolicy(Director);
		if (!MaterialResult.bSaved)
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Warning,
				TEXT("EF Clothing Morph V5.1 applied its declarative material policy in memory, but one or more changed assets could not be saved."));
		}
		const FEFClothingNativeSourceEditorGateResult GateResult =
			FEFClothingNativeSourceEditorGate::ValidateOrRefresh(
				Director,
				Registry,
				CompatibilityReference,
				true);
		if (!GateResult.bSuccess)
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Warning,
				TEXT("EF Clothing Morph V4 fit-data refresh warning: %s. Play is allowed; only affected clothes use their untouched Unreal deformation."),
				*GateResult.Report);
			return MakeValue(true);
		}

		const FEFClothingV5EditorSyncResult V5Result =
			FEFClothingV5EditorBridge::SyncFromV4(
				Director,
				GateResult.Registry,
				true);
		if (!V5Result.bSuccess)
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Warning,
				TEXT("EF Clothing Morph V5.1 publication warning: %s. PIE remains allowed through the V4 rollback registry."),
				*V5Result.Report);
		}

		if (GateResult.bRefreshed)
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Display,
				TEXT("EF Clothing Morph V4 PIE fit data refreshed: %s"),
				*GateResult.Report);
		}
		else
		{
			UE_LOG(
				LogEFClothingMorphEditor,
				Verbose,
				TEXT("EF Clothing Morph V4 PIE fit data is fresh: %s"),
				*GateResult.Report);
		}
		return MakeValue(true);
	}
};

IMPLEMENT_MODULE(FEFClothingMorphEditorModule, EFClothingMorphEditor)

#undef LOCTEXT_NAMESPACE
