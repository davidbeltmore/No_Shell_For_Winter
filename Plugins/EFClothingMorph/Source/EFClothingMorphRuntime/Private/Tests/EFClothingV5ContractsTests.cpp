#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

#include "Misc/AutomationTest.h"

#include "EFClothingBodyProfile.h"
#include "EFClothingDefinition.h"
#include "EFClothingFitProfile.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingMorphSettings.h"
#include "EFClothingSurfaceBinding.h"
#include "EFClothingSystemManifest.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFClothingV51ContractsTest,
	"EF.ClothingMorph.V51.Contracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFClothingV51ContractsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UEFClothingMorphSettings* Settings = GetDefault<UEFClothingMorphSettings>();
	TestNotNull(TEXT("V5.1 settings exist"), Settings);
	if (!Settings)
	{
		return false;
	}
	TestEqual(
		TEXT("Director remains the one stable public authoring table"),
		Settings->Director.ToSoftObjectPath(),
		FSoftObjectPath(TEXT("/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector.DA_EFClothingMorphDirector")));
	TestEqual(
		TEXT("Generated manifest is internal plugin content"),
		Settings->V5SystemManifest.ToSoftObjectPath(),
		FSoftObjectPath(TEXT("/EFClothingMorph/_Internal/Compiled/V5/DA_EFClothingSystemManifest.DA_EFClothingSystemManifest")));
	TestEqual(
		TEXT("Generated registry remains internal plugin content"),
		Settings->V5Registry.ToSoftObjectPath(),
		FSoftObjectPath(TEXT("/EFClothingMorph/_Internal/Compiled/V5/DA_EFClothingFitRegistry.DA_EFClothingFitRegistry")));

	const FProperty* DirectorSetting = FindFProperty<FProperty>(
		UEFClothingMorphSettings::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UEFClothingMorphSettings, Director));
	const FProperty* ManifestSetting = FindFProperty<FProperty>(
		UEFClothingMorphSettings::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UEFClothingMorphSettings, V5SystemManifest));
	const FProperty* RegistrySetting = FindFProperty<FProperty>(
		UEFClothingMorphSettings::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UEFClothingMorphSettings, V5Registry));
	TestTrue(TEXT("Director setting is internal"), DirectorSetting && !DirectorSetting->HasAnyPropertyFlags(CPF_Edit));
	TestTrue(TEXT("Manifest setting is internal"), ManifestSetting && !ManifestSetting->HasAnyPropertyFlags(CPF_Edit));
	TestTrue(TEXT("Registry setting is internal"), RegistrySetting && !RegistrySetting->HasAnyPropertyFlags(CPF_Edit));

	TestTrue(
		TEXT("The single Director collection keeps a table-row schema"),
		FEFClothingGarmentRow::StaticStruct()->IsChildOf(FTableRowBase::StaticStruct()));
	UEFClothingMorphDirectorPolicy* Director = NewObject<UEFClothingMorphDirectorPolicy>();
	TestEqual(TEXT("Stable Director schema is preserved"), Director->SchemaVersion, 5);
	TestEqual(TEXT("Stable Director identity is preserved"), Director->DirectorId, FName(TEXT("EFClothingMorphV4")));
	const FEFClothingGarmentRow DefaultRow;
	TestEqual(
		TEXT("Opaque is automatic for every new single-table row"),
		DefaultRow.MaterialPolicy.DefaultPolicy,
		EEFClothingMaterialPolicy::ForceOpaque);
	TestFalse(
		TEXT("Lower-body guard is opt-in for ordinary new rows"),
		DefaultRow.LowerBodyMorphGuard.bEnabled);
	TestEqual(
		TEXT("Lower-body guard keeps the reviewed 0.35 cm authoring default"),
		DefaultRow.LowerBodyMorphGuard.MaximumClearanceCm,
		0.35f);
	TestNotNull(
		TEXT("The single Director row exposes the lower-body guard"),
		FindFProperty<FProperty>(
			FEFClothingGarmentRow::StaticStruct(),
			GET_MEMBER_NAME_CHECKED(FEFClothingGarmentRow, LowerBodyMorphGuard)));

	FEFClothingLowerBodyMorphGuardRule LowerGuard;
	LowerGuard.bEnabled = true;
	LowerGuard.ExactBodyMorphNames = {TEXT("Body Voluptuous")};
	LowerGuard.MaximumClearanceCm = 0.35f;
	FString LowerGuardError;
	TestTrue(TEXT("Explicit Body Voluptuous lower guard validates"), LowerGuard.Validate(LowerGuardError));

	TMap<FName, float> BodyMorphWeights;
	TestEqual(TEXT("Missing Body Voluptuous is inactive"), LowerGuard.EvaluateActivity(BodyMorphWeights), 0.0f);
	BodyMorphWeights.Add(TEXT("Body Voluptuous"), 1.0e-5f);
	TestEqual(TEXT("Body Voluptuous below the activity epsilon is inert"), LowerGuard.EvaluateActivity(BodyMorphWeights), 0.0f);
	BodyMorphWeights[TEXT("Body Voluptuous")] = 1.0e-4f;
	TestEqual(TEXT("Body Voluptuous at the activity epsilon is inert"), LowerGuard.EvaluateActivity(BodyMorphWeights), 0.0f);
	BodyMorphWeights.Add(TEXT("Body Voluptuous"), 0.5f);
	TestEqual(TEXT("Body Voluptuous 0.5 preserves proportional activity"), LowerGuard.EvaluateActivity(BodyMorphWeights), 0.5f);
	BodyMorphWeights[TEXT("Body Voluptuous")] = 2.0f;
	TestEqual(TEXT("Body Voluptuous activity clamps at one"), LowerGuard.EvaluateActivity(BodyMorphWeights), 1.0f);
	FEFClothingLowerBodyMorphGuardRule MultiMorphGuard = LowerGuard;
	MultiMorphGuard.ExactBodyMorphNames.Add(TEXT("Body Pear Figure"));
	BodyMorphWeights[TEXT("Body Voluptuous")] = 0.25f;
	BodyMorphWeights.Add(TEXT("Body Pear Figure"), 0.75f);
	TestEqual(TEXT("The strongest positive exact morph wins"), MultiMorphGuard.EvaluateActivity(BodyMorphWeights), 0.75f);
	BodyMorphWeights.Remove(TEXT("Body Pear Figure"));
	BodyMorphWeights[TEXT("Body Voluptuous")] = -0.5f;
	TestEqual(TEXT("Negative Body Voluptuous is inert"), LowerGuard.EvaluateActivity(BodyMorphWeights), 0.0f);
	BodyMorphWeights[TEXT("Body Voluptuous")] = std::numeric_limits<float>::quiet_NaN();
	TestEqual(TEXT("NaN Body Voluptuous is inert"), LowerGuard.EvaluateActivity(BodyMorphWeights), 0.0f);
	BodyMorphWeights.Reset();
	BodyMorphWeights.Add(TEXT("Breasts Large"), 1.0f);
	TestEqual(TEXT("An upper-body morph cannot activate the lower guard"), LowerGuard.EvaluateActivity(BodyMorphWeights), 0.0f);
	LowerGuard.bEnabled = false;
	BodyMorphWeights.Add(TEXT("Body Voluptuous"), 1.0f);
	TestEqual(TEXT("A disabled lower guard remains inert"), LowerGuard.EvaluateActivity(BodyMorphWeights), 0.0f);

	FEFClothingLowerBodyMorphGuardRule DuplicateGuard;
	DuplicateGuard.bEnabled = true;
	DuplicateGuard.ExactBodyMorphNames = {TEXT("Body Voluptuous"), TEXT("Body Voluptuous")};
	TestFalse(TEXT("Duplicate exact lower-body morph names are rejected"), DuplicateGuard.Validate(LowerGuardError));
	FEFClothingLowerBodyMorphGuardRule ExcessiveGuard;
	ExcessiveGuard.bEnabled = true;
	ExcessiveGuard.ExactBodyMorphNames = {TEXT("Body Voluptuous")};
	ExcessiveGuard.MaximumClearanceCm = 0.36f;
	TestFalse(TEXT("Lower-body guard clearance above 0.35 cm is rejected"), ExcessiveGuard.Validate(LowerGuardError));
	ExcessiveGuard.MaximumClearanceCm = 0.0f;
	TestFalse(TEXT("An enabled lower-body guard needs positive clearance"), ExcessiveGuard.Validate(LowerGuardError));

	FEFClothingGarmentRow FingerprintA;
	FingerprintA.LowerBodyMorphGuard.bEnabled = true;
	FingerprintA.LowerBodyMorphGuard.ExactBodyMorphNames = {TEXT("Body Voluptuous"), TEXT("Body Pear Figure")};
	FEFClothingGarmentRow FingerprintB = FingerprintA;
	FingerprintB.LowerBodyMorphGuard.ExactBodyMorphNames = {TEXT("Body Pear Figure"), TEXT("Body Voluptuous")};
	TestEqual(
		TEXT("Lower-body guard fingerprint canonicalizes exact morph-name order"),
		FingerprintA.BuildCompileFingerprint(),
		FingerprintB.BuildCompileFingerprint());
	FingerprintB.LowerBodyMorphGuard.MaximumClearanceCm = 0.20f;
	TestNotEqual(
		TEXT("Lower-body guard clearance participates in the compile fingerprint"),
		FingerprintA.BuildCompileFingerprint(),
		FingerprintB.BuildCompileFingerprint());

	UEFClothingBodyProfile* BodyProfile = NewObject<UEFClothingBodyProfile>();
	BodyProfile->BodyProfileId = TEXT("Female");
	BodyProfile->BodySurface = TSoftObjectPtr<USkeletalMesh>(
		FSoftObjectPath(TEXT("/Game/DazToUnreal/Female/Female.Female")));
	FString ValidationError;
	TestTrue(TEXT("Minimal body profile is valid"), BodyProfile->ValidateProfile(ValidationError));
	TestTrue(TEXT("Body profile error remains empty"), ValidationError.IsEmpty());

	UEFClothingDefinition* Definition = NewObject<UEFClothingDefinition>();
	TestEqual(
		TEXT("Generated clothing definition uses the lower-guard schema"),
		Definition->SchemaVersion,
		2);
	Definition->GarmentId = TEXT("RagShirt");
	Definition->SourceGarment = TSoftObjectPtr<USkeletalMesh>(
		FSoftObjectPath(TEXT("/Game/DazToUnreal/RagShirt/RagShirt.RagShirt")));
	Definition->BodyProfile = TSoftObjectPtr<UEFClothingBodyProfile>(
		FSoftObjectPath(TEXT("/EFClothingMorph/_Internal/Compiled/V5/BodyProfiles/DA_EFBodyProfile_Female.DA_EFBodyProfile_Female")));
	TestEqual(
		TEXT("Opaque is the generated V5.1 mirror default"),
		Definition->MaterialPolicy.DefaultPolicy,
		EEFClothingMaterialPolicy::ForceOpaque);
	TestTrue(TEXT("Minimal garment definition is valid"), Definition->ValidateDefinition(ValidationError));
	Definition->LowerBodyMorphGuard.bEnabled = true;
	Definition->LowerBodyMorphGuard.ExactBodyMorphNames = {TEXT("Body Voluptuous")};
	Definition->LowerBodyMorphGuard.MaximumClearanceCm = 0.20f;
	TestTrue(TEXT("Generated definition accepts a valid lower-body guard mirror"), Definition->ValidateDefinition(ValidationError));
	Definition->LowerBodyMorphGuard.ExactBodyMorphNames.Add(TEXT("Body Voluptuous"));
	TestFalse(TEXT("Generated definition rejects an invalid lower-body guard mirror"), Definition->ValidateDefinition(ValidationError));
	Definition->LowerBodyMorphGuard = FEFClothingLowerBodyMorphGuardRule();

	UEFClothingFitRegistry* Registry = NewObject<UEFClothingFitRegistry>();
	FEFClothingV5StreamableBinding Record;
	Record.StableBindingId = TEXT("RagShirt_Female_LOD0");
	Record.GarmentId = TEXT("RagShirt");
	Record.SourceGarment = Definition->SourceGarment;
	Record.BodySurface = BodyProfile->BodySurface;
	Record.Binding = TSoftObjectPtr<UEFClothingSurfaceBinding>(
		FSoftObjectPath(TEXT("/EFClothingMorph/_Internal/Compiled/V4/DA_RagShirt.DA_RagShirt")));
	Record.LODIndex = 0;
	Record.ContentHash = TEXT("0123456789ABCDEF");
	Record.SchemaVersion = EFClothingMorphV4::SurfaceBindingSchemaVersion;
	Registry->V5StreamableBindings.Add(Record);
	Registry->RebuildV5StreamableBindingIndex();
	const FEFClothingV5StreamableBinding* Found = Registry->FindV5StreamableBinding(
		Record.GarmentId,
		Record.SourceGarment.ToSoftObjectPath(),
		Record.BodySurface.ToSoftObjectPath(),
		0);
	TestNotNull(TEXT("Exact streamable binding lookup succeeds without loading payload"), Found);
	TestTrue(TEXT("Soft binding payload stays unresolved"), Record.Binding.Get() == nullptr);

	UEFClothingSystemManifest* Manifest = NewObject<UEFClothingSystemManifest>();
	Manifest->BindingRegistry = TSoftObjectPtr<UEFClothingFitRegistry>(
		FSoftObjectPath(TEXT("/EFClothingMorph/_Internal/Compiled/V5/DA_EFClothingFitRegistry.DA_EFClothingFitRegistry")));
	Manifest->BodyProfiles.Add(TSoftObjectPtr<UEFClothingBodyProfile>(
		FSoftObjectPath(TEXT("/EFClothingMorph/_Internal/Compiled/V5/BodyProfiles/DA_EFBodyProfile_Female.DA_EFBodyProfile_Female"))));
	Manifest->Garments.Add(TSoftObjectPtr<UEFClothingDefinition>(
		FSoftObjectPath(TEXT("/EFClothingMorph/_Internal/Compiled/V5/Garments/DA_EFGarment_RagShirt.DA_EFGarment_RagShirt"))));
	TestTrue(TEXT("Internal streamable V5.1 manifest validates"), Manifest->Validate(ValidationError));

	return true;
}

#endif
