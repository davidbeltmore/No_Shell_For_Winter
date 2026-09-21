#include "Calysto/ProjectCalystoPopulationBridge.h"

#include "Actors/ACFCharacter.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Characters/ProjectEnemyLevelSubsystem.h"
#include "Companions/ProjectCompanionRuntimeAdapter.h"
#include "Companions/ProjectRecruitableCompanionComponent.h"
#include "Companions/ProjectRunCompanionSubsystem.h"
#include "Components/ACFCompanionGroupAIComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Items/ACFItem.h"
#include "Lockpicking/ProjectCalystoChest.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectCalystoPopulationBridge, Log, All);

namespace ProjectCalystoPopulationBridgePrivate
{
	static const FName DirectorAssignedLevelTag(TEXT("EF.Calysto.V6.DirectorAssignedLevel"));
	static const FString DirectorLogicalLevelPrefix(TEXT("EF.Calysto.V6.LogicalLevel."));

	bool IsCategory(const FName Value, const TCHAR* Expected)
	{
		return Value.IsEqual(FName(Expected), ENameCase::IgnoreCase);
	}

	EProjectCompanionDifficultyGrade ToProjectGrade(const EEFCalystoRarityTierV6 Tier)
	{
		switch (Tier)
		{
		case EEFCalystoRarityTierV6::Uncommon: return EProjectCompanionDifficultyGrade::Uncommon;
		case EEFCalystoRarityTierV6::Rare: return EProjectCompanionDifficultyGrade::Rare;
		case EEFCalystoRarityTierV6::Epic: return EProjectCompanionDifficultyGrade::Epic;
		case EEFCalystoRarityTierV6::Common:
		default: return EProjectCompanionDifficultyGrade::Common;
		}
	}

	FGuid StableGuidFromDecisionId(const FString& DecisionId)
	{
		FGuid Result;
		if (DecisionId.Len() >= 32)
		{
			FGuid::ParseExact(DecisionId.Left(32), EGuidFormats::Digits, Result);
		}
		return Result;
	}

	void ResolveEntryIdentity(const FName EntryId, FName& OutArchetype, FName& OutGender)
	{
		OutArchetype = TEXT("Generalist");
		OutGender = TEXT("Any");
		TArray<FString> Tokens;
		EntryId.ToString().ParseIntoArray(Tokens, TEXT("."), true);
		if (Tokens.IsEmpty())
		{
			return;
		}
		const FString& Last = Tokens.Last();
		if (Last.Equals(TEXT("Female"), ESearchCase::IgnoreCase)
			|| Last.Equals(TEXT("Male"), ESearchCase::IgnoreCase))
		{
			OutGender = FName(*Last);
			if (Tokens.Num() >= 2)
			{
				OutArchetype = FName(*Tokens[Tokens.Num() - 2]);
			}
		}
		else
		{
			OutArchetype = FName(*Last);
		}
	}

	UProjectRunCompanionSubsystem* ResolveRoster(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UProjectRunCompanionSubsystem>() : nullptr;
	}

	int32 ResolveLogicalLevel(const FEFCalystoPopulationPlanV6& Plan)
	{
		return static_cast<int32>(FMath::Clamp<int64>(Plan.FloorNumber, 1, MAX_int32));
	}

