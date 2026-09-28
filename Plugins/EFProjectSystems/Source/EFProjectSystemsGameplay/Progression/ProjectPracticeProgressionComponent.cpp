#include "Progression/ProjectPracticeProgressionComponent.h"
#include "Progression/ProjectPracticeSettings.h"
#include "Progression/ProjectPracticeMath.h"
#include "Progression/ProjectProgressionBridge.h"
#include "Progression/ProjectDoctrineAttributeSet.h"
#include "Progression/ProjectProgressionState.h"
#include "ACFGASStatisticsComponent.h"
#include "ACFSkillTreeComponent.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "GameFramework/Pawn.h"
#include "Training/ProjectPracticeComponent.h"
#include "DayCycle/ProjectDayCycleSubsystem.h"
#include "DayCycle/ProjectDayCycleSettings.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Async/TaskGraphInterfaces.h"

UProjectPracticeProgressionComponent::UProjectPracticeProgressionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

UProjectPracticeProgressionComponent* UProjectPracticeProgressionComponent::Ensure(AActor* Owner)
{
    if (!Owner || !Owner->HasAuthority() || !GetDefault<UProjectPracticeSettings>()->bEnabled) return nullptr;
    const APawn* Pawn = Cast<APawn>(Owner);
    if (!Pawn || !Pawn->IsPlayerControlled()) return nullptr;
    if (auto* Existing = Owner->FindComponentByClass<UProjectPracticeProgressionComponent>()) return Existing;
    auto* Component = NewObject<UProjectPracticeProgressionComponent>(Owner, TEXT("ProjectPracticeProgression"));
    Owner->AddInstanceComponent(Component);
    Component->RegisterComponent();
    Component->RefreshBindings();
    return Component;
}

void UProjectPracticeProgressionComponent::BeginPlay()
{
    Super::BeginPlay();
    RefreshBindings();
}

void UProjectPracticeProgressionComponent::RefreshBindings()
{
    if (!GetOwner()) return;
    Statistics = GetOwner()->FindComponentByClass<UACFGASStatisticsComponent>();
    AbilitySystem = GetOwner()->FindComponentByClass<UAbilitySystemComponent>();
    if (AbilitySystem && !AbilitySystem->GetSet<UProjectDoctrineAttributeSet>())
        AbilitySystem->AddAttributeSetSubobject(NewObject<UProjectDoctrineAttributeSet>(GetOwner()));
    if (Statistics)
    {
        Statistics->OnCharacterLevelUp.AddUniqueDynamic(this, &ThisClass::HandleLevelUp);
        Statistics->OnCurrentExpValueChanged.AddUniqueDynamic(this, &ThisClass::HandleExperienceChanged);
        // Native perks are consumed at their source event, never synchronized with another balance.
        Statistics->SetAvailablePerks(0);
    }
    if (auto* Damage = GetOwner()->FindComponentByClass<UACFDamageHandlerComponent>())
    {
        Damage->OnDamageInflicted.AddUniqueDynamic(this, &ThisClass::HandleDamageInflicted);
        Damage->OnDamageReceived.AddUniqueDynamic(this, &ThisClass::HandleDamageReceived);
    }
}

void UProjectPracticeProgressionComponent::EndPlay(EEndPlayReason::Type Reason)
{
	if (GetWorld()) if (auto* State = GetWorld()->GetSubsystem<UProjectProgressionWorldSubsystem>())
		if (State->State) State->State->Capture(GetOwner());
    if (Statistics) Statistics->OnCharacterLevelUp.RemoveDynamic(this, &ThisClass::HandleLevelUp);
    if (Statistics) Statistics->OnCurrentExpValueChanged.RemoveDynamic(this, &ThisClass::HandleExperienceChanged);
    if (auto* Damage = GetOwner()->FindComponentByClass<UACFDamageHandlerComponent>())
    {
        Damage->OnDamageInflicted.RemoveDynamic(this, &ThisClass::HandleDamageInflicted);
        Damage->OnDamageReceived.RemoveDynamic(this, &ThisClass::HandleDamageReceived);
    }
    Super::EndPlay(Reason);
}

