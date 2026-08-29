#include "Calysto/ProjectCalystoPopulationBridgeV4.h"

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
#include "Lockpicking/ProjectCalystoChestV4.h"
#include "Misc/SecureHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectCalystoPopulationBridgeV4, Log, All);

namespace ProjectCalystoPopulationBridgeV4Private
{
	EProjectCompanionDifficultyGrade ToProjectGrade(const EEFCalystoRarityTierV4 Tier)
	{
		switch (Tier)
		{
		case EEFCalystoRarityTierV4::Uncommon: return EProjectCompanionDifficultyGrade::Uncommon;
		case EEFCalystoRarityTierV4::Rare: return EProjectCompanionDifficultyGrade::Rare;
		case EEFCalystoRarityTierV4::Epic: return EProjectCompanionDifficultyGrade::Epic;
		case EEFCalystoRarityTierV4::Winter: return EProjectCompanionDifficultyGrade::Winter;
		case EEFCalystoRarityTierV4::Common:
		default: return EProjectCompanionDifficultyGrade::Common;
		}
	}

	FName ToProjectGender(const EEFCalystoGenderV4 Gender)
	{
		switch (Gender)
		{
		case EEFCalystoGenderV4::Female: return TEXT("Female");
		case EEFCalystoGenderV4::Male: return TEXT("Male");
		case EEFCalystoGenderV4::Any:
		default: return TEXT("Any");
		}
	}

	UProjectRunCompanionSubsystem* ResolveRoster(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UProjectRunCompanionSubsystem>() : nullptr;
	}
}

bool FProjectCalystoPopulationBridgeV4::HandlesCategory(
	const EEFCalystoContentCategoryV4 Category) const
{
	return Category == EEFCalystoContentCategoryV4::Enemy
		|| Category == EEFCalystoContentCategoryV4::NPC
		|| Category == EEFCalystoContentCategoryV4::Chest;
}

