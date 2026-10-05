#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "ALSSavableInterface.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "ProjectProgressionState.generated.h"

USTRUCT()
struct FProjectLearnedSkillSnapshot
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FGameplayTag Tree;
    UPROPERTY(SaveGame) FGuid Node;
    UPROPERTY(SaveGame) int32 Rank = 0;
};

// Save/transfer snapshots only. Runtime attributes, XP, level and skills remain owned by ACF.
USTRUCT()
struct FProjectProgressionSnapshot
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Version = 2;
    UPROPERTY(SaveGame) FGuid CharacterId;
    UPROPERTY(SaveGame) TArray<uint8> Statistics;
    UPROPERTY(SaveGame) TArray<uint8> Practice;
    UPROPERTY(SaveGame) TArray<uint8> Training;
    UPROPERTY(SaveGame) TArray<uint8> InnerState;
    UPROPERTY(SaveGame) TArray<uint8> SurvivalNeeds;
    UPROPERTY(SaveGame) TMap<FName, float> LearnedAttributes;
    UPROPERTY(SaveGame) TArray<FProjectLearnedSkillSnapshot> Skills;
    UPROPERTY(SaveGame) int32 SkillPoints = 0;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectProgressionTravelState : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    UPROPERTY() FProjectProgressionSnapshot Snapshot;
    // Invoke before replacing the current world with a new-character world.
    UFUNCTION(BlueprintCallable, Category="Progression") void ResetForNewCharacter();
};

UCLASS(NotPlaceable)
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectProgressionState : public AInfo, public IALSSavableInterface
{
    GENERATED_BODY()
public:
    AProjectProgressionState();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void OnSaved_Implementation() override;
    virtual void OnLoaded_Implementation() override;
    void Capture(AActor* Player);
    void FreezeForTravel();
    void DiscardForNewCharacter();
    bool Restore(AActor* Player);
    UPROPERTY(SaveGame) FProjectProgressionSnapshot Snapshot;
private:
    void Publish();
    TWeakObjectPtr<AActor> TrackedPlayer;
    bool bLoadPending = false;
    bool bRestoreFailed = false;
    bool bCaptureFrozen = false;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectProgressionWorldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& World) override;
    UPROPERTY(Transient) TObjectPtr<AProjectProgressionState> State;
private:
    void RegisterAttributes(AActor* Actor);
    void HandleWorldTearDown(UWorld* World);
    FDelegateHandle PreSpawnHandle;
    FDelegateHandle SpawnHandle;
    FDelegateHandle TearDownHandle;
};
