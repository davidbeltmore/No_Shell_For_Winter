#if WITH_DEV_AUTOMATION_TESTS
#include "Calysto/EFCalystoBakedArchitecture.h"
#include "PCGDataAsset.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/PCGMetadata.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#include "PCGSubgraph.h"
#include "UObject/UnrealType.h"
#include "HAL/PlatformTime.h"
#include "Algo/Reverse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeBakedArchitectureTest,
	"NoShellForWinter.CalystoDungeon.Director.Architecture.NativeBakedPayloadContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeBakedArchitectureTest::RunTest(const FString&)
{
	// Read the original reachable Forge payload without regenerating, exporting or saving vendor data.
	const auto* Payload=LoadObject<UPCGDataAsset>(nullptr,TEXT("/Game/Calysto/Dungeon/Demo/LevelInstance/PCGDA_Table.PCGDA_Table"));
	if (!TestNotNull(TEXT("Authored native baked Forge payload loads"),Payload)) return false;
	for (const auto& Tagged:Payload->Data.TaggedData)
	{
		const auto* Points=Cast<UPCGBasePointData>(Tagged.Data);
		if (!Points) continue;
		TArray<FName> Names; TArray<EPCGMetadataTypes> Types;
		if (Points->ConstMetadata()) Points->ConstMetadata()->GetAttributes(Names,Types);
		TArray<FString> Description;
		for (int32 I=0;I<Names.Num();++I) Description.Add(FString::Printf(TEXT("%s:%d"),*Names[I].ToString(),int32(Types[I])));
		AddInfo(FString::Printf(TEXT("Native baked %s: %d points; %s"),*Tagged.Pin.ToString(),Points->GetNumPoints(),*FString::Join(Description,TEXT(", "))));
	}
	TArray<FEFCalystoBakedMesh> Children; TArray<FSoftObjectPath> Dependencies; FString Error;
	if (!TestTrue(TEXT("Actual retained Forge payload decodes through the runtime contract"),
		FEFCalystoBakedArchitecture::Decode(*Payload,Children,Dependencies,Error))) { AddError(Error); return false; }
	TestTrue(TEXT("Selected baked payload retains real children and dependencies"),!Children.IsEmpty() && !Dependencies.IsEmpty());
	AddInfo(FString::Printf(TEXT("Native baked children=%d, exact dependencies=%d"),Children.Num(),Dependencies.Num()));
	TestTrue(TEXT("Forge's authored optional child chance is retained"),Children.ContainsByPredicate([](const auto& C) { return C.ChancePercent==50; }));
	TestTrue(TEXT("Forge's authored absolute world yaw is retained"),Children.ContainsByPredicate([](const auto& C) { return C.Rotation==EEFCalystoBakedRotation::WorldYaw; }));
	for (const auto& C:Children) AddInfo(FString::Printf(TEXT("Native baked child %s: Chance %.0f%%, rotation %d, %s"),
		*C.Id.ToString(),C.ChancePercent,int32(C.Rotation),*C.Mesh.ToString()));
	const auto OriginalChildren=Children;
	auto* Reordered=DuplicateObject<UPCGDataAsset>(Payload,GetTransientPackage());
	for (auto& Tagged:Reordered->Data.TaggedData) if (Tagged.Pin==TEXT("Points"))
	{
		const auto* Original=CastChecked<UPCGBasePointData>(Tagged.Data);
		auto* Copy=DuplicateObject<UPCGBasePointData>(Original,GetTransientPackage());
		TArray<int32> Indices; for (int32 I=Original->GetNumPoints()-1;I>=0;--I) Indices.Add(I);
		Copy->SetPointsFrom(Original,Indices); Tagged.Data=Copy;
	}
	if (!TestTrue(TEXT("Reordered native point data remains supported"),FEFCalystoBakedArchitecture::Decode(*Reordered,Children,Dependencies,Error)))
	{ AddError(Error); return false; }
	TestEqual(TEXT("Reordering preserves exact child multiplicity"),Children.Num(),OriginalChildren.Num());
	for (int32 I=0;I<FMath::Min(Children.Num(),OriginalChildren.Num());++I)
	{
		TestEqual(TEXT("Reordering preserves child identities"),Children[I].Id,OriginalChildren[I].Id);
		TestEqual(TEXT("Reordering preserves child resources"),Children[I].Mesh,OriginalChildren[I].Mesh);
		TestTrue(TEXT("Reordering preserves child transforms"),Children[I].LocalTransform.Equals(OriginalChildren[I].LocalTransform));
	}
	auto* Unsupported=DuplicateObject<UPCGDataAsset>(Payload,GetTransientPackage());
	Unsupported->Data.TaggedData[0].Pin=TEXT("UntrackedSpawn");
	TestFalse(TEXT("Unsupported baked outputs reject before execution"),FEFCalystoBakedArchitecture::Decode(*Unsupported,Children,Dependencies,Error));
	TestTrue(TEXT("Contract failure clears the whole proposed output"),Children.IsEmpty() && Dependencies.IsEmpty());
	// Preserve the actual vendor graph's read-only contract for the production adapter
	// work. Exported actor tags such as Chance50/RotateZ have no intrinsic meaning in
	// PCGDataAsset itself; their consumers must be inspected before claiming parity.
	UPCGGraph* Decoration=LoadObject<UPCGGraph>(nullptr,TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_RoomMesh.PCG_RoomMesh"));
	if (!TestNotNull(TEXT("Native decoration graph is available for contract inspection"),Decoration)) return false;
	TArray<UPCGGraph*> Queue{Decoration}; TSet<const UPCGGraph*> Seen; FString Contract; int32 Nodes=0;
	for (int32 GraphIndex=0;GraphIndex<Queue.Num();++GraphIndex)
	{
		UPCGGraph* Graph=Queue[GraphIndex]; if (Seen.Contains(Graph)) continue; Seen.Add(Graph);
		if (Seen.Num()>64) { AddError(TEXT("Native decoration inspection exceeded 64 graphs.")); return false; }
		Contract+=TEXT("GRAPH ")+Graph->GetPathName()+LINE_TERMINATOR;
		for (UPCGNode* Node:Graph->GetNodes())
		{
			const UPCGSettings* Settings=Node ? Node->GetSettings() : nullptr;
			if (!Settings || ++Nodes>1024) { AddError(TEXT("Native decoration inspection contains an invalid node or exceeds 1024 nodes.")); return false; }
			Contract+=TEXT(" NODE ")+Node->GetName()+TEXT(" ")+Settings->GetClass()->GetPathName()+LINE_TERMINATOR;
			for (TFieldIterator<FProperty> It(Settings->GetClass());It;++It)
			{
				if (!It->HasAnyPropertyFlags(CPF_Edit) || It->HasAnyPropertyFlags(CPF_Transient)) continue;
				FString Value; It->ExportText_InContainer(0,Value,Settings,nullptr,nullptr,PPF_None);
				if (Value.Len()>65536 || Contract.Len()+Value.Len()>524288)
				{ AddError(TEXT("Native decoration inspection exceeded its bounded text capacity.")); return false; }
				Contract+=TEXT("  ")+It->GetName()+TEXT(" = ")+Value+LINE_TERMINATOR;
			}
			for (const UPCGPin* Pin:Node->GetOutputPins()) if (Pin) for (const UPCGEdge* Edge:Pin->Edges)
				if (Edge && Edge->OutputPin && Edge->OutputPin->Node)
					Contract+=TEXT("  EDGE ")+Pin->Properties.Label.ToString()+TEXT(" -> ")
						+Edge->OutputPin->Node->GetName()+TEXT(".")+Edge->OutputPin->Properties.Label.ToString()+LINE_TERMINATOR;
			if (const auto* Call=Cast<UPCGBaseSubgraphSettings>(Settings))
				if (UPCGGraph* Child=Call->GetSubgraph()) { Contract+=TEXT("  SUBGRAPH ")+Child->GetPathName()+LINE_TERMINATOR; Queue.AddUnique(Child); }
		}
	}
	const FString Directory=FPaths::ProjectSavedDir()/TEXT("Migration/CalystoDungeonDirectorV7/NativeArchitectureContracts");
	IFileManager::Get().MakeDirectory(*Directory,true);
	const FString Output=Directory/(FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".txt"));
	if (!TestTrue(TEXT("Native decoration contract evidence is saved outside runtime/cook"),
		FFileHelper::SaveStringToFile(Contract,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
	AddInfo(FString::Printf(TEXT("Read-only native decoration contract: %d graphs, %d nodes, %s"),Seen.Num(),Nodes,*Output));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoBakedProbabilityTest,
	"NoShellForWinter.CalystoDungeon.Director.Architecture.BakedProbabilityAndReservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoBakedProbabilityTest::RunTest(const FString&)
{
	const FGuid ParentId(101,202,303,404);
	FEFCalystoRandomKey Random; Random.StyleId=FGuid(31,32,33,34); Random.FloorNumber=7;
	const FTransform Parent(FRotator(12,31,8),FVector(500,800,300),FVector(1.2,0.8,1.1));
	const FBox Reservation(FVector(-10000),FVector(10000));
	TArray<FEFCalystoBakedMesh> Children;
	const double Chance[]={10,30,50,75,90,100,100};
	for (int32 I=0;I<7;++I)
	{
		auto& C=Children.AddDefaulted_GetRef(); C.Id=FGuid(7,8,9,I+1);
		C.Mesh=FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"));
		C.LocalTransform=FTransform(FRotator(19,52,23),FVector(I*80,20,40),FVector(1,1.5,0.75));
		C.ExportedLocalBounds=FBox(FVector(-4,-6,-8),FVector(7,9,11)); C.ChancePercent=Chance[I];
		if (I==5) C.Rotation=EEFCalystoBakedRotation::WorldYaw;
		if (I==6) C.Rotation=EEFCalystoBakedRotation::RelativeYaw;
	}
	TArray<FEFCalystoBakedDecision> Decisions; FString Error;
	if (!TestTrue(TEXT("Entire payload envelopes fit before any outcome"),FEFCalystoBakedArchitecture::Freeze(Random,
		ParentId,Parent,Children,Reservation,Decisions,Error))) { AddError(Error); return false; }
	const auto Frozen=Decisions;
	Algo::Reverse(Children);
	for (auto& C:Children) C.Material=FSoftObjectPath(TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	if (!TestTrue(TEXT("Reordering and material changes preserve reservations"),FEFCalystoBakedArchitecture::Freeze(Random,
		ParentId,Parent,Children,Reservation,Decisions,Error))) return false;
	for (int32 I=0;I<Decisions.Num();++I)
	{
		TestEqual(TEXT("Canonical reservation identity is unchanged"),Decisions[I].Id,Frozen[I].Id);
		TestEqual(TEXT("Visual changes cannot reroll child presence"),Decisions[I].bSelected,Frozen[I].bSelected);
		TestTrue(TEXT("Visual changes cannot reroll child transforms"),Decisions[I].WorldTransform.Equals(Frozen[I].WorldTransform));
	}
	TestFalse(TEXT("Insufficient envelope rejects the whole payload without truncating children"),FEFCalystoBakedArchitecture::Freeze(Random,
		ParentId,Parent,Children,FBox(FVector(-1),FVector(1)),Decisions,Error));
	TestTrue(TEXT("Failure leaves no frozen subset"),Decisions.IsEmpty());
	Children[0].ChancePercent=0;
	TestTrue(TEXT("Explicit zero chance is supported"),FEFCalystoBakedArchitecture::Freeze(Random,ParentId,Parent,Children,Reservation,Decisions,Error));
	TestTrue(TEXT("Explicit zero never selects"),Decisions.ContainsByPredicate([](const auto& D) { return D.Child.ChancePercent==0 && !D.bSelected; }));
	Children[0].ChancePercent=100;
	constexpr int32 Trials=100000; int32 Counts[7]={}; int32 CDF[2][15]={};
	const double Started=FPlatformTime::Seconds();
	for (int32 Trial=0;Trial<Trials;++Trial)
	{
		Random.RunSeed=Trial+61027;
		if (!FEFCalystoBakedArchitecture::Freeze(Random,ParentId,Parent,Children,Reservation,Decisions,Error))
		{ AddError(Error); return false; }
		for (const auto& D:Decisions)
		{
			const int32 I=int32(D.Child.Id.D)-1; if (D.bSelected) ++Counts[I];
			if (I<5) continue;
			const FTransform Source=D.Child.LocalTransform*Parent;
			const FQuat Yaw=I==5 ? D.WorldTransform.GetRotation() : Source.GetRotation().Inverse()*D.WorldTransform.GetRotation();
			const double Angle=FRotator::ClampAxis(Yaw.Rotator().Yaw)/360;
			for (int32 K=1;K<16;++K) if (Angle<double(K)/16) ++CDF[I-5][K-1];
			if (!D.bSelected || !D.WorldTransform.GetLocation().Equals(Source.GetLocation(),1.e-9)
				|| !D.WorldTransform.GetScale3D().Equals(Source.GetScale3D(),1.e-9)
				|| !Yaw.GetAxisZ().Equals(FVector::UpVector,1.e-7))
			{ AddError(TEXT("Native yaw changed translation/scale or rotated the wrong axis.")); return false; }
			FBox Envelope; if (!FEFCalystoBakedArchitecture::GetEnvelope(D.Child,Parent,Envelope,Error)) return false;
			const FBox Actual=D.Child.ExportedLocalBounds.TransformBy(D.WorldTransform);
			if (!Envelope.IsInsideOrOn(Actual.Min) || !Envelope.IsInsideOrOn(Actual.Max))
			{ AddError(TEXT("A native rotation escaped its preselection envelope.")); return false; }
		}
	}
	for (int32 I=0;I<7;++I)
	{
		const double P=Chance[I]/100, Expected=Trials*P, Tolerance=6*FMath::Sqrt(Trials*P*(1-P));
		TestTrue(FString::Printf(TEXT("100000 independent draws: native Chance %.0f%% within predeclared six sigma"),Chance[I]),
			FMath::Abs(Counts[I]-Expected)<=Tolerance);
		AddInfo(FString::Printf(TEXT("Chance %.0f%%: %d / %d, expected %.0f, six sigma %.3f"),Chance[I],Counts[I],Trials,Expected,Tolerance));
	}
	const double CdfTolerance=6*FMath::Sqrt(0.25/Trials);
	for (int32 Axis=0;Axis<2;++Axis) for (int32 K=1;K<16;++K)
		TestTrue(TEXT("100000 absolute/relative yaw draws match independent uniform CDF at 15 fixed endpoints"),
			FMath::Abs(double(CDF[Axis][K-1])/Trials-double(K)/16)<=CdfTolerance);
	AddInfo(FString::Printf(TEXT("Baked native probability/rotation calculation: %.3f seconds; 100000 trials per law; no world generation."),FPlatformTime::Seconds()-Started));
	return true;
}
#endif
