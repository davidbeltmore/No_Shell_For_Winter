#include "Calysto/ProjectCalystoContentGameplayBridge.h"

#include "Actors/ACFCharacter.h"
#include "Calysto/ProjectCalystoActorAssignmentComponent.h"
#include "Calysto/ProjectCalystoGameplaySnapshot.h"
#include "Characters/ProjectEnemyLevelComponent.h"
#include "Characters/ProjectEnemyLevelSubsystem.h"
#include "Companions/ProjectCompanionRuntimeAdapter.h"
#include "Companions/ProjectRecruitableCompanionComponent.h"
#include "Components/ACFCompanionGroupAIComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Items/ACFItem.h"
#include "Kismet/GameplayStatics.h"
#include "Lockpicking/ProjectCalystoChest.h"
#include "Misc/SecureHash.h"
#include "Social/ProjectSocialSubsystem.h"

namespace ProjectCalystoContentBridgePrivate
{
	FString Guid(const FGuid& Id) { return Id.ToString(EGuidFormats::Digits); }
	bool IsWorldActorRole(const EEFCalystoGameplayRole Role)
	{
		return Role==EEFCalystoGameplayRole::Enemy || Role==EEFCalystoGameplayRole::SupportNPC
			|| Role==EEFCalystoGameplayRole::Armor || Role==EEFCalystoGameplayRole::LooseLoot
			|| Role==EEFCalystoGameplayRole::Food || Role==EEFCalystoGameplayRole::Drink
			|| Role==EEFCalystoGameplayRole::Container || Role==EEFCalystoGameplayRole::Prop
			|| Role==EEFCalystoGameplayRole::SpecialEvent;
	}
	bool SameIds(const TSet<FGuid>& A,const TSet<FGuid>& B)
	{
		if (A.Num()!=B.Num()) return false;
		for (const FGuid& Id:A) if (!B.Contains(Id)) return false;
		return true;
	}
}

FProjectCalystoContentGameplayBridge::FProjectCalystoContentGameplayBridge(TSharedRef<FProjectCalystoGameplaySnapshot> InSnapshot)
	: Snapshot(InSnapshot)
{
}

FString FProjectCalystoContentGameplayBridge::HashRows(TArray<FString> Rows)
{
	Rows.Sort(); return FMD5::HashAnsiString(*FString::Join(Rows,TEXT("\n")));
}

FGuid FProjectCalystoContentGameplayBridge::StableCompanionId(const FEFCalystoContentGameplayContext& Context,
	const FEFCalystoReservedContent& Reservation)
{
	return Context.Actors.FindRef(Reservation.Id).CompanionIdentity;
}

UProjectSocialSubsystem* FProjectCalystoContentGameplayBridge::ResolveSocial(const UWorld* World)
{
	UGameInstance* Instance=World?World->GetGameInstance():nullptr;
	return Instance?Instance->GetSubsystem<UProjectSocialSubsystem>():nullptr;
}

bool FProjectCalystoContentGameplayBridge::Bind(const FEFCalystoContentGameplayContext& Context,FString& Error) const
{
	Error.Reset();
	if (!Context.Token.Request.IsValid() || !Context.Token.Attempt.IsValid() || !Context.World.IsValid()
		|| !Context.AttemptOwner.IsValid() || !Context.Manifest || !Context.Manifest->IsValid()
		|| !Context.PreFloorSnapshot || Context.PreFloorSnapshot.Get()!=&Snapshot.Get()
		|| Context.PreFloorSnapshot->GetCanonicalHash().IsEmpty())
	{ Error=TEXT("The typed V7 bridge requires its exact owned world, manifest and immutable snapshot."); return false; }
	if (!OwnedToken.Request.IsValid())
	{
		OwnedToken=Context.Token; OwnedWorld=Context.World; OwnedManifestHash=Context.Manifest->GetHash();
	}
	return true;
}

bool FProjectCalystoContentGameplayBridge::Owns(const FEFCalystoContentGameplayContext& Context,FString& Error) const
{
	if (!Bind(Context,Error)) return false;
	if (!(OwnedToken==Context.Token) || OwnedWorld.Get()!=Context.World.Get()
		|| OwnedManifestHash!=Context.Manifest->GetHash() || bReleaseBegun)
	{ Error=TEXT("The typed V7 bridge does not own this exact attempt and frozen manifest."); return false; }
	return true;
}

