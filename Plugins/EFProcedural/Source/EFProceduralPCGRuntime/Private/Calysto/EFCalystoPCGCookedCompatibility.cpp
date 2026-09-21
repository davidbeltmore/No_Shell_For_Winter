#include "Calysto/EFCalystoPCGCookedCompatibility.h"

#include "Elements/PCGAttributeGetFromPointIndexElement.h"
#include "PCGCommon.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSubgraph.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoPCGCookedCompatibility, Log, All);

namespace EFCalystoPCGCookedCompatibilityPrivate
{
	static constexpr TCHAR SourceRootPath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.PCG_MassiveDungeonMaster");
	static constexpr TCHAR SourceSetDungeonMeshPath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh");
	static constexpr TCHAR SourceAddRampsPath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps.PCG_AddRamps");
	static constexpr TCHAR LegacySimplePath[] =
		TEXT("/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple.PCG_ObjectTransformSimple");
	static constexpr TCHAR CookedSimplePath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon.PCG_ObjectTransformSimpleDungeon");
	static constexpr TCHAR InternalSetDungeonMeshPath[] =
		TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_SetDungeonMeshCookedSafe.PCG_SetDungeonMeshCookedSafe");
	static constexpr TCHAR InternalAddRampsPath[] =
		TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_AddRampsCookedSafe.PCG_AddRampsCookedSafe");

	static const FName MasterSetDungeonMeshNode(TEXT("Subgraph_43"));
	static const TArray<FName>& SetDungeonMeshLegacyNodes()
	{
		static const TArray<FName> Names =
		{
			TEXT("Loop_1"), TEXT("Loop_2"), TEXT("Loop_4")
		};
		return Names;
	}

	static const TArray<FName>& SetDungeonMeshAddRampsNodes()
	{
		static const TArray<FName> Names =
		{
			TEXT("Subgraph_44"), TEXT("Subgraph_47"), TEXT("Subgraph_52")
		};
		return Names;
	}

	static const FName AddRampsLegacyNode(TEXT("Loop_9"));

	struct FResidentClosure
	{
		UPCGGraph* SourceSetDungeonMesh = nullptr;
		UPCGGraph* SourceAddRamps = nullptr;
		UPCGGraph* LegacySimple = nullptr;
		UPCGGraph* CookedSimple = nullptr;
		UPCGGraph* InternalSetDungeonMesh = nullptr;
		UPCGGraph* InternalAddRamps = nullptr;
	};

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

	static UPCGGraph* ResolveResidentGraph(
		const TCHAR* ExactPath,
		const TCHAR* Label,
		FString& OutError)
	{
		UPCGGraph* Graph = Cast<UPCGGraph>(FSoftObjectPath(ExactPath).ResolveObject());
		if (!IsValid(Graph))
		{
			OutError = FString::Printf(
				TEXT("Cooked compatibility requires resident %s graph %s; synchronous recovery is forbidden."),
				Label,
				ExactPath);
			return nullptr;
		}
		if (Graph->GetPathName() != ExactPath)
		{
			OutError = FString::Printf(
				TEXT("Resident %s graph resolved through an unexpected path: actual=%s expected=%s."),
				Label,
				*Graph->GetPathName(),
				ExactPath);
			return nullptr;
		}
		return Graph;
	}

