#include "Calysto/EFCalystoPCGCookedCompatibility.h"

#include "Elements/PCGAttributeGetFromPointIndexElement.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSubgraph.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoPCGCookedCompatibility, Log, All);

namespace EFCalystoPCGCookedCompatibilityPrivate
{
	static constexpr TCHAR SourceRootPath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.PCG_MassiveDungeonMaster");
	static constexpr TCHAR LegacySimplePath[] =
		TEXT("/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple.PCG_ObjectTransformSimple");
	static constexpr TCHAR CookedSimplePath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon.PCG_ObjectTransformSimpleDungeon");

	static const TSet<FString>& ExpectedClonedGraphPaths()
	{
		static const TSet<FString> Paths =
		{
			SourceRootPath,
			TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh"),
			TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps.PCG_AddRamps")
		};
		return Paths;
	}

	static const TSet<FString>& ExpectedLegacyReferenceIds()
	{
		static const TSet<FString> Ids =
		{
			TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh:Loop_1"),
			TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh:Loop_2"),
			TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh:Loop_4"),
			TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps.PCG_AddRamps:Loop_9")
		};
		return Ids;
	}

	static const TSet<FString>& ExpectedCloneRelinkIds()
	{
		static const TSet<FString> Ids =
		{
			TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.PCG_MassiveDungeonMaster:Subgraph_43"),
			TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh:Subgraph_44"),
			TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh:Subgraph_47"),
			TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh:Subgraph_52")
		};
		return Ids;
	}

	static bool SetsEqual(const TSet<FString>& Left, const TSet<FString>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (const FString& Value : Left)
		{
			if (!Right.Contains(Value))
			{
				return false;
			}
		}
		return true;
	}

	static bool HasConnectedSubgraphOverridePin(
		const UPCGSubgraphSettings* Settings,
		const UPCGNode* Node)
	{
		if (!IsValid(Settings) || !IsValid(Node))
		{
			return true;
		}
		for (const FPCGSettingsOverridableParam& Param : Settings->OverridableParams())
		{
			if (!Param.PropertiesNames.IsEmpty()
				&& Param.PropertiesNames.Last() == GET_MEMBER_NAME_CHECKED(UPCGSubgraphSettings, SubgraphOverride))
			{
				const UPCGPin* OverridePin = Node->GetInputPin(Param.Label);
				return OverridePin && OverridePin->IsConnected();
			}
		}
		return false;
	}

	static const TMap<FName, FName>& ExpectedLegacySelectors()
	{
		static const TMap<FName, FName> Selectors =
		{
			{ TEXT("GetAttributeFromPointIndex_0"), TEXT("Object Transform") },
			{ TEXT("GetAttributeFromPointIndex_5"), TEXT("Object Rotation") },
			{ TEXT("GetAttributeFromPointIndex_10"), TEXT("Object Scale") },
			{ TEXT("GetAttributeFromPointIndex_32"), TEXT("Object Uniform Scale") }
		};
		return Selectors;
	}

	static const TMap<FName, FName>& ExpectedCookedSelectors()
	{
		static const TMap<FName, FName> Selectors =
		{
			{ TEXT("GetAttributeFromPointIndex_0"), TEXT("ObjectTransform") },
			{ TEXT("GetAttributeFromPointIndex_5"), TEXT("ObjectRotation") },
			{ TEXT("GetAttributeFromPointIndex_10"), TEXT("ObjectScale") },
			{ TEXT("GetAttributeFromPointIndex_32"), TEXT("ObjectUniformScale") }
		};
		return Selectors;
	}