bool FProjectCalystoContentGameplayBridge::BuildCompanionDefinition(const FEFCalystoContentGameplayContext& Context,
	const FEFCalystoReservedContent& Reservation,FProjectCompanionDefinition& OutDefinition,FString& Error) const
{
	OutDefinition={}; Error.Reset();
	const FEFCalystoFrozenActorGameplay* Frozen=Context.Actors.Find(Reservation.Id);
	if (Reservation.Role!=EEFCalystoGameplayRole::SupportNPC || Reservation.InventorySlot!=INDEX_NONE
		|| !Frozen || Frozen->ReservationId!=Reservation.Id || !Frozen->CompanionIdentity.IsValid()
		|| Frozen->LogicalLevel<1 || Frozen->PhysicalLevel!=UProjectEnemyLevelComponent::ResolvePhysicalAscentLevel(Frozen->LogicalLevel)
		|| Reservation.Entry.ActorClass.IsNull() || Reservation.Entry.Archetype.IsNone())
	{ Error=TEXT("Support NPCs require typed class, archetype, level and persistent identity bindings."); return false; }
	OutDefinition.StableCompanionId=Frozen->CompanionIdentity;
	OutDefinition.SourceSpawnId=FName(*ProjectCalystoContentBridgePrivate::Guid(Reservation.Id));
	OutDefinition.ContentId=FName(*ProjectCalystoContentBridgePrivate::Guid(Reservation.Entry.Selection.Id));
	OutDefinition.CatalogVariantId=OutDefinition.ContentId;
	OutDefinition.CharacterClass=TSoftClassPtr<AACFCharacter>(Reservation.Entry.ActorClass.ToSoftObjectPath());
	OutDefinition.Archetype=Reservation.Entry.Archetype; OutDefinition.ResolvedLevel=Frozen->LogicalLevel;
	switch (Reservation.Entry.Gender)
	{
	case EEFCalystoGender::Any: OutDefinition.Gender=TEXT("Any"); break;
	case EEFCalystoGender::Female: OutDefinition.Gender=TEXT("Female"); break;
	case EEFCalystoGender::Male: OutDefinition.Gender=TEXT("Male"); break;
	default: Error=TEXT("The selected NPC gender is unsupported."); return false;
	}
	switch (Reservation.Entry.Rarity)
	{
	case EEFCalystoRarity::Common: OutDefinition.DifficultyGrade=EProjectCompanionDifficultyGrade::Common; break;
	case EEFCalystoRarity::Uncommon: OutDefinition.DifficultyGrade=EProjectCompanionDifficultyGrade::Uncommon; break;
	case EEFCalystoRarity::Rare: OutDefinition.DifficultyGrade=EProjectCompanionDifficultyGrade::Rare; break;
	case EEFCalystoRarity::Epic: OutDefinition.DifficultyGrade=EProjectCompanionDifficultyGrade::Epic; break;
	case EEFCalystoRarity::Winter: OutDefinition.DifficultyGrade=EProjectCompanionDifficultyGrade::Winter; break;
	default: Error=TEXT("The selected NPC rarity is unsupported."); return false;
	}
	switch (Reservation.Entry.Lifecycle)
	{
	case EEFCalystoLifecycle::FloorLocal: OutDefinition.Lifecycle=EProjectCompanionLifecycle::FloorLocal; break;
	case EEFCalystoLifecycle::Recruitable: OutDefinition.Lifecycle=EProjectCompanionLifecycle::Recruitable; break;
	default: Error=TEXT("The selected NPC lifecycle is unsupported."); return false;
	}
	return OutDefinition.IsValid(Error);
}

