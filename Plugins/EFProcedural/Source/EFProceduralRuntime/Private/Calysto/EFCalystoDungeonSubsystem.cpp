#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Calysto/EFCalystoDirectorSettings.h"

#include "Async/Async.h"
#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"
#include "Calysto/EFCalystoDungeonHarnessSettings.h"
#include "EFProceduralSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoDungeon, Log, All);

namespace EFCalystoDungeonSubsystemPrivate
{
	constexpr int32 MaximumRecoveryAttempts = 3;

	FString NormalizeMapPackageName(const FString& PackageName)
	{
		FString ShortMapName = FPackageName::GetShortName(PackageName);
		if (ShortMapName.StartsWith(TEXT("UEDPIE_")))
		{
			TArray<FString> Parts;
			ShortMapName.ParseIntoArray(Parts, TEXT("_"), true);
			if (Parts.Num() >= 3)
			{
				Parts.RemoveAt(0, 2);
				ShortMapName = FString::Join(Parts, TEXT("_"));
			}
		}
		const FString LongPackagePath = FPackageName::GetLongPackagePath(PackageName);
		return LongPackagePath.IsEmpty() ? ShortMapName : LongPackagePath + TEXT("/") + ShortMapName;
	}

	int64 CreatePositiveRunSeed()
	{
		const FGuid Guid = FGuid::NewGuid();
		const uint64 Mixed =
			(static_cast<uint64>(Guid.A) << 32) ^ static_cast<uint64>(Guid.B) ^
			(static_cast<uint64>(Guid.C) << 17) ^ static_cast<uint64>(Guid.D);
		return static_cast<int64>((Mixed & static_cast<uint64>(MAX_int64 - 1)) + 1);
	}

	uint64 HashPrefixToUInt64(const FString& Hash)
	{
		return FCString::Strtoui64(*Hash.Left(16), nullptr, 16);
	}

	double UnitLane(const FString& Identity, const TCHAR* Lane)
	{
		const FString Hash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(
			Identity + TEXT("|") + Lane);
		return static_cast<double>(HashPrefixToUInt64(Hash)) /
			static_cast<double>(MAX_uint64);
	}

	int64 BuildFloorSeed(const FEFCalystoDungeonGenerationContextV6& Context)
	{
		const FString Identity = FString::Printf(
			TEXT("CalystoFloorSeedV6|Run=%lld|Floor=%lld|Serial=%lld|Edge=%d|Scenario=%s"),
			static_cast<long long>(Context.RunSeed),
			static_cast<long long>(Context.FloorNumber),
			static_cast<long long>(Context.GenerationSerial),
			Context.DevelopmentForcedDungeonEdge,
			*Context.DevelopmentPopulationScenario.ToString().ToLower());
		return static_cast<int64>(HashPrefixToUInt64(
			FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Identity)) &
			static_cast<uint64>(MAX_int64));
	}

	FEFCalystoFloorOutcomeV6 MakeNeutralOutcome()
	{
		FEFCalystoFloorOutcomeV6 Outcome;
		Outcome.OutcomeHash = FEFCalystoDungeonRuntimeMathV6::ComputeFloorOutcomeHash(Outcome);
		return Outcome;
	}

	FEFCalystoDirectorIntentV6 MakeNeutralDirectorIntent()
	{
		FEFCalystoDirectorIntentV6 Intent;
		Intent.IntentHash = FEFCalystoDungeonRuntimeMathV6::ComputeDirectorIntentHash(Intent);
		return Intent;
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

	bool IsActorDecision(const FEFCalystoPopulationDecisionV6& Decision)
	{
		return Decision.Kind == EEFCalystoPopulationDecisionKindV6::Actor;
	}

	bool IsResourceCategory(const FName CategoryId)
	{
		const FString Category = CategoryId.ToString();
		return Category.Contains(TEXT("Food"), ESearchCase::IgnoreCase) ||
			Category.Contains(TEXT("Loot"), ESearchCase::IgnoreCase) ||
			Category.Contains(TEXT("Chest"), ESearchCase::IgnoreCase) ||
			Category.Contains(TEXT("Potion"), ESearchCase::IgnoreCase) ||
			Category.Contains(TEXT("Water"), ESearchCase::IgnoreCase) ||
			Category.Contains(TEXT("Armor"), ESearchCase::IgnoreCase);
	}

	bool PathsEqual(const TArray<FSoftObjectPath>& Left, const TArray<FSoftObjectPath>& Right)
	{
		TSet<FSoftObjectPath> LeftSet(Left);
		TSet<FSoftObjectPath> RightSet(Right);
		return LeftSet.Num() == RightSet.Num() && LeftSet.Includes(RightSet);
	}
}

bool UEFCalystoDungeonSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && !UEFCalystoDirectorSettings::IsEnabled();
}

void UEFCalystoDungeonSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ActiveContext = FEFCalystoDungeonGenerationContextV6();
	ActiveIntent = FEFCalystoResolvedFloorIntentV6();
	ActiveFloorPlan = FEFCalystoResolvedFloorPlanV6();
	ActiveRoomManifest = FEFCalystoRoomManifestV6();
	ActivePopulationPlan = FEFCalystoPopulationPlanV6();
	ActiveRealizedManifest = FEFCalystoRealizedFloorManifestV6();
	ExpectedRealizedManifest = FEFCalystoRealizedFloorManifestV6();
	ActiveEcology = FEFCalystoRunEcologyStateV6();
	TravelState = EEFCalystoDungeonTravelStateV6::Idle;
	ActiveTravelKind = EEFCalystoDungeonTravelKindV6::None;

	PostWorldInitializationHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(
		this, &UEFCalystoDungeonSubsystem::HandlePostWorldInitialization);
	if (GEngine)
	{
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(
			this, &UEFCalystoDungeonSubsystem::HandleTravelFailure);
	}
	BeginSessionCoreLoad();
}

void UEFCalystoDungeonSubsystem::Deinitialize()
{
	CancelWatchdog();
	if (PostWorldInitializationHandle.IsValid())
	{
		FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitializationHandle);
	}
	if (GEngine && TravelFailureHandle.IsValid())
	{
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	LoadCoordinator.ResetAll();
	SessionCoreObjects.Reset();
	RuntimePolicy = nullptr;
	ClearPendingTravel();
	Super::Deinitialize();
}

FString UEFCalystoDungeonSubsystem::ComputeCanonicalHash(const FString& Text)
{
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Text);
}

FEFCalystoRunEcologyStateV6 UEFCalystoDungeonSubsystem::BuildInitialEcologyV6(
	const int64 RunSeed,
	const FString& PolicyHash,
	const FEFCalystoCompanionRosterSnapshotV6& CompanionRoster)
{
	using namespace EFCalystoDungeonSubsystemPrivate;
	FEFCalystoRunEcologyStateV6 Result;
	Result.bInitialized = true;
	Result.RunDNAHash = ComputeCanonicalHash(FString::Printf(
		TEXT("CalystoRunDNA|Seed=%lld|Policy=%s"),
		static_cast<long long>(RunSeed), *PolicyHash.ToLower()));
	const FString Identity = Result.RunDNAHash;
	Result.Scale = static_cast<float>(UnitLane(Identity, TEXT("Scale")) * 2.0 - 1.0);
	Result.Branching = static_cast<float>(UnitLane(Identity, TEXT("Branching")) * 2.0 - 1.0);
	Result.Threat = static_cast<float>(UnitLane(Identity, TEXT("Threat")) * 2.0 - 1.0);
	Result.Abundance = static_cast<float>(UnitLane(Identity, TEXT("Abundance")) * 2.0 - 1.0);
	Result.Mystery = static_cast<float>(UnitLane(Identity, TEXT("Mystery")) * 2.0 - 1.0);
	Result.PerformanceEMA = 0.5f;
	Result.CompanionRoster = CompanionRoster;
	Result.CompanionRoster.bIsValid = true;
	Result.CompanionRoster.SnapshotHash =
		FEFCalystoDungeonRuntimeMathV6::ComputeCompanionRosterHash(Result.CompanionRoster);
	Result.EcologyHash = FEFCalystoDungeonRuntimeMathV6::ComputeRunEcologyHash(Result);
	return Result;
}

bool UEFCalystoDungeonSubsystem::CommitOutcomeToEcologyV6(
	const FEFCalystoFloorOutcomeV6& Outcome,
	const FEFCalystoResolvedFloorIntentV6& CompletedIntent,
	const FEFCalystoRealizedFloorManifestV6& CompletedManifest,
	FEFCalystoRunEcologyStateV6& InOutEcology,
	FString& OutError)
{
	OutError.Reset();
	if (!FEFCalystoDungeonRuntimeMathV6::ValidateFloorOutcome(Outcome, OutError) ||
		!FEFCalystoDungeonRuntimeMathV6::ValidateResolvedFloorIntent(CompletedIntent, OutError) ||
		!FEFCalystoDungeonRuntimeMathV6::ValidateRealizedFloorManifest(CompletedManifest, OutError) ||
		!FEFCalystoDungeonRuntimeMathV6::ValidateRunEcology(InOutEcology, OutError))
	{
		return false;
	}
	if (CompletedIntent.GenerationContext.FloorNumber <= InOutEcology.LastCommittedFloor ||
		CompletedManifest.IntentHash != CompletedIntent.IntentHash)
	{
		OutError = TEXT("The completed floor is stale or does not match its frozen intent.");
		return false;
	}

	const float Performance = FMath::Clamp(
		(Outcome.Combat + Outcome.Survival + Outcome.Resources + Outcome.Pace +
			(1.0f - Outcome.DeathsAndFailures)) / 5.0f, 0.0f, 1.0f);
	InOutEcology.PerformanceEMA = FMath::Lerp(InOutEcology.PerformanceEMA, Performance, 0.25f);
	const float Delta = (Performance - 0.5f) * 0.16f;
	InOutEcology.Threat = FMath::Clamp(InOutEcology.Threat + Delta, -1.0f, 1.0f);
	InOutEcology.Abundance = FMath::Clamp(
		InOutEcology.Abundance + (0.5f - Outcome.Resources) * 0.10f, -1.0f, 1.0f);
	InOutEcology.Mystery = FMath::Clamp(
		InOutEcology.Mystery + (0.5f - Outcome.Pace) * 0.06f, -1.0f, 1.0f);
	InOutEcology.LastCommittedFloor = CompletedIntent.GenerationContext.FloorNumber;
	++InOutEcology.Revision;
	InOutEcology.RecentStyleIds.Add(CompletedIntent.StyleId);
	while (InOutEcology.RecentStyleIds.Num() > 4)
	{
		InOutEcology.RecentStyleIds.RemoveAt(0);
	}

	bool bFoundFood = false;
	bool bFoundChest = false;
	for (const FEFCalystoRealizedPopulationActorRecordV6& Actor : CompletedManifest.Actors)
	{
		const FString Category = Actor.CategoryId.ToString();
		bFoundFood |= Category.Contains(TEXT("Food"), ESearchCase::IgnoreCase);
		bFoundChest |= Category.Contains(TEXT("Chest"), ESearchCase::IgnoreCase);
		if (Actor.CooldownFloors > 0)
		{
			FEFCalystoCooldownStateV6& Cooldown = InOutEcology.Cooldowns.AddDefaulted_GetRef();
			Cooldown.StableId = Actor.CatalogEntryId;
			Cooldown.LastSelectedFloor = CompletedIntent.GenerationContext.FloorNumber;
			Cooldown.CooldownFloors = Actor.CooldownFloors;
		}
	}
	InOutEcology.Cooldowns.Sort([](const FEFCalystoCooldownStateV6& Left, const FEFCalystoCooldownStateV6& Right)
	{
		return Left.StableId.ToString().ToLower() < Right.StableId.ToString().ToLower();
	});
	for (int32 Index = InOutEcology.Cooldowns.Num() - 1; Index > 0; --Index)
	{
		if (InOutEcology.Cooldowns[Index].StableId.IsEqual(
			InOutEcology.Cooldowns[Index - 1].StableId, ENameCase::IgnoreCase))
		{
			InOutEcology.Cooldowns.RemoveAt(Index);
		}
	}
	InOutEcology.ConsecutiveFloorsWithoutFood = bFoundFood
		? 0 : InOutEcology.ConsecutiveFloorsWithoutFood + 1;
	InOutEcology.ConsecutiveFloorsWithoutChest = bFoundChest
		? 0 : InOutEcology.ConsecutiveFloorsWithoutChest + 1;
	InOutEcology.CompanionRoster = CompletedIntent.CompanionRoster;
	InOutEcology.EcologyHash = FEFCalystoDungeonRuntimeMathV6::ComputeRunEcologyHash(InOutEcology);
	return FEFCalystoDungeonRuntimeMathV6::ValidateRunEcology(InOutEcology, OutError);
}