	static bool ResolveResidentClosure(FResidentClosure& OutClosure, FString& OutError)
	{
		OutClosure = FResidentClosure();
		OutClosure.SourceSetDungeonMesh = ResolveResidentGraph(
			SourceSetDungeonMeshPath, TEXT("vendor SetDungeonMesh"), OutError);
		OutClosure.SourceAddRamps = ResolveResidentGraph(
			SourceAddRampsPath, TEXT("vendor AddRamps"), OutError);
		OutClosure.LegacySimple = ResolveResidentGraph(
			LegacySimplePath, TEXT("vendor legacy transform helper"), OutError);
		OutClosure.CookedSimple = ResolveResidentGraph(
			CookedSimplePath, TEXT("vendor cooked-safe transform helper"), OutError);
		OutClosure.InternalSetDungeonMesh = ResolveResidentGraph(
			InternalSetDungeonMeshPath, TEXT("project-owned SetDungeonMesh closure"), OutError);
		OutClosure.InternalAddRamps = ResolveResidentGraph(
			InternalAddRampsPath, TEXT("project-owned AddRamps closure"), OutError);
		return IsValid(OutClosure.SourceSetDungeonMesh)
			&& IsValid(OutClosure.SourceAddRamps)
			&& IsValid(OutClosure.LegacySimple)
			&& IsValid(OutClosure.CookedSimple)
			&& IsValid(OutClosure.InternalSetDungeonMesh)
			&& IsValid(OutClosure.InternalAddRamps);
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

	static bool InvalidateDuplicatedCookedCompilationData(UPCGGraph* Graph, FString& OutError)
	{
		if (!IsValid(Graph) || !Graph->HasAnyFlags(RF_Transient))
		{
			OutError = FString::Printf(
				TEXT("Refusing to invalidate cooked PCG tasks on non-transient graph %s."),
				*GetPathNameSafe(Graph));
			return false;
		}

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
				OutError = FString::Printf(
					TEXT("Calysto transform helper %s contains a null node."),
					*Graph->GetPathName());
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
					TEXT("Calysto transform helper %s selector %s resolved to '%s'; expected '%s'."),
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

	static bool BuildEdgeSignature(const UPCGGraph* Graph, TArray<FString>& OutSignature)
	{
		OutSignature.Reset();
		if (!IsValid(Graph))
		{
			return false;
		}
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
	}

	static bool ValidateEquivalentStructure(
		const UPCGGraph* Source,
		const UPCGGraph* Candidate,
		FString& OutError)
	{
		if (!IsValid(Source) || !IsValid(Candidate)
			|| Source->GetNodes().Num() != Candidate->GetNodes().Num())
		{
			OutError = FString::Printf(
				TEXT("PCG graph structure count differs: source=%s candidate=%s."),
				*GetPathNameSafe(Source),
				*GetPathNameSafe(Candidate));
			return false;
		}

		TMap<FName, const UPCGNode*> CandidateNodes;
		for (const UPCGNode* Node : Candidate->GetNodes())
		{
			if (!IsValid(Node) || CandidateNodes.Contains(Node->GetFName()))
			{
				OutError = FString::Printf(
					TEXT("Candidate PCG graph %s contains a null or duplicate node name."),
					*Candidate->GetPathName());
				return false;
			}
			CandidateNodes.Add(Node->GetFName(), Node);
		}

		for (const UPCGNode* SourceNode : Source->GetNodes())
		{
			const UPCGNode* const* CandidateNodePtr =
				SourceNode ? CandidateNodes.Find(SourceNode->GetFName()) : nullptr;
			const UPCGNode* CandidateNode = CandidateNodePtr ? *CandidateNodePtr : nullptr;
			if (!SourceNode || !CandidateNode
				|| !SourceNode->GetSettings() || !CandidateNode->GetSettings()
				|| SourceNode->GetSettings()->GetClass() != CandidateNode->GetSettings()->GetClass()
				|| SourceNode->GetAuthoredTitleName() != CandidateNode->GetAuthoredTitleName()
				|| SourceNode->InputPinProperties() != CandidateNode->InputPinProperties()
				|| SourceNode->OutputPinProperties() != CandidateNode->OutputPinProperties())
			{
				OutError = FString::Printf(
					TEXT("PCG graph structure drifted at node %s between %s and %s."),
					SourceNode ? *SourceNode->GetName() : TEXT("<null>"),
					*Source->GetPathName(),
					*Candidate->GetPathName());
				return false;
			}
		}

		TArray<FString> SourceEdges;
		TArray<FString> CandidateEdges;
		if (!BuildEdgeSignature(Source, SourceEdges)
			|| !BuildEdgeSignature(Candidate, CandidateEdges)
			|| SourceEdges != CandidateEdges)
		{
			OutError = FString::Printf(
				TEXT("PCG graph edge topology differs between %s and %s."),
				*Source->GetPathName(),
				*Candidate->GetPathName());
			return false;
		}
		return true;
	}

	static const UPCGNode* FindUniqueNode(
		const UPCGGraph* Graph,
		const FName NodeName,
		FString& OutError)
	{
		const UPCGNode* Result = nullptr;
		if (!IsValid(Graph))
		{
			OutError = TEXT("Cannot find a required node in an invalid PCG graph.");
			return nullptr;
		}
		for (const UPCGNode* Node : Graph->GetNodes())
		{
			if (!IsValid(Node) || Node->GetFName() != NodeName)
			{
				continue;
			}
			if (Result)
			{
				OutError = FString::Printf(
					TEXT("Graph %s contains duplicate node name %s."),
					*Graph->GetPathName(),
					*NodeName.ToString());
				return nullptr;
			}
			Result = Node;
		}
		if (!Result)
		{
			OutError = FString::Printf(
				TEXT("Graph %s no longer contains required node %s."),
				*GetPathNameSafe(Graph),
				*NodeName.ToString());
		}
		return Result;
	}

	static const UPCGSubgraphSettings* ValidateSubgraphReference(
		const UPCGGraph* Graph,
		const FName NodeName,
		const UPCGGraph* ExpectedTarget,
		FString& OutError)
	{
		const UPCGNode* Node = FindUniqueNode(Graph, NodeName, OutError);
		const UPCGSubgraphSettings* Settings = Node
			? Cast<UPCGSubgraphSettings>(Node->GetSettings())
			: nullptr;
		if (!Node || !Settings
			|| !Node->IsIn(Graph)
			|| !IsValid(Node->GetSettingsInterface())
			|| !Node->GetSettingsInterface()->IsIn(Graph)
			|| !Settings->IsIn(Graph)
			|| !IsValid(Settings->SubgraphInstance)
			|| !Settings->SubgraphInstance->IsIn(Graph)
			|| IsValid(Settings->SubgraphOverride)
			|| HasConnectedSubgraphOverridePin(Settings, Node)
			|| Settings->GetSubgraph() != ExpectedTarget
			|| Settings->SubgraphInstance->GetGraph() != ExpectedTarget)
		{
			OutError = FString::Printf(
				TEXT("Subgraph contract drifted at %s:%s; expected exact target %s with local settings and no override."),
				*GetPathNameSafe(Graph),
				*NodeName.ToString(),
				*GetPathNameSafe(ExpectedTarget));
			return nullptr;
		}
		return Settings;
	}

	static int32 CountDirectSubgraphReferences(
		const UPCGGraph* Graph,
		const UPCGGraph* Target)
	{
		int32 Count = 0;
		if (!IsValid(Graph) || !IsValid(Target))
		{
			return 0;
		}
		for (const UPCGNode* Node : Graph->GetNodes())
		{
			const UPCGSubgraphSettings* Settings = Node
				? Cast<UPCGSubgraphSettings>(Node->GetSettings())
				: nullptr;
			if (Settings && Settings->GetSubgraph() == Target)
			{
				++Count;
			}
		}
		return Count;
	}

	static bool ValidateExactSubgraphReferences(
		const UPCGGraph* Source,
		const UPCGGraph* Candidate,
		const TMap<FName, const UPCGGraph*>& Replacements,
		FString& OutError)
	{
		if (!ValidateEquivalentStructure(Source, Candidate, OutError))
		{
			return false;
		}

		TMap<FName, const UPCGNode*> CandidateNodes;
		for (const UPCGNode* Node : Candidate->GetNodes())
		{
			CandidateNodes.Add(Node->GetFName(), Node);
		}
		TSet<FName> SeenReplacements;
		for (const UPCGNode* SourceNode : Source->GetNodes())
		{
			const UPCGSubgraphSettings* SourceSettings = SourceNode
				? Cast<UPCGSubgraphSettings>(SourceNode->GetSettings())
				: nullptr;
			const UPCGNode* const* CandidateNodePtr = SourceNode
				? CandidateNodes.Find(SourceNode->GetFName())
				: nullptr;
			const UPCGNode* CandidateNode = CandidateNodePtr ? *CandidateNodePtr : nullptr;
			const UPCGSubgraphSettings* CandidateSettings = CandidateNode
				? Cast<UPCGSubgraphSettings>(CandidateNode->GetSettings())
				: nullptr;
			if (!!SourceSettings != !!CandidateSettings)
			{
				OutError = FString::Printf(
					TEXT("Subgraph settings class parity drifted at %s:%s."),
					*Source->GetPathName(),
					SourceNode ? *SourceNode->GetName() : TEXT("<null>"));
				return false;
			}
			if (!SourceSettings)
			{
				continue;
			}

			const UPCGGraph* const* Replacement = Replacements.Find(SourceNode->GetFName());
			const UPCGGraph* ExpectedTarget = Replacement
				? *Replacement
				: SourceSettings->GetSubgraph();
			if (Replacement)
			{
				SeenReplacements.Add(SourceNode->GetFName());
			}
			if (!ValidateSubgraphReference(Candidate, SourceNode->GetFName(), ExpectedTarget, OutError))
			{
				return false;
			}
		}
		if (SeenReplacements.Num() != Replacements.Num())
		{
			OutError = FString::Printf(
				TEXT("Graph %s applied %d known subgraph substitutions; expected %d."),
				*Candidate->GetPathName(),
				SeenReplacements.Num(),
				Replacements.Num());
			return false;
		}
		return true;
	}

	static bool ValidateFrozenVendorReferenceContract(
		const FResidentClosure& Closure,
		FString& OutError)
	{
		if (!ValidateSubgraphReference(
			Closure.SourceSetDungeonMesh,
			SetDungeonMeshLegacyNodes()[0],
			Closure.LegacySimple,
			OutError))
		{
			return false;
		}
		for (const FName NodeName : SetDungeonMeshLegacyNodes())
		{
			if (!ValidateSubgraphReference(
				Closure.SourceSetDungeonMesh, NodeName, Closure.LegacySimple, OutError))
			{
				return false;
			}
		}
		for (const FName NodeName : SetDungeonMeshAddRampsNodes())
		{
			if (!ValidateSubgraphReference(
				Closure.SourceSetDungeonMesh, NodeName, Closure.SourceAddRamps, OutError))
			{
				return false;
			}
		}
		if (!ValidateSubgraphReference(
			Closure.SourceAddRamps,
			AddRampsLegacyNode,
			Closure.LegacySimple,
			OutError))
		{
			return false;
		}
		const int32 SetLegacySimpleCount = CountDirectSubgraphReferences(
			Closure.SourceSetDungeonMesh, Closure.LegacySimple);
		const int32 SetCookedSimpleCount = CountDirectSubgraphReferences(
			Closure.SourceSetDungeonMesh, Closure.CookedSimple);
		const int32 SetAddRampsCount = CountDirectSubgraphReferences(
			Closure.SourceSetDungeonMesh, Closure.SourceAddRamps);
		const int32 AddRampsLegacySimpleCount = CountDirectSubgraphReferences(
			Closure.SourceAddRamps, Closure.LegacySimple);
		const int32 AddRampsCookedSimpleCount = CountDirectSubgraphReferences(
			Closure.SourceAddRamps, Closure.CookedSimple);
		if (SetLegacySimpleCount != 3
			|| SetCookedSimpleCount != 3
			|| SetAddRampsCount != 3
			|| AddRampsLegacySimpleCount != 1
			|| AddRampsCookedSimpleCount != 0)
		{
			OutError = FString::Printf(
				TEXT("Frozen vendor closure cardinality drifted: Set->LegacySimple=%d, Set->CookedSimple=%d, Set->AddRamps=%d, AddRamps->LegacySimple=%d, AddRamps->CookedSimple=%d; expected 3/3/3/1/0."),
				SetLegacySimpleCount,
				SetCookedSimpleCount,
				SetAddRampsCount,
				AddRampsLegacySimpleCount,
				AddRampsCookedSimpleCount);
			return false;
		}
		return true;
	}

	static bool ValidateInternalClosure(
		const FResidentClosure& Closure,
		FString& OutError)
	{
		if (!ValidateSimpleGraphSelectors(
			Closure.LegacySimple, ExpectedLegacySelectors(), OutError)
			|| !ValidateSimpleGraphSelectors(
				Closure.CookedSimple, ExpectedCookedSelectors(), OutError)
			|| !ValidateEquivalentStructure(
				Closure.LegacySimple, Closure.CookedSimple, OutError)
			|| !ValidateFrozenVendorReferenceContract(Closure, OutError))
		{
			return false;
		}

		if (Closure.InternalSetDungeonMesh->GetPathName() != InternalSetDungeonMeshPath
			|| Closure.InternalAddRamps->GetPathName() != InternalAddRampsPath
			|| Closure.InternalSetDungeonMesh->HasAnyFlags(RF_Transient)
			|| Closure.InternalAddRamps->HasAnyFlags(RF_Transient))
		{
			OutError = TEXT("Calysto V6 cooked-safe closure must use the two exact persistent /EFProcedural internal graph identities.");
			return false;
		}

		TMap<FName, const UPCGGraph*> SetReplacements;
		for (const FName NodeName : SetDungeonMeshLegacyNodes())
		{
			SetReplacements.Add(NodeName, Closure.CookedSimple);
		}
		for (const FName NodeName : SetDungeonMeshAddRampsNodes())
		{
			SetReplacements.Add(NodeName, Closure.InternalAddRamps);
		}
		TMap<FName, const UPCGGraph*> AddRampsReplacements;
		AddRampsReplacements.Add(AddRampsLegacyNode, Closure.CookedSimple);

		if (!ValidateExactSubgraphReferences(
			Closure.SourceSetDungeonMesh,
			Closure.InternalSetDungeonMesh,
			SetReplacements,
			OutError)
			|| !ValidateExactSubgraphReferences(
				Closure.SourceAddRamps,
				Closure.InternalAddRamps,
				AddRampsReplacements,
				OutError))
		{
			return false;
		}

		const int32 SetCookedSimpleCount = CountDirectSubgraphReferences(
			Closure.InternalSetDungeonMesh, Closure.CookedSimple);
		const int32 SetInternalAddRampsCount = CountDirectSubgraphReferences(
			Closure.InternalSetDungeonMesh, Closure.InternalAddRamps);
		const int32 AddRampsCookedSimpleCount = CountDirectSubgraphReferences(
			Closure.InternalAddRamps, Closure.CookedSimple);
		const int32 SetLegacySimpleCount = CountDirectSubgraphReferences(
			Closure.InternalSetDungeonMesh, Closure.LegacySimple);
		const int32 AddRampsLegacySimpleCount = CountDirectSubgraphReferences(
			Closure.InternalAddRamps, Closure.LegacySimple);
		if (SetCookedSimpleCount != 6
			|| SetInternalAddRampsCount != 3
			|| AddRampsCookedSimpleCount != 1
			|| SetLegacySimpleCount != 0
			|| AddRampsLegacySimpleCount != 0)
		{
			OutError = FString::Printf(
				TEXT("Calysto V6 internal closure reference cardinality drifted: Set->CookedSimple=%d, Set->InternalAddRamps=%d, AddRamps->CookedSimple=%d, Set->LegacySimple=%d, AddRamps->LegacySimple=%d; expected 6/3/1/0/0 (three pre-existing cooked-safe helper references plus three reviewed substitutions)."),
				SetCookedSimpleCount,
				SetInternalAddRampsCount,
				AddRampsCookedSimpleCount,
				SetLegacySimpleCount,
				AddRampsLegacySimpleCount);
			return false;
		}
		return true;
	}

	static bool ValidateCookedSafeRuntimeClosure(
		const UPCGGraph* RuntimeRoot,
		const FResidentClosure& Closure,
		FString& OutError)
	{
		TSet<const UPCGGraph*> VisitedGraphs;
		int32 CookedSimpleReferenceCount = 0;
		int32 InternalSetReferenceCount = 0;
		int32 InternalAddReferenceCount = 0;
		int32 TransientGraphCount = 0;

		TFunction<bool(const UPCGGraph*)> VisitGraph =
			[&](const UPCGGraph* Graph) -> bool
		{
			if (!IsValid(Graph))
			{
				OutError = TEXT("Calysto cooked runtime closure contains an invalid graph.");
				return false;
			}
			if (VisitedGraphs.Contains(Graph))
			{
				return true;
			}
			VisitedGraphs.Add(Graph);
			if (Graph->HasAnyFlags(RF_Transient))
			{
				++TransientGraphCount;
				if (Graph != RuntimeRoot)
				{
					OutError = FString::Printf(
						TEXT("Cooked compatibility created an unexpected nested transient graph %s."),
						*Graph->GetPathName());
					return false;
				}
			}

			for (const UPCGNode* Node : Graph->GetNodes())
			{
				if (!IsValid(Node) || !IsValid(Node->GetSettings()))
				{
					OutError = FString::Printf(
						TEXT("Calysto cooked runtime closure contains an invalid node in %s."),
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
								TEXT("Cooked runtime closure still exposes unsafe selector '%s' at %s:%s."),
								*SelectorName.ToString(),
								*Graph->GetPathName(),
								*Node->GetName());
							return false;
						}
					}
				}

				const UPCGSubgraphSettings* Settings =
					Cast<UPCGSubgraphSettings>(Node->GetSettings());
				const UPCGGraph* ChildGraph = Settings ? Settings->GetSubgraph() : nullptr;
				if (!ChildGraph)
				{
					continue;
				}
				if (ChildGraph == Closure.LegacySimple
					|| ChildGraph == Closure.SourceSetDungeonMesh
					|| ChildGraph == Closure.SourceAddRamps)
				{
					OutError = FString::Printf(
						TEXT("Cooked runtime closure reaches forbidden vendor graph %s at %s:%s."),
						*ChildGraph->GetPathName(),
						*Graph->GetPathName(),
						*Node->GetName());
					return false;
				}
				if (ChildGraph == Closure.CookedSimple)
				{
					++CookedSimpleReferenceCount;
					continue;
				}
				InternalSetReferenceCount += ChildGraph == Closure.InternalSetDungeonMesh ? 1 : 0;
				InternalAddReferenceCount += ChildGraph == Closure.InternalAddRamps ? 1 : 0;
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
		if (TransientGraphCount != 1
			|| InternalSetReferenceCount != 1
			|| InternalAddReferenceCount != 3
			|| CookedSimpleReferenceCount != 7)
		{
			OutError = FString::Printf(
				TEXT("Cooked runtime closure cardinality drifted: transient=%d internalSet=%d internalAdd=%d cookedHelper=%d; expected 1/1/3/7."),
				TransientGraphCount,
				InternalSetReferenceCount,
				InternalAddReferenceCount,
				CookedSimpleReferenceCount);
			return false;
		}
		return true;
	}

#if WITH_EDITOR
	static bool PatchExactSubgraphReference(
		UPCGGraph* Graph,
		const FName NodeName,
		UPCGGraph* ExpectedCurrent,
		UPCGGraph* Replacement,
		FString& OutError)
	{
		if (!ValidateSubgraphReference(Graph, NodeName, ExpectedCurrent, OutError))
		{
			return false;
		}
		UPCGNode* Node = const_cast<UPCGNode*>(FindUniqueNode(Graph, NodeName, OutError));
		UPCGSubgraphSettings* Settings = Node
			? Cast<UPCGSubgraphSettings>(Node->GetSettings())
			: nullptr;
		if (!Node || !Settings || !IsValid(Replacement))
		{
			OutError = FString::Printf(
				TEXT("Cannot patch invalid internal subgraph reference %s:%s."),
				*GetPathNameSafe(Graph),
				*NodeName.ToString());
			return false;
		}

		Graph->Modify();
		Node->Modify();
		Node->GetSettingsInterface()->Modify();
		Settings->Modify();
		Settings->SubgraphInstance->Modify();
		Settings->SetSubgraph(Replacement);
		Node->UpdateAfterSettingsChangeDuringCreation();
		if (!ValidateSubgraphReference(Graph, NodeName, Replacement, OutError))
		{
			return false;
		}
		return true;
	}
#endif
}

bool FEFCalystoPCGCookedCompatibility::ValidateResidentInternalClosure(FString& OutError)
{
	using namespace EFCalystoPCGCookedCompatibilityPrivate;
	OutError.Reset();
	FResidentClosure Closure;
	return ResolveResidentClosure(Closure, OutError)
		&& ValidateInternalClosure(Closure, OutError);
}

#if WITH_EDITOR
bool FEFCalystoPCGCookedCompatibility::PrepareInternalClosureForEditor(
	UPCGGraph* InternalSetDungeonMesh,
	UPCGGraph* InternalAddRamps,
	FString& OutError)
{
	using namespace EFCalystoPCGCookedCompatibilityPrivate;
	OutError.Reset();
	FResidentClosure Closure;
	Closure.SourceSetDungeonMesh = ResolveResidentGraph(
		SourceSetDungeonMeshPath, TEXT("vendor SetDungeonMesh"), OutError);
	Closure.SourceAddRamps = ResolveResidentGraph(
		SourceAddRampsPath, TEXT("vendor AddRamps"), OutError);
	Closure.LegacySimple = ResolveResidentGraph(
		LegacySimplePath, TEXT("vendor legacy transform helper"), OutError);
	Closure.CookedSimple = ResolveResidentGraph(
		CookedSimplePath, TEXT("vendor cooked-safe transform helper"), OutError);
	Closure.InternalSetDungeonMesh = InternalSetDungeonMesh;
	Closure.InternalAddRamps = InternalAddRamps;
	if (!IsValid(Closure.SourceSetDungeonMesh)
		|| !IsValid(Closure.SourceAddRamps)
		|| !IsValid(Closure.LegacySimple)
		|| !IsValid(Closure.CookedSimple)
		|| !IsValid(Closure.InternalSetDungeonMesh)
		|| !IsValid(Closure.InternalAddRamps))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Internal closure preparation requires six valid resident graphs.");
		}
		return false;
	}
	if (Closure.InternalSetDungeonMesh->GetPathName() != InternalSetDungeonMeshPath
		|| Closure.InternalAddRamps->GetPathName() != InternalAddRampsPath)
	{
		OutError = TEXT("Internal closure preparation received an unexpected destination identity.");
		return false;
	}
	for (const UPCGGraph* ProtectedGraph : {
		Closure.SourceSetDungeonMesh,
		Closure.SourceAddRamps,
		Closure.LegacySimple,
		Closure.CookedSimple })
	{
		if (!ProtectedGraph || !ProtectedGraph->GetOutermost()
			|| ProtectedGraph->GetOutermost()->IsDirty())
		{
			OutError = FString::Printf(
				TEXT("Protected vendor graph is dirty before internal closure creation: %s."),
				*GetPathNameSafe(ProtectedGraph));
			return false;
		}
	}
	if (!ValidateSimpleGraphSelectors(
		Closure.LegacySimple, ExpectedLegacySelectors(), OutError)
		|| !ValidateSimpleGraphSelectors(
			Closure.CookedSimple, ExpectedCookedSelectors(), OutError)
		|| !ValidateEquivalentStructure(
			Closure.LegacySimple, Closure.CookedSimple, OutError)
		|| !ValidateFrozenVendorReferenceContract(Closure, OutError))
	{
		return false;
	}