bool FProjectCalystoContentGameplayBridge::Preflight(const FEFCalystoContentGameplayContext& Context,
	TArray<FSoftObjectPath>& AdditionalDependencies,FString& Error) const
{
	AdditionalDependencies.Reset();
	if (!Owns(Context,Error) || !Snapshot->VerifyUnchanged(Context.Token.Request,Error)) return false;
	if (!Context.ExistingContainers.IsEmpty())
	{ Error=TEXT("V7 does not support untracked insertion into an existing container."); return false; }
	for (const FEFCalystoReservedContent& Reservation : Context.Manifest->GetElements())
	{
		if (!Reservation.Id.IsValid() || !Reservation.Entry.Selection.Id.IsValid())
		{ Error=TEXT("A selected content reservation lacks its stable identity."); return false; }
		const FSoftObjectPath Payload=Reservation.InventorySlot==INDEX_NONE
			? Reservation.Entry.ActorClass.ToSoftObjectPath() : Reservation.Entry.InventoryClass.ToSoftObjectPath();
		if (!Payload.IsValid()) { Error=TEXT("A selected V7 content payload is empty."); return false; }
		AdditionalDependencies.AddUnique(Payload);
		if (Reservation.InventorySlot!=INDEX_NONE)
		{
			if (Reservation.Role!=EEFCalystoGameplayRole::ContainerContent || !Reservation.ParentContainerId.IsValid())
			{ Error=TEXT("An inventory reservation must be typed Container Content with an exact parent."); return false; }
			continue;
		}
		if (!ProjectCalystoContentBridgePrivate::IsWorldActorRole(Reservation.Role))
		{ Error=TEXT("The selected V7 gameplay role has no world-spawn contract."); return false; }
		if (Reservation.Role==EEFCalystoGameplayRole::Container)
			AdditionalDependencies.AddUnique(AProjectCalystoChest::GetDefaultVisualMeshPath());
	}
	return true;
}