	bool StampDeferredDirectorLevelIdentity(
		APawn* DeferredPawn,
		const int32 ExpectedLogicalLevel,
		FString& OutError)
	{
		OutError.Reset();
		if (!IsValid(DeferredPawn) || ExpectedLogicalLevel <= 0
			|| DeferredPawn->HasActorBegunPlay())
		{
			OutError = TEXT("The deferred Calysto enemy cannot receive a valid pre-BeginPlay level identity.");
			return false;
		}

		int32 LogicalTagCount = 0;
		int32 ExistingLogicalLevel = 0;
		for (const FName Tag : DeferredPawn->Tags)
		{
			const FString TagText = Tag.ToString();
			if (!TagText.StartsWith(DirectorLogicalLevelPrefix, ESearchCase::CaseSensitive))
			{
				continue;
			}
			++LogicalTagCount;
			int64 ParsedLevel = 0;
			if (!LexTryParseString(ParsedLevel, *TagText.Mid(DirectorLogicalLevelPrefix.Len()))
				|| ParsedLevel <= 0 || ParsedLevel > static_cast<int64>(MAX_int32))
			{
				OutError = TEXT("The deferred Calysto enemy contains a malformed Director logical-level tag.");
				return false;
			}
			ExistingLogicalLevel = static_cast<int32>(ParsedLevel);
		}

		const bool bHasMarker = DeferredPawn->ActorHasTag(DirectorAssignedLevelTag);
		if (bHasMarker || LogicalTagCount > 0)
		{
			if (!bHasMarker || LogicalTagCount != 1
				|| ExistingLogicalLevel != ExpectedLogicalLevel)
			{
				OutError = FString::Printf(
					TEXT("The deferred Calysto enemy contains a conflicting Director level identity: expected %d, tagged %d, count %d."),
					ExpectedLogicalLevel, ExistingLogicalLevel, LogicalTagCount);
				return false;
			}
			return true;
		}

		DeferredPawn->Tags.AddUnique(DirectorAssignedLevelTag);
		DeferredPawn->Tags.AddUnique(FName(*FString::Printf(
			TEXT("%s%d"), *DirectorLogicalLevelPrefix, ExpectedLogicalLevel)));
		return true;
	}
}

bool FProjectCalystoPopulationBridge::HandlesCategory(const FName CategoryId) const
{
	using namespace ProjectCalystoPopulationBridgePrivate;
	return IsCategory(CategoryId, TEXT("Enemy"))
		|| IsCategory(CategoryId, TEXT("NPC"))
		|| IsCategory(CategoryId, TEXT("Chest"));
}

bool FProjectCalystoPopulationBridge::ValidateCompanionRosterReady(
	UWorld* World,
	const FEFCalystoPopulationPlanV6& Plan,
	const FString& ExpectedSnapshotHash,
	FString& OutError) const
{
	OutError.Reset();
	UProjectRunCompanionSubsystem* Roster =
		ProjectCalystoPopulationBridgePrivate::ResolveRoster(World);
	UEFCalystoDungeonSubsystem* Director = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	if (!Roster || !Director || Plan.PopulationHash.IsEmpty()
		|| ExpectedSnapshotHash.IsEmpty())
	{
		OutError = TEXT("The V6 companion-readiness provider received an invalid world, plan, hash, or subsystem.");
		return false;
	}

	const FEFCalystoPopulationPlanV6 ActivePlan = Director->GetPopulationPlanV6();
	const FEFCalystoResolvedFloorIntentV6 Intent = Director->GetResolvedFloorIntent();
	if (!Intent.bIsValid
		|| ActivePlan.PopulationHash != Plan.PopulationHash
		|| Intent.GenerationContext.FloorNumber != Plan.FloorNumber
		|| Intent.FloorPlan.FloorPlanHash != Plan.FloorPlanHash
		|| Intent.CompanionRoster.SnapshotHash != ExpectedSnapshotHash)
	{
		OutError = TEXT("The V6 population, floor intent, and expected companion snapshot do not share one immutable identity.");
		return false;
	}

	if (Roster->IsReadyForDirectorSnapshot(ExpectedSnapshotHash, OutError))
	{
		return true;
	}
	OutError.Reset();
	if (!Roster->MaterializeActivePartyProjectionsForFloor(Intent, OutError)
		|| !Roster->FinalizeDirectorRosterReadiness(ExpectedSnapshotHash, OutError)
		|| !Roster->IsReadyForDirectorSnapshot(ExpectedSnapshotHash, OutError))
	{
		Roster->RollbackDirectorRosterProjections();
		if (OutError.IsEmpty())
		{
			OutError = TEXT("The project companion roster failed its V6 realization transaction.");
		}
		return false;
	}
	return true;
}