bool UEFCalystoDungeonSubsystem::ResolveFloorIntentForTestingV6(
	const UEFCalystoDungeonDirectorPolicyV6Asset* Policy,
	const FEFCalystoDungeonGenerationContextV6& Context,
	const FEFCalystoDirectorIntentV6& DirectorIntent,
	const FEFCalystoFloorOutcomeV6& Outcome,
	const FEFCalystoRunEcologyStateV6& Ecology,
	const FEFCalystoCompanionRosterSnapshotV6& CompanionRoster,
	FEFCalystoResolvedFloorIntentV6& OutIntent,
	FString& OutError)
{
	OutIntent = FEFCalystoResolvedFloorIntentV6();
	OutError.Reset();
	if (!Policy || !Policy->Validate(OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("A valid resident Director policy is required.");
		}
		return false;
	}
	if (!Context.PolicyHash.Equals(Policy->GetGameplayHash(), ESearchCase::IgnoreCase))
	{
		OutError = TEXT("The generation context policy hash does not match the supplied policy.");
		return false;
	}

	FEFCalystoResolvedFloorPlanV6 Plan;
	const int64 FloorSeed = EFCalystoDungeonSubsystemPrivate::BuildFloorSeed(Context);
	const bool bPlanBuilt = DirectorIntent.bHasPreferredStyle
		? Policy->BuildResolvedFloorPlanForStyle(
			FloorSeed, DirectorIntent.PreferredStyleId, Plan, OutError)
		: Policy->BuildResolvedFloorPlan(FloorSeed, Plan, OutError);
	if (!bPlanBuilt)
	{
		return false;
	}
	const FEFCalystoStyleProfileV6* Style = Policy->FindStyle(Plan.StyleId);
	if (!Style)
	{
		OutError = TEXT("The selected Style is absent from the resident policy.");
		return false;
	}
	return FEFCalystoDungeonRuntimeMathV6::BuildResolvedFloorIntent(
		Context, DirectorIntent, Outcome, Ecology, CompanionRoster,
		Plan, Style->Lighting, OutIntent, OutError);
}

bool UEFCalystoDungeonSubsystem::BuildRealizedFloorManifestV6(
	const FEFCalystoDungeonGenerationContextV6& Context,
	const FEFCalystoResolvedFloorIntentV6& Intent,
	const FEFCalystoResolvedFloorPlanV6& FloorPlan,
	const FEFCalystoRoomManifestV6& RoomManifest,
	const FEFCalystoPopulationPlanV6& PopulationPlan,
	const FString& MaterializationHash,
	const int32 CandidateAnchorCount,
	const TConstArrayView<FEFCalystoRealizedPopulationActorRecordV6> RealizedActors,
	FEFCalystoRealizedFloorManifestV6& OutManifest,
	FString& OutError)
{
	using namespace EFCalystoDungeonSubsystemPrivate;
	OutManifest = FEFCalystoRealizedFloorManifestV6();
	OutError.Reset();
	auto Fail = [&OutError](const FString& Message)
	{
		OutError = Message;
		return false;
	};
	if (!FEFCalystoDungeonRuntimeMathV6::ValidateGenerationContext(Context, OutError) ||
		!FEFCalystoDungeonRuntimeMathV6::ValidateResolvedFloorIntent(Intent, OutError))
	{
		return false;
	}
	if (Context.RunSeed != Intent.GenerationContext.RunSeed ||
		Context.FloorNumber != Intent.GenerationContext.FloorNumber ||
		Context.GenerationSerial != Intent.GenerationContext.GenerationSerial ||
		Context.RunEpoch != Intent.GenerationContext.RunEpoch ||
		!Context.ContextHash.Equals(Intent.GenerationContext.ContextHash, ESearchCase::IgnoreCase) ||
		!FloorPlan.FloorPlanHash.Equals(Intent.FloorPlan.FloorPlanHash, ESearchCase::IgnoreCase) ||
		!FloorPlan.StyleId.IsEqual(Intent.StyleId, ENameCase::IgnoreCase) ||
		RoomManifest.FloorSeed != FloorPlan.FloorSeed ||
		!RoomManifest.StyleId.IsEqual(FloorPlan.StyleId, ENameCase::IgnoreCase) ||
		!RoomManifest.FloorPlanHash.Equals(FloorPlan.FloorPlanHash, ESearchCase::IgnoreCase) ||
		!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(RoomManifest.ManifestHash) ||
		PopulationPlan.FloorSeed != FloorPlan.FloorSeed ||
		PopulationPlan.FloorNumber != Context.FloorNumber ||
		!PopulationPlan.StyleId.IsEqual(FloorPlan.StyleId, ENameCase::IgnoreCase) ||
		!PopulationPlan.FloorPlanHash.Equals(FloorPlan.FloorPlanHash, ESearchCase::IgnoreCase) ||
		!PopulationPlan.RoomManifestHash.Equals(RoomManifest.ManifestHash, ESearchCase::IgnoreCase) ||
		!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(PopulationPlan.PopulationHash) ||
		!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(MaterializationHash))
	{
		return Fail(TEXT("Realized population evidence does not belong to one canonical frozen V6 floor identity."));
	}
	if (CandidateAnchorCount < 0 || CandidateAnchorCount < RealizedActors.Num() ||
		PopulationPlan.ActorDecisionCount < 0 || PopulationPlan.ChestContentDecisionCount < 0 ||
		RealizedActors.Num() != PopulationPlan.ActorDecisionCount ||
		PopulationPlan.Rooms.Num() != RoomManifest.Rooms.Num())
	{
		return Fail(TEXT("Realized population evidence contains impossible room, anchor, actor, or chest-content counts."));
	}

	TMap<int64, const FEFCalystoRoomContextV6*> ManifestRooms;
	for (const FEFCalystoRoomContextV6& Room : RoomManifest.Rooms)
	{
		if (Room.StableRoomId <= 0 || ManifestRooms.Contains(Room.StableRoomId))
		{
			return Fail(TEXT("The frozen Room Manifest contains a zero or duplicate Stable Room ID."));
		}
		ManifestRooms.Add(Room.StableRoomId, &Room);
	}

	TMap<FString, const FEFCalystoPopulationDecisionV6*> ActorDecisions;
	TMap<FString, TArray<FString>> ExpectedContentsByParent;
	TSet<FString> AllDecisionIds;
	int32 CountedActorDecisions = 0;
	int32 CountedContentDecisions = 0;
	for (const FEFCalystoRoomPopulationPlanV6& RoomPlan : PopulationPlan.Rooms)
	{
		const FEFCalystoRoomContextV6* const* RoomPtr = ManifestRooms.Find(RoomPlan.StableRoomId);
		const FEFCalystoRoomContextV6* Room = RoomPtr ? *RoomPtr : nullptr;
		if (!Room || RoomPlan.ThemeId != Room->ThemeId ||
			!RoomPlan.EffectiveCatalogHash.Equals(Room->CatalogHash, ESearchCase::IgnoreCase) ||
			!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(RoomPlan.RoomPopulationHash) ||
			(FEFCalystoDungeonDirectorMathV6::IsProtectedRoom(Room->RoomFlags) &&
				!RoomPlan.Decisions.IsEmpty()))
		{
			return Fail(TEXT("The population plan no longer matches its frozen room records."));
		}
		for (const FEFCalystoPopulationDecisionV6& Decision : RoomPlan.Decisions)
		{
			const FString DecisionKey = Decision.DecisionId.ToLower();
			if (!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Decision.DecisionId) ||
				AllDecisionIds.Contains(DecisionKey) ||
				Decision.StableRoomId != RoomPlan.StableRoomId ||
				!Decision.StyleId.IsEqual(PopulationPlan.StyleId, ENameCase::IgnoreCase) ||
				Decision.ThemeId != RoomPlan.ThemeId || Decision.CategoryId.IsNone() ||
				Decision.EntryId.IsNone() || !Decision.ClassPath.IsValid() ||
				Decision.SpawnOrdinal < 0 || !FMath::IsFinite(Decision.ThreatCost) ||
				Decision.ThreatCost < 0.0f)
			{
				return Fail(TEXT("The population plan contains an incomplete, duplicate, or non-finite decision."));
			}
			AllDecisionIds.Add(DecisionKey);
			if (IsActorDecision(Decision))
			{
				if (!Decision.ParentDecisionId.IsEmpty())
				{
					return Fail(TEXT("An actor population decision unexpectedly has a parent."));
				}
				ActorDecisions.Add(DecisionKey, &Decision);
				++CountedActorDecisions;
			}
			else
			{
				const FString ParentKey = Decision.ParentDecisionId.ToLower();
				if (!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Decision.ParentDecisionId))
				{
					return Fail(TEXT("A chest-content decision has no canonical parent decision."));
				}
				ExpectedContentsByParent.FindOrAdd(ParentKey).Add(DecisionKey);
				++CountedContentDecisions;
			}
		}
	}
	if (CountedActorDecisions != PopulationPlan.ActorDecisionCount ||
		CountedContentDecisions != PopulationPlan.ChestContentDecisionCount)
	{
		return Fail(TEXT("The population plan decision counts drifted from its frozen summary."));
	}
	for (TPair<FString, TArray<FString>>& Pair : ExpectedContentsByParent)
	{
		const FEFCalystoPopulationDecisionV6* const* ParentPtr = ActorDecisions.Find(Pair.Key);
		const FEFCalystoPopulationDecisionV6* Parent = ParentPtr ? *ParentPtr : nullptr;
		if (!Parent || !Parent->CategoryId.IsEqual(TEXT("Chest"), ENameCase::IgnoreCase))
		{
			return Fail(TEXT("A chest-content decision does not reference a frozen chest actor."));
		}
		Pair.Value.Sort();
	}

	FEFCalystoRealizedFloorManifestV6 Manifest;
	Manifest.bIsValid = true;
	Manifest.RunSeed = Context.RunSeed;
	Manifest.FloorNumber = Context.FloorNumber;
	Manifest.GenerationSerial = Context.GenerationSerial;
	Manifest.StyleId = Intent.StyleId;
	Manifest.IntentHash = Intent.IntentHash;
	Manifest.FloorPlanHash = FloorPlan.FloorPlanHash;
	Manifest.RoomManifestHash = RoomManifest.ManifestHash;
	Manifest.AnchorTopologyHash = MaterializationHash;
	Manifest.PopulationPlanHash = PopulationPlan.PopulationHash;
	Manifest.CompanionSnapshotHash = Intent.CompanionRoster.SnapshotHash;
	Manifest.CandidateAnchorCount = CandidateAnchorCount;
	Manifest.SpawnedActorCount = RealizedActors.Num();

	TSet<FString> RealizedActorIds;
	int32 VerifiedContentCount = 0;
	for (const FEFCalystoRealizedPopulationActorRecordV6& Evidence : RealizedActors)
	{
		const FString ActorKey = Evidence.StableActorId.ToString().ToLower();
		const FEFCalystoPopulationDecisionV6* const* DecisionPtr =
			ActorDecisions.Find(ActorKey);
		const FEFCalystoPopulationDecisionV6* Decision = DecisionPtr ? *DecisionPtr : nullptr;
		const float ExpectedResourceCost = IsResourceCategory(
			Decision ? Decision->CategoryId : NAME_None) ? 1.0f : 0.0f;
		if (!Decision || RealizedActorIds.Contains(ActorKey) ||
			Evidence.StableRoomId != Decision->StableRoomId ||
			!Evidence.CategoryId.IsEqual(Decision->CategoryId, ENameCase::IgnoreCase) ||
			!Evidence.CatalogEntryId.IsEqual(Decision->EntryId, ENameCase::IgnoreCase) ||
			Evidence.ActorClass.ToSoftObjectPath() != Decision->ClassPath ||
			Evidence.Tier != Decision->Tier || Evidence.Lifecycle != Decision->Lifecycle ||
			Evidence.Transform.ContainsNaN() || Evidence.LogicalLevel != 0 ||
			Evidence.CooldownFloors != 0 || Evidence.StableCompanionId.IsValid() ||
			Evidence.ThreatCost != FMath::Max(0.0f, Decision->ThreatCost) ||
			Evidence.ResourceCost != ExpectedResourceCost)
		{
			return Fail(TEXT("A realized actor record differs from its frozen decision or contains unsupported fabricated facts."));
		}
		RealizedActorIds.Add(ActorKey);

		TArray<FString> ActualContents;
		TSet<FString> UniqueContents;
		for (const FName ContentId : Evidence.VerifiedChestContentIds)
		{
			const FString ContentKey = ContentId.ToString().ToLower();
			if (!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(ContentKey) ||
				UniqueContents.Contains(ContentKey))
			{
				return Fail(TEXT("A realized chest contains an invalid or duplicate verified content decision."));
			}
			UniqueContents.Add(ContentKey);
			ActualContents.Add(ContentKey);
		}
		ActualContents.Sort();
		const TArray<FString>* ExpectedContents = ExpectedContentsByParent.Find(ActorKey);
		const TArray<FString> EmptyContents;
		if (ActualContents != (ExpectedContents ? *ExpectedContents : EmptyContents))
		{
			return Fail(TEXT("Realized chest contents differ from the immutable parented content decisions."));
		}
		VerifiedContentCount += ActualContents.Num();

		Manifest.Actors.Add(Evidence);
		Manifest.RealizedThreatCost += Evidence.ThreatCost;
		Manifest.RealizedResourceCost += Evidence.ResourceCost;
	}
	if (RealizedActorIds.Num() != ActorDecisions.Num() ||
		VerifiedContentCount != PopulationPlan.ChestContentDecisionCount)
	{
		return Fail(TEXT("Realized population evidence omits one or more frozen actor or chest-content decisions."));
	}

	Manifest.ManifestHash =
		FEFCalystoDungeonRuntimeMathV6::ComputeRealizedFloorManifestHash(Manifest);
	if (!FEFCalystoDungeonRuntimeMathV6::ValidateRealizedFloorManifest(Manifest, OutError))
	{
		return false;
	}
	OutManifest = MoveTemp(Manifest);
	return true;
}