bool FProjectCalystoContentGameplayBridge::PrepareDeferredActor(const FEFCalystoContentGameplayContext& Context,
	const FEFCalystoReservedContent& Reservation,AActor* Actor,TConstArrayView<FEFCalystoReservedContent> ContainerContents,FString& Error)
{
	if (!Owns(Context,Error) || Reservation.InventorySlot!=INDEX_NONE || !IsValid(Actor)
		|| Actor->GetWorld()!=Context.World.Get() || !Actor->HasAuthority() || Actor->HasActorBegunPlay()
		|| Actor->GetClass()!=Reservation.Entry.ActorClass.ToSoftObjectPath().ResolveObject())
	{ if (Error.IsEmpty()) Error=TEXT("Deferred V7 actor class, world, authority or BeginPlay state changed."); return false; }
	if (FActorStage* Existing=Stages.Find(Reservation.Id))
		return Existing->Actor.Get()==Actor && Existing->Reservation.Id==Reservation.Id;
	TArray<UProjectCalystoActorAssignmentComponent*> ExistingAssignments;
	Actor->GetComponents(ExistingAssignments);
	if (!ExistingAssignments.IsEmpty()) { Error=TEXT("A selected V7 actor already has a typed assignment."); return false; }
	auto* Assignment=NewObject<UProjectCalystoActorAssignmentComponent>(Actor,NAME_None,RF_Transient);
	if (!Assignment) { Error=TEXT("The typed V7 actor assignment could not be allocated."); return false; }
	Actor->AddInstanceComponent(Assignment); Assignment->RegisterComponent();
	if (!Assignment->BindDeferred(Context,Reservation,Error)) return false;

	FActorStage& Stage=Stages.FindOrAdd(Reservation.Id); Stage.Actor=Actor; Stage.Reservation=Reservation;
	Stage.bEnemy=Reservation.Role==EEFCalystoGameplayRole::Enemy;
	Stage.bSupportNpc=Reservation.Role==EEFCalystoGameplayRole::SupportNPC;
	Stage.bContainer=Reservation.Role==EEFCalystoGameplayRole::Container;
	if (Stage.bEnemy || Stage.bSupportNpc)
	{
		APawn* Pawn=Cast<APawn>(Actor);
		if (!Pawn) { Error=TEXT("A typed combat or support role did not resolve to a Pawn."); return false; }
		Pawn->AutoPossessAI=EAutoPossessAI::Disabled;
	}
	if (Stage.bEnemy)
	{
		const FEFCalystoFrozenActorGameplay* Frozen=Context.Actors.Find(Reservation.Id);
		auto* Levels=Context.World->GetSubsystem<UProjectEnemyLevelSubsystem>();
		if (!Frozen || !Levels || !Levels->PrepareDeferredDirectorEnemy(CastChecked<APawn>(Actor),Frozen->LogicalLevel,Frozen->PhysicalLevel,Error)) return false;
	}
	else if (Stage.bSupportNpc)
	{
		if (!BuildCompanionDefinition(Context,Reservation,Stage.Companion,Error)
			|| !UProjectCompanionRuntimeAdapter::PrepareDeferredCompanion(Cast<AACFCharacter>(Actor),Stage.Companion,Error)) return false;
	}
	else if (Stage.bContainer)
	{
		AProjectCalystoChest* Chest=Cast<AProjectCalystoChest>(Actor);
		if (!Chest) { Error=TEXT("A selected container does not derive from the project Calysto chest contract."); return false; }
		TArray<FProjectCalystoResolvedChestEntry> Entries; TSet<FGuid> Ids;
		for (const FEFCalystoReservedContent& Item : ContainerContents)
		{
			UClass* ItemClass=Cast<UClass>(Item.Entry.InventoryClass.ToSoftObjectPath().ResolveObject());
			if (Item.Role!=EEFCalystoGameplayRole::ContainerContent || Item.ParentContainerId!=Reservation.Id
				|| Item.InventorySlot<0 || !ItemClass || !ItemClass->IsChildOf(UACFItem::StaticClass()) || Ids.Contains(Item.Id))
			{ Error=TEXT("A selected chest content entry is not an exact typed inventory item."); return false; }
			Ids.Add(Item.Id); Stage.ContentReservations.Add(Item.Id);
			FProjectCalystoResolvedChestEntry& Entry=Entries.AddDefaulted_GetRef();
			Entry.StableAttemptId=FName(*ProjectCalystoContentBridgePrivate::Guid(Item.Id));
			Entry.ContentCatalogId=FName(*ProjectCalystoContentBridgePrivate::Guid(Item.Entry.Selection.Id));
			Entry.ItemClass=ItemClass; Entry.Quantity=1;
		}
		if (!Chest->ConfigureResolvedLoot(Entries,Error)) return false;
	}
	else if (!ContainerContents.IsEmpty())
	{ Error=TEXT("Only a typed project chest can own selected inventory content."); return false; }
	return true;
}

bool FProjectCalystoContentGameplayBridge::StageExistingContainer(const FEFCalystoContentGameplayContext& Context,FGuid ContainerId,
	AActor* Container,TConstArrayView<FEFCalystoReservedContent> Contents,FString& Error)
{
	(void)Context; (void)ContainerId; (void)Container; (void)Contents;
	Error=TEXT("V7 does not support existing-container mutation without an explicit reversible native contract."); return false;
}

