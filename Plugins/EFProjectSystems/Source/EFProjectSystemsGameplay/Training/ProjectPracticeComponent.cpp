#include "Training/ProjectPracticeComponent.h"
#include "Progression/ProjectPracticeSettings.h"
#include "Progression/ProjectAttributeSynergies.h"
#include "Progression/ProjectPracticeProgressionComponent.h"
#include "DayCycle/ProjectDayCycleSettings.h"
#include "DayCycle/ProjectDayCycleSubsystem.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Lockpicking/ProjectLockpickableComponent.h"
#include "Locomotion/ProjectEmoteComponent.h"
#include "EngineUtils.h"
#include "Animation/AnimationAsset.h"
#include "Kismet/GameplayStatics.h"

UProjectPracticeComponent::UProjectPracticeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    SetIsReplicatedByDefault(false); // Project adapter is deliberately single-player.
}

void UProjectPracticeComponent::BeginPlay()
{
    Super::BeginPlay();
    Progression = UProjectPracticeProgressionComponent::Ensure(GetOwner());
    if (Progression && InitialStrength < 0.f) InitialStrength = Progression->GetLearnedRank(TEXT("Strength"));
    TrainingDefinitionOverrides.Reset();
    for (const auto& D : GetDefault<UProjectPracticeSettings>()->Activities)
    {
        auto& Entry = TrainingDefinitionOverrides.AddDefaulted_GetRef();
        Entry.TrainingId = D.Id;
        Entry.DisplayName = FText::Format(NSLOCTEXT("ProjectPractice", "ExerciseAttribute", "{0} · {1}"), D.Name, FText::FromName(D.Attribute));
        Entry.Description = FText::Format(NSLOCTEXT("ProjectPractice", "MenuDescription", "{0}. WASD + mouse: camera | Q/E: height | O: camera toggle | Y/Esc: stop."), D.Description);
        Entry.TargetPrimaryAttribute = FGameplayTag::RequestGameplayTag(FName(*(TEXT("RPG.PrimaryAttributes.") + D.Attribute.ToString())), false);
        Entry.TrainingAnimation = TSoftObjectPtr<UAnimationAsset>(D.Animation);
        Entry.SuccessReward = 0.0f; // No completion bonus; all progress is integrated from actual practice.
    }
}

void UProjectPracticeComponent::EndPlay(EEndPlayReason::Type Reason)
{
    CancelTrainingAuthority();
    Super::EndPlay(Reason);
}

bool UProjectPracticeComponent::NeedsAllowPractice() const
{
    if (const auto* Needs = GetOwner()->FindComponentByClass<UProjectSurvivalNeedsComponent>())
        for (FName Name : {FName(TEXT("Hunger")), FName(TEXT("Thirst")), FName(TEXT("Sleep"))})
            if (Needs->HasNeed(Name) && Needs->GetNeedNormalizedValue(Name) <= 0.15f) return false;
    return true;
}

UProjectLockpickableComponent* UProjectPracticeComponent::FindNearbyLock() const
{
    UProjectLockpickableComponent* Closest = nullptr;
    double Distance = FMath::Square(300.0);
    if (!GetWorld() || !GetOwner()) return nullptr;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        if (auto* Lock = It->FindComponentByClass<UProjectLockpickableComponent>())
        {
            const double Candidate = FVector::DistSquared(It->GetActorLocation(), GetOwner()->GetActorLocation());
            if (Candidate <= Distance && !Lock->IsSessionActive()) { Closest = Lock; Distance = Candidate; }
        }
    return Closest;
}

bool UProjectPracticeComponent::CanStartTraining(FName Id, FText& Reason) const
{
    const auto* D = GetDefault<UProjectPracticeSettings>()->Find(Id);
    if (!D || !Progression || IsTrainingActive() || !GetOwner() || !GetOwner()->HasAuthority() || UGameplayStatics::IsGamePaused(this))
        Reason = FText::FromString(TEXT("Practice is not available right now."));
    else if (!NeedsAllowPractice()) Reason = FText::FromString(TEXT("Eat, drink or sleep before practising."));
    else if (D->Method == EProjectPracticeMethod::Lock && !FindNearbyLock())
        Reason = FText::FromString(TEXT("Stand near a practice chest or lock."));
    else { Reason = FText::GetEmpty(); return true; }
    return false;
}

bool UProjectPracticeComponent::ConfigureBook(FName BookId, double RequiredCalendarHours)
{
    if (IsTrainingActive() || BookId.IsNone() || !FMath::IsFinite(RequiredCalendarHours) || RequiredCalendarHours <= 0.0 || RequiredCalendarHours > 48.0) return false;
    SelectedBook = BookId;
    BookDuration = RequiredCalendarHours;
    return true;
}

