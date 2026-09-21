#if WITH_DEV_AUTOMATION_TESTS

#include "Calysto/EFCalystoArchitecturePlanner.h"
#include "Calysto/EFCalystoContentReservationPlanner.h"
#include "Calysto/EFCalystoDungeonDirectorAsset.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

namespace EFCalystoDirectorProbabilityFullLawTests
{
constexpr int32 Trials = 100000;
constexpr int64 TestFloor = 7;

FGuid Id(const uint32 Value)
{
	return FGuid(0x46554C4C, 0x4C415731, 0, Value); // "FULLLAW1"
}

/** Independent binomial oracle. Zero and one remain exact boundary cases. */
bool WithinSixSigma(const int64 Actual, const int64 Opportunities, const double Probability)
{
	if (Opportunities < 0 || Actual < 0 || Actual > Opportunities
		|| !FMath::IsFinite(Probability) || Probability < 0.0 || Probability > 1.0) return false;
	if (Opportunities == 0) return Probability == 0.0 && Actual == 0;
	const double Expected = Opportunities * Probability;
	if (Probability == 0.0 || Probability == 1.0)
	{
		return Actual == FMath::RoundToInt64(Expected);
	}
	const double Tolerance = FMath::CeilToDouble(6.0 * FMath::Sqrt(Opportunities * Probability * (1.0 - Probability)));
	return FMath::Abs(static_cast<double>(Actual) - Expected) <= Tolerance;
}

bool Compile(FAutomationTestBase& Test, const UEFCalystoDungeonDirectorAsset& Asset, FEFCalystoCompiledDirector& OutConfiguration)
{
	TArray<FEFCalystoValidationIssue> Issues;
	if (Asset.Compile(OutConfiguration, Issues)) return true;
	for (const FEFCalystoValidationIssue& Issue : Issues) Test.AddError(Issue.Field + TEXT(": ") + Issue.Message);
	return false;
}

UEFCalystoDungeonDirectorAsset* MakeBaseAsset()
{
	auto* Asset = NewObject<UEFCalystoDungeonDirectorAsset>();
	auto& Style = Asset->Styles.AddDefaulted_GetRef();
	Style.Selection.Id = Id(1);
	Style.Selection.DisplayName = TEXT("Probability Style");
	Style.Layout.DungeonSize.Value = 24.0;
	Style.Layout.CandidateDensity.Value = 0.32;
	Style.Layout.SidePathPercent.Value = 50.0;
	const TSoftObjectPtr<UMaterialInterface> Material(FSoftObjectPath(TEXT("/Game/Test/ProbabilityMaterial.ProbabilityMaterial")));
	Style.Materials.Floor = Material;
	Style.Materials.Wall = Material;
	Style.Materials.Roof = Material;
	Style.Decals.Mode = EEFCalystoDecalMode::Block;

	FEFCalystoArchitectureEntry Required;
	Required.Selection.Id = Id(2);
	Required.Selection.DisplayName = TEXT("Required Floor");
	Required.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Test/ProbabilityMesh.ProbabilityMesh")));
	Style.Architecture.Floor.Add(Required);
	Required.Selection.Id = Id(3);
	Required.Selection.DisplayName = TEXT("Required Wall");
	Style.Architecture.Wall.Add(Required);
	Required.Selection.Id = Id(4);
	Required.Selection.DisplayName = TEXT("Required Roof");
	Style.Architecture.Roof.Add(Required);

	auto& Theme = Asset->RoomThemes.AddDefaulted_GetRef();
	Theme.Selection.Id = Id(5);
	Theme.Selection.DisplayName = TEXT("Probability Theme");
	Theme.Decals.Mode = EEFCalystoDecalMode::Block;
	return Asset;
}

FEFCalystoContentEntry MakeWorldEntry(const uint32 Value, const double Weight)
{
	FEFCalystoContentEntry Entry;
	Entry.Selection.Id = Id(Value);
	Entry.Selection.DisplayName = TEXT("Stable content label");
	Entry.Selection.Weight = Weight;
	Entry.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/Test/ProbabilityActor.ProbabilityActor_C")));
	Entry.MaximumPerFloor = 1;
	Entry.Placement.FootprintHalfExtent = FVector(20.0, 20.0, 20.0);
	Entry.Placement.Spacing = 0.0;
	Entry.Placement.Clearance = 0.0;
	Entry.Placement.PositionVariationCm = 0.0;
	return Entry;
}

FEFCalystoContentEntry MakeInventoryEntry(const uint32 Value, const double Weight)
{
	FEFCalystoContentEntry Entry = MakeWorldEntry(Value, Weight);
	Entry.ActorClass.Reset();
	Entry.InventoryClass = TSoftClassPtr<UObject>(FSoftObjectPath(TEXT("/Game/Test/ProbabilityInventory.ProbabilityInventory_C")));
	return Entry;
}

FEFCalystoContentGroup MakeGroup(const uint32 Value, const EEFCalystoGameplayRole Role, const double ChancePercent)
{
	FEFCalystoContentGroup Group;
	Group.Id = Id(Value);
	Group.DisplayName = TEXT("Probability group label");
	Group.Role = Role;
	Group.Budget = EEFCalystoBudgetMembership::None;
	Group.Mode = EEFCalystoContentMode::Replace;
	Group.MaximumPerFloor = 1;
	Group.Chance.FirstPercent = ChancePercent;
	Group.Chance.LastPercent = ChancePercent;
	Group.Amount.Distribution = EEFCalystoDistribution::Fixed;
	Group.Amount.Amount = 1;
	return Group;
}

FEFCalystoContentReservationRequest MakeWorldContentRequest(const FGuid StyleId,
	const FGuid FirstEntry, const FGuid SecondEntry, const FGuid FutureEntry)
{
	FEFCalystoContentReservationRequest Request;
	Request.Random.StyleId = StyleId;
	Request.Random.FloorNumber = TestFloor;
	auto& Room = Request.Rooms.AddDefaulted_GetRef();
	Room.RoomId = 1;
	Room.AllowedRoles.Add(EEFCalystoGameplayRole::Prop);
	auto& Surface = Request.Surfaces.AddDefaulted_GetRef();
	Surface.Id = Id(400);
	Surface.RoomId = Room.RoomId;
	Surface.Zone = EEFCalystoPlacementZone::Floor;
	Surface.AvailableHalfExtent = FVector(500.0);
	Surface.AvailableClearanceCm = 100.0;
	Surface.bCollisionValidated = true;
	Surface.bCollisionContractValidated = true;
	Surface.ReservedLocalBounds = FBox(FVector(-100.0), FVector(100.0));
	Surface.CollisionContractHash = TEXT("ProbabilityFixtureCollisionContract");
	Surface.bNavigationValidated = true;
	Surface.AllowedRoles.Add(EEFCalystoGameplayRole::Prop);
	Surface.CompatibleEntryIds.Add(FirstEntry);
	Surface.CompatibleEntryIds.Add(SecondEntry);
	Surface.CompatibleEntryIds.Add(FutureEntry);
	return Request;
}

FEFCalystoContentReservationRequest MakeContainerContentRequest(const FGuid StyleId,
	const FGuid ContainerId, const FGuid FirstEntry, const FGuid SecondEntry, const FGuid FutureEntry)
{
	FEFCalystoContentReservationRequest Request;
	Request.Random.StyleId = StyleId;
	Request.Random.FloorNumber = TestFloor;
	auto& Room = Request.Rooms.AddDefaulted_GetRef();
	Room.RoomId = 1;
	Room.AllowedRoles.Add(EEFCalystoGameplayRole::ContainerContent);
	FEFCalystoExistingContainer& Container = Request.ExistingContainers.AddDefaulted_GetRef();
	Container.ContainerId = ContainerId;
	Container.RoomId = Room.RoomId;
	Container.Capacity.Slots = 1;
	Container.Capacity.CompatibleEntryIds.Add(FirstEntry);
	Container.Capacity.CompatibleEntryIds.Add(SecondEntry);
	Container.Capacity.CompatibleEntryIds.Add(FutureEntry);
	return Request;
}

FEFCalystoArchitectureEntry MakeBakedProposal(const uint32 Value, const double Weight, const TCHAR* Path)
{
	FEFCalystoArchitectureEntry Proposal;
	Proposal.Selection.Id = Id(Value);
	Proposal.Selection.DisplayName = TEXT("Custom PCG proposal label");
	Proposal.Selection.Weight = Weight;
	Proposal.Payload = EEFCalystoArchitecturePayload::BakedPCG;
	Proposal.BakedPCG = TSoftObjectPtr<UObject>(FSoftObjectPath(Path));
	return Proposal;
}

FEFCalystoArchitectureRequest MakeBakedProposalRequest(const FGuid StyleId,
	const FGuid FirstProposal, const FGuid SecondProposal, const FGuid FutureProposal)
{
	FEFCalystoArchitectureRequest Request;
	Request.Random.StyleId = StyleId;
	Request.Random.FloorNumber = TestFloor;
	auto& Room = Request.Rooms.AddDefaulted_GetRef();
	Room.RoomId = 1;
	auto& Opportunity = Request.Opportunities.AddDefaulted_GetRef();
	Opportunity.Id = Id(700);
	Opportunity.RoomId = Room.RoomId;
	Opportunity.Zone = EEFCalystoPlacementZone::Floor;
	const FBox Bounds(FVector(-100.0), FVector(100.0));
	Opportunity.CompatibleEntryBounds.Add(FirstProposal, Bounds);
	Opportunity.CompatibleEntryBounds.Add(SecondProposal, Bounds);
	Opportunity.CompatibleEntryBounds.Add(FutureProposal, Bounds);
	return Request;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDirectorProbabilityFullLaw100000,
	"NoShellForWinter.CalystoDungeon.Director.Probability.FullLaw100000",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoDirectorProbabilityFullLaw100000::RunTest(const FString&)
{
	using namespace EFCalystoDirectorProbabilityFullLawTests;

	// Style selection must normalize only eligible entries. The high-weight entries below
	// are intentionally either future-depth or disabled, so they have exactly zero mass.
	auto* StyleAsset = MakeBaseAsset();
	const FGuid StyleA = Id(1);
	auto StyleB = StyleAsset->Styles[0];
	StyleB.Selection.Id = Id(10);
	StyleB.Selection.Weight = 3.0;
	StyleAsset->Styles[0].Selection.Weight = 2.0;
	auto FutureStyle = StyleAsset->Styles[0];
	FutureStyle.Selection.Id = Id(11);
	FutureStyle.Selection.Weight = 1000.0;
	FutureStyle.Selection.FirstEligibleFloor = static_cast<int32>(TestFloor + 1);
	auto DisabledStyle = StyleAsset->Styles[0];
	DisabledStyle.Selection.Id = Id(12);
	DisabledStyle.Selection.Weight = 1000.0;
	DisabledStyle.Selection.bEnabled = false;
	StyleAsset->Styles.Add(StyleB);
	StyleAsset->Styles.Add(FutureStyle);
	StyleAsset->Styles.Add(DisabledStyle);
	FEFCalystoCompiledDirector StyleConfiguration;
	if (!TestTrue(TEXT("Style probability fixture compiles"), Compile(*this, *StyleAsset, StyleConfiguration))) return false;

	int64 StyleACount = 0, StyleBCount = 0;
	for (int32 Trial = 0; Trial < Trials; ++Trial)
	{
		FEFCalystoRandomKey Key;
		Key.RunSeed = 1000003 + Trial;
		Key.FloorNumber = TestFloor;
		FGuid Selected;
		FString Error;
		if (!StyleConfiguration.SelectStyle(Key, {}, Selected, Error))
		{
			AddError(TEXT("Eligible Style selection failed: ") + Error);
			return false;
		}
		if (Selected == StyleA) ++StyleACount;
		else if (Selected == StyleB.Selection.Id) ++StyleBCount;
		else
		{
			AddError(TEXT("An ineligible Style received probability mass."));
			return false;
		}
	}
	TestEqual(TEXT("Every Style trial selected exactly one eligible Style"), StyleACount + StyleBCount, static_cast<int64>(Trials));
	TestTrue(TEXT("Eligible Style A weight 2/5 follows the independent six-sigma oracle"), WithinSixSigma(StyleACount, Trials, 2.0 / 5.0));
	TestTrue(TEXT("Eligible Style B weight 3/5 follows the independent six-sigma oracle"), WithinSixSigma(StyleBCount, Trials, 3.0 / 5.0));
	FEFCalystoRandomKey StyleBoundaryKey;
	StyleBoundaryKey.RunSeed = 1000003;
	StyleBoundaryKey.FloorNumber = TestFloor;
	FGuid BoundaryStyle;
	FString BoundaryError;
	const TArray<FGuid> OnlyStyleBOnCooldown = {StyleA};
	TestTrue(TEXT("Cooldown eligibility removes Style A before weighted selection"),
		StyleConfiguration.SelectStyle(StyleBoundaryKey, OnlyStyleBOnCooldown, BoundaryStyle, BoundaryError));
	TestEqual(TEXT("The remaining eligible Style is selected exactly"), BoundaryStyle, StyleB.Selection.Id);
	const TArray<FGuid> NoEligibleStyles = {StyleA, StyleB.Selection.Id};
	TestFalse(TEXT("An empty eligible Style table is rejected instead of inventing an outcome"),
		StyleConfiguration.SelectStyle(StyleBoundaryKey, NoEligibleStyles, BoundaryStyle, BoundaryError));

	// This invokes the production reservation planner: feasibility, Chance, Amount,
	// populated rarity, entry Weight, and the frozen reservation all execute in-memory.
	auto* ContentAsset = MakeBaseAsset();
	const FGuid ContentA = Id(100), ContentB = Id(101), ContentFuture = Id(102);
	auto ContentGroup = MakeGroup(200, EEFCalystoGameplayRole::Prop, 40.0);
	ContentGroup.Entries.Add(MakeWorldEntry(100, 2.0));
	ContentGroup.Entries.Add(MakeWorldEntry(101, 3.0));
	auto FutureContent = MakeWorldEntry(102, 1000.0);
	FutureContent.Selection.FirstEligibleFloor = static_cast<int32>(TestFloor + 1);
	ContentGroup.Entries.Add(FutureContent);
	ContentAsset->Styles[0].Content.Add(ContentGroup);
	FEFCalystoCompiledDirector ContentConfiguration;
	if (!TestTrue(TEXT("Content probability fixture compiles"), Compile(*this, *ContentAsset, ContentConfiguration))) return false;
	auto ContentRequest = MakeWorldContentRequest(ContentAsset->Styles[0].Selection.Id, ContentA, ContentB, ContentFuture);
	int64 ContentChanceSuccesses = 0, ContentACount = 0, ContentBCount = 0;
	for (int32 Trial = 0; Trial < Trials; ++Trial)
	{
		ContentRequest.Random.RunSeed = 2000003 + Trial;
		FEFCalystoReservedContentManifest Manifest;
		FEFCalystoContentPlanningReport Report;
		if (!FEFCalystoContentReservationPlanner::Build(ContentConfiguration, ContentRequest, Manifest, Report))
		{
			AddError(TEXT("Content planner failed during probability sampling: ") + Report.Message);
			return false;
		}
		if (!Manifest.IsValid() || Report.Opportunities.Num() != 1 || !Report.Opportunities[0].bChanceRolled)
		{
			AddError(TEXT("Content probability fixture did not reach its single feasible Chance decision."));
			return false;
		}
		const FEFCalystoContentOpportunityReport& Opportunity = Report.Opportunities[0];
		if (Opportunity.Outcome == EEFCalystoContentOpportunityOutcome::ChanceAbsent)
		{
			if (!Manifest.GetElements().IsEmpty())
			{
				AddError(TEXT("A failed content Chance roll produced a frozen reservation."));
				return false;
			}
			continue;
		}
		if (Opportunity.Outcome != EEFCalystoContentOpportunityOutcome::Reserved || Opportunity.SelectedAmount != 1
			|| Opportunity.ReservedAmount != 1 || Manifest.GetElements().Num() != 1)
		{
			AddError(TEXT("A feasible content Chance success did not freeze exactly one reservation."));
			return false;
		}
		++ContentChanceSuccesses;
		const FGuid SelectedEntry = Manifest.GetElements()[0].Entry.Selection.Id;
		if (SelectedEntry == ContentA) ++ContentACount;
		else if (SelectedEntry == ContentB) ++ContentBCount;
		else
		{
			AddError(TEXT("An ineligible content entry received probability mass."));
			return false;
		}
	}
	TestEqual(TEXT("Every successful Content Chance selected exactly one eligible entry"), ContentACount + ContentBCount, ContentChanceSuccesses);
	TestTrue(TEXT("Eligible Content Chance 40% follows the independent six-sigma oracle"),
		WithinSixSigma(ContentChanceSuccesses, Trials, 0.40));
	TestTrue(TEXT("Conditional eligible Content entry A weight 2/5 follows the independent six-sigma oracle"),
		WithinSixSigma(ContentACount, ContentChanceSuccesses, 2.0 / 5.0));
	TestTrue(TEXT("Conditional eligible Content entry B weight 3/5 follows the independent six-sigma oracle"),
		WithinSixSigma(ContentBCount, ContentChanceSuccesses, 3.0 / 5.0));

	// Boundary: a zero Chance stays a real roll with no reservation; a compatibility set
	// containing only a future entry stays ineligible and never reaches a random outcome.
	ContentAsset->Styles[0].Content[0].Chance.FirstPercent = 0.0;
	ContentAsset->Styles[0].Content[0].Chance.LastPercent = 0.0;
	if (!TestTrue(TEXT("Zero-Chance content boundary recompiles"), Compile(*this, *ContentAsset, ContentConfiguration))) return false;
	FEFCalystoReservedContentManifest ContentBoundaryManifest;
	FEFCalystoContentPlanningReport ContentBoundaryReport;
	if (!TestTrue(TEXT("Zero-Chance content request remains a valid empty manifest"),
		FEFCalystoContentReservationPlanner::Build(ContentConfiguration, ContentRequest, ContentBoundaryManifest, ContentBoundaryReport))) return false;
	if (ContentBoundaryReport.Opportunities.Num() != 1)
	{
		AddError(TEXT("Zero-Chance content boundary did not produce its single report."));
		return false;
	}
	TestEqual(TEXT("Zero Content Chance produces an explicit ChanceAbsent outcome"),
		ContentBoundaryReport.Opportunities[0].Outcome, EEFCalystoContentOpportunityOutcome::ChanceAbsent);
	TestTrue(TEXT("Zero Content Chance still represents its one evaluated Chance opportunity"), ContentBoundaryReport.Opportunities[0].bChanceRolled);
	TestTrue(TEXT("Zero Content Chance reserves nothing"), ContentBoundaryManifest.GetElements().IsEmpty());
	ContentRequest.Surfaces[0].CompatibleEntryIds = {ContentFuture};
	if (!TestTrue(TEXT("Future-only content compatibility stays a valid pre-Chance absence"),
		FEFCalystoContentReservationPlanner::Build(ContentConfiguration, ContentRequest, ContentBoundaryManifest, ContentBoundaryReport))) return false;
	if (ContentBoundaryReport.Opportunities.Num() != 1)
	{
		AddError(TEXT("Future-only content boundary did not produce its single report."));
		return false;
	}
	TestEqual(TEXT("Future-only compatibility has no eligible entries"),
		ContentBoundaryReport.Opportunities[0].Outcome, EEFCalystoContentOpportunityOutcome::NoCompatibleEntries);
	TestFalse(TEXT("Future-only compatibility never rolls Chance"), ContentBoundaryReport.Opportunities[0].bChanceRolled);

	// Contents are planned separately for each already-reserved/existing container.
	// This exercises the real inventory-slot reservation path, rather than a generic weighted table.
	auto* ContainerAsset = MakeBaseAsset();
	const FGuid ContainerId = Id(300), ContainerA = Id(301), ContainerB = Id(302), ContainerFuture = Id(303);
	auto ContainerGroup = MakeGroup(304, EEFCalystoGameplayRole::ContainerContent, 55.0);
	ContainerGroup.Entries.Add(MakeInventoryEntry(301, 1.0));
	ContainerGroup.Entries.Add(MakeInventoryEntry(302, 3.0));
	auto FutureInventory = MakeInventoryEntry(303, 1000.0);
	FutureInventory.Selection.FirstEligibleFloor = static_cast<int32>(TestFloor + 1);
	ContainerGroup.Entries.Add(FutureInventory);
	ContainerAsset->Styles[0].Content.Add(ContainerGroup);
	FEFCalystoCompiledDirector ContainerConfiguration;
	if (!TestTrue(TEXT("Container probability fixture compiles"), Compile(*this, *ContainerAsset, ContainerConfiguration))) return false;
	auto ContainerRequest = MakeContainerContentRequest(ContainerAsset->Styles[0].Selection.Id, ContainerId, ContainerA, ContainerB, ContainerFuture);
	int64 ContainerChanceSuccesses = 0, ContainerACount = 0, ContainerBCount = 0;
	for (int32 Trial = 0; Trial < Trials; ++Trial)
	{
		ContainerRequest.Random.RunSeed = 3000003 + Trial;
		FEFCalystoReservedContentManifest Manifest;
		FEFCalystoContentPlanningReport Report;
		if (!FEFCalystoContentReservationPlanner::Build(ContainerConfiguration, ContainerRequest, Manifest, Report))
		{
			AddError(TEXT("Container-content planner failed during probability sampling: ") + Report.Message);
			return false;
		}
		if (!Manifest.IsValid() || Report.Opportunities.Num() != 1 || !Report.Opportunities[0].bChanceRolled)
		{
			AddError(TEXT("Container probability fixture did not reach its single feasible conditional Chance decision."));
			return false;
		}
		const FEFCalystoContentOpportunityReport& Opportunity = Report.Opportunities[0];
		if (Opportunity.Outcome == EEFCalystoContentOpportunityOutcome::ChanceAbsent)
		{
			if (!Manifest.GetElements().IsEmpty())
			{
				AddError(TEXT("A failed container-content Chance roll produced inventory."));
				return false;
			}
			continue;
		}
		if (Opportunity.Outcome != EEFCalystoContentOpportunityOutcome::Reserved || Manifest.GetElements().Num() != 1)
		{
			AddError(TEXT("A container-content Chance success did not freeze exactly one inventory reservation."));
			return false;
		}
		const FEFCalystoReservedContent& Reservation = Manifest.GetElements()[0];
		if (Reservation.Role != EEFCalystoGameplayRole::ContainerContent || Reservation.ParentContainerId != ContainerId
			|| Reservation.InventorySlot != 0 || Manifest.GetFinalUsage().Actors != 0)
		{
			AddError(TEXT("Container contents were not reserved against their exact existing container slot."));
			return false;
		}
		++ContainerChanceSuccesses;
		if (Reservation.Entry.Selection.Id == ContainerA) ++ContainerACount;
		else if (Reservation.Entry.Selection.Id == ContainerB) ++ContainerBCount;
		else
		{
			AddError(TEXT("An ineligible container-content entry received probability mass."));
			return false;
		}
	}
	TestEqual(TEXT("Every successful container-content Chance selected exactly one eligible entry"), ContainerACount + ContainerBCount, ContainerChanceSuccesses);
	TestTrue(TEXT("Conditional container-content Chance 55% follows the independent six-sigma oracle"),
		WithinSixSigma(ContainerChanceSuccesses, Trials, 0.55));
	TestTrue(TEXT("Conditional container-content entry A weight 1/4 follows the independent six-sigma oracle"),
		WithinSixSigma(ContainerACount, ContainerChanceSuccesses, 1.0 / 4.0));
	TestTrue(TEXT("Conditional container-content entry B weight 3/4 follows the independent six-sigma oracle"),
		WithinSixSigma(ContainerBCount, ContainerChanceSuccesses, 3.0 / 4.0));
	ContainerRequest.ExistingContainers[0].Capacity.Slots = 0;
	FEFCalystoReservedContentManifest ContainerBoundaryManifest;
	FEFCalystoContentPlanningReport ContainerBoundaryReport;
	if (!TestTrue(TEXT("Zero-slot container remains a valid pre-Chance absence"),
		FEFCalystoContentReservationPlanner::Build(ContainerConfiguration, ContainerRequest, ContainerBoundaryManifest, ContainerBoundaryReport))) return false;
	if (ContainerBoundaryReport.Opportunities.Num() != 1)
	{
		AddError(TEXT("Zero-slot container boundary did not produce its single report."));
		return false;
	}
	TestEqual(TEXT("Zero-slot container has no compatible inventory proposal"),
		ContainerBoundaryReport.Opportunities[0].Outcome, EEFCalystoContentOpportunityOutcome::NoCompatibleEntries);
	TestFalse(TEXT("Zero-slot container never rolls its contents Chance"), ContainerBoundaryReport.Opportunities[0].bChanceRolled);

	// BakedPCG / Level Instance is the current supported custom-PCG proposal contract.
	// The planner selects bounded proposals only; this test never creates a PCG component or world.
	auto* ProposalAsset = MakeBaseAsset();
	const FGuid ProposalA = Id(600), ProposalB = Id(601), ProposalFuture = Id(602);
	auto& Rule = ProposalAsset->Styles[0].Architecture.Decoration.AddDefaulted_GetRef();
	Rule.Zone = EEFCalystoPlacementZone::Floor;
	Rule.bOverrideDefaultChance = true;
	Rule.Chance.FirstPercent = 60.0;
	Rule.Chance.LastPercent = 60.0;
	Rule.Alternatives.Add(MakeBakedProposal(600, 1.0, TEXT("/Game/Test/ProbabilityPCGA.ProbabilityPCGA")));
	Rule.Alternatives.Add(MakeBakedProposal(601, 3.0, TEXT("/Game/Test/ProbabilityPCGB.ProbabilityPCGB")));
	auto FutureProposal = MakeBakedProposal(602, 1000.0, TEXT("/Game/Test/ProbabilityPCGFuture.ProbabilityPCGFuture"));
	FutureProposal.Selection.FirstEligibleFloor = static_cast<int32>(TestFloor + 1);
	Rule.Alternatives.Add(FutureProposal);
	ProposalAsset->Styles[0].Architecture.MaximumDecorationsPerRoom = 1;
	FEFCalystoCompiledDirector ProposalConfiguration;
	if (!TestTrue(TEXT("Custom-PCG proposal probability fixture compiles"), Compile(*this, *ProposalAsset, ProposalConfiguration))) return false;
	auto ProposalRequest = MakeBakedProposalRequest(ProposalAsset->Styles[0].Selection.Id, ProposalA, ProposalB, ProposalFuture);
	int64 ProposalChanceSuccesses = 0, ProposalACount = 0, ProposalBCount = 0;
	for (int32 Trial = 0; Trial < Trials; ++Trial)
	{
		ProposalRequest.Random.RunSeed = 4000003 + Trial;
		TArray<FEFCalystoArchitectureDecision> Decisions;
		FString Error;
		if (!FEFCalystoArchitecturePlanner::Build(ProposalConfiguration, ProposalRequest, Decisions, Error))
		{
			AddError(TEXT("Custom-PCG proposal planner failed during probability sampling: ") + Error);
			return false;
		}
		if (Decisions.Num() != 1 || !Decisions[0].bChanceRolled)
		{
			AddError(TEXT("Custom-PCG proposal fixture did not reach its single feasible Chance decision."));
			return false;
		}
		const FEFCalystoArchitectureDecision& Decision = Decisions[0];
		if (Decision.Outcome == EEFCalystoArchitectureOutcome::ChanceAbsent) continue;
		if (Decision.Outcome != EEFCalystoArchitectureOutcome::Reserved || Decision.Entry.Payload != EEFCalystoArchitecturePayload::BakedPCG
			|| !Decision.ReservedBounds.IsValid)
		{
			AddError(TEXT("A custom-PCG proposal Chance success did not reserve an exact BakedPCG proposal."));
			return false;
		}
		++ProposalChanceSuccesses;
		if (Decision.Entry.Selection.Id == ProposalA)
		{
			if (Decision.Entry.BakedPCG.ToSoftObjectPath() != FSoftObjectPath(TEXT("/Game/Test/ProbabilityPCGA.ProbabilityPCGA")))
			{
				AddError(TEXT("Custom-PCG proposal A did not retain its frozen payload identity."));
				return false;
			}
			++ProposalACount;
		}
		else if (Decision.Entry.Selection.Id == ProposalB)
		{
			if (Decision.Entry.BakedPCG.ToSoftObjectPath() != FSoftObjectPath(TEXT("/Game/Test/ProbabilityPCGB.ProbabilityPCGB")))
			{
				AddError(TEXT("Custom-PCG proposal B did not retain its frozen payload identity."));
				return false;
			}
			++ProposalBCount;
		}
		else
		{
			AddError(TEXT("An ineligible custom-PCG proposal received probability mass."));
			return false;
		}
	}
	TestEqual(TEXT("Every successful custom-PCG Chance selected exactly one eligible proposal"), ProposalACount + ProposalBCount, ProposalChanceSuccesses);
	TestTrue(TEXT("Eligible custom-PCG proposal Chance 60% follows the independent six-sigma oracle"),
		WithinSixSigma(ProposalChanceSuccesses, Trials, 0.60));
	TestTrue(TEXT("Conditional custom-PCG proposal A weight 1/4 follows the independent six-sigma oracle"),
		WithinSixSigma(ProposalACount, ProposalChanceSuccesses, 1.0 / 4.0));
	TestTrue(TEXT("Conditional custom-PCG proposal B weight 3/4 follows the independent six-sigma oracle"),
		WithinSixSigma(ProposalBCount, ProposalChanceSuccesses, 3.0 / 4.0));
	ProposalRequest.Opportunities[0].CompatibleEntryBounds.Reset();
	TArray<FEFCalystoArchitectureDecision> ProposalBoundaryDecisions;
	FString ProposalBoundaryError;
	if (!TestTrue(TEXT("A custom-PCG opportunity without a supported proposal remains a valid absence"),
		FEFCalystoArchitecturePlanner::Build(ProposalConfiguration, ProposalRequest, ProposalBoundaryDecisions, ProposalBoundaryError))) return false;
	if (ProposalBoundaryDecisions.Num() != 1)
	{
		AddError(TEXT("Unsupported custom-PCG boundary did not produce its single decision."));
		return false;
	}
	TestEqual(TEXT("Unsupported custom-PCG proposals are rejected before Chance"),
		ProposalBoundaryDecisions[0].Outcome, EEFCalystoArchitectureOutcome::NoCompatibleAlternative);
	TestFalse(TEXT("Unsupported custom-PCG proposals never roll Chance"), ProposalBoundaryDecisions[0].bChanceRolled);

	AddInfo(FString::Printf(TEXT("FullLaw100000 used %d in-memory trials per law: Style=2:3, Content Chance=40%%/Entry=2:3, Container Chance=55%%/Entry=1:3, BakedPCG Chance=60%%/Proposal=1:3. No worlds, actors, containers, or PCG components were generated."), Trials));
	return true;
}

#endif