EEFCalystoGameplayObservation FProjectCalystoContentGameplayBridge::FinalizeSpawnedActor(const FEFCalystoContentGameplayContext& Context,
	const FEFCalystoReservedContent& Reservation,AActor* Actor,FString& Error)
{
	if (!VerifyStage(Context,Reservation,Actor,Error)) return EEFCalystoGameplayObservation::Failed;
	FActorStage& Stage=Stages.FindChecked(Reservation.Id);
	if (Stage.bFinalized) return EEFCalystoGameplayObservation::Verified;
	if (Stage.bEnemy)
	{
		const FEFCalystoFrozenActorGameplay& Frozen=Context.Actors.FindChecked(Reservation.Id);
		auto* Levels=Context.World->GetSubsystem<UProjectEnemyLevelSubsystem>();
		if (!Levels || !Levels->InitializeDirectorEnemySynchronously(CastChecked<APawn>(Actor),Frozen.LogicalLevel,Error)) return EEFCalystoGameplayObservation::Failed;
		TSharedPtr<FProjectCalystoDormantController> DormantController=MakeShared<FProjectCalystoDormantController>();
		if (!DormantController->Begin(Context.Token,CastChecked<APawn>(Actor),Context.AttemptOwner.Get(),Error)) return EEFCalystoGameplayObservation::Failed;
		Stage.DormantController=MoveTemp(DormantController);
	}
	else if (Stage.bSupportNpc)
	{
		TSharedPtr<FProjectCalystoDormantController> DormantController=MakeShared<FProjectCalystoDormantController>();
		if (!DormantController->Begin(Context.Token,CastChecked<APawn>(Actor),Context.AttemptOwner.Get(),Error)) return EEFCalystoGameplayObservation::Failed;
		Stage.DormantController=MoveTemp(DormantController);
		const FProjectCompanionSpawnResult Result=UProjectCompanionRuntimeAdapter::FinalizeDeferredCompanionForDirector(
			CastChecked<AACFCharacter>(Actor),Stage.Companion,Context.Token,*Stage.DormantController);
		if (!Result.bSucceeded) { Error=Result.Diagnostic; return EEFCalystoGameplayObservation::Failed; }
	}
	else if (Stage.bContainer)
	{
		TArray<FName> Verified; AProjectCalystoChest* Chest=CastChecked<AProjectCalystoChest>(Actor);
		if (!Chest->FinalizeAndVerifyResolvedLoot(Verified,Error) || Verified.Num()!=Stage.ContentReservations.Num()) return EEFCalystoGameplayObservation::Failed;
	}
	Stage.bFinalized=true; return EEFCalystoGameplayObservation::Verified;
}

bool FProjectCalystoContentGameplayBridge::VerifyStage(const FEFCalystoContentGameplayContext& Context,
	const FEFCalystoReservedContent& Reservation,AActor* Actor,FString& Error) const
{
	if (!Owns(Context,Error) || !IsValid(Actor) || Actor->GetWorld()!=Context.World.Get()
		|| Actor->GetClass()!=Reservation.Entry.ActorClass.ToSoftObjectPath().ResolveObject())
	{ if (Error.IsEmpty()) Error=TEXT("The selected V7 actor is not the exact live frozen payload."); return false; }
	const FActorStage* Stage=Stages.Find(Reservation.Id);
	if (!Stage || Stage->Actor.Get()!=Actor || Stage->Reservation.Id!=Reservation.Id) { Error=TEXT("The selected actor has no exact staged V7 record."); return false; }
	TArray<UProjectCalystoActorAssignmentComponent*> Assignments; Actor->GetComponents(Assignments);
	return Assignments.Num()==1 && Assignments[0]->Matches(Context,Reservation,Error);
}

FEFCalystoGameplayElementEvidence FProjectCalystoContentGameplayBridge::Evidence(const FEFCalystoContentGameplayContext& Context,
	const FEFCalystoReservedContent& Reservation,EEFCalystoGameplayObservation State,const FString& NativeStateHash,const FString& Message) const
{
	FEFCalystoGameplayElementEvidence Result; Result.Token=Context.Token; Result.ReservationId=Reservation.Id;
	Result.EntryId=Reservation.Entry.Selection.Id; Result.ParentContainerId=Reservation.ParentContainerId;
	Result.InventorySlot=Reservation.InventorySlot; Result.Payload=Reservation.InventorySlot==INDEX_NONE
		? Reservation.Entry.ActorClass.ToSoftObjectPath() : Reservation.Entry.InventoryClass.ToSoftObjectPath();
	Result.SnapshotHash=Context.PreFloorSnapshot?Context.PreFloorSnapshot->GetCanonicalHash():FString();
	Result.NativeStateHash=NativeStateHash; Result.State=State; Result.Message=Message;
	Result.Failure=EEFCalystoAttemptFailure::Configuration; return Result;
}