	static bool InvalidateDuplicatedCookedCompilationData(UPCGGraph* Graph, FString& OutError)
	{
		if (!IsValid(Graph) || !Graph->HasAnyFlags(RF_Transient))
		{
			OutError = FString::Printf(
				TEXT("Refusing to invalidate cooked PCG tasks on non-transient graph %s."),
				*GetPathNameSafe(Graph));
			return false;
		}

		// UPCGGraphCompilationData is private engine implementation data in UE 5.8,
		// but its owning UPROPERTY is intentionally reflected. DuplicateObject copies
		// that cook product along with the graph. In a non-editor executable the PCG
		// compiler consumes those copied tasks before inspecting our transient node
		// relinks, so the tasks still execute the vendor graph's legacy helper. Clear
		// only this property on the transient clone to force compilation from the
		// relinked graph; no source package or engine implementation is modified.
		FObjectProperty* CookedDataProperty = FindFProperty<FObjectProperty>(
			UPCGGraph::StaticClass(),
			TEXT("CookedCompilationData"));
		if (!CookedDataProperty
			|| !CookedDataProperty->PropertyClass
			|| CookedDataProperty->PropertyClass->GetFName() != TEXT("PCGGraphCompilationData"))
		{
			OutError = TEXT("UE 5.8 UPCGGraph::CookedCompilationData reflection contract drifted; refusing transient graph execution.");
			return false;
		}

		CookedDataProperty->SetObjectPropertyValue_InContainer(Graph, nullptr);
		if (CookedDataProperty->GetObjectPropertyValue_InContainer(Graph) != nullptr)
		{
			OutError = FString::Printf(
				TEXT("Failed to invalidate duplicated cooked PCG tasks on transient graph %s."),
				*Graph->GetPathName());
			return false;
		}
		return true;
	}

	static bool ValidateSimpleGraphSelectors(
		const UPCGGraph* Graph,
		const TMap<FName, FName>& ExpectedSelectors,
		FString& OutError)
	{
		if (!IsValid(Graph) || Graph->GetNodes().Num() != 9)
		{
			OutError = FString::Printf(
				TEXT("Calysto transform helper %s has an invalid node count; expected exactly 9."),
				*GetPathNameSafe(Graph));
			return false;
		}

		TSet<FName> SeenSelectors;
		for (const UPCGNode* Node : Graph->GetNodes())
		{
			if (!IsValid(Node))
			{
				OutError = FString::Printf(TEXT("Calysto transform helper %s contains a null node."), *Graph->GetPathName());
				return false;
			}

			const UPCGAttributeGetFromPointIndexSettings* Settings =
				Cast<UPCGAttributeGetFromPointIndexSettings>(Node->GetSettings());
			if (!Settings)
			{
				continue;
			}

			const FName* ExpectedSelector = ExpectedSelectors.Find(Node->GetFName());
			if (!ExpectedSelector || Settings->InputSource.GetName() != *ExpectedSelector)
			{
				OutError = FString::Printf(
					TEXT("Calysto transform helper %s selector %s resolved to '%s'; the frozen compatibility contract expected '%s'."),
					*Graph->GetPathName(),
					*Node->GetName(),
					*Settings->InputSource.GetName().ToString(),
					ExpectedSelector ? *ExpectedSelector->ToString() : TEXT("<no selector>"));
				return false;
			}
			SeenSelectors.Add(Node->GetFName());
		}

		if (SeenSelectors.Num() != ExpectedSelectors.Num())
		{
			OutError = FString::Printf(
				TEXT("Calysto transform helper %s exposes %d validated selectors; expected %d."),
				*Graph->GetPathName(),
				SeenSelectors.Num(),
				ExpectedSelectors.Num());
			return false;
		}
		return true;
	}