FEFCalystoDungeonSnapshotV6 UEFCalystoDungeonSubsystem::GetSnapshot() const
{
	return BuildSnapshot();
}

FEFCalystoResolvedFloorIntentV6 UEFCalystoDungeonSubsystem::GetResolvedFloorIntent() const
{
	return ActiveIntent;
}

FEFCalystoResolvedFloorPlanV6 UEFCalystoDungeonSubsystem::GetResolvedFloorPlanV6() const
{
	return ActiveFloorPlan;
}

FEFCalystoRoomManifestV6 UEFCalystoDungeonSubsystem::GetRoomManifestV6() const
{
	return ActiveRoomManifest;
}

FEFCalystoRealizedFloorManifestV6 UEFCalystoDungeonSubsystem::GetRealizedFloorManifest() const
{
	return ActiveRealizedManifest;
}

FEFCalystoRunEcologyStateV6 UEFCalystoDungeonSubsystem::GetRunEcology() const
{
	return ActiveEcology;
}

FEFCalystoDirectorIntentV6 UEFCalystoDungeonSubsystem::GetNextFloorDirectorIntent() const
{
	return bHasQueuedDirectorIntent
		? QueuedDirectorIntent
		: EFCalystoDungeonSubsystemPrivate::MakeNeutralDirectorIntent();
}

bool UEFCalystoDungeonSubsystem::SetNextFloorDirectorIntent(
	const FEFCalystoDirectorIntentV6& NewIntent)
{
	if (bTravelRequestPending)
	{
		return false;
	}
	FEFCalystoDirectorIntentV6 Candidate = NewIntent;
	Candidate.IntentHash = FEFCalystoDungeonRuntimeMathV6::ComputeDirectorIntentHash(Candidate);
	FString Error;
	if (!FEFCalystoDungeonRuntimeMathV6::ValidateDirectorIntent(Candidate, Error))
	{
		SetFailure(TEXT("DIRECTOR_INTENT_INVALID"), Error);
		return false;
	}
	QueuedDirectorIntent = MoveTemp(Candidate);
	bHasQueuedDirectorIntent = true;
	return true;
}

void UEFCalystoDungeonSubsystem::ClearNextFloorDirectorIntent()
{
	if (!bTravelRequestPending)
	{
		QueuedDirectorIntent = FEFCalystoDirectorIntentV6();
		bHasQueuedDirectorIntent = false;
	}
}

bool UEFCalystoDungeonSubsystem::SubmitFloorOutcome(const FEFCalystoFloorOutcomeV6& Outcome)
{
	if (!bHasActiveRun || TravelState != EEFCalystoDungeonTravelStateV6::Ready)
	{
		return false;
	}
	FEFCalystoFloorOutcomeV6 Candidate = Outcome;
	Candidate.OutcomeHash = FEFCalystoDungeonRuntimeMathV6::ComputeFloorOutcomeHash(Candidate);
	FString Error;
	if (!FEFCalystoDungeonRuntimeMathV6::ValidateFloorOutcome(Candidate, Error))
	{
		SetFailure(TEXT("FLOOR_OUTCOME_INVALID"), Error);
		return false;
	}
	QueuedFloorOutcome = MoveTemp(Candidate);
	bHasQueuedFloorOutcome = true;
	return true;
}

bool UEFCalystoDungeonSubsystem::SubmitCompanionRunSnapshot(
	const FEFCalystoCompanionRosterSnapshotV6& Snapshot)
{
	if (!bPreparationBroadcastActive)
	{
		return false;
	}
	FEFCalystoCompanionRosterSnapshotV6 Candidate = Snapshot;
	Candidate.RunEpoch = PendingRunEpoch;
	Candidate.SnapshotHash = FEFCalystoDungeonRuntimeMathV6::ComputeCompanionRosterHash(Candidate);
	FString Error;
	if (!FEFCalystoDungeonRuntimeMathV6::ValidateCompanionRoster(Candidate, Error))
	{
		PreparationFailureCode = TEXT("COMPANION_SNAPSHOT_INVALID");
		PreparationFailureMessage = Error;
		bPreparationFailed = true;
		return false;
	}
	QueuedCompanionRoster = MoveTemp(Candidate);
	bHasQueuedCompanionRoster = true;
	return true;
}

bool UEFCalystoDungeonSubsystem::HasActiveRun() const { return bHasActiveRun; }
int64 UEFCalystoDungeonSubsystem::GetCurrentFloor() const
{
	return bHasActiveRun ? ActiveContext.FloorNumber : 0;
}
int64 UEFCalystoDungeonSubsystem::GetRunSeed() const
{
	return bHasActiveRun ? ActiveContext.RunSeed : 0;
}
int64 UEFCalystoDungeonSubsystem::GetRunEpoch() const { return RunEpoch; }
bool UEFCalystoDungeonSubsystem::IsTravelRequestPending() const { return bTravelRequestPending; }

bool UEFCalystoDungeonSubsystem::IsGenerationBootstrapCurrentV6(
	const FString& ExpectedIntentHash) const
{
	FString Error;
	return !ExpectedIntentHash.IsEmpty()
		&& TravelState == EEFCalystoDungeonTravelStateV6::Generating
		&& bHasActiveRun
		&& !bTravelRequestPending
		&& ActiveIntent.bIsValid
		&& ActiveIntent.IntentHash.Equals(ExpectedIntentHash, ESearchCase::IgnoreCase)
		&& ValidateActiveIdentity(Error);
}

bool UEFCalystoDungeonSubsystem::RequestStartNewRun()
{
	return RequestStartNewRunWithSeed(EFCalystoDungeonSubsystemPrivate::CreatePositiveRunSeed());
}

bool UEFCalystoDungeonSubsystem::RequestStartNewRunWithSeed(const int64 NewRunSeed)
{
	if (NewRunSeed == 0 || bTravelRequestPending)
	{
		return false;
	}
	FEFCalystoDungeonGenerationContextV6 Context;
	Context.RunSeed = NewRunSeed;
	Context.FloorNumber = 1;
	Context.GenerationSerial = 0;
#if WITH_DEV_AUTOMATION_TESTS
	Context.DevelopmentForcedDungeonEdge = AutomationForcedDungeonEdge;
	Context.DevelopmentPopulationScenario = AutomationPopulationScenario;
#endif
	return BeginTravel(Context, EEFCalystoDungeonTravelKindV6::NewRun);
}

