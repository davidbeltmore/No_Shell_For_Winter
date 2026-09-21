#if WITH_DEV_AUTOMATION_TESTS

#include "Calysto/EFCalystoSurfaceCandidates.h"
#include "Calysto/EFCalystoBakedArchitecture.h"
#include "Calysto/EFCalystoNavigationBoundsVolume.h"
#include "Calysto/EFCalystoPlayerNavMesh.h"
#include "Algo/Reverse.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PCGDataAsset.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "UObject/Script.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

namespace EFCalystoSurfaceCandidateTests
{
	FGuid Id(uint32 N) { return FGuid(0x53555246, 0x41434553, 0, N); }
	struct FFixture
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		UStaticMesh* Cube = nullptr;
		UEFCalystoDungeonDirectorAsset* Asset = nullptr;
		TStrongObjectPtr<UEFCalystoDungeonDirectorAsset> AssetLifetime;
		UInstancedStaticMeshComponent* Floor = nullptr;
		FEFCalystoCompiledDirector Config;
		FEFCalystoNativeRoomConfig RoomConfig;
		FEFCalystoNativeResult Native;
		FEFCalystoStructuralNavigationInput NI;
		FEFCalystoStructuralNavigationResult Nav;
		FEFCalystoSurfaceCandidateRequest Request;
		FFixture()
		{
			if (!GEngine) return;
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(false).RequiresHitProxies(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (World) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}
		~FFixture() { if (World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); World->MarkObjectsPendingKill(); } }
		UInstancedStaticMeshComponent* Surface(EEFCalystoStructuralRole Role, const FTransform& Transform)
		{
			auto* C = NewObject<UInstancedStaticMeshComponent>(Owner); Owner->AddInstanceComponent(C);
			C->SetStaticMesh(Cube); C->SetCollisionEnabled(ECollisionEnabled::QueryOnly); C->SetCollisionObjectType(ECC_WorldStatic);
			C->SetCollisionResponseToAllChannels(ECR_Block); C->SetGenerateOverlapEvents(false); C->RegisterComponent(); C->AddInstance(Transform, true);
			FEFCalystoStructuralSource S; S.Component = C; S.Role = Role; NI.StructuralSources.Add(S); return C;
		}
		bool Compile(FAutomationTestBase& Test)
		{
			TArray<FEFCalystoValidationIssue> Issues;
			if (Asset->Compile(Config, Issues)) return true;
			for (const auto& I : Issues) Test.AddError(I.Field + TEXT(": ") + I.Message); return false;
		}
		bool Observe(FAutomationTestBase& Test)
		{
			FString Error;
			if (FEFCalystoStructuralNavigation::CollectStructuralEvidence(NI.StructuralSources, 1024, 100000, Nav.Structure, Error)) return true;
			Test.AddError(Error); return false;
		}
		bool Initialize(FAutomationTestBase& Test, bool bOtherZones = false)
		{
			if (!Test.TestNotNull(TEXT("Isolated registered physics world"), World)) return false;
			// Read only: the fixture never changes or saves this shared engine asset or its collision setup.
			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			if (!Test.TestNotNull(TEXT("Actual static mesh with cooked simple collision"), Cube)) return false;
			Owner = World->SpawnActor<AActor>(); if (!Owner) return false;
			NI.World = World; NI.DungeonOwner = Owner; NI.AttemptId = Id(90); NI.RequestDeadlineSeconds = FPlatformTime::Seconds() + 30;
			NI.CapsuleRadius = 50; NI.CapsuleHalfHeight = 88;
			Floor = Surface(EEFCalystoStructuralRole::Floor, FTransform(FQuat::Identity, FVector(0, 0, -10), FVector(4, 4, 0.2)));
			Floor->AddInstance(FTransform(FQuat::Identity, FVector(600, 0, -10), FVector(4, 4, 0.2)), true);
			Asset = NewObject<UEFCalystoDungeonDirectorAsset>(); AssetLifetime.Reset(Asset); auto& Style = Asset->Styles.AddDefaulted_GetRef();
			Style.Selection.Id = Id(1); Style.Selection.DisplayName = TEXT("Real surface fixture");
			Style.Materials.Floor = Style.Materials.Wall = Style.Materials.Roof = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Test/SurfaceMaterial.SurfaceMaterial")));
			FEFCalystoArchitectureEntry Mesh; Mesh.Mesh = Cube; Mesh.Selection.Id = Id(2); Style.Architecture.Floor.Add(Mesh);
			Mesh.Selection.Id = Id(3); Style.Architecture.Wall.Add(Mesh); Mesh.Selection.Id = Id(4); Style.Architecture.Roof.Add(Mesh);
			Style.Decals.Mode = EEFCalystoDecalMode::Block;
			auto& Theme = Asset->RoomThemes.AddDefaulted_GetRef(); Theme.Selection.Id = Id(5); Theme.Decals.Mode = EEFCalystoDecalMode::Block;
			auto& Group = Style.Content.AddDefaulted_GetRef(); Group.Id = Id(10); Group.Role = EEFCalystoGameplayRole::Prop;
			Group.Chance.FirstPercent = Group.Chance.LastPercent = 100; Group.MaximumPerFloor = 4;
			auto AddEntry = [&](uint32 N, EEFCalystoPlacementZone Zone)
			{
				auto& E = Group.Entries.AddDefaulted_GetRef(); E.Selection.Id = Id(N); E.ActorClass = AActor::StaticClass();
				E.Placement.Zone = Zone; E.Placement.FootprintHalfExtent = FVector(20, 20, 40); E.Placement.Clearance = 5;
				E.Placement.PositionVariationCm = 2; E.Placement.Spacing = 10;
			};
			AddEntry(11, EEFCalystoPlacementZone::Floor); AddEntry(12, EEFCalystoPlacementZone::Floor);
			RoomConfig.StyleId = Id(1); Native.bGraphCompleted = true; Native.GenerateRequests = 1;
			auto& Room = Native.Rooms.AddDefaulted_GetRef(); Room.RoomId = 1; Room.StyleId = Id(1);
			Room.LocalBounds = FBox(FVector(-300, -300, 0), FVector(900, 300, 0));
			Nav.State = EEFCalystoNavigationObservation::Ready; Nav.bEntryTransformValid = true;
			Nav.ValidatedEntryTransform.SetLocation(FVector(-1000, -1000, 88)); Nav.ValidatedEndApproach = FVector(1000, -1000, 0);
			for (int32 Index = 0; Index < 2; ++Index)
			{
				AActor* Marker = World->SpawnActor<AActor>(); if (!Marker) return false;
				auto* Bounds = NewObject<UBoxComponent>(Marker); Marker->AddInstanceComponent(Bounds); Marker->SetRootComponent(Bounds);
				Bounds->SetBoxExtent(FVector(10)); Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision); Bounds->SetGenerateOverlapEvents(false);
				Bounds->SetWorldLocation(FVector(Index == 0 ? -1000 : 1000, -1000, 0)); Bounds->RegisterComponent();
				(Index == 0 ? Native.NativeStartMarkers : Native.NativeEndMarkers).Add(Marker);
			}
			NI.StartMarkers = Native.NativeStartMarkers; NI.EndMarkers = Native.NativeEndMarkers;
			// Route input is controlled to isolate protection geometry. This fixture does not claim a Recast success.
			Nav.CompleteRoute = {FVector(-1000, -1000, 0), FVector(1000, -1000, 0)};
			if (bOtherZones)
			{
				Surface(EEFCalystoStructuralRole::Wall, FTransform(FQuat::Identity, FVector(210, 0, 200), FVector(0.2, 4, 4)));
				Surface(EEFCalystoStructuralRole::Wall, FTransform(FQuat::Identity, FVector(0, 210, 200), FVector(4, 0.2, 4)));
				Surface(EEFCalystoStructuralRole::Roof, FTransform(FQuat::Identity, FVector(0, 0, 410), FVector(4, 4, 0.2)));
				for (const auto Zone : {EEFCalystoPlacementZone::WallMiddle, EEFCalystoPlacementZone::CornerMiddle, EEFCalystoPlacementZone::Roof})
				{
					AddEntry(20 + uint32(Zone), Zone); auto& O = Native.Surfaces.AddDefaulted_GetRef(); O.RoomId = 1; O.OpportunityId = 100 + uint32(Zone); O.Zone = Zone;
					O.Normal = Zone == EEFCalystoPlacementZone::Roof ? -FVector::UpVector : Zone == EEFCalystoPlacementZone::CornerMiddle ? FVector(-1, -1, 0).GetSafeNormal() : -FVector::ForwardVector;
					O.Transform.SetLocation(Zone == EEFCalystoPlacementZone::Roof ? FVector(0, 0, 400) : Zone == EEFCalystoPlacementZone::CornerMiddle ? FVector(200, 200, 200) : FVector(200, 0, 200));
				}
			}
			Request.Configuration = &Config; Request.RoomConfiguration = &RoomConfig; Request.Native = &Native; Request.NavigationInput = &NI; Request.Navigation = &Nav;
			return Compile(Test) && Observe(Test);
		}
	};
	TArray<FGuid> Ids(const FEFCalystoSurfaceCandidateResult& R) { TArray<FGuid> Result; for (const auto& S : R.Surfaces) Result.Add(S.Id); return Result; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDenseSurfaceCandidateTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentSurfaces.DenseGeometryAndCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoDenseSurfaceCandidateTest::RunTest(const FString&)
{
	using namespace EFCalystoSurfaceCandidateTests; FFixture F; if (!F.Initialize(*this)) return false;
	FEFCalystoSurfaceCandidateResult R;
	if (!TestTrue(TEXT("Actual floor instance collision produces a complete catalog"), FEFCalystoSurfaceCandidates::Build(F.Request, R))) { AddError(R.Message); return false; }
	TestEqual(TEXT("Two 400cm floors produce 18 supported interior grid points without native floor anchors"), R.Surfaces.Num(), 18);
	TestTrue(TEXT("Actual support and swept-envelope collision queries executed"), R.PhysicsQueries >= 36);
	for (const auto& S : R.Surfaces)
	{
		TestEqual(TEXT("Identical physical contracts share both explicit eligible entry IDs"), S.CompatibleEntryIds.Num(), 2);
		TestTrue(TEXT("Physics is proved; navigation is not fabricated for nonnav content"), S.bCollisionValidated && !S.bNavigationValidated);
	}
	const auto Original = Ids(R);
	F.Request.Limits.MaximumCandidates=17;
	TestFalse(TEXT("A declared candidate capacity never truncates the complete native lattice"),FEFCalystoSurfaceCandidates::Build(F.Request,R));
	TestEqual(TEXT("Candidate capacity failure identifies the finite contract"),R.FailureCode,FName(TEXT("SurfaceCandidateBound")));
	TestTrue(TEXT("Candidate capacity failure retains no partial surface catalog"),R.Surfaces.IsEmpty() && R.Receipts.IsEmpty());
	F.Request.Limits.MaximumCandidates=16384;
	if (!TestTrue(TEXT("The supported V7 lattice capacity retains every feasible native opportunity"),FEFCalystoSurfaceCandidates::Build(F.Request,R)))
	{ AddError(R.Message); return false; }
	TestTrue(TEXT("Restored capacity preserves the complete canonical surface set"),Ids(R)==Original);
	Algo::Reverse(F.Asset->Styles[0].Content[0].Entries); F.Asset->Styles[0].Content[0].Chance.FirstPercent = F.Asset->Styles[0].Content[0].Chance.LastPercent = 0;
	F.RoomConfig.RunSeed = -123456789; F.NI.AttemptId = Id(91);
	if (!F.Compile(*this)) return false;
	TestTrue(TEXT("Candidate compatibility is independent of Chance, entry order, seed and request routing"), FEFCalystoSurfaceCandidates::Build(F.Request, R) && Ids(R) == Original);
	F.Nav.CompleteRoute = {FVector(-1000, 0, 0), FVector(1000, 0, 0)};
	TestTrue(TEXT("Actual route corridor filters before selection"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestEqual(TEXT("Capsule corridor removes the six middle-row opportunities"), R.Surfaces.Num(), 12);
	TestTrue(TEXT("Protected corridor rejection is explicit"), R.RejectedProtection > 0);
	F.Nav.CompleteRoute = {FVector(-1000, -1000, 0), FVector(1000, -1000, 0)};
	F.Native.NativeEndMarkers[0]->SetActorLocation(FVector(600, 0, 0));
	TestTrue(TEXT("Exact owned End marker protects its actual approach even away from the controlled route"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestFalse(TEXT("A candidate cannot occupy the native progression marker"), R.Surfaces.ContainsByPredicate([](const auto& S) { return S.Transform.GetLocation().Equals(FVector(600, 0, 45.25), 0.01); }));
	F.Native.NativeEndMarkers[0]->SetActorLocation(FVector(1000, -1000, 0));
	F.Nav.ValidatedEntryTransform.SetLocation(FVector(0, 0, 88));
	TestTrue(TEXT("Sole accepted player capsule is protected independently of raw native marker position"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestFalse(TEXT("Accepted entry space remains empty"), R.Surfaces.ContainsByPredicate([](const auto& S) { return S.Transform.GetLocation().Equals(FVector(0, 0, 45.25), 0.01); }));
	F.Nav.ValidatedEntryTransform.SetLocation(FVector(-1000, -1000, 88));
	FEFCalystoNativeRoom Overlap; Overlap.RoomId = 2; Overlap.StyleId = Id(1); Overlap.LocalBounds = FBox(FVector(15, -30, 0), FVector(150, 30, 0));
	F.Native.Rooms.Add(Overlap);
	TestTrue(TEXT("Overlapping native room bounds are handled without inventing ownership"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestFalse(TEXT("An envelope partially entering a second room is ambiguous"), R.Surfaces.ContainsByPredicate([](const auto& S) { return S.Transform.GetLocation().Equals(FVector(0, 0, 45.25), 0.01); }));
	F.Native.Rooms.Pop();
	F.Floor->UpdateInstanceTransform(0, FTransform(FQuat::Identity, FVector(0, 0, -9), FVector(4, 4, 0.2)), true, true);
	TestFalse(TEXT("Moved native physics invalidates the cached structural observation"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestTrue(TEXT("Stale geometry exposes no partial candidates"), R.Surfaces.IsEmpty() && R.Receipts.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoSurfaceEnvelopeCandidateTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentSurfaces.JitterZonesAndAtomicBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoSurfaceEnvelopeCandidateTest::RunTest(const FString&)
{
	using namespace EFCalystoSurfaceCandidateTests; FFixture F; if (!F.Initialize(*this, true)) return false;
	FEFCalystoSurfaceCandidateResult R;
	if (!TestTrue(TEXT("Native wall, corner and roof points obtain real support"), FEFCalystoSurfaceCandidates::Build(F.Request, R))) { AddError(R.Message); return false; }
	AddInfo(FString::Printf(TEXT("Jitter-zone candidate diagnostics: surfaces=%d geometry_rejections=%d protection_rejections=%d navigation_rejections=%d work_units=%lld failure=%s."),
		R.Surfaces.Num(), R.RejectedGeometry, R.RejectedProtection, R.RejectedNavigation, R.WorkUnits, *R.FailureCode.ToString()));
	for (const auto Zone : {EEFCalystoPlacementZone::WallMiddle, EEFCalystoPlacementZone::CornerMiddle, EEFCalystoPlacementZone::Roof})
	{
		int32 Count = 0; for (const auto& S : R.Surfaces) if (S.Zone == Zone) ++Count;
		AddInfo(FString::Printf(TEXT("Jitter-zone detail: zone=%d candidates=%d first_rejection=%s."), int32(Zone), Count, *R.FirstCandidateRejectionByZone.FindRef(Zone).ToString()));
		TestTrue(TEXT("Exact native zone has a feasible physical candidate"), R.Surfaces.ContainsByPredicate([&](const auto& S) { return S.Zone == Zone; }));
	}
	for (int32 I = 0; I < R.Surfaces.Num(); ++I) if (R.Surfaces[I].Zone == EEFCalystoPlacementZone::CornerMiddle)
		TestEqual(TEXT("Corner receipt contains two independently traced perpendicular wall supports"), R.Receipts[I].Supports.Num(), 2);
	for (auto& E : F.Asset->Styles[0].Content[0].Entries) if (E.Placement.Zone == EEFCalystoPlacementZone::Floor) E.Placement.PositionVariationCm = 50;
	if (!F.Compile(*this)) return false;
	TestTrue(TEXT("Whole authored jitter envelope is supported before selection"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	const FVector Center(0, 0, 45.25);
	TestTrue(TEXT("Center opportunity initially has full swept clearance"), R.Surfaces.ContainsByPredicate([&](const auto& S) { return S.Transform.GetLocation().Equals(Center, 0.01); }));
	auto* Blocker = NewObject<UBoxComponent>(F.Owner); F.Owner->AddInstanceComponent(Blocker); Blocker->SetBoxExtent(FVector(5, 5, 10));
	Blocker->SetWorldLocation(FVector(65, 0, 45)); Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Blocker->SetCollisionResponseToAllChannels(ECR_Block); Blocker->SetGenerateOverlapEvents(false); Blocker->RegisterComponent();
	TestTrue(TEXT("A blocker inside jitter reach is evaluated against actual world collision"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestFalse(TEXT("Clear center alone cannot excuse an obstructed possible jitter position"), R.Surfaces.ContainsByPredicate([&](const auto& S) { return S.Transform.GetLocation().Equals(Center, 0.01); }));
	F.Request.Limits.MaximumPhysicsQueries = 1;
	TestFalse(TEXT("Query capacity exhaustion fails the entire result"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestTrue(TEXT("Capacity failure retains no truncated compatibility set"), R.Surfaces.IsEmpty() && R.Receipts.IsEmpty());
	F.Request.Limits.MaximumPhysicsQueries = 32768; F.Asset->Styles[0].Content[0].Entries[0].Placement.bRequiresNavigation = true;
	if (!F.Compile(*this)) return false;
	TestFalse(TEXT("A required Player nav contract cannot use an arbitrary success flag"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestEqual(TEXT("Missing actual Player nav data is explicit"), R.FailureCode, FName(TEXT("SurfacePlayerNavigationMissing")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoCompleteRoomIndexTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentSurfaces.CompleteRoomIndexCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoCompleteRoomIndexTest::RunTest(const FString&)
{
	using namespace EFCalystoSurfaceCandidateTests; FFixture F; if (!F.Initialize(*this)) return false;
	// The real native attempt has hundreds of rooms. These rooms are deliberately
	// outside the two physical fixture floors: they must remain in the complete
	// ownership set, but cannot become a shortcut or an omitted overlap.
	for (int32 Index = 0; Index < 512; ++Index)
	{
		auto& Room = F.Native.Rooms.AddDefaulted_GetRef(); Room.RoomId = Index + 2; Room.StyleId = Id(1);
		const double X = 10000.0 + 1000.0 * Index;
		Room.LocalBounds = FBox(FVector(X, -300, 0), FVector(X + 400.0, 300, 0));
	}
	// The indexed proof completes below 12,000 units. The previous linear owner
	// scan takes more than 50,000 units for this same complete room set.
	F.Request.Limits.MaximumWorkUnits = 12000;
	FEFCalystoSurfaceCandidateResult Result;
	if (!TestTrue(TEXT("A complete hundreds-room ownership set fits the declared finite work budget"), FEFCalystoSurfaceCandidates::Build(F.Request, Result)))
	{ AddError(FString::Printf(TEXT("%s: %s (work_units=%lld, maximum=%lld)."), *Result.FailureCode.ToString(), *Result.Message, Result.WorkUnits, F.Request.Limits.MaximumWorkUnits)); return false; }
	TestEqual(TEXT("Remote rooms neither create nor suppress the complete local floor lattice"), Result.Surfaces.Num(), 18);
	TestTrue(TEXT("The room index keeps exact ownership proof within the declared work bound"), Result.WorkUnits <= F.Request.Limits.MaximumWorkUnits);
	return true;
}

namespace EFCalystoSurfaceCandidateTests
{
	class FPlayerNavigationCheck final : public IAutomationLatentCommand
	{
	public:
		explicit FPlayerNavigationCheck(FAutomationTestBase& InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			FEditorScriptExecutionGuard ScriptExecution;
			if (!Fixture)
			{
				// EnhancedInput's default TObjectIterator excludes Unreachable, not Garbage.
				// Finish prior owned test worlds' real GC before a new world registers its input
				// processor. This happens once, before the unchanged five-second navigation clock.
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
				Fixture = MakeUnique<FFixture>();
				if (!Fixture->Initialize(Test)) return Finish();
				auto& F = *Fixture;
				for (auto& E : F.Asset->Styles[0].Content[0].Entries) E.Placement.bRequiresNavigation = true;
				if (!F.Compile(Test)) return Finish();
				Navigation = NewObject<UNavigationSystemV1>(F.World); F.World->SetNavigationSystem(Navigation);
				const FNavDataConfig* AuthoredPlayer = Navigation->GetSupportedAgents().FindByPredicate([](const auto& A)
				{ return A.Name == FName(TEXT("Player")) && A.AgentRadius == 50 && A.AgentHeight == 176; });
				if (!Test.TestNotNull(TEXT("Fixture consumes the project's actual supported Player agent"), AuthoredPlayer)) return Finish();
				const FNavDataConfig Player = *AuthoredPlayer;
				// Instance-only filtering before world initialization; the navigation CDO/config are untouched.
				Navigation->OverrideSupportedAgents({Player}); F.NI.AgentProperties = Player;
				Bounds = F.World->SpawnActor<AEFCalystoNavigationBoundsVolume>();
				Recast = F.World->SpawnActor<AEFCalystoPlayerNavMesh>();
				if (!Test.TestNotNull(TEXT("Own generated navigation bounds"), Bounds) || !Test.TestNotNull(TEXT("Own actual Player Recast data"), Recast)) return Finish();
				Recast->SetConfig(Player);
				if (!Test.TestTrue(TEXT("Bounds cover only two tiny physical fixture floors"), Bounds->SetGeneratedGeometryBounds(FBox(FVector(-250, -250, -100), FVector(850, 250, 300))))) return Finish();
				F.Native.NativeStartMarkers[0]->SetActorLocation(FVector(-100, -100, 0));
				F.Native.NativeEndMarkers[0]->SetActorLocation(FVector(100, -100, 0));
				Navigation->RegisterNavigationInvoker(F.Owner, 1500, 1500);
				// UE's normal initialization schedules runtime-created data after populating the octree.
				// Do not call Build()/EnsureBuildCompletion(): those synchronously wait without this bound.
				Navigation->OnWorldInitDone(FNavigationSystemRunMode::GameMode);
				Deadline = FPlatformTime::Seconds() + 5; return false;
			}
			auto& F = *Fixture;
			if (FPlatformTime::Seconds() >= Deadline || ++Ticks > 600)
			{
				Test.AddError(FString::Printf(TEXT("Bounded Player Recast fixture did not settle: ticks=%d tiles=%d."), Ticks, IsValid(Recast) ? Recast->GetNumActiveTiles() : -1));
				return Finish();
			}
			F.World->Tick(LEVELTICK_All, 1.0f / 60.0f);
			if (!IsValid(Recast) || !IsValid(Navigation)) { Test.AddError(TEXT("Fixture lost its own live navigation owner.")); return Finish(); }
			if (Recast->GetNumActiveTiles() == 0 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(F.World)
				|| Navigation->HasDirtyAreasQueued()) return false;
			FNavLocation Start, End;
			if (!Recast->ProjectPoint(FVector(-100, -100, 0), Start, FVector(10, 10, 50))
				|| !Recast->ProjectPoint(FVector(100, -100, 0), End, FVector(10, 10, 50)))
			{ Test.AddError(TEXT("Actual settled Player tiles lack the independently specified fixture endpoints.")); return Finish(); }
			FPathFindingQuery Query(F.Owner, *Recast, Start.Location, End.Location, Recast->GetDefaultQueryFilter());
			Query.SetAllowPartialPaths(false);
			// One QA path establishes genuine connectivity for the fixture; the production assembler does no path search.
			const FPathFindingResult Path = Navigation->FindPathSync(F.NI.AgentProperties, Query);
			if (!Test.TestTrue(TEXT("Fixture supplies a real complete Player path"), Path.IsSuccessful() && Path.Path.IsValid() && Path.Path->IsValid() && !Path.Path->IsPartial())) return Finish();
			F.Nav.NavigationDataPath = Recast->GetPathName(); F.Nav.StartNavigationNode = Start.NodeRef; F.Nav.EndNavigationNode = End.NodeRef;
			F.Nav.NavigationAgentRadius = Recast->GetConfig().AgentRadius; F.Nav.NavigationAgentHeight = Recast->GetConfig().AgentHeight;
			F.Nav.CompleteRoute.Reset(); for (const auto& Point : Path.Path->GetPathPoints()) F.Nav.CompleteRoute.Add(Point.Location);
			F.Nav.ValidatedEntryTransform.SetLocation(FVector(-100, -100, 88)); F.Nav.ValidatedEndApproach = FVector(100, -100, 0);
			if (!F.Observe(Test)) return Finish();
			FEFCalystoSurfaceCandidateResult Result;
			if (!Test.TestTrue(TEXT("Actual Player polygons prove full supported content envelopes"), FEFCalystoSurfaceCandidates::Build(F.Request, Result)))
			{ Test.AddError(Result.Message); return Finish(); }
			Test.TestTrue(TEXT("At least one reachable non-progression placement remains"), !Result.Surfaces.IsEmpty());
			Test.TestTrue(TEXT("Actual polygon closure was examined"), Result.NavigationPolygons > 0);
			for (int32 I = 0; I < Result.Surfaces.Num(); ++I)
			{
				Test.TestTrue(TEXT("A navigation claim owns an actual reachable polygon receipt"), Result.Surfaces[I].bNavigationValidated && Result.Receipts[I].NavigationPolygon != 0);
				Test.TestTrue(TEXT("Disconnected second floor is not reachable just because it has nav polygons"), Result.Surfaces[I].Transform.GetLocation().X < 200);
			}
			Test.TestTrue(TEXT("Unreachable physically valid floor opportunities are explicitly rejected"), Result.RejectedNavigation > 0);
			return Finish();
		}
	private:
		bool Finish()
		{
			if (IsValid(Navigation)) Navigation->CancelBuild();
			Fixture.Reset(); Navigation = nullptr; Recast = nullptr; Bounds = nullptr; return true;
		}
		FAutomationTestBase& Test;
		TUniquePtr<FFixture> Fixture;
		UNavigationSystemV1* Navigation = nullptr;
		ARecastNavMesh* Recast = nullptr;
		AEFCalystoNavigationBoundsVolume* Bounds = nullptr;
		double Deadline = 0;
		int32 Ticks = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoPlayerNavigationSurfaceCandidateTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentSurfaces.ActualPlayerNavigationCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoPlayerNavigationSurfaceCandidateTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<EFCalystoSurfaceCandidateTests::FPlayerNavigationCheck>(*this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeCollisionSurfaceCandidateTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentSurfaces.NativeCollisionCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeCollisionSurfaceCandidateTest::RunTest(const FString&)
{
	using namespace EFCalystoSurfaceCandidateTests; FFixture F; if (!F.Initialize(*this)) return false;
	const TCHAR* Paths[] = {
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Floor.SM_Floor"),
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Wall.SM_Wall"),
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Roof.SM_Roof"),
		TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_WallDoor.SM_WallDoor")};
	for (int32 I = 0; I < UE_ARRAY_COUNT(Paths); ++I)
	{
		// All supporting structural assets, including the doorway wall; loads are fixture-only and read-only.
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Paths[I]);
		if (!TestNotNull(TEXT("Actual native structural mesh is present"), Mesh)) return false;
		UBodySetup* Body = Mesh->GetBodySetup(); if (!TestNotNull(TEXT("Actual native collision setup is present"), Body)) return false;
		AddInfo(FString::Printf(TEXT("NativeSupport path=%s trace_flag=%d boxes=%d convex=%d shapes=%d render_extent=%s"), Paths[I],
			int32(Body->GetCollisionTraceFlag()), Body->AggGeom.BoxElems.Num(), Body->AggGeom.ConvexElems.Num(), Body->AggGeom.GetElementCount(), *Mesh->GetBoundingBox().GetExtent().ToString()));
		if (I == 0)
		{
			F.Floor->SetStaticMesh(Mesh); F.Floor->ClearInstances(); F.Floor->AddInstance(FTransform::Identity, true);
			F.Asset->Styles[0].Architecture.Floor[0].Mesh = Mesh;
		}
		else
		{
			const auto Role = I != 2 ? EEFCalystoStructuralRole::Wall : EEFCalystoStructuralRole::Roof;
			auto* Component = F.Surface(Role, FTransform(I == 3 ? FVector(-2400, -3450, 0) : FVector(0, I == 1 ? 200 : 0, I == 1 ? 0 : 400)));
			Component->SetStaticMesh(Mesh);
			if (I != 3) (I == 1 ? F.Asset->Styles[0].Architecture.Wall : F.Asset->Styles[0].Architecture.Roof)[0].Mesh = Mesh;
		}
	}
	if (!F.Compile(*this) || !F.Observe(*this)) return false;
	FEFCalystoSurfaceCandidateResult Result;
	if (!TestTrue(TEXT("Production candidate extraction supports the actual initial native collision closure"), FEFCalystoSurfaceCandidates::Build(F.Request, Result)))
	{ AddError(Result.FailureCode.ToString() + TEXT(": ") + Result.Message); return false; }
	TestTrue(TEXT("Actual native floor produces supported dense content opportunities"), !Result.Surfaces.IsEmpty());
	TestTrue(TEXT("Proof reads the active cooked collision triangles"), Result.CookedCollisionTriangles >= 6 && Result.CookedConvexPatches >= 3);
	// Read-only geometry evidence from topology seed 1779679224 includes four wall orientations,
	// native 1.01 horizontal scale and fractional transforms. Identity-only loading missed this case.
	UStaticMesh* NativeWall = LoadObject<UStaticMesh>(nullptr, Paths[1]);
	const FTransform ObservedWalls[] = {
		FTransform(FQuat::Identity, FVector(-2400, -1650, 0), FVector(1.01, 1, 1)),
		FTransform(FQuat(0, 0, 1, 0), FVector(-300, -3450, 0), FVector(1.01, 1, 1)),
		FTransform(FQuat(0, 0, 0.7071067811865475, 0.7071067811865476), FVector(-2550, -3300, 0), FVector(1.01, 1, 1)),
		FTransform(FQuat(0, 0, -0.7071067811865475, 0.7071067811865476), FVector(-150, -1800, 0), FVector(1.01, 1, 1)),
		FTransform(FQuat(0, 0, 0.7071067811865475, 0.7071067811865476), FVector(-2550, -2999.9999910593033, 0), FVector(1.0099999924749137, 0.9999999925494194, 1)),
		FTransform(FQuat(0, 0, 0.7071067811865476, 0.7071067811865475), FVector(-1350, -1500, 0), FVector(1.0100000000000002, 1.0000000000000002, 1))};
	for (const auto& Transform : ObservedWalls)
	{
		auto* Component = F.Surface(EEFCalystoStructuralRole::Wall, Transform); Component->SetStaticMesh(NativeWall);
		if (!F.Observe(*this)) return false;
		if (!TestTrue(TEXT("Actual generated native wall transforms retain their exact planar collision"), FEFCalystoSurfaceCandidates::Build(F.Request, Result)))
		{ AddError(Result.FailureCode.ToString() + TEXT(": ") + Result.Message); return false; }
	}
	// Runtime-owned synthetic cooked collision tests the failure modes that a hull would conceal.
	// The original native mesh/body remain read-only; only transient fixture clones get new geometry.
	const auto SetCookedFloor = [&](TArray<FBox2D> Rectangles, bool bReverse)
	{
		auto* Mesh = DuplicateObject<UStaticMesh>(F.Floor->GetStaticMesh(), GetTransientPackage());
		auto* Body = NewObject<UBodySetup>(Mesh); Body->CollisionTraceFlag = CTF_UseComplexAsSimple;
		Body->bDoubleSidedGeometry = true; Body->bCreatedPhysicsMeshes = true; Body->bFailedToCreatePhysicsMeshes = false;
		Chaos::FTriangleMeshImplicitObject::ParticlesType Particles; Particles.AddParticles(Rectangles.Num() * 4);
		TArray<Chaos::TVec3<int32>> Triangles;
		for (int32 I = 0; I < Rectangles.Num(); ++I)
		{
			const auto& B = Rectangles[I]; const int32 V = I * 4;
			Particles.SetX(V, Chaos::FVec3f(float(B.Min.X), float(B.Min.Y), 0));
			Particles.SetX(V + 1, Chaos::FVec3f(float(B.Max.X), float(B.Min.Y), 0));
			Particles.SetX(V + 2, Chaos::FVec3f(float(B.Max.X), float(B.Max.Y), 0));
			Particles.SetX(V + 3, Chaos::FVec3f(float(B.Min.X), float(B.Max.Y), 0));
			Triangles.Emplace(V, V + 1, V + 2); Triangles.Emplace(V, V + 2, V + 3);
		}
		if (bReverse) Algo::Reverse(Triangles);
		Body->TriMeshGeometries.Add(Chaos::FTriangleMeshImplicitObjectPtr(new Chaos::FTriangleMeshImplicitObject(MoveTemp(Particles), MoveTemp(Triangles), TArray<uint16>())));
		Mesh->SetBodySetup(Body); F.Floor->SetStaticMesh(Mesh);
	};
	const TArray<FBox2D> Solid{FBox2D(FVector2D(-150), FVector2D(150))};
	SetCookedFloor(Solid, false); if (!F.Observe(*this)) return false;
	if (!TestTrue(TEXT("Connected two-triangle plane supports envelopes crossing its shared diagonal"), FEFCalystoSurfaceCandidates::Build(F.Request, Result))) { AddError(Result.Message); return false; }
	TestEqual(TEXT("Shared cooked edge is an exact planar union, not a false seam"), Result.Surfaces.Num(), 9);
	const auto SolidIds = Ids(Result);
	SetCookedFloor(Solid, true); if (!F.Observe(*this)) return false;
	TestTrue(TEXT("Cooked triangle ordering does not change canonical candidate geometry"), FEFCalystoSurfaceCandidates::Build(F.Request, Result) && Ids(Result) == SolidIds);
	const TArray<FBox2D> Hole{
		FBox2D(FVector2D(-150, -150), FVector2D(50, 150)), FBox2D(FVector2D(100, -150), FVector2D(150, 150)),
		FBox2D(FVector2D(50, -150), FVector2D(100, -25)), FBox2D(FVector2D(50, 25), FVector2D(100, 150))};
	SetCookedFloor(Hole, false); if (!F.Observe(*this)) return false;
	TestTrue(TEXT("A cooked planar surface may retain separate convex patches around a real hole"), FEFCalystoSurfaceCandidates::Build(F.Request, Result));
	const auto AtCenter = [](const auto& S) { return S.Transform.GetLocation().Equals(FVector(0, 0, 45.25), 0.01); };
	TestTrue(TEXT("Small center footprint is genuinely supported beside the hole"), Result.Surfaces.ContainsByPredicate(AtCenter));
	for (auto& E : F.Asset->Styles[0].Content[0].Entries) E.Placement.PositionVariationCm = 50;
	if (!F.Compile(*this)) return false;
	TestTrue(TEXT("Full jitter envelope is checked against exact cooked patches"), FEFCalystoSurfaceCandidates::Build(F.Request, Result));
	TestFalse(TEXT("A clear center trace cannot fill a hole reached only by jitter"), Result.Surfaces.ContainsByPredicate(AtCenter));
	for (auto& E : F.Asset->Styles[0].Content[0].Entries) E.Placement.PositionVariationCm = 2;
	if (!F.Compile(*this)) return false;
	SetCookedFloor({FBox2D(FVector2D(-150, -150), FVector2D(-10, 150)), FBox2D(FVector2D(10, -150), FVector2D(150, 150))}, false);
	if (!F.Observe(*this)) return false;
	TestTrue(TEXT("Disconnected cooked patches remain individually usable"), FEFCalystoSurfaceCandidates::Build(F.Request, Result));
	TestEqual(TEXT("Disconnected seam remains empty instead of being convex-hulled"), Result.Surfaces.Num(), 6);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeArchitectureSurfaceTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentSurfaces.NativeArchitectureGeometryAndVariation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeArchitectureSurfaceTest::RunTest(const FString&)
{
	using namespace EFCalystoSurfaceCandidateTests; FFixture F; if (!F.Initialize(*this, true)) return false;
	F.Native.Surfaces.Reset(); auto& Rules = F.Asset->Styles[0].Architecture.Decoration;
	for (uint8 Z = 0; Z <= uint8(EEFCalystoPlacementZone::Roof); ++Z)
	{
		const auto Zone = EEFCalystoPlacementZone(Z);
		const bool IsCorner = Zone == EEFCalystoPlacementZone::CornerBottom || Zone == EEFCalystoPlacementZone::CornerMiddle || Zone == EEFCalystoPlacementZone::CornerTop;
		const bool IsFloor = Zone == EEFCalystoPlacementZone::Floor, IsRoof = Zone == EEFCalystoPlacementZone::Roof;
		auto& Slot = F.Native.Surfaces.AddDefaulted_GetRef(); Slot.RoomId = 1; Slot.OpportunityId = Z + 1; Slot.Zone = Zone;
		const double Height = Zone == EEFCalystoPlacementZone::WallBottom || Zone == EEFCalystoPlacementZone::CornerBottom ? 100
			: Zone == EEFCalystoPlacementZone::WallTop || Zone == EEFCalystoPlacementZone::CornerTop ? 300 : 200;
		Slot.Transform.SetLocation(IsFloor ? FVector::ZeroVector : IsRoof ? FVector(0, 0, 400) : FVector(200, IsCorner ? 200 : 0, Height));
		Slot.Normal = IsFloor ? FVector::UpVector : IsRoof ? -FVector::UpVector : IsCorner ? FVector(-1, -1, 0).GetSafeNormal() : -FVector::ForwardVector;
		auto& Rule = Rules.AddDefaulted_GetRef(); Rule.Zone = Zone; Rule.bOverrideDefaultChance = true; Rule.Chance.FirstPercent = Rule.Chance.LastPercent = 0;
		auto& E = Rule.Alternatives.AddDefaulted_GetRef(); E.Selection.Id = Id(200 + Z); E.Mesh = F.Cube; E.Transform.Scale = FVector(0.4);
		E.Transform.LocationOffset = IsFloor ? FVector(0, 0, 20) : IsRoof ? FVector(0, 0, -20) : FVector(-20, IsCorner ? -20 : 0, 0);
	}
	TArray<UObject*> Resources{F.Cube}; F.Request.LoadedArchitectureResources = Resources;
	if (!F.Compile(*this)) return false;
	FEFCalystoSurfaceCandidateResult R;
	if (!TestTrue(TEXT("Actual native slots support all eight architecture zones before zero Chance"), FEFCalystoSurfaceCandidates::Build(F.Request, R))) { AddError(R.Message); return false; }
	TestEqual(TEXT("Architecture uses eight native slots, not the content floor lattice"), R.ArchitectureOpportunities.Num(), 8);
	TestEqual(TEXT("Every zone has a complete physical receipt"), R.ArchitectureReceipts.Num(), 8);
	for (const auto& O : R.ArchitectureOpportunities)
	{
		const auto* Slot = F.Native.Surfaces.FindByPredicate([&](const auto& S) { return S.Zone == O.Zone; });
		TestTrue(TEXT("The native transform has no invented placement offset"), Slot && O.NativeTransform.Equals(Slot->Transform, 0));
		TestEqual(TEXT("Actual geometry establishes one compatible entry independently of Chance"), O.CompatibleEntryBounds.Num(), 1);
	}
	TArray<FGuid> Original; for (const auto& O : R.ArchitectureOpportunities) Original.Add(O.Id);
	Algo::Reverse(F.Native.Surfaces); Algo::Reverse(Rules); F.RoomConfig.RunSeed = 4444;
	for (auto& Rule : Rules) Rule.Chance.FirstPercent = Rule.Chance.LastPercent = 100;
	if (!F.Compile(*this)) return false;
	TestTrue(TEXT("Order, seed and Chance leave native opportunity identity unchanged"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TArray<FGuid> Reordered; for (const auto& O : R.ArchitectureOpportunities) Reordered.Add(O.Id); TestTrue(TEXT("Canonical native IDs survive reorder"), Original == Reordered);
	// Isolate one native floor slot. A yaw/translation range is proved continuously; no random sample stands in for it.
	F.Native.Surfaces.RemoveAll([](const auto& S) { return S.Zone != EEFCalystoPlacementZone::Floor; });
	Rules.RemoveAll([](const auto& Rule) { return Rule.Zone != EEFCalystoPlacementZone::Floor; });
	auto& E = Rules[0].Alternatives[0]; E.Rotation = EEFCalystoArchitectureRotation::Full360; E.Variation.LocationMaximum.X = 50;
	if (!F.Compile(*this)) return false;
	TestTrue(TEXT("Full native yaw and all tangent translations fit the actual floor"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	if (!TestEqual(TEXT("A complete variation envelope remains compatible"), R.ArchitectureReceipts.Num(), 1)) return false;
	TestTrue(TEXT("Yaw sweep includes diagonal radius and translated maximum"), R.ArchitectureReceipts[0].SweptWorldBounds.Max.X >= 78.28);
	auto* Obstacle = NewObject<UBoxComponent>(F.Owner); F.Owner->AddInstanceComponent(Obstacle); Obstacle->SetBoxExtent(FVector(4));
	Obstacle->SetWorldLocation(FVector(65, 0, 20)); Obstacle->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Obstacle->SetCollisionResponseToAllChannels(ECR_Block); Obstacle->RegisterComponent();
	TestTrue(TEXT("A real blocker in an unsampled possible variation is observed"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestTrue(TEXT("The whole alternative is removed before Chance, not moved or rerolled"), R.ArchitectureReceipts.IsEmpty());
	Obstacle->DestroyComponent(); E.Variation.LocationMaximum.Z = 10;
	if (!F.Compile(*this)) return false;
	TestTrue(TEXT("Floating endpoints reject without changing authored offsets"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestTrue(TEXT("Touching at one endpoint cannot prove grounded variation"), R.ArchitectureReceipts.IsEmpty());
	E.Variation.LocationMaximum = FVector::ZeroVector; E.Rotation = EEFCalystoArchitectureRotation::Degrees45;
	E.Variation.bUniformScale = false; E.Variation.ScaleMaximum.X = 1.5;
	if (!F.Compile(*this)) return false;
	TestTrue(TEXT("All eight discrete yaw angles and every nonuniform scale endpoint retain grounded support"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestEqual(TEXT("Discrete/full scale ranges are not replaced by one sampled transform"), R.ArchitectureReceipts.Num(), 1);
	E.Variation.RotationMaximum.Pitch = 5;
	if (!F.Compile(*this)) return false;
	TestFalse(TEXT("Unsupported continuous pitch is explicit rather than hidden reweighting"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestEqual(TEXT("Unsupported geometry reports its exact capability"), R.FailureCode, FName(TEXT("UnsupportedArchitectureRotation")));
	TestTrue(TEXT("Unsupported contract clears content and architecture atomically"), R.Surfaces.IsEmpty() && R.ArchitectureOpportunities.IsEmpty() && R.ArchitectureReceipts.IsEmpty());
	E.Variation.RotationMaximum.Pitch = 0; E.Payload = EEFCalystoArchitecturePayload::Actor; E.ActorClass = AActor::StaticClass();
	if (!F.Compile(*this)) return false;
	TestFalse(TEXT("Actor payloads need their actual tracked template contract"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestEqual(TEXT("Actor capability gap remains explicit"), R.FailureCode, FName(TEXT("UnsupportedArchitectureActor")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoBakedArchitectureSurfaceTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentSurfaces.BakedGeometryBeforeConditionalChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoBakedArchitectureSurfaceTest::RunTest(const FString&)
{
	using namespace EFCalystoSurfaceCandidateTests; FFixture F; if (!F.Initialize(*this)) return false;
	auto* Data = NewObject<UPCGDataAsset>(); auto* Root = NewObject<UPCGPointData>(Data); auto* Points = NewObject<UPCGPointData>(Data);
	Root->SetPoints({FPCGPoint()}); auto& TaggedRoot = Data->Data.TaggedData.AddDefaulted_GetRef(); TaggedRoot.Pin = TEXT("Root"); TaggedRoot.Data = Root;
	auto& TaggedPoints = Data->Data.TaggedData.AddDefaulted_GetRef(); TaggedPoints.Pin = TEXT("Points"); TaggedPoints.Data = Points;
	auto* M = Points->MutableMetadata();
	auto* Mesh = M->CreateAttribute<FSoftObjectPath>(PCGLevelToAssetConstants::MeshAttributeName, FSoftObjectPath(F.Cube), false, false);
	auto* Actor = M->CreateAttribute<int64>(PCGLevelToAssetConstants::ActorIndexAttributeName, 0, false, false);
	auto* Chance = M->CreateAttribute<bool>(TEXT("Chance50"), false, false, false);
	auto* Yaw = M->CreateAttribute<bool>(TEXT("RotateZ"), false, false, false);
	if (!TestTrue(TEXT("Actual typed native metadata attributes are constructed"), Mesh && Actor && Chance && Yaw)) return false;
	TArray<FPCGPoint> Children;
	for (int32 I = 0; I < 2; ++I)
	{
		auto& P = Children.AddDefaulted_GetRef(); P.MetadataEntry = M->AddEntry(); Actor->SetValue(P.MetadataEntry, I + 1);
		// Deliberately tiny export bounds cannot replace the cube's actual 100cm mesh/collision bounds.
		P.BoundsMin = FVector(-1); P.BoundsMax = FVector(1); P.Density = 1;
		P.Transform = FTransform(FQuat::Identity, I ? FVector(55, 0, 10) : FVector(0, 0, 20), FVector(I ? 0.2 : 0.4));
		Chance->SetValue(P.MetadataEntry, I == 1); Yaw->SetValue(P.MetadataEntry, I == 1);
	}
	Points->SetPoints(Children);
	auto& Slot = F.Native.Surfaces.AddDefaulted_GetRef(); Slot.RoomId = 1; Slot.Zone = EEFCalystoPlacementZone::Floor; Slot.Transform = FTransform::Identity; Slot.Normal = FVector::UpVector;
	auto& Rule = F.Asset->Styles[0].Architecture.Decoration.AddDefaulted_GetRef(); Rule.Zone = EEFCalystoPlacementZone::Floor;
	auto& E = Rule.Alternatives.AddDefaulted_GetRef(); E.Selection.Id = Id(500); E.Payload = EEFCalystoArchitecturePayload::BakedPCG; E.BakedPCG = Data;
	TArray<UObject*> Resources{Data, F.Cube}; F.Request.LoadedArchitectureResources = Resources;
	if (!F.Compile(*this)) return false;
	FEFCalystoSurfaceCandidateResult R;
	if (!TestTrue(TEXT("Baked native metadata uses loaded child geometry before any conditional trials"), FEFCalystoSurfaceCandidates::Build(F.Request, R))) { AddError(R.Message); return false; }
	if (!TestEqual(TEXT("The whole baked parent has one supported opportunity"), R.ArchitectureReceipts.Num(), 1)) return false;
	const auto& Receipt = R.ArchitectureReceipts[0]; TestEqual(TEXT("Both potential children retain exact mesh receipts before Chance50"), Receipt.Meshes.Num(), 2);
	TestTrue(TEXT("Actual cube bounds and full child yaw exceed the tiny export metadata"), Receipt.SweptWorldBounds.Max.X >= 69.14 && Receipt.SweptWorldBounds.Min.X <= -20);
	auto* Blocker = NewObject<UBoxComponent>(F.Owner); F.Owner->AddInstanceComponent(Blocker); Blocker->SetBoxExtent(FVector(2));
	Blocker->SetWorldLocation(FVector(68, 0, 10)); Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Blocker->SetCollisionResponseToAllChannels(ECR_Block); Blocker->RegisterComponent();
	TestTrue(TEXT("A blocker in an optional child's possible yaw is queried before parent and child trials"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestTrue(TEXT("Blocked optional child invalidates the parent, never silently drops the child"), R.ArchitectureReceipts.IsEmpty());
	Blocker->DestroyComponent();
	Chance->SetValue(Children[0].MetadataEntry, true);
	TestFalse(TEXT("All-conditional baked attachment cannot silently disappear from weighted alternatives"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestEqual(TEXT("Missing guaranteed attachment is an explicit unsupported contract"), R.FailureCode, FName(TEXT("UnsupportedArchitectureAttachment")));
	TestTrue(TEXT("Unsupported attachment clears the complete catalog atomically"), R.Surfaces.IsEmpty() && R.ArchitectureOpportunities.IsEmpty());
	Chance->SetValue(Children[0].MetadataEntry, false); Yaw->SetValue(Children[0].MetadataEntry, true);
	TestFalse(TEXT("All-yaw baked attachment also requires an explicit supported contact witness"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestEqual(TEXT("All-yaw attachment exposes the same precise capability gap"), R.FailureCode, FName(TEXT("UnsupportedArchitectureAttachment")));
	Yaw->SetValue(Children[0].MetadataEntry, false);
	Resources.Remove(F.Cube); F.Request.LoadedArchitectureResources = Resources;
	TestFalse(TEXT("An already globally loaded mesh outside the retained closure is insufficient"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestEqual(TEXT("The missing leased child dependency is explicit"), R.FailureCode, FName(TEXT("ArchitectureDependencyMissing")));
	TestTrue(TEXT("Dependency loss leaves no partial compatibility output"), R.ArchitectureOpportunities.IsEmpty() && R.Surfaces.IsEmpty());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoOriginalForgeSurfaceTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentSurfaces.OriginalForgeOnNativeFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoOriginalForgeSurfaceTest::RunTest(const FString&)
{
	using namespace EFCalystoSurfaceCandidateTests; FFixture F; if (!F.Initialize(*this)) return false;
	auto* FloorMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Floor.SM_Floor"));
	auto* Data = LoadObject<UPCGDataAsset>(nullptr, TEXT("/Game/Calysto/Dungeon/Demo/LevelInstance/PCGDA_Table.PCGDA_Table"));
	if (!TestNotNull(TEXT("Actual native floor mesh"), FloorMesh) || !TestNotNull(TEXT("Original Forge baked payload"), Data)) return false;
	F.Floor->SetStaticMesh(FloorMesh); F.Floor->ClearInstances();
	for (int32 Y = -1; Y <= 1; ++Y) for (int32 X = -1; X <= 1; ++X)
		F.Floor->AddInstance(FTransform(FVector(X * 300, Y * 300, 0)), true);
	F.Native.Rooms[0].LocalBounds = FBox(FVector(-600, -600, 0), FVector(600, 600, 0));
	F.Asset->Styles[0].Content.Reset();
	auto& Slot = F.Native.Surfaces.AddDefaulted_GetRef(); Slot.RoomId = 1; Slot.Zone = EEFCalystoPlacementZone::Floor;
	Slot.Transform = FTransform::Identity; Slot.Normal = FVector::UpVector;
	auto& Rule = F.Asset->Styles[0].Architecture.Decoration.AddDefaulted_GetRef(); Rule.Zone = EEFCalystoPlacementZone::Floor;
	auto& E = Rule.Alternatives.AddDefaulted_GetRef(); E.Selection.Id = Id(700); E.Payload = EEFCalystoArchitecturePayload::BakedPCG; E.BakedPCG = Data;
	TArray<FEFCalystoBakedMesh> Children; TArray<FSoftObjectPath> Dependencies; FString Error;
	if (!TestTrue(TEXT("Native Forge contract decodes before spatial selection"), FEFCalystoBakedArchitecture::Decode(*Data, Children, Dependencies, Error))) { AddError(Error); return false; }
	TArray<UObject*> Resources{Data, FloorMesh};
	for (const auto& Path : Dependencies)
	{
		auto* Resource = Path.TryLoad(); if (!TestNotNull(TEXT("Original payload dependency loaded by fixture"), Resource)) return false;
		Resources.Add(Resource);
	}
	F.Request.LoadedArchitectureResources = Resources;
	for (const auto& Child : Children) if (Child.ChancePercent == 100)
	{
		auto* Mesh = Cast<UStaticMesh>(Child.Mesh.ResolveObject()); const auto* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
		if (!Body) return false;
		FBox Collision = Body->AggGeom.CalcAABB(Child.LocalTransform);
		if (Body->GetCollisionTraceFlag() == CTF_UseComplexAsSimple)
		{
			Collision = FBox(ForceInit);
			for (const auto& Geometry : Body->TriMeshGeometries) if (Geometry)
				for (uint32 I = 0; I < Geometry->Particles().Size(); ++I) Collision += Child.LocalTransform.TransformPosition(FVector(Geometry->Particles().GetX(I)));
		}
		AddInfo(FString::Printf(TEXT("OriginalAttachment mesh=%s trace=%d collision_min_z=%.17g render_min_z=%.17g native_transform=%s"),
			*Child.Mesh.ToString(), int32(Body->GetCollisionTraceFlag()), Collision.Min.Z,
			Mesh->GetBoundingBox().TransformBy(Child.LocalTransform).Min.Z, *Child.LocalTransform.ToString()));
	}
	if (!F.Compile(*this) || !F.Observe(*this)) return false;
	FEFCalystoSurfaceCandidateResult R;
	if (!TestTrue(TEXT("Original native payload has a supported geometry contract"), FEFCalystoSurfaceCandidates::Build(F.Request, R)))
	{ AddError(R.FailureCode.ToString() + TEXT(": ") + R.Message); return false; }
	TestEqual(TEXT("Original table and every optional child fit on actual continuous native floor"), R.ArchitectureReceipts.Num(), 1);
	AddInfo(FString::Printf(TEXT("OriginalForgeSpatial opportunities=%d compatible=%d geometry_rejected=%d protected_rejected=%d queries=%d"),
		R.ArchitectureOpportunities.Num(), R.ArchitectureReceipts.Num(), R.RejectedGeometry, R.RejectedProtection, R.PhysicsQueries));
	if (!R.FirstArchitectureRejection.IsEmpty()) AddInfo(R.FirstArchitectureRejection);
	if (R.ArchitectureReceipts.Num() != 1) return false;
	TestTrue(TEXT("Native attachment reaches actual support trace and swept collision checks"), R.PhysicsQueries >= 2);
	F.Floor->RemoveInstance(4); // Center tile is genuinely absent, not a bounding-box hole annotation.
	if (!F.Observe(*this)) return false;
	TestTrue(TEXT("Missing native floor remains an ordinary compatibility decision"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestTrue(TEXT("Visual attachment evidence cannot invent physical floor across a hole"), R.ArchitectureReceipts.IsEmpty());
	F.Floor->AddInstance(FTransform::Identity, true);
	if (!F.Observe(*this)) return false;
	TestTrue(TEXT("Restored native support is re-observed without changing the slot or seed"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestEqual(TEXT("A different storage index does not remove compatible actual support"), R.ArchitectureReceipts.Num(), 1);
	const auto RebuildTiles = [&](bool Gap, bool Missing, bool Reversed)
	{
		F.Floor->ClearInstances(); TArray<FTransform> Tiles;
		for (int32 Y = -1; Y <= 1; ++Y) for (int32 X = -1; X <= 1; ++X)
			if (!Missing || X != 1 || Y != 0) Tiles.Add(FTransform(FVector(X * 300 + (Gap && X > 0 ? 0.1 : 0), Y * 300, 0)));
		if (Reversed) Algo::Reverse(Tiles);
		for (const auto& Tile : Tiles) F.Floor->AddInstance(Tile, true);
		return F.Observe(*this);
	};
	Slot.Transform.SetLocation(FVector(150, -100, 0)); // Native opportunity crosses both X and Y tile boundaries.
	if (!RebuildTiles(false, false, false)) return false;
	TestTrue(TEXT("Unmodified native Forge payload supports actual adjacent floor collision faces"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	if (!TestEqual(TEXT("A tile seam does not remove a physically supported architecture alternative"), R.ArchitectureReceipts.Num(), 1)) return false;
	TestEqual(TEXT("Every one of the four supporting tiles has a verified receipt"), R.ArchitectureReceipts[0].Supports.Num(), 4);
	const FGuid SeamOpportunity = R.ArchitectureReceipts[0].OpportunityId;
	const FBox SeamBounds = R.ArchitectureReceipts[0].SweptWorldBounds;
	if (!RebuildTiles(false, false, true)) return false;
	TestTrue(TEXT("Reordered native instance storage remains spatially feasible"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	if (!TestEqual(TEXT("Reordering keeps the exact compatible pair"), R.ArchitectureReceipts.Num(), 1)) return false;
	TestEqual(TEXT("Storage order cannot change architecture opportunity identity"), R.ArchitectureReceipts[0].OpportunityId, SeamOpportunity);
	TestTrue(TEXT("Storage order cannot move or shrink the native payload"), R.ArchitectureReceipts[0].SweptWorldBounds == SeamBounds);
	if (!RebuildTiles(true, false, false)) return false;
	TestTrue(TEXT("A real one-millimetre gap is evaluated before any random outcome"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestTrue(TEXT("Coplanar bounds cannot bridge a physical gap"), R.ArchitectureReceipts.IsEmpty());
	if (!RebuildTiles(false, true, false)) return false;
	TestTrue(TEXT("A missing corner remains an ordinary compatibility rejection"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestTrue(TEXT("Three remaining tiles cannot invent the missing fourth support quadrant"), R.ArchitectureReceipts.IsEmpty());
	if (!RebuildTiles(false, false, false)) return false;
	auto* Blocker = NewObject<UBoxComponent>(F.Owner); F.Owner->AddInstanceComponent(Blocker);
	Blocker->SetBoxExtent(FVector(5)); Blocker->SetWorldLocation(FVector(175, -100, 40));
	Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Blocker->SetCollisionResponseToAllChannels(ECR_Block); Blocker->RegisterComponent();
	TestTrue(TEXT("Complete multi-tile envelope still checks actual blocking actors"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestTrue(TEXT("Multi-tile support never exempts unrelated blocking geometry"), R.ArchitectureReceipts.IsEmpty());
	Blocker->DestroyComponent();
	E.Transform.LocationOffset.Z = 0.1; // Existing native 1.93752mm gap plus 1mm exceeds the 2.5mm contract.
	if (!F.Compile(*this)) return false;
	TestTrue(TEXT("Larger authored gaps are checked before architecture probability"), FEFCalystoSurfaceCandidates::Build(F.Request, R));
	TestTrue(TEXT("The native compatibility tolerance does not authorize visibly floating architecture"), R.ArchitectureReceipts.IsEmpty());
	return true;
}
#endif
