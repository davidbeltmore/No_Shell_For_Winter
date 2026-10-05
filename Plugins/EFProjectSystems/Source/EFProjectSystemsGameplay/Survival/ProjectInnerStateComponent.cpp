#include "Survival/ProjectInnerStateComponent.h"
#include "Survival/ProjectInnerStateSettings.h"
#include "Survival/ProjectInnerStateGameplayAdapters.h"
#include "AbilitySystemComponent.h"
#include "ACFStatisticsSet.h"
#include "ACFGASStatisticsComponent.h"
#include "ARSFunctionLibrary.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "Survival/ProjectRealtimeSnapshotComponent.h"
#include "Defeat/ProjectDefeatFlowComponent.h"
#include "DayCycle/ProjectDayCycleSettings.h"
#include "UI/ProjectEmoteSubsystem.h"
#include "Locomotion/ProjectEmoteComponent.h"
#include "Social/ProjectSocialSubsystem.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "Components/ACFStatusEffectManagerComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "DirtyPawnComponent.h"

namespace
{
    using Math = FProjectInnerStateMath;
    double Unit(double V) { return FMath::IsFinite(V) ? FMath::Clamp(V, 0.0, 1.0) : 0.0; }
    double Percent(double V) { return FMath::IsFinite(V) ? FMath::Clamp(V, 0.0, 100.0) : 0.0; }
    double Positive(double V) { return FMath::IsFinite(V) ? FMath::Max(V, 0.0) : 0.0; }
    const FName Entries[] = {TEXT("Stress"),TEXT("Stimulation"),TEXT("Hygiene"),TEXT("Bathroom"),TEXT("Sickness"),TEXT("Awareness"),TEXT("Air"),TEXT("Composure"),TEXT("Poison"),TEXT("Curse"),TEXT("Sensation")};
}

