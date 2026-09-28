#include "Progression/ProjectProgressionState.h"
#include "Progression/ProjectPracticeSettings.h"
#include "Progression/ProjectPracticeProgressionComponent.h"
#include "Progression/ProjectProgressionBridge.h"
#include "Progression/ProjectDoctrineAttributeSet.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "EngineUtils.h"
#include "Training/ProjectPracticeComponent.h"
#include "ACFGASStatisticsComponent.h"
#include "ACFSkillTreeComponent.h"
#include "Graph/ACFBaseSkillNode.h"
#include "Graph/ACFSkillTreeGraph.h"
#include "ALSLoadAndSaveSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/UnrealType.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Async/TaskGraphInterfaces.h"

namespace
{
    struct FProgressionArchive : FObjectAndNameAsStringProxyArchive
    {
        bool bSkipVitals;
        FProgressionArchive(FArchive& Inner, bool bLoad, bool bInSkipVitals)
            : FObjectAndNameAsStringProxyArchive(Inner, bLoad), bSkipVitals(bInSkipVitals) { ArIsSaveGame = true; }
        virtual bool ShouldSkipProperty(const FProperty* Property) const override
        {
            return (bSkipVitals && Property && Property->GetFName() == TEXT("SaveableAttributes"))
                || FObjectAndNameAsStringProxyArchive::ShouldSkipProperty(Property);
        }
    };
    void WriteSnapshot(UObject* Object, TArray<uint8>& Data)
    {
        Data.Reset(); FMemoryWriter Writer(Data); FProgressionArchive Archive(Writer, false, Object->IsA<UACFGASStatisticsComponent>());
        Object->Serialize(Archive);
    }
    void ReadSnapshot(UObject* Object, const TArray<uint8>& Data)
    {
        if (Data.IsEmpty()) return;
        FMemoryReader Reader(Data); FProgressionArchive Archive(Reader, true, Object->IsA<UACFGASStatisticsComponent>());
        Object->Serialize(Archive);
    }
}

AProjectProgressionState::AProjectProgressionState()
{
    PrimaryActorTick.bCanEverTick = true;
    // Loading metadata must complete even if the destination opens a pausing menu.
    // No simulation, learning or recovery runs on this actor.
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickInterval = 0.f;
}

void UProjectProgressionTravelState::ResetForNewCharacter()
{
    if (GetWorld())
        if (auto* WorldState = GetWorld()->GetSubsystem<UProjectProgressionWorldSubsystem>())
            if (WorldState->State) WorldState->State->DiscardForNewCharacter();
    Snapshot = FProjectProgressionSnapshot();
}

void AProjectProgressionState::DiscardForNewCharacter()
{
    bCaptureFrozen = true;
    bRestoreFailed = true; // Do not capture the departing character again during Tick/EndPlay.
    Snapshot = FProjectProgressionSnapshot();
    Publish();
}

void AProjectProgressionState::BeginPlay()
{
    Super::BeginPlay();
    if (const auto* Travel = GetGameInstance()->GetSubsystem<UProjectProgressionTravelState>()) Snapshot = Travel->Snapshot;
    UE_LOG(LogTemp, Display, TEXT("[PracticeProgression] Begin %s character=%s willpower=%.3f"), *GetWorld()->GetName(), *Snapshot.CharacterId.ToString(), Snapshot.LearnedAttributes.FindRef(TEXT("Willpower")));
}

