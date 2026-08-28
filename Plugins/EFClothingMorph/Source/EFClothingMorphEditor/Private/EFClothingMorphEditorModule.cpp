#include "EFClothingGarmentRowCustomization.h"
#include "EFClothingMorphDirectorDetails.h"

#include "AssetCompilingManager.h"
#include "EFClothingFitProfile.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingMorphV2Settings.h"
#include "EFClothingNativeSourceEditorGate.h"
#include "Engine/SkeletalMesh.h"
#include "Features/IModularFeatures.h"
#include "IPIEAuthorizer.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "FEFClothingMorphEditorModule"

DEFINE_LOG_CATEGORY_STATIC(LogEFClothingMorphEditor, Log, All);

namespace EFClothingMorphEditorModulePrivate
{
	constexpr TCHAR CompatibilityReferencePath[] = TEXT("/Game/DazToUnreal/Multiple/Multiple.Multiple");
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

		IModularFeatures::Get().RegisterModularFeature(IPIEAuthorizer::GetModularFeatureName(), this);
	}

	virtual void ShutdownModule() override
	{
		IModularFeatures::Get().UnregisterModularFeature(IPIEAuthorizer::GetModularFeatureName(), this);

		if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
		{
			FPropertyEditorModule& PropertyEditor =
				FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			PropertyEditor.UnregisterCustomPropertyTypeLayout(TEXT("EFClothingGarmentRow"));
			PropertyEditor.UnregisterCustomClassLayout(TEXT("EFClothingMorphDirectorPolicy"));
			PropertyEditor.NotifyCustomizationModuleChanged();
		}
	}

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

		UEFClothingFitRegistry* Registry = Settings->Registry.LoadSynchronous();
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