UProjectInnerStateComponent::UProjectInnerStateComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f;
}
UProjectInnerStateComponent* UProjectInnerStateComponent::Ensure(AActor* Actor)
{
    if (!IsValid(Actor)) return nullptr;
    if (auto* Existing = Actor->FindComponentByClass<UProjectInnerStateComponent>()) { Existing->SetComponentTickEnabled(true); return Existing; }
    auto* Component = NewObject<UProjectInnerStateComponent>(Actor, TEXT("ProjectInnerState"));
    Actor->AddInstanceComponent(Component);
    Component->RegisterComponent();
    return Component;
}
void UProjectInnerStateComponent::BeginPlay()
{
    Super::BeginPlay();
    ResolveDependencies();
    OnStateLoaded();
}
void UProjectInnerStateComponent::EndPlay(EEndPlayReason::Type Reason)
{
    if (DamageHandler) DamageHandler->OnDamageReceived.RemoveDynamic(this, &ThisClass::HandleDamageReceived);
    if (Statuses) Statuses->OnStatusChanged.RemoveDynamic(this, &ThisClass::HandleStatusChanged);
    if (AbilitySystem && StaminaEffect.IsValid()) AbilitySystem->RemoveActiveGameplayEffect(StaminaEffect);
    Super::EndPlay(Reason);
}
void UProjectInnerStateComponent::ResolveDependencies()
{
    if (!GetOwner()) return;
    Needs = GetOwner()->FindComponentByClass<UProjectSurvivalNeedsComponent>();
    Statuses = GetOwner()->FindComponentByClass<UProjectSurvivalStatusComponent>();
    if (Statuses) Statuses->OnStatusChanged.AddUniqueDynamic(this, &ThisClass::HandleStatusChanged);
    AbilitySystem = GetOwner()->FindComponentByClass<UAbilitySystemComponent>();
    Resources = GetOwner()->FindComponentByClass<UProjectRealtimeSnapshotComponent>();
    Defeat = GetOwner()->FindComponentByClass<UProjectDefeatFlowComponent>();
    AcfStatuses = GetOwner()->FindComponentByClass<UACFStatusEffectManagerComponent>();
    auto* ResolvedDamage = GetOwner()->FindComponentByClass<UACFDamageHandlerComponent>();
    if (ResolvedDamage != DamageHandler)
    {
        if (DamageHandler) DamageHandler->OnDamageReceived.RemoveDynamic(this, &ThisClass::HandleDamageReceived);
        DamageHandler = ResolvedDamage;
        if (DamageHandler) DamageHandler->OnDamageReceived.AddUniqueDynamic(this, &ThisClass::HandleDamageReceived);
    }
    if (Needs) AddTickPrerequisiteComponent(Needs);
}
double UProjectInnerStateComponent::GetValue(FName Entry) const
{
    if (Entry == TEXT("Stress")) return State.Stress;
    if (Entry == TEXT("Stimulation")) return State.Stimulation;
    if (Entry == TEXT("Hygiene")) return State.Hygiene;
    if (Entry == TEXT("Bathroom")) return Percent(State.BathroomHours * 100.0 / FMath::Max(1.0, GetDefault<UProjectInnerStateSettings>()->BathroomDeadlineHours));
    if (Entry == TEXT("Sickness")) return State.Sickness;
    if (Entry == TEXT("Awareness")) return State.Awareness;
    if (Entry == TEXT("Air")) return State.Air;
    if (Entry == TEXT("Composure")) return State.Composure;
    if (Entry == TEXT("Poison")) return State.Poison.GetPendingPoints();
    if (Entry == TEXT("Sensation") || Entry == TEXT("Pain")) return Needs ? 100.0 * Needs->GetSensationCurrentValue(TEXT("Sensation")) / FMath::Max(.001f, Needs->GetSensationMaxValue(TEXT("Pain"))) : 0.0;
    return Needs ? Needs->GetSensationCurrentValue(Entry) : 0.0;
}
bool UProjectInnerStateComponent::IsAfflictionVisible(FName Entry) const
{
    return State.DiscoveredAfflictions.Contains(Entry) && GetValue(Entry) > 0.0;
}
float UProjectInnerStateComponent::GetPracticeMultiplier() const
{
    const auto& S = *GetDefault<UProjectInnerStateSettings>();
    return static_cast<float>(State.bDepressed ? FMath::Max(.5, 1-S.DepressionPracticePenalty) : 1 + S.ContentPracticeBonus * Math::SeverityAbove(State.Stimulation,40));
}
float UProjectInnerStateComponent::GetStaminaRecoveryMultiplier() const
{
    const auto& S = *GetDefault<UProjectInnerStateSettings>();
    return static_cast<float>(FMath::Clamp(1.0 - S.StressStaminaPenalty * Math::SeverityAbove(State.Stress,60) + S.ComposureStaminaBonus * State.Composure/100, .5, 1.25));
}
double UProjectInnerStateComponent::ReadNeedPercent(FName Name) const
{
    return Needs && Needs->HasNeed(Name) ? Needs->GetNeedNormalizedValue(Name) * 100.0 : 100.0;
}
double UProjectInnerStateComponent::ReadHealthMax() const
{
    float Current = 0, Maximum = 0;
    if (Resources && Resources->TryReadOwnerResource(TEXT("Health"), Current, Maximum)) return Positive(Maximum);
    if (const auto* Stats=GetOwner()?GetOwner()->FindComponentByClass<UACFGASStatisticsComponent>():nullptr)
        return Positive(Stats->GetMaxValueForStatitstic(UARSFunctionLibrary::GetHealthTag()));
    return 0;
}
double UProjectInnerStateComponent::GetExertion() const
{
    return GetOwner() ? Unit(GetOwner()->GetVelocity().Size2D() / 600.0) : 0.0;
}
double UProjectInnerStateComponent::GetBreathingAvailability() const
{
    double Availability = 1.0;
    for (const auto& Source : BreathingSources) Availability = FMath::Min(Availability, Source.Value);
    if (const auto* Character = Cast<ACharacter>(GetOwner()))
        if (const auto* Move = Character->GetCharacterMovement(); Move && Move->IsSwimming())
            if (const auto* Volume = Character->GetPhysicsVolume(); Volume && Volume->bWaterVolume)
            {
                const FVector Head = Character->GetActorLocation() + FVector(0,0,Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * .85f);
                if (Volume->EncompassesPoint(Head)) Availability = 0.0;
            }
    return Availability;
}
double UProjectInnerStateComponent::GetComposureTarget() const
{
    double Fear = SampledThreat;
    double Confidence = 0;
    for (const auto& Source : ComposureSources)
    {
        Fear = FMath::Min(Fear, Source.Value);
        Confidence = FMath::Max(Confidence, Source.Value);
    }
    return FMath::Clamp(Fear + Confidence, -100.0, 100.0);
}
void UProjectInnerStateComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    if (!GetOwner() || !GetOwner()->HasAuthority() || UGameplayStatics::IsGamePaused(this) || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0 || State.bFatalAsphyxia) return;
    if (!Needs || !Resources) ResolveDependencies();
    if (DamageHandler && !DamageHandler->GetIsAlive()) return;
    const double CpuStart = FPlatformTime::Seconds();
    const auto& Settings = *GetDefault<UProjectInnerStateSettings>();
    if (!Needs || !Resources)
    {
        // ACF enemies do not own the player's survival/HUD stack. Keep the same
        // poison ledger against their native statistics, without attaching that stack.
        ApplyPhysiologicalDamage(State.Poison.Advance(DeltaTime,Settings.Poison));
        Publish();
        if (State.Poison.GetPendingPoints()<=0) SetComponentTickEnabled(false);
        LastUpdateMicroseconds=(FPlatformTime::Seconds()-CpuStart)*1.e6; ++UpdateSerial;
        return;
    }
    const double Step = FMath::Clamp(Settings.AcuteStepSeconds, .02, 1.0);
    State.StepRemainderSeconds += DeltaTime;
    // Fixed simulation quantum: neither render FPS nor caller subdivision changes the math.
    while (State.StepRemainderSeconds + 1.e-9 >= Step && !State.bFatalAsphyxia)
    {
        State.StepRemainderSeconds = FMath::Max(0.0, State.StepRemainderSeconds - Step);
        AdvanceAcute(Step, Settings);
        State.SlowRemainderSeconds += Step;
        if (State.SlowRemainderSeconds + 1.e-9 >= FMath::Max(.1, Settings.SlowStepSeconds))
        {
            AdvanceSlow(State.SlowRemainderSeconds * 24.0 / FMath::Max(1.f, UProjectDayCycleSettings::Get()->DayLengthSeconds), Settings);
            State.SlowRemainderSeconds = 0.0;
        }
    }
    Publish();
    LastUpdateMicroseconds = (FPlatformTime::Seconds()-CpuStart)*1.e6;
    ++UpdateSerial;
}
void UProjectInnerStateComponent::AdvanceAcute(double Seconds, const UProjectInnerStateSettings& S)
{
    if (State.bUnconscious && Defeat) Defeat->SetPhysiologicalUnconscious(true);
    if (State.Awareness <= 0 && !State.bUnconscious)
    { State.bUnconscious=true; State.UnconsciousSeconds=0; if (Defeat) Defeat->SetPhysiologicalUnconscious(true); }
    const double Hours = Seconds * 24.0 / FMath::Max(1.f, UProjectDayCycleSettings::Get()->DayLengthSeconds);
    State.HitRecoverySeconds = FMath::Max(0.0, State.HitRecoverySeconds - Seconds);
    RecentThreatSeconds = FMath::Max(0.0, RecentThreatSeconds - Seconds);
    double CapacityLoss = 0, AcuteLoss = 0;
    for (auto& Effect : State.Influences)
    {
        if (Effect.Remaining <= 0) continue;
        CapacityLoss += Positive(Effect.AwarenessCapacityLoss);
        AcuteLoss += Positive(Effect.AwarenessLossPerSecond);
        Effect.Remaining = FMath::Max(0.0, Effect.Remaining - (Effect.bCalendarHours ? Hours : Seconds));
    }
    State.Influences.RemoveAll([](const FProjectInnerStateInfluence& E) { return E.Remaining <= 0.0; });
    const double Pain = Needs ? Needs->GetSensationNormalizedValue(TEXT("Pain")) * 100.0 : 0.0;
    State.ExtremePainSeconds = Pain >= 95 ? State.ExtremePainSeconds + Seconds : 0.0;
    if (State.ExtremePainSeconds >= S.ExtremePainGraceSeconds) AcuteLoss += Positive(S.ExtremePainLossPerSecond);
    float Health = 0, Maximum = 0;
    Resources->TryReadOwnerResource(TEXT("Health"), Health, Maximum);
    CapacityLoss += S.PainCapacityWeight * Math::SeverityAbove(Pain, 30) + S.SleepCapacityWeight * Math::SeverityBelow(ReadNeedPercent(TEXT("Sleep")), 25)
        + S.SicknessCapacityWeight * State.Sickness / 100 + S.LowHealthCapacityWeight * Math::SeverityBelow(Maximum > 0 ? 100.0 * Health / Maximum : 100.0, 25);
    const double Alcohol = Needs ? Needs->GetSensationNormalizedValue(TEXT("Alcohol")) : 0.0;
    CapacityLoss += S.AlcoholCapacityWeight * Alcohol + S.ExertionCapacityWeight * GetExertion() * Math::SeverityBelow(ReadNeedPercent(TEXT("Sleep")),25);
    if (Alcohol >= .9) AcuteLoss += 3 * (Alcohol-.9) / .1;
    const double Capacity = 100 - FMath::Min(FMath::Clamp(S.MaximumCapacityLoss,0.0,100.0), CapacityLoss);
    const double Critical = State.bCriticalSickness ? Math::SeverityAbove(State.Sickness, 80) : 0.0;
    AcuteLoss += Critical * Positive(S.CriticalSicknessAwarenessPerSecond);
    const double Availability = GetBreathingAvailability();
    const double OldAir = State.Air;
    const double AirLoss = (1.0 - Availability) * 100.0 / FMath::Max(1.0, FMath::Lerp(S.AirReserveSeconds, S.AirReserveExertionSeconds, GetExertion()));
    const double AirRate = Availability * Positive(S.AirRecoveryPerSecond) - AirLoss;
    State.Air = Percent(State.Air + AirRate * Seconds);
    const double NoAirSeconds = AirRate < 0 && State.Air <= 0 ? FMath::Clamp(Seconds - OldAir / -AirRate, 0.0, Seconds) : 0.0;
    const double Target = GetComposureTarget();
    State.Composure = Math::ApproachHalfLife(State.Composure, Target, Seconds, Target < State.Composure ? S.FearHalfLifeSeconds : S.ConfidenceHalfLifeSeconds);
    State.Awareness = Math::AdvanceAwareness(State.Awareness, Capacity, AcuteLoss, S.AwarenessRecoveryPerSecond,
        S.AwarenessCapacityFallPerSecond, Seconds - NoAirSeconds, State.HitRecoverySeconds <= 0 && State.Air > 20);
    State.Awareness = Math::AdvanceAwareness(State.Awareness, Capacity, AcuteLoss + S.AsphyxiaAwarenessPerSecond,
        0, S.AwarenessCapacityFallPerSecond, NoAirSeconds, false);
    double Damage = State.Poison.Advance(Seconds, S.Poison);
    Damage += Positive(Maximum) * (NoAirSeconds * Positive(S.AsphyxiaHealthFractionPerSecond) + Seconds * Critical * Positive(S.CriticalSicknessHealthFractionPerSecond));
    if (State.Air <= 0 && State.Awareness <= 0 && Availability < 1)
    {
        State.bFatalAsphyxia = true;
        ApplyPhysiologicalDamage(FMath::Max(0.f, Health), true);
        Publish(true);
        return;
    }
    ApplyPhysiologicalDamage(Damage);
    if (State.Awareness <= 0 && !State.bUnconscious)
    {
        State.bUnconscious = true;
        State.UnconsciousSeconds = 0;
        // Presentation is routed through the existing defeat bridge (no independent timers).
        if (Defeat) Defeat->SetPhysiologicalUnconscious(true);
    }
    if (State.bUnconscious)
    {
        State.UnconsciousSeconds += Seconds;
        if (State.Awareness >= S.WakeThreshold && State.UnconsciousSeconds >= S.MinimumUnconsciousSeconds && AcuteLoss <= 0 && State.Air > 20)
        {
            State.bUnconscious = false;
            if (Defeat) Defeat->SetPhysiologicalUnconscious(false);
        }
    }
}
void UProjectInnerStateComponent::AdvanceSlow(double Hours, const UProjectInnerStateSettings& S)
{
    State.CalendarHours += Hours;
    State.HoursWithoutSocialContact += Hours;
    if (bSleeping)
    {
        auto* Emote = GetOwner()->FindComponentByClass<UProjectEmoteComponent>();
        if (!Emote || Emote->GetActiveInteractionId()!=TEXT("Actions.Care.Rest") || State.bUnconscious || RecentThreatSeconds>0)
        {
            SetSleeping(false);
            if (Emote && Emote->GetActiveInteractionId()==TEXT("Actions.Care.Rest"))
                if (auto* Actions=GetWorld()->GetSubsystem<UProjectEmoteSubsystem>()) Actions->CancelRuntimeAction();
        }
        else
        {
            SleepingHours += Hours;
            if (Needs) Needs->ModifyNeedValue(TEXT("Sleep"),static_cast<float>(Hours*S.SleepingRecoveryPerHour*Needs->GetNeedMaxValue(TEXT("Sleep"))/100));
            if (ReadNeedPercent(TEXT("Sleep"))>=99.9)
            {
                SetSleeping(false);
                if (auto* Actions=GetWorld()->GetSubsystem<UProjectEmoteSubsystem>()) Actions->CancelRuntimeAction();
            }
        }
    }
    if (Needs && GetValue(TEXT("Sensation")) < 0) Needs->ModifySensationValue(TEXT("Sensation"), static_cast<float>(FMath::Min(-GetValue(TEXT("Sensation")), Hours * S.PleasureReturnPerHour) * Needs->GetSensationMaxValue(TEXT("Pain"))/100.0));
    const double Practice = FMath::Min(Hours, PracticeThisStepHours);
    PracticeThisStepHours = FMath::Max(0.0, PracticeThisStepHours - Practice);
    State.RepetitiveWorkHours = FMath::Max(0.0, State.RepetitiveWorkHours - 2 * (Hours - Practice));
    State.ExtraDirtBudget = FMath::Min(Positive(S.ExtraHygieneLossPerDay), State.ExtraDirtBudget + Hours * Positive(S.ExtraHygieneLossPerDay) / 24.0);
    double Contamination = 0;
    for (const auto& Source : ContaminationSources) Contamination = FMath::Max(Contamination, Source.Value);
    const double ExternalContamination=Contamination;
    // Sustained locomotion also earns the daily exercise benefit, without counting as repetitive work.
    if (GetExertion()>.5 && !bSleeping) State.ExerciseHours+=Hours;
    if (State.ExerciseHours>=.5 && State.CalendarHours>=State.NextExerciseRewardHour)
    { ApplyBenefit(TEXT("Exercise"),S.ExerciseWellbeingPerHour,12); State.NextExerciseRewardHour=State.CalendarHours+24; State.ExerciseHours=0; }
    const double ExtraDirt = FMath::Min(State.ExtraDirtBudget, Hours * (.5 * GetExertion()));
    State.ExtraDirtBudget -= ExtraDirt;
    TArray<FDirtyPawnHygieneRegion> Surfaces;
    if (auto* Dirty=UDirtyPawnComponent::FindCanonicalDirtyPawnComponent(GetOwner())) Dirty->GetHygieneSurfaceSamples(Surfaces);
    auto Exposure=FProjectHygieneMath::Evaluate(Surfaces,S.SurfaceHygiene,S.HygieneLossPerHour,Hours>0?ExtraDirt/Hours:0);
    Contamination=FMath::Max(Contamination,Exposure.Contamination);
    // Explicit polluted-water/gameplay sources can veto washing; being dirty cannot.
    if(ExternalContamination>0) Exposure.CleanWaterCoverage=0;
    State.Hygiene=FProjectHygieneMath::Advance(State.Hygiene,Hours,Exposure,S.SurfaceHygiene);
    State.BathroomHours += Hours;
    const double Deadline = FMath::Max(1.0, S.BathroomDeadlineHours);
    if (State.BathroomHours >= Deadline)
    {
        const int32 Count = FMath::FloorToInt(State.BathroomHours / Deadline);
        State.BathroomHours = FMath::Fmod(State.BathroomHours, Deadline);
        State.Hygiene = Percent(State.Hygiene - Positive(S.AccidentHygieneLoss) * Count);
        OnBathroomAccident.Broadcast(Count);
    }
    if (State.Sickness > 0)
    {
        const double Treated = FMath::Min(Hours, State.TreatmentHours);
        State.TreatmentHours = FMath::Max(0.0, State.TreatmentHours - Hours);
        State.Sickness = Percent(State.Sickness + (Hours - Treated) * Positive(S.SicknessGainPerHour) - Treated * Positive(S.TreatedSicknessRecoveryPerHour));
        if (State.Sickness >= 100) State.bCriticalSickness = true;
        else if (State.Sickness <= 80) State.bCriticalSickness = false;
    }
    if (Contamination > 0 && State.Sickness <= 0 && FMath::FRand() < Math::ExposureProbability(
        S.ContaminationHazardPerHour * Contamination * Math::SeverityBelow(State.Hygiene, 40), Hours)) ContractSickness(S.InitialSickness);
    const double Pain = Needs ? Needs->GetSensationNormalizedValue(TEXT("Pain")) * 100.0 : 0.0;
    double StressTarget = S.HungerStressWeight * Math::SeverityBelow(ReadNeedPercent(TEXT("Hunger")),25)
        + S.ThirstStressWeight * Math::SeverityBelow(ReadNeedPercent(TEXT("Thirst")),25)
        + S.SleepStressWeight * Math::SeverityBelow(ReadNeedPercent(TEXT("Sleep")),25)
        + FMath::Max(S.PainStressWeight * Math::SeverityAbove(Pain,30),S.SicknessStressWeight * State.Sickness/100)
        + S.OverworkStressWeight * Math::SeverityAbove(State.RepetitiveWorkHours,S.RepetitiveGraceHours,S.RepetitiveGraceHours+4)
        + S.HygieneStressWeight * Math::SeverityBelow(State.Hygiene,30) + S.BathroomStressWeight * Math::SeverityAbove(GetValue(TEXT("Bathroom")),85)
        + S.TerrorStressWeight * FMath::Max(0.0,-State.Composure)/100;
    double Wellbeing = 0;
    for (const auto& Effect : State.Influences) { StressTarget += Effect.StressTarget; Wellbeing += Effect.WellbeingPerHour; }
    State.Stress = Math::ApproachHalfLife(State.Stress,Percent(StressTarget),Hours,
        StressTarget > State.Stress ? S.StressRiseHalfLifeHours : bSleeping ? S.StressSleepHalfLifeHours : S.StressRecoveryHalfLifeHours);
    const double Madness = Needs ? Needs->GetSensationNormalizedValue(TEXT("Madness"))*100.0 : 0.0;
    Wellbeing -= S.StressWellbeingLossPerHour*Math::SeverityAbove(State.Stress,35) + S.HygieneWellbeingLossPerHour*Math::SeverityBelow(State.Hygiene,40)
        + S.LonelyWellbeingLossPerHour*Math::SeverityAbove(State.HoursWithoutSocialContact,S.LonelyGraceHours,S.LonelyGraceHours+S.LonelyRampHours)
        + S.MadnessWellbeingLossPerHour*Math::SeverityAbove(Madness,60) + S.SicknessWellbeingLossPerHour*State.Sickness/100;
    LastWellbeingRate = FMath::Clamp(Wellbeing,-Positive(S.MaximumWellbeingRatePerHour),Positive(S.MaximumWellbeingRatePerHour));
    State.Stimulation = Math::IntegrateWellbeing(State.Stimulation,LastWellbeingRate,Hours,S.WellbeingHalfLifeHours);
    State.DepressedHours = State.Stimulation < -40 ? State.DepressedHours+Hours : 0;
    State.RecoveringMoodHours = State.Stimulation > -25 ? State.RecoveringMoodHours+Hours : 0;
    if (State.DepressedHours >= S.DepressedEntryHours) State.bDepressed = true;
    if (State.RecoveringMoodHours >= S.DepressedExitHours) State.bDepressed = false;
    State.SevereHygieneHours = State.Hygiene < 20 ? State.SevereHygieneHours+Hours : 0;
    if (Needs && State.SevereHygieneHours >= 24) Needs->ModifySensationValue(TEXT("Madness"),static_cast<float>(S.SevereHygieneMadnessPerHour*Hours));
    if (Needs && ReadNeedPercent(TEXT("Sleep"))<10) Needs->ModifySensationValue(TEXT("Madness"),static_cast<float>(S.SevereSleepMadnessPerHour*Hours*Math::SeverityBelow(ReadNeedPercent(TEXT("Sleep")),10)));
    if (GetWorld() && GetWorld()->GetGameInstance())
        if (const auto* Social = GetWorld()->GetGameInstance()->GetSubsystem<UProjectSocialSubsystem>())
            if (Social->HasLivingRecruitedCompanionWithin(GetOwner(),1200)) NotifySocialContact(true);
    AActor* Enemy = Resources ? Resources->FindNearestRelevantEnemyToOwner() : nullptr;
    SampledThreat = Enemy && FVector::DistSquared(Enemy->GetActorLocation(),GetOwner()->GetActorLocation()) < FMath::Square(1500.f) ? -50 : 0;
    if (RecentThreatSeconds > 0) SampledThreat = FMath::Min(SampledThreat,-80.0);
    if (Statuses && Statuses->IsStatusActive(TEXT("Cursed"))) State.DiscoveredAfflictions.Add(TEXT("Curse"));
    SynchronizeStatuses();
    UpdateGameplayModifiers();
}
void UProjectInnerStateComponent::ApplyPhysiologicalDamage(double Damage, bool bFatal)
{
    if (!FMath::IsFinite(Damage) || Damage < 0 || (Damage==0 && HealthDebitRoundingRemainder<=0)) return;
    // Exact resource debit bypasses weapon/armor calculations. ACF's health-zero listener owns death.
    TGuardValue<bool> Guard(bApplyingPhysiologicalDamage,true);
    float Health=0, Maximum=0;
    auto* Stats=GetOwner()?GetOwner()->FindComponentByClass<UACFGASStatisticsComponent>():nullptr;
    const auto HealthTag=UARSFunctionLibrary::GetHealthTag();
    const bool bResourceBridge=Resources && Resources->TryReadOwnerResource(TEXT("Health"),Health,Maximum);
    if (!bResourceBridge && Stats) { Health=Stats->GetCurrentValueForStatitstic(HealthTag); Maximum=Stats->GetMaxValueForStatitstic(HealthTag); }
    if (Health<=0) return;
    const double Requested=Damage+HealthDebitRoundingRemainder;
    if (Requested<=0) { HealthDebitRoundingRemainder=Requested; return; }
    // Compensate float health debits so many 0.1-second steps cannot leave a
    // phantom sliver of life after a complete debt. Tolerance is one max-HP float ulp.
    const double Precision=FMath::Max(1.e-7,Maximum*static_cast<double>(FLT_EPSILON));
    const float Debit=static_cast<float>(Requested+Precision>=Health?Health:Requested);
    float Actual=0;
    if (bResourceBridge) Actual=-Resources->ApplyOwnerResourceDelta(TEXT("Health"),-Debit);
    else if (Stats) { Stats->ModifyStatistic(HealthTag,-Debit); Actual=Health-Stats->GetCurrentValueForStatitstic(HealthTag); }
    const double Error=Requested-Actual;
    HealthDebitRoundingRemainder=FMath::Abs(Error)<=FMath::Max(.001,Precision*4)?Error:0;
    if (bFatal) UE_LOG(LogTemp,Display,TEXT("[InnerState] FatalAsphyxia owner=%s"),*GetNameSafe(GetOwner()));
}
void UProjectInnerStateComponent::Publish(bool bForce)
{
    bool Changed = bForce;
    for (const FName Entry : Entries)
    {
        const double Value = GetValue(Entry);
        const int64 Quantized = static_cast<int64>(FMath::Clamp(FMath::RoundToDouble(Value),-1.e15,1.e15));
        const int64* Old = PublishedValues.Find(Entry);
        if (!Old || *Old != Quantized) { PublishedValues.Add(Entry,Quantized); Changed=true; }
    }
    // A face can change while its displayed integer remains +20 or -20.
    const FName MoodKey(TEXT("Stimulation.Mood"));
    const int64 Mood=Math::GetStimulationMoodBand(State.Stimulation);
    const int64* PreviousMood=PublishedValues.Find(MoodKey);
    if (!PreviousMood || *PreviousMood!=Mood) { PublishedValues.Add(MoodKey,Mood); Changed=true; }
    if (Changed) OnInnerStateChanged.Broadcast();
}
void UProjectInnerStateComponent::SynchronizeStatuses()
{
    if (!Statuses) return;
    TGuardValue<bool> Guard(bSynchronizingStatuses,true);
    const TPair<FName,bool> Values[] = {{TEXT("Stressed"),State.Stress>=60},{TEXT("Depressed"),State.bDepressed},
        {TEXT("Content"),State.Stimulation>=40},{TEXT("Unhygienic"),State.Hygiene<40},{TEXT("BathroomUrgent"),GetValue(TEXT("Bathroom"))>=90},
        {TEXT("Sick"),State.Sickness>0},{TEXT("CriticalSickness"),State.bCriticalSickness},{TEXT("Poisoned"),State.Poison.GetPendingPoints()>0},
        {TEXT("Breathless"),State.Air<25},{TEXT("Disoriented"),State.Awareness<30},{TEXT("Terrified"),State.Composure<-60},
        {TEXT("Determined"),State.Composure>60},{TEXT("Pleasure"),GetValue(TEXT("Sensation")) < -20},{TEXT("PhysiologicallyUnconscious"),State.bUnconscious}};
    for (const auto& Pair : Values)
        if (!PublishedStatuses.Contains(Pair.Key) || PublishedStatuses[Pair.Key]!=Pair.Value)
        { PublishedStatuses.Add(Pair.Key,Pair.Value); Statuses->SetForcedStatusActive(Pair.Key,Pair.Value); }
}
bool UProjectInnerStateComponent::AddPoison(double Points,EProjectPoisonSpeed Speed,FName Toxin,FGuid ExposureId)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || (Statuses && Statuses->IsStatusImmune(TEXT("Poisoned")))) return false;
    const bool Added = State.Poison.AddDose(ExposureId,Toxin,Speed,Points,ReadHealthMax(),GetDefault<UProjectInnerStateSettings>()->Poison);
    if (Added) { SetComponentTickEnabled(true); State.DiscoveredAfflictions.Add(TEXT("Poison")); SynchronizeStatuses(); Publish(true); }
    return Added;
}
double UProjectInnerStateComponent::UseAntidote(FName Toxin)
{
    const double Removed = State.Poison.Neutralize(Toxin); SynchronizeStatuses(); Publish(true); return Removed;
}
bool UProjectInnerStateComponent::ContractSickness(double InitialSeverity)
{
    if (!FMath::IsFinite(InitialSeverity) || InitialSeverity<=0 || (Statuses && Statuses->IsStatusImmune(TEXT("Sick")))) return false;
    State.Sickness=FMath::Max(State.Sickness,Percent(InitialSeverity)); State.DiscoveredAfflictions.Add(TEXT("Sickness"));
    if (State.Sickness>=100) State.bCriticalSickness=true;
    SynchronizeStatuses(); Publish(true); return true;
}
void UProjectInnerStateComponent::TreatSickness(double Hours) { State.TreatmentHours=FMath::Max(State.TreatmentHours,Positive(Hours)); }
void UProjectInnerStateComponent::Wash(bool bFullBath)
{
    State.Hygiene=bFullBath?100:FMath::Max(State.Hygiene,FMath::Min(75.0,State.Hygiene+25)); State.SevereHygieneHours=0; Publish(true);
    if (bFullBath) if (auto* Dirty=UDirtyPawnComponent::FindCanonicalDirtyPawnComponent(GetOwner())) Dirty->SetFadeWashVariablesBand(0,Dirty->GetDirtyPawnBodyReferenceHeight(),true,true,true);
}
bool UProjectInnerStateComponent::RelieveBathroom(bool bSanitary)
{
    if (State.bUnconscious || State.BathroomHours<1.0) return false;
    State.BathroomHours=0; if (!bSanitary) State.Hygiene=Percent(State.Hygiene-5); SynchronizeStatuses(); Publish(true); return true;
}
void UProjectInnerStateComponent::ApplyBenefit(FName Family,double Rate,double Hours)
{
    FProjectInnerStateInfluence Effect; Effect.Source=Family; Effect.Family=Family;
    Effect.Remaining=Hours; Effect.WellbeingPerHour=Rate; SetInfluence(Effect);
}
void UProjectInnerStateComponent::NotifySocialContact(bool bMeaningful)
{
    if (!bMeaningful) return;
    State.HoursWithoutSocialContact=0;
    if (State.CalendarHours>=State.NextSocialRewardHour)
    { ApplyBenefit(TEXT("Social"),GetDefault<UProjectInnerStateSettings>()->SocialWellbeingPerHour,12); State.NextSocialRewardHour=State.CalendarHours+24; }
}
void UProjectInnerStateComponent::NotifySleepCompleted(double SleptHours,bool bRestorative)
{
    if (!FMath::IsFinite(SleptHours) || SleptHours<4 || !bRestorative) return;
    if (auto* Doctrine=GetOwner()->FindComponentByClass<UProjectInnerDoctrineComponent>()) Doctrine->NotifySleepCompleted(TEXT("InnerState.Rest"));
    if (State.CalendarHours>=State.NextRestRewardHour)
    { ApplyBenefit(TEXT("Rested"),GetDefault<UProjectInnerStateSettings>()->RestWellbeingPerHour,8); State.NextRestRewardHour=State.CalendarHours+16; }
}
void UProjectInnerStateComponent::SetSleeping(bool Sleeping)
{
    if (bSleeping && !Sleeping) NotifySleepCompleted(SleepingHours,true);
    if (!bSleeping && Sleeping) SleepingHours=0;
    bSleeping=Sleeping;
}
void UProjectInnerStateComponent::NotifyAwarenessImpact(double Points)
{
    if (!FMath::IsFinite(Points) || Points<=0) return;
    State.Awareness=Percent(State.Awareness-Points); State.HitRecoverySeconds=GetDefault<UProjectInnerStateSettings>()->HitRecoveryDelaySeconds;
    Publish(true);
}
void UProjectInnerStateComponent::HandleDamageReceived(const FACFDamageEvent& Event)
{
    if (bApplyingPhysiologicalDamage || Event.FinalDamage<=0) return;
    RecentThreatSeconds=20;
    if (const auto* Type = Event.DamageClass ? Cast<UProjectConcussiveDamageType>(Event.DamageClass->GetDefaultObject()) : nullptr)
    {
        const double Head = Event.hitResult.BoneName.ToString().Contains(TEXT("head"), ESearchCase::IgnoreCase) ? Type->HeadMultiplier : 1.0;
        if (ReadHealthMax() > 0) NotifyAwarenessImpact(100.0 * Event.FinalDamage / ReadHealthMax() * Type->AwarenessPerHealthPercent * Head);
    }
}
void UProjectInnerStateComponent::SetInfluence(const FProjectInnerStateInfluence& Influence)
{
    if (Influence.Source.IsNone() || !FMath::IsFinite(Influence.Remaining) || Influence.Remaining<=0
        || !FMath::IsFinite(Influence.WellbeingPerHour) || !FMath::IsFinite(Influence.StressTarget)
        || !FMath::IsFinite(Influence.AwarenessCapacityLoss) || !FMath::IsFinite(Influence.AwarenessLossPerSecond)) return;
    // One source per family: refresh, never multiply a repeatable benefit.
    State.Influences.RemoveAll([&](const FProjectInnerStateInfluence& E) { return E.Source==Influence.Source || (!Influence.Family.IsNone() && E.Family==Influence.Family); });
    State.Influences.Add(Influence);
}
void UProjectInnerStateComponent::RemoveInfluence(FName Source) { State.Influences.RemoveAll([Source](const FProjectInnerStateInfluence& E){return E.Source==Source;}); }
void UProjectInnerStateComponent::SetBreathingSource(FName Source,double Availability) { if (!Source.IsNone()) BreathingSources.Add(Source,Unit(Availability)); }
void UProjectInnerStateComponent::ClearBreathingSource(FName Source) { BreathingSources.Remove(Source); }
void UProjectInnerStateComponent::SetContaminationSource(FName Source,double Intensity) { if (!Source.IsNone()) ContaminationSources.Add(Source,Unit(Intensity)); }
void UProjectInnerStateComponent::ClearContaminationSource(FName Source) { ContaminationSources.Remove(Source); }
void UProjectInnerStateComponent::SetComposureContext(FName Source,double Target) { if (!Source.IsNone() && FMath::IsFinite(Target)) ComposureSources.Add(Source,FMath::Clamp(Target,-100.0,100.0)); }
void UProjectInnerStateComponent::ClearComposureContext(FName Source) { ComposureSources.Remove(Source); }
void UProjectInnerStateComponent::NotifyPractice(double Hours,bool bPhysical)
{
    if (!FMath::IsFinite(Hours) || Hours<=0) return;
    State.RepetitiveWorkHours+=Hours; PracticeThisStepHours+=Hours;
    if (bPhysical) State.ExerciseHours+=Hours;
    if (State.ExerciseHours>=.5 && State.CalendarHours>=State.NextExerciseRewardHour)
    { ApplyBenefit(TEXT("Exercise"),GetDefault<UProjectInnerStateSettings>()->ExerciseWellbeingPerHour,12); State.NextExerciseRewardHour=State.CalendarHours+24; State.ExerciseHours=0; }
}
void UProjectInnerStateComponent::OnStateLoaded()
{
    HealthDebitRoundingRemainder=0;
    ResolveDependencies();
    State.Poison.Sanitize();
    // Sanitize persisted clocks without resetting valid progress or incubation.
    double* Clocks[]={&State.CalendarHours,&State.HoursWithoutSocialContact,&State.RepetitiveWorkHours,&State.ExerciseHours,
        &State.NextExerciseRewardHour,&State.NextRestRewardHour,&State.NextSocialRewardHour,&State.DepressedHours,
        &State.RecoveringMoodHours,&State.SevereHygieneHours,&State.TreatmentHours,&State.ExtraDirtBudget,
        &State.ExtremePainSeconds,&State.HitRecoverySeconds,&State.UnconsciousSeconds,&State.StepRemainderSeconds,&State.SlowRemainderSeconds};
    for (double* Clock:Clocks) *Clock=Positive(*Clock);
    State.Influences.RemoveAll([](const FProjectInnerStateInfluence& E) { return E.Source.IsNone() || !FMath::IsFinite(E.Remaining) || E.Remaining<=0
        || !FMath::IsFinite(E.WellbeingPerHour) || !FMath::IsFinite(E.StressTarget) || !FMath::IsFinite(E.AwarenessCapacityLoss) || !FMath::IsFinite(E.AwarenessLossPerSecond); });
    State.Stress=Percent(State.Stress); State.Hygiene=Percent(State.Hygiene); State.Awareness=Percent(State.Awareness);
    State.Air=Percent(State.Air); State.Sickness=Percent(State.Sickness); State.BathroomHours=Positive(State.BathroomHours);
    State.Stimulation=FMath::IsFinite(State.Stimulation)?FMath::Clamp(State.Stimulation,-100.0,100.0):0;
    State.Composure=FMath::IsFinite(State.Composure)?FMath::Clamp(State.Composure,-100.0,100.0):0;
    PublishedValues.Reset(); PublishedStatuses.Reset();
    SynchronizeStatuses();
    if (Defeat) Defeat->SetPhysiologicalUnconscious(State.bUnconscious);
    Publish(true);
}
bool UProjectInnerStateComponent::SetDebugValue(FName Entry,double Value)
{
    if (!FMath::IsFinite(Value)) return false;
    if (Entry==TEXT("Stress")) State.Stress=Percent(Value);
    else if (Entry==TEXT("Stimulation")) State.Stimulation=FMath::Clamp(Value,-100.0,100.0);
    else if (Entry==TEXT("Hygiene")) State.Hygiene=Percent(Value);
    else if (Entry==TEXT("Bathroom")) State.BathroomHours=Percent(Value)*GetDefault<UProjectInnerStateSettings>()->BathroomDeadlineHours/100;
    else if (Entry==TEXT("Awareness")) State.Awareness=Percent(Value);
    else if (Entry==TEXT("Air")) State.Air=Percent(Value);
    else if (Entry==TEXT("Composure")) State.Composure=FMath::Clamp(Value,-100.0,100.0);
    else if (Entry==TEXT("Sickness")) { State.Sickness=Percent(Value); State.bCriticalSickness=Value>=100; if (Value>0) State.DiscoveredAfflictions.Add(Entry); }
    else if (Entry==TEXT("Sensation") && Needs) Needs->SetSensationCurrentValue(Entry,static_cast<float>(FMath::Clamp(Value,-100.0,100.0) * Needs->GetSensationMaxValue(TEXT("Pain"))/100.0));
    else return false;
    SynchronizeStatuses(); Publish(true); return true;
}

