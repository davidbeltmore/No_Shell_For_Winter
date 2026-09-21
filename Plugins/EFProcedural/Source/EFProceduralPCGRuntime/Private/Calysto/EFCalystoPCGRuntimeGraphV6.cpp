#include "Calysto/EFCalystoPCGRuntimeGraphV6.h"

#include "Calysto/EFCalystoPCGCookedCompatibility.h"
#include "Elements/PCGCreateAttribute.h"
#include "Elements/PCGAttributeFilter.h"
#include "Elements/Metadata/PCGMetadataMathsOpElement.h"
#include "Elements/PCGReroute.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Hash/Blake3.h"
#include "MeshSelectors/PCGMeshSelectorByAttribute.h"
#include "PCGComponent.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGInputOutputSettings.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSubgraph.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoPCGRuntimeGraphV6, Log, All);

const FName FEFCalystoPlacementCandidatePinsV6::Floor(TEXT("EF Placement Floor"));
const FName FEFCalystoPlacementCandidatePinsV6::WallBottom(TEXT("EF Placement Wall Bottom"));
const FName FEFCalystoPlacementCandidatePinsV6::WallMiddle(TEXT("EF Placement Wall Middle"));
const FName FEFCalystoPlacementCandidatePinsV6::WallTop(TEXT("EF Placement Wall Top"));
const FName FEFCalystoPlacementCandidatePinsV6::CornerBottom(TEXT("EF Placement Corner Bottom"));
const FName FEFCalystoPlacementCandidatePinsV6::CornerMiddle(TEXT("EF Placement Corner Middle"));
const FName FEFCalystoPlacementCandidatePinsV6::CornerTop(TEXT("EF Placement Corner Top"));
const FName FEFCalystoPlacementCandidatePinsV6::Roof(TEXT("EF Placement Roof"));

namespace EFCalystoPCGRuntimeGraphV6Private
{
	static constexpr TCHAR SourceRootPath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.PCG_MassiveDungeonMaster");
	static constexpr TCHAR ShapeGraphPath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonShape.PCG_MassiveDungeonShape");
	static constexpr TCHAR SetDungeonMeshGraphPath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh");
	static constexpr TCHAR InternalSetDungeonMeshGraphPath[] =
		TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_SetDungeonMeshCookedSafe.PCG_SetDungeonMeshCookedSafe");
	static constexpr TCHAR GenerateRoomPointsGraphPath[] =
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_GenerateRoomPoints.PCG_GenerateRoomPoints");
	static const FName NativeThemeNodeName(TEXT("MatchAndSetAttributes_26"));
	static const FName NativeThemeMergeName(TEXT("MergePoints_48"));
	static const FName NativePlacementNodeName(TEXT("Subgraph_3"));
	static const FName NativePlacementWallLightPin(TEXT("Wall Light"));
	static const FName RejectedPlacementWallLightOutput(TEXT("EF Placement Wall Light"));
	static const FName AllUsedRoomsDeclaration(TEXT("All Used Rooms"));
	static const FName MainRoomsDeclaration(TEXT("Main Rooms"));
	static const FName SideRoomsDeclaration(TEXT("Side Rooms"));
	static const FName StartRoomsDeclaration(TEXT("Start Main"));
	static const FName EndRoomsDeclaration(TEXT("End Main"));
	static const FName AllUsedRoomsNodeName(TEXT("NamedRerouteDeclaration_34"));
	static const FName MainRoomsNodeName(TEXT("NamedRerouteDeclaration_2"));
	static const FName SideRoomsNodeName(TEXT("NamedRerouteDeclaration_32"));
	static const FName StartRoomsNodeName(TEXT("NamedRerouteDeclaration_38"));
	static const FName EndRoomsNodeName(TEXT("NamedRerouteDeclaration_39"));
	static constexpr TCHAR EmptyRoomSchemaPath[] =
		TEXT("/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes.Default__PDA_RoomMeshes_C");

	struct FPlacementRoute
	{
		FName NativeOutput;
		FName RootOutput;
	};

	static const TArray<FPlacementRoute>& PlacementRoutes()
	{
		static const TArray<FPlacementRoute> Routes =
		{
			{ TEXT("Floor"), FEFCalystoPlacementCandidatePinsV6::Floor },
			{ TEXT("Wall Bottom"), FEFCalystoPlacementCandidatePinsV6::WallBottom },
			{ TEXT("Wall Middle"), FEFCalystoPlacementCandidatePinsV6::WallMiddle },
			{ TEXT("Wall Top"), FEFCalystoPlacementCandidatePinsV6::WallTop },
			{ TEXT("Corner Bottom"), FEFCalystoPlacementCandidatePinsV6::CornerBottom },
			{ TEXT("Corner Middle"), FEFCalystoPlacementCandidatePinsV6::CornerMiddle },
			{ TEXT("Corner Top"), FEFCalystoPlacementCandidatePinsV6::CornerTop },
			{ TEXT("Roof"), FEFCalystoPlacementCandidatePinsV6::Roof }
		};
		return Routes;
	}

	static const TArray<FName>& ExpectedNativePlacementOutputs()
	{
		static const TArray<FName> Outputs =
		{
			TEXT("Wall Bottom"),
			TEXT("Wall Middle"),
			TEXT("Wall Top"),
			TEXT("Floor"),
			TEXT("Corner Bottom"),
			TEXT("Corner Middle"),
			TEXT("Corner Top"),
			TEXT("Roof"),
			NativePlacementWallLightPin
		};
		return Outputs;
	}

	static FString HashCanonicalUtf8(const FString& Canonical)
	{
		FTCHARToUTF8 Utf8(*Canonical);
		return LexToString(FBlake3::HashBuffer(Utf8.Get(), static_cast<uint64>(Utf8.Length()))).ToUpper();
	}

	static FString BuildPlacementRoutingFingerprint()
	{
		FString Canonical = FString::Printf(
			TEXT("Calysto.PlacementRouting.V6|Root=%s|Producer=%s|Graph=%s|Type=Point|NativeEdges=1"),
			SourceRootPath,
			*NativePlacementNodeName.ToString(),
			GenerateRoomPointsGraphPath);
		for (const FPlacementRoute& Route : PlacementRoutes())
		{
			Canonical += FString::Printf(
				TEXT("|%s>%s"),
				*Route.NativeOutput.ToString(),
				*Route.RootOutput.ToString());
		}
		Canonical += FString::Printf(
			TEXT("|%s>StyleArchitectureOnly"),
			*NativePlacementWallLightPin.ToString());
		return HashCanonicalUtf8(Canonical);
	}

	static FString BuildRuntimeGraphConfigurationFingerprint(
		const FEFCalystoRoomThemeGenerationConfigV6& Config,
		const FString& PlacementFingerprint)
	{
		const FString ThemeFingerprint =
			FEFCalystoRoomThemeDeterminismV6::BuildConfigFingerprint(Config);
		return HashCanonicalUtf8(FString::Printf(
			TEXT("Calysto.RuntimeGraph.Config.V6|NativeRoomStream=CompleteWithNeutralSchema1|EndpointPool=MinimumOne1|Theme=%s|Placement=%s"),
			*ThemeFingerprint,
			*PlacementFingerprint));
	}

	static const TMap<FName, FName>& ExpectedSurfaceMaterialConsumers()
	{
		// The vendor graph owns three independently transformed consumers for each
		// surface. Keep every one explicit: the historical defect connected the
		// roof group to FloorMaterial and was visually masked when all slots shared
		// one material.
		static const TMap<FName, FName> Consumers =
		{
			{ TEXT("StaticMeshSpawner_1"), TEXT("FloorMaterial") },
			{ TEXT("StaticMeshSpawner_2"), TEXT("FloorMaterial") },
			{ TEXT("StaticMeshSpawner_3"), TEXT("FloorMaterial") },
			{ TEXT("StaticMeshSpawner_0"), TEXT("WallMaterial") },
			{ TEXT("StaticMeshSpawner_4"), TEXT("WallMaterial") },
			{ TEXT("StaticMeshSpawner_5"), TEXT("WallMaterial") },
			{ TEXT("StaticMeshSpawner_9"), TEXT("RoofMaterial") },
			{ TEXT("StaticMeshSpawner_48"), TEXT("RoofMaterial") },
			{ TEXT("StaticMeshSpawner_58"), TEXT("RoofMaterial") }
		};
		return Consumers;
	}

	static bool ReadAttributeMaterialOverrideContract(
		const UPCGMeshSelectorBase* Selector,
		bool& bOutEnabled,
		TArray<FName>& OutAttributes,
		FString& OutError)
	{
		bOutEnabled = false;
		OutAttributes.Reset();
		if (!IsValid(Selector))
		{
			return true;
		}

		const FBoolProperty* EnabledProperty = FindFProperty<FBoolProperty>(
			Selector->GetClass(),
			TEXT("bUseAttributeMaterialOverrides"));
		if (!EnabledProperty)
		{
			return true;
		}
		bOutEnabled = EnabledProperty->GetPropertyValue_InContainer(Selector);
		if (!bOutEnabled)
		{
			return true;
		}

		const FArrayProperty* AttributesProperty = FindFProperty<FArrayProperty>(
			Selector->GetClass(),
			TEXT("MaterialOverrideAttributes"));
		const FNameProperty* AttributeNameProperty = AttributesProperty
			? CastField<FNameProperty>(AttributesProperty->Inner)
			: nullptr;
		if (!AttributesProperty || !AttributeNameProperty)
		{
			OutError = FString::Printf(
				TEXT("Calysto surface selector %s enables attribute material overrides without a TArray<FName> contract."),
				*Selector->GetClass()->GetPathName());
			return false;
		}

		FScriptArrayHelper_InContainer Attributes(AttributesProperty, Selector);
		OutAttributes.Reserve(Attributes.Num());
		for (int32 Index = 0; Index < Attributes.Num(); ++Index)
		{
			OutAttributes.Add(AttributeNameProperty->GetPropertyValue(Attributes.GetRawPtr(Index)));
		}
		return true;
	}

