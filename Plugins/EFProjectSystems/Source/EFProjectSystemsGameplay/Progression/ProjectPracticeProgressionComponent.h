#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AttributeSet.h"
#include "Game/ACFDamageType.h"
#include "ProjectPracticeProgressionComponent.generated.h"

class UACFGASStatisticsComponent;
class UAbilitySystemComponent;

USTRUCT(BlueprintType)
struct FProjectAttributePractice
{
    GENERATED_BODY()
    UPROPERTY(SaveGame, BlueprintReadOnly) FName Attribute;
    UPROPERTY(SaveGame, BlueprintReadOnly) double Progress = 0.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) double ExposureHours = 0.0;
};

// Progress fractions and recovery are not another XP currency. Learned ranks live in GAS.
UCLASS(ClassGroup=(Progression), meta=(BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectPracticeProgressionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UProjectPracticeProgressionComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    static UProjectPracticeProgressionComponent* Ensure(AActor* Owner);
    static FGameplayAttribute ResolveAttribute(FName Name);
    UFUNCTION(BlueprintPure, Category="Progression") float GetLearnedRank(FName Attribute) const;
    UFUNCTION(BlueprintPure, Category="Progression") double GetPracticeProgress(FName Attribute) const;
    UFUNCTION(BlueprintPure, Category="Progression") double GetPracticeEfficiency(FName Attribute) const;
    UFUNCTION(BlueprintPure, Category="Progression") double GetRestHours(FName Attribute) const;
    UFUNCTION(BlueprintCallable, Category="Progression") double Practice(FName Attribute, double Hours, double Rate, bool bGrantGlobalExperience = true);
    bool AddLearnedRanks(FName Attribute, int32 Ranks);
    bool AddBackgroundRanks(FName Attribute, int32 Ranks);
    void ResetBackgroundRanks();
    void SetPracticingAttribute(FName Attribute) { PracticingAttribute = Attribute; }
    bool GrantExperience(double Amount);
    void RefreshBindings();
    UFUNCTION() void OnComponentLoaded();
    UPROPERTY(SaveGame) TArray<FProjectAttributePractice> PracticeState;
    UPROPERTY(SaveGame) double ExperienceFraction = 0.0;
    UPROPERTY(SaveGame) int32 RewardedThroughLevel = 1;
    UPROPERTY(SaveGame) TMap<FName, int32> BackgroundRanks;
private:
    UFUNCTION() void HandleLevelUp(int32 Level);
    UFUNCTION() void HandleExperienceChanged(int32 CurrentExperience, int32 Delta);
    UFUNCTION() void HandleDamageInflicted(const FACFDamageEvent& Event);
    UFUNCTION() void HandleDamageReceived(const FACFDamageEvent& Event);
    void NotifyRankChanged(FName Attribute, int32 OldRank, int32 NewRank);
    UPROPERTY(Transient) TObjectPtr<UACFGASStatisticsComponent> Statistics;
    UPROPERTY(Transient) TObjectPtr<UAbilitySystemComponent> AbilitySystem;
    FName PracticingAttribute;
    double RecentPracticeUntil = 0.0;
    FName RecentPracticeAttribute;
    TSet<TWeakObjectPtr<AActor>> HitTargetsThisFrame;
    double LastHitTime = -1.0;
};
