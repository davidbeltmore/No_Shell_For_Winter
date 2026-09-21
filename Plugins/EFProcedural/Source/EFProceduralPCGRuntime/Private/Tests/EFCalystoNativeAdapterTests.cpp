#if WITH_DEV_AUTOMATION_TESTS
#include "Calysto/EFCalystoNativeAdapter.h"
#include "Data/PCGPointData.h"
#include "Elements/Metadata/PCGMetadataMathsOpElement.h"
#include "Elements/PCGReroute.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"
#include "Misc/AutomationTest.h"
#include "PCGContext.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSubgraph.h"
#include "UObject/Package.h"
#include <limits>

namespace EFCalystoNativeTests
{
	static FEFCalystoNativeRoomConfig Config()
	{
		FEFCalystoNativeRoomConfig C;
		C.RunSeed = 709202607; C.TopologySeed = 1779679224; C.StyleId = FGuid(1,2,3,4);
		UClass* Schema = LoadClass<UObject>(nullptr, TEXT("/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes.PDA_RoomMeshes_C"));
		C.EmptyRoomSchema = Schema ? Schema->GetDefaultObject() : nullptr;
		C.FloorMaterial = FSoftObjectPath(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		C.WallMaterial = C.RoofMaterial = C.FloorMaterial;
		FEFCalystoNativeTheme Theme; Theme.Id = FGuid(5,6,7,8); Theme.RoomSchema = C.EmptyRoomSchema;
		Theme.FloorMaterial = Theme.WallMaterial = Theme.RoofMaterial = C.FloorMaterial;
		C.Themes.Add(Theme);
		return C;
	}
	static void Input(FPCGDataCollection& Data, FName Pin, TArray<int32> Positions)
	{
		UPCGPointData* Points = NewObject<UPCGPointData>();
		for (int32 X : Positions)
		{
			FPCGPoint P; P.Transform.SetLocation(FVector(X,0,0));
			P.BoundsMin = FVector(-200,-200,-50); P.BoundsMax = FVector(200,200,50);
			P.MetadataEntry = Points->MutableMetadata()->AddEntry(); Points->GetMutablePoints().Add(P);
		}
		FPCGTaggedData& T = Data.TaggedData.Emplace_GetRef(); T.Pin = Pin; T.Data = Points;
	}
	static bool Run(const FEFCalystoNativeRoomConfig& C, bool Reverse, FPCGDataCollection& Output)
	{
		UPCGGraph* Graph = NewObject<UPCGGraph>();
		auto* Settings = NewObject<UPCGCalystoDirectorRoomSettings>(Graph); Settings->Config = C;
		UPCGNode* Node = Graph->AddNode(Settings);
		FPCGDataCollection Inputs;
		Input(Inputs, FEFCalystoNativeRoomPins::MainRooms, Reverse ? TArray<int32>{2000,1000,0} : TArray<int32>{0,1000,2000});
		Input(Inputs, FEFCalystoNativeRoomPins::SideRooms, {1000}); // Native duplicate must not duplicate construction.
		Input(Inputs, FEFCalystoNativeRoomPins::StartRooms, {0});
		Input(Inputs, FEFCalystoNativeRoomPins::EndRooms, {2000});
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Inputs, {}, Node)));
		// Match UE 5.8 PCGTestsCommon::InitializeTestContext before direct execution.
		Context->InitializeSettings();
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Step = 0; Step < 8; ++Step)
			if (Element->Execute(Context.Get())) { Output = Context->OutputData; return true; }
		return false;
	}
	static bool RunRoomOverrideOwner(const FPCGDataCollection& InputData, FPCGDataCollection& Output)
	{
		UPCGGraph* Graph = NewObject<UPCGGraph>();
		auto* Settings = NewObject<UPCGCalystoRoomOverrideOwnerSettings>(Graph);
		UPCGNode* Node = Graph->AddNode(Settings);
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&InputData, {}, Node)));
		Context->InitializeSettings();
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Step = 0; Step < 8; ++Step)
			if (Element->Execute(Context.Get())) { Output = Context->OutputData; return !Output.TaggedData.IsEmpty(); }
		return false;
	}
	static bool RunDoorMaterialProvenance(const FPCGDataCollection& InputData, FPCGDataCollection& Output)
	{
		UPCGGraph* Graph = NewObject<UPCGGraph>();
		auto* Settings = NewObject<UPCGCalystoDoorMaterialProvenanceSettings>(Graph);
		UPCGNode* Node = Graph->AddNode(Settings);
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&InputData, {}, Node)));
		Context->InitializeSettings();
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Step = 0; Step < 8; ++Step)
			if (Element->Execute(Context.Get())) { Output = Context->OutputData; return !Output.TaggedData.IsEmpty(); }
		return false;
	}
	static bool RunDoorMaterialCarrier(const FPCGDataCollection& InputData, FPCGDataCollection& Output)
	{
		UPCGGraph* Graph = NewObject<UPCGGraph>();
		auto* Settings = NewObject<UPCGCalystoDoorMaterialCarrierSettings>(Graph);
		UPCGNode* Node = Graph->AddNode(Settings);
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&InputData, {}, Node)));
		Context->InitializeSettings();
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Step = 0; Step < 8; ++Step)
			if (Element->Execute(Context.Get())) { Output = Context->OutputData; return !Output.TaggedData.IsEmpty(); }
		return false;
	}
	static bool RunStyleWallFallback(const FPCGDataCollection& InputData, const FSoftObjectPath& StyleWallMaterial, FPCGDataCollection& Output)
	{
		UPCGGraph* Graph = NewObject<UPCGGraph>();
		auto* Settings = NewObject<UPCGCalystoStyleWallFallbackSettings>(Graph);
		Settings->StyleWallMaterial = StyleWallMaterial;
		UPCGNode* Node = Graph->AddNode(Settings);
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&InputData, {}, Node)));
		Context->InitializeSettings();
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Step = 0; Step < 8; ++Step)
			if (Element->Execute(Context.Get())) { Output = Context->OutputData; return !Output.TaggedData.IsEmpty(); }
		return false;
	}

	static UPCGPointData* FirstOutputPoints(const FPCGDataCollection& Data)
	{
		const TArray<FPCGTaggedData>& Tagged = Data.GetInputsByPin(PCGPinConstants::DefaultOutputLabel);
		return Tagged.Num() == 1 ? const_cast<UPCGPointData*>(Cast<UPCGPointData>(Tagged[0].Data)) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeCompleteRoomStreamTest,
	"NoShellForWinter.CalystoDungeon.Director.Native.CompleteRoomStream",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeCompleteRoomStreamTest::RunTest(const FString&)
{
	auto C = EFCalystoNativeTests::Config(); FString Error;
	if (!TestTrue(TEXT("Native schema fixture validates"), C.Validate(Error))) { AddError(Error); return false; }
	FPCGDataCollection A, B;
	if (!TestTrue(TEXT("Room node completes within eight bounded phases"), EFCalystoNativeTests::Run(C, false, A))) return false;
	TArray<FEFCalystoNativeRoom> Rooms;
	if (!TestTrue(TEXT("Typed room output validates"), FEFCalystoNativeRoomReader::Read(A,C,Rooms,Error))) { AddError(Error); return false; }
	TestEqual(TEXT("One copy of every native room survives"), Rooms.Num(), 3);
	int32 NativeCount = 0, ThemedCount = 0;
	for (const auto& T : A.GetInputsByPin(FEFCalystoNativeRoomPins::NativeRooms))
		if (const auto* P = Cast<UPCGPointData>(T.Data)) NativeCount += P->GetNumPoints();
	TestEqual(TEXT("Construction receives Start, End and the eligible room"), NativeCount, 3);
	for (const auto& R : Rooms)
	{
		ThemedCount += R.ThemeId.IsValid();
		if (R.Protection != EEFCalystoProtectedRoom::None) TestFalse(TEXT("Protected room remains unthemed"), R.ThemeId.IsValid());
	}
	TestEqual(TEXT("One eligible room is guaranteed"), ThemedCount, 1);
	TestTrue(TEXT("Reordered inputs finish"), EFCalystoNativeTests::Run(C, true, B));
	TArray<FEFCalystoNativeRoom> Reordered;
	TestTrue(TEXT("Reordered contexts validate"), FEFCalystoNativeRoomReader::Read(B,C,Reordered,Error));
	if (Rooms.Num() == Reordered.Num()) for (int32 I = 0; I < Rooms.Num(); ++I)
	{
		TestEqual(TEXT("Stable room identity ignores input order"), Rooms[I].RoomId, Reordered[I].RoomId);
		TestEqual(TEXT("Theme selection ignores input order"), Rooms[I].ThemeId, Reordered[I].ThemeId);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeGraphRoomContractTest,
	"NoShellForWinter.CalystoDungeon.Director.Native.GraphRoomContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeGraphRoomContractTest::RunTest(const FString&)
{
	UPCGGraph* Source = LoadObject<UPCGGraph>(nullptr, TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.PCG_MassiveDungeonMaster"));
	if (!TestNotNull(TEXT("Exact native Master exists"), Source)) return false;
	const bool DirtyBefore = Source->GetOutermost()->IsDirty();
	auto C = EFCalystoNativeTests::Config(); FString Error;
	UPCGGraph* Clone = FEFCalystoNativeAdapter::ComposeGraph(Source, GetTransientPackage(), C, Error);
	if (!TestNotNull(TEXT("Exact native graph composition succeeds"), Clone)) { AddError(Error); return false; }
	TestTrue(TEXT("Complete room stream validates"), FEFCalystoNativeAdapter::ValidateGraph(Clone, Error));
	TestEqual(TEXT("Vendor dirty state preserved"), Source->GetOutermost()->IsDirty(), DirtyBefore);
	UPCGNode* ShapeNode = nullptr;
	for (UPCGNode* N : Clone->GetNodes()) if (N && N->GetFName() == TEXT("Subgraph_0")) ShapeNode = N;
	const auto* Call = ShapeNode ? Cast<UPCGSubgraphSettings>(ShapeNode->GetSettings()) : nullptr;
	UPCGNode* Rooms = nullptr; UPCGNode* Merge = nullptr;
	if (Call && Call->GetSubgraph()) for (UPCGNode* N : Call->GetSubgraph()->GetNodes())
	{
		if (N && Cast<UPCGCalystoDirectorRoomSettings>(N->GetSettings())) Rooms = N;
		if (N && N->GetFName() == TEXT("MergePoints_48")) Merge = N;
	}
	if (!TestNotNull(TEXT("Room node exists"), Rooms) || !TestNotNull(TEXT("Native merge exists"), Merge)) return false;
	Merge->GetInputPin(TEXT("In"))->BreakEdgeTo(Rooms->GetOutputPin(FEFCalystoNativeRoomPins::NativeRooms));
	TestFalse(TEXT("Missing complete construction stream is rejected"), FEFCalystoNativeAdapter::ValidateGraph(Clone, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeWallOwnerProvenanceTest,
	"NoShellForWinter.CalystoDungeon.Director.Native.WallOwnerProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeWallOwnerProvenanceTest::RunTest(const FString&)
{
	FPCGDataCollection Input;
	UPCGPointData* RoomOverride = NewObject<UPCGPointData>();
	auto* OwnerIds = RoomOverride->MutableMetadata()->CreateAttribute<int64>(TEXT("EF_RoomId"), 0, false, false);
	auto* OwnerMaterials = RoomOverride->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("WallMaterial"), FSoftObjectPath{}, false, false);
	if (!TestNotNull(TEXT("RoomOverride fixture has a typed owner"), OwnerIds)
		|| !TestNotNull(TEXT("RoomOverride fixture has a typed frozen wall material"), OwnerMaterials)) return false;
	UMaterialInterface* OwnerMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInterface* SourceMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	if (!TestNotNull(TEXT("RoomOverride fixture resolves its frozen wall material"), OwnerMaterial)
		|| !TestNotNull(TEXT("Piece fixture resolves a distinct native wall material"), SourceMaterial)) return false;
	const FSoftObjectPath OwnerMaterialPath(OwnerMaterial);
	const FSoftObjectPath SourceMaterialPath(SourceMaterial);
	if (!TestTrue(TEXT("RoomOverride fixture distinguishes the active room material from native source material"), OwnerMaterialPath != SourceMaterialPath)) return false;
	FPCGPoint OwnerPoint; OwnerPoint.Transform.SetLocation(FVector(10, 20, 30));
	OwnerPoint.MetadataEntry = RoomOverride->MutableMetadata()->AddEntry(); OwnerIds->SetValue(OwnerPoint.MetadataEntry, 73); OwnerMaterials->SetValue(OwnerPoint.MetadataEntry, OwnerMaterialPath);
	RoomOverride->GetMutablePoints().Add(OwnerPoint);
	FPCGTaggedData& OwnerInput = Input.TaggedData.Emplace_GetRef();
	OwnerInput.Pin = FEFCalystoRoomOverrideOwnerPins::RoomOverride; OwnerInput.Data = RoomOverride;

	UPCGPointData* Pieces = NewObject<UPCGPointData>();
	auto* ExistingIds = Pieces->MutableMetadata()->CreateAttribute<int64>(TEXT("EF_RoomId"), 0, false, false);
	auto* Meshes = Pieces->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath{}, false, false);
	auto* Materials = Pieces->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("WallMaterial"), FSoftObjectPath{}, false, false);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Piece fixture has typed native ownership"), ExistingIds)
		|| !TestNotNull(TEXT("Piece fixture has typed mesh provenance"), Meshes)
		|| !TestNotNull(TEXT("Piece fixture has typed wall material provenance"), Materials)
		|| !TestNotNull(TEXT("Piece fixture resolves its mesh"), Mesh)) return false;
	const FSoftObjectPath MeshPath(Mesh);
	int32 PieceIndex = 0;
	for (const int64 ExistingId : {int64(0), int64(99)})
	{
		FPCGPoint Point;
		Point.Transform = FTransform(FRotator(0, 25 * PieceIndex, 0), FVector(100 + ExistingId, 20 * PieceIndex, 10), FVector(1.0 + PieceIndex, 1.5, 0.75));
		Point.MetadataEntry = Pieces->MutableMetadata()->AddEntry(); ExistingIds->SetValue(Point.MetadataEntry, ExistingId);
		Meshes->SetValue(Point.MetadataEntry, MeshPath); Materials->SetValue(Point.MetadataEntry, SourceMaterialPath);
		Pieces->GetMutablePoints().Add(Point);
		++PieceIndex;
	}
	FPCGTaggedData& PiecesInput = Input.TaggedData.Emplace_GetRef();
	PiecesInput.Pin = FEFCalystoRoomOverrideOwnerPins::Pieces; PiecesInput.Data = Pieces;
	PiecesInput.Tags.Add(TEXT("WallOverride")); PiecesInput.Tags.Add(TEXT("NativeDoorPiece"));
	FPCGDataCollection Output;
	if (!TestTrue(TEXT("Exact loop owner copy completes"), EFCalystoNativeTests::RunRoomOverrideOwner(Input, Output))) return false;
	UPCGPointData* Owned = EFCalystoNativeTests::FirstOutputPoints(Output);
	if (!TestNotNull(TEXT("Exact loop owner copy produces the final piece stream"), Owned)) return false;
	const TArray<FPCGTaggedData>& OwnedTagged = Output.GetInputsByPin(PCGPinConstants::DefaultOutputLabel);
	if (!TestEqual(TEXT("Exact loop owner copy keeps one final tagged piece stream"), OwnedTagged.Num(), 1)
		|| !TestEqual(TEXT("Exact loop owner copy preserves final piece cardinality"), Owned->GetNumPoints(), Pieces->GetNumPoints())) return false;
	TestEqual(TEXT("Exact loop owner copy preserves final piece tag count"), OwnedTagged[0].Tags.Num(), PiecesInput.Tags.Num());
	TestTrue(TEXT("Exact loop owner copy preserves the native wall tags"),
		OwnedTagged[0].Tags.Contains(TEXT("WallOverride")) && OwnedTagged[0].Tags.Contains(TEXT("NativeDoorPiece")));
	const auto* OutputIds = Owned->ConstMetadata()->GetConstTypedAttribute<int64>(TEXT("EF_RoomId"));
	const auto* OutputMeshes = Owned->ConstMetadata()->GetConstTypedAttribute<FSoftObjectPath>(TEXT("Mesh"));
	const auto* OutputMaterials = Owned->ConstMetadata()->GetConstTypedAttribute<FSoftObjectPath>(TEXT("WallMaterial"));
	if (!TestNotNull(TEXT("Exact loop owner copy emits typed EF_RoomId"), OutputIds)
		|| !TestNotNull(TEXT("Exact loop owner copy preserves typed mesh provenance"), OutputMeshes)
		|| !TestNotNull(TEXT("Exact loop owner copy preserves typed wall material provenance"), OutputMaterials)) return false;
	const FConstPCGPointValueRanges PieceRanges(Pieces);
	const FConstPCGPointValueRanges OwnedRanges(Owned);
	for (int32 Index = 0; Index < Pieces->GetNumPoints(); ++Index)
	{
		const FPCGPoint InputPoint = PieceRanges.GetPoint(Index);
		const FPCGPoint OutputPoint = OwnedRanges.GetPoint(Index);
		TestTrue(TEXT("Exact loop owner copy preserves each native transform"), OutputPoint.Transform.Equals(InputPoint.Transform, 0.0));
		TestEqual(TEXT("Every final wall piece receives the exact active RoomOverride identity despite source ownership"), OutputIds->GetValueFromItemKey(OutputPoint.MetadataEntry), int64(73));
		TestTrue(TEXT("Exact loop owner copy preserves each native mesh"), OutputMeshes->GetValueFromItemKey(OutputPoint.MetadataEntry) == MeshPath);
		TestTrue(TEXT("Every final wall piece receives the exact active RoomOverride wall material despite source material"), OutputMaterials->GetValueFromItemKey(OutputPoint.MetadataEntry) == OwnerMaterialPath);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeDoorMaterialCarrierTest,
	"NoShellForWinter.CalystoDungeon.Director.Native.DoorMaterialCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeDoorMaterialCarrierTest::RunTest(const FString&)
{
	UMaterialInterface* FirstMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInterface* SecondMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	if (!TestNotNull(TEXT("Door carrier fixture resolves first material"), FirstMaterial)
		|| !TestNotNull(TEXT("Door carrier fixture resolves second material"), SecondMaterial)) return false;
	const FSoftObjectPath FirstPath(FirstMaterial);
	const FSoftObjectPath SecondPath(SecondMaterial);
	if (!TestTrue(TEXT("Door carrier fixture uses distinct materials"), FirstPath != SecondPath)) return false;

	UPCGPointData* Pieces = NewObject<UPCGPointData>();
	auto* Owners = Pieces->MutableMetadata()->CreateAttribute<int64>(TEXT("EF_RoomId"), 0, false, false);
	auto* Materials = Pieces->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("WallMaterial"), FSoftObjectPath{}, false, false);
	if (!TestNotNull(TEXT("Door carrier fixture has typed ownership"), Owners)
		|| !TestNotNull(TEXT("Door carrier fixture has typed material"), Materials)) return false;
	const TPair<int64, FSoftObjectPath> Values[] = {{71, FirstPath}, {71, SecondPath}, {93, SecondPath}};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Values); ++Index)
	{
		FPCGPoint Point;
		Point.Transform = FTransform(FRotator(0, Index * 17, 0), FVector(80 + Index * 33, -70, 410), FVector(1, 1.1, 0.9));
		Point.MetadataEntry = Pieces->MutableMetadata()->AddEntry();
		Owners->SetValue(Point.MetadataEntry, Values[Index].Key);
		Materials->SetValue(Point.MetadataEntry, Values[Index].Value);
		Pieces->GetMutablePoints().Add(Point);
	}
	FPCGDataCollection Input;
	FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
	Tagged.Pin = PCGPinConstants::DefaultInputLabel; Tagged.Data = Pieces;
	Tagged.Tags.Add(TEXT("Wall")); Tagged.Tags.Add(TEXT("Door"));
	FPCGDataCollection Output;
	if (!TestTrue(TEXT("Carrier copies exact material before native construction"),
		EFCalystoNativeTests::RunDoorMaterialCarrier(Input, Output))) return false;
	UPCGPointData* Carried = EFCalystoNativeTests::FirstOutputPoints(Output);
	if (!TestNotNull(TEXT("Carrier produces one point stream"), Carried)) return false;
	const TArray<FPCGTaggedData>& OutputTagged = Output.GetInputsByPin(PCGPinConstants::DefaultOutputLabel);
	if (!TestEqual(TEXT("Carrier preserves cardinality"), Carried->GetNumPoints(), Pieces->GetNumPoints())
		|| !TestEqual(TEXT("Carrier preserves one tagged stream"), OutputTagged.Num(), 1)) return false;
	TestTrue(TEXT("Carrier preserves native tags"), OutputTagged[0].Tags.Contains(TEXT("Wall")) && OutputTagged[0].Tags.Contains(TEXT("Door")));
	const auto* OutputOwners = Carried->ConstMetadata()->GetConstTypedAttribute<int64>(TEXT("EF_RoomId"));
	const auto* OutputMaterials = Carried->ConstMetadata()->GetConstTypedAttribute<FSoftObjectPath>(TEXT("WallMaterial"));
	const auto* Carriers = Carried->ConstMetadata()->GetConstTypedAttribute<FSoftObjectPath>(TEXT("EF_DoorMaterialCarrier"));
	if (!TestNotNull(TEXT("Carrier preserves typed ownership"), OutputOwners)
		|| !TestNotNull(TEXT("Carrier preserves typed WallMaterial"), OutputMaterials)
		|| !TestNotNull(TEXT("Carrier writes typed per-piece material"), Carriers)) return false;
	const FConstPCGPointValueRanges InputRanges(Pieces);
	const FConstPCGPointValueRanges OutputRanges(Carried);
	for (int32 Index = 0; Index < Carried->GetNumPoints(); ++Index)
	{
		const FPCGPoint Before = InputRanges.GetPoint(Index);
		const FPCGPoint After = OutputRanges.GetPoint(Index);
		TestTrue(TEXT("Carrier preserves exact native transform"), After.Transform.Equals(Before.Transform, 0.0));
		TestEqual(TEXT("Carrier preserves each room identity"), OutputOwners->GetValueFromItemKey(After.MetadataEntry), Values[Index].Key);
		TestTrue(TEXT("Carrier preserves WallMaterial"), OutputMaterials->GetValueFromItemKey(After.MetadataEntry) == Values[Index].Value);
		TestTrue(TEXT("Carrier equals its exact source point material"), Carriers->GetValueFromItemKey(After.MetadataEntry) == Values[Index].Value);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeDoorMaterialProvenanceTest,
	"NoShellForWinter.CalystoDungeon.Director.Native.DoorMaterialProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeDoorMaterialProvenanceTest::RunTest(const FString&)
{
	UMaterialInterface* RoomOneMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInterface* RoomTwoMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	if (!TestNotNull(TEXT("Door provenance fixture resolves room one material"), RoomOneMaterial)
		|| !TestNotNull(TEXT("Door provenance fixture resolves room two material"), RoomTwoMaterial)) return false;
	const FSoftObjectPath RoomOnePath(RoomOneMaterial);
	const FSoftObjectPath RoomTwoPath(RoomTwoMaterial);
	if (!TestTrue(TEXT("Door provenance fixture uses distinct effective materials"), RoomOnePath != RoomTwoPath)) return false;

	UPCGPointData* DoorSource = NewObject<UPCGPointData>();
	auto* SourceOwners = DoorSource->MutableMetadata()->CreateAttribute<int64>(TEXT("EF_RoomId"), 0, false, false);
	auto* SourceMaterials = DoorSource->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("WallMaterial"), FSoftObjectPath{}, false, false);
	auto* SourceCarriers = DoorSource->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("EF_DoorMaterialCarrier"), FSoftObjectPath{}, false, false);
	auto* SourceStyleOwned = DoorSource->MutableMetadata()->CreateAttribute<bool>(TEXT("EF_StyleOwnedWall"), false, false, false);
	if (!TestNotNull(TEXT("Door provenance source has typed room ownership"), SourceOwners)
		|| !TestNotNull(TEXT("Door provenance source has typed material ownership"), SourceMaterials)
		|| !TestNotNull(TEXT("Door provenance source has typed per-piece carrier"), SourceCarriers)
		|| !TestNotNull(TEXT("Door provenance source has typed Style authority"), SourceStyleOwned)) return false;
	const TPair<int64, FSoftObjectPath> SourceValues[] = {{71, RoomOnePath}, {71, RoomTwoPath}, {93, RoomTwoPath}, {0, RoomOnePath}};
	const bool SourceStyleValues[] = {false, false, false, true};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SourceValues); ++Index)
	{
		FPCGPoint Point;
		Point.Transform = FTransform(FRotator(0, Index * 20, 0), FVector(100 + Index * 25, -200, 400), FVector(1, 1, 1));
		Point.MetadataEntry = DoorSource->MutableMetadata()->AddEntry();
		SourceOwners->SetValue(Point.MetadataEntry, SourceValues[Index].Key);
		SourceMaterials->SetValue(Point.MetadataEntry, SourceValues[Index].Value);
		SourceCarriers->SetValue(Point.MetadataEntry, SourceValues[Index].Value);
		SourceStyleOwned->SetValue(Point.MetadataEntry, SourceStyleValues[Index]);
		DoorSource->GetMutablePoints().Add(Point);
	}

	UPCGPointData* FinalDoors = NewObject<UPCGPointData>();
	auto* FinalOwners = FinalDoors->MutableMetadata()->CreateAttribute<int64>(TEXT("EF_RoomId"), 0, false, false);
	auto* FinalCarriers = FinalDoors->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("EF_DoorMaterialCarrier"), FSoftObjectPath{}, false, false);
	auto* FinalStyleOwned = FinalDoors->MutableMetadata()->CreateAttribute<bool>(TEXT("EF_StyleOwnedWall"), false, false, false);
	if (!TestNotNull(TEXT("Final native door fixture retains typed room ownership"), FinalOwners)
		|| !TestNotNull(TEXT("Final native door fixture retains typed per-piece carrier"), FinalCarriers)
		|| !TestNotNull(TEXT("Final native door fixture retains typed Style authority"), FinalStyleOwned)) return false;
	const int64 FinalIds[] = {71, 71, 93, 0};
	const FSoftObjectPath FinalMaterials[] = {RoomTwoPath, RoomOnePath, RoomTwoPath, RoomOnePath};
	const bool FinalStyleValues[] = {false, false, false, true};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(FinalIds); ++Index)
	{
		FPCGPoint Point;
		// The final native stream can reorder or filter points. Its exact per-point
		// carrier, not room ID, proximity, or array order, remains authoritative.
		Point.Transform = FTransform(FRotator(0, 90 - Index * 15, 0), FVector(-500 + Index * 333, 400, 800), FVector(1.5, 1, 0.75));
		Point.MetadataEntry = FinalDoors->MutableMetadata()->AddEntry();
		FinalOwners->SetValue(Point.MetadataEntry, FinalIds[Index]);
		FinalCarriers->SetValue(Point.MetadataEntry, FinalMaterials[Index]);
		FinalStyleOwned->SetValue(Point.MetadataEntry, FinalStyleValues[Index]);
		FinalDoors->GetMutablePoints().Add(Point);
	}
	FPCGDataCollection Input;
	FPCGTaggedData& SourceTagged = Input.TaggedData.Emplace_GetRef();
	SourceTagged.Pin = FEFCalystoDoorMaterialProvenancePins::DoorSource; SourceTagged.Data = DoorSource;
	SourceTagged.Tags.Add(TEXT("ExactDoorSource"));
	FPCGTaggedData& FinalTagged = Input.TaggedData.Emplace_GetRef();
	FinalTagged.Pin = FEFCalystoDoorMaterialProvenancePins::FinalDoorPieces; FinalTagged.Data = FinalDoors;
	FinalTagged.Tags.Add(TEXT("NativeDoorPiece")); FinalTagged.Tags.Add(TEXT("Final"));

	FPCGDataCollection Output;
	if (!TestTrue(TEXT("Final doors restore exact per-piece material provenance"),
		EFCalystoNativeTests::RunDoorMaterialProvenance(Input, Output))) return false;
	UPCGPointData* Provenanced = EFCalystoNativeTests::FirstOutputPoints(Output);
	if (!TestNotNull(TEXT("Door provenance produces one final stream"), Provenanced)) return false;
	const TArray<FPCGTaggedData>& OutputTagged = Output.GetInputsByPin(PCGPinConstants::DefaultOutputLabel);
	if (!TestEqual(TEXT("Door provenance preserves final cardinality"), Provenanced->GetNumPoints(), FinalDoors->GetNumPoints())
		|| !TestEqual(TEXT("Door provenance preserves one final tagged stream"), OutputTagged.Num(), 1)) return false;
	TestTrue(TEXT("Door provenance preserves native final tags"),
		OutputTagged[0].Tags.Contains(TEXT("NativeDoorPiece")) && OutputTagged[0].Tags.Contains(TEXT("Final")));
	const auto* OutputOwners = Provenanced->ConstMetadata()->GetConstTypedAttribute<int64>(TEXT("EF_RoomId"));
	const auto* OutputMaterials = Provenanced->ConstMetadata()->GetConstTypedAttribute<FSoftObjectPath>(TEXT("WallMaterial"));
	const auto* OutputStyleOwned = Provenanced->ConstMetadata()->GetConstTypedAttribute<bool>(TEXT("EF_StyleOwnedWall"));
	if (!TestNotNull(TEXT("Door provenance rewrites typed final ownership"), OutputOwners)
		|| !TestNotNull(TEXT("Door provenance restores typed final material"), OutputMaterials)
		|| !TestNotNull(TEXT("Door provenance restores typed Style authority"), OutputStyleOwned)) return false;
	TestFalse(TEXT("Final output does not retain internal carrier metadata"),
		Provenanced->ConstMetadata()->HasAttribute(TEXT("EF_DoorMaterialCarrier")));
	const FConstPCGPointValueRanges FinalRanges(FinalDoors);
	const FConstPCGPointValueRanges OutputRanges(Provenanced);
	for (int32 Index = 0; Index < Provenanced->GetNumPoints(); ++Index)
	{
		const FPCGPoint Before = FinalRanges.GetPoint(Index);
		const FPCGPoint After = OutputRanges.GetPoint(Index);
		TestTrue(TEXT("Door provenance preserves exact native final transforms"), After.Transform.Equals(Before.Transform, 0.0));
		TestEqual(TEXT("Door provenance preserves the exact final room ID"), OutputOwners->GetValueFromItemKey(After.MetadataEntry), FinalIds[Index]);
		TestTrue(TEXT("Door provenance restores the exact final carrier material"),
			OutputMaterials->GetValueFromItemKey(After.MetadataEntry) == FinalMaterials[Index]);
		TestEqual(TEXT("Door provenance restores the exact final Style authority"),
			OutputStyleOwned->GetValueFromItemKey(After.MetadataEntry), FinalStyleValues[Index]);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeStyleWallFallbackProvenanceTest,
	"NoShellForWinter.CalystoDungeon.Director.Native.StyleWallFallbackProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeStyleWallFallbackProvenanceTest::RunTest(const FString&)
{
	FPCGDataCollection Input;
	UPCGPointData* Pieces = NewObject<UPCGPointData>();
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* StyleMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInterface* NativeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	if (!TestNotNull(TEXT("Style fallback fixture resolves its native mesh"), Mesh)
		|| !TestNotNull(TEXT("Style fallback fixture resolves the frozen Style material"), StyleMaterial)
		|| !TestNotNull(TEXT("Style fallback fixture resolves the prior native wall material"), NativeMaterial)) return false;
	const FSoftObjectPath MeshPath(Mesh);
	const FSoftObjectPath StylePath(StyleMaterial);
	const FSoftObjectPath NativePath(NativeMaterial);
	if (!TestTrue(TEXT("Style fallback fixture distinguishes native and frozen Style materials"), NativePath != StylePath)) return false;
	auto* Meshes = Pieces->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath{}, false, false);
	auto* NativeMaterials = Pieces->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("WallMaterial"), FSoftObjectPath{}, false, false);
	auto* NativeOwners = Pieces->MutableMetadata()->CreateAttribute<int64>(TEXT("EF_RoomId"), 0, false, false);
	if (!TestNotNull(TEXT("Style fallback fixture has typed mesh provenance"), Meshes)
		|| !TestNotNull(TEXT("Style fallback fixture has a pre-existing native wall material"), NativeMaterials)
		|| !TestNotNull(TEXT("Style fallback fixture has canonical unthemed room provenance"), NativeOwners)) return false;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FPCGPoint Point;
		Point.Transform = FTransform(FRotator(0, 30 * Index, 0), FVector(200 + Index * 75, -50, 300), FVector(1, 1.25, 1));
		Point.MetadataEntry = Pieces->MutableMetadata()->AddEntry();
		Meshes->SetValue(Point.MetadataEntry, MeshPath);
		NativeMaterials->SetValue(Point.MetadataEntry, NativePath);
		NativeOwners->SetValue(Point.MetadataEntry, Index == 0 ? int64(173) : int64(0));
		Pieces->GetMutablePoints().Add(Point);
	}
	FPCGTaggedData& PieceInput = Input.TaggedData.Emplace_GetRef();
	PieceInput.Pin = PCGPinConstants::DefaultInputLabel; PieceInput.Data = Pieces;
	PieceInput.Tags.Add(TEXT("Wall")); PieceInput.Tags.Add(TEXT("Default"));
	FPCGDataCollection Output;
	if (!TestTrue(TEXT("Exact default-wall branch receives frozen Style authority"),
		EFCalystoNativeTests::RunStyleWallFallback(Input, StylePath, Output))) return false;
	UPCGPointData* Styled = EFCalystoNativeTests::FirstOutputPoints(Output);
	if (!TestNotNull(TEXT("Style fallback produces final wall pieces"), Styled)) return false;
	const TArray<FPCGTaggedData>& Tagged = Output.GetInputsByPin(PCGPinConstants::DefaultOutputLabel);
	if (!TestEqual(TEXT("Style fallback preserves one tagged default-wall stream"), Tagged.Num(), 1)
		|| !TestEqual(TEXT("Style fallback preserves default-wall cardinality"), Styled->GetNumPoints(), Pieces->GetNumPoints())) return false;
	TestTrue(TEXT("Style fallback preserves default-wall tags"),
		Tagged[0].Tags.Contains(TEXT("Wall")) && Tagged[0].Tags.Contains(TEXT("Default")));
	const auto* Owners = Styled->ConstMetadata()->GetConstTypedAttribute<int64>(TEXT("EF_RoomId"));
	const auto* Materials = Styled->ConstMetadata()->GetConstTypedAttribute<FSoftObjectPath>(TEXT("WallMaterial"));
	const auto* StyleOwned = Styled->ConstMetadata()->GetConstTypedAttribute<bool>(TEXT("EF_StyleOwnedWall"));
	if (!TestNotNull(TEXT("Style fallback emits an explicit absent room owner"), Owners)
		|| !TestNotNull(TEXT("Style fallback emits the selected Style wall material"), Materials)
		|| !TestNotNull(TEXT("Style fallback emits its explicit Style authority marker"), StyleOwned)) return false;
	const FConstPCGPointValueRanges InputRanges(Pieces);
	const FConstPCGPointValueRanges OutputRanges(Styled);
	for (int32 Index = 0; Index < Pieces->GetNumPoints(); ++Index)
	{
		const FPCGPoint Before = InputRanges.GetPoint(Index);
		const FPCGPoint After = OutputRanges.GetPoint(Index);
		TestTrue(TEXT("Style fallback preserves each exact native transform"), After.Transform.Equals(Before.Transform, 0.0));
		TestEqual(TEXT("Style fallback clears stale input ownership on its exact Difference branch"), Owners->GetValueFromItemKey(After.MetadataEntry), int64(0));
		TestTrue(TEXT("Style fallback uses its exact frozen Style material"), Materials->GetValueFromItemKey(After.MetadataEntry) == StylePath);
		TestTrue(TEXT("Style fallback marks every exact Difference result as Style-owned"), StyleOwned->GetValueFromItemKey(After.MetadataEntry));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeWallExtensionContractTest,
	"NoShellForWinter.CalystoDungeon.Director.Native.ZeroHeightWallExtensionContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeWallExtensionContractTest::RunTest(const FString&)
{
	UPCGGraph* Source = LoadObject<UPCGGraph>(nullptr,
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh"));
	if (!TestNotNull(TEXT("Audited native wall construction graph is available"), Source)) return false;
	const bool bDirtyBefore = Source->GetOutermost()->IsDirty();
	FString Error;
	TestTrue(TEXT("Equal positive runtime heights permit the exact native upper extension"),
		FEFCalystoNativeAdapter::ValidateZeroHeightWallExtensionContract(Source, 300, 300, Error));
	TestFalse(TEXT("Unequal valid heights do not permit zero-height walls"),
		FEFCalystoNativeAdapter::ValidateZeroHeightWallExtensionContract(Source, 600, 300, Error));
	TestTrue(TEXT("Unequal valid heights are not a configuration failure"), Error.IsEmpty());
	for (const double Invalid : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
	{
		TestFalse(TEXT("Invalid initial height cannot authorize a degenerate wall"),
			FEFCalystoNativeAdapter::ValidateZeroHeightWallExtensionContract(Source, 300, Invalid, Error));
		TestFalse(TEXT("Invalid initial height has an explicit diagnostic"), Error.IsEmpty());
	}
	UPCGGraph* Clone = DuplicateObject<UPCGGraph>(Source, GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), Source->GetClass(), TEXT("WallExtensionContractFixture")));
	if (!TestNotNull(TEXT("Mutation fixture is a separate transient graph"), Clone)) return false;
	Clone->ClearFlags(RF_Public | RF_Standalone); Clone->SetFlags(RF_Transient);
	UPCGNode* Subtract = nullptr; UPCGNode* Divide = nullptr;
	UPCGNode* Declaration = nullptr; UPCGNode* Usage = nullptr;
	for (UPCGNode* N : Clone->GetNodes()) if (N)
	{
		if (N->GetFName() == TEXT("AttributeMathsOp_13")) Subtract = N;
		if (N->GetFName() == TEXT("AttributeMathsOp_11")) Divide = N;
		if (N->GetFName() == TEXT("NamedRerouteDeclaration_12")) Declaration = N;
		if (N->GetFName() == TEXT("NamedRerouteUsage_16")) Usage = N;
	}
	auto* Math = Subtract ? Cast<UPCGMetadataMathsSettings>(Subtract->GetSettings()) : nullptr;
	if (!TestNotNull(TEXT("Exact extension subtraction exists"), Math) || !TestNotNull(TEXT("Exact height ratio exists"), Divide)) return false;
	if (!TestNotNull(TEXT("Exact named scale declaration exists"), Declaration) || !TestNotNull(TEXT("Exact named scale usage exists"), Usage)) return false;
	UPCGPin* UsageInput = Usage->GetInputPin(TEXT("In"));
	if (!TestNotNull(TEXT("Named reroute has its hidden incoming pin"), UsageInput)) return false;
	UsageInput->BreakEdgeTo(Declaration->GetOutputPin(PCGNamedRerouteConstants::InvisiblePinLabel));
	Clone->AddEdge(Declaration, TEXT("Out"), Usage, TEXT("In"));
	TestFalse(TEXT("The visible output cannot substitute for the native hidden reroute link"),
		FEFCalystoNativeAdapter::ValidateZeroHeightWallExtensionContract(Clone, 300, 300, Error));
	TestTrue(TEXT("The rejected connection identifies the exact required hidden output"), Error.Contains(TEXT("InvisiblePin")));
	UsageInput->BreakEdgeTo(Declaration->GetOutputPin(TEXT("Out")));
	Clone->AddEdge(Declaration, PCGNamedRerouteConstants::InvisiblePinLabel, Usage, TEXT("In"));
	TestTrue(TEXT("Restoring the exact hidden link restores the audited contract"),
		FEFCalystoNativeAdapter::ValidateZeroHeightWallExtensionContract(Clone, 300, 300, Error));
	Math->Operation = EPCGMetadataMathsOperation::Add;
	TestFalse(TEXT("Changed native mathematics cannot receive the exemption"),
		FEFCalystoNativeAdapter::ValidateZeroHeightWallExtensionContract(Clone, 300, 300, Error));
	Math->Operation = EPCGMetadataMathsOperation::Subtract;
	Subtract->GetInputPin(TEXT("InA"))->BreakEdgeTo(Divide->GetOutputPin(TEXT("Out")));
	TestFalse(TEXT("Missing native formula edge cannot receive the exemption"),
		FEFCalystoNativeAdapter::ValidateZeroHeightWallExtensionContract(Clone, 300, 300, Error));
	TestEqual(TEXT("Vendor graph dirty state is unchanged"), Source->GetOutermost()->IsDirty(), bDirtyBefore);

	FPCGDataCollection Data;
	auto* Points = NewObject<UPCGPointData>();
	auto* Mesh = NewObject<UStaticMesh>(GetTransientPackage());
	const FSoftObjectPath MeshPath(Mesh);
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!TestNotNull(TEXT("Provenance fixture has a resident wall material"), Material)) return false;
	const FSoftObjectPath MaterialPath(Material);
	auto* MeshAttribute = Points->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), MeshPath, false, false);
	auto* RoomAttribute = Points->MutableMetadata()->CreateAttribute<int64>(TEXT("EF_RoomId"), 7, false, false);
	auto* MaterialAttribute = Points->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("WallMaterial"), MaterialPath, false, false);
	if (!TestNotNull(TEXT("Provenance fixture has actual typed mesh metadata"), MeshAttribute)
		|| !TestNotNull(TEXT("Provenance fixture has typed room metadata"), RoomAttribute)
		|| !TestNotNull(TEXT("Provenance fixture has typed material metadata"), MaterialAttribute)) return false;
	FPCGPoint Point; Point.Transform = FTransform(FQuat::Identity, FVector(10,20,300.5), FVector(1.01,1,0));
	Point.MetadataEntry = Points->MutableMetadata()->AddEntry();
	MeshAttribute->SetValue(Point.MetadataEntry, MeshPath);
	RoomAttribute->SetValue(Point.MetadataEntry, 7);
	MaterialAttribute->SetValue(Point.MetadataEntry, MaterialPath);
	Points->GetMutablePoints().Add(Point); Points->GetMutablePoints().Add(Point);
	FPCGTaggedData& Tagged = Data.TaggedData.Emplace_GetRef();
	Tagged.Data = Points; Tagged.Pin = TEXT("EF Native Final Wall"); Tagged.Tags.Add(TEXT("NoSocket"));
	TArray<FEFCalystoNativeWallSurface> Surfaces;
	TArray<FEFCalystoNativeWallExtension> Extensions;
	TestTrue(TEXT("Audited final native zero-height points provide complete provenance"),
		FEFCalystoNativeAdapter::ReadFinalWallSurfaces(Data, true, Surfaces, Extensions, Error));
	TestEqual(TEXT("Each final native point retains room/material ownership"), Surfaces.Num(), 2);
	TestEqual(TEXT("Identical transforms preserve two native instance requests"), Extensions.Num(), 2);
	FPCGDataCollection DoorData;
	UPCGPointData* DoorPoints = NewObject<UPCGPointData>();
	auto* DoorMeshAttribute = DoorPoints->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), MeshPath, false, false);
	auto* DoorRoomAttribute = DoorPoints->MutableMetadata()->CreateAttribute<int64>(TEXT("EF_RoomId"), 7, false, false);
	auto* DoorMaterialAttribute = DoorPoints->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("WallMaterial"), MaterialPath, false, false);
	if (!TestNotNull(TEXT("Door provenance fixture has mesh metadata"), DoorMeshAttribute)
		|| !TestNotNull(TEXT("Door provenance fixture has room metadata"), DoorRoomAttribute)
		|| !TestNotNull(TEXT("Door provenance fixture has material metadata"), DoorMaterialAttribute)) return false;
	FPCGPoint DoorPoint; DoorPoint.Transform = FTransform(FQuat::Identity, FVector(50,60,700), FVector(1.25,1.5,1));
	DoorPoint.MetadataEntry = DoorPoints->MutableMetadata()->AddEntry();
	DoorMeshAttribute->SetValue(DoorPoint.MetadataEntry, MeshPath);
	DoorRoomAttribute->SetValue(DoorPoint.MetadataEntry, 7);
	DoorMaterialAttribute->SetValue(DoorPoint.MetadataEntry, MaterialPath);
	DoorPoints->GetMutablePoints().Add(DoorPoint);
	FPCGTaggedData& DoorTagged = DoorData.TaggedData.Emplace_GetRef();
	DoorTagged.Data = DoorPoints; DoorTagged.Pin = TEXT("EF Native Final Wall-Door");
	TArray<FEFCalystoNativeWallSurface> DoorSurfaces;
	TArray<FEFCalystoNativeWallExtension> DoorExtensions;
	if (!TestTrue(TEXT("A native door-wall receives final provenance without the upper-extension exemption"),
		FEFCalystoNativeAdapter::ReadFinalWallSurfaces(DoorData, false, DoorSurfaces, DoorExtensions, Error))) return false;
	if (!TestEqual(TEXT("One native door-wall produces one final structural provenance record"), DoorSurfaces.Num(), 1)
		|| !TestEqual(TEXT("A normal-height native door-wall produces no zero-height extension"), DoorExtensions.Num(), 0)) return false;
	TestTrue(TEXT("Door-wall mesh provenance is exact"), DoorSurfaces[0].Mesh == MeshPath);
	TestTrue(TEXT("Door-wall material provenance is exact"), DoorSurfaces[0].Material == MaterialPath);
	TestEqual(TEXT("Door-wall room provenance is exact"), DoorSurfaces[0].RoomId, int64(7));
	TestTrue(TEXT("Door-wall retains its exact final branch origin"), DoorSurfaces[0].bDoorway);
	TestFalse(TEXT("Door-wall is never classified as a zero-height extension"), DoorSurfaces[0].bZeroHeightExtension);
	TestTrue(TEXT("Door-wall transform provenance is exact"), DoorSurfaces[0].WorldTransform.Equals(DoorPoint.Transform, 0.0));
	TestFalse(TEXT("Point tags alone cannot authorize zero-height geometry"),
		FEFCalystoNativeAdapter::ReadFinalWallSurfaces(Data, false, Surfaces, Extensions, Error));
	FPCGDataCollection StyleData;
	UPCGPointData* StylePoints = NewObject<UPCGPointData>();
	auto* StyleMesh = StylePoints->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), MeshPath, false, false);
	auto* StyleRoom = StylePoints->MutableMetadata()->CreateAttribute<int64>(TEXT("EF_RoomId"), 0, false, false);
	auto* StyleMaterial = StylePoints->MutableMetadata()->CreateAttribute<FSoftObjectPath>(TEXT("WallMaterial"), MaterialPath, false, false);
	auto* StyleOwned = StylePoints->MutableMetadata()->CreateAttribute<bool>(TEXT("EF_StyleOwnedWall"), true, false, false);
	if (!TestNotNull(TEXT("Style-owned final wall has mesh metadata"), StyleMesh)
		|| !TestNotNull(TEXT("Style-owned final wall has explicit absent room metadata"), StyleRoom)
		|| !TestNotNull(TEXT("Style-owned final wall has Style material metadata"), StyleMaterial)
		|| !TestNotNull(TEXT("Style-owned final wall has explicit authority metadata"), StyleOwned)) return false;
	FPCGPoint StylePoint; StylePoint.Transform = FTransform(FQuat::Identity, FVector(100, 200, 300), FVector(1, 1, 1));
	StylePoint.MetadataEntry = StylePoints->MutableMetadata()->AddEntry();
	StyleMesh->SetValue(StylePoint.MetadataEntry, MeshPath);
	StyleRoom->SetValue(StylePoint.MetadataEntry, 0);
	StyleMaterial->SetValue(StylePoint.MetadataEntry, MaterialPath);
	StyleOwned->SetValue(StylePoint.MetadataEntry, true);
	StylePoints->GetMutablePoints().Add(StylePoint);
	FPCGTaggedData& StyleTagged = StyleData.TaggedData.Emplace_GetRef();
	StyleTagged.Data = StylePoints; StyleTagged.Pin = TEXT("EF Native Final Wall");
	TArray<FEFCalystoNativeWallSurface> StyleSurfaces;
	TArray<FEFCalystoNativeWallExtension> StyleExtensions;
	if (!TestTrue(TEXT("An explicit Style-owned default wall has valid final provenance"),
		FEFCalystoNativeAdapter::ReadFinalWallSurfaces(StyleData, false, StyleSurfaces, StyleExtensions, Error))) return false;
	if (!TestEqual(TEXT("Style-owned default wall remains a normal wall"), StyleExtensions.Num(), 0)
		|| !TestEqual(TEXT("Style-owned default wall has one final provenance record"), StyleSurfaces.Num(), 1)) return false;
	TestTrue(TEXT("Style-owned default wall retains Style authority"), StyleSurfaces[0].bStyleOwned);
	TestEqual(TEXT("Style-owned default wall retains no invented RoomId"), StyleSurfaces[0].RoomId, int64(0));
	StyleOwned->SetValue(StylePoint.MetadataEntry, false);
	TestFalse(TEXT("A zero RoomId without explicit Style authority is rejected"),
		FEFCalystoNativeAdapter::ReadFinalWallSurfaces(StyleData, false, StyleSurfaces, StyleExtensions, Error));
	Tagged.Tags.Reset();
	TestFalse(TEXT("The formula alone cannot authorize an untracked wall point"),
		FEFCalystoNativeAdapter::ReadFinalWallSurfaces(Data, true, Surfaces, Extensions, Error));
	Tagged.Tags.Add(TEXT("NoSocket"));
	Points->MutableMetadata()->DeleteAttribute(TEXT("WallMaterial"));
	TestFalse(TEXT("A final wall cannot infer a missing material from room location"),
		FEFCalystoNativeAdapter::ReadFinalWallSurfaces(Data, true, Surfaces, Extensions, Error));
	TestTrue(TEXT("A final wall metadata rejection identifies its exact source and field"),
		Error.Contains(TEXT("source=EF Native Final Wall")) && Error.Contains(TEXT("WallMaterial=Missing")));
	return true;
}
#endif