	static bool IsSetDungeonMeshGraph(const UPCGGraph* Graph)
	{
		if (!IsValid(Graph))
		{
			return false;
		}
		if (Graph->GetPathName() == SetDungeonMeshGraphPath
			|| Graph->GetPathName() == InternalSetDungeonMeshGraphPath)
		{
			return true;
		}
		return Graph->HasAnyFlags(RF_Transient)
			&& Graph->GetName().Contains(TEXT("PCG_SetDungeonMesh"), ESearchCase::CaseSensitive);
	}

	static bool ValidateSurfaceMaterialConsumerGraph(
		const UPCGGraph* Graph,
		FString& OutError)
	{
		if (!IsValid(Graph))
		{
			OutError = TEXT("Cannot validate surface material consumers on an invalid PCG graph.");
			return false;
		}

		const TMap<FName, FName>& ExpectedConsumers = ExpectedSurfaceMaterialConsumers();
		TSet<FName> SeenConsumers;
		TMap<FName, int32> SurfaceCounts;

		for (const UPCGNode* Node : Graph->GetNodes())
		{
			const UPCGStaticMeshSpawnerSettings* Spawner = Node
				? Cast<UPCGStaticMeshSpawnerSettings>(Node->GetSettings())
				: nullptr;
			if (!Spawner)
			{
				continue;
			}

			bool bUsesAttributeMaterialOverrides = false;
			TArray<FName> MaterialAttributes;
			if (!ReadAttributeMaterialOverrideContract(
				Spawner->MeshSelectorParameters,
				bUsesAttributeMaterialOverrides,
				MaterialAttributes,
				OutError))
			{
				return false;
			}

			const FName* ExpectedAttribute = ExpectedConsumers.Find(Node->GetFName());
			if (!ExpectedAttribute)
			{
				if (bUsesAttributeMaterialOverrides)
				{
					OutError = FString::Printf(
						TEXT("Calysto SetDungeonMesh exposes unexpected attribute-material consumer %s."),
						*Node->GetName());
					return false;
				}
				continue;
			}

			if (SeenConsumers.Contains(Node->GetFName()))
			{
				OutError = FString::Printf(
					TEXT("Calysto SetDungeonMesh duplicates required surface consumer %s."),
					*Node->GetName());
				return false;
			}
			SeenConsumers.Add(Node->GetFName());

			if (Spawner->MeshSelectorType != UPCGMeshSelectorByAttribute::StaticClass()
				|| !Cast<UPCGMeshSelectorByAttribute>(Spawner->MeshSelectorParameters)
				|| !bUsesAttributeMaterialOverrides
				|| MaterialAttributes.Num() != 1
				|| MaterialAttributes[0] != *ExpectedAttribute)
			{
				const FString ActualAttributes = MaterialAttributes.IsEmpty()
					? TEXT("<none>")
					: FString::JoinBy(MaterialAttributes, TEXT(","), [](const FName Value)
					{
						return Value.ToString();
					});
				OutError = FString::Printf(
					TEXT("Calysto surface consumer %s expected '%s' but receives '%s' (selector=%s enabled=%s)."),
					*Node->GetName(),
					*ExpectedAttribute->ToString(),
					*ActualAttributes,
					*GetPathNameSafe(Spawner->MeshSelectorParameters),
					bUsesAttributeMaterialOverrides ? TEXT("true") : TEXT("false"));
				return false;
			}
			SurfaceCounts.FindOrAdd(*ExpectedAttribute)++;
		}

		if (SeenConsumers.Num() != ExpectedConsumers.Num()
			|| SurfaceCounts.FindRef(TEXT("FloorMaterial")) != 3
			|| SurfaceCounts.FindRef(TEXT("WallMaterial")) != 3
			|| SurfaceCounts.FindRef(TEXT("RoofMaterial")) != 3)
		{
			OutError = FString::Printf(
				TEXT("Calysto SetDungeonMesh surface contract is incomplete: consumers=%d/%d floor=%d wall=%d roof=%d; each surface requires exactly three."),
				SeenConsumers.Num(),
				ExpectedConsumers.Num(),
				SurfaceCounts.FindRef(TEXT("FloorMaterial")),
				SurfaceCounts.FindRef(TEXT("WallMaterial")),
				SurfaceCounts.FindRef(TEXT("RoofMaterial")));
			return false;
		}
		return true;
	}

