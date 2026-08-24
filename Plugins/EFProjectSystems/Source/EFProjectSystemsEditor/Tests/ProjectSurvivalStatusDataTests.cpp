#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalNeedsSettings.h"
#include "Survival/ProjectSurvivalNeedsWidget.h"
#include "Survival/ProjectSurvivalStatusCatalog.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "Survival/ProjectSurvivalStatusSettings.h"
#include "Survival/ProjectSurvivalStatusTypes.h"
#include "Survival/ProjectSurvivalStatusWidget.h"
#include "UI/ProjectWidgetClassResolver.h"
#include "Engine/DataTable.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectSurvivalStatusDataTableOverrideTest,
	"NoShellForWinter.Survival.Status.DataTableOverridesDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSurvivalStatusDataTableOverrideTest::RunTest(const FString& Parameters)
{
	TArray<FProjectSurvivalStatusDefinition> FallbackDefinitions;
	FProjectSurvivalStatusDefinition BleedingFallback;
	BleedingFallback.StatusName = TEXT("Bleeding");
	BleedingFallback.DisplayName = TEXT("BLEEDING");
	BleedingFallback.DamagePerSecond = 1.f;
	BleedingFallback.DurationSeconds = 5.f;
	BleedingFallback.HudPriority = 10;
	FallbackDefinitions.Add(BleedingFallback);

	UDataTable* StatusTable = NewObject<UDataTable>();
	TestNotNull(TEXT("Status table should be constructible"), StatusTable);
	if (!StatusTable)
	{
		return false;
	}

	StatusTable->RowStruct = FProjectSurvivalStatusTableRow::StaticStruct();

	FProjectSurvivalStatusTableRow BleedingRow;
	BleedingRow.DisplayName = TEXT("BLEEDING CUSTOM");
	BleedingRow.Description = TEXT("Custom bleed description");
	BleedingRow.DamagePerSecond = 3.f;
	BleedingRow.DurationSeconds = 12.f;
	BleedingRow.HudPriority = 900;
	BleedingRow.HudSlotSize = FVector2D(72.f, 64.f);
	BleedingRow.HudIconSize = FVector2D(180.f, 70.f);
	BleedingRow.HudIconSlotOffset = FVector2D(2.f, 5.f);
	BleedingRow.HudNameFontSize = 14;
	BleedingRow.HudDescriptionFontSize = 9;
	BleedingRow.HudMetaFontSize = 11;
	BleedingRow.HudNameTextColor = FLinearColor(0.4f, 0.2f, 0.1f, 1.f);
	BleedingRow.HudDescriptionTextOffset = FVector2D(3.f, -2.f);
	StatusTable->AddRow(TEXT("Bleeding"), BleedingRow);

	FProjectSurvivalStatusTableRow DirtyRow;
	DirtyRow.DisplayName = TEXT("DIRTY");
	DirtyRow.Description = TEXT("New table-only status");
	DirtyRow.MinimalIconName = TEXT("Status.Dirty");
	DirtyRow.HudPriority = 50;
	StatusTable->AddRow(TEXT("Dirty"), DirtyRow);

	const TArray<FProjectSurvivalStatusDefinition> ResolvedDefinitions =
		UProjectSurvivalStatusSettings::BuildDefinitionsFromTable(StatusTable, FallbackDefinitions);

	const FProjectSurvivalStatusDefinition* ResolvedBleeding = ResolvedDefinitions.FindByPredicate([](const FProjectSurvivalStatusDefinition& Definition)
	{
		return Definition.StatusName == TEXT("Bleeding");
	});
	TestNotNull(TEXT("Bleeding should resolve from table override"), ResolvedBleeding);
	if (ResolvedBleeding)
	{
		TestEqual(TEXT("Bleeding display name should come from table"), ResolvedBleeding->DisplayName, FString(TEXT("BLEEDING CUSTOM")));
		TestEqual(TEXT("Bleeding description should come from table"), ResolvedBleeding->Description, FString(TEXT("Custom bleed description")));
		TestTrue(TEXT("Bleeding damage should come from table"), FMath::IsNearlyEqual(ResolvedBleeding->DamagePerSecond, 3.f));
		TestTrue(TEXT("Bleeding duration should come from table"), FMath::IsNearlyEqual(ResolvedBleeding->DurationSeconds, 12.f));
		TestEqual(TEXT("Bleeding priority should come from table"), ResolvedBleeding->HudPriority, 900);
		TestTrue(TEXT("Bleeding slot size should come from table"), ResolvedBleeding->HudSlotSize.Equals(FVector2D(72.f, 64.f)));
		TestTrue(TEXT("Bleeding icon size should allow arbitrary table values"), ResolvedBleeding->HudIconSize.Equals(FVector2D(180.f, 70.f)));
		TestTrue(TEXT("Bleeding icon slot offset should come from table"), ResolvedBleeding->HudIconSlotOffset.Equals(FVector2D(2.f, 5.f)));
		TestEqual(TEXT("Bleeding name font size should come from table"), ResolvedBleeding->HudNameFontSize, 14);
		TestEqual(TEXT("Bleeding description font size should come from table"), ResolvedBleeding->HudDescriptionFontSize, 9);
		TestEqual(TEXT("Bleeding meta font size should come from table"), ResolvedBleeding->HudMetaFontSize, 11);
		TestTrue(TEXT("Bleeding name color should come from table"), ResolvedBleeding->HudNameTextColor.Equals(FLinearColor(0.4f, 0.2f, 0.1f, 1.f)));
		TestTrue(TEXT("Bleeding description text offset should come from table"), ResolvedBleeding->HudDescriptionTextOffset.Equals(FVector2D(3.f, -2.f)));
	}

	const FProjectSurvivalStatusDefinition* ResolvedDirty = ResolvedDefinitions.FindByPredicate([](const FProjectSurvivalStatusDefinition& Definition)
	{
		return Definition.StatusName == TEXT("Dirty");
	});
	TestNotNull(TEXT("Table-only Dirty status should be appended"), ResolvedDirty);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectSurvivalStatusDataTableFallbackTest,
	"NoShellForWinter.Survival.Status.MissingDataTableUsesFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSurvivalStatusDataTableFallbackTest::RunTest(const FString& Parameters)
{
	FProjectSurvivalStatusDefinition FearFallback;
	FearFallback.StatusName = TEXT("Fear");
	FearFallback.DisplayName = TEXT("FEAR");
	FearFallback.DurationSeconds = 5.f;

	TArray<FProjectSurvivalStatusDefinition> FallbackDefinitions;
	FallbackDefinitions.Add(FearFallback);

	const TArray<FProjectSurvivalStatusDefinition> ResolvedDefinitions =
		UProjectSurvivalStatusSettings::BuildDefinitionsFromTable(nullptr, FallbackDefinitions);

	TestEqual(TEXT("Fallback count should be preserved without table"), ResolvedDefinitions.Num(), 1);
	TestEqual(TEXT("Fallback status name should be preserved"), ResolvedDefinitions[0].StatusName, FName(TEXT("Fear")));
	TestEqual(TEXT("Fallback display name should be preserved"), ResolvedDefinitions[0].DisplayName, FString(TEXT("FEAR")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectSurvivalStatusVisibleTopFiveTest,
	"NoShellForWinter.Survival.Status.VisibleTopFiveAndOverflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSurvivalStatusVisibleTopFiveTest::RunTest(const FString& Parameters)
{
	TArray<FProjectSurvivalStatusSnapshot> ActiveSnapshots;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		FProjectSurvivalStatusSnapshot Snapshot;
		Snapshot.StatusName = FName(*FString::Printf(TEXT("Status%d"), Index));
		Snapshot.DisplayName = Snapshot.StatusName.ToString();
		Snapshot.HudPriority = Index;
		ActiveSnapshots.Add(Snapshot);
	}

	int32 OverflowCount = 0;
	const TArray<FProjectSurvivalStatusSnapshot> VisibleSnapshots =
		UProjectSurvivalStatusComponent::SelectVisibleStatusSnapshotsForHud(ActiveSnapshots, 5, OverflowCount);

	TestEqual(TEXT("Exactly five statuses should be visible"), VisibleSnapshots.Num(), 5);
	TestEqual(TEXT("Overflow should report two hidden statuses"), OverflowCount, 2);
	TestEqual(TEXT("Highest priority should appear first"), VisibleSnapshots[0].StatusName, FName(TEXT("Status6")));
	TestEqual(TEXT("Fifth visible status should be priority two"), VisibleSnapshots[4].StatusName, FName(TEXT("Status2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectSurvivalStatusInvertedInputMagnitudeTest,
	"NoShellForWinter.Survival.Status.InvertedInputKeepsMagnitude",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSurvivalStatusInvertedInputMagnitudeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(
		TEXT("Forward input is inverted without applying the status speed scale a second time."),
		FMath::IsNearlyEqual(
			UProjectSurvivalStatusComponent::AutomationResolveInvertedMovementInput(1.f),
			-1.f));
	TestTrue(
		TEXT("Partial analog input keeps its magnitude when inverted."),
		FMath::IsNearlyEqual(
			UProjectSurvivalStatusComponent::AutomationResolveInvertedMovementInput(-0.35f),
			0.35f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectSurvivalThresholdHysteresisTest,
	"NoShellForWinter.Survival.Status.ThresholdHysteresis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSurvivalThresholdHysteresisTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestFalse(TEXT("WellFed stays inactive below activation"), UProjectSurvivalStatusComponent::AutomationEvaluateThresholdStatus(false, 0.89f, EProjectSurvivalStatusThresholdMode::AtOrAbove, 0.90f, 0.75f));
	TestTrue(TEXT("WellFed activates at ninety percent"), UProjectSurvivalStatusComponent::AutomationEvaluateThresholdStatus(false, 0.90f, EProjectSurvivalStatusThresholdMode::AtOrAbove, 0.90f, 0.75f));
	TestTrue(TEXT("WellFed remains active inside hysteresis band"), UProjectSurvivalStatusComponent::AutomationEvaluateThresholdStatus(true, 0.80f, EProjectSurvivalStatusThresholdMode::AtOrAbove, 0.90f, 0.75f));
	TestFalse(TEXT("WellFed clears at seventy-five percent"), UProjectSurvivalStatusComponent::AutomationEvaluateThresholdStatus(true, 0.75f, EProjectSurvivalStatusThresholdMode::AtOrAbove, 0.90f, 0.75f));
	TestFalse(TEXT("Alcoholized stays inactive below twenty-five percent"), UProjectSurvivalStatusComponent::AutomationEvaluateThresholdStatus(false, 0.24f, EProjectSurvivalStatusThresholdMode::AtOrAbove, 0.25f, 0.10f));
	TestTrue(TEXT("Alcoholized activates at twenty-five percent"), UProjectSurvivalStatusComponent::AutomationEvaluateThresholdStatus(false, 0.25f, EProjectSurvivalStatusThresholdMode::AtOrAbove, 0.25f, 0.10f));
	TestTrue(TEXT("Alcoholized remains active while metabolizing through the hysteresis band"), UProjectSurvivalStatusComponent::AutomationEvaluateThresholdStatus(true, 0.15f, EProjectSurvivalStatusThresholdMode::AtOrAbove, 0.25f, 0.10f));
	TestFalse(TEXT("Alcoholized clears at ten percent"), UProjectSurvivalStatusComponent::AutomationEvaluateThresholdStatus(true, 0.10f, EProjectSurvivalStatusThresholdMode::AtOrAbove, 0.25f, 0.10f));
	TestTrue(TEXT("Below-threshold mode remains available for future status definitions"), UProjectSurvivalStatusComponent::AutomationEvaluateThresholdStatus(false, 0.10f, EProjectSurvivalStatusThresholdMode::AtOrBelow, 0.10f, 0.25f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectSurvivalNutritionAlcoholDefaultsTest,
	"NoShellForWinter.Survival.Status.NutritionAlcoholDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSurvivalNutritionAlcoholDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UProjectSurvivalNeedsSettings* NeedsSettings = UProjectSurvivalNeedsSettings::Get();
	TestNotNull(TEXT("Needs settings should exist"), NeedsSettings);
	if (!NeedsSettings)
	{
		return false;
	}

	const FProjectSurvivalSensationState* Alcohol = NeedsSettings->DefaultSensations.FindByPredicate([](const FProjectSurvivalSensationState& Sensation)
	{
		return Sensation.SensationName == TEXT("Alcohol");
	});
	TestNotNull(TEXT("Alcohol should be a default sensation"), Alcohol);
	if (Alcohol)
	{
		TestTrue(TEXT("Alcohol should start empty"), FMath::IsNearlyZero(Alcohol->CurrentValue));
		TestTrue(TEXT("Alcohol should use a one hundred point range"), FMath::IsNearlyEqual(Alcohol->MaxValue, 100.f));
		TestTrue(TEXT("Alcohol should metabolize at 0.25 points per second"), FMath::IsNearlyEqual(Alcohol->PassiveDeltaPerSecond, -0.25f));
	}
	TestTrue(TEXT("Alcohol should be hidden from the Needs HUD"), NeedsSettings->HiddenHudEntryNames.Contains(TEXT("Alcohol")));
	TestTrue(TEXT("Needs widget filter should hide Alcohol"), UProjectSurvivalNeedsWidget::IsEntryHiddenFromHud(TEXT("Alcohol")));
	TestFalse(TEXT("Needs widget filter should keep Hunger visible"), UProjectSurvivalNeedsWidget::IsEntryHiddenFromHud(TEXT("Hunger")));

	const FProjectSurvivalStatusCatalog& Catalog = GetProjectSurvivalStatusCatalog();
	const FProjectSurvivalStatusDefinition* WellFed = Catalog.StatusDefinitions.FindByPredicate([](const FProjectSurvivalStatusDefinition& Definition)
	{
		return Definition.StatusName == TEXT("WellFed");
	});
	const FProjectSurvivalStatusDefinition* Alcoholized = Catalog.StatusDefinitions.FindByPredicate([](const FProjectSurvivalStatusDefinition& Definition)
	{
		return Definition.StatusName == TEXT("Alcoholized");
	});
	const FProjectSurvivalStatusDefinition* Starving = Catalog.StatusDefinitions.FindByPredicate([](const FProjectSurvivalStatusDefinition& Definition)
	{
		return Definition.StatusName == TEXT("Starving");
	});
	TestNotNull(TEXT("WellFed should exist"), WellFed);
	TestNotNull(TEXT("Alcoholized should exist"), Alcoholized);
	TestNotNull(TEXT("Legacy Starving should remain"), Starving);
	if (WellFed)
	{
		TestEqual(TEXT("WellFed should use Hunger"), WellFed->SourceEntryName, FName(TEXT("Hunger")));
		TestEqual(TEXT("WellFed should be cosmetic"), WellFed->MovementInputScale, 1.f);
		TestEqual(TEXT("WellFed should not alter attributes"), WellFed->AttributeModifiers.Num(), 0);
		TestEqual(TEXT("WellFed should not alter need decay"), WellFed->NeedDecayModifiers.Num(), 0);
	}
	if (Alcoholized)
	{
		TestEqual(TEXT("Alcoholized should use Alcohol"), Alcoholized->SourceEntryName, FName(TEXT("Alcohol")));
		TestTrue(TEXT("Alcoholized should reduce movement by fifteen percent"), FMath::IsNearlyEqual(Alcoholized->MovementInputScale, 0.85f));
		TestFalse(TEXT("Alcoholized should not invert movement"), Alcoholized->bInvertMovementInput);
	}
	if (Starving)
	{
		TestTrue(TEXT("Legacy empty-need activation remains enabled"), Starving->bTriggerAtNeedEmpty);
		TestEqual(TEXT("Legacy Starving source remains Hunger"), Starving->SourceNeedName, FName(TEXT("Hunger")));
	}

	TestTrue(
		TEXT("Passive metabolism should remove ten Alcohol points in forty seconds"),
		FMath::IsNearlyEqual(UProjectSurvivalNeedsComponent::AutomationIntegrateSensationValue(30.f, 100.f, -0.25f, 40.f), 20.f));
	TestTrue(
		TEXT("Passive metabolism should clamp Alcohol at zero"),
		FMath::IsNearlyZero(UProjectSurvivalNeedsComponent::AutomationIntegrateSensationValue(2.f, 100.f, -0.25f, 40.f)));
	TestTrue(
		TEXT("Alcohol stacking should clamp at one hundred"),
		FMath::IsNearlyEqual(UProjectSurvivalNeedsComponent::AutomationIntegrateSensationValue(90.f, 100.f, 30.f, 1.f), 100.f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectSurvivalStatusWidgetResolverFallbackTest,
	"NoShellForWinter.Survival.Status.WidgetResolverFallsBackToNative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSurvivalStatusWidgetResolverFallbackTest::RunTest(const FString& Parameters)
{
	AddExpectedErrorPlain(
		TEXT("Class /Script/Engine.Actor is not a child class of Class /Script/UMG.UserWidget"),
		EAutomationExpectedErrorFlags::Contains,
		1);

	UClass* ResolvedClass = ProjectWidgetClassResolver::ResolveWidgetClass(
		FSoftClassPath(TEXT("/Script/Engine.Actor")),
		UProjectSurvivalStatusWidget::StaticClass(),
		TEXT("ProjectSurvivalStatusWidget"));

	TestNotNull(TEXT("Resolver should always return a class"), ResolvedClass);
	TestTrue(TEXT("Resolver fallback should derive from status widget"), ResolvedClass && ResolvedClass->IsChildOf(UProjectSurvivalStatusWidget::StaticClass()));
	return true;
}

#endif