void AProjectProgressionState::Capture(AActor* Player)
{
    if (!IsValid(Player) || bLoadPending || bRestoreFailed || bCaptureFrozen) return;
    auto* Stats = Player->FindComponentByClass<UACFGASStatisticsComponent>();
    auto* Practice = Player->FindComponentByClass<UProjectPracticeProgressionComponent>();
    if (!Stats || !Practice || !Stats->GetOwnerAbilityComponent()) return;
    for (const auto& Attribute : FProjectProgressionBridge::GetPurchasableAttributes())
        if (!Stats->GetOwnerAbilityComponent()->HasAttributeSetForAttribute(Attribute)) return;
    if (!Snapshot.CharacterId.IsValid()) Snapshot.CharacterId = FGuid::NewGuid();
    // Only native level/XP metadata travels here. Health/stamina belong to ALS and
    // the respawn flow; carrying a dead pawn's zero health would kill its replacement.
    WriteSnapshot(Stats, Snapshot.Statistics);
    WriteSnapshot(Practice, Snapshot.Practice);
    if (auto* Training = Player->FindComponentByClass<UProjectPracticeComponent>()) WriteSnapshot(Training, Snapshot.Training);
    Snapshot.LearnedAttributes.Reset();
    for (const auto& Attribute : FProjectProgressionBridge::GetPurchasableAttributes())
        if (Stats->GetOwnerAbilityComponent()->HasAttributeSetForAttribute(Attribute))
            Snapshot.LearnedAttributes.Add(FName(*Attribute.GetName()), Stats->GetOwnerAbilityComponent()->GetNumericAttributeBase(Attribute));
    Snapshot.Skills.Reset();
    if (auto* Skills = Player->FindComponentByClass<UACFSkillTreeComponent>())
    {
        Snapshot.SkillPoints = Skills->GetSkillPoints();
        for (auto* Tree : Skills->GetSkillTrees()) if (Tree)
            for (auto* Node : Tree->GetAllNodes()) if (auto* Skill = Cast<UACFBaseSkillNode>(Node))
            {
                const int32 Rank = Skills->GetSkillCurrentLevel(Skill->GetNodeId());
                if (Rank > 0) { auto& S = Snapshot.Skills.AddDefaulted_GetRef(); S.Tree = Tree->GetSkillTreeTag(); S.Node = Skill->GetNodeId(); S.Rank = Rank; }
            }
    }
    Publish();
    UE_LOG(LogTemp, Display, TEXT("[PracticeProgression] Capture %s pawn=%s willpower=%.3f level=%d skills=%d"), *GetWorld()->GetName(), *Player->GetName(), Snapshot.LearnedAttributes.FindRef(TEXT("Willpower")), Stats->GetCurrentLevel(), Snapshot.SkillPoints);
}

void AProjectProgressionState::FreezeForTravel()
{
    Capture(UGameplayStatics::GetPlayerPawn(this, 0));
    bCaptureFrozen = true;
    Publish();
}

