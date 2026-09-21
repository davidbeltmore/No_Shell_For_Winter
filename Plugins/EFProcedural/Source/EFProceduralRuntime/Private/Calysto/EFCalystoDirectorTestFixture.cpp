#include "Calysto/EFCalystoDirectorTestFixture.h"

#if !UE_BUILD_SHIPPING
#include "Calysto/EFCalystoDungeonDirectorAsset.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"

namespace EFCalystoDirectorTestFixture
{
	FGuid StyleId() { return FGuid(0x43414c59, 0x53544f4e, 0x41544956, 0x45535459); }

	static FEFCalystoSelection Selection(uint32 Entry, const TCHAR* Name)
	{
		FEFCalystoSelection S;
		S.Id = FGuid(0x43414c59, 0x50415249, 0x54594658, Entry);
		S.DisplayName = Name;
		S.Weight = 1.0;
		return S;
	}

	static FEFCalystoArchitectureEntry Mesh(uint32 Entry, const TCHAR* Name, const TCHAR* Path,
		FVector Scale = FVector::OneVector, FVector Offset = FVector::ZeroVector)
	{
		FEFCalystoArchitectureEntry E;
		E.Selection = Selection(Entry, Name);
		E.Payload = EEFCalystoArchitecturePayload::Mesh;
		E.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Path));
		E.Transform.bUniformScale = Scale.X == Scale.Y && Scale.Y == Scale.Z;
		E.Transform.Scale = Scale;
		E.Transform.LocationOffset = Offset;
		return E;
	}

	UEFCalystoDungeonDirectorAsset* MakeTransientFixture(bool bEnableNativeWallLights,bool bEnableReservedArchitecture)
	{
		if (!IsInGameThread()) return nullptr;
		auto* Asset = NewObject<UEFCalystoDungeonDirectorAsset>(GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UEFCalystoDungeonDirectorAsset::StaticClass(), TEXT("CalystoNativeParityFixture")), RF_Transient);
		Asset->Dungeon.GuaranteedThemedRooms = 1;
		Asset->Dungeon.AdditionalRoomThemeChancePercent = 25.0;
		Asset->Advanced.Adaptation.bEnabled = false;
		Asset->Advanced.MaximumAttempts = 4;
		Asset->Advanced.RequestDeadlineSeconds = 30.0;
		FEFCalystoStyle S;
		S.Selection = Selection(1, TEXT("Native Parity Fixture"));
		S.Selection.Id = StyleId();
		S.Layout.DungeonSize.Distribution = EEFCalystoDistribution::Fixed;
		S.Layout.DungeonSize.Value = 27.0;
		S.Layout.CandidateDensity.Distribution = EEFCalystoDistribution::Fixed;
		S.Layout.CandidateDensity.Value = 0.32;
		S.Layout.SidePathPercent.Distribution = EEFCalystoDistribution::Fixed;
		S.Layout.SidePathPercent.Value = 50.0;
		S.Layout.MinimumRoomSize = 4;
		S.Layout.MaximumRoomSize = 8;
		const TSoftObjectPtr<UMaterialInterface> Grey(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Material/MI_GreyTiles.MI_GreyTiles")));
		S.Materials.Floor = S.Materials.Wall = S.Materials.Roof = Grey;
		auto& A = S.Architecture;
		A.ProgressionDoorMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors.SM_SquaredArchedWoodenDoors")));
		A.Floor.Add(Mesh(10, TEXT("Native Floor"), TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Floor.SM_Floor")));
		A.Wall.Add(Mesh(11, TEXT("Native Wall"), TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Wall.SM_Wall"), FVector(1.01,1,1)));
		A.Roof.Add(Mesh(12, TEXT("Native Roof"), TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Roof.SM_Roof")));
		A.DoorFrames.Add(Mesh(13, TEXT("Native Door Frame"), TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_IronDoorFrameClean.SM_IronDoorFrameClean"), FVector(0.999802,1.832301,0.934894)));
		A.RampTop.Add(Mesh(14, TEXT("Native Ramp Top"), TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_RampBevel.SM_RampBevel"), FVector(1,1.5,1), FVector(0,5,0)));
		A.RampBottom.Add(Mesh(15, TEXT("Native Ramp Bottom"), TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_RampBevel.SM_RampBevel"), FVector(1.02,2,1)));
		FEFCalystoArchitectureEntry Door;
		Door.Selection = Selection(16, TEXT("Native Door"));
		Door.Payload = EEFCalystoArchitecturePayload::Actor;
		Door.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Blueprint/Utility/BP_Door.BP_Door_C")));
		Door.Transform.bUniformScale = false;
		Door.Transform.LocationOffset = FVector(-64.410089,0,0);
		Door.Transform.RotationOffset = FRotator(0,103.693343,0);
		Door.Transform.Scale = FVector(1.119666,1,1.136592);
		A.Doors.Add(Door);
		FEFCalystoDoorway Way;
		Way.Selection = Selection(17, TEXT("Native Wall Doorway"));
		Way.WallMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_WallDoor.SM_WallDoor")));
		Way.FrameMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_IronDoorFrameClean.SM_IronDoorFrameClean")));
		Way.DoorClass = Door.ActorClass;
		Way.DoorTransform.LocationOffset = FVector(-70.218424,0,0);
		Way.DoorTransform.RotationOffset = FRotator(0,124.242083,0);
		Way.DoorTransform.Scale = FVector(1.2,1.2,1.2);
		A.Doorways.Add(Way);
		// The fixture deliberately authors no optional content; no authored master is stripped or modified.
		S.Content.Reset();
		S.Decals.Mode = EEFCalystoDecalMode::Block;
		S.Lighting.WallLights.Reset();
		if (bEnableNativeWallLights)
		{
			FEFCalystoArchitectureEntry Light;
			Light.Selection = Selection(18, TEXT("Native Wall Torch"));
			Light.Payload = EEFCalystoArchitecturePayload::Actor;
			Light.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Blueprint/Lightning/BP_WallTorch.BP_WallTorch_C")));
			S.Lighting.WallLights.Add(MoveTemp(Light));
		}
		S.FloorBudgets.Enemies = S.FloorBudgets.LooseFood = S.FloorBudgets.Chests = 0;
		S.FloorBudgets.LootActors = S.FloorBudgets.SpecialEvents = S.FloorBudgets.TotalActors = 0;
		S.FloorBudgets.Threat.FirstValue = S.FloorBudgets.Threat.LastValue = 0.0;
		Asset->Styles.Add(MoveTemp(S));
		FEFCalystoTheme T;
		T.Selection = Selection(100, TEXT("Neutral Parity Theme"));
		T.Decals.Mode = EEFCalystoDecalMode::Block;
		if (bEnableReservedArchitecture)
		{
			FEFCalystoSurfaceDecoration Floor; Floor.Zone=EEFCalystoPlacementZone::Floor;
			Floor.bOverrideDefaultChance=true; Floor.Chance.FirstPercent=Floor.Chance.LastPercent=100;
			FEFCalystoArchitectureEntry Table; Table.Selection=Selection(101,TEXT("Native Baked Table"));
			Table.Payload=EEFCalystoArchitecturePayload::BakedPCG;
			Table.BakedPCG=TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Demo/LevelInstance/PCGDA_Table.PCGDA_Table")));
			Floor.Alternatives.Add(MoveTemp(Table)); T.Architecture.Add(MoveTemp(Floor));
		}
		// Inherit every material; optional architecture is an explicit fixture input, never a master override.
		Asset->RoomThemes.Add(MoveTemp(T));
		return Asset;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeParityFixtureTest,
	"NoShellForWinter.CalystoDungeon.Director.Native.TransientParityFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeParityFixtureTest::RunTest(const FString&)
{
	auto* A = EFCalystoDirectorTestFixture::MakeTransientFixture();
	auto* B = EFCalystoDirectorTestFixture::MakeTransientFixture();
	if (!TestNotNull(TEXT("Fixture exists"), A) || !TestNotNull(TEXT("Second fixture exists"), B)) return false;
	TestTrue(TEXT("Fixture belongs only to transient storage"), A->GetOutermost() == GetTransientPackage() && A->HasAnyFlags(RF_Transient));
	TestEqual(TEXT("Independent creations preserve Style identity"), A->Styles[0].Selection.Id, B->Styles[0].Selection.Id);
	TestEqual(TEXT("Independent creations preserve Theme identity"), A->RoomThemes[0].Selection.Id, B->RoomThemes[0].Selection.Id);
	TestEqual(TEXT("Fixed native parity size"), A->Styles[0].Layout.DungeonSize.Value, 27.0);
	TestTrue(TEXT("Explicitly empty gameplay fixture"), A->Styles[0].Content.IsEmpty() && A->Styles[0].Decals.Mode == EEFCalystoDecalMode::Block);
	const auto* Lit = EFCalystoDirectorTestFixture::MakeTransientFixture(true);
	TestTrue(TEXT("Unlit diagnostic fixture remains explicit"), A->Styles[0].Lighting.WallLights.IsEmpty());
	TestEqual(TEXT("Lit native fixture retains the same Style identity"), Lit->Styles[0].Selection.Id, A->Styles[0].Selection.Id);
	TestEqual(TEXT("Lit native fixture owns one authored native torch alternative"), Lit->Styles[0].Lighting.WallLights.Num(), 1);
	TestTrue(TEXT("Lit fixture still excludes population and decals"), Lit->Styles[0].Content.IsEmpty() && Lit->Styles[0].Decals.Mode == EEFCalystoDecalMode::Block);
	FEFCalystoCompiledDirector Compiled; TArray<FEFCalystoValidationIssue> Issues;
	const bool Valid = A->Compile(Compiled, Issues);
	TestTrue(TEXT("Fixture compiles through the new model"), Valid);
	for (const auto& Issue : Issues) AddError(Issue.Field + TEXT(": ") + Issue.Message);
	Issues.Reset();
	const bool LitValid = Lit->Compile(Compiled, Issues);
	TestTrue(TEXT("Lit fixture compiles through the same model"), LitValid);
	for (const auto& Issue : Issues) AddError(Issue.Field + TEXT(": ") + Issue.Message);
	return Valid && LitValid;
}
#endif
#endif
