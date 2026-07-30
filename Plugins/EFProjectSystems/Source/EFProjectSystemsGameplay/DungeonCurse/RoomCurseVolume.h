#pragma once

#include "CoreMinimal.h"
#include "DungeonCurse/DungeonCurseTypes.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"
#include "RoomCurseVolume.generated.h"

class ADungeonCurseManager;
class ARoomGameplayMarker;
class UDungeonCurseComponent;
class UBoxComponent;
class ULightComponent;
class UPrimitiveComponent;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API ARoomCurseVolume : public AActor
{
	GENERATED_BODY()

public:
	ARoomCurseVolume();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	void InitializeFromDetectedRoom(const FDetectedDungeonCurseRoom& RoomData, const FRoomCurseDefinition& Curse, ADungeonCurseManager* InManager);

	UFUNCTION(BlueprintPure, Category = "Dungeon Curse|Volume")
	FName GetCurseSourceID() const;

	UFUNCTION(BlueprintPure, Category = "Dungeon Curse|Volume")
	bool ContainsActor(AActor* Actor) const;

	UFUNCTION(BlueprintCallable, Category = "Dungeon Curse|Volume")
	void ApplyEnemyLevelCurseToCurrentEnemies();

	bool ApplyEnemyLevelCurseToActor(AActor* EnemyActor);
	bool WantsEnemyLevelProcessing() const;
	void RestoreModifiedLights();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon Curse|Volume")
	TObjectPtr<UBoxComponent> BoxComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Volume")
	FRoomCurseDefinition AssignedCurse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Volume")
	EGeneratedRoomType RoomType;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Dungeon Curse|Volume")
	bool bActive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Debug")
	bool bDebugDraw;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Debug")
	FColor DebugColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Volume", meta = (ClampMin = "1.0", UIMin = "1.0"))
	FVector VolumeExtent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Volume", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float TickInterval;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Dungeon Curse|Volume")
	TObjectPtr<AActor> SourceActor;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Dungeon Curse|Volume")
	TObjectPtr<ARoomGameplayMarker> SourceMarker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Volume")
	TSubclassOf<AActor> PlayerClassFilter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Enemies")
	bool bEnableMaxLevelEnemyCurse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Curse|Enemies", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxEnemyLevelForCurrentFloor;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void UpdateBoxExtent();
	bool IsValidTarget(AActor* Actor) const;
	UDungeonCurseComponent* ResolveOrCreateCurseComponent(AActor* Actor) const;
	void ApplyVolumeOnlyEffects(AActor* TargetActor);
	void RemoveVolumeOnlyEffects();
	void ApplyLightReduction();
	void CollectLocalLights(TArray<ULightComponent*>& OutLights) const;
	void PlayWhisper(AActor* TargetActor) const;
	void SpawnOptionalVFX() const;
	void NotifyListenersEntered() const;
	void NotifyListenersExited() const;
	void NotifyListenersDiscovered() const;
	void DrawVolumeDebug() const;
	FBox GetCurseBounds() const;
	FName ResolveDisplayCurseID() const;

private:
	UPROPERTY(Transient)
	TObjectPtr<ADungeonCurseManager> OwningManager;

	UPROPERTY(VisibleInstanceOnly, Category = "Dungeon Curse|Volume")
	FName CurseSourceID;

	TMap<TObjectKey<AActor>, TWeakObjectPtr<AActor>> ActiveTargets;
	TMap<TWeakObjectPtr<ULightComponent>, float> ModifiedLightIntensities;
	bool bLightReductionApplied = false;
	bool bWarnedNoLights = false;
	bool bWarnedNoEnemies = false;
};