	const TMap<FName, const UPCGGraph*> NoReplacements;
	if (!ValidateExactSubgraphReferences(
		Closure.SourceSetDungeonMesh,
		Closure.InternalSetDungeonMesh,
		NoReplacements,
		OutError)
		|| !ValidateExactSubgraphReferences(
			Closure.SourceAddRamps,
			Closure.InternalAddRamps,
			NoReplacements,
			OutError))
	{
		OutError = FString::Printf(
			TEXT("Fresh internal assets are not exact unpatched vendor duplicates: %s"),
			*OutError);
		return false;
	}

	if (!PatchExactSubgraphReference(
		Closure.InternalAddRamps,
		AddRampsLegacyNode,
		Closure.LegacySimple,
		Closure.CookedSimple,
		OutError))
	{
		return false;
	}
	for (const FName NodeName : SetDungeonMeshLegacyNodes())
	{
		if (!PatchExactSubgraphReference(
			Closure.InternalSetDungeonMesh,
			NodeName,
			Closure.LegacySimple,
			Closure.CookedSimple,
			OutError))
		{
			return false;
		}
	}
	for (const FName NodeName : SetDungeonMeshAddRampsNodes())
	{
		if (!PatchExactSubgraphReference(
			Closure.InternalSetDungeonMesh,
			NodeName,
			Closure.SourceAddRamps,
			Closure.InternalAddRamps,
			OutError))
		{
			return false;
		}
	}
	Closure.InternalAddRamps->ForceNotificationForEditor(EPCGChangeType::Structural);
	Closure.InternalSetDungeonMesh->ForceNotificationForEditor(EPCGChangeType::Structural);

