#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Calysto/EFCalystoContentGameplayBridge.h"
#include "ProjectCalystoActorAssignmentComponent.generated.h"

/** Exact immutable instance identity. Never authored on a Blueprint CDO and never encoded in actor tags. */
UCLASS(Transient,NotBlueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCalystoActorAssignmentComponent final : public UActorComponent
{
	GENERATED_BODY()
public:
	UProjectCalystoActorAssignmentComponent();
	bool BindDeferred(const FEFCalystoContentGameplayContext& Context,const FEFCalystoReservedContent& Reservation,FString& Error);
	bool Matches(const FEFCalystoContentGameplayContext& Context,const FEFCalystoReservedContent& Reservation,FString& Error) const;
	bool HasValidFrozenAssignment() const;
	int32 GetLogicalLevel() const { return LogicalLevel; }
	int32 GetPhysicalLevel() const { return PhysicalLevel; }
	FGuid GetReservationId() const { return ReservationId; }
private:
	UPROPERTY(Transient) FGuid RequestId;
	UPROPERTY(Transient) FGuid AttemptId;
	UPROPERTY(Transient) FGuid ReservationId;
	UPROPERTY(Transient) FGuid EntryId;
	UPROPERTY(Transient) FGuid CompanionIdentity;
	UPROPERTY(Transient) int32 LogicalLevel=0;
	UPROPERTY(Transient) int32 PhysicalLevel=0;
	UPROPERTY(Transient) int64 FloorNumber=0;
	UPROPERTY(Transient) EEFCalystoGameplayRole Role=EEFCalystoGameplayRole::Prop;
	UPROPERTY(Transient) FString SnapshotHash;
	UPROPERTY(Transient) FSoftObjectPath ActorClass;
	UPROPERTY(Transient) bool bBound=false;
};

