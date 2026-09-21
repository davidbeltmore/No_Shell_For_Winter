#if WITH_DEV_AUTOMATION_TESTS

#include "Calysto/EFCalystoAssignRoomThemeV6.h"
#include "Calysto/EFCalystoDecalPoolV6.h"
#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"
#include "Calysto/EFCalystoPCGCookedCompatibility.h"
#include "Calysto/EFCalystoPCGRuntimeGraphV6.h"
#include "Elements/PCGCreateAttribute.h"
#include "Elements/Metadata/PCGMetadataMathsOpElement.h"

#include "Data/PCGPointData.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Materials/MaterialInstance.h"
#include "Metadata/PCGMetadata.h"
#include "MeshSelectors/PCGMeshSelectorByAttribute.h"
#include "Misc/AutomationTest.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGPoint.h"
#include "PCGSubgraph.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace EFCalystoRoomThemeV6TestsPrivate
{
	static UPCGNode* FindThemeSelectorNode(
		UPCGGraph* Root,
		TSet<const UPCGGraph*>& Visited)
	{
		if (!IsValid(Root) || Visited.Contains(Root))
		{
			return nullptr;
		}
		Visited.Add(Root);
		for (UPCGNode* Node : Root->GetNodes())
		{
			if (IsValid(Node)
				&& Cast<UPCGCalystoAssignRoomThemeSettings>(Node->GetSettings()))
			{
				return Node;
			}
			if (const UPCGSubgraphSettings* Subgraph = IsValid(Node)
				? Cast<UPCGSubgraphSettings>(Node->GetSettings()) : nullptr)
			{
				if (UPCGNode* Result = FindThemeSelectorNode(
					Subgraph->GetSubgraph(), Visited))
				{
					return Result;
				}
			}
		}
		return nullptr;
	}

	static UPCGGraph* FindSetDungeonMeshGraph(
		UPCGGraph* Root,
		TSet<const UPCGGraph*>& Visited)
	{
		if (!IsValid(Root) || Visited.Contains(Root))
		{
			return nullptr;
		}
		Visited.Add(Root);
		if (Root->GetName().Contains(TEXT("PCG_SetDungeonMesh"), ESearchCase::CaseSensitive))
		{
			return Root;
		}
		for (UPCGNode* Node : Root->GetNodes())
		{
			UPCGSubgraphSettings* Subgraph = Node
				? Cast<UPCGSubgraphSettings>(Node->GetSettings())
				: nullptr;
			if (UPCGGraph* Found = FindSetDungeonMeshGraph(
				Subgraph ? Subgraph->GetSubgraph() : nullptr,
				Visited))
			{
				return Found;
			}
		}
		return nullptr;
	}

	static UPCGMeshSelectorByAttribute* FindSurfaceSelector(
		UPCGGraph* Graph,
		const FName ConsumerNodeName)
	{
		for (UPCGNode* Node : Graph ? Graph->GetNodes() : TArray<TObjectPtr<UPCGNode>>())
		{
			const UPCGStaticMeshSpawnerSettings* Spawner = Node && Node->GetFName() == ConsumerNodeName
				? Cast<UPCGStaticMeshSpawnerSettings>(Node->GetSettings())
				: nullptr;
			if (Spawner)
			{
				return Cast<UPCGMeshSelectorByAttribute>(Spawner->MeshSelectorParameters);
			}
		}
		return nullptr;
	}

	static UPCGSubgraphSettings* FindSubgraphReference(
		UPCGGraph* Root,
		const UPCGGraph* Target,
		TSet<const UPCGGraph*>& Visited)
	{
		if (!IsValid(Root) || !IsValid(Target) || Visited.Contains(Root))
		{
			return nullptr;
		}
		Visited.Add(Root);
		for (UPCGNode* Node : Root->GetNodes())
		{
			UPCGSubgraphSettings* Subgraph = Node
				? Cast<UPCGSubgraphSettings>(Node->GetSettings())
				: nullptr;
			UPCGGraph* Child = Subgraph ? Subgraph->GetSubgraph() : nullptr;
			if (Child == Target)
			{
				return Subgraph;
			}
			if (UPCGSubgraphSettings* Found = FindSubgraphReference(Child, Target, Visited))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoRoomIdentityTranslationV6Test,
	"NoShellForWinter.CalystoDungeon.V6.PCG.RoomIdentityIgnoresDungeonWorldTranslation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoRoomIdentityTranslationV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPCGPoint First;
	First.Transform.SetLocation(FVector(1200.0, -300.0, 50.0));
	First.BoundsMin = FVector(-150.0, -200.0, -50.0);
	First.BoundsMax = FVector(150.0, 200.0, 50.0);
	First.Seed = 17;

	const FTransform FirstDungeon(FVector(1000.0, -500.0, 0.0));
	const FVector TranslationDelta(8000.0, 4000.0, 125.0);
	FPCGPoint Second = First;
	Second.Transform.AddToTranslation(TranslationDelta);
	const FTransform SecondDungeon(FirstDungeon.GetLocation() + TranslationDelta);

	const FString FirstId = FEFCalystoRoomThemeDeterminismV6::BuildStableRoomId(
		First, FirstDungeon, 1.0, 99173, TEXT("AllUsedRooms"));
	const FString SecondId = FEFCalystoRoomThemeDeterminismV6::BuildStableRoomId(
		Second, SecondDungeon, 1.0, 99173, TEXT("AllUsedRooms"));
	TestEqual(TEXT("Room identity remains stable when the entire dungeon moves"), FirstId, SecondId);
	TestEqual(TEXT("Room identity is a 128-bit uppercase hexadecimal token"), FirstId.Len(), 32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoThemeProbabilityLanesV6Test,
	"NoShellForWinter.CalystoDungeon.V6.PCG.ProbabilityLanesAreIndependentAndOrderStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoThemeProbabilityLanesV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FString MetadataSchemaError;
	TestTrue(
		*FString::Printf(TEXT("Every V6 PCG metadata name is valid and unique: %s"), *MetadataSchemaError),
		FEFCalystoRoomThemeMetadataV6::ValidateSchema(MetadataSchemaError));
	TestEqual(TEXT("The V6 Room ID uses the canonical UE 5.8-safe schema name"),
		FEFCalystoRoomThemeMetadataV6::RoomId, FName(TEXT("EF_RoomId")));
	TestEqual(TEXT("The V6 Theme ID uses the canonical UE 5.8-safe schema name"),
		FEFCalystoRoomThemeMetadataV6::ThemeId, FName(TEXT("EF_ThemeId")));
	TestEqual(TEXT("The V6 upstream protection mask uses the canonical UE 5.8-safe schema name"),
		FEFCalystoRoomThemeMetadataV6::RoomFlags, FName(TEXT("EF_RoomFlags")));
	UPCGPointData* SchemaData = NewObject<UPCGPointData>(GetTransientPackage());
	UPCGMetadata* SchemaMetadata = SchemaData ? SchemaData->MutableMetadata() : nullptr;
	TestNotNull(TEXT("A transient metadata owner exists for physical schema validation"), SchemaMetadata);
	if (SchemaMetadata)
	{
		TestNotNull(TEXT("Room ID string attribute can be created"),
			SchemaMetadata->CreateAttribute<FString>(FEFCalystoRoomThemeMetadataV6::RoomId, FString(), false, false));
		for (const FName Name : {
			FEFCalystoRoomThemeMetadataV6::ThemeId,
			FEFCalystoRoomThemeMetadataV6::StyleId,
			FEFCalystoRoomThemeMetadataV6::TopologyKind,
			FEFCalystoRoomThemeMetadataV6::CatalogId})
		{
			TestNotNull(*FString::Printf(TEXT("FName attribute '%s' can be created"), *Name.ToString()),
				SchemaMetadata->CreateAttribute<FName>(Name, NAME_None, false, false));
		}
		for (const FName Name : {
			FEFCalystoRoomThemeMetadataV6::RoomFlags,
			FEFCalystoRoomThemeMetadataV6::CollisionOrdinal,
			FEFCalystoRoomThemeMetadataV6::FloorMaterialAuthority,
			FEFCalystoRoomThemeMetadataV6::WallMaterialAuthority,
			FEFCalystoRoomThemeMetadataV6::RoofMaterialAuthority})
		{
			TestNotNull(*FString::Printf(TEXT("int32 attribute '%s' can be created"), *Name.ToString()),
				SchemaMetadata->CreateAttribute<int32>(Name, 0, false, false));
		}
		TestNotNull(TEXT("Catalog color attribute can be created"),
			SchemaMetadata->CreateAttribute<FVector4>(FEFCalystoRoomThemeMetadataV6::CatalogColor, FVector4::Zero(), false, false));
		FPCGMetadataAttribute<FSoftObjectPath>* RoomTypeAttribute = SchemaMetadata->CreateAttribute<FSoftObjectPath>(
			FEFCalystoRoomThemeMetadataV6::VendorRoomType, FSoftObjectPath(), false, false);
		TestNotNull(TEXT("Room Type uses the supported soft-path transport"), RoomTypeAttribute);
		if (RoomTypeAttribute)
		{
			const PCGMetadataEntryKey Key = SchemaMetadata->AddEntry();
			UObject* Object = NewObject<UPCGGraph>(GetTransientPackage());
			RoomTypeAttribute->SetValue(Key, FSoftObjectPath(Object));
			TestTrue(TEXT("Transient Room Type transport resolves without loading an authored asset"),
				RoomTypeAttribute->GetValueFromItemKey(Key).ResolveObject() == Object);
		}
		for (const FName Name : {
			FEFCalystoRoomThemeMetadataV6::EffectiveFloorMaterial,
			FEFCalystoRoomThemeMetadataV6::EffectiveWallMaterial,
			FEFCalystoRoomThemeMetadataV6::EffectiveRoofMaterial,
			FEFCalystoRoomThemeMetadataV6::VendorFloorMaterial,
			FEFCalystoRoomThemeMetadataV6::VendorWallMaterial,
			FEFCalystoRoomThemeMetadataV6::VendorRoofMaterial})
		{
			TestNotNull(*FString::Printf(TEXT("soft path attribute '%s' can be created"), *Name.ToString()),
				SchemaMetadata->CreateAttribute<FSoftObjectPath>(Name, FSoftObjectPath(), false, false));
		}
		for (const FName Name : {
			FEFCalystoRoomThemeMetadataV6::VendorOverrideFloorMaterial,
			FEFCalystoRoomThemeMetadataV6::VendorOverrideWallMaterial,
			FEFCalystoRoomThemeMetadataV6::VendorOverrideRoofMaterial})
		{
			TestNotNull(*FString::Printf(TEXT("bool attribute '%s' can be created"), *Name.ToString()),
				SchemaMetadata->CreateAttribute<bool>(Name, false, false, false));
		}
		TestNotNull(TEXT("Presence diagnostic attribute can be created"),
			SchemaMetadata->CreateAttribute<double>(FEFCalystoRoomThemeMetadataV6::PresenceDraw, 0.0, false, false));
		TestNotNull(TEXT("Theme diagnostic attribute can be created"),
			SchemaMetadata->CreateAttribute<double>(FEFCalystoRoomThemeMetadataV6::ThemeDraw, 0.0, false, false));
	}
	UPCGPointData* TypeCollisionData = NewObject<UPCGPointData>(GetTransientPackage());
	UPCGMetadata* TypeCollisionMetadata = TypeCollisionData ? TypeCollisionData->MutableMetadata() : nullptr;
	if (TypeCollisionMetadata)
	{
		TestNotNull(TEXT("A wrong-type collision fixture can be created"),
			TypeCollisionMetadata->CreateAttribute<int32>(FEFCalystoRoomThemeMetadataV6::RoomId, 0, false, false));
		TestNull(TEXT("A wrong-type Room ID lookup fails without an unsafe cast"),
			TypeCollisionMetadata->GetConstTypedAttribute<FString>(FEFCalystoRoomThemeMetadataV6::RoomId));
	}

	FEFCalystoPCGThemeProfileV6 Forge;
	Forge.ThemeId = TEXT("Forge");
	Forge.SelectionWeight = 4.0;
	FEFCalystoPCGThemeProfileV6 Shrine;
	Shrine.ThemeId = TEXT("Shrine");
	Shrine.SelectionWeight = 1.0;
	TArray<FEFCalystoPCGThemeProfileV6> Original{Forge, Shrine};
	TArray<FEFCalystoPCGThemeProfileV6> Reordered{Shrine, Forge};

	for (int32 RoomOrdinal = 0; RoomOrdinal < 128; ++RoomOrdinal)
	{
		const FString RoomId = FString::Printf(TEXT("ROOM_%03d"), RoomOrdinal);
		const bool bPresentBefore = FEFCalystoRoomThemeDeterminismV6::IsThemePresent(4401, RoomId, 0.25);
		Original[0].SelectionWeight = 100.0 + RoomOrdinal;
		Original[1].SelectionWeight = 0.25;
		const bool bPresentAfter = FEFCalystoRoomThemeDeterminismV6::IsThemePresent(4401, RoomId, 0.25);
		TestEqual(TEXT("Changing conditional Theme weights cannot change the presence subset"), bPresentBefore, bPresentAfter);

		const int32 OriginalIndex = FEFCalystoRoomThemeDeterminismV6::SelectThemeIndex(4401, RoomId, Original);
		Reordered[0].SelectionWeight = Original[1].SelectionWeight;
		Reordered[1].SelectionWeight = Original[0].SelectionWeight;
		const int32 ReorderedIndex = FEFCalystoRoomThemeDeterminismV6::SelectThemeIndex(4401, RoomId, Reordered);
		TestTrue(TEXT("Both weighted selections resolve"), Original.IsValidIndex(OriginalIndex) && Reordered.IsValidIndex(ReorderedIndex));
		if (Original.IsValidIndex(OriginalIndex) && Reordered.IsValidIndex(ReorderedIndex))
		{
			TestEqual(TEXT("Authored array order cannot change the selected Theme ID"), Original[OriginalIndex].ThemeId, Reordered[ReorderedIndex].ThemeId);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoProtectedRoomResolutionV6Test,
	"NoShellForWinter.CalystoDungeon.V6.PCG.ProtectedRoomResolutionFailsClosedOnExactTie",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoProtectedRoomResolutionV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<FBox> DistinctBounds{
		FBox(FVector(-100.0, -100.0, -10.0), FVector(100.0, 100.0, 10.0)),
		FBox(FVector(300.0, -100.0, -10.0), FVector(500.0, 100.0, 10.0))};
	const TArray<FString> DistinctIds{TEXT("A"), TEXT("B")};
	TestEqual(
		TEXT("Contained marker resolves one protected room"),
		FEFCalystoRoomThemeDeterminismV6::ResolveProtectedRoomIndex(FVector::ZeroVector, DistinctBounds, DistinctIds, 0.0),
		0);
	const int32 ProgressionMarkerIndex =
		FEFCalystoRoomThemeDeterminismV6::ResolveProtectedRoomIndex(
			FVector(400.0, 0.0, 0.0), DistinctBounds, DistinctIds, 0.0);
	TestEqual(TEXT("A progression marker resolves its independent protected room"), ProgressionMarkerIndex, 1);
	TestEqual(
		TEXT("Uncontained marker fails closed"),
		FEFCalystoRoomThemeDeterminismV6::ResolveProtectedRoomIndex(FVector(200.0, 0.0, 0.0), DistinctBounds, DistinctIds, 0.0),
		INDEX_NONE);

	const TArray<FBox> AmbiguousBounds{
		FBox(FVector(-100.0), FVector(100.0)),
		FBox(FVector(-100.0), FVector(100.0))};
	TestEqual(
		TEXT("Two different rooms at the exact same distance fail closed"),
		FEFCalystoRoomThemeDeterminismV6::ResolveProtectedRoomIndex(FVector::ZeroVector, AmbiguousBounds, DistinctIds, 0.0),
		INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoDoorClearanceGeometryV6Test,
	"NoShellForWinter.CalystoDungeon.V6.PCG.DoorClearanceGeometryIsDeterministicAndFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoDoorClearanceGeometryV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<FBox> Bounds{
		FBox(FVector(-100.0, -100.0, -50.0), FVector(100.0, 100.0, 50.0)),
		FBox(FVector(150.0, -100.0, -50.0), FVector(350.0, 100.0, 50.0)),
		FBox(FVector(500.0, -100.0, -50.0), FVector(700.0, 100.0, 50.0))};
	const TArray<FString> Ids{TEXT("Start"), TEXT("Adjacent"), TEXT("Remote")};
	TArray<int32> Protected;
	FString Error;
	TestTrue(
		TEXT("A valid connector compiles one deterministic Euclidean AABB halo"),
		FEFCalystoRoomThemeDeterminismV6::ResolveDoorClearanceRoomIndexes(
			{FVector::ZeroVector}, Bounds, Ids, 2.0, 200.0, Protected, Error));
	TestEqual(TEXT("The anchor and one nearby room are protected"), Protected.Num(), 2);
	if (Protected.Num() == 2)
	{
		TestEqual(TEXT("The anchor room is first in canonical index order"), Protected[0], 0);
		TestEqual(TEXT("The adjacent room is inside the 200 cm halo"), Protected[1], 1);
	}

	const TArray<FBox> ReorderedBounds{Bounds[2], Bounds[0], Bounds[1]};
	const TArray<FString> ReorderedIds{Ids[2], Ids[0], Ids[1]};
	TArray<int32> ReorderedProtected;
	TestTrue(
		TEXT("Input reordering preserves the protected room identities"),
		FEFCalystoRoomThemeDeterminismV6::ResolveDoorClearanceRoomIndexes(
			{FVector::ZeroVector}, ReorderedBounds, ReorderedIds, 2.0, 200.0, ReorderedProtected, Error));
	TSet<FString> FirstProtectedIds;
	for (const int32 Index : Protected)
	{
		FirstProtectedIds.Add(Ids[Index]);
	}
	TSet<FString> SecondProtectedIds;
	for (const int32 Index : ReorderedProtected)
	{
		SecondProtectedIds.Add(ReorderedIds[Index]);
	}
	bool bSameProtectedIds = FirstProtectedIds.Num() == SecondProtectedIds.Num();
	for (const FString& Id : FirstProtectedIds)
	{
		bSameProtectedIds &= SecondProtectedIds.Contains(Id);
	}
	TestTrue(TEXT("Room identity set is invariant to point/index order"), bSameProtectedIds);

	Protected = {0};
	TestFalse(
		TEXT("A connector outside every room fails closed"),
		FEFCalystoRoomThemeDeterminismV6::ResolveDoorClearanceRoomIndexes(
			{FVector(2000.0, 0.0, 0.0)}, Bounds, Ids, 2.0, 200.0, Protected, Error));
	TestTrue(TEXT("Fail-closed resolution emits no stale indexes"), Protected.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoVendorMainSideTopologySignatureV6Test,
	"NoShellForWinter.CalystoDungeon.V6.PCG.VendorMainSideTopologySignature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoVendorMainSideTopologySignatureV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestNotNull(TEXT("The neutral native Room Type schema is resident before composition"),
		LoadObject<UObject>(nullptr, TEXT("/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes.Default__PDA_RoomMeshes_C")));
	UPCGGraph* RootGraph = LoadObject<UPCGGraph>(nullptr,
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.PCG_MassiveDungeonMaster"));
	UPCGGraph* ShapeGraph = LoadObject<UPCGGraph>(nullptr,
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonShape.PCG_MassiveDungeonShape"));
	TestNotNull(TEXT("The exact protected Master graph loads"), RootGraph);
	TestNotNull(TEXT("The exact protected Shape graph loads"), ShapeGraph);
	if (!RootGraph || !ShapeGraph)
	{
		return false;
	}
	UPackage* RootPackage = RootGraph->GetOutermost();
	UPackage* ShapePackage = ShapeGraph->GetOutermost();
	TestFalse(TEXT("Protected Master starts clean"), RootPackage->IsDirty());
	TestFalse(TEXT("Protected Shape starts clean"), ShapePackage->IsDirty());

	FEFCalystoRoomThemeGenerationConfigV6 Config;
	Config.GenerationSeed = 778899;
	Config.ThemePresenceChance = 0.0;
	Config.Style.StyleId = TEXT("SignatureTest");
	Config.Style.FloorMaterial = TSoftObjectPtr<UMaterialInstance>(
		FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Material/MI_GreenTile.MI_GreenTile")));
	Config.Style.WallMaterial = Config.Style.FloorMaterial;
	Config.Style.RoofMaterial = Config.Style.FloorMaterial;
	Config.DoorClearanceRadiusCm = 200.0;
	FString MaterialValidationError;
	FEFCalystoRoomThemeGenerationConfigV6 ThemeMaterialConfig = Config;
	ThemeMaterialConfig.ThemePresenceChance = 0.25;
	FEFCalystoPCGThemeProfileV6& ThemeMaterial =
		ThemeMaterialConfig.Themes.AddDefaulted_GetRef();
	ThemeMaterial.ThemeId = TEXT("MaterialContractTheme");
	ThemeMaterial.RoomType = NewObject<UPCGGraph>(GetTransientPackage());
	ThemeMaterial.ArchitectureHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(TEXT("Fixture Architecture"));
	ThemeMaterial.FloorMaterial = Config.Style.FloorMaterial;
	const bool bValidThemeOverride = ThemeMaterialConfig.Validate(MaterialValidationError);
	TestTrue(
		*FString::Printf(TEXT("Valid Style materials and an optional Theme override pass validation: %s"), *MaterialValidationError),
		bValidThemeOverride);
	ThemeMaterial.FloorMaterial = TSoftObjectPtr<UMaterialInstance>();
	const bool bValidThemeInherit = ThemeMaterialConfig.Validate(MaterialValidationError);
	TestTrue(
		*FString::Printf(TEXT("A null Theme material remains the explicit Inherit representation: %s"), *MaterialValidationError),
		bValidThemeInherit);
	FEFCalystoRoomThemeGenerationConfigV6 MissingStyleMaterialConfig = Config;
	MissingStyleMaterialConfig.Style.FloorMaterial = TSoftObjectPtr<UMaterialInstance>();
	const bool bMissingStyleMaterialAccepted = MissingStyleMaterialConfig.Validate(MaterialValidationError);
	TestFalse(
		TEXT("A null required Style material fails configuration validation"),
		bMissingStyleMaterialAccepted);
	const FEFCalystoPCGRuntimeGraphResultV6 Result =
		FEFCalystoPCGRuntimeGraphBuilderV6::TryBuild(RootGraph, GetTransientPackage(), Config);
	TestTrue(*FString::Printf(TEXT("Transient V6 graph accepts the exact vendor signature: %s"), *Result.FailureReason), Result.bBuilt);
	TestNotNull(TEXT("Transient V6 graph exists"), Result.RuntimeGraph);
	FString ValidationError;
	const bool bRuntimeGraphValid = Result.RuntimeGraph
		&& FEFCalystoPCGRuntimeGraphBuilderV6::ValidateRuntimeGraph(Result.RuntimeGraph, ValidationError);
	TestTrue(
		*FString::Printf(TEXT("Runtime graph freezes connected Main/Side/Start/End pins: %s"), *ValidationError),
		bRuntimeGraphValid);
	TSet<const UPCGGraph*> SelectorVisited;
	UPCGNode* SelectorNode = Result.RuntimeGraph
		? EFCalystoRoomThemeV6TestsPrivate::FindThemeSelectorNode(
			Result.RuntimeGraph, SelectorVisited)
		: nullptr;
	TestNotNull(TEXT("The transient graph contains exactly the V6 selector interface"), SelectorNode);
	if (SelectorNode)
	{
		UPCGPin* Unthemed = SelectorNode->GetOutputPin(FEFCalystoRoomThemePinsV6::UnthemedRooms);
		TestTrue(TEXT("NoTheme and protected rooms remain connected to native construction"),
			Unthemed && Unthemed->Edges.Num() == 1);
		if (Unthemed && Unthemed->Edges.Num() == 1 && Unthemed->Edges[0])
		{
			UPCGPin* NeutralInput = Unthemed->Edges[0]->OutputPin;
			UPCGNode* NeutralNode = NeutralInput ? NeutralInput->Node.Get() : nullptr;
			UPCGAddAttributeSettings* NeutralSettings = NeutralNode
				? Cast<UPCGAddAttributeSettings>(NeutralNode->GetSettings()) : nullptr;
			TestNotNull(TEXT("Unthemed rooms receive the supported native metadata adapter"), NeutralSettings);
			if (NeutralSettings)
			{
				TestEqual(TEXT("Neutral transport assigns RoomType, not a gameplay Theme"),
					NeutralSettings->OutputTarget.GetAttributeName(), FEFCalystoRoomThemeMetadataV6::VendorRoomType);
				TestTrue(TEXT("Neutral Room Type soft path resolves to the resident native CDO"),
					NeutralSettings->AttributeTypes.Type == EPCGMetadataTypes::SoftObjectPath
					&& NeutralSettings->AttributeTypes.SoftObjectPathValue.ResolveObject()
					&& NeutralSettings->AttributeTypes.SoftObjectPathValue.ResolveObject()->HasAnyFlags(RF_ClassDefaultObject));
				const FSoftObjectPath SavedRoomType = NeutralSettings->AttributeTypes.SoftObjectPathValue;
				NeutralSettings->AttributeTypes.SoftObjectPathValue.Reset();
				TestFalse(TEXT("A missing neutral RoomType cannot pass the runtime graph contract"),
					FEFCalystoPCGRuntimeGraphBuilderV6::ValidateRuntimeGraph(Result.RuntimeGraph, ValidationError));
				NeutralSettings->AttributeTypes.SoftObjectPathValue = SavedRoomType;
			}
			if (NeutralInput && NeutralNode)
			{
				TestTrue(TEXT("Fault fixture disconnects the protected-room stream"), Unthemed->BreakEdgeTo(NeutralInput));
				TestFalse(TEXT("The historical themed-only structural stream is rejected"),
					FEFCalystoPCGRuntimeGraphBuilderV6::ValidateRuntimeGraph(Result.RuntimeGraph, ValidationError));
				CastChecked<UPCGGraph>(SelectorNode->GetOuter())->AddEdge(SelectorNode,
					FEFCalystoRoomThemePinsV6::UnthemedRooms, NeutralNode, PCGPinConstants::DefaultInputLabel);
				TestTrue(TEXT("Restoring the complete native stream restores structural capability validation"),
					FEFCalystoPCGRuntimeGraphBuilderV6::ValidateRuntimeGraph(Result.RuntimeGraph, ValidationError));
			}
		}
		const UPCGPin* CriticalPin = SelectorNode->GetInputPin(
			FEFCalystoRoomThemePinsV6::CriticalRooms);
		const UPCGPin* ProgressionPin = SelectorNode->GetInputPin(
			FEFCalystoRoomThemePinsV6::ProgressionRooms);
		TestNotNull(TEXT("The optional Critical Rooms marker pin exists"), CriticalPin);
		TestNotNull(TEXT("The optional Progression Rooms marker pin exists"), ProgressionPin);
		TestTrue(TEXT("The current frozen vendor signature has no Critical Rooms producer"),
			CriticalPin && !CriticalPin->IsConnected());
		TestTrue(TEXT("The current frozen vendor signature has no Progression Rooms producer"),
			ProgressionPin && !ProgressionPin->IsConnected());
	}
	TestEqual(TEXT("Exactly one native Theme stage is replaced"), Result.ReplacedNativeThemeNodeCount, 1);
	TestFalse(TEXT("Protected Master remains clean"), RootPackage->IsDirty());
	TestFalse(TEXT("Protected Shape remains clean"), ShapePackage->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoCookedResidentClosureSignatureV6Test,
	"NoShellForWinter.CalystoDungeon.V6.PCG.CookedResidentClosureSignature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoCookedResidentClosureSignatureV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestNotNull(TEXT("Cooked fixture preloads the neutral native Room Type schema"),
		LoadObject<UObject>(nullptr, TEXT("/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes.Default__PDA_RoomMeshes_C")));
	UPCGGraph* RootGraph = LoadObject<UPCGGraph>(nullptr,
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.PCG_MassiveDungeonMaster"));
	UPCGGraph* ShapeGraph = LoadObject<UPCGGraph>(nullptr,
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonShape.PCG_MassiveDungeonShape"));
	UPCGGraph* LegacyHelper = LoadObject<UPCGGraph>(nullptr,
		TEXT("/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple.PCG_ObjectTransformSimple"));
	UPCGGraph* CookedHelper = LoadObject<UPCGGraph>(nullptr,
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon.PCG_ObjectTransformSimpleDungeon"));
	UPCGGraph* InternalSetDungeonMesh = LoadObject<UPCGGraph>(nullptr,
		TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_SetDungeonMeshCookedSafe.PCG_SetDungeonMeshCookedSafe"));
	UPCGGraph* InternalAddRamps = LoadObject<UPCGGraph>(nullptr,
		TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_AddRampsCookedSafe.PCG_AddRampsCookedSafe"));
	TestNotNull(TEXT("Master is resident before runtime composition"), RootGraph);
	TestNotNull(TEXT("Shape is resident before runtime composition"), ShapeGraph);
	TestNotNull(TEXT("Legacy helper is resident before runtime composition"), LegacyHelper);
	TestNotNull(TEXT("Cooked-safe helper is resident before runtime composition"), CookedHelper);
	TestNotNull(TEXT("Project-owned cooked-safe SetDungeonMesh is resident"), InternalSetDungeonMesh);
	TestNotNull(TEXT("Project-owned cooked-safe AddRamps is resident"), InternalAddRamps);
	if (!RootGraph || !ShapeGraph || !LegacyHelper || !CookedHelper
		|| !InternalSetDungeonMesh || !InternalAddRamps)
	{
		return false;
	}
	FString InternalClosureError;
	const bool bInternalClosureValid =
		FEFCalystoPCGCookedCompatibility::ValidateResidentInternalClosure(InternalClosureError);
	TestTrue(
		*FString::Printf(TEXT("Resident internal closure is structurally exact: %s"), *InternalClosureError),
		bInternalClosureValid);
	const FEFCalystoPCGCookedCompatibilityResult Compatibility =
		FEFCalystoPCGCookedCompatibility::TryBuild(
			RootGraph, GetTransientPackage(), true);
	TestTrue(
		*FString::Printf(TEXT("Cooked compatibility accepts the internal closure: %s"), *Compatibility.FailureReason),
		Compatibility.bApplied);
	TestEqual(TEXT("Cooked compatibility clones only Master"),
		Compatibility.ClonedGraphCount, 1);
	TestEqual(TEXT("Cooked compatibility relinks exactly Master's closure call"),
		Compatibility.RelinkedInternalClosureCount, 1);
	TestEqual(TEXT("Cooked compatibility validates both internal graphs"),
		Compatibility.ValidatedInternalGraphCount, 2);

	FEFCalystoRoomThemeGenerationConfigV6 Config;
	Config.GenerationSeed = 778899;
	Config.ThemePresenceChance = 0.0;
	Config.Style.StyleId = TEXT("CookedSignatureTest");
	Config.Style.FloorMaterial = TSoftObjectPtr<UMaterialInstance>(
		FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Material/MI_GreenTile.MI_GreenTile")));
	Config.Style.WallMaterial = Config.Style.FloorMaterial;
	Config.Style.RoofMaterial = Config.Style.FloorMaterial;
	Config.DoorClearanceRadiusCm = 200.0;
	const FEFCalystoPCGRuntimeGraphResultV6 Result =
		FEFCalystoPCGRuntimeGraphBuilderV6::TryBuild(
			RootGraph, GetTransientPackage(), Config, true);
	TestTrue(
		*FString::Printf(TEXT("Forced cooked composition uses only resident assets: %s"), *Result.FailureReason),
		Result.bBuilt);
	TestEqual(TEXT("Forced-cooked V6 owns transient Master, Shape and endpoint selection"),
		Result.ClonedGraphCount, 3);
	TestEqual(TEXT("Every transient graph invalidates its copied cooked task cache"),
		Result.InvalidatedCookedCompilationDataCount, 3);
	TSet<const UPCGGraph*> EndpointVisited;
	UPCGNode* ThemeSelector = EFCalystoRoomThemeV6TestsPrivate::FindThemeSelectorNode(Result.RuntimeGraph, EndpointVisited);
	UPCGGraph* Shape = ThemeSelector ? ThemeSelector->GetTypedOuter<UPCGGraph>() : nullptr;
	int32 EndpointPoolNodes = 0;
	for (UPCGNode* Call : Shape ? Shape->GetNodes() : TArray<TObjectPtr<UPCGNode>>())
	{
		const UPCGSubgraphSettings* Subgraph = Call ? Cast<UPCGSubgraphSettings>(Call->GetSettings()) : nullptr;
		UPCGGraph* Endpoint = Subgraph ? Subgraph->GetSubgraph() : nullptr;
		if (!Endpoint || !Endpoint->GetName().StartsWith(TEXT("EFCalystoV6_EndpointPool"))) continue;
		TestTrue(TEXT("Endpoint pool is transient and owned by Shape"), Endpoint->HasAnyFlags(RF_Transient) && Endpoint->IsIn(Shape));
		for (UPCGNode* Node : Endpoint->GetNodes())
		{
			const UPCGMetadataMathsSettings* Maths = Cast<UPCGMetadataMathsSettings>(Node->GetSettings());
			if (!Maths || !Maths->GetName().StartsWith(TEXT("EFCalystoEndpointPoolMaximum"))) continue;
			++EndpointPoolNodes;
			TestTrue(TEXT("Endpoint pool clamps the lower bound with Max"), Maths->Operation == EPCGMetadataMathsOperation::Max);
			const UPCGPin* MinimumInput = Node->GetInputPin(TEXT("InB"));
			const UPCGNode* MinimumNode = MinimumInput && MinimumInput->Edges.Num() == 1
				? MinimumInput->Edges[0]->GetInputNode() : nullptr;
			const UPCGCreateAttributeSetSettings* Minimum = MinimumNode
				? Cast<UPCGCreateAttributeSetSettings>(MinimumNode->GetSettings()) : nullptr;
			TestTrue(TEXT("Minimum candidate count is exactly one"), Minimum
				&& Minimum->AttributeTypes.Type == EPCGMetadataTypes::Double && Minimum->AttributeTypes.DoubleValue == 1.0);
			TestEqual(TEXT("Both native endpoint filters receive the corrected threshold"),
				Node->GetOutputPin(PCGPinConstants::DefaultOutputLabel)->Edges.Num(), 2);
		}
	}
	TestEqual(TEXT("Exactly one bounded endpoint pool was composed"), EndpointPoolNodes, 1);
	TestEqual(TEXT("Exactly one native Theme stage is replaced in cooked composition"),
		Result.ReplacedNativeThemeNodeCount, 1);
	if (!Result.bBuilt || !Result.RuntimeGraph)
	{
		return false;
	}

	FString SurfaceContractError;
	TestTrue(
		*FString::Printf(TEXT("The complete 3x3 surface consumer contract is valid: %s"), *SurfaceContractError),
		FEFCalystoPCGRuntimeGraphBuilderV6::ValidateRuntimeGraph(Result.RuntimeGraph, SurfaceContractError));

	TSet<const UPCGGraph*> VisitedGraphs;
	UPCGGraph* SetDungeonMeshGraph = EFCalystoRoomThemeV6TestsPrivate::FindSetDungeonMeshGraph(
		Result.RuntimeGraph,
		VisitedGraphs);
	TestNotNull(TEXT("Forced cooked composition owns a reachable SetDungeonMesh graph"), SetDungeonMeshGraph);
	if (!SetDungeonMeshGraph)
	{
		return false;
	}
	TSet<const UPCGGraph*> ReferenceVisited;
	UPCGSubgraphSettings* SurfaceGraphReference =
		EFCalystoRoomThemeV6TestsPrivate::FindSubgraphReference(
			Result.RuntimeGraph,
			SetDungeonMeshGraph,
			ReferenceVisited);
	TestNotNull(TEXT("The transient Master owns the exact SetDungeonMesh reference"),
		SurfaceGraphReference);
	if (!SurfaceGraphReference)
	{
		return false;
	}
	TestTrue(TEXT("Only transient V6 state may be rewired by the regression fixture"),
		SurfaceGraphReference->IsIn(Result.RuntimeGraph));

	UPCGGraph* SurfaceRegressionGraph = DuplicateObject<UPCGGraph>(
		SetDungeonMeshGraph,
		GetTransientPackage(),
		TEXT("EFCalysto_PCG_SetDungeonMesh_SurfaceRegression"));
	TestNotNull(TEXT("A transient SetDungeonMesh regression fixture is created"),
		SurfaceRegressionGraph);
	if (!SurfaceRegressionGraph)
	{
		return false;
	}
	SurfaceRegressionGraph->ClearFlags(RF_Public | RF_Standalone);
	SurfaceRegressionGraph->SetFlags(RF_Transient);

	UPCGMeshSelectorByAttribute* RoofSelector =
		EFCalystoRoomThemeV6TestsPrivate::FindSurfaceSelector(
			SurfaceRegressionGraph,
			TEXT("StaticMeshSpawner_9"));
	TestNotNull(TEXT("The exact primary Roof consumer exists"), RoofSelector);
	if (!RoofSelector)
	{
		return false;
	}
	const TArray<FName> OriginalRoofAttributes = RoofSelector->MaterialOverrideAttributes;
	TestEqual(TEXT("The primary Roof consumer has one material attribute"),
		OriginalRoofAttributes.Num(), 1);
	if (OriginalRoofAttributes.Num() == 1)
	{
		TestEqual(TEXT("The primary Roof consumer reads RoofMaterial"),
			OriginalRoofAttributes[0], FName(TEXT("RoofMaterial")));
	}

	// Reproduce the historical vendor defect on the transient fixture. Runtime
	// validation must reject FloorMaterial on a Roof consumer, then accept the
	// graph again after the exact contract is restored.
	RoofSelector->MaterialOverrideAttributes = { TEXT("FloorMaterial") };
	SurfaceGraphReference->SetSubgraph(SurfaceRegressionGraph);
	FString HistoricalDefectError;
	TestFalse(
		TEXT("Historical Floor-to-Roof cross-wiring fails closed"),
		FEFCalystoPCGRuntimeGraphBuilderV6::ValidateRuntimeGraph(
			Result.RuntimeGraph,
			HistoricalDefectError));
	TestTrue(
		*FString::Printf(TEXT("Cross-wiring reports the exact consumer and attributes: %s"), *HistoricalDefectError),
		HistoricalDefectError.Contains(TEXT("StaticMeshSpawner_9"))
			&& HistoricalDefectError.Contains(TEXT("RoofMaterial"))
			&& HistoricalDefectError.Contains(TEXT("FloorMaterial")));

	SurfaceGraphReference->SetSubgraph(SetDungeonMeshGraph);
	RoofSelector->MaterialOverrideAttributes = OriginalRoofAttributes;
	FString RestoredContractError;
	TestTrue(
		*FString::Printf(TEXT("Restoring RoofMaterial restores the complete graph contract: %s"), *RestoredContractError),
		FEFCalystoPCGRuntimeGraphBuilderV6::ValidateRuntimeGraph(
			Result.RuntimeGraph,
			RestoredContractError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoDecalPlacementProtectionV6Test,
	"NoShellForWinter.CalystoDungeon.V6.PCG.DecalPlacementProtectsEveryReservedRoomRole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoDecalPlacementProtectionV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	EEFCalystoPCGRoomFlagsV6 UpstreamProtectionFlags =
		EEFCalystoPCGRoomFlagsV6::None;
	FString ProtectionError;
	TestTrue(TEXT("An upstream Progression room flag is preserved"),
		FEFCalystoRoomThemeDeterminismV6::ResolveUpstreamProtectionFlags(
			static_cast<int32>(EEFCalystoPCGRoomFlagsV6::Progression),
			UpstreamProtectionFlags,
			ProtectionError));
	TestTrue(TEXT("The preserved upstream flag remains Progression"),
		EnumHasAnyFlags(
			UpstreamProtectionFlags,
			EEFCalystoPCGRoomFlagsV6::Progression));
	ProtectionError.Reset();
	TestFalse(TEXT("Topology-owned MainPath cannot be injected through metadata"),
		FEFCalystoRoomThemeDeterminismV6::ResolveUpstreamProtectionFlags(
			static_cast<int32>(EEFCalystoPCGRoomFlagsV6::MainPath),
			UpstreamProtectionFlags,
			ProtectionError));
	for (const EEFCalystoRoomFlagsV6 Flag : {
		EEFCalystoRoomFlagsV6::Start,
		EEFCalystoRoomFlagsV6::End,
		EEFCalystoRoomFlagsV6::Critical,
		EEFCalystoRoomFlagsV6::Progression,
		EEFCalystoRoomFlagsV6::MainPath,
		EEFCalystoRoomFlagsV6::DoorClearance})
	{
		TestTrue(
			TEXT("Every reserved topology role blocks decal placement"),
			FEFCalystoDecalPlacementMathV6::IsPlacementProtectedRoom(static_cast<int32>(Flag)));
	}
	TestFalse(
		TEXT("An ordinary room remains decal eligible"),
		FEFCalystoDecalPlacementMathV6::IsPlacementProtectedRoom(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoDecalPlacementManifestV6Test,
	"NoShellForWinter.CalystoDungeon.V6.PCG.DecalPlacementIsDeterministicAndThemeScoped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoDecalPlacementManifestV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEFCalystoResolvedFloorPlanV6 Plan;
	Plan.FloorSeed = 77123;
	Plan.StyleId = TEXT("Standard");
	Plan.FloorPlanHash = FString::ChrN(64, TCHAR('A'));
	Plan.DecalComponentPoolCapacity = 24;
	Plan.StyleDecalHash = FString::ChrN(64, TCHAR('B'));
	Plan.StyleDecals.Mode = EEFCalystoDecalResolutionModeV6::Replace;
	Plan.StyleDecals.ChancePerEligibleRoom = 0.10f;
	Plan.StyleDecals.MaximumPerRoom = 1;
	Plan.StyleDecals.MaximumActivePerFloor = 8;
	Plan.StyleDecals.FloorLimit = 3;
	Plan.StyleDecals.WallLimit = 4;
	Plan.StyleDecals.RoofLimit = 1;
	Plan.StyleDecals.MinimumSizeCm = 35.0f;
	Plan.StyleDecals.MaximumSizeCm = 110.0f;
	Plan.StyleDecals.FadeStartDistanceCm = 2500.0f;
	Plan.StyleDecals.CullDistanceCm = 4000.0f;
	for (const EEFCalystoDecalSurfaceV6 Surface : {
		EEFCalystoDecalSurfaceV6::Floor,
		EEFCalystoDecalSurfaceV6::Wall,
		EEFCalystoDecalSurfaceV6::Roof})
	{
		FEFCalystoDecalVariantV6& Variant = Plan.StyleDecals.Catalog.AddDefaulted_GetRef();
		Variant.StableId = FName(*FString::Printf(TEXT("Variant.%d"), static_cast<int32>(Surface)));
		Variant.Material = TSoftObjectPtr<UMaterialInterface>(
			FSoftObjectPath(TEXT("/EFProcedural/Test/MI_Decal.MI_Decal")));
		Variant.AllowedSurfaces = static_cast<int32>(Surface);
	}

	FEFCalystoResolvedThemeProfileV6 Forge;
	Forge.ThemeId = TEXT("Forge");
	Forge.Decals = Plan.StyleDecals;
	Forge.Decals.ChancePerEligibleRoom = 0.25f;
	Forge.DecalHash = FString::ChrN(64, TCHAR('C'));
	Plan.Themes.Add(Forge);
	FEFCalystoResolvedThemeProfileV6 Shrine;
	Shrine.ThemeId = TEXT("Shrine");
	Shrine.Decals.Mode = EEFCalystoDecalResolutionModeV6::Block;
	Shrine.DecalHash = FString::ChrN(64, TCHAR('D'));
	Plan.Themes.Add(Shrine);

	auto FindPassingId = [&Plan](const FName ThemeId, const double Chance)
	{
		for (int64 Id = 1; Id < 100000; ++Id)
		{
			if (FEFCalystoDecalPlacementMathV6::UniformLane(
				Plan.FloorSeed, Id, Plan.StyleId, ThemeId, TEXT("Presence")) < Chance)
			{
				return Id;
			}
		}
		return int64(0);
	};

	FEFCalystoRoomManifestV6 Manifest;
	Manifest.FloorSeed = Plan.FloorSeed;
	Manifest.StyleId = Plan.StyleId;
	Manifest.FloorPlanHash = Plan.FloorPlanHash;
	Manifest.ManifestHash = FString::ChrN(64, TCHAR('E'));
	auto AddRoom = [&Manifest, &Plan](const int64 Id, const FName ThemeId, const bool bThemed,
		const FString& DecalHash, const int32 Flags)
	{
		FEFCalystoRoomContextV6& Room = Manifest.Rooms.AddDefaulted_GetRef();
		Room.StableRoomId = Id;
		Room.LocalCenter = FVector(static_cast<double>(Manifest.Rooms.Num()) * 500.0, 0.0, 150.0);
		Room.Extents = FVector(200.0, 200.0, 150.0);
		Room.StyleId = Plan.StyleId;
		Room.ThemeId = ThemeId;
		Room.bIsThemed = bThemed;
		Room.DecalHash = DecalHash;
		Room.RoomFlags = Flags;
	};
	const int64 StandardId = FindPassingId(TEXT("NoTheme"), 0.10);
	int64 ForgeId = FindPassingId(TEXT("Forge"), 0.25);
	if (ForgeId == StandardId)
	{
		do
		{
			++ForgeId;
		}
		while (FEFCalystoDecalPlacementMathV6::UniformLane(
			Plan.FloorSeed, ForgeId, Plan.StyleId, TEXT("Forge"), TEXT("Presence")) >= 0.25);
	}
	AddRoom(StandardId, TEXT("NoTheme"), false, Plan.StyleDecalHash, 0);
	AddRoom(ForgeId, TEXT("Forge"), true, Forge.DecalHash, 0);
	// Vendor room-context points are commonly planar. Zero Z extent must not
	// reject deterministic decal candidate compilation or the whole floor.
	Manifest.Rooms[0].Extents.Z = 0.0;
	Manifest.Rooms[1].Extents.Z = 0.0;
	AddRoom(900001, TEXT("Shrine"), true, Shrine.DecalHash, 0);
	AddRoom(900002, TEXT("NoTheme"), false, Plan.StyleDecalHash,
		static_cast<int32>(EEFCalystoRoomFlagsV6::DoorClearance));

	TArray<FEFCalystoDecalPlacementCandidateV6> First;
	TArray<FEFCalystoDecalPlacementCandidateV6> Second;
	FString Error;
	TestTrue(TEXT("First frozen placement compile succeeds"),
		FEFCalystoDecalPlacementMathV6::BuildCandidates(Plan, Manifest, First, Error));
	TestTrue(TEXT("Repeated frozen placement compile succeeds"),
		FEFCalystoDecalPlacementMathV6::BuildCandidates(Plan, Manifest, Second, Error));
	TestEqual(TEXT("Repeated compile has identical cardinality"), First.Num(), Second.Num());
	for (int32 Index = 0; Index < FMath::Min(First.Num(), Second.Num()); ++Index)
	{
		TestEqual(TEXT("Stable decal IDs are byte-stable"), First[Index].StableDecalId, Second[Index].StableDecalId);
		TestEqual(TEXT("Selected surfaces are stable"),
			static_cast<uint8>(First[Index].Surface), static_cast<uint8>(Second[Index].Surface));
		TestNotEqual(TEXT("Shrine Block never emits a candidate"), First[Index].StableRoomId, int64(900001));
		TestNotEqual(TEXT("Door clearance never emits a candidate"), First[Index].StableRoomId, int64(900002));
	}
	TestTrue(TEXT("Style and Forge eligible rooms both resolve candidates"), First.Num() == 2);
	TestEqual(
		TEXT("A probability lane is repeatable"),
		FEFCalystoDecalPlacementMathV6::UniformLane(Plan.FloorSeed, StandardId, Plan.StyleId, TEXT("NoTheme"), TEXT("Presence")),
		FEFCalystoDecalPlacementMathV6::UniformLane(Plan.FloorSeed, StandardId, Plan.StyleId, TEXT("NoTheme"), TEXT("Presence")));
	TestNotEqual(
		TEXT("Presence and transform use independent hash lanes"),
		FEFCalystoDecalPlacementMathV6::UniformLane(Plan.FloorSeed, StandardId, Plan.StyleId, TEXT("NoTheme"), TEXT("Presence")),
		FEFCalystoDecalPlacementMathV6::UniformLane(Plan.FloorSeed, StandardId, Plan.StyleId, TEXT("NoTheme"), TEXT("TransformA")));
	return true;
}

#endif
