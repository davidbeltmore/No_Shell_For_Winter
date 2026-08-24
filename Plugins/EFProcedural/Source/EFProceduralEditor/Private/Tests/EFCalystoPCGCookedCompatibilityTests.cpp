#include "Calysto/EFCalystoPCGCookedCompatibility.h"

#include "Misc/AutomationTest.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSubgraph.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV4PCGCookedCompatibilityTest,
	"NoShellForWinter.CalystoDungeon.V4.PCG.CookedUserDefinedStructCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV4PCGCookedCompatibilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	static const TCHAR* RootPath =
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.PCG_MassiveDungeonMaster");

	UPCGGraph* RootGraph = LoadObject<UPCGGraph>(nullptr, RootPath);
	TestNotNull(TEXT("The exact Calysto master graph is available."), RootGraph);
	if (!RootGraph)
	{
		return false;
	}
	auto FindSubgraph = [this](UPCGGraph* Graph, const FName NodeName) -> UPCGGraph*
	{
		if (!Graph)
		{
			return nullptr;
		}
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && Node->GetFName() == NodeName)
			{
				const UPCGSubgraphSettings* Settings = Cast<UPCGSubgraphSettings>(Node->GetSettings());
				TestNotNull(*FString::Printf(TEXT("Expected subgraph settings at %s:%s."), *Graph->GetPathName(), *NodeName.ToString()), Settings);
				return Settings ? Settings->GetSubgraph() : nullptr;
			}
		}
		AddError(FString::Printf(TEXT("Expected node %s was not found in %s."), *NodeName.ToString(), *Graph->GetPathName()));
		return nullptr;
	};

	const TArray<FString> ProtectedGraphPaths =
	{
		RootPath,
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh"),
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps.PCG_AddRamps"),
		TEXT("/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple.PCG_ObjectTransformSimple"),
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon.PCG_ObjectTransformSimpleDungeon")
	};
	TArray<UPackage*> ProtectedPackages;
	for (const FString& Path : ProtectedGraphPaths)
	{
		UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *Path);
		TestNotNull(*FString::Printf(TEXT("Protected graph loads: %s"), *Path), Graph);
		if (!Graph)
		{
			return false;
		}
		ProtectedPackages.Add(Graph->GetOutermost());
		TestFalse(*FString::Printf(TEXT("Protected graph starts clean: %s"), *Path), Graph->GetOutermost()->IsDirty());
	}
	UPCGGraph* SourceSetDungeonMesh = LoadObject<UPCGGraph>(nullptr,
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh"));
	UPCGGraph* SourceAddRamps = LoadObject<UPCGGraph>(nullptr,
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps.PCG_AddRamps"));
	UPCGGraph* SourceLegacyHelper = LoadObject<UPCGGraph>(nullptr,
		TEXT("/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple.PCG_ObjectTransformSimple"));
	UPCGGraph* CookedHelper = LoadObject<UPCGGraph>(nullptr,
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon.PCG_ObjectTransformSimpleDungeon"));
	TestTrue(TEXT("The frozen source root reaches SetDungeonMesh at Subgraph_43."),
		FindSubgraph(RootGraph, TEXT("Subgraph_43")) == SourceSetDungeonMesh);
	for (const FName NodeName : { FName(TEXT("Subgraph_44")), FName(TEXT("Subgraph_47")), FName(TEXT("Subgraph_52")) })
	{
		TestTrue(*FString::Printf(TEXT("The frozen SetDungeonMesh source reaches AddRamps at %s."), *NodeName.ToString()),
			FindSubgraph(SourceSetDungeonMesh, NodeName) == SourceAddRamps);
	}
	for (const FName NodeName : { FName(TEXT("Loop_1")), FName(TEXT("Loop_2")), FName(TEXT("Loop_4")) })
	{
		TestTrue(*FString::Printf(TEXT("The frozen SetDungeonMesh source keeps its legacy helper at %s."), *NodeName.ToString()),
			FindSubgraph(SourceSetDungeonMesh, NodeName) == SourceLegacyHelper);
	}
	TestTrue(TEXT("The frozen AddRamps source keeps its legacy helper at Loop_9."),
		FindSubgraph(SourceAddRamps, TEXT("Loop_9")) == SourceLegacyHelper);

	const FEFCalystoPCGCookedCompatibilityResult EditorResult =
		FEFCalystoPCGCookedCompatibility::TryBuild(RootGraph, GetTransientPackage());
	TestTrue(TEXT("Normal Editor execution leaves the original graph authoritative."), EditorResult.bApplied);
	TestTrue(TEXT("Normal Editor execution returns the exact original graph."), EditorResult.RuntimeGraph == RootGraph);
	TestEqual(TEXT("Normal Editor execution creates no graph clones."), EditorResult.ClonedGraphCount, 0);
	TestEqual(TEXT("Normal Editor execution swaps no subgraphs."), EditorResult.ReplacedLegacySubgraphCount, 0);

	const FEFCalystoPCGCookedCompatibilityResult CookedSimulation =
		FEFCalystoPCGCookedCompatibility::TryBuild(RootGraph, GetTransientPackage(), true);
	TestTrue(*FString::Printf(TEXT("Cooked compatibility succeeds: %s"), *CookedSimulation.FailureReason), CookedSimulation.bApplied);
	TestNotNull(TEXT("Cooked compatibility returns a runtime graph."), CookedSimulation.RuntimeGraph);
	TestTrue(TEXT("Cooked compatibility returns a distinct transient root."),
		CookedSimulation.RuntimeGraph && CookedSimulation.RuntimeGraph != RootGraph);
	TestTrue(TEXT("Cooked compatibility root is transient."),
		CookedSimulation.RuntimeGraph && CookedSimulation.RuntimeGraph->HasAnyFlags(RF_Transient));
	TestEqual(TEXT("Only Master, SetDungeonMesh and AddRamps are cloned."), CookedSimulation.ClonedGraphCount, 3);
	TestEqual(TEXT("Every transient clone invalidates copied cooked task data."),
		CookedSimulation.InvalidatedCookedCompilationDataCount, 3);
	TestEqual(TEXT("Exactly four cooked-unsafe helper references are replaced."),
		CookedSimulation.ReplacedLegacySubgraphCount, 4);

	UPCGGraph* RuntimeSetDungeonMesh = FindSubgraph(CookedSimulation.RuntimeGraph, TEXT("Subgraph_43"));
	TestTrue(TEXT("SetDungeonMesh is a distinct transient clone."),
		RuntimeSetDungeonMesh && RuntimeSetDungeonMesh != SourceSetDungeonMesh && RuntimeSetDungeonMesh->HasAnyFlags(RF_Transient));
	UPCGGraph* RuntimeAddRamps = FindSubgraph(RuntimeSetDungeonMesh, TEXT("Subgraph_44"));
	TestTrue(TEXT("AddRamps is a distinct transient clone."),
		RuntimeAddRamps && RuntimeAddRamps != SourceAddRamps && RuntimeAddRamps->HasAnyFlags(RF_Transient));
	FObjectProperty* CookedDataProperty = FindFProperty<FObjectProperty>(
		UPCGGraph::StaticClass(),
		TEXT("CookedCompilationData"));
	TestNotNull(TEXT("UE 5.8 exposes the cooked PCG task cache property used by the transient adapter."), CookedDataProperty);
	for (UPCGGraph* RuntimeClone : { CookedSimulation.RuntimeGraph, RuntimeSetDungeonMesh, RuntimeAddRamps })
	{
		TestNull(
			*FString::Printf(TEXT("Transient graph %s cannot retain source cooked task data."), *GetPathNameSafe(RuntimeClone)),
			CookedDataProperty && RuntimeClone
				? CookedDataProperty->GetObjectPropertyValue_InContainer(RuntimeClone)
				: nullptr);
	}
	for (const FName NodeName : { FName(TEXT("Subgraph_47")), FName(TEXT("Subgraph_52")) })
	{
		TestTrue(*FString::Printf(TEXT("Every SetDungeonMesh relink shares the one AddRamps clone at %s."), *NodeName.ToString()),
			FindSubgraph(RuntimeSetDungeonMesh, NodeName) == RuntimeAddRamps);
	}
	for (const FName NodeName : { FName(TEXT("Loop_1")), FName(TEXT("Loop_2")), FName(TEXT("Loop_4")) })
	{
		TestTrue(*FString::Printf(TEXT("SetDungeonMesh uses the cooked-safe helper at %s."), *NodeName.ToString()),
			FindSubgraph(RuntimeSetDungeonMesh, NodeName) == CookedHelper);
	}
	TestTrue(TEXT("AddRamps uses the cooked-safe helper at Loop_9."),
		FindSubgraph(RuntimeAddRamps, TEXT("Loop_9")) == CookedHelper);

	// A second independent build proves the first operation did not mutate the source chain.
	const FEFCalystoPCGCookedCompatibilityResult RepeatSimulation =
		FEFCalystoPCGCookedCompatibility::TryBuild(RootGraph, GetTransientPackage(), true);
	TestTrue(*FString::Printf(TEXT("A repeat compatibility build remains valid: %s"), *RepeatSimulation.FailureReason),
		RepeatSimulation.bApplied);
	TestEqual(TEXT("The repeat build still observes four source references."),
		RepeatSimulation.ReplacedLegacySubgraphCount, 4);
	TestTrue(TEXT("The source root remains connected to the source SetDungeonMesh graph."),
		FindSubgraph(RootGraph, TEXT("Subgraph_43")) == SourceSetDungeonMesh);
	for (const FName NodeName : { FName(TEXT("Loop_1")), FName(TEXT("Loop_2")), FName(TEXT("Loop_4")) })
	{
		TestTrue(*FString::Printf(TEXT("The source SetDungeonMesh reference remains untouched at %s."), *NodeName.ToString()),
			FindSubgraph(SourceSetDungeonMesh, NodeName) == SourceLegacyHelper);
	}
	TestTrue(TEXT("The source AddRamps reference remains untouched at Loop_9."),
		FindSubgraph(SourceAddRamps, TEXT("Loop_9")) == SourceLegacyHelper);

	for (UPackage* Package : ProtectedPackages)
	{
		TestFalse(*FString::Printf(TEXT("Compatibility never dirties vendor package %s."), *GetPathNameSafe(Package)),
			Package && Package->IsDirty());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