void UProjectPracticeProgressionComponent::HandleLevelUp(int32 Level)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Statistics) return;
    Statistics->SetAvailablePerks(0);
    if (Level <= RewardedThroughLevel) return;
    if (auto* Skills = GetOwner()->FindComponentByClass<UACFSkillTreeComponent>())
    {
        Skills->AddSkillPoints((Level - RewardedThroughLevel) * GetDefault<UProjectPracticeSettings>()->SkillPointsPerLevel);
        RewardedThroughLevel = Level;
    }
}

void UProjectPracticeProgressionComponent::OnComponentLoaded()
{
    if (!IsInGameThread())
    {
        const TWeakObjectPtr<UProjectPracticeProgressionComponent> WeakThis(this);
        const auto Task = FFunctionGraphTask::CreateAndDispatchWhenReady([WeakThis]()
        {
            if (WeakThis.IsValid()) WeakThis->OnComponentLoaded();
        }, TStatId(), nullptr, ENamedThreads::GameThread);
        FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
        return;
    }
    RefreshBindings();
    PracticingAttribute = NAME_None;
    ExperienceFraction = FMath::IsFinite(ExperienceFraction) ? FMath::Clamp(ExperienceFraction, 0.0, 0.999999999999) : 0.0;
    for (auto& State : PracticeState)
    {
        if (!FMath::IsFinite(State.Progress) || State.Progress < 0.0) State.Progress = 0.0;
        if (!FMath::IsFinite(State.ExposureHours) || State.ExposureHours < 0.0) State.ExposureHours = 0.0;
    }
}

FGameplayAttribute UProjectPracticeProgressionComponent::ResolveAttribute(FName Name)
{
    for (const auto& Attribute : FProjectProgressionBridge::GetPurchasableAttributes())
        if (Attribute.GetName() == Name.ToString()) return Attribute;
    return FGameplayAttribute();
}

float UProjectPracticeProgressionComponent::GetLearnedRank(FName Name) const
{
    const auto Attribute = ResolveAttribute(Name);
    return AbilitySystem && Attribute.IsValid() && AbilitySystem->HasAttributeSetForAttribute(Attribute)
        ? AbilitySystem->GetNumericAttributeBase(Attribute) : 0.f;
}

double UProjectPracticeProgressionComponent::GetPracticeProgress(FName Attribute) const
{
    for (const auto& State : PracticeState) if (State.Attribute == Attribute) return State.Progress;
    return 0.0;
}

double UProjectPracticeProgressionComponent::GetPracticeEfficiency(FName Attribute) const
{
    for (const auto& State : PracticeState) if (State.Attribute == Attribute)
        return ProjectPracticeMath::Efficiency(State.ExposureHours, GetDefault<UProjectPracticeSettings>()->ExposureHalfLifeHours);
    return 1.0;
}

float UProjectPracticeProgressionComponent::GetRankProgressNormalized(FName Attribute) const
{
    const auto* S = GetDefault<UProjectPracticeSettings>();
    const int32 Rank = FMath::FloorToInt(GetLearnedRank(Attribute));
    if (Rank >= S->MaximumAttributeRank) return 1.f;
    const double Cost = FMath::Max(1.0, S->PracticeForFirstRank * (1.0 + S->RankCostGrowth * Rank));
    return static_cast<float>(FMath::Clamp(GetPracticeProgress(Attribute) / Cost, 0.0, 1.0));
}

double UProjectPracticeProgressionComponent::GetRestHours(FName Attribute) const
{
    const auto* S = GetDefault<UProjectPracticeSettings>();
    for (const auto& State : PracticeState) if (State.Attribute == Attribute)
        return ProjectPracticeMath::HoursToRecover(State.ExposureHours, S->RecoveryPerCalendarHour, S->RecoveryDebtScale);
    return 0.0;
}

