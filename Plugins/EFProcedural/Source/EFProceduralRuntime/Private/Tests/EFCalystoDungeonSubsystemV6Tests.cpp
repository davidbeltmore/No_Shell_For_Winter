#include "Calysto/EFCalystoDungeonSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace EFCalystoDungeonSubsystemV6Tests
{
	FString Hash(const TCHAR* Text)
	{
		return UEFCalystoDungeonSubsystem::ComputeCanonicalHash(Text);
	}

	void SetRange(
		FEFCalystoPertRangeV6& Range,
		const float Minimum,
		const float Mode,
		const float Maximum)
	{
		Range.Minimum = Minimum;
		Range.Mode = Mode;
		Range.Maximum = Maximum;
		Range.Shape = 4.0f;
	}

	FEFCalystoResolvedFloorPlanV6 MakeFloorPlan()
	{
		FEFCalystoResolvedFloorPlanV6 Plan;
		Plan.FloorSeed = 40102030;
		Plan.StyleId = TEXT("Standard");
		Plan.ThemeRoomChance = 0.25f;
		Plan.RoomIdentityQuantizationCm = 10.0f;
		Plan.MaximumRoomRecords = 2048;
		Plan.DecalComponentPoolCapacity = 24;
		SetRange(Plan.Layout.DungeonSize, 18.0f, 24.0f, 30.0f);
		SetRange(Plan.Layout.CandidateDensity, 0.20f, 0.32f, 0.50f);
		SetRange(Plan.Layout.SidePathChance, 0.30f, 0.50f, 0.70f);
		Plan.Layout.MinimumRoomSize = 4;
		Plan.Layout.MaximumRoomSize = 8;
		Plan.PolicyHash = Hash(TEXT("Subsystem.Realized.Policy"));
		Plan.StyleCatalogHash = Hash(TEXT("Subsystem.Realized.Catalog"));
		Plan.StyleMaterialHash = Hash(TEXT("Subsystem.Realized.Material"));
		Plan.StyleDecalHash = Hash(TEXT("Subsystem.Realized.Decal"));
		Plan.FloorPlanHash = Hash(TEXT("Subsystem.Realized.FloorPlan"));
		FEFCalystoResolvedThemeProfileV6& Theme = Plan.Themes.AddDefaulted_GetRef();
		Theme.ThemeId = TEXT("Forge");
		Theme.SelectionWeight = 1.0f;
		Theme.CatalogHash = Hash(TEXT("Subsystem.Realized.Forge.Catalog"));
		Theme.MaterialHash = Hash(TEXT("Subsystem.Realized.Forge.Material"));
		Theme.DecalHash = Hash(TEXT("Subsystem.Realized.Forge.Decal"));
		Plan.ThemeAliasProbability.Add(1.0f);
		Plan.ThemeAliasIndex.Add(0);
		return Plan;
	}

	FEFCalystoCompanionRosterSnapshotV6 MakeEmptyRoster(const int64 RunEpoch)
	{
		FEFCalystoCompanionRosterSnapshotV6 Roster;
		Roster.bIsValid = true;
		Roster.RunEpoch = RunEpoch;
		Roster.SnapshotHash =
			FEFCalystoDungeonRuntimeMathV6::ComputeCompanionRosterHash(Roster);
		return Roster;
	}

	bool MakeRealizedFixture(
		FEFCalystoDungeonGenerationContextV6& OutContext,
		FEFCalystoResolvedFloorIntentV6& OutIntent,
		FEFCalystoResolvedFloorPlanV6& OutFloorPlan,
		FEFCalystoRoomManifestV6& OutRoomManifest,
		FEFCalystoPopulationPlanV6& OutPopulationPlan,
		FEFCalystoRealizedPopulationActorRecordV6& OutActor,
		FString& OutMaterializationHash,
		FString& OutError)
	{
		OutFloorPlan = MakeFloorPlan();
		const FEFCalystoCompanionRosterSnapshotV6 Roster = MakeEmptyRoster(3);
		const FEFCalystoRunEcologyStateV6 Ecology =
			UEFCalystoDungeonSubsystem::BuildInitialEcologyV6(
				9918273, OutFloorPlan.PolicyHash, Roster);
		OutContext.RunSeed = 9918273;
		OutContext.FloorNumber = 7;
		OutContext.GenerationSerial = 4;
		OutContext.RunEpoch = 3;
		OutContext.TravelKind = EEFCalystoDungeonTravelKindV6::Advance;
		OutContext.PolicyHash = OutFloorPlan.PolicyHash;
		OutContext.ContextHash =
			FEFCalystoDungeonRuntimeMathV6::ComputeGenerationContextHash(OutContext);
		FEFCalystoDirectorIntentV6 DirectorIntent;
		DirectorIntent.IntentHash =
			FEFCalystoDungeonRuntimeMathV6::ComputeDirectorIntentHash(DirectorIntent);
		FEFCalystoFloorOutcomeV6 Outcome;
		Outcome.OutcomeHash =
			FEFCalystoDungeonRuntimeMathV6::ComputeFloorOutcomeHash(Outcome);
		FEFCalystoLightingPolicyV6 Lighting;
		if (!FEFCalystoDungeonRuntimeMathV6::BuildResolvedFloorIntent(
				OutContext, DirectorIntent, Outcome, Ecology, Roster,
				OutFloorPlan, Lighting, OutIntent, OutError))
		{
			return false;
		}

		OutRoomManifest.FloorSeed = OutFloorPlan.FloorSeed;
		OutRoomManifest.StyleId = OutFloorPlan.StyleId;
		OutRoomManifest.FloorPlanHash = OutFloorPlan.FloorPlanHash;
		FEFCalystoRoomContextV6& Room = OutRoomManifest.Rooms.AddDefaulted_GetRef();
		Room.StableRoomId = 701;
		Room.LocalCenter = FVector(1000.0, 500.0, 100.0);
		Room.Extents = FVector(400.0, 400.0, 200.0);
		Room.TopologyKind = TEXT("Ordinary");
		Room.StyleId = OutFloorPlan.StyleId;
		Room.ThemeId = UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId;
		Room.CatalogHash = OutFloorPlan.StyleCatalogHash;
		Room.RoomContextHash = Hash(TEXT("Subsystem.Realized.Room.Context"));
		OutRoomManifest.EligibleRoomCount = 1;
		OutRoomManifest.ManifestHash = Hash(TEXT("Subsystem.Realized.Room.Manifest"));

		OutPopulationPlan.FloorSeed = OutFloorPlan.FloorSeed;
		OutPopulationPlan.FloorNumber = OutContext.FloorNumber;
		OutPopulationPlan.StyleId = OutFloorPlan.StyleId;
		OutPopulationPlan.FloorPlanHash = OutFloorPlan.FloorPlanHash;
		OutPopulationPlan.RoomManifestHash = OutRoomManifest.ManifestHash;
		FEFCalystoRoomPopulationPlanV6& RoomPlan =
			OutPopulationPlan.Rooms.AddDefaulted_GetRef();
		RoomPlan.StableRoomId = Room.StableRoomId;
		RoomPlan.ThemeId = Room.ThemeId;
		RoomPlan.EffectiveCatalogHash = Room.CatalogHash;
		RoomPlan.RoomPopulationHash = Hash(TEXT("Subsystem.Realized.Room.Population"));
		FEFCalystoPopulationDecisionV6& Chest = RoomPlan.Decisions.AddDefaulted_GetRef();
		Chest.Kind = EEFCalystoPopulationDecisionKindV6::Actor;
		Chest.StableRoomId = Room.StableRoomId;
		Chest.StyleId = OutFloorPlan.StyleId;
		Chest.ThemeId = Room.ThemeId;
		Chest.CategoryId = TEXT("Chest");
		Chest.EntryId = TEXT("Chest.Locked.Test");
		Chest.ClassPath = FSoftObjectPath(TEXT("/Script/Engine.Actor"));
		Chest.Tier = EEFCalystoRarityTierV6::Rare;
		Chest.DecisionId = Hash(TEXT("Subsystem.Realized.Decision.Chest"));
		FEFCalystoPopulationDecisionV6& Content = RoomPlan.Decisions.AddDefaulted_GetRef();
		Content.Kind = EEFCalystoPopulationDecisionKindV6::ChestContent;
		Content.StableRoomId = Room.StableRoomId;
		Content.StyleId = OutFloorPlan.StyleId;
		Content.ThemeId = Room.ThemeId;
		Content.CategoryId = TEXT("ChestContents");
		Content.EntryId = TEXT("Potion.Health.Test");
		Content.ClassPath = FSoftObjectPath(TEXT("/Script/CoreUObject.Object"));
		Content.Tier = EEFCalystoRarityTierV6::Rare;
		Content.ParentDecisionId = Chest.DecisionId;
		Content.DecisionId = Hash(TEXT("Subsystem.Realized.Decision.Content"));
		OutPopulationPlan.ActorDecisionCount = 1;
		OutPopulationPlan.ChestContentDecisionCount = 1;
		OutPopulationPlan.ChestCount = 1;
		OutPopulationPlan.PreloadClassPaths = {Chest.ClassPath, Content.ClassPath};
		OutPopulationPlan.PopulationHash = Hash(TEXT("Subsystem.Realized.Population"));

		OutActor.StableActorId = FName(*Chest.DecisionId);
		OutActor.StableRoomId = Chest.StableRoomId;
		OutActor.CategoryId = Chest.CategoryId;
		OutActor.CatalogEntryId = Chest.EntryId;
		OutActor.ActorClass = TSoftClassPtr<AActor>(Chest.ClassPath);
		OutActor.Transform = FTransform(
			FRotator(0.0, 37.0, 0.0), FVector(1120.0, 620.0, 140.0));
		OutActor.Tier = Chest.Tier;
		OutActor.Lifecycle = Chest.Lifecycle;
		OutActor.ResourceCost = 1.0f;
		OutActor.VerifiedChestContentIds.Add(FName(*Content.DecisionId));
		OutMaterializationHash = Hash(TEXT("Subsystem.Realized.Materialization"));
		return true;
	}

	const FStructProperty* FindReturnStruct(const UClass* Class, const FName FunctionName)
	{
		const UFunction* Function = Class ? Class->FindFunctionByName(FunctionName) : nullptr;
		if (!Function)
		{
			return nullptr;
		}
		for (TFieldIterator<FProperty> It(Function, EFieldIterationFlags::None); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				return CastField<FStructProperty>(*It);
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoSubsystemPublicContractV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Runtime.Subsystem.PublicContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoSubsystemPublicContractV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UClass* Class = UEFCalystoDungeonSubsystem::StaticClass();
	const FStructProperty* SnapshotReturn =
		EFCalystoDungeonSubsystemV6Tests::FindReturnStruct(Class, TEXT("GetSnapshot"));
	const FStructProperty* IntentReturn =
		EFCalystoDungeonSubsystemV6Tests::FindReturnStruct(Class, TEXT("GetResolvedFloorIntent"));
	const FStructProperty* ManifestReturn =
		EFCalystoDungeonSubsystemV6Tests::FindReturnStruct(Class, TEXT("GetRealizedFloorManifest"));
	const FStructProperty* EcologyReturn =
		EFCalystoDungeonSubsystemV6Tests::FindReturnStruct(Class, TEXT("GetRunEcology"));

	TestNotNull(TEXT("GetSnapshot is reflected"), SnapshotReturn);
	TestNotNull(TEXT("GetResolvedFloorIntent is reflected"), IntentReturn);
	TestNotNull(TEXT("GetRealizedFloorManifest is reflected"), ManifestReturn);
	TestNotNull(TEXT("GetRunEcology is reflected"), EcologyReturn);
	TestEqual(TEXT("Unsuffixed snapshot is the definitive struct"),
		SnapshotReturn ? SnapshotReturn->Struct.Get() : nullptr,
		FEFCalystoDungeonSnapshotV6::StaticStruct());
	TestEqual(TEXT("Unsuffixed intent is the definitive struct"),
		IntentReturn ? IntentReturn->Struct.Get() : nullptr,
		FEFCalystoResolvedFloorIntentV6::StaticStruct());
	TestEqual(TEXT("Unsuffixed manifest is the definitive struct"),
		ManifestReturn ? ManifestReturn->Struct.Get() : nullptr,
		FEFCalystoRealizedFloorManifestV6::StaticStruct());
	TestEqual(TEXT("Unsuffixed ecology is the definitive struct"),
		EcologyReturn ? EcologyReturn->Struct.Get() : nullptr,
		FEFCalystoRunEcologyStateV6::StaticStruct());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoSubsystemInitialEcologyV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Runtime.Subsystem.InitialEcology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoSubsystemInitialEcologyV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoDungeonSubsystemV6Tests;
	const FString PolicyHash = UEFCalystoDungeonSubsystem::ComputeCanonicalHash(
		TEXT("Subsystem.InitialEcology.Policy"));
	const FEFCalystoCompanionRosterSnapshotV6 Roster = MakeEmptyRoster(7);
	const FEFCalystoRunEcologyStateV6 A =
		UEFCalystoDungeonSubsystem::BuildInitialEcologyV6(9918273, PolicyHash, Roster);
	const FEFCalystoRunEcologyStateV6 B =
		UEFCalystoDungeonSubsystem::BuildInitialEcologyV6(9918273, PolicyHash, Roster);
	const FEFCalystoRunEcologyStateV6 DifferentSeed =
		UEFCalystoDungeonSubsystem::BuildInitialEcologyV6(9918274, PolicyHash, Roster);

	FString Error;
	TestTrue(*FString::Printf(TEXT("Initial ecology validates: %s"), *Error),
		FEFCalystoDungeonRuntimeMathV6::ValidateRunEcology(A, Error));
	TestEqual(TEXT("Equal run identity produces byte-stable ecology identity"),
		A.EcologyHash, B.EcologyHash);
	TestNotEqual(TEXT("Run seed changes the run DNA"),
		A.RunDNAHash, DifferentSeed.RunDNAHash);
	TestNotEqual(TEXT("Run seed changes the ecology identity"),
		A.EcologyHash, DifferentSeed.EcologyHash);
	TestEqual(TEXT("Initial ecology preserves the frozen companion roster"),
		A.CompanionRoster.SnapshotHash, Roster.SnapshotHash);
	TestTrue(TEXT("Initial Scale stays normalized"), A.Scale >= -1.0f && A.Scale <= 1.0f);
	TestTrue(TEXT("Initial Branching stays normalized"), A.Branching >= -1.0f && A.Branching <= 1.0f);
	TestTrue(TEXT("Initial Threat stays normalized"), A.Threat >= -1.0f && A.Threat <= 1.0f);
	TestTrue(TEXT("Initial Abundance stays normalized"), A.Abundance >= -1.0f && A.Abundance <= 1.0f);
	TestTrue(TEXT("Initial Mystery stays normalized"), A.Mystery >= -1.0f && A.Mystery <= 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoSubsystemThemeWeightIsolationV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Runtime.Subsystem.ThemeWeightIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoSubsystemThemeWeightIsolationV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoDungeonSubsystemV6Tests;
	UEFCalystoDungeonDirectorPolicyV6Asset* BaselinePolicy =
		NewObject<UEFCalystoDungeonDirectorPolicyV6Asset>(
			GetTransientPackage(), NAME_None, RF_Transient);
	UEFCalystoDungeonDirectorPolicyV6Asset* ReweightedPolicy =
		NewObject<UEFCalystoDungeonDirectorPolicyV6Asset>(
			GetTransientPackage(), NAME_None, RF_Transient);
	TestNotNull(TEXT("Baseline transient V6 policy"), BaselinePolicy);
	TestNotNull(TEXT("Reweighted transient V6 policy"), ReweightedPolicy);
	if (!BaselinePolicy || !ReweightedPolicy)
	{
		return false;
	}
	BaselinePolicy->InitializeV6Defaults();
	ReweightedPolicy->InitializeV6Defaults();
	for (FEFCalystoRoomThemeProfileV6& Theme : ReweightedPolicy->RoomThemes)
	{
		if (Theme.ThemeId.IsEqual(TEXT("Forge"), ENameCase::IgnoreCase))
		{
			Theme.SelectionWeight = 1.0f;
		}
		else if (Theme.ThemeId.IsEqual(TEXT("Shrine"), ENameCase::IgnoreCase))
		{
			Theme.SelectionWeight = 500.0f;
		}
	}
#if WITH_EDITOR
	FPropertyChangedEvent Change(nullptr);
	ReweightedPolicy->PostEditChangeProperty(Change);
#else
	ReweightedPolicy->PostLoad();
#endif

	const FString BaselinePolicyHash = BaselinePolicy->GetGameplayHash();
	const FString ReweightedPolicyHash = ReweightedPolicy->GetGameplayHash();
	TestNotEqual(TEXT("Changing Theme weights changes the policy identity"),
		BaselinePolicyHash, ReweightedPolicyHash);

	const int64 RunSeed = 9918273;
	const int64 RunEpoch = 11;
	const FEFCalystoCompanionRosterSnapshotV6 Roster = MakeEmptyRoster(RunEpoch);
	FEFCalystoDirectorIntentV6 DirectorIntent;
	DirectorIntent.IntentHash =
		FEFCalystoDungeonRuntimeMathV6::ComputeDirectorIntentHash(DirectorIntent);
	FEFCalystoFloorOutcomeV6 Outcome;
	Outcome.OutcomeHash =
		FEFCalystoDungeonRuntimeMathV6::ComputeFloorOutcomeHash(Outcome);

	auto Resolve = [&](const UEFCalystoDungeonDirectorPolicyV6Asset* Policy,
		const FString& PolicyHash, FEFCalystoResolvedFloorIntentV6& OutIntent,
		FString& OutError)
	{
		FEFCalystoDungeonGenerationContextV6 Context;
		Context.RunSeed = RunSeed;
		Context.FloorNumber = 4;
		Context.GenerationSerial = 2;
		Context.RunEpoch = RunEpoch;
		Context.TravelKind = EEFCalystoDungeonTravelKindV6::Replay;
		Context.PolicyHash = PolicyHash;
		Context.ContextHash =
			FEFCalystoDungeonRuntimeMathV6::ComputeGenerationContextHash(Context);
		const FEFCalystoRunEcologyStateV6 Ecology =
			UEFCalystoDungeonSubsystem::BuildInitialEcologyV6(
				RunSeed, PolicyHash, Roster);
		return UEFCalystoDungeonSubsystem::ResolveFloorIntentForTestingV6(
			Policy, Context, DirectorIntent, Outcome, Ecology, Roster,
			OutIntent, OutError);
	};

	FEFCalystoResolvedFloorIntentV6 BaselineIntent;
	FEFCalystoResolvedFloorIntentV6 ReweightedIntent;
	FString BaselineError;
	FString ReweightedError;
	if (!TestTrue(*FString::Printf(TEXT("Baseline floor intent resolves: %s"),
		*BaselineError), Resolve(BaselinePolicy, BaselinePolicyHash,
			BaselineIntent, BaselineError)) ||
		!TestTrue(*FString::Printf(TEXT("Reweighted floor intent resolves: %s"),
			*ReweightedError), Resolve(ReweightedPolicy, ReweightedPolicyHash,
				ReweightedIntent, ReweightedError)))
	{
		return false;
	}

	TestEqual(TEXT("Theme weights cannot change the floor seed"),
		BaselineIntent.FloorPlan.FloorSeed, ReweightedIntent.FloorPlan.FloorSeed);
	TestEqual(TEXT("Theme weights cannot change the selected Style"),
		BaselineIntent.StyleId, ReweightedIntent.StyleId);
	TestEqual(TEXT("Theme weights cannot change dungeon dimensions"),
		BaselineIntent.DungeonSize, ReweightedIntent.DungeonSize);
	TestEqual(TEXT("Theme weights cannot change candidate density"),
		BaselineIntent.CandidateDensity, ReweightedIntent.CandidateDensity);
	TestEqual(TEXT("Theme weights cannot change side-path probability"),
		BaselineIntent.SidePathChance, ReweightedIntent.SidePathChance);
	TestEqual(TEXT("Theme weights cannot change the PCG seed"),
		BaselineIntent.PCGSeed, ReweightedIntent.PCGSeed);

	int32 ThemedRooms = 0;
	int32 ChangedThemeTypes = 0;
	for (int64 StableRoomId = 1; StableRoomId <= 4096; ++StableRoomId)
	{
		FEFCalystoRoomContextV6 BaselineRoom;
		FEFCalystoRoomContextV6 ReweightedRoom;
		BaselineError.Reset();
		ReweightedError.Reset();
		if (!FEFCalystoDungeonDirectorMathV6::ResolveRoomContext(
				BaselineIntent.FloorPlan, StableRoomId, 0,
				BaselineRoom, BaselineError) ||
			!FEFCalystoDungeonDirectorMathV6::ResolveRoomContext(
				ReweightedIntent.FloorPlan, StableRoomId, 0,
				ReweightedRoom, ReweightedError))
		{
			AddError(FString::Printf(
				TEXT("Room %lld did not resolve in both policies: baseline=%s; reweighted=%s"),
				static_cast<long long>(StableRoomId), *BaselineError, *ReweightedError));
			return false;
		}
		if (BaselineRoom.bIsThemed != ReweightedRoom.bIsThemed)
		{
			AddError(FString::Printf(
				TEXT("Theme-presence membership changed for Stable Room ID %lld."),
				static_cast<long long>(StableRoomId)));
			return false;
		}
		if (BaselineRoom.bIsThemed)
		{
			++ThemedRooms;
			if (!BaselineRoom.ThemeId.IsEqual(
				ReweightedRoom.ThemeId, ENameCase::IgnoreCase))
			{
				++ChangedThemeTypes;
			}
		}
	}
	TestTrue(TEXT("The fixture exercises themed rooms"), ThemedRooms > 0);
	TestTrue(TEXT("Reweighting changes Theme type without changing Theme presence"),
		ChangedThemeTypes > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoSubsystemRealizedEvidenceV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Runtime.Subsystem.RealizedEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoSubsystemRealizedEvidenceV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoDungeonSubsystemV6Tests;
	FEFCalystoDungeonGenerationContextV6 Context;
	FEFCalystoResolvedFloorIntentV6 Intent;
	FEFCalystoResolvedFloorPlanV6 FloorPlan;
	FEFCalystoRoomManifestV6 RoomManifest;
	FEFCalystoPopulationPlanV6 PopulationPlan;
	FEFCalystoRealizedPopulationActorRecordV6 Actor;
	FString MaterializationHash;
	FString Error;
	if (!MakeRealizedFixture(
			Context, Intent, FloorPlan, RoomManifest, PopulationPlan,
			Actor, MaterializationHash, Error))
	{
		AddError(FString::Printf(TEXT("Could not build realized-evidence fixture: %s"), *Error));
		return false;
	}

	FEFCalystoRealizedFloorManifestV6 A;
	TestTrue(*FString::Printf(TEXT("Actual materializer evidence builds: %s"), *Error),
		UEFCalystoDungeonSubsystem::BuildRealizedFloorManifestV6(
			Context, Intent, FloorPlan, RoomManifest, PopulationPlan,
			MaterializationHash, 3, MakeArrayView(&Actor, 1), A, Error));
	TestEqual(TEXT("The candidate-anchor count is real evidence"), A.CandidateAnchorCount, 3);
	TestEqual(TEXT("The actual actor transform is retained"), A.Actors[0].Transform, Actor.Transform);
	TestEqual(TEXT("The verified chest-content ID is retained"),
		A.Actors[0].VerifiedChestContentIds, Actor.VerifiedChestContentIds);

	FEFCalystoRealizedFloorManifestV6 Repeated;
	Error.Reset();
	TestTrue(TEXT("Identical evidence rebuilds"),
		UEFCalystoDungeonSubsystem::BuildRealizedFloorManifestV6(
			Context, Intent, FloorPlan, RoomManifest, PopulationPlan,
			MaterializationHash, 3, MakeArrayView(&Actor, 1), Repeated, Error));
	TestEqual(TEXT("Identical evidence is byte-identical by canonical hash"),
		Repeated.ManifestHash, A.ManifestHash);

	FEFCalystoRealizedPopulationActorRecordV6 MovedActor = Actor;
	MovedActor.Transform.AddToTranslation(FVector(10.0, 0.0, 0.0));
	FEFCalystoRealizedFloorManifestV6 Moved;
	Error.Reset();
	TestTrue(TEXT("A different real placement remains valid evidence"),
		UEFCalystoDungeonSubsystem::BuildRealizedFloorManifestV6(
			Context, Intent, FloorPlan, RoomManifest, PopulationPlan,
			MaterializationHash, 3, MakeArrayView(&MovedActor, 1), Moved, Error));
	TestNotEqual(TEXT("Actual transform drift changes manifest identity"),
		Moved.ManifestHash, A.ManifestHash);

	FEFCalystoRealizedPopulationActorRecordV6 MissingContent = Actor;
	MissingContent.VerifiedChestContentIds.Reset();
	FEFCalystoRealizedFloorManifestV6 Rejected;
	Error.Reset();
	TestFalse(TEXT("Missing verified chest content fails closed"),
		UEFCalystoDungeonSubsystem::BuildRealizedFloorManifestV6(
			Context, Intent, FloorPlan, RoomManifest, PopulationPlan,
			MaterializationHash, 3, MakeArrayView(&MissingContent, 1), Rejected, Error));
	Error.Reset();
	TestFalse(TEXT("Fewer candidate anchors than realized actors fails closed"),
		UEFCalystoDungeonSubsystem::BuildRealizedFloorManifestV6(
			Context, Intent, FloorPlan, RoomManifest, PopulationPlan,
			MaterializationHash, 0, MakeArrayView(&Actor, 1), Rejected, Error));
	return true;
}

#endif
