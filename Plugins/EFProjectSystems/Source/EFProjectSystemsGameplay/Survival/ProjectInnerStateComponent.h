#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "Survival/ProjectInnerStateMath.h"
#include "ProjectInnerStateComponent.generated.h"

class UProjectSurvivalNeedsComponent;
class UProjectSurvivalStatusComponent;
class UProjectRealtimeSnapshotComponent;
class UProjectDefeatFlowComponent;
class UProjectInnerStateSettings;
class UACFDamageHandlerComponent;
class UACFStatusEffectManagerComponent;
struct FACFDamageEvent;
class UAbilitySystemComponent;

/** Source refreshes itself; a nonempty Family replaces its previous member (anti-stacking). */
USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectInnerStateInfluence
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FName Source;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FName Family;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) double Remaining = 0.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bCalendarHours = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) double WellbeingPerHour = 0.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) double StressTarget = 0.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) double AwarenessCapacityLoss = 0.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) double AwarenessLossPerSecond = 0.0;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectInnerStatePersistentState
{
    GENERATED_BODY()
    UPROPERTY(SaveGame, BlueprintReadOnly) int32 Version = 1;
    UPROPERTY(SaveGame, BlueprintReadOnly) double Stress = 0.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) double Stimulation = 0.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) double Hygiene = 100.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) double BathroomHours = 0.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) double Sickness = 0.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) double Awareness = 100.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) double Air = 100.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) double Composure = 0.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) double CalendarHours = 0.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) double HoursWithoutSocialContact = 0.0;
    UPROPERTY(SaveGame) double RepetitiveWorkHours = 0.0;
    UPROPERTY(SaveGame) double ExerciseHours = 0.0;
    UPROPERTY(SaveGame) double NextExerciseRewardHour = 0.0;
    UPROPERTY(SaveGame) double NextRestRewardHour = 0.0;
    UPROPERTY(SaveGame) double NextSocialRewardHour = 0.0;
    UPROPERTY(SaveGame) double DepressedHours = 0.0;
    UPROPERTY(SaveGame) double RecoveringMoodHours = 0.0;
    UPROPERTY(SaveGame) double SevereHygieneHours = 0.0;
    UPROPERTY(SaveGame) double TreatmentHours = 0.0;
    UPROPERTY(SaveGame) double ExtraDirtBudget = 12.0;
    UPROPERTY(SaveGame) double ExtremePainSeconds = 0.0;
    UPROPERTY(SaveGame) double HitRecoverySeconds = 0.0;
    UPROPERTY(SaveGame) double UnconsciousSeconds = 0.0;
    UPROPERTY(SaveGame) double StepRemainderSeconds = 0.0;
    UPROPERTY(SaveGame) double SlowRemainderSeconds = 0.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) bool bDepressed = false;
    UPROPERTY(SaveGame, BlueprintReadOnly) bool bCriticalSickness = false;
    UPROPERTY(SaveGame, BlueprintReadOnly) bool bUnconscious = false;
    UPROPERTY(SaveGame, BlueprintReadOnly) bool bFatalAsphyxia = false;
    UPROPERTY(SaveGame, BlueprintReadOnly) TSet<FName> DiscoveredAfflictions;
    UPROPERTY(SaveGame, BlueprintReadOnly) FProjectPoisonLedger Poison;
    UPROPERTY(SaveGame) TArray<FProjectInnerStateInfluence> Influences;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FProjectInnerStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProjectInnerStateAccident, int32, Count);

/** Authoritative simulation for new Inner State channels. Existing need/pain APIs
 * remain adapters. No widget, actor search, or per-effect timer owns physiology. */