bool AProjectProgressionState::Restore(AActor* Player)
{
    if (!Player || Snapshot.Version != 2 || !Snapshot.CharacterId.IsValid()) return false;
    auto* Stats = Player->FindComponentByClass<UACFGASStatisticsComponent>();
    auto* Practice = UProjectPracticeProgressionComponent::Ensure(Player);
    auto* Skills = Player->FindComponentByClass<UACFSkillTreeComponent>();
    auto* Training = Player->FindComponentByClass<UProjectPracticeComponent>();
    if (!Stats || !Practice || !Training || !Skills || !Stats->GetOwnerAbilityComponent()) return false;
    Training->CancelTraining();
    ReadSnapshot(Practice, Snapshot.Practice);
    ReadSnapshot(Training, Snapshot.Training);
    ReadSnapshot(Stats, Snapshot.Statistics);
    Practice->OnComponentLoaded();
    auto* ASC = Stats->GetOwnerAbilityComponent();
    TMap<FGameplayAttribute, float> LoadedBaseValues;
    if (bLoadPending)
    {
        TArray<FGameplayAttribute> AllAttributes;
        ASC->GetAllAttributes(AllAttributes);
        for (const auto& Attribute : AllAttributes) LoadedBaseValues.Add(Attribute, ASC->GetNumericAttributeBase(Attribute));
    }
    for (const auto& Value : Snapshot.LearnedAttributes)
    {
        const auto Attribute = Practice->ResolveAttribute(Value.Key);
        if (Attribute.IsValid() && ASC->HasAttributeSetForAttribute(Attribute) && FMath::IsFinite(Value.Value))
            ASC->SetNumericAttributeBase(Attribute, Value.Value);
    }
    // Native deserialization alone does not recreate skill effect/ability handles in ACF 4.3.5.
    // Clear existing handles through its public API, then replay native purchases in parent order.
    // A load may have overwritten ActiveSkills before ALS invokes this actor. Read
    // the native handle map to also remove skills learned AFTER the loaded snapshot.
    // ACF 4.3.5 has no public API to enumerate those handles. No native fields are written here.
    if (const auto* Handles = FindFProperty<FMapProperty>(Skills->GetClass(), TEXT("SkillHandlers")))
    {
        const auto* Values = CastField<FStructProperty>(Handles->ValueProp);
        if (Values && Values->Struct == FAbilityHandlers::StaticStruct())
        {
            FScriptMapHelper Map(Handles, Handles->ContainerPtrToValuePtr<void>(Skills));
            for (int32 I=0; I<Map.GetMaxIndex(); ++I) if (Map.IsValidIndex(I))
            {
                const auto* H = reinterpret_cast<const FAbilityHandlers*>(Map.GetValuePtr(I));
                if (H->GEHandle.IsValid()) ASC->RemoveActiveGameplayEffect(H->GEHandle);
                if (H->GAHandle.IsValid()) ASC->ClearAbility(H->GAHandle);
            }
        }
    }
    Skills->Respec();
    int32 ReplayBudget = FMath::Max(0, Snapshot.SkillPoints);
    for (const auto& Saved : Snapshot.Skills)
        if (auto* Node = Skills->GetSkillByIds(Saved.Tree, Saved.Node))
            ReplayBudget += FMath::Clamp(Saved.Rank, 0, Node->GetMaxLevel()) * FMath::Max(0, Node->GetSkillToGrant().RequiredSkillPoint);
    Skills->SetSkillPoints(ReplayBudget);
    TArray<FProjectLearnedSkillSnapshot> Pending = Snapshot.Skills;
    for (int32 Pass=0; Pass<Snapshot.Skills.Num()+1 && !Pending.IsEmpty(); ++Pass)
    {
        for (int32 I=Pending.Num()-1; I>=0; --I)
        {
            const auto Saved = Pending[I];
            const auto* Node = Skills->GetSkillByIds(Saved.Tree, Saved.Node);
            if (!Node) continue;
            ECanUnlockSkillResult Result;
            while (Skills->GetSkillCurrentLevel(Saved.Node) < FMath::Min(Saved.Rank, Node->GetMaxLevel()) &&
                Skills->CanUnlockSkill(Saved.Tree, Saved.Node, Result))
            {
                const int32 PreviousRank = Skills->GetSkillCurrentLevel(Saved.Node);
                Skills->UnlockSkillFromTree(Saved.Tree, Saved.Node);
                if (Skills->GetSkillCurrentLevel(Saved.Node) <= PreviousRank) break;
            }
            if (Skills->GetSkillCurrentLevel(Saved.Node) >= Saved.Rank) Pending.RemoveAt(I);
        }
    }
    bRestoreFailed = !Pending.IsEmpty();
    for (const auto& Saved : Snapshot.Skills)
        if (const auto* Node = Skills->GetSkillByIds(Saved.Tree, Saved.Node))
        {
            const auto Config = Node->GetSkillToGrant();
            if (Config.AbilityToGrant.GameplayAbility && !ASC->FindAbilitySpecFromClass(Config.AbilityToGrant.GameplayAbility))
                bRestoreFailed = true;
            if (Config.GameplayEffect.Effect && Config.GameplayEffect.Effect.GetDefaultObject()->DurationPolicy == EGameplayEffectDurationType::Infinite &&
                ASC->GetGameplayEffectCount(Config.GameplayEffect.Effect, nullptr, false) == 0)
                bRestoreFailed = true;
        }
    if (bRestoreFailed)
    {
        UE_LOG(LogTemp, Error, TEXT("[PracticeProgression] Skill restoration incomplete (%d pending ranks or missing native effects/abilities); original snapshot retained, automatic replay stopped"), Pending.Num());
    }
    else Skills->SetSkillPoints(Snapshot.SkillPoints);
    // Instant skill effects are already represented in the saved base values.
    // Reapply these values after replay so instant modifiers cannot accumulate on load.
    for (const auto& Value : LoadedBaseValues) ASC->SetNumericAttributeBase(Value.Key, Value.Value);
    for (const auto& Value : Snapshot.LearnedAttributes)
    {
        const auto Attribute = Practice->ResolveAttribute(Value.Key);
        if (Attribute.IsValid() && ASC->HasAttributeSetForAttribute(Attribute) && FMath::IsFinite(Value.Value))
            ASC->SetNumericAttributeBase(Attribute, Value.Value);
    }
    if (auto* Doctrine = Player->FindComponentByClass<UProjectInnerDoctrineComponent>()) Doctrine->RefreshLearnedAttributeEffects();
    Stats->OnAttributeSetModified.Broadcast();
    UE_LOG(LogTemp, Display, TEXT("[PracticeProgression] Restore %s pawn=%s willpower=%.3f level=%d pendingSkills=%d"), *GetWorld()->GetName(), *Player->GetName(), Practice->GetLearnedRank(TEXT("Willpower")), Stats->GetCurrentLevel(), Pending.Num());
    return !bRestoreFailed;
}

