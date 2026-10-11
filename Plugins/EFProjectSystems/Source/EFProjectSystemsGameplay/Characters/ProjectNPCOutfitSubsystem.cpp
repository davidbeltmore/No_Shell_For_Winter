#include "Characters/ProjectNPCOutfitSubsystem.h"
#include "Characters/ProjectNPCOutfitComponent.h"
#include "Characters/ProjectNPCRoster.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "ALSLoadAndSaveSubsystem.h"
#include "Engine/GameInstance.h"

void UProjectNPCOutfitSubsystem::AppearanceReady(APawn* Pawn)
{
    if (!IsValid(Pawn) || Pawn->IsPlayerControlled() || !Pawn->HasAuthority() || bRosterFailed) return;
    Pending.AddUnique(Pawn);
    if (!Roster && !RosterLoad)
    {
        LoadStarted = FPlatformTime::Seconds();
        const FSoftObjectPath Path(TEXT("/Game/_Game/Characters/Data/DA_NPCRoster.DA_NPCRoster"));
        RosterLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Path, FStreamableDelegate::CreateWeakLambda(this, [this, Path]
        {
            Roster = Cast<UProjectNPCRoster>(Path.ResolveObject());
            bRosterFailed = !Roster;
            if (bRosterFailed) UE_LOG(LogTemp, Error, TEXT("NPCOutfits FAIL: roster could not load"));
        }));
    }
}
bool UProjectNPCOutfitSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game || Type == EWorldType::PIE; }
bool UProjectNPCOutfitSubsystem::IsTickable() const { return !IsTemplate() && (!Pending.IsEmpty() || !Active.IsEmpty()); }
TStatId UProjectNPCOutfitSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectNPCOutfitSubsystem, STATGROUP_Tickables); }
void UProjectNPCOutfitSubsystem::Tick(float DeltaTime)
{
    if (!Roster && !bRosterFailed && FPlatformTime::Seconds() - LoadStarted > 20)
    { bRosterFailed = true; if (RosterLoad) RosterLoad->CancelHandle(); UE_LOG(LogTemp, Error, TEXT("NPCOutfits FAIL: roster load exceeded 20 seconds")); }
    if (bRosterFailed) { Pending.Reset(); return; }
    auto* Save = GetWorld()->GetGameInstance()->GetSubsystem<UALSLoadAndSaveSubsystem>();
    if (Save && Save->GetSystemState() != ELoadingState::EIdle) return;
    for (int32 I = Active.Num()-1; I >= 0; --I)
    {
        auto* Component = Active[I].Get();
        if (Component) Component->Advance();
        if (!Component || Component->State == EProjectNPCOutfitState::Ready || Component->State == EProjectNPCOutfitState::Failed) Active.RemoveAtSwap(I);
    }
    if (!Roster || Pending.IsEmpty() || Active.Num() >= 4) return;
    APawn* Pawn = Pending[0].Get(); Pending.RemoveAt(0);
    if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed()) return;
    auto* Component = Pawn->FindComponentByClass<UProjectNPCOutfitComponent>();
    if (Component && !Component->bEnabled) return;
    if (Component && Active.Contains(Component)) return;
    TArray<FProjectNPCOutfitChoice> Choices;
    if (Component && !Component->OverrideChoices.IsEmpty()) Choices = Component->OverrideChoices;
    else for (const auto& Row : Roster->Archetypes)
        if (Row.Male.ToSoftObjectPath() == FSoftObjectPath(Pawn->GetClass()) || Row.Female.ToSoftObjectPath() == FSoftObjectPath(Pawn->GetClass()))
        { Choices = Row.Outfits; break; }
    if (Choices.IsEmpty()) return;
    if (!Component)
    {
        Component = NewObject<UProjectNPCOutfitComponent>(Pawn, TEXT("ProjectNPCOutfit"));
        Pawn->AddInstanceComponent(Component); Component->RegisterComponent();
    }
    Component->Start(Choices);
    Active.AddUnique(Component);
}
void UProjectNPCOutfitSubsystem::Deinitialize()
{
    if (RosterLoad) RosterLoad->CancelHandle();
    RosterLoad.Reset(); Pending.Reset(); Active.Reset(); Roster = nullptr;
    Super::Deinitialize();
}