double UProjectPracticeProgressionComponent::Practice(FName Name, double Hours, double Rate, bool bGrantGlobalExperience)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || UGameplayStatics::IsGamePaused(this) ||
        !FMath::IsFinite(Hours) || Hours <= 0.0 || !FMath::IsFinite(Rate) || Rate <= 0.0) return 0.0;
    const auto Attribute = ResolveAttribute(Name);
    if (!AbilitySystem || !Attribute.IsValid() || !AbilitySystem->HasAttributeSetForAttribute(Attribute)) return 0.0;
    const auto* S = GetDefault<UProjectPracticeSettings>();
    auto* State = PracticeState.FindByPredicate([Name](const auto& V) { return V.Attribute == Name; });
    if (!State) { State = &PracticeState.AddDefaulted_GetRef(); State->Attribute = Name; }
    double Affinity = 1.0;
    if (const auto* Doctrine = GetOwner()->FindComponentByClass<UProjectInnerDoctrineComponent>())
        for (int32 I=0; I<static_cast<int32>(EProjectDoctrineAttribute::Count); ++I)
            if (StaticEnum<EProjectDoctrineAttribute>()->GetNameStringByValue(I) == Name.ToString())
                Affinity = Doctrine->GetDxpGainMultiplierForAttribute(static_cast<EProjectDoctrineAttribute>(I));
    const double Gain = Rate * FMath::Max(0.01, Affinity) * ProjectPracticeMath::EffectiveHours(State->ExposureHours, Hours, S->ExposureHalfLifeHours);
    State->ExposureHours += Hours;
    RecentPracticeAttribute = Name;
    RecentPracticeUntil = GetWorld()->GetTimeSeconds() + 3.0;
    State->Progress += Gain;
    const int32 OldRank = FMath::FloorToInt(GetLearnedRank(Name));
    int32 Rank = OldRank;
    while (Rank < S->MaximumAttributeRank)
    {
        const double Cost = FMath::Max(1.0, S->PracticeForFirstRank * (1.0 + S->RankCostGrowth * Rank));
        if (State->Progress < Cost) break;
        State->Progress -= Cost;
        ++Rank;
    }
    if (Rank != OldRank)
    {
        AbilitySystem->SetNumericAttributeBase(Attribute, GetLearnedRank(Name) + Rank - OldRank);
        NotifyRankChanged(Name, OldRank, Rank);
    }
    if (Rank >= S->MaximumAttributeRank) State->Progress = 0.0;
    if (bGrantGlobalExperience) GrantExperience(Gain * S->ExperiencePerPractice);
    return Gain;
}

bool UProjectPracticeProgressionComponent::AddLearnedRanks(FName Name, int32 Ranks)
{
    const auto Attribute = ResolveAttribute(Name);
    if (!GetOwner() || !GetOwner()->HasAuthority() || !AbilitySystem || !Attribute.IsValid() ||
        !AbilitySystem->HasAttributeSetForAttribute(Attribute) || Ranks <= 0) return false;
    const int32 OldRank = FMath::FloorToInt(GetLearnedRank(Name));
    const int32 NewRank = FMath::Min(OldRank + Ranks, GetDefault<UProjectPracticeSettings>()->MaximumAttributeRank);
    if (NewRank <= OldRank) return false;
    AbilitySystem->SetNumericAttributeBase(Attribute, GetLearnedRank(Name) + NewRank - OldRank);
    NotifyRankChanged(Name, OldRank, NewRank);
    return true;
}

void UProjectPracticeProgressionComponent::NotifyRankChanged(FName Name, int32 OldRank, int32 NewRank)
{
    if (auto* Doctrine = GetOwner()->FindComponentByClass<UProjectInnerDoctrineComponent>())
    {
        for (int32 I=0; I<static_cast<int32>(EProjectDoctrineAttribute::Count); ++I)
        {
            const auto Kind = static_cast<EProjectDoctrineAttribute>(I);
            if (StaticEnum<EProjectDoctrineAttribute>()->GetNameStringByValue(I) == Name.ToString())
                Doctrine->NotifyLearnedAttributeChanged(Kind, OldRank, NewRank);
        }
    }
    if (Statistics) Statistics->OnAttributeSetModified.Broadcast();
}