bool UEFCalystoDungeonSubsystem::RequestAdvanceFloor()
{
	if (!bHasActiveRun || bTravelRequestPending ||
		TravelState != EEFCalystoDungeonTravelStateV6::Ready)
	{
		return false;
	}
	BeforeFloorAdvanceEvent.Broadcast(ActiveContext.FloorNumber, ActiveIntent);
	FEFCalystoDungeonGenerationContextV6 Context = ActiveContext;
	++Context.FloorNumber;
	++Context.GenerationSerial;
	return BeginTravel(Context, EEFCalystoDungeonTravelKindV6::Advance);
}

bool UEFCalystoDungeonSubsystem::RequestRerollCurrentFloor()
{
	if (!bHasActiveRun || bTravelRequestPending ||
		TravelState != EEFCalystoDungeonTravelStateV6::Ready)
	{
		return false;
	}
	FEFCalystoDungeonGenerationContextV6 Context = ActiveContext;
	++Context.GenerationSerial;
	return BeginTravel(Context, EEFCalystoDungeonTravelKindV6::Reroll);
}

bool UEFCalystoDungeonSubsystem::RequestReplayCurrentFloor()
{
	if (!bHasActiveRun || bTravelRequestPending ||
		TravelState == EEFCalystoDungeonTravelStateV6::Preloading ||
		TravelState == EEFCalystoDungeonTravelStateV6::Traveling)
	{
		return false;
	}
	return BeginTravel(ActiveContext, EEFCalystoDungeonTravelKindV6::Replay);
}

bool UEFCalystoDungeonSubsystem::RequestTravelToFloor(const int64 TargetFloor)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	if (!bHasActiveRun || bTravelRequestPending || TargetFloor < 1)
	{
		return false;
	}
	FEFCalystoDungeonGenerationContextV6 Context = ActiveContext;
	Context.FloorNumber = TargetFloor;
	++Context.GenerationSerial;
	return BeginTravel(Context, EEFCalystoDungeonTravelKindV6::DevelopmentJump);
#endif
}

bool UEFCalystoDungeonSubsystem::BeginTravel(
	const FEFCalystoDungeonGenerationContextV6& Context,
	const EEFCalystoDungeonTravelKindV6 TravelKind)
{
	if (bTravelRequestPending || TravelKind == EEFCalystoDungeonTravelKindV6::None ||
		TravelKind == EEFCalystoDungeonTravelKindV6::RecoverToHub)
	{
		return false;
	}

	UWorld* SourceWorld = GetWorld();
	if (!IsValid(SourceWorld) || !SourceWorld->IsGameWorld())
	{
		SetFailure(TEXT("SOURCE_WORLD_INVALID"), TEXT("Calysto travel requires a live game world."));
		return false;
	}

	PendingContext = Context;
	PendingTravelKind = TravelKind;
	PendingRunEpoch = (TravelKind == EEFCalystoDungeonTravelKindV6::NewRun ||
		TravelKind == EEFCalystoDungeonTravelKindV6::RestartSameSeed)
		? RunEpoch + 1 : RunEpoch;
	PendingContext.RunEpoch = PendingRunEpoch;
	PendingContext.TravelKind = TravelKind;
	PendingTransactionId = ++NextTransactionId;
	SourceTravelState = TravelState;
	SourceWorldPackage = FName(*EFCalystoDungeonSubsystemPrivate::NormalizeMapPackageName(
		SourceWorld->GetPackage()->GetName()));
	if (!IsConfiguredDungeonWorld(SourceWorld))
	{
		ReturnWorldPackage = SourceWorldPackage;
	}
	bTravelRequestPending = true;
	bOpenLevelIssued = false;
	bPreparationFailed = false;
	bPreparationBroadcastActive = true;
	PreparationFailureCode = NAME_None;
	PreparationFailureMessage.Reset();
	TravelState = EEFCalystoDungeonTravelStateV6::Preloading;

	BeforeAnyDirectorTravelEvent.Broadcast(TravelKind);
	bPreparationBroadcastActive = false;
	if (bPreparationFailed)
	{
		RejectPendingTravel(
			PreparationFailureCode.IsNone() ? FName(TEXT("PREPARATION_FAILED")) : PreparationFailureCode,
			PreparationFailureMessage.IsEmpty()
				? TEXT("A pre-travel adapter rejected the transaction.")
				: PreparationFailureMessage,
			false);
		return false;
	}

	if (!bHasQueuedCompanionRoster)
	{
		QueuedCompanionRoster = ActiveEcology.bInitialized
			? ActiveEcology.CompanionRoster
			: EFCalystoDungeonSubsystemPrivate::MakeEmptyRoster(PendingRunEpoch);
		QueuedCompanionRoster.RunEpoch = PendingRunEpoch;
		QueuedCompanionRoster.SnapshotHash =
			FEFCalystoDungeonRuntimeMathV6::ComputeCompanionRosterHash(QueuedCompanionRoster);
		bHasQueuedCompanionRoster = true;
	}

	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	ArmWatchdog(Settings ? Settings->TravelWatchdogSeconds : 30.0);
	if (bSessionCoreReady)
	{
		FString Error;
		if (!PreparePendingIntent(Error) || !BeginPendingFloorVisualLoad(Error))
		{
			RejectPendingTravel(TEXT("PRETRAVEL_RESOLUTION_FAILED"), Error, false);
			return false;
		}
	}
	return true;
}

void UEFCalystoDungeonSubsystem::ReportDirectorTravelPreparationFailure(
	const FName FailureCode, const FString& FailureMessage)
{
	if (!bPreparationBroadcastActive)
	{
		return;
	}
	bPreparationFailed = true;
	PreparationFailureCode = FailureCode.IsNone() ? FName(TEXT("PREPARATION_FAILED")) : FailureCode;
	PreparationFailureMessage = FailureMessage;
}

bool UEFCalystoDungeonSubsystem::PreparePendingIntent(FString& OutError)
{
	OutError.Reset();
	if (!bTravelRequestPending || !RuntimePolicy || RuntimePolicyHash.IsEmpty())
	{
		OutError = TEXT("A resident V6 policy and pending transaction are required.");
		return false;
	}

	PendingContext.PolicyHash = RuntimePolicyHash;
	PendingContext.ContextHash =
		FEFCalystoDungeonRuntimeMathV6::ComputeGenerationContextHash(PendingContext);
	bPendingConsumesDirectorIntent = false;
	bPendingConsumesFloorOutcome = false;
	bPendingConsumesCompanionRoster = false;
	PendingExpectedManifest = FEFCalystoRealizedFloorManifestV6();

	if (PendingTravelKind == EEFCalystoDungeonTravelKindV6::Replay)
	{
		if (!ActiveIntent.bIsValid)
		{
			OutError = TEXT("Replay requires an immutable active floor intent.");
			return false;
		}
		PendingContext = ActiveIntent.GenerationContext;
		PendingIntent = ActiveIntent;
		PendingFloorPlan = ActiveFloorPlan;
		PendingEcology = ActiveEcology;
		PendingExpectedManifest = ActiveRealizedManifest;
		bPendingConsumesCompanionRoster = true;
		return true;
	}

	FEFCalystoCompanionRosterSnapshotV6 Roster = QueuedCompanionRoster;
	Roster.RunEpoch = PendingRunEpoch;
	Roster.SnapshotHash = FEFCalystoDungeonRuntimeMathV6::ComputeCompanionRosterHash(Roster);
	if (!FEFCalystoDungeonRuntimeMathV6::ValidateCompanionRoster(Roster, OutError))
	{
		return false;
	}
	bPendingConsumesCompanionRoster = true;

	if (PendingTravelKind == EEFCalystoDungeonTravelKindV6::NewRun ||
		PendingTravelKind == EEFCalystoDungeonTravelKindV6::RestartSameSeed)
	{
		PendingEcology = BuildInitialEcologyV6(PendingContext.RunSeed, RuntimePolicyHash, Roster);
	}
	else
	{
		PendingEcology = ActiveEcology;
		if (PendingTravelKind == EEFCalystoDungeonTravelKindV6::Advance)
		{
			const FEFCalystoFloorOutcomeV6 Outcome = bHasQueuedFloorOutcome
				? QueuedFloorOutcome
				: EFCalystoDungeonSubsystemPrivate::MakeNeutralOutcome();
			if (!CommitOutcomeToEcologyV6(
				Outcome, ActiveIntent, ActiveRealizedManifest, PendingEcology, OutError))
			{
				return false;
			}
			bPendingConsumesFloorOutcome = bHasQueuedFloorOutcome;
		}
		if (PendingTravelKind == EEFCalystoDungeonTravelKindV6::DevelopmentJump)
		{
			PendingEcology.bDevelopmentSyntheticHistory = true;
		}
		PendingEcology.CompanionRoster = Roster;
		PendingEcology.EcologyHash =
			FEFCalystoDungeonRuntimeMathV6::ComputeRunEcologyHash(PendingEcology);
	}

	const FEFCalystoDirectorIntentV6 DirectorIntent = bHasQueuedDirectorIntent
		? QueuedDirectorIntent
		: EFCalystoDungeonSubsystemPrivate::MakeNeutralDirectorIntent();
	bPendingConsumesDirectorIntent = bHasQueuedDirectorIntent;
	const FEFCalystoFloorOutcomeV6 FrozenOutcome =
		PendingTravelKind == EEFCalystoDungeonTravelKindV6::Advance && bHasQueuedFloorOutcome
			? QueuedFloorOutcome
			: EFCalystoDungeonSubsystemPrivate::MakeNeutralOutcome();

	const int64 FloorSeed = EFCalystoDungeonSubsystemPrivate::BuildFloorSeed(PendingContext);
	const bool bPlanBuilt = DirectorIntent.bHasPreferredStyle
		? RuntimePolicy->BuildResolvedFloorPlanForStyle(
			FloorSeed, DirectorIntent.PreferredStyleId, PendingFloorPlan, OutError)
		: RuntimePolicy->BuildResolvedFloorPlan(FloorSeed, PendingFloorPlan, OutError);
	if (!bPlanBuilt)
	{
		return false;
	}
	const FEFCalystoStyleProfileV6* Style = RuntimePolicy->FindStyle(PendingFloorPlan.StyleId);
	if (!Style)
	{
		OutError = TEXT("The selected Style is absent from the compiled policy.");
		return false;
	}
	return FEFCalystoDungeonRuntimeMathV6::BuildResolvedFloorIntent(
		PendingContext, DirectorIntent, FrozenOutcome, PendingEcology, Roster,
		PendingFloorPlan, Style->Lighting, PendingIntent, OutError);
}

bool UEFCalystoDungeonSubsystem::BeginPendingFloorVisualLoad(FString& OutError)
{
	OutError.Reset();
	if (!bTravelRequestPending || !RuntimePolicy || !PendingIntent.bIsValid)
	{
		OutError = TEXT("Floor Visual loading requires a frozen pending intent.");
		return false;
	}
	TArray<FSoftObjectPath> Paths;
	if (!RuntimePolicy->GatherReachableVisualPreloadPaths(PendingFloorPlan, Paths, OutError))
	{
		return false;
	}
	LoadCoordinator.ResetFloor();
	const int64 TransactionId = PendingTransactionId;
	return LoadCoordinator.BeginPhase(
		EEFCalystoLoadPhase::FloorVisual,
		Paths,
		FStreamableDelegate::CreateUObject(
			this, &UEFCalystoDungeonSubsystem::HandlePendingFloorVisualReady, TransactionId),
		OutError);
}