bool FProjectCalystoPopulationBridgeV4::GatherAdditionalPreloadPaths(
	const FEFCalystoResolvedFloorIntentV4& Intent,
	TArray<FSoftObjectPath>& OutAssetPaths,
	FString& OutError) const
{
	OutAssetPaths.Reset();
	OutError.Reset();
	if (!Intent.bIsValid || Intent.GeneratorVersion != 4)
	{
		OutError = TEXT("The project V4 preload bridge requires a valid frozen intent.");
		return false;
	}

	const bool bHasNPCDirective = Intent.SpawnDirectives.ContainsByPredicate(
		[](const FEFCalystoSpawnInstanceDirectiveV4& Directive)
		{
			return Directive.Category == EEFCalystoContentCategoryV4::NPC;
		});
	const bool bHasChestDirective = Intent.SpawnDirectives.ContainsByPredicate(
		[](const FEFCalystoSpawnInstanceDirectiveV4& Directive)
		{
			return Directive.Category == EEFCalystoContentCategoryV4::Chest;
		});
	if (bHasChestDirective)
	{
		const FSoftObjectPath ChestVisualPath = AProjectCalystoChestV4::GetDefaultVisualMeshPath();
		if (!ChestVisualPath.IsValid())
		{
			OutError = TEXT("The project V4 chest visual preload contract is invalid.");
			return false;
		}
		OutAssetPaths.AddUnique(ChestVisualPath);
	}

	// Every current NPC definition is repair-capable. The adapter uses this
	// table only when the selected ACF instance actually lacks CharacterRow;
	// preloading it for an NPC floor keeps PrepareDeferredActor load-free.
	if (bHasNPCDirective)
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

void FProjectCalystoPopulationBridgeV4::PruneInvalidPendingNPCs() const
{
	for (auto It = PendingNPCs.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

FGuid FProjectCalystoPopulationBridgeV4::StableGuidFromInstanceId(
	const FEFCalystoResolvedFloorIntentV4& Intent,
	const FName StableInstanceId)
{
	if (!Intent.bIsValid || StableInstanceId.IsNone())
	{
		return FGuid();
	}
	const FString Digest = FMD5::HashAnsiString(
		*FString::Printf(TEXT("CalystoDirectorV4|NPC|%lld|%lld|%lld|%d|%s|%s|%s"),
			Intent.RunSeed,
			Intent.FloorNumber,
			Intent.GenerationSerial,
			Intent.GeneratorVersion,
			*Intent.PolicyHash,
			*Intent.EcologyHash,
			*StableInstanceId.ToString()));
	FGuid Result;
	FGuid::ParseExact(Digest, EGuidFormats::Digits, Result);
	return Result;
}

bool FProjectCalystoPopulationBridgeV4::BuildRandomNPCDefinition(
	UWorld* World,
	const FEFCalystoSpawnInstanceDirectiveV4& Directive,
	FProjectCompanionDefinition& OutDefinition,
	FString& OutError)
{
	OutDefinition = FProjectCompanionDefinition();
	OutError.Reset();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UEFCalystoDungeonSubsystem* Director = GameInstance
		? GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	const FEFCalystoResolvedFloorIntentV4 Intent = Director
		? Director->GetResolvedFloorIntent()
		: FEFCalystoResolvedFloorIntentV4();
	return BuildRandomNPCDefinitionFromIntent(Intent, Directive, OutDefinition, OutError);
}

bool FProjectCalystoPopulationBridgeV4::BuildRandomNPCDefinitionFromIntent(
	const FEFCalystoResolvedFloorIntentV4& Intent,
	const FEFCalystoSpawnInstanceDirectiveV4& Directive,
	FProjectCompanionDefinition& OutDefinition,
	FString& OutError)
{
	OutDefinition = FProjectCompanionDefinition();
	OutError.Reset();
	if (!Intent.bIsValid || Directive.Category != EEFCalystoContentCategoryV4::NPC
		|| Directive.StableCompanionId.IsValid()
		|| Directive.StableInstanceId.IsNone()
		|| Directive.CatalogId.IsNone()
		|| Directive.ActorClass.IsNull()
		|| Directive.LogicalLevel < 1
		|| Directive.PhysicalACFLevel != FMath::Min(Directive.LogicalLevel, 100))
	{
		OutError = TEXT("The V4 random NPC directive is incomplete or incorrectly identifies a roster companion.");
		return false;
	}

	OutDefinition.StableCompanionId = StableGuidFromInstanceId(Intent, Directive.StableInstanceId);
	OutDefinition.SourceSpawnId = Directive.StableInstanceId;
	OutDefinition.ContentId = Directive.CatalogId;
	OutDefinition.CatalogVariantId = Directive.VariantId;
	OutDefinition.CharacterClass = TSoftClassPtr<AACFCharacter>(Directive.ActorClass.ToSoftObjectPath());
	OutDefinition.Archetype = Directive.Archetype;
	OutDefinition.Gender = ProjectCalystoPopulationBridgeV4Private::ToProjectGender(Directive.Gender);
	OutDefinition.DifficultyGrade = ProjectCalystoPopulationBridgeV4Private::ToProjectGrade(Directive.Tier);
	OutDefinition.ResolvedLevel = Directive.LogicalLevel;
	OutDefinition.Lifecycle = Directive.Lifecycle == EEFCalystoLifecycleV4::Recruitable
		? EProjectCompanionLifecycle::Recruitable
		: EProjectCompanionLifecycle::FloorLocal;
	return OutDefinition.IsValid(OutError);
}

bool FProjectCalystoPopulationBridgeV4::PrepareDeferredActor(
	UWorld* World,
	AActor* DeferredActor,
	const FEFCalystoSpawnInstanceDirectiveV4& Directive,
	const TConstArrayView<FEFCalystoChestContentDirectiveV4> ChestContent,
	FString& OutError)
{
	OutError.Reset();
	PruneInvalidPendingNPCs();
	if (!World || !DeferredActor || DeferredActor->GetWorld() != World
		|| !HandlesCategory(Directive.Category))
	{
		OutError = TEXT("The V4 population bridge received an invalid world, actor or category.");
		return false;
	}
	UClass* FrozenActorClass = Directive.ActorClass.Get();
	if (!FrozenActorClass || !DeferredActor->IsA(FrozenActorClass))
	{
		OutError = TEXT("The deferred actor does not match the preloaded class frozen in the V4 directive.");
		return false;
	}

	if (Directive.Category == EEFCalystoContentCategoryV4::Enemy)
	{
		APawn* Enemy = Cast<APawn>(DeferredActor);
		UProjectEnemyLevelSubsystem* EnemyLevels = World->GetSubsystem<UProjectEnemyLevelSubsystem>();
		if (!Enemy || !EnemyLevels || !ChestContent.IsEmpty())
		{
			OutError = TEXT("The deferred V4 enemy, level subsystem or empty-content invariant is invalid.");
			return false;
		}
		return EnemyLevels->PrepareDeferredDirectorEnemy(
			Enemy,
			Directive.LogicalLevel,
			Directive.PhysicalACFLevel,
			OutError);
	}

	if (Directive.Category == EEFCalystoContentCategoryV4::NPC)
	{
		AACFCharacter* Character = Cast<AACFCharacter>(DeferredActor);
		const TWeakObjectPtr<AActor> ActorKey(DeferredActor);
		UProjectRunCompanionSubsystem* Roster =
			ProjectCalystoPopulationBridgeV4Private::ResolveRoster(World);
		if (!Character || !Roster || PendingNPCs.Contains(ActorKey))
		{
			OutError = TEXT("The deferred NPC, roster subsystem or single-prepare invariant is invalid.");
			return false;
		}

		FPendingNPC Pending;
		Pending.bRosterProjection = Directive.StableCompanionId.IsValid();
		const bool bDefinitionResolved = Pending.bRosterProjection
			? Roster->ResolveFrozenRosterProjection(Directive, Pending.Definition, OutError)
			: BuildRandomNPCDefinition(World, Directive, Pending.Definition, OutError);
		if (!bDefinitionResolved
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

	AProjectCalystoChestV4* Chest = Cast<AProjectCalystoChestV4>(DeferredActor);
	if (!Chest || Directive.ChestContentAttemptCount < 0
		|| Directive.ChestContentAttemptCount > 3
		|| ChestContent.Num() > Directive.ChestContentAttemptCount)
	{
		OutError = TEXT("The selected chest actor/content attempts violate the frozen V4 chest contract.");
		return false;
	}

	TArray<FProjectCalystoResolvedChestEntryV4> ResolvedEntries;
	ResolvedEntries.Reserve(ChestContent.Num());
	for (const FEFCalystoChestContentDirectiveV4& Content : ChestContent)
	{
		UClass* ContentClass = Content.ContentClass.Get();
		if (Content.ContainerInstanceId != Directive.StableInstanceId
			|| Content.StableAttemptId.IsNone() || Content.ContentCatalogId.IsNone()
			|| !ContentClass || !ContentClass->IsChildOf(UACFItem::StaticClass()))
		{
			OutError = TEXT("A frozen chest content class/ID is invalid, not preloaded or belongs to another container.");
			return false;
		}
		FProjectCalystoResolvedChestEntryV4& Entry = ResolvedEntries.AddDefaulted_GetRef();
		Entry.StableAttemptId = Content.StableAttemptId;
		Entry.ContentCatalogId = Content.ContentCatalogId;
		Entry.ItemClass = ContentClass;
		Entry.Quantity = 1;
	}
	return Chest->ConfigureResolvedLoot(ResolvedEntries, OutError);
}

bool FProjectCalystoPopulationBridgeV4::FinalizeSpawnedActor(
	UWorld* World,
	AActor* SpawnedActor,
	const FEFCalystoSpawnInstanceDirectiveV4& Directive,
	const TConstArrayView<FEFCalystoChestContentDirectiveV4> ChestContent,
	TArray<FEFCalystoChestContentDirectiveV4>& OutVerifiedChestContents,
	FString& OutError)
{
	OutVerifiedChestContents.Reset();
	OutError.Reset();
	PruneInvalidPendingNPCs();
	if (!World || !SpawnedActor || SpawnedActor->GetWorld() != World)
	{
		OutError = TEXT("The finished V4 bridge actor/world is invalid.");
		return false;
	}
	UClass* FrozenActorClass = Directive.ActorClass.Get();
	if (!FrozenActorClass || !SpawnedActor->IsA(FrozenActorClass))
	{
		OutError = TEXT("The finished bridge actor drifted from the frozen V4 actor class.");
		return false;
	}

	if (Directive.Category == EEFCalystoContentCategoryV4::Enemy)
	{
		APawn* Enemy = Cast<APawn>(SpawnedActor);
		UProjectEnemyLevelSubsystem* EnemyLevels = World->GetSubsystem<UProjectEnemyLevelSubsystem>();
		if (!Enemy || !EnemyLevels || !ChestContent.IsEmpty()
			|| !EnemyLevels->InitializeDirectorEnemySynchronously(
				Enemy, Directive.LogicalLevel, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("The finished V4 enemy could not complete its exact logical-level transaction.");
			}
			return false;
		}
		UE_LOG(
			LogProjectCalystoPopulationBridgeV4,
			Verbose,
			TEXT("EnemyLevelVerifiedV4 instance=%s logical=%d physical=%d actor=%s."),
			*Directive.StableInstanceId.ToString(),
			Directive.LogicalLevel,
			Directive.PhysicalACFLevel,
			*GetNameSafe(Enemy));
		return true;
	}

	if (Directive.Category == EEFCalystoContentCategoryV4::NPC)
	{
		AACFCharacter* Character = Cast<AACFCharacter>(SpawnedActor);
		const TWeakObjectPtr<AActor> ActorKey(SpawnedActor);
		FPendingNPC* Pending = PendingNPCs.Find(ActorKey);
		UProjectRunCompanionSubsystem* Roster =
			ProjectCalystoPopulationBridgeV4Private::ResolveRoster(World);
		if (!Character || !Pending || Pending->bFinalized || !Roster)
		{
			OutError = TEXT("The NPC was not prepared exactly once or lacks the roster subsystem.");
			return false;
		}

		UACFCompanionGroupAIComponent* Group = Pending->bRosterProjection
			? UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(World->GetFirstPlayerController()
				? World->GetFirstPlayerController()->GetPawn() : nullptr)
			: nullptr;
		const FProjectCompanionSpawnResult Result =
			UProjectCompanionRuntimeAdapter::FinalizeDeferredCompanion(
				Character, Pending->Definition, Group, Pending->bRosterProjection);
		if (!Result.bSucceeded)
		{
			UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Character, Group);
			OutError = Result.Diagnostic;
			PendingNPCs.Remove(ActorKey);
			return false;
		}
		if (Pending->bRosterProjection
			&& !Roster->AdoptDirectorRosterProjection(Character, Pending->Definition, OutError))
		{
			UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Character, Group);
			PendingNPCs.Remove(ActorKey);
			return false;
		}
		bool bRecruitmentHookReady = false;
		if (!Pending->bRosterProjection
			&& Pending->Definition.Lifecycle == EProjectCompanionLifecycle::Recruitable)
		{
			UACFCompanionGroupAIComponent* RecruitmentGroup =
				UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(
					World->GetFirstPlayerController()
						? World->GetFirstPlayerController()->GetPawn() : nullptr);
			UProjectRecruitableCompanionComponent* RecruitmentHook =
				NewObject<UProjectRecruitableCompanionComponent>(
					Character, TEXT("ProjectRecruitableCompanionV4"), RF_Transient);
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
			LogProjectCalystoPopulationBridgeV4,
			Log,
			TEXT("CALYSTO_V4_COMPANION_VERIFIED instance=%s hook=%s roster_projection=%s repair_used=%s %s"),
			*Directive.StableInstanceId.ToString(),
			bRecruitmentHookReady ? TEXT("true") : TEXT("false"),
			Pending->bRosterProjection ? TEXT("true") : TEXT("false"),
			Pending->bStatisticsRepairApplied ? TEXT("true") : TEXT("false"),
			*Result.Diagnostic);
		return true;
	}

	AProjectCalystoChestV4* Chest = Cast<AProjectCalystoChestV4>(SpawnedActor);
	if (Directive.Category != EEFCalystoContentCategoryV4::Chest || !Chest)
	{
		OutError = TEXT("The finalized bridge actor is not the selected V4 chest class.");
		return false;
	}
	TArray<FName> VerifiedCatalogIds;
	if (!Chest->FinalizeAndVerifyResolvedLoot(VerifiedCatalogIds, OutError)
		|| VerifiedCatalogIds.Num() != ChestContent.Num())
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("The V4 chest did not verify every frozen content directive.");
		}
		return false;
	}
	for (int32 Index = 0; Index < ChestContent.Num(); ++Index)
	{
		if (VerifiedCatalogIds[Index] != ChestContent[Index].ContentCatalogId)
		{
			OutError = TEXT("The verified chest catalog order drifted from the frozen directives.");
			return false;
		}
		OutVerifiedChestContents.Add(ChestContent[Index]);
	}
	return true;
}

void FProjectCalystoPopulationBridgeV4::RollbackSpawnedActor(
	UWorld* World,
	AActor* SpawnedActor,
	const FEFCalystoSpawnInstanceDirectiveV4& Directive)
{
	PruneInvalidPendingNPCs();
	if (!SpawnedActor)
	{
		return;
	}
	if (Directive.Category == EEFCalystoContentCategoryV4::Enemy)
	{
		APawn* Enemy = Cast<APawn>(SpawnedActor);
		if (UProjectEnemyLevelSubsystem* EnemyLevels = World
			? World->GetSubsystem<UProjectEnemyLevelSubsystem>()
			: nullptr)
		{
			EnemyLevels->RollbackDirectorEnemy(Enemy);
		}
		SpawnedActor->Destroy();
		return;
	}
	if (Directive.Category == EEFCalystoContentCategoryV4::NPC)
	{
		AACFCharacter* Character = Cast<AACFCharacter>(SpawnedActor);
		const TWeakObjectPtr<AActor> ActorKey(SpawnedActor);
		const FPendingNPC* Pending = PendingNPCs.Find(ActorKey);
		UProjectRunCompanionSubsystem* Roster =
			ProjectCalystoPopulationBridgeV4Private::ResolveRoster(World);
		if (Pending && Pending->bRosterProjection && Roster)
		{
			Roster->ReleaseDirectorRosterProjection(Pending->Definition.StableCompanionId, Character);
		}
		else if (Pending && Roster)
		{
			Roster->RollbackUncommittedDirectorRecruitment(
				Pending->Definition.StableCompanionId, Character);
		}
		UACFCompanionGroupAIComponent* Group =
			UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(
				World && World->GetFirstPlayerController()
					? World->GetFirstPlayerController()->GetPawn() : nullptr);
		UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Character, Group);
		PendingNPCs.Remove(ActorKey);
		return;
	}
	SpawnedActor->Destroy();
}

bool FProjectCalystoPopulationBridgeV4::IsCompanionRosterReady(
	UWorld* World,
	const FString& ExpectedCompanionSnapshotHash,
	FString& OutError) const
{
	PruneInvalidPendingNPCs();
	UProjectRunCompanionSubsystem* Roster =
		ProjectCalystoPopulationBridgeV4Private::ResolveRoster(World);
	if (!Roster)
	{
		OutError = TEXT("The project companion GameInstance subsystem is unavailable.");
		return false;
	}
	return Roster->FinalizeDirectorRosterReadiness(ExpectedCompanionSnapshotHash, OutError);
}