	static bool ValidateSurfaceMaterialConsumersRecursive(
		const UPCGGraph* RuntimeRoot,
		FString& OutError)
	{
		TSet<const UPCGGraph*> Visited;
		int32 MatchingGraphCount = 0;
		TFunction<bool(const UPCGGraph*)> Visit = [&](const UPCGGraph* Graph) -> bool
		{
			if (!IsValid(Graph))
			{
				OutError = TEXT("Calysto runtime closure contains an invalid graph while validating surface consumers.");
				return false;
			}
			if (Visited.Contains(Graph))
			{
				return true;
			}
			Visited.Add(Graph);

			if (IsSetDungeonMeshGraph(Graph))
			{
				++MatchingGraphCount;
				if (!ValidateSurfaceMaterialConsumerGraph(Graph, OutError))
				{
					return false;
				}
			}

			for (const UPCGNode* Node : Graph->GetNodes())
			{
				const UPCGSubgraphSettings* Subgraph = Node
					? Cast<UPCGSubgraphSettings>(Node->GetSettings())
					: nullptr;
				const UPCGGraph* Child = Subgraph ? Subgraph->GetSubgraph() : nullptr;
				if (Child && !Visit(Child))
				{
					return false;
				}
			}
			return true;
		};

		if (!Visit(RuntimeRoot))
		{
			return false;
		}
		if (MatchingGraphCount != 1)
		{
			OutError = FString::Printf(
				TEXT("Calysto runtime closure contains %d SetDungeonMesh graphs; exactly one validated surface authority is required."),
				MatchingGraphCount);
			return false;
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

	static bool InvalidateDuplicatedCookedCompilationData(UPCGGraph* Graph, FString& OutError)
	{
		if (!IsValid(Graph) || !Graph->HasAnyFlags(RF_Transient))
		{
			OutError = TEXT("Refusing to invalidate cooked tasks on a non-transient PCG graph.");
			return false;
		}
		FObjectProperty* CookedDataProperty = FindFProperty<FObjectProperty>(UPCGGraph::StaticClass(), TEXT("CookedCompilationData"));
		if (!CookedDataProperty
			|| !CookedDataProperty->PropertyClass
			|| CookedDataProperty->PropertyClass->GetFName() != TEXT("PCGGraphCompilationData"))
		{
			OutError = TEXT("UE 5.8 UPCGGraph::CookedCompilationData reflection contract drifted.");
			return false;
		}
		CookedDataProperty->SetObjectPropertyValue_InContainer(Graph, nullptr);
		return CookedDataProperty->GetObjectPropertyValue_InContainer(Graph) == nullptr;
	}

	static UPCGPin* FindUniqueDataPinByLabel(
		UPCGNode* Node,
		const bool bInput,
		const FName ExpectedLabel,
		FString& OutError)
	{
		if (!IsValid(Node))
		{
			OutError = TEXT("Cannot inspect data pins on an invalid PCG node.");
			return nullptr;
		}

		UPCGPin* Result = nullptr;
		const TArray<TObjectPtr<UPCGPin>>& Pins = bInput ? Node->GetInputPins() : Node->GetOutputPins();
		for (UPCGPin* Pin : Pins)
		{
			if (!IsValid(Pin)
				|| Pin->Properties.Usage == EPCGPinUsage::DependencyOnly
				|| Pin->Properties.Label != ExpectedLabel)
			{
				continue;
			}
			if (Result)
			{
				OutError = FString::Printf(
					TEXT("Node %s exposes more than one %s data pin labelled '%s'."),
					*GetPathNameSafe(Node),
					bInput ? TEXT("input") : TEXT("output"),
					*ExpectedLabel.ToString());
				return nullptr;
			}
			Result = Pin;
		}
		if (!Result)
		{
			OutError = FString::Printf(
				TEXT("Node %s exposes no %s data pin labelled '%s'."),
				*GetPathNameSafe(Node),
				bInput ? TEXT("input") : TEXT("output"),
				*ExpectedLabel.ToString());
		}
		return Result;
	}

	static UPCGPin* FindUniqueInputConnectedFromNode(
		UPCGNode* DownstreamNode,
		UPCGNode* UpstreamNode,
		UPCGPin*& OutUpstreamPin,
		FString& OutError)
	{
		UPCGPin* Result = nullptr;
		OutUpstreamPin = nullptr;
		int32 MatchingEdgeCount = 0;
		for (UPCGPin* InputPin : DownstreamNode
			? DownstreamNode->GetInputPins()
			: TArray<TObjectPtr<UPCGPin>>())
		{
			if (!IsValid(InputPin) ||
				InputPin->Properties.Usage == EPCGPinUsage::DependencyOnly)
			{
				continue;
			}
			for (UPCGEdge* Edge : InputPin->Edges)
			{
				if (!IsValid(Edge) || !Edge->IsValid() ||
					Edge->OutputPin != InputPin ||
					!IsValid(Edge->InputPin) ||
					Edge->InputPin->Node != UpstreamNode)
				{
					continue;
				}
				++MatchingEdgeCount;
				Result = InputPin;
				OutUpstreamPin = Edge->InputPin;
			}
		}
		if (MatchingEdgeCount != 1 || !Result || !OutUpstreamPin)
		{
			OutError = FString::Printf(
				TEXT("Node %s has %d data edges from required upstream node %s; exactly one is required."),
				*GetPathNameSafe(DownstreamNode),
				MatchingEdgeCount,
				*GetPathNameSafe(UpstreamNode));
			return nullptr;
		}
		return Result;
	}

	static UPCGNode* FindUniqueNodeByObjectName(UPCGGraph* Graph, const FName NodeName, FString& OutError)
	{
		if (!IsValid(Graph))
		{
			OutError = TEXT("Cannot find a required node in an invalid PCG graph.");
			return nullptr;
		}

		UPCGNode* Result = nullptr;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (IsValid(Node) && Node->GetFName() == NodeName)
			{
				if (Result)
				{
					OutError = FString::Printf(TEXT("Graph %s contains duplicate node name %s."), *Graph->GetPathName(), *NodeName.ToString());
					return nullptr;
				}
				Result = Node;
			}
		}
		if (!Result)
		{
			OutError = FString::Printf(TEXT("Graph %s no longer contains required node %s."), *GetPathNameSafe(Graph), *NodeName.ToString());
		}
		return Result;
	}

	static const UPCGNode* FindUniqueNodeByObjectName(
		const UPCGGraph* Graph,
		const FName NodeName,
		FString& OutError)
	{
		if (!IsValid(Graph))
		{
			OutError = TEXT("Cannot find a required node in an invalid PCG graph.");
			return nullptr;
		}

		const UPCGNode* Result = nullptr;
		for (const UPCGNode* Node : Graph->GetNodes())
		{
			if (IsValid(Node) && Node->GetFName() == NodeName)
			{
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

	static int32 CountDirectEdges(const UPCGPin* UpstreamPin, const UPCGPin* DownstreamPin)
	{
		int32 Count = 0;
		for (const UPCGEdge* Edge : IsValid(UpstreamPin)
			? UpstreamPin->Edges
			: TArray<TObjectPtr<UPCGEdge>>())
		{
			if (IsValid(Edge)
				&& Edge->IsValid()
				&& Edge->InputPin == UpstreamPin
				&& Edge->OutputPin == DownstreamPin)
			{
				++Count;
			}
		}
		return Count;
	}

	static bool CaptureEdgeMultiset(
		const UPCGGraph* Graph,
		TMap<FString, int32>& OutEdges,
		FString& OutError)
	{
		OutEdges.Reset();
		if (!IsValid(Graph))
		{
			OutError = TEXT("Cannot capture edges from an invalid PCG graph.");
			return false;
		}
		for (const UPCGEdge* Edge : Graph->GetAllEdges())
		{
			if (!IsValid(Edge)
				|| !Edge->IsValid()
				|| !IsValid(Edge->InputPin)
				|| !IsValid(Edge->OutputPin)
				|| !IsValid(Edge->InputPin->Node)
				|| !IsValid(Edge->OutputPin->Node))
			{
				OutError = FString::Printf(
					TEXT("Graph %s contains an invalid edge before placement routing."),
					*Graph->GetPathName());
				return false;
			}
			const FString EdgeKey = FString::Printf(
				TEXT("%s:%s>%s:%s"),
				*Edge->InputPin->Node->GetPathName(),
				*Edge->InputPin->Properties.Label.ToString(),
				*Edge->OutputPin->Node->GetPathName(),
				*Edge->OutputPin->Properties.Label.ToString());
			OutEdges.FindOrAdd(EdgeKey)++;
		}
		return true;
	}

	static bool ValidateNativePlacementRouting(
		const UPCGGraph* RuntimeRoot,
		const bool bExpectRootRouting,
		const UPCGNode*& OutProducer,
		FString& OutError)
	{
		OutProducer = FindUniqueNodeByObjectName(RuntimeRoot, NativePlacementNodeName, OutError);
		const UPCGSubgraphSettings* Settings = OutProducer
			? Cast<UPCGSubgraphSettings>(OutProducer->GetSettings())
			: nullptr;
		const UPCGGraph* NativeGraph = Settings ? Settings->GetSubgraph() : nullptr;
		if (!OutProducer
			|| !Settings
			|| !OutProducer->IsIn(RuntimeRoot)
			|| !Settings->IsIn(RuntimeRoot)
			|| !IsValid(Settings->SubgraphInstance)
			|| !Settings->SubgraphInstance->IsIn(RuntimeRoot)
			|| IsValid(Settings->SubgraphOverride)
			|| HasConnectedSubgraphOverridePin(Settings, OutProducer)
			|| !IsValid(NativeGraph)
			|| NativeGraph->GetPathName() != GenerateRoomPointsGraphPath)
		{
			OutError = FString::Printf(
				TEXT("Calysto placement producer %s no longer owns the exact unoverridden %s subgraph contract."),
				*GetPathNameSafe(OutProducer),
				GenerateRoomPointsGraphPath);
			return false;
		}

		TMap<FName, int32> OutputLabelCounts;
		int32 DataOutputCount = 0;
		for (const UPCGPin* Pin : OutProducer->GetOutputPins())
		{
			if (!IsValid(Pin) || Pin->Properties.Usage == EPCGPinUsage::DependencyOnly)
			{
				continue;
			}
			++DataOutputCount;
			OutputLabelCounts.FindOrAdd(Pin->Properties.Label)++;
		}
		if (DataOutputCount != ExpectedNativePlacementOutputs().Num()
			|| OutputLabelCounts.Num() != ExpectedNativePlacementOutputs().Num())
		{
			OutError = FString::Printf(
				TEXT("Calysto placement producer %s exposes %d/%d unique data outputs; the vendor signature drifted."),
				*OutProducer->GetName(),
				DataOutputCount,
				ExpectedNativePlacementOutputs().Num());
			return false;
		}

		const UPCGNode* OutputNode = RuntimeRoot->GetOutputNode();
		if (!IsValid(OutputNode)
			|| !Cast<UPCGGraphInputOutputSettings>(OutputNode->GetSettings())
			|| OutputNode->GetInputPin(RejectedPlacementWallLightOutput))
		{
			OutError = TEXT("Calysto runtime root has an invalid output boundary or exposes the reserved Wall Light stream.");
			return false;
		}

		for (const FName ExpectedOutput : ExpectedNativePlacementOutputs())
		{
			const UPCGPin* SourcePin = OutProducer->GetOutputPin(ExpectedOutput);
			const int32* LabelCount = OutputLabelCounts.Find(ExpectedOutput);
			const bool bWallLight = ExpectedOutput == NativePlacementWallLightPin;
			const FPlacementRoute* Route = PlacementRoutes().FindByPredicate(
				[ExpectedOutput](const FPlacementRoute& Candidate)
				{
					return Candidate.NativeOutput == ExpectedOutput;
				});
			if (!IsValid(SourcePin)
				|| !LabelCount
				|| *LabelCount != 1
				|| SourcePin->Properties.Usage != EPCGPinUsage::Normal
				|| !(SourcePin->Properties.AllowedTypes == EPCGDataType::Point)
				|| !SourcePin->Properties.AllowsMultipleConnections()
				|| bWallLight == (Route != nullptr))
			{
				OutError = FString::Printf(
					TEXT("Calysto native placement output '%s' lost its unique normal Point/multi-connection contract."),
					*ExpectedOutput.ToString());
				return false;
			}

			const int32 ExpectedEdgeCount = bExpectRootRouting && !bWallLight ? 2 : 1;
			if (SourcePin->Edges.Num() != ExpectedEdgeCount)
			{
				OutError = FString::Printf(
					TEXT("Calysto native placement output '%s' has %d edges; expected exactly %d so native consumers remain intact."),
					*ExpectedOutput.ToString(),
					SourcePin->Edges.Num(),
					ExpectedEdgeCount);
				return false;
			}
			for (const UPCGEdge* Edge : SourcePin->Edges)
			{
				if (!IsValid(Edge) || !Edge->IsValid() || Edge->InputPin != SourcePin)
				{
					OutError = FString::Printf(
						TEXT("Calysto native placement output '%s' owns an invalid or reversed edge."),
						*ExpectedOutput.ToString());
					return false;
				}
			}

			if (bWallLight)
			{
				continue;
			}
			const UPCGPin* RootPin = OutputNode->GetInputPin(Route->RootOutput);
			if (bExpectRootRouting)
			{
				if (!IsValid(RootPin)
					|| RootPin->Properties.Usage != EPCGPinUsage::Normal
					|| !(RootPin->Properties.AllowedTypes == EPCGDataType::Point)
					|| RootPin->Edges.Num() != 1
					|| CountDirectEdges(SourcePin, RootPin) != 1)
				{
					OutError = FString::Printf(
						TEXT("Calysto root output '%s' is missing its unique direct route from native '%s'."),
						*Route->RootOutput.ToString(),
						*ExpectedOutput.ToString());
					return false;
				}
			}
			else if (RootPin)
			{
				OutError = FString::Printf(
					TEXT("Calysto source root already exposes reserved V6 placement output '%s'."),
					*Route->RootOutput.ToString());
				return false;
			}
		}
		return true;
	}

	static bool RouteNativePlacementCandidates(
		UPCGGraph* RuntimeRoot,
		int32& OutRoutedOutputCount,
		FString& OutError)
	{
		OutRoutedOutputCount = 0;
		const UPCGNode* ValidatedProducer = nullptr;
		if (!ValidateNativePlacementRouting(
			RuntimeRoot,
			false,
			ValidatedProducer,
			OutError))
		{
			return false;
		}

		TMap<FString, int32> EdgesBefore;
		if (!CaptureEdgeMultiset(RuntimeRoot, EdgesBefore, OutError))
		{
			return false;
		}
		UPCGNode* Producer = const_cast<UPCGNode*>(ValidatedProducer);
		UPCGNode* OutputNode = RuntimeRoot->GetOutputNode();
		UPCGGraphInputOutputSettings* OutputSettings = OutputNode
			? Cast<UPCGGraphInputOutputSettings>(OutputNode->GetSettings())
			: nullptr;
		if (!OutputSettings)
		{
			OutError = TEXT("Calysto runtime root cannot expose placement candidates without a graph output settings object.");
			return false;
		}

		for (const FPlacementRoute& Route : PlacementRoutes())
		{
			const FPCGPinProperties& AddedPin = OutputSettings->AddPin(
				FPCGPinProperties(Route.RootOutput, EPCGDataType::Point));
			if (AddedPin.Label != Route.RootOutput)
			{
				OutError = FString::Printf(
					TEXT("Calysto runtime root renamed reserved placement output '%s'."),
					*Route.RootOutput.ToString());
				return false;
			}
		}
		OutputNode->UpdateAfterSettingsChangeDuringCreation();

		for (const FPlacementRoute& Route : PlacementRoutes())
		{
			if (!OutputNode->GetInputPin(Route.RootOutput))
			{
				OutError = FString::Printf(
					TEXT("Calysto runtime root did not materialize placement output '%s'."),
					*Route.RootOutput.ToString());
				return false;
			}
			RuntimeRoot->AddEdge(Producer, Route.NativeOutput, OutputNode, Route.RootOutput);
		}

		const UPCGNode* RoutedProducer = nullptr;
		if (!ValidateNativePlacementRouting(
			RuntimeRoot,
			true,
			RoutedProducer,
			OutError))
		{
			return false;
		}

		TMap<FString, int32> EdgesAfter;
		if (!CaptureEdgeMultiset(RuntimeRoot, EdgesAfter, OutError))
		{
			return false;
		}
		int32 EdgeCountBefore = 0;
		int32 EdgeCountAfter = 0;
		for (const TPair<FString, int32>& Pair : EdgesBefore)
		{
			EdgeCountBefore += Pair.Value;
			if (EdgesAfter.FindRef(Pair.Key) < Pair.Value)
			{
				OutError = FString::Printf(
					TEXT("Calysto placement routing removed or replaced an existing native edge: %s."),
					*Pair.Key);
				return false;
			}
		}
		for (const TPair<FString, int32>& Pair : EdgesAfter)
		{
			EdgeCountAfter += Pair.Value;
		}
		if (EdgeCountAfter != EdgeCountBefore + PlacementRoutes().Num())
		{
			OutError = FString::Printf(
				TEXT("Calysto placement routing changed the Master edge count by %d; expected exactly %d additive routes."),
				EdgeCountAfter - EdgeCountBefore,
				PlacementRoutes().Num());
			return false;
		}

		OutRoutedOutputCount = PlacementRoutes().Num();
		return true;
	}

	static UPCGNode* FindUniqueNamedDeclaration(UPCGGraph* Graph, const FName AuthoredTitle, FString& OutError)
	{
		if (!IsValid(Graph))
		{
			OutError = TEXT("Cannot find a named declaration in an invalid PCG graph.");
			return nullptr;
		}

		UPCGNode* Result = nullptr;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (!IsValid(Node)
				|| !Cast<UPCGNamedRerouteDeclarationSettings>(Node->GetSettings())
				|| Node->GetAuthoredTitleName() != AuthoredTitle)
			{
				continue;
			}
			if (Result)
			{
				OutError = FString::Printf(TEXT("Graph %s contains multiple named declarations titled '%s'."), *Graph->GetPathName(), *AuthoredTitle.ToString());
				return nullptr;
			}
			Result = Node;
		}
		if (!Result)
		{
			OutError = FString::Printf(TEXT("Graph %s has no named declaration titled '%s'."), *GetPathNameSafe(Graph), *AuthoredTitle.ToString());
		}
		return Result;
	}

	static UPCGPin* ValidateNamedRoomDeclaration(
		UPCGNode* Node,
		const FName ExpectedNodeName,
		const FName ExpectedTitle,
		FString& OutError)
	{
		if (!IsValid(Node)
			|| Node->GetFName() != ExpectedNodeName
			|| Node->GetAuthoredTitleName() != ExpectedTitle
			|| !Cast<UPCGNamedRerouteDeclarationSettings>(Node->GetSettings()))
		{
			OutError = FString::Printf(
				TEXT("Calysto vendor declaration '%s' no longer matches required node %s."),
				*ExpectedTitle.ToString(),
				*ExpectedNodeName.ToString());
			return nullptr;
		}
		UPCGPin* Input = FindUniqueDataPinByLabel(Node, true, TEXT("In"), OutError);
		UPCGPin* Output = FindUniqueDataPinByLabel(Node, false, TEXT("Out"), OutError);
		if (!Input || !Output)
		{
			return nullptr;
		}
		if (!Input->IsConnected())
		{
			OutError = FString::Printf(
				TEXT("Calysto vendor declaration '%s' no longer exposes its connected In -> Out data contract."),
				*ExpectedTitle.ToString());
			return nullptr;
		}
		return Output;
	}

	static bool AddContextGraphOutput(UPCGGraph* Graph, UPCGNode* Producer, FString& OutError)
	{
		if (!IsValid(Graph) || !IsValid(Producer)
			|| !Producer->GetOutputPin(FEFCalystoRoomThemePinsV6::RoomContexts))
		{
			OutError = TEXT("Cannot route Room Contexts from an invalid graph producer.");
			return false;
		}
		UPCGNode* OutputNode = Graph->GetOutputNode();
		UPCGGraphInputOutputSettings* OutputSettings = OutputNode
			? Cast<UPCGGraphInputOutputSettings>(OutputNode->GetSettings())
			: nullptr;
		if (!OutputSettings || OutputNode->GetInputPin(FEFCalystoRoomThemePinsV6::RoomContexts))
		{
			OutError = FString::Printf(TEXT("Graph %s cannot add one unique Room Contexts output."), *Graph->GetPathName());
			return false;
		}

		const FPCGPinProperties& AddedPin = OutputSettings->AddPin(
			FPCGPinProperties(FEFCalystoRoomThemePinsV6::RoomContexts, EPCGDataType::Point));
		if (AddedPin.Label != FEFCalystoRoomThemePinsV6::RoomContexts)
		{
			OutError = FString::Printf(TEXT("Graph %s renamed the reserved Room Contexts output."), *Graph->GetPathName());
			return false;
		}
		OutputNode->UpdateAfterSettingsChangeDuringCreation();
		if (!OutputNode->GetInputPin(FEFCalystoRoomThemePinsV6::RoomContexts))
		{
			OutError = FString::Printf(TEXT("Graph %s did not materialize its Room Contexts output pin."), *Graph->GetPathName());
			return false;
		}
		Graph->AddEdge(
			Producer,
			FEFCalystoRoomThemePinsV6::RoomContexts,
			OutputNode,
			FEFCalystoRoomThemePinsV6::RoomContexts);
		const UPCGPin* RoutedPin = OutputNode->GetInputPin(FEFCalystoRoomThemePinsV6::RoomContexts);
		if (!RoutedPin || !RoutedPin->IsConnected())
		{
			OutError = FString::Printf(TEXT("Graph %s failed to route Room Contexts to its boundary."), *Graph->GetPathName());
			return false;
		}
		return true;
	}

	using FNeutralRoomAttribute = TPair<FName, FPCGMetadataTypesConstantStruct>;

	static bool BuildNeutralRoomAttributes(
		const FEFCalystoRoomThemeGenerationConfigV6& Config,
		TArray<FNeutralRoomAttribute>& OutAttributes,
		FString& OutError)
	{
		OutAttributes.Reset();
		// The native RoomType property class retains its CDO through the actor's
		// RoomTheme schema. Resolve that already-resident schema; never load an
		// asset or create/save a secondary Room Type asset during generation.
		UObject* EmptyRoomSchema = FSoftObjectPath(EmptyRoomSchemaPath).ResolveObject();
		if (!IsValid(EmptyRoomSchema) || !EmptyRoomSchema->HasAnyFlags(RF_ClassDefaultObject))
		{
			OutError = TEXT("The native empty Room Type schema is not resident; preload PDA_RoomMeshes before graph composition.");
			return false;
		}
		for (const TCHAR* Name : {TEXT("WallBottom"), TEXT("WallMiddle"), TEXT("WallTop"), TEXT("Floor"),
			TEXT("CornerBottom"), TEXT("CornerMiddle"), TEXT("CornerTop"), TEXT("Roof")})
		{
			const FArrayProperty* Property = FindFProperty<FArrayProperty>(EmptyRoomSchema->GetClass(), Name);
			if (!Property || !CastField<FStructProperty>(Property->Inner)
				|| FScriptArrayHelper_InContainer(Property, EmptyRoomSchema).Num() != 0)
			{
				OutError = FString::Printf(TEXT("Native NoTheme schema field %s must be an empty decoration array."), Name);
				return false;
			}
		}
		const auto AddPath = [&OutAttributes](const FName Name, const FSoftObjectPath& Value)
		{
			FPCGMetadataTypesConstantStruct Attribute;
			Attribute.Type = EPCGMetadataTypes::SoftObjectPath;
			Attribute.SoftObjectPathValue = Value;
			OutAttributes.Emplace(Name, Attribute);
		};
		const auto AddFalse = [&OutAttributes](const FName Name)
		{
			FPCGMetadataTypesConstantStruct Attribute;
			Attribute.Type = EPCGMetadataTypes::Boolean;
			Attribute.BoolValue = false;
			OutAttributes.Emplace(Name, Attribute);
		};
		AddPath(FEFCalystoRoomThemeMetadataV6::VendorRoomType, FSoftObjectPath(EmptyRoomSchema));
		AddFalse(FEFCalystoRoomThemeMetadataV6::VendorOverrideFloorMaterial);
		AddPath(FEFCalystoRoomThemeMetadataV6::VendorFloorMaterial, Config.Style.FloorMaterial.ToSoftObjectPath());
		AddFalse(FEFCalystoRoomThemeMetadataV6::VendorOverrideWallMaterial);
		AddPath(FEFCalystoRoomThemeMetadataV6::VendorWallMaterial, Config.Style.WallMaterial.ToSoftObjectPath());
		AddFalse(FEFCalystoRoomThemeMetadataV6::VendorOverrideRoofMaterial);
		AddPath(FEFCalystoRoomThemeMetadataV6::VendorRoofMaterial, Config.Style.RoofMaterial.ToSoftObjectPath());
		return true;
	}

	static bool ValidateCompleteNativeRoomStream(const UPCGNode* Selector, FString& OutError)
	{
		const UPCGCalystoAssignRoomThemeSettings* SelectorSettings = Selector
			? Cast<UPCGCalystoAssignRoomThemeSettings>(Selector->GetSettings()) : nullptr;
		const UPCGGraph* Shape = Selector ? Cast<UPCGGraph>(Selector->GetOuter()) : nullptr;
		const UPCGNode* Merge = FindUniqueNodeByObjectName(Shape, NativeThemeMergeName, OutError);
		const UPCGNode* AllRooms = FindUniqueNodeByObjectName(Shape, AllUsedRoomsNodeName, OutError);
		const UPCGPin* MergeInput = Merge ? Merge->GetInputPin(PCGPinConstants::DefaultInputLabel) : nullptr;
		const UPCGPin* MergeOutput = Merge ? Merge->GetOutputPin(PCGPinConstants::DefaultOutputLabel) : nullptr;
		const UPCGPin* AllInput = AllRooms ? AllRooms->GetInputPin(PCGPinConstants::DefaultInputLabel) : nullptr;
		const UPCGPin* Themed = Selector ? Selector->GetOutputPin(FEFCalystoRoomThemePinsV6::ThemedRooms) : nullptr;
		const UPCGPin* Current = Selector ? Selector->GetOutputPin(FEFCalystoRoomThemePinsV6::UnthemedRooms) : nullptr;
		const auto HasSoleEdgeTo = [](const UPCGPin* Output, const UPCGPin* Input)
		{
			return Output && Input && Output->Edges.Num() == 1 && Output->Edges[0]
				&& Output->Edges[0]->IsValid() && Output->Edges[0]->InputPin == Output
				&& Output->Edges[0]->OutputPin == Input;
		};
		if (!SelectorSettings || !MergeInput || MergeInput->Edges.Num() != 2
			|| !HasSoleEdgeTo(Themed, MergeInput) || !HasSoleEdgeTo(MergeOutput, AllInput)
			|| !AllInput || AllInput->Edges.Num() != 1)
		{
			OutError = TEXT("Native All Used Rooms must receive exactly the themed and neutral unthemed streams through MergePoints_48.");
			return false;
		}
		TArray<FNeutralRoomAttribute> Expected;
		if (!BuildNeutralRoomAttributes(SelectorSettings->ResolvedConfig, Expected, OutError)) return false;
		for (const FNeutralRoomAttribute& Attribute : Expected)
		{
			const UPCGPin* Input = Current && Current->Edges.Num() == 1 && Current->Edges[0]
				&& Current->Edges[0]->IsValid() ? Current->Edges[0]->OutputPin : nullptr;
			const UPCGNode* Node = Input ? Input->Node : nullptr;
			const UPCGAddAttributeSettings* Settings = Node ? Cast<UPCGAddAttributeSettings>(Node->GetSettings()) : nullptr;
			if (!Settings || !Node->IsIn(Shape) || Input->Edges.Num() != 1
				|| Input->Properties.Label != PCGPinConstants::DefaultInputLabel
				|| Settings->bCopyAllAttributes || Settings->OutputTarget.GetAttributeName() != Attribute.Key
				|| Settings->AttributeTypes.Type != Attribute.Value.Type
				|| (Attribute.Value.Type == EPCGMetadataTypes::Boolean && Settings->AttributeTypes.BoolValue)
				|| (Attribute.Value.Type == EPCGMetadataTypes::SoftObjectPath
					&& Settings->AttributeTypes.SoftObjectPathValue != Attribute.Value.SoftObjectPathValue))
			{
				OutError = FString::Printf(TEXT("Native NoTheme room stream lost its exact neutral %s attribute contract."), *Attribute.Key.ToString());
				return false;
			}
			Current = Node->GetOutputPin(PCGPinConstants::DefaultOutputLabel);
		}
		if (!HasSoleEdgeTo(Current, MergeInput))
		{
			OutError = TEXT("Neutral unthemed rooms do not reach the native structural room merge.");
			return false;
		}
		return true;
	}

	struct FBuildContext
	{
		const FEFCalystoRoomThemeGenerationConfigV6& Config;
		TMap<const UPCGGraph*, bool> ContainsShapeMemo;
		TSet<const UPCGGraph*> ContainsShapeStack;
		TMap<const UPCGGraph*, UPCGGraph*> Clones;
		int32 ClonedGraphCount = 0;
		int32 InvalidatedCookedDataCount = 0;
		int32 RoutedBoundaryCount = 0;
		int32 ReplacedThemeNodeCount = 0;
		FString Error;

		bool ContainsShape(const UPCGGraph* Graph)
		{
			if (!IsValid(Graph))
			{
				return false;
			}
			if (Graph->GetPathName() == ShapeGraphPath)
			{
				return true;
			}
			if (const bool* Cached = ContainsShapeMemo.Find(Graph))
			{
				return *Cached;
			}
			if (ContainsShapeStack.Contains(Graph))
			{
				return false;
			}
			ContainsShapeStack.Add(Graph);
			bool bContains = false;
			for (const UPCGNode* Node : Graph->GetNodes())
			{
				const UPCGSubgraphSettings* Settings = Node ? Cast<UPCGSubgraphSettings>(Node->GetSettings()) : nullptr;
				if (Settings && ContainsShape(Settings->GetSubgraph()))
				{
					bContains = true;
					break;
				}
			}
			ContainsShapeStack.Remove(Graph);
			ContainsShapeMemo.Add(Graph, bContains);
			return bContains;
		}

		bool PatchEndpointSelection(UPCGGraph* ShapeClone)
		{
			int32 PatchedCalls = 0;
			for (UPCGNode* Call : ShapeClone->GetNodes())
			{
				UPCGSubgraphSettings* CallSettings = Call ? Cast<UPCGSubgraphSettings>(Call->GetSettings()) : nullptr;
				UPCGGraph* Source = CallSettings ? CallSettings->GetSubgraph() : nullptr;
				if (!Source || Source->GetPathName() != TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_GetRandomStartEnd.PCG_GetRandomStartEnd")) continue;
				if (!CallSettings->IsIn(ShapeClone) || !CallSettings->SubgraphInstance
					|| !CallSettings->SubgraphInstance->IsIn(ShapeClone) || CallSettings->SubgraphOverride
					|| HasConnectedSubgraphOverridePin(CallSettings, Call))
				{
					Error = TEXT("Endpoint selection must be an owned, non-overridden native Shape call.");
					return false;
				}
				UPCGGraph* Clone = DuplicateObject<UPCGGraph>(Source, ShapeClone,
					MakeUniqueObjectName(ShapeClone, Source->GetClass(), TEXT("EFCalystoV6_EndpointPool")));
				if (!Clone) return false;
				Clone->ClearFlags(RF_Public | RF_Standalone);
				Clone->SetFlags(RF_Transient);
				if (!InvalidateDuplicatedCookedCompilationData(Clone, Error)) return false;
				++ClonedGraphCount;
				++InvalidatedCookedDataCount;
				UPCGNode* Multiply = FindUniqueNodeByObjectName(Clone, TEXT("AttributeMathsOp_7"), Error);
				UPCGNode* Fraction = FindUniqueNodeByObjectName(Clone, TEXT("CreateAttribute_8"), Error);
				UPCGNode* Start = FindUniqueNodeByObjectName(Clone, TEXT("AttributeFilter_6"), Error);
				UPCGNode* End = FindUniqueNodeByObjectName(Clone, TEXT("AttributeFilter_9"), Error);
				UPCGNode* Reroute = FindUniqueNodeByObjectName(Clone, TEXT("Reroute_10"), Error);
				const UPCGMetadataMathsSettings* Maths = Multiply ? Cast<UPCGMetadataMathsSettings>(Multiply->GetSettings()) : nullptr;
				const UPCGCreateAttributeSetSettings* Constant = Fraction ? Cast<UPCGCreateAttributeSetSettings>(Fraction->GetSettings()) : nullptr;
				const UPCGAttributeFilteringSettings* StartFilter = Start ? Cast<UPCGAttributeFilteringSettings>(Start->GetSettings()) : nullptr;
				const UPCGAttributeFilteringSettings* EndFilter = End ? Cast<UPCGAttributeFilteringSettings>(End->GetSettings()) : nullptr;
				const auto IsIndexFilter = [](const UPCGAttributeFilteringSettings* Filter)
				{
					return Filter && Filter->Operator == EPCGAttributeFilterOperator::Lesser
						&& !Filter->bUseConstantThreshold && Filter->TargetAttribute.ToString() == TEXT("$Index");
				};
				UPCGPin* Output = Multiply ? Multiply->GetOutputPin(PCGPinConstants::DefaultOutputLabel) : nullptr;
				UPCGPin* StartInput = Start ? Start->GetInputPin(TEXT("Filter")) : nullptr;
				UPCGPin* EndInput = Reroute ? Reroute->GetInputPin(PCGPinConstants::DefaultInputLabel) : nullptr;
				const auto HasSoleEdgeTo = [](const UPCGPin* Input, const UPCGPin* Producer)
				{
					return Input && Input->Edges.Num() == 1 && Input->Edges[0]
						&& Input->Edges[0]->GetOtherPin(Input) == Producer;
				};
				if (!Maths || Maths->Operation != EPCGMetadataMathsOperation::Multiply || !Constant
					|| Constant->AttributeTypes.Type != EPCGMetadataTypes::Double || Constant->AttributeTypes.DoubleValue != 0.1
					|| !IsIndexFilter(StartFilter) || !IsIndexFilter(EndFilter)
					|| !Output || Output->Edges.Num() != 2 || !HasSoleEdgeTo(StartInput, Output) || !HasSoleEdgeTo(EndInput, Output))
				{
					Error = TEXT("Native endpoint top-ten-percent signature changed; refusing an unverified topology patch.");
					return false;
				}
				// UE 5.8 filters convert the threshold to the target's int64 $Index type.
				// For 1..9 candidates, count * 0.1 truncates to zero and removes EVERY
				// endpoint. Keep native top-10% semantics, but retain at least one candidate.
				// Copy selectors/schema from the native operation; never touch source assets.
				UPCGCreateAttributeSetSettings* One = DuplicateObject<UPCGCreateAttributeSetSettings>(Constant, Clone,
					MakeUniqueObjectName(Clone, Constant->GetClass(), TEXT("EFCalystoEndpointMinimumOne")));
				One->SetFlags(RF_Transient);
				One->AttributeTypes.DoubleValue = 1.0;
				UPCGMetadataMathsSettings* Maximum = DuplicateObject<UPCGMetadataMathsSettings>(Maths, Clone,
					MakeUniqueObjectName(Clone, Maths->GetClass(), TEXT("EFCalystoEndpointPoolMaximum")));
				Maximum->SetFlags(RF_Transient);
				Maximum->Operation = EPCGMetadataMathsOperation::Max;
				UPCGNode* OneNode = Clone->AddNode(One);
				UPCGNode* MaxNode = Clone->AddNode(Maximum);
				if (!OneNode || !MaxNode || !StartInput->BreakEdgeTo(Output) || !EndInput->BreakEdgeTo(Output))
				{
					Error = TEXT("Could not compose the endpoint minimum-one compatibility nodes.");
					return false;
				}
				Clone->AddEdge(Multiply, PCGPinConstants::DefaultOutputLabel, MaxNode, TEXT("InA"));
				Clone->AddEdge(OneNode, PCGPinConstants::DefaultOutputLabel, MaxNode, TEXT("InB"));
				Clone->AddEdge(MaxNode, PCGPinConstants::DefaultOutputLabel, Start, TEXT("Filter"));
				Clone->AddEdge(MaxNode, PCGPinConstants::DefaultOutputLabel, Reroute, PCGPinConstants::DefaultInputLabel);
				CallSettings->SetSubgraph(Clone);
				Call->UpdateAfterSettingsChangeDuringCreation();
				++PatchedCalls;
			}
			if (PatchedCalls != 1)
			{
				Error = TEXT("Expected exactly one native endpoint selection call in Shape.");
				return false;
			}
			return true;
		}

		bool PatchShapeGraph(UPCGGraph* ShapeClone)
		{
			if (!PatchEndpointSelection(ShapeClone)) return false;
			UPCGNode* NativeThemeNode = FindUniqueNodeByObjectName(ShapeClone, NativeThemeNodeName, Error);
			UPCGNode* MergeNode = FindUniqueNodeByObjectName(ShapeClone, NativeThemeMergeName, Error);
			UPCGNode* AllRoomsNode = FindUniqueNamedDeclaration(ShapeClone, AllUsedRoomsDeclaration, Error);
			UPCGNode* MainRoomsNode = FindUniqueNamedDeclaration(ShapeClone, MainRoomsDeclaration, Error);
			UPCGNode* SideRoomsNode = FindUniqueNamedDeclaration(ShapeClone, SideRoomsDeclaration, Error);
			UPCGNode* StartNode = FindUniqueNamedDeclaration(ShapeClone, StartRoomsDeclaration, Error);
			UPCGNode* EndNode = FindUniqueNamedDeclaration(ShapeClone, EndRoomsDeclaration, Error);
			if (!NativeThemeNode || !MergeNode || !AllRoomsNode || !MainRoomsNode || !SideRoomsNode || !StartNode || !EndNode)
			{
				return false;
			}

			UPCGPin* AllRoomsOutput = ValidateNamedRoomDeclaration(
				AllRoomsNode, AllUsedRoomsNodeName, AllUsedRoomsDeclaration, Error);
			UPCGPin* MainRoomsOutput = ValidateNamedRoomDeclaration(
				MainRoomsNode, MainRoomsNodeName, MainRoomsDeclaration, Error);
			UPCGPin* SideRoomsOutput = ValidateNamedRoomDeclaration(
				SideRoomsNode, SideRoomsNodeName, SideRoomsDeclaration, Error);
			UPCGPin* StartOutput = ValidateNamedRoomDeclaration(
				StartNode, StartRoomsNodeName, StartRoomsDeclaration, Error);
			UPCGPin* EndOutput = ValidateNamedRoomDeclaration(
				EndNode, EndRoomsNodeName, EndRoomsDeclaration, Error);
			UPCGPin* NativeThemeOutput = nullptr;
			UPCGPin* MergeInput = FindUniqueInputConnectedFromNode(
				MergeNode, NativeThemeNode, NativeThemeOutput, Error);
			if (!AllRoomsOutput || !MainRoomsOutput || !SideRoomsOutput || !StartOutput || !EndOutput || !MergeInput)
			{
				return false;
			}
			UPCGCalystoAssignRoomThemeSettings* SelectorSettings = NewObject<UPCGCalystoAssignRoomThemeSettings>(
				ShapeClone,
				MakeUniqueObjectName(ShapeClone, UPCGCalystoAssignRoomThemeSettings::StaticClass(), TEXT("EFCalystoAssignRoomThemeV6")),
				RF_Transient);
			SelectorSettings->SetResolvedConfig(Config);
			UPCGNode* SelectorNode = ShapeClone->AddNode(SelectorSettings);
			if (!IsValid(SelectorNode)
				|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::MainRooms)
				|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::SideRooms)
				|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::CriticalRooms)
				|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::ProgressionRooms)
				|| !SelectorNode->GetOutputPin(FEFCalystoRoomThemePinsV6::RoomContexts))
			{
				Error = TEXT("Failed to create the native Calysto V6 Room Theme node on the transient Shape graph.");
				return false;
			}

			if (!MergeInput->BreakEdgeTo(NativeThemeOutput))
			{
				Error = TEXT("Calysto V6 could not remove the unique native Theme edge from MergePoints_48.");
				return false;
			}
			ShapeClone->AddEdge(MainRoomsNode, MainRoomsOutput->Properties.Label, SelectorNode, FEFCalystoRoomThemePinsV6::MainRooms);
			ShapeClone->AddEdge(SideRoomsNode, SideRoomsOutput->Properties.Label, SelectorNode, FEFCalystoRoomThemePinsV6::SideRooms);
			ShapeClone->AddEdge(StartNode, StartOutput->Properties.Label, SelectorNode, FEFCalystoRoomThemePinsV6::StartRooms);
			ShapeClone->AddEdge(EndNode, EndOutput->Properties.Label, SelectorNode, FEFCalystoRoomThemePinsV6::EndRooms);
			// MergePoints_48 feeds native All Used Rooms, room splines and structural
			// Floor/Wall/Roof generation. Protected and NoTheme rooms must survive.
			// Supply a valid empty native decoration schema on the unthemed stream
			// so downstream RoomType property reads remain valid without theming it.
			ShapeClone->AddEdge(SelectorNode, FEFCalystoRoomThemePinsV6::ThemedRooms, MergeNode, MergeInput->Properties.Label);
			TArray<FNeutralRoomAttribute> NeutralAttributes;
			if (!BuildNeutralRoomAttributes(Config, NeutralAttributes, Error)) return false;
			UPCGNode* NeutralProducer = SelectorNode;
			FName NeutralOutput = FEFCalystoRoomThemePinsV6::UnthemedRooms;
			for (const FNeutralRoomAttribute& Attribute : NeutralAttributes)
			{
				UPCGAddAttributeSettings* Settings = NewObject<UPCGAddAttributeSettings>(ShapeClone,
					MakeUniqueObjectName(ShapeClone, UPCGAddAttributeSettings::StaticClass(), TEXT("EFCalystoNeutralRoomAttribute")), RF_Transient);
				Settings->OutputTarget.SetAttributeName(Attribute.Key);
				Settings->AttributeTypes = Attribute.Value;
				UPCGNode* Node = ShapeClone->AddNode(Settings);
				if (!Node || !Node->GetInputPin(PCGPinConstants::DefaultInputLabel)
					|| !Node->GetOutputPin(PCGPinConstants::DefaultOutputLabel))
				{
					Error = TEXT("Could not create a native neutral Room Type attribute node.");
					return false;
				}
				ShapeClone->AddEdge(NeutralProducer, NeutralOutput, Node, PCGPinConstants::DefaultInputLabel);
				NeutralProducer = Node;
				NeutralOutput = PCGPinConstants::DefaultOutputLabel;
			}
			ShapeClone->AddEdge(NeutralProducer, NeutralOutput, MergeNode, MergeInput->Properties.Label);

			if (!SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::MainRooms)->IsConnected()
				|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::SideRooms)->IsConnected()
				|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::StartRooms)->IsConnected()
				|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::EndRooms)->IsConnected()
				|| !SelectorNode->GetOutputPin(FEFCalystoRoomThemePinsV6::ThemedRooms)->IsConnected()
				|| !SelectorNode->GetOutputPin(FEFCalystoRoomThemePinsV6::UnthemedRooms)->IsConnected())
			{
				Error = TEXT("Calysto room graph edges did not preserve both themed and unthemed native construction streams.");
				return false;
			}

			ShapeClone->RemoveNode(NativeThemeNode);
			++ReplacedThemeNodeCount;
			if (!AddContextGraphOutput(ShapeClone, SelectorNode, Error))
			{
				return false;
			}
			++RoutedBoundaryCount;
			return true;
		}

		UPCGGraph* ComposePathToShape(
			UPCGGraph* Source,
			UObject* Outer,
			const bool bReuseOwnedTransientRoot = false)
		{
			if (!IsValid(Source) || !IsValid(Outer) || !ContainsShape(Source))
			{
				Error = TEXT("Cannot compose an invalid Calysto graph path to the Shape graph.");
				return nullptr;
			}
			if (bReuseOwnedTransientRoot && !Source->HasAnyFlags(RF_Transient))
			{
				Error = TEXT("Only the already-owned transient cooked Master may be reused by the V6 compositor.");
				return nullptr;
			}
			if (UPCGGraph** Existing = Clones.Find(Source))
			{
				return *Existing;
			}

			UPCGGraph* Clone = Source;
			if (!bReuseOwnedTransientRoot)
			{
				const FName CloneName = MakeUniqueObjectName(
					Outer,
					Source->GetClass(),
					FName(*FString::Printf(TEXT("EFCalystoV6_%s"), *Source->GetName())));
				Clone = DuplicateObject<UPCGGraph>(Source, Outer, CloneName);
				if (!IsValid(Clone))
				{
					Error = FString::Printf(TEXT("Failed to duplicate graph %s for Calysto V6."), *Source->GetPathName());
					return nullptr;
				}
				Clone->ClearFlags(RF_Public | RF_Standalone);
				Clone->SetFlags(RF_Transient);
				if (!InvalidateDuplicatedCookedCompilationData(Clone, Error))
				{
					return nullptr;
				}
				++ClonedGraphCount;
				++InvalidatedCookedDataCount;
			}
			else if (Clone->GetOuter() != Outer)
			{
				Error = FString::Printf(
					TEXT("Cooked Master %s is not owned by the compositor's exact transient outer."),
					*Clone->GetPathName());
				return nullptr;
			}
			Clones.Add(Source, Clone);

			if (Source->GetPathName() == ShapeGraphPath)
			{
				return PatchShapeGraph(Clone) ? Clone : nullptr;
			}

			int32 ShapeChildCount = 0;
			for (UPCGNode* Node : Clone->GetNodes())
			{
				UPCGSubgraphSettings* Settings = Node ? Cast<UPCGSubgraphSettings>(Node->GetSettings()) : nullptr;
				UPCGGraph* ChildSource = Settings ? Settings->GetSubgraph() : nullptr;
				if (!Settings || !ContainsShape(ChildSource))
				{
					continue;
				}
				++ShapeChildCount;
				if (!Node->IsIn(Clone)
					|| !Settings->IsIn(Clone)
					|| !IsValid(Settings->SubgraphInstance)
					|| !Settings->SubgraphInstance->IsIn(Clone)
					|| IsValid(Settings->SubgraphOverride)
					|| HasConnectedSubgraphOverridePin(Settings, Node))
				{
					Error = FString::Printf(TEXT("Subgraph ownership or override state drifted at %s:%s."), *Source->GetPathName(), *Node->GetName());
					return nullptr;
				}
				UPCGGraph* ChildClone = ComposePathToShape(ChildSource, Clone);
				if (!ChildClone)
				{
					return nullptr;
				}
				Settings->SetSubgraph(ChildClone);
				Node->UpdateAfterSettingsChangeDuringCreation();
				if (Settings->GetSubgraph() != ChildClone
					|| !Node->GetOutputPin(FEFCalystoRoomThemePinsV6::RoomContexts))
				{
					Error = FString::Printf(TEXT("V6 subgraph output did not propagate at %s:%s."), *Source->GetPathName(), *Node->GetName());
					return nullptr;
				}
				if (!AddContextGraphOutput(Clone, Node, Error))
				{
					return nullptr;
				}
				++RoutedBoundaryCount;
			}
			if (ShapeChildCount != 1)
			{
				Error = FString::Printf(TEXT("Graph %s reaches the Shape graph through %d child nodes; exactly one is required."), *Source->GetPathName(), ShapeChildCount);
				return nullptr;
			}
			return Clone;
		}
	};

	static int32 CountSelectorNodesRecursive(const UPCGGraph* Root, TSet<const UPCGGraph*>& Visited)
	{
		if (!IsValid(Root) || Visited.Contains(Root))
		{
			return 0;
		}
		Visited.Add(Root);
		int32 Count = 0;
		for (const UPCGNode* Node : Root->GetNodes())
		{
			if (!IsValid(Node))
			{
				continue;
			}
			Count += Cast<UPCGCalystoAssignRoomThemeSettings>(Node->GetSettings()) ? 1 : 0;
			if (const UPCGSubgraphSettings* Subgraph = Cast<UPCGSubgraphSettings>(Node->GetSettings()))
			{
				Count += CountSelectorNodesRecursive(Subgraph->GetSubgraph(), Visited);
			}
		}
		return Count;
	}

	static const UPCGNode* FindSelectorNodeRecursive(
		const UPCGGraph* Root,
		TSet<const UPCGGraph*>& Visited)
	{
		if (!IsValid(Root) || Visited.Contains(Root))
		{
			return nullptr;
		}
		Visited.Add(Root);
		for (const UPCGNode* Node : Root->GetNodes())
		{
			if (!IsValid(Node))
			{
				continue;
			}
			if (Cast<UPCGCalystoAssignRoomThemeSettings>(Node->GetSettings()))
			{
				return Node;
			}
			if (const UPCGSubgraphSettings* Subgraph = Cast<UPCGSubgraphSettings>(Node->GetSettings()))
			{
				if (const UPCGNode* Found = FindSelectorNodeRecursive(Subgraph->GetSubgraph(), Visited))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}
}

FEFCalystoPCGRuntimeGraphResultV6 FEFCalystoPCGRuntimeGraphBuilderV6::TryBuild(
	UPCGGraph* SourceRootGraph,
	UObject* TransientOuter,
	const FEFCalystoRoomThemeGenerationConfigV6& ResolvedConfig,
	const bool bForceCookedRulesForAutomation)
{
	using namespace EFCalystoPCGRuntimeGraphV6Private;
	FEFCalystoPCGRuntimeGraphResultV6 Result;
	auto Fail = [&Result](FString&& Reason)
	{
		Result.FailureReason = MoveTemp(Reason);
		return Result;
	};
	if (!IsInGameThread())
	{
		return Fail(TEXT("Calysto V6 runtime graph construction must run on the game thread."));
	}
	if (!IsValid(SourceRootGraph) || SourceRootGraph->GetPathName() != SourceRootPath)
	{
		return Fail(FString::Printf(TEXT("Calysto V6 source is %s; expected exact root %s."), *GetPathNameSafe(SourceRootGraph), SourceRootPath));
	}
	if (!IsValid(TransientOuter))
	{
		return Fail(TEXT("Calysto V6 runtime graph construction requires a valid transient outer."));
	}
	FString Error;
	if (!ResolvedConfig.Validate(Error))
	{
		return Fail(FString::Printf(TEXT("Calysto V6 resolved Room Theme configuration is invalid: %s"), *Error));
	}

	TSet<const UPCGGraph*> ExistingVisited;
	if (CountSelectorNodesRecursive(SourceRootGraph, ExistingVisited) != 0)
	{
		return Fail(TEXT("The immutable Calysto source graph unexpectedly contains a V6 Room Theme node."));
	}

	const FEFCalystoPCGCookedCompatibilityResult Compatibility =
		FEFCalystoPCGCookedCompatibility::TryBuild(SourceRootGraph, TransientOuter, bForceCookedRulesForAutomation);
	if (!Compatibility.bApplied || !IsValid(Compatibility.RuntimeGraph))
	{
		return Fail(FString::Printf(TEXT("Cooked compatibility composition failed: %s"), *Compatibility.FailureReason));
	}

	FBuildContext BuildContext{ResolvedConfig};
	if (!BuildContext.ContainsShape(Compatibility.RuntimeGraph))
	{
		return Fail(TEXT("The Calysto runtime root no longer reaches the exact MassiveDungeonShape graph."));
	}
	// Cooked compatibility already owns the only transient Master. Reuse it and
	// clone only Shape; normal Editor composition still clones Master + Shape.
	UPCGGraph* RuntimeGraph = BuildContext.ComposePathToShape(
		Compatibility.RuntimeGraph,
		TransientOuter,
		Compatibility.RuntimeGraph->HasAnyFlags(RF_Transient));
	if (!RuntimeGraph)
	{
		return Fail(BuildContext.Error.IsEmpty() ? TEXT("Failed to compose the Calysto V6 transient graph.") : MoveTemp(BuildContext.Error));
	}
	int32 RoutedPlacementOutputCount = 0;
	if (!RouteNativePlacementCandidates(
		RuntimeGraph,
		RoutedPlacementOutputCount,
		Error))
	{
		return Fail(MoveTemp(Error));
	}
	if (!ValidateRuntimeGraph(RuntimeGraph, Error))
	{
		return Fail(MoveTemp(Error));
	}

	Result.RuntimeGraph = RuntimeGraph;
	Result.PlacementRoutingFingerprint = BuildPlacementRoutingFingerprint();
	Result.ConfigurationFingerprint = BuildRuntimeGraphConfigurationFingerprint(
		ResolvedConfig,
		Result.PlacementRoutingFingerprint);
	Result.ClonedGraphCount = BuildContext.ClonedGraphCount + Compatibility.ClonedGraphCount;
	Result.InvalidatedCookedCompilationDataCount = BuildContext.InvalidatedCookedDataCount + Compatibility.InvalidatedCookedCompilationDataCount;
	Result.RoutedContextBoundaryCount = BuildContext.RoutedBoundaryCount;
	Result.RoutedPlacementOutputCount = RoutedPlacementOutputCount;
	Result.ReplacedNativeThemeNodeCount = BuildContext.ReplacedThemeNodeCount;
	Result.bCookedCompatibilityApplied = Compatibility.ClonedGraphCount > 0;
	Result.bBuilt = true;
	UE_LOG(
		LogEFCalystoPCGRuntimeGraphV6,
		Log,
		TEXT("PASS V6 runtimeGraph=%s config=%s placement=%s clones=%d invalidatedCookedCaches=%d contextBoundaries=%d placementOutputs=%d nativeThemeNodesReplaced=%d vendorAssetsMutated=0."),
		*RuntimeGraph->GetPathName(),
		*Result.ConfigurationFingerprint,
		*Result.PlacementRoutingFingerprint,
		Result.ClonedGraphCount,
		Result.InvalidatedCookedCompilationDataCount,
		Result.RoutedContextBoundaryCount,
		Result.RoutedPlacementOutputCount,
		Result.ReplacedNativeThemeNodeCount);
	return Result;
}

bool FEFCalystoPCGRuntimeGraphBuilderV6::ValidateRuntimeGraph(const UPCGGraph* RuntimeGraph, FString& OutError)
{
	using namespace EFCalystoPCGRuntimeGraphV6Private;
	OutError.Reset();
	if (!IsValid(RuntimeGraph) || !RuntimeGraph->HasAnyFlags(RF_Transient))
	{
		OutError = TEXT("Calysto V6 runtime graph must be valid and transient.");
		return false;
	}
	if (!ValidateSurfaceMaterialConsumersRecursive(RuntimeGraph, OutError))
	{
		return false;
	}
	TSet<const UPCGGraph*> Visited;
	const int32 SelectorCount = CountSelectorNodesRecursive(RuntimeGraph, Visited);
	if (SelectorCount != 1)
	{
		OutError = FString::Printf(TEXT("Calysto V6 runtime closure contains %d Room Theme assignment nodes; exactly one is required."), SelectorCount);
		return false;
	}
	TSet<const UPCGGraph*> SelectorVisited;
	const UPCGNode* SelectorNode = FindSelectorNodeRecursive(RuntimeGraph, SelectorVisited);
	if (!SelectorNode
		|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::MainRooms)
		|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::MainRooms)->IsConnected()
		|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::SideRooms)
		|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::SideRooms)->IsConnected()
		|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::StartRooms)
		|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::StartRooms)->IsConnected()
		|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::EndRooms)
		|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::EndRooms)->IsConnected()
		|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::CriticalRooms)
		|| !SelectorNode->GetInputPin(FEFCalystoRoomThemePinsV6::ProgressionRooms)
		|| !SelectorNode->GetOutputPin(FEFCalystoRoomThemePinsV6::ThemedRooms)
		|| !SelectorNode->GetOutputPin(FEFCalystoRoomThemePinsV6::ThemedRooms)->IsConnected()
		|| !SelectorNode->GetOutputPin(FEFCalystoRoomThemePinsV6::UnthemedRooms)
		|| !SelectorNode->GetOutputPin(FEFCalystoRoomThemePinsV6::UnthemedRooms)->IsConnected()
		|| !SelectorNode->GetOutputPin(FEFCalystoRoomThemePinsV6::RoomContexts)
		|| !SelectorNode->GetOutputPin(FEFCalystoRoomThemePinsV6::RoomContexts)->IsConnected())
	{
		OutError = TEXT("Calysto V6 selector lost its exact Main Rooms, Side Rooms, Start, End, protected-marker pin, or output edge contract.");
		return false;
	}
	if (!ValidateCompleteNativeRoomStream(SelectorNode, OutError)) return false;
	const UPCGNode* OutputNode = RuntimeGraph->GetOutputNode();
	const UPCGPin* ContextPin = OutputNode ? OutputNode->GetInputPin(FEFCalystoRoomThemePinsV6::RoomContexts) : nullptr;
	if (!ContextPin || !ContextPin->IsConnected())
	{
		OutError = TEXT("Calysto V6 root graph does not expose one connected Room Contexts output.");
		return false;
	}
	return true;
}