UCLASS(ClassGroup=(Survival), BlueprintType, meta=(BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UProjectInnerStateComponent();
    static UProjectInnerStateComponent* Ensure(AActor* Actor);
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

    UFUNCTION(BlueprintPure, Category="Inner State") double GetValue(FName Entry) const;
    UFUNCTION(BlueprintPure, Category="Inner State") bool IsAfflictionVisible(FName Entry) const;
    UFUNCTION(BlueprintPure, Category="Inner State") double GetWellbeingRatePerHour() const { return LastWellbeingRate; }
    // Instrumentation samples component work, including status/UI publication, not frame time.
    double LastUpdateMicroseconds = 0;
    uint64 UpdateSerial = 0;
    UFUNCTION(BlueprintPure, Category="Inner State") float GetPracticeMultiplier() const;
    UFUNCTION(BlueprintPure, Category="Inner State") float GetMadnessGainMultiplier() const;
    UFUNCTION(BlueprintPure, Category="Inner State") float GetStaminaRecoveryMultiplier() const;
    UFUNCTION(BlueprintCallable, Category="Inner State") bool AddPoison(double Points, EProjectPoisonSpeed Speed, FName Toxin, FGuid ExposureId);
    UFUNCTION(BlueprintCallable, Category="Inner State") double UseAntidote(FName Toxin = NAME_None);
    UFUNCTION(BlueprintCallable, Category="Inner State") bool ContractSickness(double InitialSeverity = 25.0);
    UFUNCTION(BlueprintCallable, Category="Inner State") void TreatSickness(double Hours = 24.0);
    UFUNCTION(BlueprintCallable, Category="Inner State") void Wash(bool bFullBath = true);
    UFUNCTION(BlueprintCallable, Category="Inner State") bool RelieveBathroom(bool bSanitary = true);
    UFUNCTION(BlueprintCallable, Category="Inner State") void NotifySocialContact(bool bMeaningful = true);
    UFUNCTION(BlueprintCallable, Category="Inner State") void NotifySleepCompleted(double SleptHours, bool bRestorative = true);
    UFUNCTION(BlueprintCallable, Category="Inner State") void NotifyAwarenessImpact(double Points);
    UFUNCTION(BlueprintCallable, Category="Inner State") void SetSleeping(bool bSleeping);
    UFUNCTION(BlueprintCallable, Category="Inner State|Care") bool RequestCareAction(FName Action);
    UFUNCTION(BlueprintPure, Category="Inner State|Care") bool IsSleeping() const { return bSleeping; }
    UFUNCTION(BlueprintCallable, Category="Inner State") void SetInfluence(const FProjectInnerStateInfluence& Influence);
    UFUNCTION(BlueprintCallable, Category="Inner State") void RemoveInfluence(FName Source);
    UFUNCTION(BlueprintCallable, Category="Inner State") void SetBreathingSource(FName Source, double Availability);
    UFUNCTION(BlueprintCallable, Category="Inner State") void ClearBreathingSource(FName Source);
    UFUNCTION(BlueprintCallable, Category="Inner State") void SetContaminationSource(FName Source, double Intensity);
    UFUNCTION(BlueprintCallable, Category="Inner State") void ClearContaminationSource(FName Source);
    UFUNCTION(BlueprintCallable, Category="Inner State") void SetComposureContext(FName Source, double Target);
    UFUNCTION(BlueprintCallable, Category="Inner State") void ClearComposureContext(FName Source);
    UFUNCTION(BlueprintCallable, Category="Inner State") void NotifyPractice(double Hours, bool bPhysical);
    UFUNCTION(BlueprintCallable, Category="Inner State") void OnStateLoaded();
    UFUNCTION(BlueprintCallable, Category="Inner State|Debug") bool SetDebugValue(FName Entry, double Value);
    UPROPERTY(SaveGame, BlueprintReadOnly, Category="Inner State") FProjectInnerStatePersistentState State;
    UPROPERTY(BlueprintAssignable) FProjectInnerStateChanged OnInnerStateChanged;
    UPROPERTY(BlueprintAssignable) FProjectInnerStateAccident OnBathroomAccident;

private:
    void ResolveDependencies();
    void AdvanceAcute(double Seconds, const UProjectInnerStateSettings& Settings);
    void AdvanceSlow(double Hours, const UProjectInnerStateSettings& Settings);
    void Publish(bool bForce = false);
    void SynchronizeStatuses();
    void ApplyPhysiologicalDamage(double Damage, bool bFatal = false);
    void ApplyBenefit(FName Family, double Rate, double Hours);
    double ReadNeedPercent(FName Name) const;
    double GetBreathingAvailability() const;
    double GetExertion() const;
    double GetComposureTarget() const;
    double ReadHealthMax() const;
    void UpdateGameplayModifiers();
    UFUNCTION() void HandleStatusChanged(FName StatusName, bool bActive);
    UFUNCTION() void HandleDamageReceived(const FACFDamageEvent& Event);
    UPROPERTY(Transient) TObjectPtr<UProjectSurvivalNeedsComponent> Needs;
    UPROPERTY(Transient) TObjectPtr<UProjectSurvivalStatusComponent> Statuses;
    UPROPERTY(Transient) TObjectPtr<UProjectRealtimeSnapshotComponent> Resources;
    UPROPERTY(Transient) TObjectPtr<UProjectDefeatFlowComponent> Defeat;
    UPROPERTY(Transient) TObjectPtr<UACFDamageHandlerComponent> DamageHandler;
    UPROPERTY(Transient) TObjectPtr<UACFStatusEffectManagerComponent> AcfStatuses;
    TMap<FName, double> BreathingSources;
    TMap<FName, double> ContaminationSources;
    TMap<FName, double> ComposureSources;
    TMap<FName, int64> PublishedValues;
    TMap<FName, bool> PublishedStatuses;
    UPROPERTY(Transient) TObjectPtr<UAbilitySystemComponent> AbilitySystem;
    FActiveGameplayEffectHandle StaminaEffect;
    int32 AppliedStaminaPercent = -1;
    bool bSynchronizingStatuses = false;
    double LastWellbeingRate = 0.0;
    double RecentThreatSeconds = 0.0;
    double SampledThreat = 0.0;
    double PracticeThisStepHours = 0.0;
    double SleepingHours = 0.0;
    bool bSleeping = false;
    bool bApplyingPhysiologicalDamage = false;
};