FEFCalystoGameplayElementEvidence FProjectCalystoContentGameplayBridge::ObserveActor(const FEFCalystoContentGameplayContext& Context,
	const FEFCalystoReservedContent& Reservation,AActor* Actor) const
{
	FString Error;
	if (!VerifyStage(Context,Reservation,Actor,Error)) return Evidence(Context,Reservation,EEFCalystoGameplayObservation::Failed,TEXT("invalid"),Error);
	const FActorStage& Stage=Stages.FindChecked(Reservation.Id);
	if (!Stage.bFinalized) return Evidence(Context,Reservation,EEFCalystoGameplayObservation::Pending,TEXT("pending"));
	if (Stage.bEnemy)
	{
		const FEFCalystoFrozenActorGameplay& Frozen=Context.Actors.FindChecked(Reservation.Id);
		auto* Levels=Context.World->GetSubsystem<UProjectEnemyLevelSubsystem>();
		if (!Levels || !Stage.DormantController || !Levels->ValidateDirectorEnemyInitialization(CastChecked<APawn>(Actor),Frozen.LogicalLevel,Error)
			|| !Stage.DormantController->Verify(Context.Token,CastChecked<APawn>(Actor),Error))
			return Evidence(Context,Reservation,EEFCalystoGameplayObservation::Failed,TEXT("invalid"),Error);
	}
	if (Stage.bSupportNpc && (!Stage.DormantController
		|| !UProjectCompanionRuntimeAdapter::ValidateDeferredCompanionForDirector(CastChecked<AACFCharacter>(Actor),Stage.Companion,Context.Token,*Stage.DormantController,Error)))
		return Evidence(Context,Reservation,EEFCalystoGameplayObservation::Failed,TEXT("invalid"),Error);
	return Evidence(Context,Reservation,EEFCalystoGameplayObservation::Verified,HashRows({
		ProjectCalystoContentBridgePrivate::Guid(Reservation.Id),Actor->GetClass()->GetPathName(),FString::FromInt(int32(Reservation.Role))}));
}

FEFCalystoGameplayElementEvidence FProjectCalystoContentGameplayBridge::ObserveInventoryItem(const FEFCalystoContentGameplayContext& Context,
	const FEFCalystoReservedContent& Reservation,AActor* Container) const
{
	FString Error;
	if (!Owns(Context,Error) || Reservation.InventorySlot<0 || Reservation.Role!=EEFCalystoGameplayRole::ContainerContent
		|| !Reservation.ParentContainerId.IsValid() || !IsValid(Container))
		return Evidence(Context,Reservation,EEFCalystoGameplayObservation::Failed,TEXT("invalid"),Error.IsEmpty()?TEXT("The selected inventory binding is invalid."):Error);
	const FActorStage* Stage=Stages.Find(Reservation.ParentContainerId);
	UClass* Expected=Cast<UClass>(Reservation.Entry.InventoryClass.ToSoftObjectPath().ResolveObject());
	AProjectCalystoChest* Chest=Cast<AProjectCalystoChest>(Container);
	if (!Stage || !Stage->bContainer || Stage->Actor.Get()!=Container || !Stage->ContentReservations.Contains(Reservation.Id)
		|| !Expected || !Expected->IsChildOf(UACFItem::StaticClass()) || !Chest)
		return Evidence(Context,Reservation,EEFCalystoGameplayObservation::Failed,TEXT("invalid"),TEXT("The selected chest does not retain its exact inventory reservation."));
	int32 Count=0; for (const FInventoryItem& Item:Chest->GetItems()) if (Item.ItemClass==Expected && Item.Count>0) Count+=Item.Count;
	if (Count<1) return Evidence(Context,Reservation,EEFCalystoGameplayObservation::Failed,TEXT("invalid"),TEXT("The selected chest item class is absent after native finalization."));
	return Evidence(Context,Reservation,EEFCalystoGameplayObservation::Verified,HashRows({
		ProjectCalystoContentBridgePrivate::Guid(Reservation.Id),Expected->GetPathName(),FString::FromInt(Count)}));
}