FEFCalystoPCGSingleGenerationGateV6::FEFCalystoPCGSingleGenerationGateV6(
	UPCGComponent* InComponent,
	UPCGGraph* InRuntimeGraph)
	: Component(InComponent)
	, RuntimeGraph(InRuntimeGraph)
{
}

TSharedPtr<FEFCalystoPCGSingleGenerationGateV6> FEFCalystoPCGSingleGenerationGateV6::CreateAndPrepare(
	UPCGComponent* Component,
	UPCGGraph* RuntimeGraph,
	FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread() || !IsValid(Component) || !IsValid(RuntimeGraph))
	{
		OutError = TEXT("Single-generation preparation requires valid objects on the game thread.");
		return nullptr;
	}
	if (Component->GenerationTrigger != EPCGComponentGenerationTrigger::GenerateOnDemand)
	{
		OutError = TEXT("Single-generation preparation requires Generate On Demand.");
		return nullptr;
	}
	if (Component->IsGenerating() || Component->IsCleaningUp() || Component->bGenerated)
	{
		OutError = TEXT("PCG component is already generated, generating, or cleaning; refusing a second generation path.");
		return nullptr;
	}
	if (Component->GetGraph() == RuntimeGraph)
	{
		OutError = TEXT("The V6 runtime graph was already installed; refusing to create a second generation gate.");
		return nullptr;
	}
	if (!FEFCalystoPCGRuntimeGraphBuilderV6::ValidateRuntimeGraph(RuntimeGraph, OutError))
	{
		return nullptr;
	}
	Component->SetGraphLocal(RuntimeGraph);
	if (Component->GetGraph() != RuntimeGraph)
	{
		OutError = TEXT("SetGraphLocal did not install the exact Calysto V6 transient graph.");
		return nullptr;
	}
	return MakeShareable(new FEFCalystoPCGSingleGenerationGateV6(Component, RuntimeGraph));
}