void AProjectProgressionState::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (bRestoreFailed) return;
    auto* Save = GetGameInstance()->GetSubsystem<UALSLoadAndSaveSubsystem>();
    if (Save && Save->GetSystemState() != ELoadingState::EIdle) return;
    AActor* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Player) return;
    UProjectPracticeProgressionComponent::Ensure(Player);
    if (Player != TrackedPlayer.Get() || bLoadPending)
    {
        if (Snapshot.CharacterId.IsValid() && !Restore(Player)) return;
        TrackedPlayer = Player;
        bLoadPending = false;
        Capture(Player);
    }
}

void AProjectProgressionState::OnSaved_Implementation()
{
    // ALS 4.3.5 invokes this interface from its save worker. Build the immutable
    // snapshot on the game thread before that worker serializes the actor.
    if (!IsInGameThread())
    {
        const TWeakObjectPtr<AProjectProgressionState> WeakThis(this);
        const auto Task = FFunctionGraphTask::CreateAndDispatchWhenReady([WeakThis]()
        {
            if (WeakThis.IsValid()) WeakThis->OnSaved_Implementation();
        }, TStatId(), nullptr, ENamedThreads::GameThread);
        FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
        return;
    }
    Capture(UGameplayStatics::GetPlayerPawn(this, 0));
}
void AProjectProgressionState::OnLoaded_Implementation() { bRestoreFailed = false; bLoadPending = true; Publish(); }
void AProjectProgressionState::Publish()
{
    if (auto* Travel = GetGameInstance()->GetSubsystem<UProjectProgressionTravelState>()) Travel->Snapshot = Snapshot;
}
void AProjectProgressionState::EndPlay(EEndPlayReason::Type Reason)
{
    Capture(TrackedPlayer.Get());
    Publish();
    Super::EndPlay(Reason);
}

bool UProjectProgressionWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const auto* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld();
}