bool UProjectPracticeComponent::StartTrainingAuthority(FName Id)
{
    FText Reason;
    if (!CanStartTraining(Id, Reason)) { OnTrainingRejected.Broadcast(Id, Reason); return false; }
    const auto* D = GetDefault<UProjectPracticeSettings>()->Find(Id);
    SessionDuration = D->DurationHours;
    if (D->Method == EProjectPracticeMethod::Physical)
    {
        const double PhysicalLimit = FMath::Clamp(3.0 + 6.0 * FMath::Max(0.f, Progression->GetLearnedRank(TEXT("Strength")) - InitialStrength)
            / FMath::Max(1.f, GetDefault<UProjectPracticeSettings>()->MaximumAttributeRank - InitialStrength), 3.0, 9.0);
        // Each exercise has its own baseline duration; capacity sets the ceiling.
        SessionDuration = D->DurationHours > 0.0 ? FMath::Min(PhysicalLimit, D->DurationHours * PhysicalLimit / 3.0) : PhysicalLimit;
    }
    if (D->Method == EProjectPracticeMethod::Book)
    {
        double& Read = BookHours.FindOrAdd(SelectedBook);
        if (Read >= BookDuration) Read = 0.0; // Rereading retains attribute exposure, never resets efficiency.
        SessionDuration = BookDuration - Read;
    }
    if (D->Method == EProjectPracticeMethod::Lock)
    {
        PracticeLock = FindNearbyLock();
        if (!PracticeLock.IsValid()) return false;
        SessionDuration = (0.25 + 2.75 * FMath::Clamp(PracticeLock->Difficulty, 1, 100) / 100.0)
            / (1.0 + 0.01 * (FProjectAttributeSynergies::EffectiveRank(GetOwner(), TEXT("Cunning"))
                + FProjectAttributeSynergies::IntelligenceLockpickBonus(GetOwner())));
    }
    ActiveTrainingId = Id;
    ActiveTrainingStartTime = GetWorld()->GetTimeSeconds();
    PracticedAttribute = D->Attribute;
    Progression->SetPracticingAttribute(PracticedAttribute);
    SessionHours = SessionPractice = 0.0;
    const auto* Native = FindTrainingDefinition(Id);
    OnTrainingStarted.Broadcast(Id, Native ? Native->TargetPrimaryAttribute : FGameplayTag());
    return true;
}

void UProjectPracticeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    if (!IsTrainingActive() || UGameplayStatics::IsGamePaused(this)) return;
    const auto* D = GetDefault<UProjectPracticeSettings>()->Find(ActiveTrainingId);
    if (!D || !Progression || !NeedsAllowPractice()) { Finish(EACFTrainingSessionResult::Cancelled); return; }
    if (const auto* Emote = GetOwner()->FindComponentByClass<UProjectEmoteComponent>(); Emote && !Emote->IsEmoteActive() && !Emote->IsEmoteTransitionPending())
    { Finish(EACFTrainingSessionResult::Cancelled); return; }
    if (D->Method == EProjectPracticeMethod::Lock && (!PracticeLock.IsValid() || PracticeLock->IsSessionActive() ||
        FVector::DistSquared(GetOwner()->GetActorLocation(), PracticeLock->GetOwner()->GetActorLocation()) > FMath::Square(300.0)))
    { Finish(EACFTrainingSessionResult::Cancelled); return; }
    double Hours = UProjectDayCycleSubsystem::SimulationSecondsToCalendarHours(DeltaTime, UProjectDayCycleSettings::Get()->DayLengthSeconds);
    if (SessionDuration > 0.0) Hours = FMath::Min(Hours, FMath::Max(0.0, SessionDuration - SessionHours));
    SessionPractice += Progression->Practice(PracticedAttribute, Hours, D->PracticePerHour);
    SessionHours += Hours;
    if (D->Method == EProjectPracticeMethod::Book) BookHours.FindOrAdd(SelectedBook) += Hours;
    if (SessionDuration > 0.0 && SessionHours >= SessionDuration - 1.e-9) Finish(EACFTrainingSessionResult::Succeeded);
}

void UProjectPracticeComponent::CompleteTrainingAuthority(bool bSuccess)
{
    // External minigame/Blueprint callbacks cannot mint a completion reward or skip elapsed time.
    if (!bSuccess && !UGameplayStatics::IsGamePaused(this)) CancelTrainingAuthority();
}

void UProjectPracticeComponent::CancelTrainingAuthority() { Finish(EACFTrainingSessionResult::Cancelled); }

void UProjectPracticeComponent::Finish(EACFTrainingSessionResult Result)
{
    if (!IsTrainingActive()) return;
    const FName Id = ActiveTrainingId;
    const auto* D = FindTrainingDefinition(Id);
    const FGameplayTag Tag = D ? D->TargetPrimaryAttribute : FGameplayTag();
    ActiveTrainingId = NAME_None;
    ClearTimingMinigameSession();
    if (Progression) Progression->SetPracticingAttribute(NAME_None);
    PracticeLock.Reset();
    OnTrainingCompleted.Broadcast(Id, Result, Tag, static_cast<float>(SessionPractice));
    if (auto* Emote = GetOwner()->FindComponentByClass<UProjectEmoteComponent>();
        Emote && (Emote->IsEmoteActive() || Emote->IsEmoteTransitionPending())) Emote->StopEmote();
}

double UProjectPracticeComponent::GetActivityProgress() const
{
    if (ActiveTrainingId == TEXT("ReadBook")) return BookHours.FindRef(SelectedBook) / FMath::Max(0.01, BookDuration);
    return SessionDuration > 0.0 ? FMath::Clamp(SessionHours / SessionDuration, 0.0, 1.0) : 0.0;
}

FText UProjectPracticeComponent::GetPracticeDescription() const
{
    const bool bBook = ActiveTrainingId == TEXT("ReadBook");
    const double Elapsed = bBook ? BookHours.FindRef(SelectedBook) : SessionHours;
    const double Duration = bBook ? BookDuration : SessionDuration;
    const FString Time = Duration > 0.0
        ? FString::Printf(TEXT("%.2f / %.1f h"), Elapsed, Duration)
        : FString::Printf(TEXT("%.2f h"), Elapsed);
    return FText::FromString(FString::Printf(TEXT("%s | %s\n%.2f%% learning | %.1fh rest"),
        *PracticedAttribute.ToString(), *Time,
        Progression ? Progression->GetPracticeEfficiency(PracticedAttribute) * 100.0 : 0.0,
        Progression ? Progression->GetRestHours(PracticedAttribute) : 0.0));
}
