#if WITH_DEV_AUTOMATION_TESTS

#include "Calysto/EFCalystoNavigationBoundsVolume.h"
#include "Calysto/EFCalystoPlayerNavMesh.h"
#include "Calysto/EFCalystoStructuralNavigation.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Script.h"

namespace EFCalystoStructuralNavigationTestsPrivate
{
	struct FTestWorld
	{
		UWorld* World = nullptr;
		FTestWorld()
		{
			const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
				.RequiresHitProxies(false).ShouldSimulatePhysics(false);
			// CreateWorld already initializes its level and WorldSettings in UE 5.8.
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
				true, ERHIFeatureLevel::Num, &Initialization);
			if (World && GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}
		~FTestWorld()
		{
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
			World->MarkObjectsPendingKill();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoStructuralInstanceBoundsTest,
	"NoShellForWinter.CalystoDungeon.StructuralNavigation.InstanceBoundsAndCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoStructuralInstanceBoundsTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine fixture mesh is resident"), Cube)) return false;
	TStrongObjectPtr<UInstancedStaticMeshComponent> Instances(NewObject<UInstancedStaticMeshComponent>());
	Instances->SetStaticMesh(Cube);
	Instances->SetWorldTransform(FTransform(FRotator(0.0, 90.0, 0.0), FVector(1000.0, 2000.0, 300.0)));
	const FTransform First(FRotator::ZeroRotator, FVector(-600.0, 0.0, 0.0), FVector(2.0, 3.0, 0.25));
	const FTransform Second(FRotator(0.0, 45.0, 0.0), FVector(600.0, 0.0, 200.0), FVector(1.0, 2.0, 1.0));
	Instances->AddInstance(First);
	Instances->AddInstance(Second);
	const TArray<FEFCalystoStructuralSource> Sources{{Instances.Get(), EEFCalystoStructuralRole::Floor}};
	FEFCalystoStructuralEvidence Evidence;
	FString Error;
	TestTrue(TEXT("Bounded collector reads both actual instances"),
		FEFCalystoStructuralNavigation::CollectStructuralEvidence(Sources, 4, 8, Evidence, Error));
	TestEqual(TEXT("Floor count is instance count, not component count"), Evidence.FloorInstanceCount, 2);
	FTransform FirstWorld, SecondWorld;
	Instances->GetInstanceTransform(0, FirstWorld, true);
	Instances->GetInstanceTransform(1, SecondWorld, true);
	FBox Expected = Cube->GetBoundingBox().TransformBy(FirstWorld);
	Expected += Cube->GetBoundingBox().TransformBy(SecondWorld);
	TestTrue(TEXT("Bounds include each transformed mesh at its actual position"),
		Evidence.StructuralBounds.Min.Equals(Expected.Min, 0.001) && Evidence.StructuralBounds.Max.Equals(Expected.Max, 0.001));
	TestFalse(TEXT("Unregistered geometry cannot be treated as ready"), Evidence.bWalkableComponentsReady);
	TestFalse(TEXT("Instance capacity failure does not silently truncate"),
		FEFCalystoStructuralNavigation::CollectStructuralEvidence(Sources, 4, 1, Evidence, Error));
	TArray<FEFCalystoStructuralSource> Duplicate = Sources;
	Duplicate.Add(Sources[0]);
	TestFalse(TEXT("Duplicate binding fails instead of double-counting bounds"),
		FEFCalystoStructuralNavigation::CollectStructuralEvidence(Duplicate, 4, 8, Evidence, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoRuntimeBoxNavigationBoundsTest,
	"NoShellForWinter.CalystoDungeon.StructuralNavigation.RuntimeBoxHasRealExtent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoRuntimeBoxNavigationBoundsTest::RunTest(const FString& Parameters)
{
	using namespace EFCalystoStructuralNavigationTestsPrivate;
	FTestWorld Fixture;
	AEFCalystoNavigationBoundsVolume* Volume = Fixture.World->SpawnActor<AEFCalystoNavigationBoundsVolume>();
	if (!TestNotNull(TEXT("Transient volume spawns without editor brush generation"), Volume)) return false;
	const FBox Requested(FVector(-4000.0, -7200.0, -200.0), FVector(2500.0, 6100.0, 800.0));
	TestTrue(TEXT("Real box applies full generated geometry extent"), Volume->SetGeneratedGeometryBounds(Requested));
	const FBox Actual = Volume->GetComponentsBoundingBox(true);
	TestTrue(TEXT("UE navigation's exact bounds source includes requested minimum"), Actual.IsInsideOrOn(Requested.Min));
	TestTrue(TEXT("UE navigation's exact bounds source includes requested maximum"), Actual.IsInsideOrOn(Requested.Max));
	TestTrue(TEXT("Runtime volume has nonzero bounds on all three axes"), Actual.GetExtent().GetMin() > 0.0);
	TestFalse(TEXT("Invalid geometry cannot replace valid bounds"), Volume->SetGeneratedGeometryBounds(FBox(ForceInit)));
	const FBox Fractional(FVector(0.1, -7200.00000003, -200.74), FVector(1000.3, 6100.754, 800.009));
	TestTrue(TEXT("Fractional generated corners remain enclosed after center/extent reconstruction"), Volume->SetGeneratedGeometryBounds(Fractional));
	const FBox FractionalActual = Volume->GetComponentsBoundingBox(true);
	TestTrue(TEXT("Fractional minimum remains covered"), FractionalActual.IsInsideOrOn(Fractional.Min));
	TestTrue(TEXT("Fractional maximum remains covered"), FractionalActual.IsInsideOrOn(Fractional.Max));
	TestTrue(TEXT("Outward rounding is bounded to less than one centimetre per face"),
		(Fractional.Min - FractionalActual.Min).GetMax() < 1.0 && (FractionalActual.Max - Fractional.Max).GetMax() < 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNavigationCompletionOwnershipTest,
	"NoShellForWinter.CalystoDungeon.StructuralNavigation.CompletionOwnershipAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoNavigationCompletionOwnershipTest::RunTest(const FString& Parameters)
{
	using namespace EFCalystoStructuralNavigationTestsPrivate;
	FTestWorld Fixture;
	// This fixture has no gameplay BeginPlay. Actor::ProcessEvent otherwise
	// suppresses even native dynamic delegates before actors initialize (UE 5.8).
	FEditorScriptExecutionGuard ScriptExecution;
	// ANavigationData self-destructs in a game world without its actual system.
	// Register the fixture owner before spawning data, without building any tiles.
	TStrongObjectPtr<UNavigationSystemV1> Navigation(NewObject<UNavigationSystemV1>(Fixture.World));
	Fixture.World->SetNavigationSystem(Navigation.Get());
	AEFCalystoNavigationBoundsVolume* Volume = Fixture.World->SpawnActor<AEFCalystoNavigationBoundsVolume>();
	ARecastNavMesh* PlayerData = Fixture.World->SpawnActor<AEFCalystoPlayerNavMesh>();
	ARecastNavMesh* OtherData = Fixture.World->SpawnActor<ARecastNavMesh>();
	if (!TestNotNull(TEXT("Bounds owner exists"), Volume) || !TestNotNull(TEXT("Relevant data exists"), PlayerData)
		|| !TestNotNull(TEXT("Other agent data exists"), OtherData)) return false;
	if (!TestTrue(TEXT("Fixture data remains valid in its registered navigation world"), IsValid(PlayerData) && IsValid(OtherData))) return false;
	Volume->ObserveNavigationCompletion(Navigation.Get(), PlayerData);
	if (!TestTrue(TEXT("The actual dynamic completion delegate is bound"), Navigation->OnNavigationGenerationFinishedDelegate.IsBound())) return false;
	Navigation->OnNavigationGenerationFinishedDelegate.Broadcast(OtherData);
	TestEqual(TEXT("Other agent completion cannot settle this attempt"), Volume->GetNavigationCompletionRevision(), uint64(0));
	Navigation->OnNavigationGenerationFinishedDelegate.Broadcast(PlayerData);
	TestEqual(TEXT("Actual UE tile completion advances the relevant observation"), Volume->GetNavigationCompletionRevision(), uint64(1));
	TestTrue(TEXT("Completion records a bounded-observation timestamp"), Volume->GetLastNavigationCompletionSeconds() > 0);
	Volume->ObserveNavigationCompletion(Navigation.Get(), PlayerData);
	Navigation->OnNavigationGenerationFinishedDelegate.Broadcast(PlayerData);
	TestEqual(TEXT("Repeated observation neither resets revision nor duplicates callbacks"), Volume->GetNavigationCompletionRevision(), uint64(2));
	Volume->StopObservingNavigationCompletion();
	Navigation->OnNavigationGenerationFinishedDelegate.Broadcast(PlayerData);
	TestEqual(TEXT("Late completion after release cannot change the old attempt"), Volume->GetNavigationCompletionRevision(), uint64(2));
	TestFalse(TEXT("Cleanup removes its dynamic subscription"), Navigation->OnNavigationGenerationFinishedDelegate.IsBound());
	Volume->ObserveNavigationCompletion(Navigation.Get(), PlayerData);
	TestEqual(TEXT("An explicitly rebound observation starts a fresh revision"), Volume->GetNavigationCompletionRevision(), uint64(0));
	Volume->StopObservingNavigationCompletion();
	if (!TestTrue(TEXT("Fixture has an authored supported agent"), Navigation->GetSupportedAgents().Num() > 0)) return false;
	const FNavDataConfig Agent = Navigation->GetSupportedAgents().Last();
	PlayerData->SetConfig(Agent);
	if (!TestNull(TEXT("Relevant data is absent before deferred registration"), Navigation->GetNavDataForProps(Agent, FVector::ZeroVector))) return false;
	Volume->ObserveNavigationCompletion(Navigation.Get(), nullptr, &Agent, FVector::ZeroVector);
	TestTrue(TEXT("Absent data does not postpone the completion subscription"), Navigation->OnNavigationGenerationFinishedDelegate.IsBound());
	// One ordinary system tick drains the actual deferred registration queue;
	// keep tile building locked because this fixture tests completion ownership.
	Navigation->AddNavigationBuildLock(ENavigationBuildLock::Custom);
	Navigation->Tick(0.0f);
	if (!TestTrue(TEXT("Deferred registration resolves the exact requested agent"), Navigation->GetNavDataForProps(Agent, FVector::ZeroVector) == PlayerData)) return false;
	TestEqual(TEXT("Registered Player data retains the full capsule radius"), PlayerData->GetConfig().AgentRadius, 50.0f);
	TestEqual(TEXT("Registered Player data retains the full capsule height"), PlayerData->GetConfig().AgentHeight, 176.0f);
	TestEqual(TEXT("Player resolution is configured before its generator initializes"), PlayerData->GetCellSize(ENavigationDataResolution::Default), 10.0f);
	TestEqual(TEXT("The native Default agent keeps its original raster precision"), OtherData->GetCellSize(ENavigationDataResolution::Default), 19.0f);
	Navigation->OnNavigationGenerationFinishedDelegate.Broadcast(PlayerData);
	TestEqual(TEXT("Completion before the next poll is retained"), Volume->GetNavigationCompletionRevision(), uint64(1));
	Volume->ObserveNavigationCompletion(Navigation.Get(), PlayerData, &Agent, FVector::ZeroVector);
	TestEqual(TEXT("First poll after data appears preserves completion"), Volume->GetNavigationCompletionRevision(), uint64(1));
	Navigation->OnNavigationGenerationFinishedDelegate.Broadcast(OtherData);
	TestEqual(TEXT("Late other-agent completion cannot alter the captured revision"), Volume->GetNavigationCompletionRevision(), uint64(1));
	Volume->Destroy();
	TestFalse(TEXT("Bounds destruction also removes its subscription"), Navigation->OnNavigationGenerationFinishedDelegate.IsBound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoStructuralSharedMeshRuleTest,
	"NoShellForWinter.CalystoDungeon.StructuralNavigation.SharedMeshRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoStructuralSharedMeshRuleTest::RunTest(const FString& Parameters)
{
	using namespace EFCalystoStructuralNavigationTestsPrivate;
	const FSoftObjectPath RampPath(TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_RampBevel.SM_RampBevel"));
	TArray<FEFCalystoStructuralMeshRule> Rules{{RampPath, EEFCalystoStructuralRole::Ramp}, {RampPath, EEFCalystoStructuralRole::Ramp}};
	TArray<FEFCalystoStructuralMeshRule> Normalized;
	FString Error;
	if (!TestTrue(TEXT("Native RampTop and RampBottom may reuse the same ramp mesh"),
		FEFCalystoStructuralNavigation::NormalizeMeshRules(Rules, Normalized, Error))) return false;
	TestEqual(TEXT("Repeated schema entries become one mesh classifier"), Normalized.Num(), 1);
	if (Normalized.Num() != 1) return false;
	TestEqual(TEXT("The native mesh identity is preserved"), Normalized[0].Mesh.ToString(), RampPath.ToString());
	TestTrue(TEXT("The shared role is preserved"), Normalized[0].Role == EEFCalystoStructuralRole::Ramp);
	Rules.Add({RampPath, EEFCalystoStructuralRole::Floor});
	TestFalse(TEXT("One mesh cannot silently change structural role"), FEFCalystoStructuralNavigation::NormalizeMeshRules(Rules, Normalized, Error));
	TestTrue(TEXT("Conflict identifies the exact mesh and both roles"), Error.Contains(RampPath.ToString())
		&& Error.Contains(TEXT("Ramp")) && Error.Contains(TEXT("Floor")));
	TestTrue(TEXT("Failed preflight publishes no partial schema"), Normalized.IsEmpty());
	Rules = {{FSoftObjectPath(), EEFCalystoStructuralRole::Ramp}};
	TestFalse(TEXT("An empty mesh remains invalid"), FEFCalystoStructuralNavigation::NormalizeMeshRules(Rules, Normalized, Error));
	Rules = {{RampPath, EEFCalystoStructuralRole(255)}};
	TestFalse(TEXT("An unsupported role remains invalid"), FEFCalystoStructuralNavigation::NormalizeMeshRules(Rules, Normalized, Error));
	Rules.Reset();
	TestFalse(TEXT("An empty schema remains invalid"), FEFCalystoStructuralNavigation::NormalizeMeshRules(Rules, Normalized, Error));

	// Reusing a schema mesh must not collapse the actual components or their instances.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine structural fixture mesh is resident"), Cube)) return false;
	FTestWorld Fixture;
	AActor* Owner = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Isolated structural owner exists"), Owner)) return false;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transient);
		Owner->AddInstanceComponent(Component);
		Component->SetStaticMesh(Cube);
		Component->AddInstance(FTransform(FVector(double(Index) * 900.0, 0.0, 0.0)));
	}
	Rules = {{FSoftObjectPath(Cube), EEFCalystoStructuralRole::Ramp}, {FSoftObjectPath(Cube), EEFCalystoStructuralRole::Ramp}};
	TArray<FEFCalystoStructuralSource> Sources;
	if (!TestTrue(TEXT("Gather accepts repeated same-role mesh rules"),
		FEFCalystoStructuralNavigation::GatherNativeSources(*Owner, nullptr, Rules, Sources, Error))) return false;
	TestEqual(TEXT("Both physical components remain separate sources"), Sources.Num(), 2);
	FEFCalystoStructuralEvidence Evidence;
	TestTrue(TEXT("Each physical instance is counted exactly once"),
		FEFCalystoStructuralNavigation::CollectStructuralEvidence(Sources, 2, 2, Evidence, Error));
	TestEqual(TEXT("Ramp schema deduplication does not remove geometry"), Evidence.TotalInstanceCount, 2);
	TestEqual(TEXT("Ramp geometry does not impersonate required flat Floor instances"), Evidence.FloorInstanceCount, 0);
	if (Sources.Num() != 2) return false;
	auto* ExtensionComponent = CastChecked<UInstancedStaticMeshComponent>(Sources[0].Component.Get());
	ExtensionComponent->UpdateInstanceTransform(0, FTransform(FQuat::Identity, FVector(0, 0, 300), FVector(1, 1, 0)), false, false, true);
	Sources[0].Role = EEFCalystoStructuralRole::Wall;
	TestFalse(TEXT("A wall role alone does not permit a zero-height instance"),
		FEFCalystoStructuralNavigation::CollectStructuralEvidence(Sources, 2, 2, Evidence, Error));
	Sources[0].NativeZeroHeightWallExtensionIndices.Add(0);
	TestTrue(TEXT("An exact adapter-proved native upper extension remains observable"),
		FEFCalystoStructuralNavigation::CollectStructuralEvidence(Sources, 2, 2, Evidence, Error));
	TestEqual(TEXT("Zero-height native extensions remain in the complete instance count"), Evidence.TotalInstanceCount, 2);
	TestEqual(TEXT("The zero-height extension is explicitly reported"), Evidence.NativeZeroHeightWallExtensionCount, 1);
	TestEqual(TEXT("A zero-height extension cannot satisfy required volumetric walls"), Evidence.WallInstanceCount, 0);
	Sources[0].Role = EEFCalystoStructuralRole::Floor;
	TestFalse(TEXT("An extension proof cannot excuse a degenerate required Floor"),
		FEFCalystoStructuralNavigation::CollectStructuralEvidence(Sources, 2, 2, Evidence, Error));
	Sources[0].Role = EEFCalystoStructuralRole::Wall;
	Sources[0].NativeZeroHeightWallExtensionIndices.Add(1);
	TestFalse(TEXT("A stale extension index cannot be accepted"),
		FEFCalystoStructuralNavigation::CollectStructuralEvidence(Sources, 2, 2, Evidence, Error));
	Rules.Add({FSoftObjectPath(Cube), EEFCalystoStructuralRole::Wall});
	TestFalse(TEXT("Gather also rejects conflicting callers before collecting geometry"),
		FEFCalystoStructuralNavigation::GatherNativeSources(*Owner, nullptr, Rules, Sources, Error));
	TestTrue(TEXT("Conflict returns no partial geometry sources"), Sources.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDesignatedRoomNavigationTest,
	"NoShellForWinter.CalystoDungeon.StructuralNavigation.DesignatedRoomAndPadding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoDesignatedRoomNavigationTest::RunTest(const FString& Parameters)
{
	FEFCalystoDesignatedRoom Room;
	Room.StableRoomId = 7;
	Room.LocalBounds = FBox(FVector(-300.0, -200.0, -50.0), FVector(300.0, 200.0, 300.0));
	Room.RoomToWorld = FTransform(FRotator(0.0, 90.0, 0.0), FVector(2000.0, -3000.0, 0.0));
	TestTrue(TEXT("Translated and rotated native room owns its marker"), Room.ContainsWorldPoint(Room.RoomToWorld.TransformPosition(FVector::ZeroVector)));
	TestFalse(TEXT("A marker in another room cannot become an entry fallback"), Room.ContainsWorldPoint(Room.RoomToWorld.TransformPosition(FVector(900.0, 0.0, 0.0))));
	FEFCalystoStructuralEvidence Evidence;
	Evidence.StructuralBounds = FBox(FVector(-100.0, -200.0, 0.0), FVector(300.0, 400.0, 800.0));
	Evidence.WalkableGeometryBounds = FBox(FVector(-100.0, -200.0, 0.0), FVector(300.0, 400.0, 0.0));
	const FBox Bounds = FEFCalystoStructuralNavigation::ComputeNavigationBounds(Evidence, FVector(200.0, 200.0, 200.0));
	TestTrue(TEXT("Flat floor bounds produce vertical navigation coverage from actual structure and padding"),
		Bounds.Min.Equals(FVector(-300.0, -400.0, -200.0)) && Bounds.Max.Equals(FVector(500.0, 600.0, 1000.0)));
	TestFalse(TEXT("Absent walkable geometry cannot produce navigation bounds"),
		FEFCalystoStructuralNavigation::ComputeNavigationBounds({}, FVector(200.0)).IsValid != 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNavigationPendingDeadlineTest,
	"NoShellForWinter.CalystoDungeon.StructuralNavigation.PendingDeadlineAndCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoNavigationPendingDeadlineTest::RunTest(const FString& Parameters)
{
	using namespace EFCalystoStructuralNavigationTestsPrivate;
	FTestWorld Fixture;
	AActor* Owner = Fixture.World->SpawnActor<AActor>();
	FEFCalystoStructuralNavigationInput Input;
	Input.AttemptId = FGuid::NewGuid();
	Input.World = Fixture.World;
	Input.DungeonOwner = Owner;
	Input.AgentProperties = FNavAgentProperties(42.0f, 192.0f);
	Input.RequestDeadlineSeconds = 130.0;
	Input.StartRoom.StableRoomId = 1;
	Input.StartRoom.LocalBounds = FBox(FVector(-300.0), FVector(300.0));
	Input.EndRoom = Input.StartRoom;
	Input.EndRoom.StableRoomId = 2;
	Input.EndRoom.RoomToWorld.SetLocation(FVector(2000.0, 0.0, 0.0));
	Input.EndApproachWorld = Input.EndRoom.RoomToWorld.GetLocation();
	FEFCalystoStructuralNavigation Observer;
	TestTrue(TEXT("Unfinished generation remains Pending"), Observer.Observe(Input, 101.0, false).State == EEFCalystoNavigationObservation::Pending);
	TestTrue(TEXT("Unfinished generation does not consume a synthetic retry"), Observer.Observe(Input, 129.0, false).State == EEFCalystoNavigationObservation::Pending);
	TestTrue(TEXT("One absolute deadline bounds all observations"), Observer.Observe(Input, 130.0, false).Code == FName(TEXT("REQUEST_DEADLINE_EXHAUSTED")));
	Observer.Cancel();
	TestTrue(TEXT("Cancelled empty observation releases immediately"), Observer.IsReleased());
	TestTrue(TEXT("Late observations cannot resurrect cancellation"), Observer.Observe(Input, 131.0, true).State == EEFCalystoNavigationObservation::Cancelled);
	return true;
}

#endif
