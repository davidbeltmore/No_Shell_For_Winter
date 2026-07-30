#include "ACFTrainingSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectTrainingNeutralRequirementsTest,
	"NoShellForWinter.Training.Requirements.NeutralCurseGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectTrainingNeutralRequirementsTest::RunTest(const FString& Parameters)
{
	const FACFTrainingFutureRequirements Requirements = UACFTrainingSettings::MakeDefaultFutureRequirements();

	TestFalse(TEXT("Future scalar requirements should remain opt-in"), Requirements.bEnableScalarRequirements);
	TestEqual(TEXT("The neutral default gate set should contain six resources"), Requirements.ScalarRequirements.Num(), 6);

	const TSet<FName> ExpectedResources = {
		TEXT("Hunger"),
		TEXT("Thirst"),
		TEXT("Sleep"),
		TEXT("Curse"),
		TEXT("Madness"),
		TEXT("Pain")
	};

	TSet<FName> ActualResources;
	const FACFTrainingScalarGate* CurseGate = nullptr;
	for (const FACFTrainingScalarGate& Gate : Requirements.ScalarRequirements)
	{
		ActualResources.Add(Gate.ResourceName);
		if (Gate.ResourceName == TEXT("Curse"))
		{
			CurseGate = &Gate;
		}
	}

	bool bContainsExpectedResources = true;
	for (const FName ExpectedResource : ExpectedResources)
	{
		bContainsExpectedResources &= ActualResources.Contains(ExpectedResource);
	}
	TestTrue(TEXT("Default scalar resources should use the neutral contract"), bContainsExpectedResources);
	TestEqual(TEXT("Default scalar resources should not contain unexpected entries"), ActualResources.Num(), ExpectedResources.Num());
	if (TestNotNull(TEXT("Curse should have a default scalar gate"), CurseGate))
	{
		TestFalse(TEXT("Curse gate should not require a minimum"), CurseGate->bUseMinimumValue);
		TestTrue(TEXT("Curse gate should enforce the warning threshold as a maximum"), CurseGate->bUseMaximumValue);
		TestEqual(TEXT("Curse gate should use the 0-100 scale"), CurseGate->MaximumValue, 50.0f);
	}

	return true;
}

#endif
