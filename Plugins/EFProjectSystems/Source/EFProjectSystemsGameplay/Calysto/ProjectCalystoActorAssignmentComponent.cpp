#include "Calysto/ProjectCalystoActorAssignmentComponent.h"
#include "Characters/ProjectEnemyLevelComponent.h"
#include "GameFramework/Actor.h"

UProjectCalystoActorAssignmentComponent::UProjectCalystoActorAssignmentComponent()
{ PrimaryComponentTick.bCanEverTick=false; bAutoActivate=false; }

bool UProjectCalystoActorAssignmentComponent::HasValidFrozenAssignment() const
{
	return bBound && IsValid(GetOwner()) && !IsTemplate() && RequestId.IsValid() && AttemptId.IsValid()
		&& ReservationId.IsValid() && EntryId.IsValid() && FloorNumber>0 && !SnapshotHash.IsEmpty()
		&& LogicalLevel>0 && PhysicalLevel==UProjectEnemyLevelComponent::ResolvePhysicalAscentLevel(LogicalLevel)
		&& ActorClass.ResolveObject()==GetOwner()->GetClass();
}

bool UProjectCalystoActorAssignmentComponent::Matches(const FEFCalystoContentGameplayContext& C,
	const FEFCalystoReservedContent& R,FString& Error) const
{
	Error.Reset(); const auto* G=C.Actors.Find(R.Id);
	if (!HasValidFrozenAssignment() || !G || !C.PreFloorSnapshot || GetOwner()->GetWorld()!=C.World.Get()
		|| RequestId!=C.Token.Request || AttemptId!=C.Token.Attempt || ReservationId!=R.Id || EntryId!=R.Entry.Selection.Id
		|| CompanionIdentity!=G->CompanionIdentity || LogicalLevel!=G->LogicalLevel || PhysicalLevel!=G->PhysicalLevel
		|| Role!=R.Role || FloorNumber!=C.FloorNumber || SnapshotHash!=C.PreFloorSnapshot->GetCanonicalHash()
		|| ActorClass!=R.Entry.ActorClass.ToSoftObjectPath())
	{ Error=TEXT("The actual actor assignment differs from its exact frozen token, payload, level or snapshot."); return false; }
	return true;
}

bool UProjectCalystoActorAssignmentComponent::BindDeferred(const FEFCalystoContentGameplayContext& C,
	const FEFCalystoReservedContent& R,FString& Error)
{
	Error.Reset(); if (bBound) return Matches(C,R,Error);
	const auto* G=C.Actors.Find(R.Id);
	if (!G || !C.PreFloorSnapshot || !IsValid(GetOwner()) || GetOwner()->HasActorBegunPlay() || IsTemplate()
		|| R.InventorySlot!=INDEX_NONE || !C.Token.Request.IsValid() || !C.Token.Attempt.IsValid())
	{ Error=TEXT("A frozen assignment requires an exact deferred instance and real pre-floor snapshot."); return false; }
	RequestId=C.Token.Request; AttemptId=C.Token.Attempt; ReservationId=R.Id; EntryId=R.Entry.Selection.Id;
	CompanionIdentity=G->CompanionIdentity; LogicalLevel=G->LogicalLevel; PhysicalLevel=G->PhysicalLevel;
	FloorNumber=C.FloorNumber; Role=R.Role; SnapshotHash=C.PreFloorSnapshot->GetCanonicalHash(); ActorClass=R.Entry.ActorClass.ToSoftObjectPath(); bBound=true;
	return Matches(C,R,Error);
}

