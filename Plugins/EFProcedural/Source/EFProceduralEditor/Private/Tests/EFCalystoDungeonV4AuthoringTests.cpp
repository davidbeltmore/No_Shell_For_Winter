#include "Calysto/EFCalystoDungeonTypesV4.h"
#include "Calysto/EFCalystoTierMixV4Customization.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/UnrealType.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV4TierAuthoringSurfaceTest,
	"NoShellForWinter.CalystoDungeon.V4.Editor.TierAuthoringSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV4TierAuthoringSurfaceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	float Nothing = 0.0f;
	TestTrue(
		TEXT("A tier mix that assigns exactly 0.90 is accepted."),
		FEFCalystoTierMixV4Customization::ValidateAuthoredMass(
			0.35f, 0.28f, 0.18f, 0.09f, Nothing));
	TestTrue(
		TEXT("Exactly 0.10 remains Nothing."),
		FMath::IsNearlyEqual(
			Nothing,
			0.10f,
			FEFCalystoTierMixV4Customization::ValidationTolerance));

	FText Error;
	TestFalse(
		TEXT("A tier mix above 0.90 is rejected rather than normalized."),
		FEFCalystoTierMixV4Customization::ValidateAuthoredMass(
			0.36f, 0.28f, 0.18f, 0.09f, Nothing, &Error));
	TestFalse(TEXT("A rejected edit provides visible feedback."), Error.IsEmpty());

	TestFalse(
		TEXT("NaN is rejected fail-closed."),
		FEFCalystoTierMixV4Customization::ValidateAuthoredMass(
			std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f, Nothing));
	TestFalse(
		TEXT("Negative probability is rejected fail-closed."),
		FEFCalystoTierMixV4Customization::ValidateAuthoredMass(
			-0.01f, 0.0f, 0.0f, 0.0f, Nothing));

	const FProperty* NothingProperty = FEFCalystoTierMixV4::StaticStruct()->FindPropertyByName(
		GET_MEMBER_NAME_CHECKED(FEFCalystoTierMixV4, Nothing));
	TestNotNull(TEXT("Nothing remains a reflected field for clear Data Asset UX."), NothingProperty);
	if (NothingProperty)
	{
		TestTrue(
			TEXT("Nothing is read-only in the authored schema."),
			NothingProperty->HasAnyPropertyFlags(CPF_EditConst));
	}

	FPropertyEditorModule& PropertyEditor =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	const FCustomPropertyTypeLayoutMap EmptyInstanceLayouts;
	TestTrue(
		TEXT("EFProceduralEditor registered the V4 tier mix customization."),
		PropertyEditor.IsCustomizedStruct(
			FEFCalystoTierMixV4::StaticStruct(),
			EmptyInstanceLayouts));

	return true;
}

#endif