FPCGTaskId FEFCalystoPCGSingleGenerationGateV6::GenerateOnce(FString& OutError, const bool bForce)
{
	OutError.Reset();
	UPCGComponent* StrongComponent = Component.Get();
	UPCGGraph* StrongGraph = RuntimeGraph.Get();
	if (bConsumed)
	{
		OutError = TEXT("The Calysto V6 single-generation gate was already consumed.");
		return InvalidPCGTaskId;
	}
	if (!IsInGameThread() || !IsValid(StrongComponent) || !IsValid(StrongGraph)
		|| StrongComponent->GetGraph() != StrongGraph)
	{
		OutError = TEXT("The Calysto V6 generation gate lost its component or exact runtime graph.");
		return InvalidPCGTaskId;
	}
	if (StrongComponent->IsGenerating() || StrongComponent->IsCleaningUp() || StrongComponent->bGenerated)
	{
		OutError = TEXT("PCG component is already generated, generating, or cleaning; refusing duplicate GenerateLocal.");
		return InvalidPCGTaskId;
	}

	// Consume before scheduling. A scheduling failure remains consumed and therefore fails closed.
	bConsumed = true;
	const FPCGTaskId TaskId = StrongComponent->GenerateLocalGetTaskId(bForce);
	if (TaskId == InvalidPCGTaskId)
	{
		OutError = TEXT("GenerateLocal did not return a valid task; the one-shot gate remains consumed.");
	}
	return TaskId;
}