	static bool ValidateEquivalentTopology(
		const UPCGGraph* LegacyGraph,
		const UPCGGraph* CookedGraph,
		FString& OutError)
	{
		if (!IsValid(LegacyGraph) || !IsValid(CookedGraph)
			|| LegacyGraph->GetNodes().Num() != CookedGraph->GetNodes().Num())
		{
			OutError = TEXT("Legacy and cooked-safe Calysto transform helpers do not have matching node counts.");
			return false;
		}

		TMap<FName, const UPCGNode*> CookedNodes;
		for (const UPCGNode* Node : CookedGraph->GetNodes())
		{
			if (!IsValid(Node) || CookedNodes.Contains(Node->GetFName()))
			{
				OutError = TEXT("Cooked-safe Calysto transform helper contains a null or duplicate node name.");
				return false;
			}
			CookedNodes.Add(Node->GetFName(), Node);
		}

		for (const UPCGNode* LegacyNode : LegacyGraph->GetNodes())
		{
			const UPCGNode* const* CookedNodePtr = LegacyNode ? CookedNodes.Find(LegacyNode->GetFName()) : nullptr;
			const UPCGNode* CookedNode = CookedNodePtr ? *CookedNodePtr : nullptr;
			if (!LegacyNode || !CookedNode
				|| !LegacyNode->GetSettings() || !CookedNode->GetSettings()
				|| LegacyNode->GetSettings()->GetClass() != CookedNode->GetSettings()->GetClass()
				|| LegacyNode->InputPinProperties() != CookedNode->InputPinProperties()
				|| LegacyNode->OutputPinProperties() != CookedNode->OutputPinProperties())
			{
				OutError = FString::Printf(
					TEXT("Calysto transform helper topology drifted at node %s."),
					LegacyNode ? *LegacyNode->GetName() : TEXT("<null>"));
				return false;
			}
		}

		auto BuildEdgeSignature = [](const UPCGGraph* Graph, TArray<FString>& OutSignature) -> bool
		{
			OutSignature.Reset();
			for (const UPCGEdge* Edge : Graph->GetAllEdges())
			{
				if (!IsValid(Edge) || !Edge->IsValid()
					|| !IsValid(Edge->GetInputNode()) || !IsValid(Edge->GetOutputNode()))
				{
					return false;
				}
				OutSignature.Add(FString::Printf(
					TEXT("%s:%s->%s:%s"),
					*Edge->GetInputNode()->GetName(),
					*Edge->GetInputPinLabel().ToString(),
					*Edge->GetOutputNode()->GetName(),
					*Edge->GetOutputPinLabel().ToString()));
			}
			OutSignature.Sort();
			return true;
		};

		TArray<FString> LegacyEdges;
		TArray<FString> CookedEdges;
		if (!BuildEdgeSignature(LegacyGraph, LegacyEdges)
			|| !BuildEdgeSignature(CookedGraph, CookedEdges)
			|| LegacyEdges != CookedEdges)
		{
			OutError = TEXT("Legacy and cooked-safe Calysto transform helpers do not have identical canonical edge topology.");
			return false;
		}
		return true;
	}

	static bool ValidateCookedSafeRuntimeClosure(
		const UPCGGraph* RuntimeRoot,
		const UPCGGraph* LegacySimpleGraph,
		const UPCGGraph* CookedSimpleGraph,
		FString& OutError)
	{
		TSet<const UPCGGraph*> VisitedGraphs;
		int32 CookedSimpleReferenceCount = 0;

		TFunction<bool(const UPCGGraph*)> VisitGraph =
			[&](const UPCGGraph* Graph) -> bool
		{
			if (!IsValid(Graph))
			{
				OutError = TEXT("Transient Calysto runtime closure contains an invalid graph.");
				return false;
			}
			if (VisitedGraphs.Contains(Graph))
			{
				return true;
			}
			VisitedGraphs.Add(Graph);

			for (const UPCGNode* Node : Graph->GetNodes())
			{
				if (!IsValid(Node) || !IsValid(Node->GetSettings()))
				{
					OutError = FString::Printf(
						TEXT("Transient Calysto runtime closure contains an invalid node in %s."),
						*Graph->GetPathName());
					return false;
				}

				if (const UPCGAttributeGetFromPointIndexSettings* AttributeSettings =
					Cast<UPCGAttributeGetFromPointIndexSettings>(Node->GetSettings()))
				{
					const FName SelectorName = AttributeSettings->InputSource.GetName();
					for (const TPair<FName, FName>& LegacySelector : ExpectedLegacySelectors())
					{
						if (SelectorName == LegacySelector.Value)
						{
							OutError = FString::Printf(
								TEXT("Transient Calysto runtime closure still exposes cooked-unsafe selector '%s' at %s:%s."),
								*SelectorName.ToString(),
								*Graph->GetPathName(),
								*Node->GetName());
							return false;
						}
					}
				}

				const UPCGSubgraphSettings* SubgraphSettings =
					Cast<UPCGSubgraphSettings>(Node->GetSettings());
				if (!SubgraphSettings)
				{
					continue;
				}
				const UPCGGraph* ChildGraph = SubgraphSettings->GetSubgraph();
				if (!ChildGraph)
				{
					continue;
				}
				if (ChildGraph == LegacySimpleGraph)
				{
					OutError = FString::Printf(
						TEXT("Transient Calysto runtime closure still reaches the legacy helper at %s:%s."),
						*Graph->GetPathName(),
						*Node->GetName());
					return false;
				}
				if (ChildGraph == CookedSimpleGraph)
				{
					++CookedSimpleReferenceCount;
					continue;
				}
				if (!VisitGraph(ChildGraph))
				{
					return false;
				}
			}
			return true;
		};

		if (!VisitGraph(RuntimeRoot))
		{
			return false;
		}
		// The frozen root already contains three dungeon-safe helper references;
		// this adapter replaces four additional legacy references. Keep both
		// contracts explicit: ReplacedLegacySubgraphCount validates the four
		// mutations, while this closure gate validates the final total of seven.
		if (CookedSimpleReferenceCount != 7)
		{
			OutError = FString::Printf(
				TEXT("Transient Calysto runtime closure contains %d cooked-safe helper references; expected exactly 7 (3 native plus 4 transient replacements)."),
				CookedSimpleReferenceCount);
			return false;
		}
		return true;
	}