void UEFCalystoDungeonSubsystem::HandlePendingFloorVisualReady(const int64 TransactionId)
{
	if (!bTravelRequestPending || TransactionId != PendingTransactionId)
	{
		return;
	}
	FString Error;
	if (!LoadCoordinator.IsPhaseReady(EEFCalystoLoadPhase::FloorVisual, Error))
	{
		RejectPendingTravel(TEXT("FLOOR_VISUAL_LOAD_FAILED"), Error, bOpenLevelIssued);
		return;
	}
	ExecutePendingTravel(TransactionId);
}

void UEFCalystoDungeonSubsystem::ExecutePendingTravel(const int64 TransactionId)
{
	if (!bTravelRequestPending || bOpenLevelIssued || TransactionId != PendingTransactionId ||
		PendingRunEpoch != PendingContext.RunEpoch)
	{
		return;
	}
	FString Error;
	if (!FEFCalystoDungeonRuntimeMathV6::ValidateResolvedFloorIntent(PendingIntent, Error) ||
		!LoadCoordinator.IsPhaseReady(EEFCalystoLoadPhase::FloorVisual, Error))
	{
		RejectPendingTravel(TEXT("PENDING_INTENT_INVALID"), Error, false);
		return;
	}
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	UWorld* World = GetWorld();
	if (!Settings || Settings->DungeonMap.IsNull() || !IsValid(World))
	{
		RejectPendingTravel(
			TEXT("DUNGEON_DESTINATION_INVALID"),
			TEXT("The configured dungeon destination is unavailable."), false);
		return;
	}

	bOpenLevelIssued = true;
	TravelState = PendingTravelKind == EEFCalystoDungeonTravelKindV6::Retry
		? EEFCalystoDungeonTravelStateV6::Recovering
		: EEFCalystoDungeonTravelStateV6::Traveling;
	ArmWatchdog(Settings->TravelWatchdogSeconds);
	UGameplayStatics::OpenLevelBySoftObjectPtr(World, Settings->DungeonMap, true);
}

void UEFCalystoDungeonSubsystem::HandlePostWorldInitialization(
	UWorld* World, const UWorld::InitializationValues IVS)
{
	(void)IVS;
	if (!IsConfiguredDungeonWorld(World))
	{
		return;
	}
	if (bTravelRequestPending && bOpenLevelIssued && PendingTransactionId > 0)
	{
		const int64 TransactionId = PendingTransactionId;
		TWeakObjectPtr<UEFCalystoDungeonSubsystem> WeakThis(this);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, WeakWorld, TransactionId]()
		{
			if (UEFCalystoDungeonSubsystem* StrongThis = WeakThis.Get())
			{
				StrongThis->HandleDungeonWorldReady(WeakWorld, TransactionId);
			}
		});
		return;
	}

	// Direct PIE/editor entry receives the same pre-travel contract and reloads
	// once; generation is never allowed to run without a frozen V6 intent.
	if (!bHasActiveRun && !bTravelRequestPending)
	{
		TWeakObjectPtr<UEFCalystoDungeonSubsystem> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis]()
		{
			if (UEFCalystoDungeonSubsystem* StrongThis = WeakThis.Get())
			{
				StrongThis->RequestStartNewRun();
			}
		});
	}
}

void UEFCalystoDungeonSubsystem::HandleDungeonWorldReady(
	TWeakObjectPtr<UWorld> World, const int64 TransactionId)
{
	UWorld* ResolvedWorld = World.Get();
	if (!bTravelRequestPending || !bOpenLevelIssued || TransactionId != PendingTransactionId ||
		PendingRunEpoch != PendingContext.RunEpoch || !IsConfiguredDungeonWorld(ResolvedWorld))
	{
		return;
	}
	FString Error;
	if (!LoadCoordinator.IsPhaseReady(EEFCalystoLoadPhase::FloorVisual, Error))
	{
		// A world callback can race the final streamable completion. The pending
		// transaction remains armed; only the watchdog or explicit failure rejects it.
		return;
	}
	if (!FEFCalystoDungeonRuntimeMathV6::ValidateResolvedFloorIntent(PendingIntent, Error))
	{
		RejectPendingTravel(TEXT("WORLD_INTENT_REJECTED"), Error, true);
		return;
	}
	AcceptPendingWorld(ResolvedWorld);
}

void UEFCalystoDungeonSubsystem::AcceptPendingWorld(UWorld* World)
{
	if (!bTravelRequestPending || !IsConfiguredDungeonWorld(World))
	{
		return;
	}
	const EEFCalystoDungeonTravelKindV6 AcceptedKind = PendingTravelKind;
	const int64 AcceptedRunEpoch = PendingRunEpoch;
	const bool bNewRun = AcceptedKind == EEFCalystoDungeonTravelKindV6::NewRun ||
		AcceptedKind == EEFCalystoDungeonTravelKindV6::RestartSameSeed;

	ActiveContext = PendingIntent.GenerationContext;
	ActiveContext.RunEpoch = AcceptedRunEpoch;
	ActiveIntent = PendingIntent;
	ActiveIntent.GenerationContext.RunEpoch = AcceptedRunEpoch;
	ActiveFloorPlan = PendingFloorPlan;
	ActiveEcology = PendingEcology;
	ExpectedRealizedManifest = PendingExpectedManifest;
	ActiveTravelKind = AcceptedKind;
	RunEpoch = AcceptedRunEpoch;
	bHasActiveRun = true;
	ResetActiveFloorEvidence();

	if (bPendingConsumesDirectorIntent)
	{
		QueuedDirectorIntent = FEFCalystoDirectorIntentV6();
		bHasQueuedDirectorIntent = false;
	}
	if (bPendingConsumesFloorOutcome)
	{
		QueuedFloorOutcome = FEFCalystoFloorOutcomeV6();
		bHasQueuedFloorOutcome = false;
	}
	if (bPendingConsumesCompanionRoster)
	{
		QueuedCompanionRoster = FEFCalystoCompanionRosterSnapshotV6();
		bHasQueuedCompanionRoster = false;
	}

	bTravelRequestPending = false;
	bOpenLevelIssued = false;
	TravelState = EEFCalystoDungeonTravelStateV6::Generating;
	RecoveryAttempt = AcceptedKind == EEFCalystoDungeonTravelKindV6::Retry
		? RecoveryAttempt : 0;
	PendingTravelKind = EEFCalystoDungeonTravelKindV6::None;
	PendingTransactionId = 0;
	PendingRunEpoch = 0;
	CancelWatchdog();
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	ArmWatchdog(Settings ? Settings->TravelWatchdogSeconds : 30.0);

	if (bNewRun)
	{
		NewRunInitializedEvent.Broadcast(RunEpoch);
	}
	DirectorWorldAcceptedEvent.Broadcast(RunEpoch, AcceptedKind, ActiveIntent);
	UE_LOG(LogEFCalystoDungeon, Log,
		TEXT("CALYSTO_V6_WORLD_ACCEPTED epoch=%lld floor=%lld serial=%lld transaction=%lld style=%s intent=%s"),
		static_cast<long long>(RunEpoch),
		static_cast<long long>(ActiveContext.FloorNumber),
		static_cast<long long>(ActiveContext.GenerationSerial),
		static_cast<long long>(NextTransactionId),
		*ActiveIntent.StyleId.ToString(), *ActiveIntent.IntentHash);
}

void UEFCalystoDungeonSubsystem::HandleTravelFailure(
	UWorld* World, const ETravelFailure::Type FailureType, const FString& ErrorString)
{
	(void)World;
	if (!bTravelRequestPending)
	{
		return;
	}
	RejectPendingTravel(
		TEXT("ENGINE_TRAVEL_FAILURE"),
		FString::Printf(TEXT("Engine travel failure %d: %s"),
			static_cast<int32>(FailureType), *ErrorString),
		bOpenLevelIssued);
}

