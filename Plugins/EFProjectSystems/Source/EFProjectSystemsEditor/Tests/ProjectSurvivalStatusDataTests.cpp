#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
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
