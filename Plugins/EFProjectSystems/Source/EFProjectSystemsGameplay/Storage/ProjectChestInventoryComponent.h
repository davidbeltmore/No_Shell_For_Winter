#pragma once
#include "CoreMinimal.h"
#include "ACFVendorComponent.h"
#include "Engine/DataAsset.h"
#include "Lockpicking/ProjectCalystoChest.h"
#include "ProjectChestInventoryComponent.generated.h"

/** Uses ACF's existing item catalog and rule format. Empty rule rarity inherits ChestRarity. */
UCLASS(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChestProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chest") FGameplayTag ChestRarity;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chest") TArray<FACFItemGenerationRule> ContentRules;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chest", meta=(ClampMin="1", ClampMax="100")) int32 LockDifficulty = 50;
};

/** Native deposit storage with optional one-time ACF generation and lock persistence. */
UCLASS(ClassGroup=(Storage), BlueprintType, meta=(BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChestInventoryComponent : public UACFVendorComponent
{
    GENERATED_BODY()
public:
    UProjectChestInventoryComponent();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chest") TObjectPtr<UProjectChestProfile> Profile;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chest", meta=(DisplayName="Procedural Mode")) bool bProceduralMode = false;
    UFUNCTION(BlueprintCallable, Category="Chest") bool EnsureContents(FString& Error);
    UFUNCTION(BlueprintPure, Category="Chest") bool HasGeneratedContents() const { return bGenerated; }
    /** Called by the child deposit interaction gate before its native parent. */
    UFUNCTION(BlueprintCallable, Category="Chest", meta=(DefaultToSelf="Owner")) static bool PrepareInteraction(AActor* Owner);
    bool ConfigureResolvedLoot(TConstArrayView<FProjectCalystoResolvedChestEntry> Entries, FString& Error);
    bool FinalizeAndVerifyResolvedLoot(TArray<FName>& Verified, FString& Error);
protected:
    virtual void BeginPlay() override;
    virtual void OnComponentSaved_Implementation() override;
    virtual void OnComponentLoaded_Implementation() override;
private:
    UPROPERTY(SaveGame) bool bGenerated = false;
    UPROPERTY(SaveGame) bool bSavedLockState = false;
    UPROPERTY(SaveGame) bool bSavedLocked = false;
    UPROPERTY(Transient) TArray<FProjectCalystoResolvedChestEntry> FrozenContents;
    bool bDirectorOwnsContents = false;
    bool bDirectorVerified = false;
    bool bGenerating = false;
    bool bUnlockContinuationPending = false;
    bool VerifyResolvedLoot(FString& Error) const;
    UFUNCTION() void ContinueUnlockedInteraction(APawn* Pawn, const FString& InteractionType);
};