void UEFCalystoDungeonSubsystem::BeginSessionCoreLoad()
{
	bSessionCoreReady = false;
	RuntimePolicy = nullptr;
	RuntimePolicyHash.Reset();
	SessionCoreObjects.Reset();
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	if (!Settings)
	{
		SetFailure(TEXT("SETTINGS_MISSING"), TEXT("Calysto Director settings are unavailable."));
		return;
	}

	TArray<FSoftObjectPath> Paths;
	auto AddPath = [&Paths](const FSoftObjectPath& Path)
	{
		if (Path.IsValid())
		{
			Paths.AddUnique(Path);
		}
	};
	AddPath(Settings->DirectorPolicy.ToSoftObjectPath());
	AddPath(Settings->DungeonMeshDataAsset.ToSoftObjectPath());
	AddPath(Settings->SpawnerDataAsset.ToSoftObjectPath());
	AddPath(Settings->RoomThemeDataAsset.ToSoftObjectPath());
	AddPath(Settings->DungeonMaterialDataAsset.ToSoftObjectPath());
	AddPath(Settings->DungeonFloorDoorClass.ToSoftObjectPath());
	AddPath(Settings->DungeonFloorDoorMesh.ToSoftObjectPath());
	AddPath(Settings->PopulationAnchorClass.ToSoftObjectPath());
	// These exact graph assets are part of Session Core so cooked compatibility
	// can use find-only resolution during generation. No PCG runtime path is
	// permitted to turn an absent resident dependency into a blocking load.
	AddPath(FSoftObjectPath(
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.PCG_MassiveDungeonMaster")));
	AddPath(FSoftObjectPath(
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonShape.PCG_MassiveDungeonShape")));
	AddPath(FSoftObjectPath(
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh")));
	AddPath(FSoftObjectPath(
		TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps.PCG_AddRamps")));
	AddPath(FSoftObjectPath(
		TEXT("/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple.PCG_ObjectTransformSimple")));
	AddPath(FSoftObjectPath(
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon.PCG_ObjectTransformSimpleDungeon")));
	AddPath(FSoftObjectPath(
		TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_SetDungeonMeshCookedSafe.PCG_SetDungeonMeshCookedSafe")));
	AddPath(FSoftObjectPath(
		TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_AddRampsCookedSafe.PCG_AddRampsCookedSafe")));
	if (const UEFProceduralSettings* ProceduralSettings = UEFProceduralSettings::Get())
	{
		AddPath(ProceduralSettings->GetDungeonActorClassResolved().ToSoftObjectPath());
		AddPath(ProceduralSettings->GetStartPointActorClassResolved().ToSoftObjectPath());
		AddPath(ProceduralSettings->GetMeleeAIControllerClassResolved().ToSoftObjectPath());
		AddPath(ProceduralSettings->GetRangedAIControllerClassResolved().ToSoftObjectPath());
	}

	FString Error;
	if (!LoadCoordinator.BeginPhase(
		EEFCalystoLoadPhase::SessionCore,
		Paths,
		FStreamableDelegate::CreateUObject(this, &UEFCalystoDungeonSubsystem::HandleSessionCoreReady),
		Error))
	{
		SetFailure(TEXT("SESSION_CORE_LOAD_START_FAILED"), Error);
	}
}

void UEFCalystoDungeonSubsystem::HandleSessionCoreReady()
{
	FString Error;
	if (!LoadCoordinator.IsPhaseReady(EEFCalystoLoadPhase::SessionCore, Error) ||
		!CompilePolicyFromResidentSessionCore(Error))
	{
		bSessionCoreReady = false;
		SetFailure(TEXT("SESSION_CORE_INVALID"), Error);
		if (bTravelRequestPending)
		{
			RejectPendingTravel(TEXT("SESSION_CORE_INVALID"), Error, false);
		}
		return;
	}
	bSessionCoreReady = true;
	if (bTravelRequestPending && !bOpenLevelIssued)
	{
		if (!PreparePendingIntent(Error) || !BeginPendingFloorVisualLoad(Error))
		{
			RejectPendingTravel(TEXT("PRETRAVEL_RESOLUTION_FAILED"), Error, false);
		}
	}
}

bool UEFCalystoDungeonSubsystem::CompilePolicyFromResidentSessionCore(FString& OutError)
{
	OutError.Reset();
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	if (!Settings || Settings->DirectorPolicy.IsNull())
	{
		OutError = TEXT("The definitive Director policy path is not configured.");
		return false;
	}
	RuntimePolicy = Settings->DirectorPolicy.Get();
	if (!RuntimePolicy)
	{
		OutError = TEXT("Session Core completed without resolving the definitive Director policy.");
		return false;
	}
	if (!RuntimePolicy->Validate(OutError))
	{
		RuntimePolicy = nullptr;
		return false;
	}
	RuntimePolicyHash = RuntimePolicy->GetGameplayHash();
	if (!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(RuntimePolicyHash))
	{
		OutError = TEXT("The definitive Director policy produced a noncanonical gameplay hash.");
		RuntimePolicy = nullptr;
		return false;
	}
	for (const FSoftObjectPath& Path :
		LoadCoordinator.GetPhasePaths(EEFCalystoLoadPhase::SessionCore))
	{
		if (UObject* Object = Path.ResolveObject())
		{
			SessionCoreObjects.AddUnique(Object);
		}
	}
	return true;
}

bool UEFCalystoDungeonSubsystem::BuildAndFreezeRoomManifestV6(
	const TArray<FEFCalystoRoomIdentityInputV6>& RoomInputs,
	FEFCalystoRoomManifestV6& OutManifest,
	FString& OutError)
{
	OutManifest = FEFCalystoRoomManifestV6();
	OutError.Reset();
	if (!RuntimePolicy || !ActiveIntent.bIsValid ||
		TravelState != EEFCalystoDungeonTravelStateV6::Generating)
	{
		OutError = TEXT("Room topology can only freeze during active V6 generation.");
		return false;
	}
	if (!ActiveRoomManifest.ManifestHash.IsEmpty())
	{
		FEFCalystoRoomManifestV6 RecomputedManifest;
		if (!RuntimePolicy->BuildRoomManifest(
				ActiveFloorPlan, RoomInputs, RecomputedManifest, OutError) ||
			!RecomputedManifest.ManifestHash.Equals(
				ActiveRoomManifest.ManifestHash, ESearchCase::IgnoreCase) ||
			!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(
				ActivePopulationPlan.PopulationHash) ||
			!ActivePopulationPlan.FloorPlanHash.Equals(
				ActiveFloorPlan.FloorPlanHash, ESearchCase::IgnoreCase) ||
			!ActivePopulationPlan.RoomManifestHash.Equals(
				ActiveRoomManifest.ManifestHash, ESearchCase::IgnoreCase))
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("A repeated topology freeze differs from the immutable V6 room or population transaction.");
			}
			return false;
		}
		OutManifest = ActiveRoomManifest;
		return true;
	}

	FEFCalystoRoomManifestV6 CandidateManifest;
	if (!RuntimePolicy->BuildRoomManifest(
		ActiveFloorPlan, RoomInputs, CandidateManifest, OutError))
	{
		return false;
	}

	FEFCalystoPopulationBuildOptionsV6 PopulationOptions;
	PopulationOptions.FloorNumber = ActiveContext.FloorNumber;
	PopulationOptions.bGraveyardEligible = false;
	for (const FEFCalystoCooldownStateV6& Cooldown : ActiveEcology.Cooldowns)
	{
		const int64 FloorsSinceSelection =
			ActiveContext.FloorNumber - Cooldown.LastSelectedFloor;
		if (!Cooldown.StableId.IsNone() && FloorsSinceSelection > 0 &&
			FloorsSinceSelection <= Cooldown.CooldownFloors)
		{
			// The persistent ledger intentionally remains type-agnostic. Excluding the
			// stable ID from both tables prevents an actor/content ID collision from
			// silently escaping its authored cooldown.
			PopulationOptions.CoolingDownActorEntryIds.Add(Cooldown.StableId);
			PopulationOptions.CoolingDownContentEntryIds.Add(Cooldown.StableId);
		}
	}
	FEFCalystoPopulationPlanV6 CandidatePopulationPlan;
	if (!FEFCalystoPopulationPlannerV6::BuildPlan(
		ActiveFloorPlan,
		CandidateManifest,
		PopulationOptions,
		CandidatePopulationPlan,
		OutError))
	{
		return false;
	}

	TArray<FSoftObjectPath> DecalPaths;
	if (!RuntimePolicy->GatherPostTopologyDecalPaths(
		ActiveFloorPlan, CandidateManifest, DecalPaths, OutError) ||
		!LoadCoordinator.BeginPhase(
			EEFCalystoLoadPhase::OptionalDecals, DecalPaths, FStreamableDelegate(), OutError))
	{
		return false;
	}

	ActiveRoomManifest = MoveTemp(CandidateManifest);
	ActivePopulationPlan = MoveTemp(CandidatePopulationPlan);
	OutManifest = ActiveRoomManifest;
	UE_LOG(
		LogEFCalystoDungeon,
		Log,
		TEXT("CALYSTO_V6_ROOM_AND_POPULATION_FROZEN rooms=%d actors=%d chest_contents=%d room_manifest=%s population=%s"),
		ActiveRoomManifest.Rooms.Num(),
		ActivePopulationPlan.ActorDecisionCount,
		ActivePopulationPlan.ChestContentDecisionCount,
		*ActiveRoomManifest.ManifestHash,
		*ActivePopulationPlan.PopulationHash);
	return true;
}

bool UEFCalystoDungeonSubsystem::BeginPostTopologyContentLoadV6(
	const TConstArrayView<FSoftObjectPath> PopulationMaterializationPaths,
	FString& OutError)
{
	OutError.Reset();
	if (!RuntimePolicy || ActiveRoomManifest.ManifestHash.IsEmpty())
	{
		OutError = TEXT("Post-topology loading requires a frozen room manifest.");
		return false;
	}
	TArray<FSoftObjectPath> Paths;
	if (!RuntimePolicy->GatherPostTopologyContentPaths(
		ActiveFloorPlan, ActiveRoomManifest, Paths, OutError))
	{
		return false;
	}
	for (const FSoftObjectPath& Path : PopulationMaterializationPaths)
	{
		if (Path.IsValid())
		{
			Paths.AddUnique(Path);
		}
	}
	for (const FGuid& ActiveCompanionId : ActiveIntent.CompanionRoster.ActiveParty)
	{
		const FEFCalystoCompanionRecordV6* Record =
			ActiveIntent.CompanionRoster.Records.FindByPredicate(
				[&ActiveCompanionId](const FEFCalystoCompanionRecordV6& Candidate)
				{
					return Candidate.StableCompanionId == ActiveCompanionId;
				});
		if (!Record || Record->ActorClass.IsNull())
		{
			OutError = TEXT("An active companion is missing its frozen actor class.");
			return false;
		}
		Paths.AddUnique(Record->ActorClass.ToSoftObjectPath());
	}
	if (LoadCoordinator.HasPhase(EEFCalystoLoadPhase::PostTopologyContent))
	{
		if (!EFCalystoDungeonSubsystemPrivate::PathsEqual(
			LoadCoordinator.GetPhasePaths(EEFCalystoLoadPhase::PostTopologyContent), Paths))
		{
			OutError = TEXT("The immutable post-topology load closure changed after it was started.");
			return false;
		}
		return true;
	}
	return LoadCoordinator.BeginPhase(
		EEFCalystoLoadPhase::PostTopologyContent,
		Paths, FStreamableDelegate(), OutError);
}

bool UEFCalystoDungeonSubsystem::ArePostTopologyAssetsReadyV6(FString& OutError) const
{
	return GetPostTopologyAssetLoadStateV6(OutError) ==
		EEFCalystoLoadPhaseState::Ready;
}

EEFCalystoLoadPhaseState UEFCalystoDungeonSubsystem::GetPostTopologyAssetLoadStateV6(
	FString& OutError) const
{
	OutError.Reset();
	FString ContentError;
	FString DecalError;
	const EEFCalystoLoadPhaseState ContentState = LoadCoordinator.GetPhaseState(
		EEFCalystoLoadPhase::PostTopologyContent, ContentError);
	const EEFCalystoLoadPhaseState DecalState = LoadCoordinator.GetPhaseState(
		EEFCalystoLoadPhase::OptionalDecals, DecalError);
	if (ContentState == EEFCalystoLoadPhaseState::Failed ||
		DecalState == EEFCalystoLoadPhaseState::Failed)
	{
		OutError = !ContentError.IsEmpty() ? ContentError : DecalError;
		return EEFCalystoLoadPhaseState::Failed;
	}
	if (ContentState == EEFCalystoLoadPhaseState::NotStarted ||
		DecalState == EEFCalystoLoadPhaseState::NotStarted)
	{
		OutError = !ContentError.IsEmpty() ? ContentError : DecalError;
		return EEFCalystoLoadPhaseState::NotStarted;
	}
	if (ContentState == EEFCalystoLoadPhaseState::Loading ||
		DecalState == EEFCalystoLoadPhaseState::Loading)
	{
		OutError = !ContentError.IsEmpty() ? ContentError : DecalError;
		return EEFCalystoLoadPhaseState::Loading;
	}
	return EEFCalystoLoadPhaseState::Ready;
}

bool UEFCalystoDungeonSubsystem::EnsureFloorVisualAssetsV6(
	FStreamableDelegate Completion, FString& OutError)
{
	OutError.Reset();
	if (LoadCoordinator.HasPhase(EEFCalystoLoadPhase::FloorVisual))
	{
		const EEFCalystoLoadPhaseState ExistingState =
			GetFloorVisualAssetLoadStateV6(OutError);
		if (ExistingState == EEFCalystoLoadPhaseState::Ready)
		{
			Completion.ExecuteIfBound();
			return true;
		}
		if (ExistingState == EEFCalystoLoadPhaseState::Loading)
		{
			OutError.Reset();
			return true;
		}
		if (OutError.IsEmpty())
		{
			OutError = TEXT("The Floor Visual load phase is not available for use.");
		}
		return false;
	}
	if (!RuntimePolicy || !ActiveIntent.bIsValid)
	{
		OutError = TEXT("Floor Visual loading requires an active frozen intent.");
		return false;
	}
	TArray<FSoftObjectPath> Paths;
	if (!RuntimePolicy->GatherReachableVisualPreloadPaths(ActiveFloorPlan, Paths, OutError))
	{
		return false;
	}
	return LoadCoordinator.BeginPhase(
		EEFCalystoLoadPhase::FloorVisual, Paths, MoveTemp(Completion), OutError);
}

bool UEFCalystoDungeonSubsystem::AreFloorVisualAssetsReadyV6(FString& OutError) const
{
	return GetFloorVisualAssetLoadStateV6(OutError) ==
		EEFCalystoLoadPhaseState::Ready;
}

EEFCalystoLoadPhaseState UEFCalystoDungeonSubsystem::GetFloorVisualAssetLoadStateV6(
	FString& OutError) const
{
	return LoadCoordinator.GetPhaseState(EEFCalystoLoadPhase::FloorVisual, OutError);
}

bool UEFCalystoDungeonSubsystem::NotifyPopulationRealizedV6(
	const FString& MaterializationHash,
	const int32 CandidateAnchorCount,
	const TConstArrayView<FEFCalystoRealizedPopulationActorRecordV6> RealizedActors)
{
	FString Error;
	if (TravelState != EEFCalystoDungeonTravelStateV6::Generating ||
		bPopulationRealized || ActiveRoomManifest.ManifestHash.IsEmpty() ||
		!ValidateActiveIdentity(Error) ||
		!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(
			ActivePopulationPlan.PopulationHash))
	{
		SetFailure(TEXT("POPULATION_ACCEPTANCE_REJECTED"),
			Error.IsEmpty()
				? TEXT("Population evidence does not match the frozen V6 floor transaction.")
				: Error);
		return false;
	}

	FEFCalystoRealizedFloorManifestV6 Manifest;
	if (!BuildRealizedFloorManifestV6(
			ActiveContext,
			ActiveIntent,
			ActiveFloorPlan,
			ActiveRoomManifest,
			ActivePopulationPlan,
			MaterializationHash,
			CandidateAnchorCount,
			RealizedActors,
			Manifest,
			Error))
	{
		SetFailure(TEXT("POPULATION_MANIFEST_INVALID"), Error);
		return false;
	}
	if (ExpectedRealizedManifest.bIsValid &&
		(ExpectedRealizedManifest.PopulationPlanHash != Manifest.PopulationPlanHash ||
		 ExpectedRealizedManifest.AnchorTopologyHash != Manifest.AnchorTopologyHash ||
		 ExpectedRealizedManifest.ManifestHash != Manifest.ManifestHash))
	{
		SetFailure(TEXT("REPLAY_MANIFEST_DRIFT"),
			TEXT("Replay or Retry did not reproduce the frozen realized manifest."));
		return false;
	}

	ActiveRealizedManifest = MoveTemp(Manifest);
	AcceptedMaterializationHash = MaterializationHash;
	AcceptedSpawnedActorCount = ActiveRealizedManifest.SpawnedActorCount;
	AcceptedChestContentCount = 0;
	for (const FEFCalystoRealizedPopulationActorRecordV6& Actor :
		ActiveRealizedManifest.Actors)
	{
		AcceptedChestContentCount += Actor.VerifiedChestContentIds.Num();
	}
	bPopulationRealized = true;
	return true;
}

void UEFCalystoDungeonSubsystem::RollbackPopulationRealizedV6(
	const FString& PopulationHash, const FString& MaterializationHash)
{
	if (!bPopulationRealized || ActivePopulationPlan.PopulationHash != PopulationHash ||
		AcceptedMaterializationHash != MaterializationHash)
	{
		return;
	}
	ActiveRealizedManifest = FEFCalystoRealizedFloorManifestV6();
	AcceptedMaterializationHash.Reset();
	AcceptedSpawnedActorCount = 0;
	AcceptedChestContentCount = 0;
	bPopulationRealized = false;
}

bool UEFCalystoDungeonSubsystem::IsPopulationRealizedV6() const
{
	return bPopulationRealized && ActiveRealizedManifest.bIsValid;
}

bool UEFCalystoDungeonSubsystem::NotifyCompanionRosterReady(
	const FString& CompanionSnapshotHash)
{
	if (TravelState != EEFCalystoDungeonTravelStateV6::Generating ||
		!ActiveIntent.bIsValid ||
		!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(CompanionSnapshotHash) ||
		!CompanionSnapshotHash.Equals(
			ActiveIntent.CompanionRoster.SnapshotHash, ESearchCase::IgnoreCase))
	{
		return false;
	}
	bCompanionRosterReady = true;
	return true;
}

bool UEFCalystoDungeonSubsystem::IsCompanionRosterReady() const
{
	return bCompanionRosterReady;
}

bool UEFCalystoDungeonSubsystem::NotifyFloorReady()
{
	FString Error;
	if (TravelState != EEFCalystoDungeonTravelStateV6::Generating ||
		!ValidateActiveIdentity(Error) || !bPopulationRealized || !bCompanionRosterReady ||
		!AreFloorVisualAssetsReadyV6(Error) || !ArePostTopologyAssetsReadyV6(Error) ||
		!FEFCalystoDungeonRuntimeMathV6::ValidateRealizedFloorManifest(
			ActiveRealizedManifest, Error))
	{
		SetFailure(TEXT("FLOOR_READY_GATE_REJECTED"), Error.IsEmpty()
			? TEXT("One or more immutable floor readiness gates are incomplete.") : Error);
		return false;
	}
	TravelState = EEFCalystoDungeonTravelStateV6::Ready;
	LastFailureCode = NAME_None;
	LastFailureMessage.Reset();
	ExpectedRealizedManifest = FEFCalystoRealizedFloorManifestV6();
	CancelWatchdog();
	FloorReadyEvent.Broadcast(
		ActiveContext.FloorNumber, ActiveIntent.PCGSeed, ActiveIntent, ActiveRealizedManifest);
	return true;
}

bool UEFCalystoDungeonSubsystem::NotifyGenerationFailed(
	const FName FailureCode, const FString& FailureMessage)
{
	if (!bHasActiveRun ||
		(TravelState != EEFCalystoDungeonTravelStateV6::Generating &&
		 TravelState != EEFCalystoDungeonTravelStateV6::Recovering))
	{
		return false;
	}
	SetFailure(FailureCode.IsNone() ? FName(TEXT("GENERATION_FAILED")) : FailureCode,
		FailureMessage);
	FString Error;
	if (RecoveryAttempt < EFCalystoDungeonSubsystemPrivate::MaximumRecoveryAttempts &&
		BeginFloorRecovery(Error))
	{
		return true;
	}
	ReturnFromFailedDungeon(
		FailureCode.IsNone() ? FName(TEXT("GENERATION_FAILED")) : FailureCode,
		Error.IsEmpty() ? FailureMessage : Error);
	return true;
}

bool UEFCalystoDungeonSubsystem::BeginFloorRecovery(FString& OutError)
{
	OutError.Reset();
	if (!bHasActiveRun || !ActiveIntent.bIsValid || bTravelRequestPending)
	{
		OutError = TEXT("Floor recovery requires an active, nonpending floor intent.");
		return false;
	}
	++RecoveryAttempt;
	PendingContext = ActiveIntent.GenerationContext;
	PendingContext.TravelKind = EEFCalystoDungeonTravelKindV6::Retry;
	PendingIntent = ActiveIntent;
	PendingIntent.GenerationContext = PendingContext;
	PendingFloorPlan = ActiveFloorPlan;
	PendingEcology = ActiveEcology;
	PendingExpectedManifest = ActiveRealizedManifest.bIsValid
		? ActiveRealizedManifest : ExpectedRealizedManifest;
	// Empty topology and exhausted placement are deterministic for a frozen
	// seed. Retrying that identical seed cannot repair them. Only unaccepted
	// floors may receive a new generation serial; replay of accepted evidence
	// must remain exact. Preserve run, floor, Style and the already-committed
	// ecology/outcome rather than consuming the previous floor a second time.
	const bool bEmptyTopology = LastFailureCode == TEXT("V6_ROOM_MANIFEST_INVALID")
		&& LastFailureMessage.Contains(TEXT("found 0 across 0 data sets"));
	const bool bExhaustedPlacement = LastFailureCode == TEXT("V6_POPULATION_FAILED")
		&& (LastFailureMessage.Contains(TEXT("candidates in placement zone"))
			|| LastFailureMessage.Contains(TEXT("No collision-free room-matched anchor")));
	if ((bEmptyTopology || bExhaustedPlacement)
		&& !ExpectedRealizedManifest.bIsValid && !ActiveRealizedManifest.bIsValid)
	{
		if (!RuntimePolicy || PendingContext.GenerationSerial == MAX_int64)
		{
			OutError = TEXT("Unrealized floor recovery requires a resident policy and an available generation serial.");
			return false;
		}
		++PendingContext.GenerationSerial;
		PendingContext.ContextHash = FEFCalystoDungeonRuntimeMathV6::ComputeGenerationContextHash(PendingContext);
		if (!RuntimePolicy->BuildResolvedFloorPlanForStyle(
			EFCalystoDungeonSubsystemPrivate::BuildFloorSeed(PendingContext),
			ActiveIntent.StyleId, PendingFloorPlan, OutError))
		{
			return false;
		}
		const FEFCalystoStyleProfileV6* Style = RuntimePolicy->FindStyle(ActiveIntent.StyleId);
		if (!Style || !FEFCalystoDungeonRuntimeMathV6::BuildResolvedFloorIntent(
			PendingContext, ActiveIntent.DirectorIntent, ActiveIntent.FrozenOutcome,
			PendingEcology, ActiveIntent.CompanionRoster, PendingFloorPlan,
			Style->Lighting, PendingIntent, OutError))
		{
			return false;
		}
		UE_LOG(LogEFCalystoDungeon, Warning,
			TEXT("CALYSTO_V6_UNREALIZED_FLOOR_RECOVERY attempt=%d floor=%lld serial=%lld old_seed=%d new_seed=%d style=%s."),
			RecoveryAttempt, PendingContext.FloorNumber, PendingContext.GenerationSerial,
			ActiveIntent.PCGSeed, PendingIntent.PCGSeed, *ActiveIntent.StyleId.ToString());
	}
	PendingRunEpoch = RunEpoch;
	PendingTravelKind = EEFCalystoDungeonTravelKindV6::Retry;
	PendingTransactionId = ++NextTransactionId;
	bTravelRequestPending = true;
	bOpenLevelIssued = false;
	TravelState = EEFCalystoDungeonTravelStateV6::Recovering;
	bPreparationFailed = false;
	bPreparationBroadcastActive = true;
	PreparationFailureCode = NAME_None;
	PreparationFailureMessage.Reset();
	BeforeAnyDirectorTravelEvent.Broadcast(EEFCalystoDungeonTravelKindV6::Retry);
	bPreparationBroadcastActive = false;
	if (bPreparationFailed)
	{
		OutError = PreparationFailureMessage.IsEmpty()
			? TEXT("A pre-travel adapter rejected the floor recovery transaction.")
			: PreparationFailureMessage;
		return false;
	}
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	ArmWatchdog(Settings ? Settings->TravelWatchdogSeconds : 30.0);
	return BeginPendingFloorVisualLoad(OutError);
}

void UEFCalystoDungeonSubsystem::RejectPendingTravel(
	const FName FailureCode, const FString& FailureMessage, const bool bRecoverToSourceWorld)
{
	const bool bHadIssuedOpen = bOpenLevelIssued;
	SetFailure(FailureCode, FailureMessage);
	LoadCoordinator.ResetFloor();
	ClearPendingTravel();
	CancelWatchdog();
	TravelState = bHasActiveRun ? SourceTravelState : EEFCalystoDungeonTravelStateV6::Failed;
	if (bRecoverToSourceWorld && bHadIssuedOpen)
	{
		ReturnFromFailedDungeon(FailureCode, FailureMessage);
		return;
	}
	FloorTravelFailedEvent.Broadcast();
}

void UEFCalystoDungeonSubsystem::ReturnFromFailedDungeon(
	const FName FailureCode, const FString& FailureMessage)
{
	SetFailure(FailureCode, FailureMessage);
	CancelWatchdog();
	LoadCoordinator.ResetFloor();
	bTravelRequestPending = false;
	bOpenLevelIssued = false;
	TravelState = EEFCalystoDungeonTravelStateV6::Failed;
	FloorTravelFailedEvent.Broadcast();
	UWorld* World = GetWorld();
	const FName Destination = GetSafeReturnPackageName();
	if (IsValid(World) && !Destination.IsNone() &&
		!IsConfiguredDungeonWorld(World))
	{
		return;
	}
	if (IsValid(World) && !Destination.IsNone())
	{
		TravelState = EEFCalystoDungeonTravelStateV6::Recovering;
		UGameplayStatics::OpenLevel(World, Destination, true);
	}
}

void UEFCalystoDungeonSubsystem::ClearPendingTravel()
{
	PendingContext = FEFCalystoDungeonGenerationContextV6();
	PendingIntent = FEFCalystoResolvedFloorIntentV6();
	PendingFloorPlan = FEFCalystoResolvedFloorPlanV6();
	PendingEcology = FEFCalystoRunEcologyStateV6();
	PendingExpectedManifest = FEFCalystoRealizedFloorManifestV6();
	PendingTravelKind = EEFCalystoDungeonTravelKindV6::None;
	PendingRunEpoch = 0;
	PendingTransactionId = 0;
	bTravelRequestPending = false;
	bOpenLevelIssued = false;
	bPreparationBroadcastActive = false;
	bPreparationFailed = false;
	bPendingConsumesDirectorIntent = false;
	bPendingConsumesFloorOutcome = false;
	bPendingConsumesCompanionRoster = false;
}

void UEFCalystoDungeonSubsystem::ResetActiveFloorEvidence()
{
	ActiveRoomManifest = FEFCalystoRoomManifestV6();
	ActivePopulationPlan = FEFCalystoPopulationPlanV6();
	ActiveRealizedManifest = FEFCalystoRealizedFloorManifestV6();
	AcceptedMaterializationHash.Reset();
	AcceptedSpawnedActorCount = 0;
	AcceptedChestContentCount = 0;
	bPopulationRealized = false;
	bCompanionRosterReady = false;
}

bool UEFCalystoDungeonSubsystem::IsConfiguredDungeonWorld(const UWorld* World) const
{
	if (!IsValid(World) || !World->IsGameWorld() ||
		World->GetGameInstance() != GetGameInstance())
	{
		return false;
	}
	const FName Configured = GetConfiguredDungeonPackageName();
	const FName Actual(*EFCalystoDungeonSubsystemPrivate::NormalizeMapPackageName(
		World->GetPackage()->GetName()));
	return !Configured.IsNone() && Configured.IsEqual(Actual, ENameCase::IgnoreCase);
}

FName UEFCalystoDungeonSubsystem::GetConfiguredDungeonPackageName() const
{
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	if (!Settings || Settings->DungeonMap.IsNull())
	{
		return NAME_None;
	}
	return FName(*Settings->DungeonMap.ToSoftObjectPath().GetLongPackageName());
}

FName UEFCalystoDungeonSubsystem::GetSafeReturnPackageName() const
{
	if (!ReturnWorldPackage.IsNone() &&
		!ReturnWorldPackage.IsEqual(GetConfiguredDungeonPackageName(), ENameCase::IgnoreCase))
	{
		return ReturnWorldPackage;
	}
	return TEXT("/Game/_Game/Hub/HUB");
}

bool UEFCalystoDungeonSubsystem::ArmWatchdog(const double TimeoutSeconds)
{
	CancelWatchdog();
	WatchdogDeadlineSeconds = FPlatformTime::Seconds() + FMath::Clamp(TimeoutSeconds, 1.0, 30.0);
	WatchdogTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UEFCalystoDungeonSubsystem::HandleWatchdogTick),
		0.25f);
	return WatchdogTickerHandle.IsValid();
}

void UEFCalystoDungeonSubsystem::CancelWatchdog()
{
	if (WatchdogTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(WatchdogTickerHandle);
		WatchdogTickerHandle.Reset();
	}
	WatchdogDeadlineSeconds = 0.0;
}

bool UEFCalystoDungeonSubsystem::HandleWatchdogTick(const float DeltaSeconds)
{
	(void)DeltaSeconds;
	if (WatchdogDeadlineSeconds <= 0.0 || FPlatformTime::Seconds() < WatchdogDeadlineSeconds)
	{
		return true;
	}
	WatchdogTickerHandle.Reset();
	WatchdogDeadlineSeconds = 0.0;
	if (bTravelRequestPending)
	{
		RejectPendingTravel(
			TEXT("TRAVEL_TIMEOUT"),
			TEXT("The V6 preload/travel transaction exceeded its explicit watchdog."),
			bOpenLevelIssued);
		return false;
	}
	if (TravelState == EEFCalystoDungeonTravelStateV6::Generating)
	{
		NotifyGenerationFailed(
			TEXT("GENERATION_TIMEOUT"),
			TEXT("The V6 floor did not satisfy every readiness gate before its watchdog expired."));
	}
	return false;
}

bool UEFCalystoDungeonSubsystem::ValidateActiveIdentity(FString& OutError) const
{
	OutError.Reset();
	if (!bHasActiveRun || !RuntimePolicy ||
		!FEFCalystoDungeonRuntimeMathV6::ValidateResolvedFloorIntent(ActiveIntent, OutError) ||
		ActiveContext.RunSeed != ActiveIntent.GenerationContext.RunSeed ||
		ActiveContext.FloorNumber != ActiveIntent.GenerationContext.FloorNumber ||
		ActiveContext.GenerationSerial != ActiveIntent.GenerationContext.GenerationSerial ||
		RunEpoch != ActiveIntent.GenerationContext.RunEpoch ||
		ActiveFloorPlan.FloorPlanHash != ActiveIntent.FloorPlan.FloorPlanHash ||
		RuntimePolicyHash != ActiveIntent.GenerationContext.PolicyHash)
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("The active V6 identity no longer matches the accepted transaction.");
		}
		return false;
	}
	return true;
}

void UEFCalystoDungeonSubsystem::SetFailure(
	const FName FailureCode, const FString& FailureMessage)
{
	LastFailureCode = FailureCode;
	LastFailureMessage = FailureMessage;
	UE_LOG(LogEFCalystoDungeon, Error, TEXT("CALYSTO_V6_FAILURE code=%s message=%s"),
		*FailureCode.ToString(), *FailureMessage);
}

FEFCalystoDungeonSnapshotV6 UEFCalystoDungeonSubsystem::BuildSnapshot() const
{
	FEFCalystoDungeonSnapshotV6 Snapshot;
	Snapshot.State = TravelState;
	Snapshot.TravelKind = bTravelRequestPending ? PendingTravelKind : ActiveTravelKind;
	Snapshot.bHasActiveRun = bHasActiveRun;
	Snapshot.bPolicyValid = RuntimePolicy != nullptr &&
		FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(RuntimePolicyHash);
	Snapshot.PolicyError = Snapshot.bPolicyValid ? FString() : LastFailureMessage;
	Snapshot.bHasQueuedDirectorIntent = bHasQueuedDirectorIntent;
	if (bHasActiveRun)
	{
		Snapshot.RunSeed = ActiveContext.RunSeed;
		Snapshot.FloorNumber = ActiveContext.FloorNumber;
		Snapshot.GenerationSerial = ActiveContext.GenerationSerial;
		Snapshot.StyleId = ActiveIntent.StyleId;
		Snapshot.DungeonSize = ActiveIntent.DungeonSize;
		Snapshot.PCGSeed = ActiveIntent.PCGSeed;
		Snapshot.FloorPlanHash = ActiveFloorPlan.FloorPlanHash;
		Snapshot.FloorIntentHash = ActiveIntent.IntentHash;
		Snapshot.RoomManifestHash = ActiveRoomManifest.ManifestHash;
		Snapshot.PopulationManifestHash = ActiveRealizedManifest.ManifestHash;
	}
	Snapshot.bPCGComplete = TravelState == EEFCalystoDungeonTravelStateV6::Ready;
	Snapshot.bNavigationPathReady = TravelState == EEFCalystoDungeonTravelStateV6::Ready;
	Snapshot.bRoomManifestReady = !ActiveRoomManifest.ManifestHash.IsEmpty();
	Snapshot.bPopulationReady = bPopulationRealized;
	FString VisualError;
	Snapshot.bVisualsReady = LoadCoordinator.IsPhaseReady(
		EEFCalystoLoadPhase::FloorVisual, VisualError);
	Snapshot.bDoorEnabled = TravelState == EEFCalystoDungeonTravelStateV6::Ready;
	Snapshot.FailureReason = LastFailureMessage;
	Snapshot.SnapshotHash = FEFCalystoDungeonRuntimeMathV6::ComputeDungeonSnapshotHash(Snapshot);
	return Snapshot;
}

FString UEFCalystoDungeonSubsystem::SampleDirectorRolls(const int32 SampleCount) const
{
#if UE_BUILD_SHIPPING
	return FString();
#else
	if (!RuntimePolicy || SampleCount <= 0)
	{
		return TEXT("Director V6 policy is not resident.");
	}
	TMap<FName, int32> StyleCounts;
	for (int32 Index = 0; Index < FMath::Min(SampleCount, 100000); ++Index)
	{
		FEFCalystoResolvedFloorPlanV6 Plan;
		FString Error;
		const int64 Seed = static_cast<int64>(Index) + 1;
		if (RuntimePolicy->BuildResolvedFloorPlan(Seed, Plan, Error))
		{
			++StyleCounts.FindOrAdd(Plan.StyleId);
		}
	}
	TArray<FName> StyleIds;
	StyleCounts.GetKeys(StyleIds);
	StyleIds.Sort(FNameLexicalLess());
	TArray<FString> Parts;
	for (const FName StyleId : StyleIds)
	{
		Parts.Add(FString::Printf(TEXT("%s=%d"),
			*StyleId.ToString(), StyleCounts.FindChecked(StyleId)));
	}
	return FString::Printf(TEXT("Calysto V6 Style samples (%d): %s"),
		SampleCount, *FString::Join(Parts, TEXT(", ")));
#endif
}
