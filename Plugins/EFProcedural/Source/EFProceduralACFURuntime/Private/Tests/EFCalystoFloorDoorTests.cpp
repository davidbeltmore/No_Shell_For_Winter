#if WITH_DEV_AUTOMATION_TESTS

#include "Calysto/EFCalystoFloorDoor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoFloorDoorAppearanceTest,
	"NoShellForWinter.CalystoDungeon.Director.Native.ProgressionDoorAppearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoFloorDoorAppearanceTest::RunTest(const FString&)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false)
		.RequiresHitProxies(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
		true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("Door collision fixture world exists"), World)) return false;
	AEFCalystoFloorDoor* Door = World->SpawnActor<AEFCalystoFloorDoor>();
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors.SM_SquaredArchedWoodenDoors"));
	if (!TestNotNull(TEXT("Native door actor exists"), Door) || !TestNotNull(TEXT("Selected native artwork exists"), Mesh))
	{ World->DestroyWorld(false); World->MarkObjectsPendingKill(); return false; }
	UStaticMeshComponent* Component = Door->FindComponentByClass<UStaticMeshComponent>();
	if (!TestNotNull(TEXT("Travel-owned visual component exists"), Component))
	{ World->DestroyWorld(false); World->MarkObjectsPendingKill(); return false; }
	TestTrue(TEXT("Selected appearance aligns successfully"), Door->SetDirectorAppearance(Mesh));
	const FBox Aligned = Mesh->GetBoundingBox().TransformBy(Component->GetRelativeTransform());
	TestTrue(TEXT("Door base rests at the native floor marker"), FMath::IsNearlyZero(Aligned.Min.Z, 0.001));
	TestTrue(TEXT("Door width is centered on its marker"), FMath::IsNearlyZero(Aligned.GetCenter().X, 0.001));
	TestTrue(TEXT("Door thickness is centered on its marker"), FMath::IsNearlyZero(Aligned.GetCenter().Y, 0.001));
	TestTrue(TEXT("Selected artwork retains its authored size"), Aligned.GetSize().Equals(Mesh->GetBoundingBox().GetSize(), 0.001));
	TestTrue(TEXT("The progression door remains physically blocking"), Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
	const FVector Center = Door->GetDirectorApproachWorld() + FVector(0, 0, 90);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(50, 88);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(EFCalystoDoorAppearanceTest), false);
	TestFalse(TEXT("Real 50 by 88 cm player capsule clears aligned selected artwork"),
		World->OverlapBlockingTestByChannel(Center, FQuat::Identity, ECC_Pawn, Capsule, Query));
	Component->SetRelativeTransform(FTransform(FRotator(0, -90, 0), FVector(0, 60, -110)));
	TestTrue(TEXT("The old elevated-pivot transform reproduces the observed End obstruction"),
		World->OverlapBlockingTestByChannel(Center, FQuat::Identity, ECC_Pawn, Capsule, Query));
	TestTrue(TEXT("Reapplying identical selected artwork repairs alignment idempotently"), Door->SetDirectorAppearance(Mesh));
	TestFalse(TEXT("Null artwork cannot silently replace the selected door"), Door->SetDirectorAppearance(nullptr));
	World->DestroyWorld(false);
	World->MarkObjectsPendingKill();
	return true;
}

#endif