	if (!ValidateInternalClosure(Closure, OutError))
	{
		return false;
	}
	for (const UPCGGraph* ProtectedGraph : {
		Closure.SourceSetDungeonMesh,
		Closure.SourceAddRamps,
		Closure.LegacySimple,
		Closure.CookedSimple })
	{
		if (ProtectedGraph->GetOutermost()->IsDirty())
		{
			OutError = FString::Printf(
				TEXT("Internal closure creation dirtied protected vendor package %s."),
				*ProtectedGraph->GetOutermost()->GetName());
			return false;
		}
	}
	return true;
}
#endif

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

	FResidentClosure Closure;
	FString Error;
	if (!ResolveResidentClosure(Closure, Error)
		|| !ValidateInternalClosure(Closure, Error))
	{
		return Fail(MoveTemp(Error));
	}
	if (!ValidateSubgraphReference(
		SourceRootGraph,
		MasterSetDungeonMeshNode,
		Closure.SourceSetDungeonMesh,
		Error)
		|| CountDirectSubgraphReferences(
			SourceRootGraph, Closure.SourceSetDungeonMesh) != 1)
	{
		return Fail(Error.IsEmpty()
			? TEXT("Master must contain exactly one direct SetDungeonMesh call at Subgraph_43.")
			: MoveTemp(Error));
	}

	const FName CloneName = MakeUniqueObjectName(
		TransientOuter,
		SourceRootGraph->GetClass(),
		TEXT("EFCalystoCooked_PCG_MassiveDungeonMaster"));
	UPCGGraph* RuntimeGraph = DuplicateObject<UPCGGraph>(
		SourceRootGraph,
		TransientOuter,
		CloneName);
	if (!IsValid(RuntimeGraph))
	{
		return Fail(TEXT("Failed to duplicate the exact Calysto Master graph transiently."));
	}
	RuntimeGraph->ClearFlags(RF_Public | RF_Standalone);
	RuntimeGraph->SetFlags(RF_Transient);
	if (!InvalidateDuplicatedCookedCompilationData(RuntimeGraph, Error))
	{
		return Fail(MoveTemp(Error));
	}

	UPCGNode* MasterNode = const_cast<UPCGNode*>(FindUniqueNode(
		RuntimeGraph, MasterSetDungeonMeshNode, Error));
	UPCGSubgraphSettings* MasterSettings = MasterNode
		? Cast<UPCGSubgraphSettings>(MasterNode->GetSettings())
		: nullptr;
	if (!MasterSettings
		|| !ValidateSubgraphReference(
			RuntimeGraph,
			MasterSetDungeonMeshNode,
			Closure.SourceSetDungeonMesh,
			Error))
	{
		return Fail(MoveTemp(Error));
	}
	MasterSettings->SetSubgraph(Closure.InternalSetDungeonMesh);
	MasterNode->UpdateAfterSettingsChangeDuringCreation();
	if (!ValidateSubgraphReference(
		RuntimeGraph,
		MasterSetDungeonMeshNode,
		Closure.InternalSetDungeonMesh,
		Error)
		|| CountDirectSubgraphReferences(
			RuntimeGraph, Closure.InternalSetDungeonMesh) != 1
		|| CountDirectSubgraphReferences(
			RuntimeGraph, Closure.SourceSetDungeonMesh) != 0
		|| !ValidateSubgraphReference(
			SourceRootGraph,
			MasterSetDungeonMeshNode,
			Closure.SourceSetDungeonMesh,
			Error)
		|| !ValidateCookedSafeRuntimeClosure(RuntimeGraph, Closure, Error))
	{
		return Fail(MoveTemp(Error));
	}

	Result.RuntimeGraph = RuntimeGraph;
	Result.ClonedGraphCount = 1;
	Result.RelinkedInternalClosureCount = 1;
	Result.ValidatedInternalGraphCount = 2;
	Result.InvalidatedCookedCompilationDataCount = 1;
	Result.bApplied = true;
	UE_LOG(
		LogEFCalystoPCGCookedCompatibility,
		Log,
		TEXT("PASS source=%s runtime=%s transientClones=1 internalGraphs=2 masterRelinks=1 invalidatedCookedTaskCaches=1 cookedSafeHelperRefs=7 vendorAssetsMutated=0."),
		*SourceRootGraph->GetPathName(),
		*RuntimeGraph->GetPathName());
	return Result;
}