	struct FCloneContext
	{
		UPCGGraph* LegacySimpleGraph = nullptr;
		UPCGGraph* CookedSimpleGraph = nullptr;
		TMap<const UPCGGraph*, bool> ContainsLegacyMemo;
		TSet<const UPCGGraph*> ContainsLegacyStack;
		TMap<const UPCGGraph*, UPCGGraph*> Clones;
		TSet<FString> ClonedSourcePaths;
		TSet<FString> ReplacedLegacyReferenceIds;
		TSet<FString> CloneRelinkIds;
		int32 ReplacedLegacySubgraphCount = 0;
		int32 InvalidatedCookedCompilationDataCount = 0;
		FString Error;

		bool ContainsLegacy(const UPCGGraph* Graph)
		{
			if (!IsValid(Graph))
			{
				return false;
			}
			if (Graph == LegacySimpleGraph)
			{
				return true;
			}
			if (const bool* Cached = ContainsLegacyMemo.Find(Graph))
			{
				return *Cached;
			}
			if (ContainsLegacyStack.Contains(Graph))
			{
				return false;
			}

			ContainsLegacyStack.Add(Graph);
			bool bContains = false;
			for (const UPCGNode* Node : Graph->GetNodes())
			{
				const UPCGSubgraphSettings* Settings = Node
					? Cast<UPCGSubgraphSettings>(Node->GetSettings())
					: nullptr;
				if (Settings && ContainsLegacy(Settings->GetSubgraph()))
				{
					bContains = true;
					break;
				}
			}
			ContainsLegacyStack.Remove(Graph);
			ContainsLegacyMemo.Add(Graph, bContains);
			return bContains;
		}