bool FProjectCalystoPopulationBridge::GatherAdditionalPreloadPaths(
	const FEFCalystoPopulationPlanV6& Plan,
	TArray<FSoftObjectPath>& OutAssetPaths,
	FString& OutError) const
{
	using namespace ProjectCalystoPopulationBridgePrivate;
	OutAssetPaths.Reset();
	OutError.Reset();
	bool bHasNpc = false;
	bool bHasChest = false;
	for (const FEFCalystoRoomPopulationPlanV6& Room : Plan.Rooms)
	{
		for (const FEFCalystoPopulationDecisionV6& Decision : Room.Decisions)
		{
			if (Decision.Kind != EEFCalystoPopulationDecisionKindV6::Actor)
			{
				continue;
			}
			bHasNpc |= IsCategory(Decision.CategoryId, TEXT("NPC"));
			bHasChest |= IsCategory(Decision.CategoryId, TEXT("Chest"));
		}
	}
	if (bHasChest)
	{
		const FSoftObjectPath ChestVisualPath = AProjectCalystoChest::GetDefaultVisualMeshPath();
		if (!ChestVisualPath.IsValid())
		{
			OutError = TEXT("The project Calysto chest visual preload contract is invalid.");
			return false;
		}
		OutAssetPaths.AddUnique(ChestVisualPath);
	}
	if (bHasNpc)
	{
		const FProjectCompanionDefinition RepairContract;
		if (RepairContract.bRepairMissingStatisticsRow)
		{
			const FSoftObjectPath RepairTablePath =
				RepairContract.StatisticsRepairDataTable.ToSoftObjectPath();
			if (!RepairTablePath.IsValid() || RepairContract.StatisticsRepairRow.IsNone())
			{
				OutError = TEXT("The project companion statistics-repair preload contract is incomplete.");
				return false;
			}
			OutAssetPaths.AddUnique(RepairTablePath);
		}
	}
	return true;
}

