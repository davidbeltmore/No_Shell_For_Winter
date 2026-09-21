#if WITH_DEV_AUTOMATION_TESTS
#include "Calysto/EFCalystoArchitectureMeshBatch.h"
#include "Calysto/EFCalystoArchitectureManifest.h"
#include "Algo/Reverse.h"
#include "PCGDataAsset.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoArchitectureMeshRealizationTest,
	"NoShellForWinter.CalystoDungeon.Director.Architecture.ExactMeshRealizationAndRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoArchitectureMeshRealizationTest::RunTest(const FString&)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).RequiresHitProxies(false).ShouldSimulatePhysics(false);
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!TestNotNull(TEXT("Actual isolated physics world"),World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	struct FCleanup { UWorld* World; ~FCleanup() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->MarkObjectsPendingKill(); } } Cleanup{World};
	UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Actual mesh resource"),Mesh) || !TestNotNull(TEXT("Actual authored material"),Mesh->GetMaterial(0))) return false;
	const FEFCalystoAttemptToken Token{FGuid(1,2,3,4),FGuid(5,6,7,8)}, Stale{Token.Request,FGuid(5,6,7,9)};
	AActor* Owner=World->SpawnActor<AActor>();
	TArray<FEFCalystoFrozenArchitectureMesh> Frozen;
	for (int32 I=0;I<3;++I)
	{
		auto& F=Frozen.AddDefaulted_GetRef(); F.Id=FGuid(3,4,5,I+1); F.ParentReservationId=FGuid(10,11,12,13);
		F.Mesh=FSoftObjectPath(Mesh); F.Materials={FSoftObjectPath(Mesh->GetMaterial(0))};
		F.WorldTransform=FTransform(FRotator(0,I*45,0),FVector(I*200,100,150),FVector(1+I*0.1,0.8,1.3));
	}
	const double Now=FPlatformTime::Seconds(), Deadline=Now+30; FString Error; TSet<FGuid> Verified;
	FEFCalystoArchitectureMeshBatch Batch;
	if (!TestTrue(TEXT("Frozen selected meshes begin without spawning an actor"),Batch.Begin(Token,World,Owner,Frozen,Deadline,Error)))
	{ AddError(Error); return false; }
	struct FRelease { FEFCalystoArchitectureMeshBatch& Batch; FEFCalystoAttemptToken Token; ~FRelease() { Batch.Release(Token); } } Release{Batch,Token};
	TestEqual(TEXT("Begin only retains resources, no components"),Batch.GetOwnedComponentCount(),0);
	TestTrue(TEXT("One bounded operation materializes one selected instance"),Batch.Prepare(Now,1,Error));
	TestEqual(TEXT("Partial batch remains preparing"),Batch.GetState(),EEFCalystoArchitectureMeshState::Preparing);
	TestFalse(TEXT("Partial instances cannot produce complete verification"),Batch.Verify(Verified,Error));
	TestTrue(TEXT("Identical request is idempotent while preparing"),Batch.Begin(Token,World,Owner,Frozen,Deadline,Error));
	Batch.Release(Stale);
	TestEqual(TEXT("A stale cleanup cannot destroy current instances"),Batch.GetOwnedComponentCount(),1);
	if (!TestTrue(TEXT("Remaining bounded preparation succeeds"),Batch.Prepare(Now,2,Error))) { AddError(Error); return false; }
	TestEqual(TEXT("Three same-mesh/material children share one native ISM component"),Batch.GetOwnedComponentCount(),1);
	TestTrue(TEXT("Actual collision, navigation relevance, materials and transforms verify"),Batch.Verify(Verified,Error));
	TestEqual(TEXT("Every requested child is verified"),Verified.Num(),3);
	TInlineComponentArray<UInstancedStaticMeshComponent*> Components(Owner);
	if (!TestEqual(TEXT("Owner has exactly one actual component"),Components.Num(),1)) return false;
	TestFalse(TEXT("Staged mesh instances remain invisible"),Components[0]->IsVisible());
	TestFalse(TEXT("Stale activation is ignored"),Batch.Activate(Stale,Error));
	TestEqual(TEXT("Stale activation preserves the prepared owner"),Batch.GetState(),EEFCalystoArchitectureMeshState::Prepared);
	TestTrue(TEXT("Exact accepted token activates the verified batch"),Batch.Activate(Token,Error));
	TestTrue(TEXT("Accepted native instances are visible"),Components[0]->IsVisible());
	TestTrue(TEXT("Repeat activation is idempotent"),Batch.Activate(Token,Error));
	Components[0]->UpdateInstanceTransform(1,FTransform(FVector(900,900,900)),true,true);
	TestFalse(TEXT("Moved selected instance invalidates actual evidence"),Batch.Verify(Verified,Error));
	TestTrue(TEXT("Changed batch publishes no partially verified identity subset"),Verified.IsEmpty());
	Batch.Release(Token);
	TestTrue(TEXT("Whole token cleanup releases all instances, components and resource references"),Batch.GetReleaseEvidence().IsComplete());
	TestFalse(TEXT("Released native component is registered"),Components[0]->IsRegistered());
	TestEqual(TEXT("Released native component retains no instance data"),Components[0]->GetInstanceCount(),0);
	TestEqual(TEXT("Owner no longer retains the batch component"),Owner->GetInstanceComponents().Num(),0);
	TestTrue(TEXT("A new batch can reuse released ownership"),Batch.Begin(Token,World,Owner,Frozen,Deadline,Error));
	TestFalse(TEXT("Expired preparation creates no partial acceptance"),Batch.Prepare(Deadline,1,Error));
	Batch.Release(Token);
	TestTrue(TEXT("Deadline rejection also releases exact ownership"),Batch.GetReleaseEvidence().IsComplete());
	Frozen[0].Materials[0]=FSoftObjectPath(TEXT("/Game/Missing/NoSubstitution.NoSubstitution"));
	TestFalse(TEXT("Missing selected material rejects before any component creation"),Batch.Begin(Token,World,Owner,Frozen,Deadline,Error));
	TestEqual(TEXT("Resource failure cannot spawn a substitute"),Batch.GetOwnedComponentCount(),0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoArchitectureManifestRealizationTest,
	"NoShellForWinter.CalystoDungeon.Director.Architecture.NativeManifestToInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoArchitectureManifestRealizationTest::RunTest(const FString&)
{
	const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).RequiresHitProxies(false).ShouldSimulatePhysics(false);
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
	if (!TestNotNull(TEXT("Isolated native realization world"),World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	struct FCleanup { UWorld* World; ~FCleanup() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->MarkObjectsPendingKill(); } } Cleanup{World};
	auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto* Table=LoadObject<UPCGDataAsset>(nullptr,TEXT("/Game/Calysto/Dungeon/Demo/LevelInstance/PCGDA_Table.PCGDA_Table"));
	if (!TestNotNull(TEXT("Real mesh loads"),Cube) || !TestNotNull(TEXT("Original native baked payload loads read-only"),Table)) return false;
	TArray<FEFCalystoBakedMesh> Children; TArray<FSoftObjectPath> Dependencies; FString Error;
	if (!FEFCalystoBakedArchitecture::Decode(*Table,Children,Dependencies,Error)) { AddError(Error); return false; }
	// Test setup loads the actual closure; production obtains the same objects from its retained async leases.
	for (const auto& Path:Dependencies) if (!TestNotNull(TEXT("Exact native baked dependency"),Path.TryLoad())) return false;
	FEFCalystoRandomKey Key; Key.RunSeed=730; Key.StyleId=FGuid(21,22,23,24);
	TArray<FEFCalystoArchitectureDecision> Decisions;
	auto& Mesh=Decisions.AddDefaulted_GetRef(); Mesh.OpportunityId=FGuid(31,32,33,34);
	Mesh.RoomId=10; Mesh.Outcome=EEFCalystoArchitectureOutcome::Reserved;
	Mesh.Entry.Selection.Id=FGuid(41,42,43,44); Mesh.Entry.Mesh=Cube;
	Mesh.WorldTransform=FTransform(FRotator(0,45,0),FVector(500,100,200),FVector(1.3,0.8,1.2));
	Mesh.ReservedBounds=Cube->GetBoundingBox().TransformBy(Mesh.WorldTransform).ExpandBy(1);
	auto& Baked=Decisions.AddDefaulted_GetRef(); Baked.OpportunityId=FGuid(51,52,53,54);
	Baked.RoomId=10; Baked.Outcome=EEFCalystoArchitectureOutcome::Reserved;
	Baked.Entry.Selection.Id=FGuid(61,62,63,64); Baked.Entry.Payload=EEFCalystoArchitecturePayload::BakedPCG; Baked.Entry.BakedPCG=Table;
	Baked.WorldTransform=FTransform(FRotator(0,90,0),FVector(1000,0,0),FVector::OneVector);
	Baked.ReservedBounds=FBox(FVector(-2000),FVector(4000));
	FEFCalystoArchitectureManifest Manifest;
	if (!TestTrue(TEXT("Reserved native payload freezes all child selections before spawning"),FEFCalystoArchitectureManifest::Build(Key,Decisions,Manifest,Error)))
	{ AddError(Error); return false; }
	TestEqual(TEXT("Both selected native parents survive"),Manifest.GetParents().Num(),2);
	TestTrue(TEXT("Mandatory table and standalone mesh both materialize"),Manifest.GetMeshes().Num()>=2);
	for (const auto& Parent:Manifest.GetParents()) if (!Parent.BakedChildren.IsEmpty())
		TestEqual(TEXT("All nine authored child trials, including absences, remain observable"),Parent.BakedChildren.Num(),9);
	const FString OriginalHash=Manifest.GetHash(); Algo::Reverse(Decisions);
	for (auto& D:Decisions) D.Entry.Selection.DisplayName=TEXT("Renamed display label");
	FEFCalystoArchitectureManifest Reordered;
	TestTrue(TEXT("Reordered/renamed native reservations freeze"),FEFCalystoArchitectureManifest::Build(Key,Decisions,Reordered,Error));
	TestEqual(TEXT("Labels/order cannot change exact frozen children or hash"),Reordered.GetHash(),OriginalHash);
	const FEFCalystoAttemptToken Token{FGuid(71,72,73,74),FGuid(81,82,83,84)};
	AActor* Owner=World->SpawnActor<AActor>(); FEFCalystoArchitectureMeshBatch Batch;
	struct FRelease { FEFCalystoArchitectureMeshBatch& B; FEFCalystoAttemptToken T; ~FRelease() { B.Release(T); } } Release{Batch,Token};
	const double Now=FPlatformTime::Seconds();
	if (!TestTrue(TEXT("Production batch consumes exact frozen native meshes/materials"),Batch.Begin(Token,World,Owner,Manifest.GetMeshes(),Now+30,Error))
		|| !TestTrue(TEXT("Production batch realizes selected native children"),Batch.Prepare(Now,64,Error))) { AddError(Error); return false; }
	TSet<FGuid> Actual,Verified;
	if (!Batch.Verify(Actual,Error)) { AddError(Error); return false; }
	TestTrue(TEXT("Every selected child proves both parent reservations"),Manifest.VerifyRealizedChildren(Actual,Verified,Error));
	TestEqual(TEXT("Reserved and verified identities match exactly"),Verified.Num(),Manifest.GetReservedElements().Num());
	Actual.Remove(Manifest.GetMeshes()[0].Id);
	TestFalse(TEXT("Missing one child rejects the entire parent and floor manifest"),Manifest.VerifyRealizedChildren(Actual,Verified,Error));
	TestTrue(TEXT("No partial parent success is published"),Verified.IsEmpty());
	Batch.Release(Token); TestTrue(TEXT("Actual native baked child ownership cleans up completely"),Batch.GetReleaseEvidence().IsComplete());
	Decisions[0].ReservedBounds=FBox(FVector::ZeroVector,FVector(1));
	TestFalse(TEXT("Insufficient preselected capacity never samples then drops a native child"),FEFCalystoArchitectureManifest::Build(Key,Decisions,Reordered,Error));
	TestFalse(TEXT("Failed freezing publishes no usable partial manifest"),Reordered.IsValid());
	return true;
}
#endif
