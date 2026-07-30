#pragma once

#include "Components/ActorComponent.h"
#include "DungeonCurse/DungeonCurseTypes.h"
#include "DungeonCurseComponent.generated.h"

class ARoomCurseVolume;
class AActor;
class UProjectInnerDoctrineComponent;
class UProjectSurvivalNeedsComponent;

struct FActiveDungeonRoomCurse
{
	FRoomCurseDefinition Curse;
	TWeakObjectPtr<ARoomCurseVolume> SourceVolume;
	FTimerHandle PeriodicTimerHandle;
	FTimerHandle DurationTimerHandle;
	float TickInterval = 1.0f;
	FGuid ActivationId;
	uint32 NextPeriodicApplicationSequence = 0;
};

UCLASS(ClassGroup = (DungeonCurse), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UDungeonCurseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDungeonCurseComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Dungeon Curse")
	bool ApplyRoomCurse(ARoomCurseVolume* SourceVolume, const FRoomCurseDefinition& Curse, float InTickInterval);

	UFUNCTION(BlueprintCallable, Category = "Dungeon Curse")
	bool RemoveRoomCurse(FName SourceID);

	UFUNCTION(BlueprintCallable, Category = "Dungeon Curse")
	void ClearAllRoomCurses();

	UFUNCTION(BlueprintPure, Category = "Dungeon Curse")
	bool HasActiveCurse(FName SourceID) const;

	UFUNCTION(BlueprintPure, Category = "Dungeon Curse")
	TArray<FName> GetActiveCurseIDs() const;

	static FName MakeFallbackSourceID(const UObject* SourceObject, const FRoomCurseDefinition& Curse);
	static FProjectCurseApplicationContext BuildPeriodicCurseApplicationContext(
		const UObject* TargetObject,
		AActor* SourceActor,
		FName SourceID,
		const FGuid& ActivationId,
		uint32 SequenceIndex,
		float Amount,
		EProjectCurseSourceKind SourceKind);

private:
	UProjectSurvivalNeedsComponent* ResolveNeedsComponent();
	UProjectInnerDoctrineComponent* ResolveInnerDoctrineComponent();
	void ApplyEntryEffects(const FName SourceID, FActiveDungeonRoomCurse& Entry);
	void RemoveEntryEffects(const FName SourceID, FActiveDungeonRoomCurse& Entry);
	void ApplyPeriodicCurse(FName SourceID);
	void HandleDurationExpired(FName SourceID);
	bool ApplyCurseBuildup(FName SourceID, FActiveDungeonRoomCurse& Entry);
	bool ApplyInnerStateDelta(FName SourceID, FName StateName, float DeltaValue);
	bool ApplyNeedDrainMultiplier(FName SourceID, FName NeedName, float Multiplier, bool bEnabled);
	void NotifyCurseApplied(const FRoomCurseDefinition& Curse) const;
	void NotifyCurseRemoved(const FRoomCurseDefinition& Curse) const;
	FName ResolveCurseID(const FRoomCurseDefinition& Curse) const;

private:
	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalNeedsComponent> CachedNeedsComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectInnerDoctrineComponent> CachedInnerDoctrineComponent;

	TMap<FName, FActiveDungeonRoomCurse> ActiveCurses;
	TSet<FName> WarnedMissingInnerStateSources;
	TSet<FName> WarnedMissingDoctrineSources;
};