void FProjectCalystoPopulationBridge::PruneTransientState() const
{
	for (auto It = PendingNPCs.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = CommittedPopulationByWorld.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool FProjectCalystoPopulationBridge::BuildRandomNPCDefinition(
	const FEFCalystoPopulationPlanV6& Plan,
	const FEFCalystoPopulationDecisionV6& Decision,
	FProjectCompanionDefinition& OutDefinition,
	FString& OutError)
{
	using namespace ProjectCalystoPopulationBridgePrivate;
	OutDefinition = FProjectCompanionDefinition();
	OutError.Reset();
	if (!IsCategory(Decision.CategoryId, TEXT("NPC"))
		|| Decision.Kind != EEFCalystoPopulationDecisionKindV6::Actor
		|| Decision.DecisionId.IsEmpty()
		|| Decision.EntryId.IsNone()
		|| !Decision.ClassPath.IsValid()
		|| Plan.FloorNumber < 1)
	{
		OutError = TEXT("The Calysto NPC decision is incomplete.");
		return false;
	}

	OutDefinition.StableCompanionId = StableGuidFromDecisionId(Decision.DecisionId);
	OutDefinition.SourceSpawnId = FName(*Decision.DecisionId);
	OutDefinition.ContentId = Decision.EntryId;
	OutDefinition.CatalogVariantId = Decision.EntryId;
	OutDefinition.CharacterClass = TSoftClassPtr<AACFCharacter>(Decision.ClassPath);
	ResolveEntryIdentity(Decision.EntryId, OutDefinition.Archetype, OutDefinition.Gender);
	OutDefinition.DifficultyGrade = ToProjectGrade(Decision.Tier);
	OutDefinition.ResolvedLevel = ResolveLogicalLevel(Plan);
	OutDefinition.Lifecycle = Decision.Lifecycle == EEFCalystoLifecycleV6::Recruitable
		? EProjectCompanionLifecycle::Recruitable
		: EProjectCompanionLifecycle::FloorLocal;
	if (!OutDefinition.StableCompanionId.IsValid())
	{
		OutError = TEXT("The Calysto NPC Decision ID could not produce a stable companion GUID.");
		return false;
	}
	return OutDefinition.IsValid(OutError);
}

bool FProjectCalystoPopulationBridge::PrepareDeferredActor(
	UWorld* World,
	AActor* DeferredActor,
	const FEFCalystoPopulationPlanV6& Plan,
	const FEFCalystoPopulationDecisionV6& ActorDecision,
	const TConstArrayView<FEFCalystoPopulationDecisionV6> ChestContents,
	FString& OutError)
{
	using namespace ProjectCalystoPopulationBridgePrivate;
	OutError.Reset();
	PruneTransientState();
	if (!IsValid(World) || !IsValid(DeferredActor)
		|| DeferredActor->GetWorld() != World || !HandlesCategory(ActorDecision.CategoryId))
	{
		OutError = TEXT("The project Calysto population bridge received an invalid world, actor, or category.");
		return false;
	}
	UClass* FrozenActorClass = Cast<UClass>(ActorDecision.ClassPath.ResolveObject());
	if (!IsValid(FrozenActorClass) || !DeferredActor->IsA(FrozenActorClass))
	{
		OutError = TEXT("The deferred actor does not match its asynchronously preloaded Calysto class.");
		return false;
	}

	if (IsCategory(ActorDecision.CategoryId, TEXT("Enemy")))
	{
		APawn* Enemy = Cast<APawn>(DeferredActor);
		UProjectEnemyLevelSubsystem* EnemyLevels = World->GetSubsystem<UProjectEnemyLevelSubsystem>();
		const int32 LogicalLevel = ResolveLogicalLevel(Plan);
		if (!Enemy || !EnemyLevels || !ChestContents.IsEmpty())
		{
			OutError = TEXT("The deferred Calysto enemy or its level subsystem is invalid.");
			return false;
		}
		return StampDeferredDirectorLevelIdentity(Enemy, LogicalLevel, OutError)
			&& EnemyLevels->PrepareDeferredDirectorEnemy(
			Enemy, LogicalLevel, FMath::Min(LogicalLevel, 100), OutError);
	}

	if (IsCategory(ActorDecision.CategoryId, TEXT("NPC")))
	{
		AACFCharacter* Character = Cast<AACFCharacter>(DeferredActor);
		const TWeakObjectPtr<AActor> ActorKey(DeferredActor);
		if (!Character || !ChestContents.IsEmpty() || PendingNPCs.Contains(ActorKey)
			|| !ResolveRoster(World))
		{
			OutError = TEXT("The deferred Calysto NPC, roster, or single-prepare invariant is invalid.");
			return false;
		}
		FPendingNPC Pending;
		if (!BuildRandomNPCDefinition(Plan, ActorDecision, Pending.Definition, OutError)
			|| !UProjectCompanionRuntimeAdapter::PrepareDeferredCompanion(
				Character,
				Pending.Definition,
				OutError,
				&Pending.bStatisticsRepairApplied))
		{
			return false;
		}
		PendingNPCs.Add(ActorKey, MoveTemp(Pending));
		return true;
	}

	AProjectCalystoChest* Chest = Cast<AProjectCalystoChest>(DeferredActor);
	if (!Chest || ChestContents.Num() > 3)
	{
		OutError = TEXT("The selected Calysto chest actor or content count violates the frozen contract.");
		return false;
	}
	TArray<FProjectCalystoResolvedChestEntry> ResolvedEntries;
	ResolvedEntries.Reserve(ChestContents.Num());
	TSet<FString> DecisionIds;
	for (const FEFCalystoPopulationDecisionV6& Content : ChestContents)
	{
		UClass* ContentClass = Cast<UClass>(Content.ClassPath.ResolveObject());
		if (Content.Kind != EEFCalystoPopulationDecisionKindV6::ChestContent
			|| Content.ParentDecisionId != ActorDecision.DecisionId
			|| Content.StableRoomId != ActorDecision.StableRoomId
			|| Content.DecisionId.IsEmpty()
			|| DecisionIds.Contains(Content.DecisionId)
			|| !IsValid(ContentClass)
			|| !ContentClass->IsChildOf(UACFItem::StaticClass()))
		{
			OutError = TEXT("A Calysto chest-content decision is invalid, not resident, or belongs to another parent.");
			return false;
		}
		DecisionIds.Add(Content.DecisionId);
		FProjectCalystoResolvedChestEntry& Entry = ResolvedEntries.AddDefaulted_GetRef();
		Entry.StableAttemptId = FName(*Content.DecisionId);
		Entry.ContentCatalogId = Content.EntryId;
		Entry.ItemClass = ContentClass;
		Entry.Quantity = 1;
	}
	return Chest->ConfigureResolvedLoot(ResolvedEntries, OutError);
}

bool FProjectCalystoPopulationBridge::FinalizeSpawnedActor(
	UWorld* World,
	AActor* SpawnedActor,
	const FEFCalystoPopulationPlanV6& Plan,
	const FEFCalystoPopulationDecisionV6& ActorDecision,
	const TConstArrayView<FEFCalystoPopulationDecisionV6> ChestContents,
	TArray<FString>& OutVerifiedChestContentDecisionIds,
	FString& OutError)
{
	using namespace ProjectCalystoPopulationBridgePrivate;
	OutVerifiedChestContentDecisionIds.Reset();
	OutError.Reset();
	PruneTransientState();
	if (!IsValid(World) || !IsValid(SpawnedActor) || SpawnedActor->GetWorld() != World)
	{
		OutError = TEXT("The finished Calysto bridge actor or world is invalid.");
		return false;
	}
	UClass* FrozenActorClass = Cast<UClass>(ActorDecision.ClassPath.ResolveObject());
	if (!IsValid(FrozenActorClass) || !SpawnedActor->IsA(FrozenActorClass))
	{
		OutError = TEXT("The finished Calysto bridge actor drifted from its frozen class.");
		return false;
	}

	if (IsCategory(ActorDecision.CategoryId, TEXT("Enemy")))
	{
		APawn* Enemy = Cast<APawn>(SpawnedActor);
		UProjectEnemyLevelSubsystem* EnemyLevels = World->GetSubsystem<UProjectEnemyLevelSubsystem>();
		if (!Enemy || !EnemyLevels || !ChestContents.IsEmpty()
			|| !EnemyLevels->InitializeDirectorEnemySynchronously(
				Enemy, ResolveLogicalLevel(Plan), OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("The Calysto enemy could not complete its exact logical-level transaction.");
			}
			return false;
		}
		return true;
	}

	if (IsCategory(ActorDecision.CategoryId, TEXT("NPC")))
	{
		AACFCharacter* Character = Cast<AACFCharacter>(SpawnedActor);
		const TWeakObjectPtr<AActor> ActorKey(SpawnedActor);
		FPendingNPC* Pending = PendingNPCs.Find(ActorKey);
		if (!Character || !Pending || Pending->bFinalized || !ResolveRoster(World))
		{
			OutError = TEXT("The Calysto NPC was not prepared exactly once or lacks the roster subsystem.");
			return false;
		}
		const FProjectCompanionSpawnResult SpawnResult =
			UProjectCompanionRuntimeAdapter::FinalizeDeferredCompanion(
				Character, Pending->Definition, nullptr, false);
		if (!SpawnResult.bSucceeded)
		{
			UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Character, nullptr);
			OutError = SpawnResult.Diagnostic;
			PendingNPCs.Remove(ActorKey);
			return false;
		}

		bool bRecruitmentHookReady = false;
		if (Pending->Definition.Lifecycle == EProjectCompanionLifecycle::Recruitable)
		{
			UACFCompanionGroupAIComponent* RecruitmentGroup =
				UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(
					World->GetFirstPlayerController()
						? World->GetFirstPlayerController()->GetPawn()
						: nullptr);
			UProjectRecruitableCompanionComponent* RecruitmentHook =
				NewObject<UProjectRecruitableCompanionComponent>(
					Character, TEXT("ProjectRecruitableCompanion"), RF_Transient);
			if (!RecruitmentHook)
			{
				UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Character, nullptr);
				OutError = TEXT("Could not allocate the canonical ACF recruitment hook.");
				PendingNPCs.Remove(ActorKey);
				return false;
			}
			Character->AddInstanceComponent(RecruitmentHook);
			RecruitmentHook->RegisterComponent();
			if (!RecruitmentHook->InitializeRecruitmentHook(
					Pending->Definition, RecruitmentGroup, OutError))
			{
				RecruitmentHook->DestroyComponent();
				UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Character, nullptr);
				PendingNPCs.Remove(ActorKey);
				return false;
			}
			bRecruitmentHookReady = true;
		}
		Pending->bFinalized = true;
		UE_LOG(
			LogProjectCalystoPopulationBridge,
			Verbose,
			TEXT("CALYSTO_V6_COMPANION_VERIFIED decision=%s hook=%s repair_used=%s."),
			*ActorDecision.DecisionId,
			bRecruitmentHookReady ? TEXT("true") : TEXT("false"),
			Pending->bStatisticsRepairApplied ? TEXT("true") : TEXT("false"));
		return true;
	}

	AProjectCalystoChest* Chest = Cast<AProjectCalystoChest>(SpawnedActor);
	if (!IsCategory(ActorDecision.CategoryId, TEXT("Chest")) || !Chest)
	{
		OutError = TEXT("The finalized Calysto bridge actor is not the selected chest class.");
		return false;
	}
	TArray<FName> VerifiedCatalogIds;
	if (!Chest->FinalizeAndVerifyResolvedLoot(VerifiedCatalogIds, OutError)
		|| VerifiedCatalogIds.Num() != ChestContents.Num())
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("The Calysto chest did not verify every frozen content decision.");
		}
		return false;
	}
	for (int32 Index = 0; Index < ChestContents.Num(); ++Index)
	{
		if (VerifiedCatalogIds[Index] != ChestContents[Index].EntryId)
		{
			OutError = TEXT("The verified chest catalog order drifted from its frozen decisions.");
			return false;
		}
		OutVerifiedChestContentDecisionIds.Add(ChestContents[Index].DecisionId);
	}
	return true;
}

