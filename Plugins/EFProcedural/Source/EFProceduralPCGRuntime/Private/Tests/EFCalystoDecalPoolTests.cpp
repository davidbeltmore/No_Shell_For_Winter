#if WITH_DEV_AUTOMATION_TESTS

#include "Calysto/EFCalystoDecalPool.h"
#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Misc/AutomationTest.h"
#include "PCGComponent.h"

namespace EFCalystoDecalPoolTests
{
	FGuid Id(uint32 N) { return FGuid(0x44454341, 0x4C504F4F, 0, N); }
	struct FFixture
	{
		UWorld* World = nullptr; AActor* Root = nullptr; UPCGComponent* PCG = nullptr;
		AEFCalystoDecalPoolOwner* Pool = nullptr; UMaterial* Material = nullptr;
		TArray<UObject*> Resources; FEFCalystoCompiledDirector Config; FEFCalystoNativeRoomConfig RoomConfig;
		FEFCalystoNativeResult Native; TArray<FEFCalystoReservedDecal> Proposals;
		FFixture()
		{
			if (!GEngine) return;
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(false).RequiresHitProxies(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (World) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}
		~FFixture()
		{
			if (World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); World->MarkObjectsPendingKill(); }
		}
		bool Initialize(FAutomationTestBase& Test, int32 Count)
		{
			if (!Test.TestNotNull(TEXT("Isolated physics fixture exists"), World)) return false;
			Root = World->SpawnActor<AActor>(); Pool = World->SpawnActor<AEFCalystoDecalPoolOwner>();
			if (!Root || !Pool) return false;
			// PCG ignores its generated supports when deriving input bounds; register a real input volume first.
			auto* Scene = NewObject<UBoxComponent>(Root); Root->AddInstanceComponent(Scene); Root->SetRootComponent(Scene);
			Scene->SetBoxExtent(FVector(FMath::Max(Count, 1) * 1000.0, 500.0, 500.0));
			Scene->SetCollisionEnabled(ECollisionEnabled::NoCollision); Scene->SetGenerateOverlapEvents(false);
			Scene->RegisterComponent();
			PCG = NewObject<UPCGComponent>(Root); PCG->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
			Root->AddInstanceComponent(PCG); PCG->RegisterComponent();
			Material = NewObject<UMaterial>(); Material->MaterialDomain = MD_DeferredDecal;
			auto* Color = NewObject<UTexture2D>(); auto* Normal = NewObject<UTexture2D>(); Resources = {Material, Color, Normal};
			auto* Asset = NewObject<UEFCalystoDungeonDirectorAsset>(); auto& Style = Asset->Styles.AddDefaulted_GetRef();
			Style.Selection.Id = Id(1); Style.Selection.DisplayName = TEXT("Decal pool fixture");
			Style.Materials.Floor = Style.Materials.Wall = Style.Materials.Roof = TSoftObjectPtr<UMaterialInterface>(Material);
			FEFCalystoArchitectureEntry Mesh; Mesh.Selection.Id = Id(2); Mesh.Mesh = TSoftObjectPtr<UStaticMesh>(NewObject<UStaticMesh>());
			Style.Architecture.Floor.Add(Mesh); Mesh.Selection.Id = Id(3); Style.Architecture.Wall.Add(Mesh);
			Mesh.Selection.Id = Id(4); Style.Architecture.Roof.Add(Mesh);
			FEFCalystoDecalVariant Variant; Variant.Selection.Id = Id(5); Variant.Material = TSoftObjectPtr<UMaterialInterface>(Material);
			Variant.ColorTexture = TSoftObjectPtr<UTexture2D>(Color); Variant.NormalTexture = TSoftObjectPtr<UTexture2D>(Normal);
			Style.Decals.Variants.Add(Variant);
			auto& Theme = Asset->RoomThemes.AddDefaulted_GetRef(); Theme.Selection.Id = Id(6);
			TArray<FEFCalystoValidationIssue> Issues;
			if (!Asset->Compile(Config, Issues)) { for (const auto& I : Issues) Test.AddError(I.Field + TEXT(": ") + I.Message); return false; }
			RoomConfig.StyleId = Id(1); RoomConfig.FloorNumber = 1;
			Native.bGraphCompleted = true; Native.GenerateRequests = 1;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const auto Surface = Index < 3 ? EEFCalystoDecalSurface::Floor : Index < 7 ? EEFCalystoDecalSurface::Wall : EEFCalystoDecalSurface::Roof;
				const FVector NormalVector = Surface == EEFCalystoDecalSurface::Floor ? FVector::UpVector
					: Surface == EEFCalystoDecalSurface::Roof ? -FVector::UpVector : FVector::ForwardVector;
				const FVector Point(Index * 1000, 0, 0);
				auto* Box = NewObject<UBoxComponent>(Root); Root->AddInstanceComponent(Box); Box->SetupAttachment(Scene);
				Box->SetBoxExtent(FVector(200, 200, 8)); Box->SetWorldLocationAndRotation(Point - NormalVector * 8,
					FRotationMatrix::MakeFromZ(NormalVector).ToQuat());
				Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionObjectType(ECC_WorldStatic);
				Box->SetCollisionResponseToAllChannels(ECR_Block); Box->SetGenerateOverlapEvents(false);
				Box->ComponentTags.Add(TEXT("PCG Generated Component")); Box->RegisterComponent();
				auto& Room = Native.Rooms.AddDefaulted_GetRef(); Room.RoomId = Index + 1; Room.StyleId = Id(1);
				Room.Transform.SetLocation(Point); Room.LocalBounds = FBox(FVector(-250, -250, 0), FVector(250, 250, 0));
				auto& Opportunity = Native.Surfaces.AddDefaulted_GetRef(); Opportunity.RoomId = Room.RoomId; Opportunity.OpportunityId = 100 + Index;
				Opportunity.Zone = Surface == EEFCalystoDecalSurface::Floor ? EEFCalystoPlacementZone::Floor
					: Surface == EEFCalystoDecalSurface::Roof ? EEFCalystoPlacementZone::Roof : EEFCalystoPlacementZone::WallMiddle;
				Opportunity.Transform.SetLocation(Point); Opportunity.Normal = NormalVector;
				auto& P = Proposals.AddDefaulted_GetRef(); P.Id = Id(1000 + Index); P.RoomId = Room.RoomId;
				P.OpportunityId = Opportunity.OpportunityId; P.VariantId = Id(5); P.Surface = Surface;
				P.WorldTransform = FTransform(FRotationMatrix::MakeFromX(NormalVector).ToQuat(), Point);
				P.SurfaceNormal = NormalVector; P.Support = Box; P.SupportTransform = Box->GetComponentTransform();
				P.FootprintSupports.Add({Box, INDEX_NONE, Box->GetComponentTransform(), Point, NormalVector});
			}
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDecalPoolLifecycleTest,
	"NoShellForWinter.CalystoDungeon.Director.Decals.StagingCullingAndRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoDecalPoolLifecycleTest::RunTest(const FString&)
{
	using namespace EFCalystoDecalPoolTests; FFixture F; if (!F.Initialize(*this, 8)) return false;
	const FEFCalystoAttemptToken Token{Id(50), Id(51)}; FString Error;
	if (!TestTrue(TEXT("Exact eight reserved physical supports stage"), F.Pool->BeginObserved(Token, F.Config, F.RoomConfig,
		F.Root, F.PCG, F.Native, F.Proposals, F.Resources, Error))) { AddError(Error); return false; }
	TestEqual(TEXT("One owner preallocates exactly 24 reusable components"), F.Pool->Slots.Num(), 24);
	TestEqual(TEXT("Only shared material/color/normal references are retained"), F.Pool->ResourceLeases.Num(), 3);
	for (const UDecalComponent* C : F.Pool->Slots) TestFalse(TEXT("Begin never presents a staged decal"), C->IsVisible());
	TestTrue(TEXT("Exact replay is idempotent"), F.Pool->BeginObserved(Token, F.Config, F.RoomConfig,
		F.Root, F.PCG, F.Native, F.Proposals, F.Resources, Error));
	const FString Hash = F.Pool->GetProposalHash(); auto Changed = F.Proposals;
	Changed[0].WorldTransform.AddToTranslation(FVector(1e-7, 0, 0));
	TestFalse(TEXT("Even sub-display-precision proposal changes cannot replace an active lease"), F.Pool->BeginObserved(Token,
		F.Config, F.RoomConfig, F.Root, F.PCG, F.Native, Changed, F.Resources, Error));
	TestEqual(TEXT("Rejected replay preserves the original proposal hash"), F.Pool->GetProposalHash(), Hash);
	const TArray<FVector> AtOrigin = {FVector::ZeroVector};
	TestTrue(TEXT("Staged view update remains nonpresenting"), F.Pool->UpdateViewLocations(Token, AtOrigin, Error));
	TestFalse(TEXT("View positions cannot activate before commit"), F.Pool->Slots[0]->IsVisible());
	TestTrue(TEXT("Explicit committed activation verifies real components"), F.Pool->ActivateCommitted(Token, Error));
	TestTrue(TEXT("Committed near-camera visibility applies"), F.Pool->UpdateViewLocations(Token, AtOrigin, Error));
	TestTrue(TEXT("Near decal is actually visible"), F.Pool->Slots[0]->IsVisible());
	TestTrue(TEXT("Exactly at 4000 cm remains visible"), F.Pool->Slots[4]->IsVisible());
	TestFalse(TEXT("Beyond authored world cull distance is hidden"), F.Pool->Slots[5]->IsVisible());
	TestEqual(TEXT("Screen fading retains the authored native-pool default"), F.Pool->Slots[0]->FadeScreenSize, 0.01f);
	const TArray<FVector> TwoViews = {FVector::ZeroVector, FVector(7000, 0, 0)};
	TestTrue(TEXT("Any valid local camera can retain a nearby decal"), F.Pool->UpdateViewLocations(Token, TwoViews, Error));
	TestTrue(TEXT("Second-view nearby roof is actually visible"), F.Pool->Slots[7]->IsVisible());
	const TArray<FVector> NoViews;
	TestTrue(TEXT("No views hide presentation"), F.Pool->UpdateViewLocations(Token, NoViews, Error));
	TestFalse(TEXT("No-view behavior never leaves the last visibility latched"), F.Pool->Slots[0]->IsVisible());
	TestTrue(TEXT("Every exact realization remains verifiable"), F.Pool->Verify(Token, Error));
	F.Pool->Slots[0]->SetFadeScreenSize(0.02f);
	TestFalse(TEXT("External component fade mutation is detected"), F.Pool->Verify(Token, Error));
	TestFalse(TEXT("A stale token cannot release another attempt"), F.Pool->Release({Id(50), Id(99)}));
	UDecalComponent* Reused = F.Pool->Slots[0];
	TestTrue(TEXT("Release clears every leased slot/resource"), F.Pool->Release(Token));
	TestTrue(TEXT("Release is idempotent"), F.Pool->Release(Token));
	TestTrue(TEXT("Release receipt is based on actual hidden/material-free components"), F.Pool->GetReleaseEvidence(Token).bReleased);
	TestFalse(TEXT("An old released token cannot reactivate"), F.Pool->ActivateCommitted(Token, Error));
	const FEFCalystoAttemptToken Next{Id(50), Id(52)};
	TestTrue(TEXT("Next attempt reuses cleared pool"), F.Pool->BeginObserved(Next, F.Config, F.RoomConfig,
		F.Root, F.PCG, F.Native, F.Proposals, F.Resources, Error));
	TestEqual(TEXT("No extra component was allocated for another stain/floor"), F.Pool->Slots[0].Get(), Reused);
	F.Proposals[0].Support->AddWorldOffset(FVector(0, 0, 2));
	TestFalse(TEXT("Moved physical support invalidates the exact reservation"), F.Pool->Verify(Next, Error));
	TestTrue(TEXT("Rejected support still releases all leases"), F.Pool->Release(Next));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDecalPoolRejectionTest,
	"NoShellForWinter.CalystoDungeon.Director.Decals.ReservationRejectionIsAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoDecalPoolRejectionTest::RunTest(const FString&)
{
	using namespace EFCalystoDecalPoolTests; FFixture F; if (!F.Initialize(*this, 9)) return false;
	const FEFCalystoAttemptToken Token{Id(60), Id(61)}; FString Error;
	TestFalse(TEXT("Nine reservations cannot exceed the authored eight-active budget"), F.Pool->BeginObserved(Token,
		F.Config, F.RoomConfig, F.Root, F.PCG, F.Native, F.Proposals, F.Resources, Error));
	F.Proposals.SetNum(3); F.Proposals[2].RoomId = F.Proposals[1].RoomId;
	TestFalse(TEXT("Two decals cannot claim one room"), F.Pool->BeginObserved(Token,
		F.Config, F.RoomConfig, F.Root, F.PCG, F.Native, F.Proposals, F.Resources, Error));
	F.Proposals.SetNum(1);
	for (const auto Protection : {EEFCalystoProtectedRoom::Start, EEFCalystoProtectedRoom::End,
		EEFCalystoProtectedRoom::Critical, EEFCalystoProtectedRoom::Progression})
	{
		F.Native.Rooms[0].Protection = Protection;
		TestFalse(TEXT("Actual native protected-room record rejects presentation"), F.Pool->BeginObserved(Token,
			F.Config, F.RoomConfig, F.Root, F.PCG, F.Native, F.Proposals, F.Resources, Error));
	}
	F.Native.Rooms[0].Protection = EEFCalystoProtectedRoom::None; F.Native.Rooms[0].bMainPath = true;
	TestFalse(TEXT("Main path is protected without a proposal success flag"), F.Pool->BeginObserved(Token,
		F.Config, F.RoomConfig, F.Root, F.PCG, F.Native, F.Proposals, F.Resources, Error));
	F.Native.Rooms[0].bMainPath = false; F.Native.Rooms[0].bDoorClearance = true;
	TestFalse(TEXT("Native door clearance rejects"), F.Pool->BeginObserved(Token,
		F.Config, F.RoomConfig, F.Root, F.PCG, F.Native, F.Proposals, F.Resources, Error));
	F.Native.Rooms[0].bDoorClearance = false;
	F.Proposals[0].Support->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Named ownership without real physical support cannot pass"), F.Pool->BeginObserved(Token,
		F.Config, F.RoomConfig, F.Root, F.PCG, F.Native, F.Proposals, F.Resources, Error));
	F.Proposals[0].Support->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	F.Proposals[0].WorldTransform.AddToTranslation(FVector(0, 0, 4));
	TestFalse(TEXT("A trace near geometry cannot excuse the wrong selected position"), F.Pool->BeginObserved(Token,
		F.Config, F.RoomConfig, F.Root, F.PCG, F.Native, F.Proposals, F.Resources, Error));
	F.Proposals[0].WorldTransform.AddToTranslation(FVector(0, 0, -4));
	auto Missing = F.Resources; Missing.Pop();
	TestFalse(TEXT("Missing selected texture binding rejects before mutation"), F.Pool->BeginObserved(Token,
		F.Config, F.RoomConfig, F.Root, F.PCG, F.Native, F.Proposals, Missing, Error));
	TestEqual(TEXT("Rejected proposals retained no resource leases"), F.Pool->ResourceLeases.Num(), 0);
	TestEqual(TEXT("Rejected proposals allocated no extra slots"), F.Pool->Slots.Num(), 24);
	for (const UDecalComponent* C : F.Pool->Slots)
		TestTrue(TEXT("Every rejected batch leaves actual slots hidden and material-free"), !C->IsVisible() && !C->GetDecalMaterial());
	return true;
}
#endif
