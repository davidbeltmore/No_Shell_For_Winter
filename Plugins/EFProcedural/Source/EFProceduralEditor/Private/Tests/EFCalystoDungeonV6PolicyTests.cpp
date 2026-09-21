#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/IsSorted.h"
#include "Algo/Reverse.h"
#include "Engine/DataTable.h"
#include "Materials/MaterialInstance.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace EFCalystoV6PolicyTests
{
UEFCalystoDungeonDirectorPolicyV6Asset* MakePolicy()
{
	UEFCalystoDungeonDirectorPolicyV6Asset* Policy =
		NewObject<UEFCalystoDungeonDirectorPolicyV6Asset>(
			GetTransientPackage(), NAME_None, RF_Transient);
	if (Policy) Policy->InitializeV6Defaults();
	return Policy;
}

bool IsSha256(const FString& Value)
{
	if (Value.Len() != 64) return false;
	for (const TCHAR Character : Value)
	{
		if (!FChar::IsHexDigit(Character)) return false;
	}
	return true;
}

bool HasPath(const TArray<FSoftObjectPath>& Paths, const TCHAR* Fragment)
{
	return Paths.ContainsByPredicate([Fragment](const FSoftObjectPath& Path)
	{
		return Path.ToString().Contains(Fragment, ESearchCase::IgnoreCase);
	});
}

FEFCalystoRoomIdentityInputV6 MakeRoom(const int32 Index, const int32 Flags = 0)
{
	FEFCalystoRoomIdentityInputV6 Result;
	Result.LocalCenter = FVector(Index * 1000.0, (Index % 11) * 1000.0, 0.0);
	Result.Extents = FVector(400.0, 400.0, 250.0);
	Result.TopologyKind = TEXT("Ordinary");
	Result.CollisionOrdinal = Index / 1000;
	Result.RoomFlags = Flags;
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV6PolicyShapeTest,
	"NoShellForWinter.CalystoDungeon.V6.Policy.SingleAssetShapeAndDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoV6PolicyShapeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV6PolicyTests;
	TestEqual(TEXT("Portable SHA-256 empty vector"),
		FEFCalystoDungeonDirectorMathV6::HashCanonicalText(FString()),
		FString(TEXT("E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855")));
	TestEqual(TEXT("Portable SHA-256 abc vector"),
		FEFCalystoDungeonDirectorMathV6::HashCanonicalText(TEXT("abc")),
		FString(TEXT("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD")));
	UEFCalystoDungeonDirectorPolicyV6Asset* Policy = MakePolicy();
	TestNotNull(TEXT("V6 policy"), Policy);
	if (!Policy) return false;
	FString Error;
	TestTrue(*FString::Printf(TEXT("Default V6 validates: %s"), *Error), Policy->Validate(Error));
	TestTrue(TEXT("V6 is one Primary Data Asset"), Policy->IsA<UPrimaryDataAsset>());
	TestFalse(TEXT("V6 is not a DataTable"), Policy->IsA<UDataTable>());
	TestEqual(TEXT("Schema"), Policy->SchemaVersion, 6);
	TestEqual(TEXT("Generator"), Policy->RuntimeGeneratorVersion, 6);
	TestEqual(TEXT("Identity schema"), Policy->IdentityHashSchemaVersion, 3);
	TestEqual(TEXT("Three Dungeon Styles"), Policy->Styles.Num(), 3);
	TestEqual(TEXT("Two authored Room Themes"), Policy->RoomThemes.Num(), 2);
	TestNull(TEXT("NoTheme is not authored"), Policy->FindRoomTheme(TEXT("NoTheme")));
	TestNull(TEXT("Legacy authored Calysto Room Type property was removed"),
		FindFProperty<FProperty>(FEFCalystoRoomThemeProfileV6::StaticStruct(), TEXT("CalystoRoomType")));
	TestEqual(TEXT("Standard weight"), Policy->FindStyle(TEXT("Standard"))->SelectionWeight, 0.50f);
	TestEqual(TEXT("Forge weight"), Policy->FindRoomTheme(TEXT("Forge"))->SelectionWeight, 5.0f);
	TestEqual(TEXT("Shrine weight"), Policy->FindRoomTheme(TEXT("Shrine"))->SelectionWeight, 3.0f);
	for (const FEFCalystoStyleProfileV6& Style : Policy->Styles)
	{
		TestEqual(TEXT("Theme chance starts at 25%"), Style.RoomThemeChance, 0.25f);
		TestTrue(TEXT("Style uses Grey material"),
			Style.DungeonMaterials.WallMaterial.ToSoftObjectPath().ToString().Contains(TEXT("MI_GreyTiles")));
		TestTrue(TEXT("Style owns inline floor architecture"), !Style.Architecture.Floor.IsEmpty());
		TestTrue(TEXT("Style owns inline wall architecture"), !Style.Architecture.Wall.IsEmpty());
		TestTrue(TEXT("Style owns inline roof architecture"), !Style.Architecture.Roof.IsEmpty());
		TestTrue(TEXT("Style owns inline wall lights"), !Style.Architecture.WallLights.IsEmpty());
		if (!Style.Architecture.WallLights.IsEmpty())
		{
			TestEqual(TEXT("Native wall torch defaults to Wall Middle"),
				Style.Architecture.WallLights[0].PlacementZone,
				EEFCalystoPlacementZoneV6::WallMiddle);
			TestEqual(TEXT("Native wall torch defaults to 2 cm jitter"),
				Style.Architecture.WallLights[0].PositionJitterCm, 2.0f);
		}
	}
	const FEFCalystoRoomThemeProfileV6* Forge = Policy->FindRoomTheme(TEXT("Forge"));
	const FEFCalystoRoomThemeProfileV6* Shrine = Policy->FindRoomTheme(TEXT("Shrine"));
	TestNotNull(TEXT("Forge Theme"), Forge);
	TestNotNull(TEXT("Shrine Theme"), Shrine);
	if (!Forge || !Shrine) return false;
	TestTrue(TEXT("Forge uses Base Orange material"), Forge->RoomMaterials.Overrides.WallMaterial.ToSoftObjectPath().ToString().Contains(TEXT("MI_Template_BaseOrange")));
	TestTrue(TEXT("Shrine uses Display Blue material"), Shrine->RoomMaterials.Overrides.WallMaterial.ToSoftObjectPath().ToString().Contains(TEXT("MI_Display_Blue")));
	TestEqual(TEXT("Forge inline Wall Bottom parity"), Forge->Architecture.WallBottom.Num(), 2);
	TestEqual(TEXT("Forge inline Wall Middle parity"), Forge->Architecture.WallMiddle.Num(), 4);
	TestEqual(TEXT("Forge inline Wall Top parity"), Forge->Architecture.WallTop.Num(), 4);
	TestEqual(TEXT("Forge inline Floor parity"), Forge->Architecture.Floor.Num(), 2);
	TestEqual(TEXT("Forge inline Roof parity"), Forge->Architecture.Roof.Num(), 3);
	TestEqual(TEXT("Shrine inline Wall Bottom parity"), Shrine->Architecture.WallBottom.Num(), 1);
	TestEqual(TEXT("Shrine inline Floor parity"), Shrine->Architecture.Floor.Num(), 2);
	TestEqual(TEXT("Shrine inline Corner Bottom parity"), Shrine->Architecture.CornerBottom.Num(), 1);
	for (const FEFCalystoCatalogOverlayV6& Category : Policy->Styles[0].Catalogs)
	{
		for (const FEFCalystoCatalogEntryV6& Entry : Category.Catalog)
		{
			TestEqual(TEXT("Existing catalog actors default to Floor"), Entry.PlacementZone, EEFCalystoPlacementZoneV6::Floor);
			TestEqual(TEXT("Existing catalog actors default to 2 cm jitter"), Entry.PositionJitterCm, 2.0f);
		}
	}
	const TArray<FEFCalystoStyleProbabilityPreviewV6> Preview = Policy->GetProbabilityPreview();
	TestEqual(TEXT("One preview per Style"), Preview.Num(), 3);
	for (const FEFCalystoStyleProbabilityPreviewV6& StylePreview : Preview)
	{
		TestEqual(TEXT("NoTheme preview is 75%"), StylePreview.NoThemeProbability, 0.75f);
		TestEqual(TEXT("Two conditional Theme previews"), StylePreview.Themes.Num(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV6PolicyFreezeTest,
	"NoShellForWinter.CalystoDungeon.V6.Policy.FrozenFloorAndIndependentLanes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoV6PolicyFreezeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV6PolicyTests;
	UEFCalystoDungeonDirectorPolicyV6Asset* Policy = MakePolicy();
	FString Error;
	FEFCalystoResolvedFloorPlanV6 Plan;
	TestTrue(*FString::Printf(TEXT("Standard floor freezes: %s"), *Error),
		Policy && Policy->BuildResolvedFloorPlanForStyle(778899, TEXT("Standard"), Plan, Error));
	if (!Policy || !IsSha256(Plan.FloorPlanHash)) return false;
	TestEqual(TEXT("Exactly one selected Style"), Plan.StyleId, FName(TEXT("Standard")));
	TestEqual(TEXT("Theme chance frozen"), Plan.ThemeRoomChance, 0.25f);
	TestEqual(TEXT("Room identity quantization is frozen"), Plan.RoomIdentityQuantizationCm, 10.0f);
	TestEqual(TEXT("Maximum room records are frozen"), Plan.MaximumRoomRecords, 2048);
	TestEqual(TEXT("Decal component pool capacity is frozen"), Plan.DecalComponentPoolCapacity, 24);
	TestTrue(TEXT("Style Architecture is frozen inline"), !Plan.StyleArchitecture.Floor.IsEmpty());
	TestTrue(TEXT("Style Architecture hash is frozen"), IsSha256(Plan.StyleArchitectureHash));
	TestEqual(TEXT("Exactly two Theme Architecture snapshots are frozen"), Plan.Themes.Num(), 2);
	for (const FEFCalystoResolvedThemeProfileV6& Theme : Plan.Themes)
	{
		TestTrue(TEXT("Each frozen Theme owns an Architecture hash"),
			IsSha256(Theme.ArchitectureHash));
	}
	Policy->PerformanceAndSafety.DecalComponentPoolCapacity = 25;
	TestFalse(TEXT("The shared decal pool has a hard 24-component ceiling"), Policy->Validate(Error));
	Policy->PerformanceAndSafety.DecalComponentPoolCapacity = 24;
	TestEqual(TEXT("Sorted Theme snapshots"), Plan.Themes.Num(), 2);
	TestEqual(TEXT("First canonical Theme"), Plan.Themes[0].ThemeId, FName(TEXT("Forge")));
	TestEqual(TEXT("Second canonical Theme"), Plan.Themes[1].ThemeId, FName(TEXT("Shrine")));
	TestEqual(TEXT("Forge floor material authority is frozen"), Plan.Themes[0].FloorMaterialMode,
		EEFCalystoMaterialResolutionModeV6::Override);
	TestEqual(TEXT("Forge wall material authority is frozen"), Plan.Themes[0].WallMaterialMode,
		EEFCalystoMaterialResolutionModeV6::Override);
	TestEqual(TEXT("Forge roof material authority is frozen"), Plan.Themes[0].RoofMaterialMode,
		EEFCalystoMaterialResolutionModeV6::Override);
	TestEqual(TEXT("Shrine floor material authority is frozen"), Plan.Themes[1].FloorMaterialMode,
		EEFCalystoMaterialResolutionModeV6::Override);
	TestEqual(TEXT("Shrine wall material authority is frozen"), Plan.Themes[1].WallMaterialMode,
		EEFCalystoMaterialResolutionModeV6::Override);
	TestEqual(TEXT("Shrine roof material authority is frozen"), Plan.Themes[1].RoofMaterialMode,
		EEFCalystoMaterialResolutionModeV6::Override);
	TestEqual(TEXT("Alias table count"), Plan.ThemeAliasProbability.Num(), 2);
	TestTrue(TEXT("Reachable visuals contain Grey"), HasPath(Plan.ReachableVisualPreloadPaths, TEXT("MI_GreyTiles")));
	TestTrue(TEXT("Reachable visuals contain Base Orange"), HasPath(Plan.ReachableVisualPreloadPaths, TEXT("MI_Template_BaseOrange")));
	TestTrue(TEXT("Reachable visuals contain Display Blue"), HasPath(Plan.ReachableVisualPreloadPaths, TEXT("MI_Display_Blue")));

	UEFCalystoDungeonDirectorPolicyV6Asset* InheritPolicy = MakePolicy();
	if (!InheritPolicy) return false;
	FEFCalystoRoomThemeProfileV6* ForgeTheme = InheritPolicy->RoomThemes.FindByPredicate(
		[](const FEFCalystoRoomThemeProfileV6& Theme)
		{
			return Theme.ThemeId == FName(TEXT("Forge"));
		});
	TestNotNull(TEXT("Forge Theme remains editable before floor freeze"), ForgeTheme);
	if (!ForgeTheme) return false;
	ForgeTheme->RoomMaterials.WallMode = EEFCalystoMaterialResolutionModeV6::InheritStyle;
	FEFCalystoResolvedFloorPlanV6 InheritPlan;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Mixed-authority floor freezes: %s"), *Error),
		InheritPolicy->BuildResolvedFloorPlanForStyle(778899, TEXT("Standard"), InheritPlan, Error));
	if (InheritPlan.Themes.Num() != 2) return false;
	TestEqual(TEXT("Inherited wall authority is explicit in the frozen Theme"),
		InheritPlan.Themes[0].WallMaterialMode, EEFCalystoMaterialResolutionModeV6::InheritStyle);
	TestTrue(TEXT("Inherited wall resolves to the selected Style material"),
		InheritPlan.Themes[0].EffectiveMaterials.WallMaterial.ToSoftObjectPath() ==
		InheritPlan.StyleMaterials.WallMaterial.ToSoftObjectPath());
	TestTrue(TEXT("Independent floor override remains the Forge material"),
		InheritPlan.Themes[0].EffectiveMaterials.FloorMaterial.ToSoftObjectPath().ToString().Contains(TEXT("MI_Template_BaseOrange")));

	for (int64 RoomId = 1; RoomId <= 1000; ++RoomId)
	{
		const double PresenceBefore = FEFCalystoDungeonDirectorMathV6::ThemePresenceUniform(Plan.FloorSeed, RoomId, Plan.StyleId);
		const double TypeBefore = FEFCalystoDungeonDirectorMathV6::ThemeTypeUniform(Plan.FloorSeed, RoomId, Plan.StyleId);
		Plan.Themes[0].SelectionWeight = 500.0f;
		Plan.Themes[1].SelectionWeight = 1.0f;
		TestEqual(TEXT("Theme weights do not perturb presence lane"),
			FEFCalystoDungeonDirectorMathV6::ThemePresenceUniform(Plan.FloorSeed, RoomId, Plan.StyleId), PresenceBefore);
		TestEqual(TEXT("Presence and type domains remain stable"),
			FEFCalystoDungeonDirectorMathV6::ThemeTypeUniform(Plan.FloorSeed, RoomId, Plan.StyleId), TypeBefore);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV6PolicyStatisticalTest,
	"NoShellForWinter.CalystoDungeon.V6.Policy.RoomThemeStatistics100K",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoV6PolicyStatisticalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV6PolicyTests;
	UEFCalystoDungeonDirectorPolicyV6Asset* Policy = MakePolicy();
	FString Error;
	FEFCalystoResolvedFloorPlanV6 Plan;
	if (!Policy || !Policy->BuildResolvedFloorPlanForStyle(0x12345678, TEXT("Standard"), Plan, Error))
	{
		AddError(Error);
		return false;
	}
	constexpr int32 Trials = 100000;
	int32 Themed = 0;
	int32 Forge = 0;
	int32 Shrine = 0;
	for (int64 RoomId = 1; RoomId <= Trials; ++RoomId)
	{
		FEFCalystoRoomContextV6 Context;
		if (!FEFCalystoDungeonDirectorMathV6::ResolveRoomContext(Plan, RoomId, 0, Context, Error))
		{
			AddError(Error);
			return false;
		}
		if (Context.bIsThemed)
		{
			++Themed;
			if (Context.ThemeId == FName(TEXT("Forge"))) ++Forge;
			if (Context.ThemeId == FName(TEXT("Shrine"))) ++Shrine;
		}
	}
	const double ThemeSigma = FMath::Sqrt(Trials * 0.25 * 0.75);
	TestTrue(TEXT("Theme presence is inside six sigma of 25%"),
		FMath::Abs(Themed - Trials * 0.25) <= 6.0 * ThemeSigma);
	const double ForgeExpected = Themed * 0.625;
	const double ForgeSigma = FMath::Sqrt(Themed * 0.625 * 0.375);
	TestTrue(TEXT("Forge conditional share is inside six sigma of 62.5%"),
		FMath::Abs(Forge - ForgeExpected) <= 6.0 * ForgeSigma);
	TestEqual(TEXT("Every themed room resolves exactly one known Theme"), Forge + Shrine, Themed);

	for (const int32 ProtectedFlag : {
		static_cast<int32>(EEFCalystoRoomFlagsV6::Start),
		static_cast<int32>(EEFCalystoRoomFlagsV6::End),
		static_cast<int32>(EEFCalystoRoomFlagsV6::Critical),
		static_cast<int32>(EEFCalystoRoomFlagsV6::Progression)})
	{
		for (int64 RoomId = 1; RoomId <= 1000; ++RoomId)
		{
			FEFCalystoRoomContextV6 Context;
			TestTrue(TEXT("Protected room resolves"), FEFCalystoDungeonDirectorMathV6::ResolveRoomContext(Plan, RoomId, ProtectedFlag, Context, Error));
			TestFalse(TEXT("Protected room never receives a Theme"), Context.bIsThemed);
			TestEqual(TEXT("Protected room is internal NoTheme"), Context.ThemeId, UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV6PolicyManifestTest,
	"NoShellForWinter.CalystoDungeon.V6.Policy.StableRoomManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoV6PolicyManifestTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV6PolicyTests;
	UEFCalystoDungeonDirectorPolicyV6Asset* Policy = MakePolicy();
	FString Error;
	FEFCalystoResolvedFloorPlanV6 Plan;
	if (!Policy || !Policy->BuildResolvedFloorPlanForStyle(919191, TEXT("Branching"), Plan, Error))
	{
		AddError(Error);
		return false;
	}
	TArray<FEFCalystoRoomIdentityInputV6> Forward;
	Forward.Add(MakeRoom(0, static_cast<int32>(EEFCalystoRoomFlagsV6::Start)));
	for (int32 Index = 1; Index < 64; ++Index) Forward.Add(MakeRoom(Index));
	Forward.Add(MakeRoom(64, static_cast<int32>(EEFCalystoRoomFlagsV6::End)));
	TArray<FEFCalystoRoomIdentityInputV6> Reverse = Forward;
	Algo::Reverse(Reverse);
	FEFCalystoRoomManifestV6 A;
	FEFCalystoRoomManifestV6 B;
	TestTrue(TEXT("Forward manifest resolves"), Policy->BuildRoomManifest(Plan, Forward, A, Error));
	TestTrue(TEXT("Reverse manifest resolves"), Policy->BuildRoomManifest(Plan, Reverse, B, Error));
	TestEqual(TEXT("Input point order does not affect manifest identity"), A.ManifestHash, B.ManifestHash);
	TestEqual(TEXT("Start and End are excluded from eligible denominator"), A.EligibleRoomCount, 63);
	TestTrue(TEXT("Every floor with eligible rooms realizes at least one Room Theme"), A.ThemedRoomCount >= 1);
	TArray<int64> EligibleRoomIds;
	for (const FEFCalystoRoomContextV6& Room : A.Rooms)
	{
		if (!FEFCalystoDungeonDirectorMathV6::IsProtectedRoom(Room.RoomFlags))
		{
			EligibleRoomIds.Add(Room.StableRoomId);
		}
	}
	const int64 GuaranteedRoomId = FEFCalystoDungeonDirectorMathV6::SelectGuaranteedThemeRoomId(
		Plan.FloorSeed, Plan.StyleId, EligibleRoomIds);
	const FEFCalystoRoomContextV6* GuaranteedRoom = A.Rooms.FindByPredicate(
		[GuaranteedRoomId](const FEFCalystoRoomContextV6& Room)
		{
			return Room.StableRoomId == GuaranteedRoomId;
		});
	TestTrue(TEXT("The deterministic minimum-Theme room is themed"),
		GuaranteedRoom && GuaranteedRoom->bIsThemed);
	for (const FEFCalystoRoomContextV6& Room : A.Rooms)
	{
		if (FEFCalystoDungeonDirectorMathV6::IsProtectedRoom(Room.RoomFlags)
			|| Room.StableRoomId == GuaranteedRoomId)
		{
			continue;
		}
		const bool bExpectedFromIndependentChance =
			FEFCalystoDungeonDirectorMathV6::ThemePresenceUniform(
				Plan.FloorSeed, Room.StableRoomId, Plan.StyleId)
			< static_cast<double>(Plan.ThemeRoomChance);
		TestEqual(TEXT("Every non-guaranteed eligible room keeps the independent 25% roll"),
			Room.bIsThemed, bExpectedFromIndependentChance);
	}
	TArray<int64> ReorderedEligibleRoomIds = EligibleRoomIds;
	Algo::Reverse(ReorderedEligibleRoomIds);
	TestEqual(TEXT("The minimum-Theme room is independent from incoming room order"),
		FEFCalystoDungeonDirectorMathV6::SelectGuaranteedThemeRoomId(
			Plan.FloorSeed, Plan.StyleId, ReorderedEligibleRoomIds),
		GuaranteedRoomId);
	TestTrue(TEXT("Room records are canonical"), Algo::IsSorted(A.Rooms, [](const FEFCalystoRoomContextV6& Left, const FEFCalystoRoomContextV6& Right)
	{
		return Left.StableRoomId < Right.StableRoomId;
	}));
	FEFCalystoRoomIdentityInputV6 CollisionA = MakeRoom(500);
	FEFCalystoRoomIdentityInputV6 CollisionB = CollisionA;
	CollisionB.CollisionOrdinal = 1;
	TestNotEqual(TEXT("Collision ordinal participates in Stable Room ID"),
		FEFCalystoDungeonDirectorMathV6::BuildStableRoomId(Plan.FloorSeed, CollisionA, 10.0f),
		FEFCalystoDungeonDirectorMathV6::BuildStableRoomId(Plan.FloorSeed, CollisionB, 10.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV6PolicyHashesAndClosureTest,
	"NoShellForWinter.CalystoDungeon.V6.Policy.HashesValidationAndCookClosure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoV6PolicyHashesAndClosureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV6PolicyTests;
	UEFCalystoDungeonDirectorPolicyV6Asset* Policy = MakePolicy();
	if (!Policy) return false;
	const FString GameplayHash = Policy->GetGameplayHash();
	const FString AuthoringHash = Policy->GetAuthoringHash();
	const FString MaterialHash = Policy->GetMaterialHash();
	const FString DecalHash = Policy->GetDecalHash();
	TestTrue(TEXT("Gameplay SHA-256"), IsSha256(GameplayHash));
	TestTrue(TEXT("Authoring SHA-256"), IsSha256(AuthoringHash));
	TestTrue(TEXT("Material SHA-256"), IsSha256(MaterialHash));
	TestTrue(TEXT("Decal SHA-256"), IsSha256(DecalHash));

	UEFCalystoDungeonDirectorPolicyV6Asset* Duplicate =
		DuplicateObject<UEFCalystoDungeonDirectorPolicyV6Asset>(Policy, GetTransientPackage());
	Duplicate->RoomThemes[0].Description = TEXT("Authoring-only text changed.");
	TestEqual(TEXT("Authoring text does not change gameplay"), Duplicate->GetGameplayHash(), GameplayHash);
	TestNotEqual(TEXT("Authoring text changes authoring identity"), Duplicate->GetAuthoringHash(), AuthoringHash);
	Duplicate->RoomThemes[0].RoomMaterials.Overrides.WallMaterial =
		TSoftObjectPtr<UMaterialInstance>(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Material/MI_GreenTile.MI_GreenTile")));
	TestNotEqual(TEXT("Material edit changes material identity"), Duplicate->GetMaterialHash(), MaterialHash);
	TestNotEqual(TEXT("Material edit changes gameplay identity"), Duplicate->GetGameplayHash(), GameplayHash);
	Duplicate->RoomThemes[0].RoomMaterials.Overrides.WallMaterial.Reset();
	FString Error;
	TestFalse(TEXT("Override plus null fails closed"), Duplicate->Validate(Error));
	Duplicate = DuplicateObject<UEFCalystoDungeonDirectorPolicyV6Asset>(Policy, GetTransientPackage());
	Duplicate->RoomThemes[0].Architecture.WallBottom[0].SelectionWeight += 1;
	TestNotEqual(TEXT("Inline Architecture edit changes gameplay identity"),
		Duplicate->GetGameplayHash(), GameplayHash);
	Duplicate = DuplicateObject<UEFCalystoDungeonDirectorPolicyV6Asset>(Policy, GetTransientPackage());
	Duplicate->Styles[0].Catalogs[0].Catalog[0].PlacementZone =
		EEFCalystoPlacementZoneV6::WallBottom;
	TestNotEqual(TEXT("Catalog Placement Zone changes gameplay identity"),
		Duplicate->GetGameplayHash(), GameplayHash);

	const TArray<FString> Paths = Policy->GetCookBundleAssetPaths();
	TestFalse(TEXT("Forge authored Room Data Asset is absent from cook closure"),
		Paths.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("DA_RoomForge")); }));
	TestFalse(TEXT("Shrine authored Room Data Asset is absent from cook closure"),
		Paths.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("DA_RoomShrine")); }));
	TestTrue(TEXT("Inline Style floor mesh is cooked"),
		Paths.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("SM_Floor.SM_Floor")); }));
	TestTrue(TEXT("Inline Forge Level Instance is cooked"),
		Paths.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("PCGDA_Table.PCGDA_Table")); }));
	TestTrue(TEXT("Definitive locked chest is cooked"), Paths.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("BP_CalystoLockedChest.BP_CalystoLockedChest_C")); }));
	TestTrue(TEXT("Definitive armor pickup is cooked"), Paths.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("BP_CalystoArmorPickup.BP_CalystoArmorPickup_C")); }));
	TestTrue(TEXT("Minimal blood color texture is cooked"), Paths.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("T_Splat_04")); }));
	TestTrue(TEXT("Minimal blood normal texture is cooked"), Paths.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("T_Splat_N_04")); }));
	TestTrue(TEXT("Cooked-safe SetDungeonMesh closure is Director-owned"), Paths.Contains(
		TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_SetDungeonMeshCookedSafe.PCG_SetDungeonMeshCookedSafe")));
	TestTrue(TEXT("Cooked-safe AddRamps closure is Director-owned"), Paths.Contains(
		TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_AddRampsCookedSafe.PCG_AddRampsCookedSafe")));
	TestFalse(TEXT("RealisticBlood Demo is excluded"), Paths.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("RealisticBlood/Demo")); }));
	TestFalse(TEXT("Niagara is excluded"), Paths.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("Niagara")); }));
	TestFalse(TEXT("Version-suffixed content BP paths are excluded"), Paths.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("CalystoLockedChestV")) || Path.Contains(TEXT("CalystoArmorPickupV")); }));
	TestTrue(TEXT("V6 exposes the primary cook bundle"), Policy->GetCookBundleNames().Contains(TEXT("CalystoFloorV6")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV6PolicyStreamingTest,
	"NoShellForWinter.CalystoDungeon.V6.Policy.PhasedStreamingClosure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoV6PolicyStreamingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV6PolicyTests;
	UEFCalystoDungeonDirectorPolicyV6Asset* Policy = MakePolicy();
	FString Error;
	FEFCalystoResolvedFloorPlanV6 Plan;
	if (!Policy || !Policy->BuildResolvedFloorPlanForStyle(314159, TEXT("Standard"), Plan, Error))
	{
		AddError(Error);
		return false;
	}
	TArray<FEFCalystoRoomIdentityInputV6> Inputs;
	for (int32 Index = 0; Index < 128; ++Index) Inputs.Add(MakeRoom(Index));
	FEFCalystoRoomManifestV6 Manifest;
	if (!Policy->BuildRoomManifest(Plan, Inputs, Manifest, Error))
	{
		AddError(Error);
		return false;
	}
	TArray<FSoftObjectPath> ContentPaths;
	TArray<FSoftObjectPath> DecalPaths;
	TestTrue(TEXT("Post-topology content closure resolves"), Policy->GatherPostTopologyContentPaths(Plan, Manifest, ContentPaths, Error));
	TestTrue(TEXT("Post-topology decal closure resolves"), Policy->GatherPostTopologyDecalPaths(Plan, Manifest, DecalPaths, Error));
	TestTrue(TEXT("Content closure has gameplay classes"), !ContentPaths.IsEmpty());
	TestTrue(TEXT("Decal closure contains project-owned shared MIs"), HasPath(DecalPaths, TEXT("MI_CalystoBloodDecal")));
	TestTrue(TEXT("Visual phase includes the selected native architectural torch"), HasPath(Plan.ReachableVisualPreloadPaths, TEXT("BP_WallTorch")));
	for (const FSoftObjectPath& Path : ContentPaths)
	{
		TestFalse(TEXT("Post-topology gameplay catalog stays out of Floor Visual"), Plan.ReachableVisualPreloadPaths.Contains(Path));
	}
	TestFalse(TEXT("Content phase does not pull the blood pack"), HasPath(ContentPaths, TEXT("RealisticBlood")));
	TestFalse(TEXT("Decal phase excludes Demo"), HasPath(DecalPaths, TEXT("RealisticBlood/Demo")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