void FProjectCalystoPopulationBridge::RollbackSpawnedActor(
	UWorld* World,
	AActor* SpawnedActor,
	const FEFCalystoPopulationPlanV6& Plan,
	const FEFCalystoPopulationDecisionV6& ActorDecision)
{
	using namespace ProjectCalystoPopulationBridgePrivate;
	(void)Plan;
	PruneTransientState();
	if (!IsValid(SpawnedActor))
	{
		return;
	}
	if (IsCategory(ActorDecision.CategoryId, TEXT("Enemy")))
	{
		if (UProjectEnemyLevelSubsystem* EnemyLevels = World
			? World->GetSubsystem<UProjectEnemyLevelSubsystem>()
			: nullptr)
		{
			EnemyLevels->RollbackDirectorEnemy(Cast<APawn>(SpawnedActor));
		}
		SpawnedActor->Destroy();
		return;
	}
	if (IsCategory(ActorDecision.CategoryId, TEXT("NPC")))
	{
		AACFCharacter* Character = Cast<AACFCharacter>(SpawnedActor);
		const TWeakObjectPtr<AActor> ActorKey(SpawnedActor);
		const FPendingNPC* Pending = PendingNPCs.Find(ActorKey);
		if (Pending)
		{
			if (UProjectRunCompanionSubsystem* Roster = ResolveRoster(World))
			{
				Roster->RollbackUncommittedDirectorRecruitment(
					Pending->Definition.StableCompanionId, Character);
			}
		}
		UACFCompanionGroupAIComponent* Group =
			UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(
				World && World->GetFirstPlayerController()
					? World->GetFirstPlayerController()->GetPawn()
					: nullptr);
		UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Character, Group);
		PendingNPCs.Remove(ActorKey);
		return;
	}
	SpawnedActor->Destroy();
}