void UProjectInnerStateComponent::HandleStatusChanged(FName StatusName, bool bActive)
{
    if (bSynchronizingStatuses || !bActive) return;
    if (StatusName == TEXT("Sick")) ContractSickness(GetDefault<UProjectInnerStateSettings>()->InitialSickness);
    else if (StatusName == TEXT("Cursed")) State.DiscoveredAfflictions.Add(TEXT("Curse"));
    else if (StatusName == TEXT("Dizzy")) NotifyAwarenessImpact(10);
}
void UProjectInnerStateComponent::UpdateGameplayModifiers()
{
    if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(GetDefault<UACFStatisticsSet>()->StaminaRegenAttribute())) return;
    const int32 PercentValue = FMath::RoundToInt(GetStaminaRecoveryMultiplier()*100);
    if (PercentValue == AppliedStaminaPercent && StaminaEffect.IsValid()) return;
    AppliedStaminaPercent = PercentValue;
    if (StaminaEffect.IsValid()) AbilitySystem->RemoveActiveGameplayEffect(StaminaEffect);
    auto Spec = AbilitySystem->MakeOutgoingSpec(UProjectInnerStateStaminaEffect::StaticClass(),1,AbilitySystem->MakeEffectContext());
    if (Spec.IsValid())
    {
        Spec.Data->SetSetByCallerMagnitude(FName(TEXT("InnerState.StaminaRecovery")), PercentValue/100.f);
        StaminaEffect = AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    }
}

