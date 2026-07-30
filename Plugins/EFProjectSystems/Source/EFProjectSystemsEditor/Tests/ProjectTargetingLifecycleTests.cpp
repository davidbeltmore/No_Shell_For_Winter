#include "Characters/ProjectTargetingFixComponent.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectTargetRestoreDetachedPawnTest,
	"NoShellForWinter.ProjectSystems.Targeting.RestoreRejectsDetachedPawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectTargetRestoreDetachedPawnTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	APawn* DetachedPawn = NewObject<APawn>(GetTransientPackage());
	AActor* TargetActor = NewObject<AActor>(GetTransientPackage());
	UProjectTargetingFixComponent* TargetingFix = NewObject<UProjectTargetingFixComponent>(DetachedPawn);

	TestNotNull(TEXT("Detached pawn was created"), DetachedPawn);
	TestNotNull(TEXT("Target actor was created"), TargetActor);
	TestNotNull(TEXT("Project targeting bridge was created"), TargetingFix);
	if (!DetachedPawn || !TargetActor || !TargetingFix)
	{
		return false;
	}

	TestFalse(
		TEXT("Target restore must be rejected after the owner pawn loses possession"),
		TargetingFix->RestoreCurrentTargetActor(TargetActor));
	return true;
}