void UProjectProgressionWorldSubsystem::RegisterAttributes(AActor* Actor)
{
    if (!Actor) return;
    // The native serialization table is global, so every existing ASC needs the
    // referenced set, including NPCs. This adds no ASC or progression component.
    if (auto* ASC = Actor->FindComponentByClass<UAbilitySystemComponent>())
        if (!ASC->GetSet<UProjectDoctrineAttributeSet>())
            ASC->AddAttributeSetSubobject(NewObject<UProjectDoctrineAttributeSet>(Actor));

    if (!GetDefault<UProjectPracticeSettings>()->bEnabled) return;
    auto* Stats = Actor->FindComponentByClass<UACFGASStatisticsComponent>();
    if (!Stats) return;
    if (auto* ASC = Actor->FindComponentByClass<UAbilitySystemComponent>(); ASC &&
        !ASC->OnActiveGameplayEffectAddedDelegateToSelf.IsBoundToObject(this))
    {
        // ACF 4.3.5 forgets the previous permanent handles on reinitialization.
        // Keep its newest handle and remove only older instances of a starting GE.
        ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddWeakLambda(this,
            [WeakStats = TWeakObjectPtr<UACFGASStatisticsComponent>(Stats)]
            (UAbilitySystemComponent* Target, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle NewHandle)
            {
                if (!WeakStats.IsValid() || !Spec.Def || !NewHandle.IsValid()) return;
                const UClass* EffectClass = Spec.Def->GetClass();
                const auto* List = FindFProperty<FArrayProperty>(WeakStats->GetClass(), TEXT("StartingEffects"));
                const auto* Item = List ? CastField<FClassProperty>(List->Inner) : nullptr;
                if (!Item || Item->MetaClass != UGameplayEffect::StaticClass()) return;
                FScriptArrayHelper Entries(List, List->ContainerPtrToValuePtr<void>(WeakStats.Get()));
                bool bStartingEffect = false;
                for (int32 I = 0; I < Entries.Num(); ++I)
                    bStartingEffect |= Item->GetObjectPropertyValue(Entries.GetRawPtr(I)) == EffectClass;
                if (!bStartingEffect) return;
                for (const auto Handle : Target->GetActiveEffects(FGameplayEffectQuery()))
                    if (Handle != NewHandle)
                        if (const auto* Existing = Target->GetGameplayEffectDefForHandle(Handle);
                            Existing && Existing->GetClass() == EffectClass)
                            Target->RemoveActiveGameplayEffect(Handle);
            });
    }
    // ACF exposes StartingEffects as editable data, but has no runtime setter.
    // Adapt only instance data before initialization; never apply a second GE.
    const auto* Initialized = FindFProperty<FBoolProperty>(Stats->GetClass(), TEXT("bIsInitialized"));
    const auto* Effects = FindFProperty<FArrayProperty>(Stats->GetClass(), TEXT("StartingEffects"));
    const auto* Classes = Effects ? CastField<FClassProperty>(Effects->Inner) : nullptr;
    if (!Initialized || Initialized->GetPropertyValue_InContainer(Stats) || !Classes ||
        Classes->MetaClass != UGameplayEffect::StaticClass()) return;
    FScriptArrayHelper Array(Effects, Effects->ContainerPtrToValuePtr<void>(Stats));
    for (int32 I=0; I<Array.Num(); ++I)
    {
        UClass* Original = Cast<UClass>(Classes->GetObjectPropertyValue(Array.GetRawPtr(I)));
        if (!Original) continue;
        FString Target;
        if (Original->GetPathName() == TEXT("/AscentCombatFramework/GASRuntime/GameplayEffects/ACF_AttDependencies_GE.ACF_AttDependencies_GE_C"))
            Target = TEXT("/Game/_Game/Data/Progression/GE_ProjectAttDependencies_GE.GE_ProjectAttDependencies_GE_C");
        else if (Original->GetPathName() == TEXT("/AscentCombatFramework/GASRuntime/GameplayEffects/ACF_StatInit_GE.ACF_StatInit_GE_C"))
            Target = TEXT("/Game/_Game/Data/Progression/GE_ProjectStatInit_GE.GE_ProjectStatInit_GE_C");
        if (Target.IsEmpty()) continue;
        if (UClass* Replacement = LoadClass<UGameplayEffect>(nullptr, *Target))
            Classes->SetObjectPropertyValue(Array.GetRawPtr(I), Replacement);
        else UE_LOG(LogTemp, Error, TEXT("[PracticeProgression] Missing validated curve adapter %s"), *Target);
    }
}

void UProjectProgressionWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    PreSpawnHandle = GetWorld()->AddOnActorPreSpawnInitialization(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::RegisterAttributes));
    SpawnHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::RegisterAttributes));
    TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::HandleWorldTearDown);
}

void UProjectProgressionWorldSubsystem::HandleWorldTearDown(UWorld* World)
{
    if (World == GetWorld() && State) State->FreezeForTravel();
}

void UProjectProgressionWorldSubsystem::Deinitialize()
{
    GetWorld()->RemoveOnActorPreSpawnInitialization(PreSpawnHandle);
    GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
    FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
    Super::Deinitialize();
}

void UProjectProgressionWorldSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    for (TActorIterator<AActor> It(&World); It; ++It) RegisterAttributes(*It);
    if (!GetDefault<UProjectPracticeSettings>()->bEnabled) return;
    FString MenuMap;
    GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), MenuMap, GEngineIni);
    if (!MenuMap.IsEmpty() && UGameplayStatics::GetCurrentLevelName(&World, true) ==
        FPackageName::GetShortName(FPackageName::ObjectPathToPackageName(MenuMap)))
    {
        // Returning to the title screen ends the previous in-memory character.
        // Continue/load restores its own ALS snapshot; New Game starts clean.
        if (auto* Travel = World.GetGameInstance()->GetSubsystem<UProjectProgressionTravelState>()) Travel->ResetForNewCharacter();
        return;
    }
    FActorSpawnParameters P; P.Name = TEXT("ProjectProgressionState");
    State = World.SpawnActor<AProjectProgressionState>(AProjectProgressionState::StaticClass(), FTransform::Identity, P);
}