EEFCalystoGameplayObservation FProjectCalystoContentGameplayBridge::PrepareCommit(const FEFCalystoContentGameplayContext& Context,
	FEFCalystoGameplayCommitReceipt& Receipt,FString& Error)
{
	Receipt={}; if (!Owns(Context,Error)) return EEFCalystoGameplayObservation::Failed;
	bool bHasSupportNpc=false;
	for (const FEFCalystoReservedContent& Reservation:Context.Manifest->GetElements())
	{
		const FGuid OwnerId=Reservation.InventorySlot==INDEX_NONE ? Reservation.Id : Reservation.ParentContainerId;
		const FActorStage* Stage=Stages.Find(OwnerId);
		if (!Stage)
		{
			Error=TEXT("A selected V7 reservation has no exact staged owner during commit preparation.");
			return EEFCalystoGameplayObservation::Failed;
		}
		const FEFCalystoGameplayElementEvidence Element=Reservation.InventorySlot==INDEX_NONE
			? ObserveActor(Context,Reservation,Stage->Actor.Get())
			: ObserveInventoryItem(Context,Reservation,Stage->Actor.Get());
		if (Element.State==EEFCalystoGameplayObservation::Pending) return Element.State;
		if (Element.State!=EEFCalystoGameplayObservation::Verified) { Error=Element.Message; return EEFCalystoGameplayObservation::Failed; }
		bHasSupportNpc|=Reservation.Role==EEFCalystoGameplayRole::SupportNPC;
	}
	if (bHasSupportNpc)
	{
		auto* Social=ResolveSocial(Context.World.Get());
		if (!Social || !Social->PrepareStagedParticipants(Context.Token,Error)) return EEFCalystoGameplayObservation::Failed;
	}
	Receipt.Token=Context.Token; Receipt.ManifestHash=Context.Manifest->GetHash();
	Receipt.PreFloorSnapshotHash=Context.PreFloorSnapshot->GetCanonicalHash();
	for (const FEFCalystoReservedContent& Reservation:Context.Manifest->GetElements()) Receipt.PreparedElements.Add(Reservation.Id);
	TArray<FString> Rows; Rows.Add(Receipt.ManifestHash); Rows.Add(Receipt.PreFloorSnapshotHash);
	for (const FGuid& Id:Receipt.PreparedElements) Rows.Add(ProjectCalystoContentBridgePrivate::Guid(Id));
	Receipt.PreparedStateHash=HashRows(MoveTemp(Rows)); bPrepared=true; return EEFCalystoGameplayObservation::Verified;
}

bool FProjectCalystoContentGameplayBridge::CommitPrepared(const FEFCalystoContentGameplayContext& Context,
	const FEFCalystoGameplayCommitReceipt& Receipt,FString& Error)
{
	if (!Owns(Context,Error) || !bPrepared || !Receipt.Matches(Context,Error)) return false;
	bool bHasSupportNpc=false; for (const auto& Pair:Stages) bHasSupportNpc|=Pair.Value.bSupportNpc;
	if (bHasSupportNpc)
	{
		auto* Social=ResolveSocial(Context.World.Get());
		if (!Social || !Social->PublishStagedParticipantsWithoutEvents(Context.Token,Error)) return false;
	}
	bPublished=true; return true;
}

EEFCalystoActivationResult FProjectCalystoContentGameplayBridge::ConfirmAccepted(const FEFCalystoContentGameplayContext& Context,
	const FEFCalystoGameplayCommitReceipt& Receipt,FString& Error)
{
	if (!Owns(Context,Error) || !bPublished || !Receipt.Matches(Context,Error)) return EEFCalystoActivationResult::InvariantFailure;
	bool bHasSupportNpc=false; for (const auto& Pair:Stages) bHasSupportNpc|=Pair.Value.bSupportNpc;
	if (bHasSupportNpc)
	{
		auto* Social=ResolveSocial(Context.World.Get());
		if (!Social || !Social->ConfirmStagedParticipants(Context.Token,Error)) return EEFCalystoActivationResult::InvariantFailure;
	}
	for (auto& Pair:Stages)
	{
		FActorStage& Stage=Pair.Value; AActor* Actor=Stage.Actor.Get();
		if ((Stage.bEnemy || Stage.bSupportNpc) && (!Stage.DormantController
			|| !Stage.DormantController->ActivateAccepted(Context.Token,Cast<APawn>(Actor),Error))) return EEFCalystoActivationResult::InvariantFailure;
		if (Stage.bSupportNpc && Stage.Companion.Lifecycle==EProjectCompanionLifecycle::Recruitable)
		{
			APawn* Player=UGameplayStatics::GetPlayerPawn(Context.World.Get(),0);
			auto* Group=UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(Player);
			auto* Hook=NewObject<UProjectRecruitableCompanionComponent>(Actor,NAME_None,RF_Transient);
			if (!Hook || !Group) { Error=TEXT("Accepted recruitable NPC lacks the native player companion group."); return EEFCalystoActivationResult::InvariantFailure; }
			Actor->AddInstanceComponent(Hook); Hook->RegisterComponent();
			if (!Hook->InitializeRecruitmentHook(Stage.Companion,Group,Error)) return EEFCalystoActivationResult::InvariantFailure;
		}
	}
	bAccepted=true; return EEFCalystoActivationResult::Activated;
}

