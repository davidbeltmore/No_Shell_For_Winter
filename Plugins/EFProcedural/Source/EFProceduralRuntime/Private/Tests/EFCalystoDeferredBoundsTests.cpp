#if WITH_DEV_AUTOMATION_TESTS
#include "EFCalystoDeferredBoundsTestActor.h"
#include "Calysto/EFCalystoContentCollisionContract.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoRuntimeDeferredBoundsTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentRealization.RuntimeDeferredBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoRuntimeDeferredBoundsTest::RunTest(const FString&)
{
	auto* CDO = GetMutableDefault<AEFCalystoDeferredBoundsTestActor>();
	auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Fixture mesh is resident"), Mesh)) return false;
	TGuardValue<TSoftObjectPtr<UStaticMesh>> KeepMesh(CDO->VisualMesh, TSoftObjectPtr<UStaticMesh>(Mesh));
	TGuardValue<TArray<FEFCalystoDeferredMeshBinding>> KeepBindings(CDO->Bindings, {{TEXT("VisualMesh"), TEXT("VisualAttachment")}});
	FEFCalystoContentCollisionContract Result; FString Error;
	auto Build = [&]() { return FEFCalystoContentCollisionContracts::Build(CDO->GetClass(), Result, Error); };
	TestTrue(TEXT("Runtime provider reserves mesh bounds without CDO primitive or editor metadata"), Build());
	TestTrue(TEXT("Declared mesh contributes the complete original bound"), Result.bHasPrimitiveBounds && Result.LocalPrimitiveBounds.GetExtent().Equals(Mesh->GetBounds().BoxExtent));
	const FString StableHash = Result.Hash;
	TestTrue(TEXT("Runtime declaration has stable identity"), Build() && StableHash == Result.Hash);
	CDO->VisualMesh.Reset(); TestFalse(TEXT("Unloaded soft resource is rejected without loading it"), Build()); CDO->VisualMesh = Mesh;
	CDO->Bindings[0].MeshProperty = TEXT("WrongType"); TestFalse(TEXT("Non-mesh scalar is rejected"), Build());
	CDO->Bindings[0].MeshProperty = TEXT("Missing"); TestFalse(TEXT("Missing property is rejected"), Build());
	CDO->Bindings[0].MeshProperty = TEXT("VisualMesh");
	CDO->Bindings[0].AttachmentComponent = TEXT("Missing"); TestFalse(TEXT("Missing attachment is rejected"), Build());
	CDO->Bindings[0].AttachmentComponent = TEXT("VisualAttachment");
	const auto RepeatedBinding = CDO->Bindings[0];
	CDO->Bindings.Add(RepeatedBinding); TestFalse(TEXT("Duplicate mesh declarations are rejected"), Build());
	while (CDO->Bindings.Num() < 17) CDO->Bindings.Add(RepeatedBinding);
	TestFalse(TEXT("Finite declaration bound is enforced"), Build());
	return true;
}
#endif