		UPCGGraph* CloneCompatibleGraph(UPCGGraph* Source, UObject* Outer)
		{
			if (!IsValid(Source) || !IsValid(Outer))
			{
				Error = TEXT("Cannot clone an invalid Calysto graph or transient outer.");
				return nullptr;
			}
			if (Source == LegacySimpleGraph)
			{
				Error = TEXT("Legacy helper replacement requires an owning subgraph node identity.");
				return nullptr;
			}
			if (UPCGGraph** Existing = Clones.Find(Source))
			{
				return *Existing;
			}
			if (!ContainsLegacy(Source))
			{
				return Source;
			}

			const FName CloneName = MakeUniqueObjectName(
				Outer,
				Source->GetClass(),
				FName(*FString::Printf(TEXT("EFCalystoCooked_%s"), *Source->GetName())));
			UPCGGraph* Clone = DuplicateObject<UPCGGraph>(Source, Outer, CloneName);
			if (!IsValid(Clone))
			{
				Error = FString::Printf(TEXT("Failed to duplicate Calysto graph %s transiently."), *Source->GetPathName());
				return nullptr;
			}
			Clone->ClearFlags(RF_Public | RF_Standalone);
			Clone->SetFlags(RF_Transient);
			if (!InvalidateDuplicatedCookedCompilationData(Clone, Error))
			{
				return nullptr;
			}
			++InvalidatedCookedCompilationDataCount;
			Clones.Add(Source, Clone);
			ClonedSourcePaths.Add(Source->GetPathName());

			for (UPCGNode* Node : Clone->GetNodes())
			{
				UPCGSubgraphSettings* Settings = Node
					? Cast<UPCGSubgraphSettings>(Node->GetSettings())
					: nullptr;
				if (!Settings)
				{
					continue;
				}
				UPCGGraph* ChildSource = Settings->GetSubgraph();
				if (!ContainsLegacy(ChildSource))
				{
					continue;
				}
				// Only the exact references that lead to the legacy helper are touched.
				// Unrelated vendor subgraphs may legitimately use dynamic settings.
				if (!Node->IsIn(Clone)
					|| !IsValid(Node->GetSettingsInterface())
					|| !Node->GetSettingsInterface()->IsIn(Clone)
					|| !Settings->IsIn(Clone)
					|| !IsValid(Settings->SubgraphInstance)
					|| !Settings->SubgraphInstance->IsIn(Clone)
					|| IsValid(Settings->SubgraphOverride)
					|| HasConnectedSubgraphOverridePin(Settings, Node))
				{
					Error = FString::Printf(
						TEXT("Transient Calysto graph ownership or override state drifted at %s:%s; refusing to touch external settings."),
						*Source->GetPathName(),
						*Node->GetName());
					return nullptr;
				}
				UPCGGraph* ChildRuntime = nullptr;
				const FString ReferenceId = FString::Printf(TEXT("%s:%s"), *Source->GetPathName(), *Node->GetName());
				if (ChildSource == LegacySimpleGraph)
				{
					ChildRuntime = CookedSimpleGraph;
					++ReplacedLegacySubgraphCount;
					ReplacedLegacyReferenceIds.Add(ReferenceId);
				}
				else
				{
					ChildRuntime = CloneCompatibleGraph(ChildSource, Clone);
					CloneRelinkIds.Add(ReferenceId);
				}
				if (!IsValid(ChildRuntime))
				{
					return nullptr;
				}
				Settings->SetSubgraph(ChildRuntime);
				if (IsValid(Settings->SubgraphOverride)
					|| HasConnectedSubgraphOverridePin(Settings, Node)
					|| Settings->GetSubgraph() != ChildRuntime
					|| !IsValid(Settings->SubgraphInstance)
					|| Settings->SubgraphInstance->GetGraph() != ChildRuntime)
				{
					Error = FString::Printf(
						TEXT("Transient Calysto subgraph replacement did not become authoritative at %s."),
						*ReferenceId);
					return nullptr;
				}
			}
			return Clone;
		}
	};
}

