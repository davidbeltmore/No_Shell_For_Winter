#pragma once

#include "CoreMinimal.h"
#include "DungeonCurse/DungeonCurseTypes.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"
#include "DungeonCurseManager.generated.h"

class ARoomCurseVolume;
class ARoomGameplayMarker;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API ADungeonCurseManager : public AActor
{
	GENERATED_BODY()

public:
	ADungeonCurseManager();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Dungeon Curse")
	void InitializeCurseSystem();

	UFUNCTION(BlueprintCallable, Category = "Dungeon Curse")
	void ClearCurseSystem();

	UFUNCTION(BlueprintCallable, Category = "Dungeon Curse")
	void RegenerateCurses();

	UFUNCTION(BlueprintCallable, Category = "Dungeon Curse|Debug")
	void PrintCurseSummary() const;

	UFUNCTION(BlueprintCallable, Category = "Dungeon Curse|Debug")
	void DrawDebugCursedRooms() const;

	UFUNCTION(BlueprintCallable, Category = "Dungeon Curse")
	TArray<ARoomCurseVolume*> GetActiveCursedRooms() const;

	static bool SelectWeightedCurseDefinition(
		const TArray<FRoomCurseDefinition>& Definitions,
		const FDetectedDungeonCurseRoom& Room,
		bool bInEnableMaxLevelEnemyCurse,
		bool bInEnableSealedCombatRooms,
		FRandomStream& RandomStream,
		TSet<FName>& UsedNonRepeatableCurseIDs,
		FRoomCurseDefinition& OutCurse);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Definitions")
	TArray<FRoomCurseDefinition> CurseDefinitions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Probabilities", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BaseCurseChance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Probabilities", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CombatRoomCurseChance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Probabilities", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TreasureRoomCurseChance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Probabilities", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RestRoomCurseChance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Probabilities", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BossRoomCurseChance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Probabilities", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CorridorCurseChance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Probabilities", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxCursedRoomsPerFloor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Probabilities", meta = (ClampMin = "0", UIMin = "0"))
	int32 MinCursedRoomsPerFloor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Probabilities")
	bool bAllowEntranceRoomCurse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Probabilities")
	bool bAllowExitRoomCurse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Random")
	int32 RandomSeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Random")
	bool bUseDeterministicSeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Debug")
	bool DebugMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Debug")
	bool bDebugDraw;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Detection")
	TArray<FName> MarkerTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Detection")
	FName GeneratedActorTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Detection")
	FVector MinimumRoomExtent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Detection")
	TSubclassOf<AActor> PlayerClassFilter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Initialization")
	bool bAutoInitialize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Initialization", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float InitializationDelay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Initialization", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float ReadinessPollInterval;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Initialization", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxReadinessPollAttempts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Spawning")
	TSubclassOf<ARoomCurseVolume> RoomCurseVolumeClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Enemies")
	bool bEnableMaxLevelEnemyCurse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Enemies", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxEnemyLevelForCurrentFloor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Enemies", meta = (ClampMin = "0", UIMin = "0"))
	int32 EnemyOverrideRetryCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Enemies", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float EnemyOverrideRetryDelay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Phase 2")
	bool bEnableSealedCombatRooms;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Phase 2")
	TSubclassOf<AActor> SmokeBlockerActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Phase 2")
	FName ExitMarkerTag;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Dungeon Curse|Runtime")
	TArray<FDetectedDungeonCurseRoom> ValidRooms;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Dungeon Curse|Runtime")
	TArray<FDetectedDungeonCurseRoom> CursedRooms;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Dungeon Curse|Runtime")
	TArray<TObjectPtr<ARoomCurseVolume>> SpawnedCurseVolumes;

private:
	void ScheduleAutoInitialize();
	void TryInitializeWhenReady();
	bool IsProceduralRuntimeReady() const;
	int32 ResolveRandomSeed() const;
	void GenerateCurseAssignments(FRandomStream& RandomStream);
	void SpawnCurseVolumeForRoom(const FDetectedDungeonCurseRoom& Room, const FRoomCurseDefinition& Curse);
	float GetCurseChanceForRoom(const FDetectedDungeonCurseRoom& Room) const;
	bool IsRoomAllowedByType(const FDetectedDungeonCurseRoom& Room) const;
	TArray<FDetectedDungeonCurseRoom> DiscoverRooms(int32& OutMarkerCount, int32& OutTaggedActorCount, int32& OutPCGActorCount) const;
	void ScanMarkerRooms(TArray<FDetectedDungeonCurseRoom>& OutRooms) const;
	void ScanTaggedActorRooms(TArray<FDetectedDungeonCurseRoom>& OutRooms) const;
	void ScanPCGActorRooms(TArray<FDetectedDungeonCurseRoom>& OutRooms) const;
	bool BuildRoomFromMarker(ARoomGameplayMarker* Marker, FDetectedDungeonCurseRoom& OutRoom) const;
	bool BuildRoomFromActor(AActor* Actor, int32 Priority, FDetectedDungeonCurseRoom& OutRoom) const;
	bool IsCandidateActor(AActor* Actor) const;
	bool HasAnyMarkerTag(const AActor* Actor) const;
	EGeneratedRoomType InferRoomTypeFromActor(const AActor* Actor) const;
	bool HasValidCandidateExtent(const FVector& Extent) const;
	void SortRooms(TArray<FDetectedDungeonCurseRoom>& Rooms) const;
	void RegisterSpawnHandler();
	void UnregisterSpawnHandler();
	void HandleActorSpawned(AActor* SpawnedActor);
	void RetryApplySpawnedEnemyCurse(TWeakObjectPtr<AActor> SpawnedActorPtr, int32 AttemptIndex);

private:
	FTimerHandle AutoInitializeTimerHandle;
	FDelegateHandle ActorSpawnedHandle;
	int32 CurrentReadinessAttempt = 0;
};