bool UProjectPracticeProgressionComponent::AddBackgroundRanks(FName Name, int32 Ranks)
{
    const int32 OldRank = FMath::FloorToInt(GetLearnedRank(Name));
    if (!AddLearnedRanks(Name, Ranks)) return false;
    BackgroundRanks.FindOrAdd(Name) += FMath::FloorToInt(GetLearnedRank(Name)) - OldRank;
    return true;
}

void UProjectPracticeProgressionComponent::ResetBackgroundRanks()
{
    if (!AbilitySystem) return;
    for (const auto& Rank : BackgroundRanks)
    {
        const auto Attribute = ResolveAttribute(Rank.Key);
        if (!Attribute.IsValid() || !AbilitySystem->HasAttributeSetForAttribute(Attribute)) continue;
        const int32 OldRank = FMath::FloorToInt(GetLearnedRank(Rank.Key));
        const int32 NewRank = FMath::Max(0, OldRank - Rank.Value);
        AbilitySystem->SetNumericAttributeBase(Attribute, NewRank);
        NotifyRankChanged(Rank.Key, OldRank, NewRank);
    }
    BackgroundRanks.Reset();
}

void UProjectPracticeProgressionComponent::HandleExperienceChanged(int32 CurrentExperience, int32 Delta)
{
    if (auto* Doctrine = GetOwner()->FindComponentByClass<UProjectInnerDoctrineComponent>())
        Doctrine->OnDxpChanged.Broadcast(FMath::Max(0, CurrentExperience-Delta), CurrentExperience,
            Doctrine->GetMetaBankDxp(), Doctrine->GetMetaBankDxp());
}

bool UProjectPracticeProgressionComponent::GrantExperience(double Amount)
{
    if (!FMath::IsFinite(Amount) || Amount <= 0.0 || Amount > MAX_int32 || !Statistics) return false;
    const double Total = ExperienceFraction + Amount;
    if (Total > MAX_int32) return false;
    const int32 Whole = FMath::FloorToInt(Total);
    if (Whole && !FProjectProgressionBridge::GrantExperience(Statistics, Whole, 255)) return false;
    ExperienceFraction = Total - Whole;
    return true;
}

void UProjectPracticeProgressionComponent::HandleDamageInflicted(const FACFDamageEvent& Event)
{
    if (Event.FinalDamage <= 0.f || !IsValid(Event.DamageReceiver) || Event.DamageReceiver == GetOwner()) return;
    const double Now = GetWorld()->GetTimeSeconds();
    // One practice credit per receiver per frame, even when damage feedback broadcasts twice.
    if (LastHitTime != Now) { HitTargetsThisFrame.Reset(); LastHitTime = Now; }
    if (HitTargetsThisFrame.Contains(Event.DamageReceiver)) return;
    HitTargetsThisFrame.Add(Event.DamageReceiver);
    Practice(TEXT("Offensive"), 0.02, 25.0, false); // Kill XP remains exclusively native ACF.
}

void UProjectPracticeProgressionComponent::HandleDamageReceived(const FACFDamageEvent& Event)
{
    if (Event.FinalDamage <= 0.f) return;
    if (auto* Training = GetOwner()->FindComponentByClass<UProjectPracticeComponent>()) Training->CancelTraining();
}

void UProjectPracticeProgressionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    if (!GetOwner() || !GetOwner()->HasAuthority() || UGameplayStatics::IsGamePaused(this)) return;
    const auto* S = GetDefault<UProjectPracticeSettings>();
    const double Hours = UProjectDayCycleSubsystem::SimulationSecondsToCalendarHours(DeltaTime, UProjectDayCycleSettings::Get()->DayLengthSeconds);
    for (auto& State : PracticeState) if (State.Attribute != PracticingAttribute &&
        (State.Attribute != RecentPracticeAttribute || GetWorld()->GetTimeSeconds() > RecentPracticeUntil))
        State.ExposureHours = ProjectPracticeMath::Recover(State.ExposureHours, Hours, S->RecoveryPerCalendarHour, S->RecoveryDebtScale);
}
