#include "Debug/ProjectGameplayDebugCommandExecutor.h"

#include "Combat/ProjectCombatAttributeComponent.h"
#include "GameFramework/Actor.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Survival/ProjectRealtimeSnapshotComponent.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalStatusComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectGameplayDebugRestoreHealthTest,
	"NoShellForWinter.GameplayDebug.Commands.RestoreHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectGameplayDebugRestoreHealthTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>();
	UProjectCombatAttributeComponent* CombatComponent = NewObject<UProjectCombatAttributeComponent>(Owner);
	UProjectRealtimeSnapshotComponent* SnapshotComponent = NewObject<UProjectRealtimeSnapshotComponent>(Owner);
	TestNotNull(TEXT("Owner should be constructible"), Owner);
	TestNotNull(TEXT("Combat component should be constructible"), CombatComponent);
	TestNotNull(TEXT("Realtime snapshot component should be constructible"), SnapshotComponent);
	if (!Owner || !CombatComponent || !SnapshotComponent)
	{
		return false;
	}

	Owner->AddInstanceComponent(CombatComponent);
	Owner->AddInstanceComponent(SnapshotComponent);

	TestTrue(TEXT("Test setup should lower health"), CombatComponent->SetAttributeCurrentValue(TEXT("Health"), 12.5f));
	TestTrue(TEXT("Debug health restore should apply"), FProjectGameplayDebugCommandExecutor::RestoreAcfHealth(Owner));
	TestTrue(TEXT("Health should be restored to max"),
		FMath::IsNearlyEqual(CombatComponent->GetAttributeCurrentValue(TEXT("Health")), CombatComponent->GetAttributeMaxValue(TEXT("Health")), 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectGameplayDebugRestoreNeedsSensationsTest,
	"NoShellForWinter.GameplayDebug.Commands.RestoreNeedsAndSensations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectGameplayDebugRestoreNeedsSensationsTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>();
	UProjectSurvivalNeedsComponent* NeedsComponent = NewObject<UProjectSurvivalNeedsComponent>(Owner);
	UProjectInnerDoctrineComponent* DoctrineComponent = NewObject<UProjectInnerDoctrineComponent>(Owner);
	TestNotNull(TEXT("Owner should be constructible"), Owner);
	TestNotNull(TEXT("Needs component should be constructible"), NeedsComponent);
	TestNotNull(TEXT("Inner Doctrine component should be constructible"), DoctrineComponent);
	if (!Owner || !NeedsComponent || !DoctrineComponent)
	{
		return false;
	}

	Owner->AddInstanceComponent(NeedsComponent);
	Owner->AddInstanceComponent(DoctrineComponent);

	NeedsComponent->SetNeedCurrentValue(TEXT("Hunger"), 1.f, false);
	NeedsComponent->SetNeedCurrentValue(TEXT("Thirst"), 2.f, false);
	NeedsComponent->SetNeedCurrentValue(TEXT("Sleep"), 3.f, false);
	NeedsComponent->SetSensationCurrentValue(TEXT("Madness"), NeedsComponent->GetSensationMaxValue(TEXT("Madness")), false);
	FProjectCurseApplicationContext CurseContext;
	CurseContext.Amount = DoctrineComponent->GetCurseMax();
	CurseContext.SourceKind = EProjectCurseSourceKind::Debug;
	CurseContext.ApplicationId = FGuid::NewGuid();
	CurseContext.bResistible = false;
	CurseContext.bCanTriggerCursed = false;
	DoctrineComponent->ApplyCurse(CurseContext);
	NeedsComponent->SetSensationCurrentValue(TEXT("Pain"), NeedsComponent->GetSensationMaxValue(TEXT("Pain")), false);

	TestTrue(TEXT("Debug needs/sensations restore should apply"), FProjectGameplayDebugCommandExecutor::RestoreNeedsAndSensations(Owner));
	TestTrue(TEXT("Hunger should be restored to max"),
		FMath::IsNearlyEqual(NeedsComponent->GetNeedCurrentValue(TEXT("Hunger")), NeedsComponent->GetNeedMaxValue(TEXT("Hunger")), 0.0001f));
	TestTrue(TEXT("Thirst should be restored to max"),
		FMath::IsNearlyEqual(NeedsComponent->GetNeedCurrentValue(TEXT("Thirst")), NeedsComponent->GetNeedMaxValue(TEXT("Thirst")), 0.0001f));
	TestTrue(TEXT("Sleep should be restored to max"),
		FMath::IsNearlyEqual(NeedsComponent->GetNeedCurrentValue(TEXT("Sleep")), NeedsComponent->GetNeedMaxValue(TEXT("Sleep")), 0.0001f));
	TestTrue(TEXT("Madness should be cleared"),
		FMath::IsNearlyEqual(NeedsComponent->GetSensationCurrentValue(TEXT("Madness")), 0.f, 0.0001f));
	TestTrue(TEXT("Curse should be cleared"),
		FMath::IsNearlyEqual(DoctrineComponent->GetCurse(), 0.f, 0.0001f));
	TestTrue(TEXT("Pain should be cleared"),
		FMath::IsNearlyEqual(NeedsComponent->GetSensationCurrentValue(TEXT("Pain")), 0.f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectGameplayDebugSetNeedsSensationsPercentTest,
	"NoShellForWinter.GameplayDebug.Commands.SetNeedsSensationsPercent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectGameplayDebugSetNeedsSensationsPercentTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>();
	UProjectSurvivalNeedsComponent* NeedsComponent = NewObject<UProjectSurvivalNeedsComponent>(Owner);
	UProjectInnerDoctrineComponent* DoctrineComponent = NewObject<UProjectInnerDoctrineComponent>(Owner);
	TestNotNull(TEXT("Owner should be constructible"), Owner);
	TestNotNull(TEXT("Needs component should be constructible"), NeedsComponent);
	TestNotNull(TEXT("Inner Doctrine component should be constructible"), DoctrineComponent);
	if (!Owner || !NeedsComponent || !DoctrineComponent)
	{
		return false;
	}

	Owner->AddInstanceComponent(NeedsComponent);
	Owner->AddInstanceComponent(DoctrineComponent);

	NeedsComponent->SetNeedMaxValue(TEXT("Hunger"), 120.f, true);
	NeedsComponent->SetNeedMaxValue(TEXT("Thirst"), 80.f, true);
	NeedsComponent->SetNeedMaxValue(TEXT("Sleep"), 150.f, true);
	NeedsComponent->SetSensationMaxValue(TEXT("Madness"), 90.f, true);
	NeedsComponent->SetSensationMaxValue(TEXT("Pain"), 115.f, true);

	TestTrue(TEXT("Hunger should accept a percent set"),
		FProjectGameplayDebugCommandExecutor::SetNeedOrSensationToPercent(Owner, TEXT("Hunger"), 0.5f));
	TestTrue(TEXT("Thirst should accept a percent set"),
		FProjectGameplayDebugCommandExecutor::SetNeedOrSensationToPercent(Owner, TEXT("Thirst"), 0.5f));
	TestTrue(TEXT("Sleep should accept a percent set"),
		FProjectGameplayDebugCommandExecutor::SetNeedOrSensationToPercent(Owner, TEXT("Sleep"), 0.5f));
	TestTrue(TEXT("Madness should accept a percent set"),
		FProjectGameplayDebugCommandExecutor::SetNeedOrSensationToPercent(Owner, TEXT("Madness"), 0.5f));
	TestTrue(TEXT("Curse should accept a percent set"),
		FProjectGameplayDebugCommandExecutor::SetNeedOrSensationToPercent(Owner, TEXT("Curse"), 0.5f));
	TestTrue(TEXT("Pain should accept a percent set"),
		FProjectGameplayDebugCommandExecutor::SetNeedOrSensationToPercent(Owner, TEXT("Pain"), 0.5f));

	TestTrue(TEXT("Hunger should be half of live max"),
		FMath::IsNearlyEqual(NeedsComponent->GetNeedCurrentValue(TEXT("Hunger")), 60.f, 0.0001f));
	TestTrue(TEXT("Thirst should be half of live max"),
		FMath::IsNearlyEqual(NeedsComponent->GetNeedCurrentValue(TEXT("Thirst")), 40.f, 0.0001f));
	TestTrue(TEXT("Sleep should be half of live max"),
		FMath::IsNearlyEqual(NeedsComponent->GetNeedCurrentValue(TEXT("Sleep")), 75.f, 0.0001f));
	TestTrue(TEXT("Madness should be half of live max"),
		FMath::IsNearlyEqual(NeedsComponent->GetSensationCurrentValue(TEXT("Madness")), 45.f, 0.0001f));
	TestTrue(TEXT("Curse should be half of fixed max"),
		FMath::IsNearlyEqual(DoctrineComponent->GetCurse(), DoctrineComponent->GetCurseMax() * 0.5f, 0.0001f));
	TestTrue(TEXT("Pain should be half of live max"),
		FMath::IsNearlyEqual(NeedsComponent->GetSensationCurrentValue(TEXT("Pain")), 57.5f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectGameplayDebugStatusBypassTest,
	"NoShellForWinter.GameplayDebug.Commands.StatusBypassesImmunity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectGameplayDebugStatusBypassTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>();
	UProjectSurvivalStatusComponent* StatusComponent = NewObject<UProjectSurvivalStatusComponent>(Owner);
	TestNotNull(TEXT("Owner should be constructible"), Owner);
	TestNotNull(TEXT("Status component should be constructible"), StatusComponent);
	if (!Owner || !StatusComponent)
	{
		return false;
	}

	Owner->AddInstanceComponent(StatusComponent);
	StatusComponent->ForceRefresh();
	TArray<FName> ImmuneStatusNames;
	ImmuneStatusNames.Add(TEXT("Fear"));
	StatusComponent->SetStatusImmunitySource(TEXT("Automation"), ImmuneStatusNames);

	TestFalse(TEXT("Gameplay apply should respect immunity"),
		StatusComponent->ApplyStatus(TEXT("Fear"), 5.f, Owner));
	TestFalse(TEXT("Fear should not be active after an immune gameplay apply"),
		StatusComponent->IsStatusActive(TEXT("Fear")));
	TestTrue(TEXT("Debug status apply should bypass immunity"),
		FProjectGameplayDebugCommandExecutor::ForceApplyStatus(Owner, TEXT("Fear")));
	TestTrue(TEXT("Fear should be active through the debug bypass path"),
		StatusComponent->IsStatusActive(TEXT("Fear")));

	StatusComponent->ClearAllDebugStatuses();
	TestFalse(TEXT("Debug clear should remove debug-forced Fear"),
		StatusComponent->IsStatusActive(TEXT("Fear")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectGameplayDebugRaiseAttributeTargetTest,
	"NoShellForWinter.GameplayDebug.Commands.RaiseAttributeTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectGameplayDebugRaiseAttributeTargetTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>();
	UProjectInnerDoctrineComponent* DoctrineComponent = NewObject<UProjectInnerDoctrineComponent>(Owner);
	TestNotNull(TEXT("Owner should be constructible"), Owner);
	TestNotNull(TEXT("Inner Doctrine component should be constructible"), DoctrineComponent);
	if (!Owner || !DoctrineComponent)
	{
		return false;
	}

	Owner->AddInstanceComponent(DoctrineComponent);

	TestTrue(TEXT("Debug raise should reach level 10"),
		FProjectGameplayDebugCommandExecutor::RaiseDoctrineAttributeToLevel(Owner, EProjectDoctrineAttribute::Willpower, 10));
	TestEqual(TEXT("Willpower should be level 10"), DoctrineComponent->GetAttributeLevel(EProjectDoctrineAttribute::Willpower), 10);

	TestTrue(TEXT("Debug raise to a lower target should report success without downgrading"),
		FProjectGameplayDebugCommandExecutor::RaiseDoctrineAttributeToLevel(Owner, EProjectDoctrineAttribute::Willpower, 5));
	TestEqual(TEXT("Willpower should remain level 10"), DoctrineComponent->GetAttributeLevel(EProjectDoctrineAttribute::Willpower), 10);

	TestTrue(TEXT("Debug raise should reach level 5 for another attribute"),
		FProjectGameplayDebugCommandExecutor::RaiseDoctrineAttributeToLevel(Owner, EProjectDoctrineAttribute::Cunning, 5));
	TestEqual(TEXT("Cunning should be level 5"), DoctrineComponent->GetAttributeLevel(EProjectDoctrineAttribute::Cunning), 5);
	return true;
}

#endif