FEFCalystoPCGCookedCompatibilityResult FEFCalystoPCGCookedCompatibility::TryBuild(
	UPCGGraph* SourceRootGraph,
	UObject* TransientOuter,
	const bool bForceCookedRulesForAutomation)
{
	using namespace EFCalystoPCGCookedCompatibilityPrivate;

	FEFCalystoPCGCookedCompatibilityResult Result;
	auto Fail = [&Result](FString&& Reason)
	{
		Result.FailureReason = MoveTemp(Reason);
		return Result;
	};
	if (!IsInGameThread())
	{
		return Fail(TEXT("Cooked compatibility must be built on the game thread."));
	}

	if (!IsValid(SourceRootGraph) || SourceRootGraph->GetPathName() != SourceRootPath)
	{
		return Fail(FString::Printf(
			TEXT("Cooked compatibility source is %s; expected exact root %s."),
			*GetPathNameSafe(SourceRootGraph),
			SourceRootPath));
	}
	if (!IsValid(TransientOuter))
	{
		return Fail(TEXT("Cooked compatibility requires a valid transient outer."));
	}

#if WITH_EDITOR
	if (!bForceCookedRulesForAutomation)
	{
		Result.RuntimeGraph = SourceRootGraph;
		Result.bApplied = true;
		return Result;
	}
#else
	(void)bForceCookedRulesForAutomation;
#endif

	UPCGGraph* LegacySimpleGraph = LoadObject<UPCGGraph>(nullptr, LegacySimplePath);
	UPCGGraph* CookedSimpleGraph = LoadObject<UPCGGraph>(nullptr, CookedSimplePath);
	FString Error;
	if (!ValidateSimpleGraphSelectors(LegacySimpleGraph, ExpectedLegacySelectors(), Error)
		|| !ValidateSimpleGraphSelectors(CookedSimpleGraph, ExpectedCookedSelectors(), Error)
		|| !ValidateEquivalentTopology(LegacySimpleGraph, CookedSimpleGraph, Error))
	{
		return Fail(MoveTemp(Error));
	}

	FCloneContext Context;
	Context.LegacySimpleGraph = LegacySimpleGraph;
	Context.CookedSimpleGraph = CookedSimpleGraph;
	if (!Context.ContainsLegacy(SourceRootGraph))
	{
		return Fail(TEXT("The exact Calysto root no longer reaches the legacy Object Transform helper."));
	}

	UPCGGraph* RuntimeGraph = Context.CloneCompatibleGraph(SourceRootGraph, TransientOuter);
	if (!IsValid(RuntimeGraph))
	{
		return Fail(Context.Error.IsEmpty()
			? TEXT("Failed to create the transient cooked-compatible Calysto graph chain.")
			: MoveTemp(Context.Error));
	}
	if (Context.ReplacedLegacySubgraphCount != 4)
	{
		return Fail(FString::Printf(
			TEXT("Cooked compatibility replaced %d legacy helpers; the validated Calysto contract requires exactly 4."),
			Context.ReplacedLegacySubgraphCount));
	}
	if (!SetsEqual(Context.ReplacedLegacyReferenceIds, ExpectedLegacyReferenceIds()))
	{
		TArray<FString> Ids = Context.ReplacedLegacyReferenceIds.Array();
		Ids.Sort();
		return Fail(FString::Printf(
			TEXT("Cooked compatibility replaced unexpected legacy subgraph nodes (%s)."),
			*FString::Join(Ids, TEXT(", "))));
	}
	if (!SetsEqual(Context.CloneRelinkIds, ExpectedCloneRelinkIds()))
	{
		TArray<FString> Ids = Context.CloneRelinkIds.Array();
		Ids.Sort();
		return Fail(FString::Printf(
			TEXT("Cooked compatibility reconnected an unexpected transient graph chain (%s)."),
			*FString::Join(Ids, TEXT(", "))));
	}
	if (!SetsEqual(Context.ClonedSourcePaths, ExpectedClonedGraphPaths()))
	{
		TArray<FString> Paths = Context.ClonedSourcePaths.Array();
		Paths.Sort();
		return Fail(FString::Printf(
			TEXT("Cooked compatibility cloned an unexpected graph set (%s); expected only Master, SetDungeonMesh and AddRamps."),
			*FString::Join(Paths, TEXT(", "))));
	}
	if (!ValidateCookedSafeRuntimeClosure(
		RuntimeGraph,
		LegacySimpleGraph,
		CookedSimpleGraph,
		Error))
	{
		return Fail(MoveTemp(Error));
	}

	Result.RuntimeGraph = RuntimeGraph;
	Result.ClonedGraphCount = Context.ClonedSourcePaths.Num();
	Result.ReplacedLegacySubgraphCount = Context.ReplacedLegacySubgraphCount;
	Result.InvalidatedCookedCompilationDataCount = Context.InvalidatedCookedCompilationDataCount;
	Result.bApplied = true;
	UE_LOG(
		LogEFCalystoPCGCookedCompatibility,
		Log,
		TEXT("PASS source=%s runtime=%s transientClones=%d invalidatedCookedTaskCaches=%d cookedSafeSubgraphSwaps=%d validatedCookedSafeClosure=1 vendorAssetsMutated=0."),
		*SourceRootGraph->GetPathName(),
		*RuntimeGraph->GetPathName(),
		Result.ClonedGraphCount,
		Result.InvalidatedCookedCompilationDataCount,
		Result.ReplacedLegacySubgraphCount);
	return Result;
}