void FProjectCalystoContentGameplayBridge::BeginRelease(const FEFCalystoContentGameplayContext& Context,
	const EEFCalystoContentReleaseIntent Intent)
{
	FString Error; if (!Bind(Context,Error) || bReleaseBegun) return;
	bReleaseBegun=true; bSocialReleaseSucceeded=true;
	bool bHasSupportNpc=false; for (const auto& Pair:Stages) bHasSupportNpc|=Pair.Value.bSupportNpc;
	if (bHasSupportNpc)
	{
		if (auto* Social=ResolveSocial(Context.World.Get())) bSocialReleaseSucceeded=Social->ReleaseStagedParticipants(Context.Token,
			Intent==EEFCalystoContentReleaseIntent::AcceptedFloorExit,Error);
		else bSocialReleaseSucceeded=false;
	}
	for (auto& Pair:Stages)
	{
		FActorStage& Stage=Pair.Value; APawn* Pawn=Cast<APawn>(Stage.Actor.Get());
		if (Intent==EEFCalystoContentReleaseIntent::RejectedAttempt)
		{
			if (Stage.bEnemy && Pawn) if (auto* Levels=Context.World->GetSubsystem<UProjectEnemyLevelSubsystem>()) Levels->RollbackDirectorEnemy(Pawn);
			if (Stage.DormantController) Stage.DormantController->BeginRelease(Context.Token);
		}
		else if (Stage.DormantController && Pawn && !Stage.bControllerTransferred)
		{
			Stage.bControllerTransferred=Stage.DormantController->TransferAccepted(Context.Token,Pawn,Error)!=nullptr;
		}
	}
}

FEFCalystoGameplayReleaseEvidence FProjectCalystoContentGameplayBridge::ObserveRelease(const FEFCalystoContentGameplayContext& Context,
	const EEFCalystoContentReleaseIntent Intent) const
{
	FEFCalystoGameplayReleaseEvidence Result; Result.Token=Context.Token;
	FString Error; if (!Bind(Context,Error) || !bReleaseBegun) { Result.PendingCallbacks=1; return Result; }
	bool bControllersReleased=true;
	for (const auto& Pair:Stages)
	{
		const FActorStage& Stage=Pair.Value; APawn* Pawn=Cast<APawn>(Stage.Actor.Get());
		if (Intent==EEFCalystoContentReleaseIntent::RejectedAttempt && Stage.DormantController)
			bControllersReleased&=Stage.DormantController->ObserveRelease(Context.Token);
		if (Intent==EEFCalystoContentReleaseIntent::AcceptedFloorExit)
		{
			if ((Stage.bEnemy || Stage.bSupportNpc) && !Stage.bControllerTransferred) bControllersReleased=false;
			if (Stage.Actor.IsValid()) Result.TransferredActors.Add(Pair.Key,Stage.Actor);
		}
	}
	Result.PendingCallbacks=bControllersReleased?0:1;
	Result.OwnedObjects=0; Result.SnapshotLeases=0; Result.bSafeToDestroyActors=bControllersReleased;
	if (Intent==EEFCalystoContentReleaseIntent::RejectedAttempt)
		Result.bPersistentStateVerified=bSocialReleaseSucceeded && Snapshot->VerifyUnchanged(Context.Token.Request,Error);
	else Result.bPersistentStateVerified=bSocialReleaseSucceeded && bAccepted;
	return Result;
}
