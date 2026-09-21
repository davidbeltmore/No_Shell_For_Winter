#if WITH_DEV_AUTOMATION_TESTS

#include "EFProceduralPCGSubsystem.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoPCGSynchronousCompletionOrderingV6Test,
	"NoShellForWinter.CalystoDungeon.V6.PCG.SynchronousCompletionReadinessOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoPCGSynchronousCompletionOrderingV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEFProceduralPCGSubsystem::FDungeonRuntimeState State;
	State.bPCGGenerationTriggered = true;
	FString Error;
	TestTrue(
		TEXT("The first and only GenerateLocal request arms successfully"),
		UEFProceduralPCGSubsystem::ArmGenerateLocalRequestV6(State, Error));
	TestTrue(TEXT("Arming does not report an error"), Error.IsEmpty());
	TestEqual(TEXT("The request is consumed before entering PCG"), State.GenerateLocalRequestCount, 1);
	TestEqual(TEXT("GenerateLocal is registered before PCG can callback"), State.ReadinessTrace.Num(), 1);
	if (State.ReadinessTrace.Num() == 1)
	{
		TestEqual(TEXT("The first milestone is GenerateLocal"), State.ReadinessTrace[0], FName(TEXT("GenerateLocal")));
	}

	// This is the critical re-entrant boundary: a generated delegate may run inside
	// GenerateLocalGetTaskId before that function returns its task id.
	TestTrue(
		TEXT("A synchronous completion can immediately append PCGComplete"),
		UEFProceduralPCGSubsystem::RecordReadinessMilestone(State, TEXT("PCGComplete")));
	TestEqual(TEXT("The synchronous callback preserves the exact trace"), State.ReadinessTrace.Num(), 2);

	State.bPCGGenerationFinished = true;
	State.bRoomManifestReady = true;
	State.bPopulationPlanReady = true;
	State.bPostTopologyLoadRequested = true;
	State.bPostTopologyAssetsReady = true;
	State.bVisualsReady = true;
	State.bDecalsRealized = true;
	State.RealizedDecalCount = 4;
	State.bPopulationMaterializationStarted = true;
	State.bPopulationReady = true;
	State.bCompanionRosterReady = true;
	State.bDungeonReady = true;
	State.bFloorReadyNotified = true;
	State.ControlledGenerationTaskId = 42;

	// Model the invalid-task-id branch after a re-entrant completion. Physical world
	// cleanup is performed by FailClosed; this helper is its authoritative state rollback.
	UEFProceduralPCGSubsystem::RollbackGenerationLaunchStateV6(State);
	TestTrue(TEXT("An invalid launch is terminal PCG failure"), State.bPCGGenerationFailed);
	TestFalse(TEXT("The failed launch is not still triggered"), State.bPCGGenerationTriggered);
	TestFalse(TEXT("Synchronous completion is rolled back"), State.bPCGGenerationFinished);
	TestFalse(TEXT("The room manifest is rolled back"), State.bRoomManifestReady);
	TestFalse(TEXT("The population plan is rolled back"), State.bPopulationPlanReady);
	TestFalse(TEXT("Post-topology loading is rolled back"), State.bPostTopologyLoadRequested);
	TestFalse(TEXT("Visual readiness is rolled back"), State.bVisualsReady);
	TestFalse(TEXT("Decal realization is rolled back"), State.bDecalsRealized);
	TestEqual(TEXT("Realized decal count is cleared"), State.RealizedDecalCount, 0);
	TestFalse(TEXT("Population materialization is rolled back"), State.bPopulationMaterializationStarted);
	TestFalse(TEXT("Population readiness is rolled back"), State.bPopulationReady);
	TestFalse(TEXT("Companion readiness is rolled back"), State.bCompanionRosterReady);
	TestFalse(TEXT("The dungeon remains closed"), State.bDungeonReady);
	TestFalse(TEXT("Floor-ready notification is rolled back"), State.bFloorReadyNotified);
	TestTrue(TEXT("The rejected readiness trace is cleared"), State.ReadinessTrace.IsEmpty());
	TestEqual(
		TEXT("The attempted request remains consumed to prevent a silent retry"),
		State.GenerateLocalRequestCount,
		1);
	TestFalse(
		TEXT("A failed launch cannot be armed a second time in the same world"),
		UEFProceduralPCGSubsystem::ArmGenerateLocalRequestV6(State, Error));
	TestTrue(TEXT("The duplicate-arm failure is explicit"), Error.Contains(TEXT("already requested")));

	UEFProceduralPCGSubsystem::FDungeonRuntimeState InvalidOrderState;
	InvalidOrderState.ReadinessTrace.Add(TEXT("PCGComplete"));
	TestFalse(
		TEXT("A corrupted pre-launch trace fails before consuming a request"),
		UEFProceduralPCGSubsystem::ArmGenerateLocalRequestV6(InvalidOrderState, Error));
	TestEqual(
		TEXT("A rejected pre-launch trace does not claim GenerateLocal was issued"),
		InvalidOrderState.GenerateLocalRequestCount,
		0);

	return true;
}

#endif