float UProjectInnerStateComponent::GetMadnessGainMultiplier() const
{
    const auto& S = *GetDefault<UProjectInnerStateSettings>();
    if (State.bDepressed) return static_cast<float>(S.DepressionMadnessMultiplier);
    return static_cast<float>(FMath::Lerp(1.0, S.ContentMadnessMultiplier, Math::SeverityAbove(State.Stimulation,40)));
}

bool UProjectInnerStateComponent::RequestCareAction(FName Action)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || State.bUnconscious || State.bFatalAsphyxia || RecentThreatSeconds>0
        || SampledThreat<0 || (Defeat && Defeat->GetCurrentPhase()!=EProjectDefeatPhase::None)) return false;
    if (Action==TEXT("Bathroom")) return RelieveBathroom(false);
    if (Action==TEXT("Wash"))
    {
        for (const auto& Source:ContaminationSources) if (Source.Value>0) return false;
        TArray<FDirtyPawnHygieneRegion> Surfaces;
        if (auto* Dirty=UDirtyPawnComponent::FindCanonicalDirtyPawnComponent(GetOwner())) Dirty->GetHygieneSurfaceSamples(Surfaces);
        // Water already washes continuously. Repeating this action cannot grant instant hygiene.
        return Surfaces.ContainsByPredicate([](const FDirtyPawnHygieneRegion& R){ return R.CleanWater>0; });
    }
    if (Action==TEXT("Rest"))
    {
        if (State.Poison.GetPendingPoints()>0 || State.Sickness>=80 || State.Air<100 || ReadNeedPercent(TEXT("Sleep"))>=99) return false;
        auto* Actions=GetWorld()->GetSubsystem<UProjectEmoteSubsystem>();
        FProjectEmoteRuntimeActionRequest Request;
        Request.RuntimeActionId=TEXT("InnerState.Rest"); Request.InteractionId=TEXT("Actions.Care.Rest");
        Request.Source=EProjectEmoteRuntimeActionSource::Menu;
        if (!Actions || !Actions->StartRuntimeAction(Request)) return false;
        SetSleeping(true); return true;
    }
    return false;
}