bool FProjectCalystoPopulationBridge::CommitPopulationPlan(
	UWorld* World,
	const FEFCalystoPopulationPlanV6& Plan,
	FString& OutError)
{
	OutError.Reset();
	PruneTransientState();
	if (!IsValid(World) || Plan.PopulationHash.IsEmpty())
	{
		OutError = TEXT("The project Calysto bridge cannot commit an invalid world or population hash.");
		return false;
	}
	const TWeakObjectPtr<UWorld> WorldKey(World);
	if (const FString* Existing = CommittedPopulationByWorld.Find(WorldKey))
	{
		OutError = FString::Printf(
			TEXT("A Calysto population plan is already committed in this world (%s)."),
			**Existing);
		return false;
	}
	CommittedPopulationByWorld.Add(WorldKey, Plan.PopulationHash);
	return true;
}

void FProjectCalystoPopulationBridge::RollbackPopulationPlan(
	UWorld* World,
	const FEFCalystoPopulationPlanV6& Plan)
{
	PruneTransientState();
	const TWeakObjectPtr<UWorld> WorldKey(World);
	if (const FString* Existing = CommittedPopulationByWorld.Find(WorldKey);
		Existing && *Existing == Plan.PopulationHash)
	{
		CommittedPopulationByWorld.Remove(WorldKey);
	}
	if (UProjectRunCompanionSubsystem* Roster =
		ProjectCalystoPopulationBridgePrivate::ResolveRoster(World))
	{
		Roster->RollbackDirectorRosterProjections();
	}
}
