#include "EFEquipmentSaveSubsystem.h"
#include "EFEquipmentSaveGame.h"
#include "EFEquipmentSaveFiles.h"
#include "EFEquipmentPersistence.h"
#include "EFEquipmentTravelPolicy.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentData.h"
#include "ALSFunctionLibrary.h"
#include "ALSSaveGameSettings.h"
#include "ALSSaveInfo.h"
#include "ANSUIPlayerSubsystem.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"

namespace EFEquipmentSaveIntegration
{
    bool CurrentContract(UALSLoadAndSaveSubsystem* Native, FString& Error)
    {
        const auto* Property = FindFProperty<FObjectPropertyBase>(UALSLoadAndSaveSubsystem::StaticClass(), TEXT("currentSavegame"));
        if (Native && Property && Property->PropertyClass == UALSSaveGame::StaticClass()) return true;
        Error = TEXT("The native ALS current-save contract changed."); return false;
    }
    bool SetCurrent(UALSLoadAndSaveSubsystem* Native, UALSSaveGame* Save, FString& Error)
    {
        static auto* Property = FindFProperty<FObjectPropertyBase>(UALSLoadAndSaveSubsystem::StaticClass(), TEXT("currentSavegame"));
        if (!Native || !Property || Property->PropertyClass != UALSSaveGame::StaticClass())
        { Error = TEXT("The native ALS current-save contract changed."); return false; }
        Property->SetObjectPropertyValue_InContainer(Native, Save); return true;
    }

    UEFEquipmentSaveGame* CloneForSave(UALSSaveGame* Original, FString& Error)
    {
        if (auto* Own = Cast<UEFEquipmentSaveGame>(Original))
        {
            if (Own->EquipmentVersion != 1 || Own->EquipmentPlayer.Version != 1)
            { Error = TEXT("This save uses an unsupported equipment version."); return nullptr; }
            return DuplicateObject<UEFEquipmentSaveGame>(Own, GetTransientPackage());
        }
        if (Original && Original->GetClass() != UALSSaveGame::StaticClass())
        { Error = TEXT("This custom save class needs an explicit equipment adapter."); return nullptr; }
        auto* Candidate = NewObject<UEFEquipmentSaveGame>();
        if (Original) for (TFieldIterator<FProperty> It(UALSSaveGame::StaticClass()); It; ++It)
            if (It->HasAnyPropertyFlags(CPF_SaveGame)) It->CopyCompleteValue_InContainer(Candidate, Original);
        return Candidate;
    }

    bool CaptureLocal(UWorld* World, UEFEquipmentSaveGame* Candidate, FString& Error)
    {
        auto* Controller = UGameplayStatics::GetPlayerController(World, 0);
        APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
        auto* Equipment = UEFEquipmentCoordinatorComponent::FindOrCreate(Pawn);
        if (!Controller || !Pawn || !Equipment || Equipment->IsBusy() || !UALSFunctionLibrary::ShouldSaveActor(Controller) || !UALSFunctionLibrary::ShouldSaveActor(Pawn))
        { Error = TEXT("The local player is not ready for an equipment save."); return false; }
        if (!Equipment->FlushContainers(Error)) return false;
        FALSPlayerData Player;
        Player.PlayerController = UALSFunctionLibrary::SerializeActor(Controller);
        Player.Pawn = UALSFunctionLibrary::SerializeActor(Pawn);
        return EFEquipmentPersistence::CapturePlayer(Candidate, Equipment, Player, Error);
    }
}

void UEFEquipmentSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); Collection.InitializeDependency<UALSLoadAndSaveSubsystem>();
    GetGameInstance()->GetOnPawnControllerChanged().AddUniqueDynamic(this, &ThisClass::HandlePlayerPossessed);
    PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
}
void UEFEquipmentSaveSubsystem::Deinitialize()
{
    GetGameInstance()->GetOnPawnControllerChanged().RemoveDynamic(this, &ThisClass::HandlePlayerPossessed);
    ClearTravelArrival();
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
    Super::Deinitialize();
}
void UEFEquipmentSaveSubsystem::HandlePlayerPossessed(APawn* Pawn, AController* Controller)
{
    if (Pawn && Controller && Controller->IsLocalPlayerController() && Pawn->HasAuthority())
        UEFEquipmentCoordinatorComponent::FindOrCreate(Pawn);
}
void UEFEquipmentSaveSubsystem::HandlePostLoadMap(UWorld* World)
{
    if (!World || World->GetGameInstance() != GetGameInstance() || AutomaticTravelDestination.IsEmpty()
        || UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) != AutomaticTravelDestination) return;
    AutomaticTravelDestination.Reset();
    // A native caller may already have restored the same single-use snapshot.
    if (!TravelingPlayer) return;
    TravelArrivalWorld = World; TravelArrivalDeadline = FPlatformTime::Seconds() + 5.0;
    World->GetTimerManager().SetTimer(TravelArrivalTimer, this, &ThisClass::PollTravelArrival, .1f, true);
}
void UEFEquipmentSaveSubsystem::ClearTravelArrival()
{
    if (auto* World = TravelArrivalWorld.Get()) World->GetTimerManager().ClearTimer(TravelArrivalTimer);
    TravelArrivalWorld.Reset();
}
void UEFEquipmentSaveSubsystem::PollTravelArrival()
{
    if (!TravelingPlayer) { ClearTravelArrival(); return; }
    auto* World = TravelArrivalWorld.Get();
    auto* Controller = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
    APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    FALSPlayerData Player; TravelingPlayer->GetLocalPlayer(Player);
    const auto* GameMode = World ? World->GetAuthGameMode() : nullptr;
    const bool bReady = World && World == GetWorld() && Controller && Pawn && Pawn->HasActorBegunPlay()
        && EFEquipmentTravelPolicy::IsPawnClassCompatible(Player.Pawn.Class, Pawn->GetClass(),
            GameMode ? GameMode->DefaultPawnClass.Get() : nullptr, true)
        && Player.PlayerController.Class == Controller->GetClass()
        && Native()->GetSystemState() == ELoadingState::EIdle && !bBusy;
    if (!bReady && FPlatformTime::Seconds() < TravelArrivalDeadline) return;
    ClearTravelArrival();
    if (!bReady)
    {
        LastError = FString::Printf(TEXT("Travel restoration is waiting for the saved player/controller classes. Expected %s / %s; found %s / %s. The recovery snapshot was retained."),
            *GetNameSafe(Player.Pawn.Class), *GetNameSafe(Player.PlayerController.Class),
            *GetNameSafe(Pawn ? Pawn->GetClass() : nullptr), *GetNameSafe(Controller ? Controller->GetClass() : nullptr));
    }
    if (!bReady || !RestoreTravel())
    {
        if (auto* UI = GetGameInstance()->GetSubsystem<UANSUIPlayerSubsystem>()) UI->BroadcastNotification(GetLastError(), 12.f);
        UE_LOG(LogTemp, Error, TEXT("EFEquipment travel restore retained its recovery snapshot: %s"), *LastError);
    }
}

UALSLoadAndSaveSubsystem* UEFEquipmentSaveSubsystem::Native() const
{ return GetGameInstance()->GetSubsystem<UALSLoadAndSaveSubsystem>(); }

FString UEFEquipmentSaveSubsystem::GetActiveSlot() const
{ return !ActiveSlot.IsEmpty() ? ActiveSlot : Native()->GetCurrentSaveName(); }

bool UEFEquipmentSaveSubsystem::Save(const FString& Slot, bool bWorld, bool bLocalPlayer, bool bScreenshot, const FString& Description)
{
    using namespace EFEquipmentSaveIntegration;
    LastError.Reset(); auto* ALS = Native();
    if (IsBusy() || !ALS || ALS->GetSystemState() != ELoadingState::EIdle)
    { LastError = TEXT("A save or load is already in progress."); return false; }
    TGuardValue<bool> Busy(bBusy, true);
    FEFEquipmentSlotBackup Before, BeforeMetadata;
    const auto* Settings = GetDefault<UALSSaveGameSettings>();
    if (Slot.Equals(Settings->GetSaveMetadataName(), ESearchCase::IgnoreCase))
    { LastError = TEXT("This slot name is reserved for the save list."); return false; }
    if (!CurrentContract(ALS, LastError)) return false;
    if (!EFEquipmentSaveFiles::Backup(Slot, Before, LastError)
        || !EFEquipmentSaveFiles::Backup(Settings->GetSaveMetadataName(), BeforeMetadata, LastError)) return false;
    TStrongObjectPtr<UALSSaveGame> DiskOriginal(Before.bExisted ? Cast<UALSSaveGame>(UGameplayStatics::LoadGameFromMemory(Before.OriginalBytes)) : nullptr);
    if (Before.bExisted && !DiskOriginal) { LastError = TEXT("The original slot is not a readable ALS save. Its backup was retained."); return false; }
    TStrongObjectPtr<UEFEquipmentSaveGame> Candidate(CloneForSave(ALS->GetCurrentSaveGame() ? ALS->GetCurrentSaveGame() : DiskOriginal.Get(), LastError));
    if (!Candidate) return false;
    if (bLocalPlayer && !CaptureLocal(GetWorld(), Candidate.Get(), LastError)) return false;
    if (bWorld)
    {
        const FString LevelName = UGameplayStatics::GetCurrentLevelName(GetWorld(), true);
        FALSLevelData Level; Candidate->TryGetLevelData(LevelName, Level); Level.CleanActors();
        TArray<TWeakObjectPtr<AActor>> Actors;
        for (TActorIterator<AActor> It(GetWorld()); It; ++It) Actors.Add(*It);
        for (const auto& Pointer : Actors)
            if (auto* Actor = Pointer.Get(); Actor && UALSFunctionLibrary::ShouldSaveActor(Actor) && !UALSFunctionLibrary::IsSpecialActor(GetWorld(), Actor))
            {
                TArray<UACFInventoryComponent*> Inventories; Actor->GetComponents(Inventories);
                for (auto* Inventory : Inventories) if (!EFEquipmentPersistence::ValidateContents(Inventory, LastError)) return false;
                auto Record = UALSFunctionLibrary::SerializeActor(Actor);
                if (!EFEquipmentPersistence::ValidateActorInventories(Actor, Record, LastError)) return false;
                Level.AddActorRecord(Record);
            }
        Candidate->AddLevel(LevelName, Level);
        if (auto* State = GetWorld()->GetGameState(); UALSFunctionLibrary::ShouldSaveActor(State)) Candidate->StoreGameState(UALSFunctionLibrary::SerializeActor(State));
        if (auto* Controller = UGameplayStatics::GetPlayerController(GetWorld(), 0); Controller && UALSFunctionLibrary::ShouldSaveActor(Controller->PlayerState))
            Candidate->StorePlayerState(UALSFunctionLibrary::SerializeActor(Controller->PlayerState));
    }
    const int32 PlayTime = FMath::Max(0, Candidate->GetPlayTime()) + FMath::Max(0, static_cast<int32>((FDateTime::Now() - ALS->GetStartPlayTime()).GetTotalSeconds()));
    Candidate->SetPlayTime(PlayTime); Candidate->OnSaved();
    auto Validate = [&](USaveGame* Object, FString& Error) { return EFEquipmentPersistence::ValidateSlot(GetWorld(), Cast<UEFEquipmentSaveGame>(Object), Error); };
    TStrongObjectPtr<UALSSaveInfo> Metadata(BeforeMetadata.bExisted ? Cast<UALSSaveInfo>(UGameplayStatics::LoadGameFromMemory(BeforeMetadata.OriginalBytes)) : NewObject<UALSSaveInfo>());
    if (!Metadata) { LastError = TEXT("The native save-list metadata is unreadable. The original was retained."); return false; }
    FALSSaveMetadata Entry; Entry.SaveName = Slot; Entry.MapToLoad = UGameplayStatics::GetCurrentLevelName(GetWorld(), true);
    Entry.SaveDescription = Description; Entry.Data = FDateTime::Now(); Entry.PlayTime = PlayTime; Metadata->AddSlot(Entry);
    if (!EFEquipmentSaveFiles::Publish(Candidate.Get(), Before, Validate, LastError)) return false;
    auto ValidateMetadata = [&](USaveGame* Object, FString& Error)
    {
        auto* Info = Cast<UALSSaveInfo>(Object); FALSSaveMetadata Actual;
        if (!Info || !Info->TryGetSaveSlotData(Slot, Actual) || Actual.MapToLoad != Entry.MapToLoad || Actual.PlayTime != Entry.PlayTime)
        { Error = TEXT("The native save-list metadata failed validation."); return false; }
        return true;
    };
    if (!EFEquipmentSaveFiles::Publish(Metadata.Get(), BeforeMetadata, ValidateMetadata, LastError))
    {
        FString RecoveryError;
        if (!EFEquipmentSaveFiles::Recover(Before, RecoveryError)) LastError += TEXT(" ") + RecoveryError;
        return false;
    }
    if (!SetCurrent(ALS, Candidate.Get(), LastError))
    {
        FString RecoveryError;
        if (!EFEquipmentSaveFiles::Recover(Before, RecoveryError)) LastError += TEXT(" ") + RecoveryError;
        if (!EFEquipmentSaveFiles::Recover(BeforeMetadata, RecoveryError)) LastError += TEXT(" ") + RecoveryError;
        return false;
    }
    ActiveSlot = Slot; ALS->StartPlaytimeTracking();
    if (bScreenshot) UALSFunctionLibrary::TrySaveScreenshot(Slot, Settings->GetDefaultScreenshotWidth(), Settings->GetDefaultScreenshotHeight());
    return true;
}

UEFEquipmentSaveGame* UEFEquipmentSaveSubsystem::PrepareLoad(const FString& Slot)
{
    LastMigrationSummary = FText::GetEmpty(); bConvertedOnLoad = false;
    FEFEquipmentSlotBackup Before;
    if (!EFEquipmentSaveFiles::Backup(Slot, Before, LastError)) return nullptr;
    if (!Before.bExisted) { LastError = TEXT("The selected save slot does not exist."); return nullptr; }
    TStrongObjectPtr<UALSSaveGame> Original(Cast<UALSSaveGame>(UGameplayStatics::LoadGameFromMemory(Before.OriginalBytes)));
    auto* Registry = GetDefault<UEFEquipmentSettings>()->SlotRegistry.LoadSynchronous();
    auto* Candidate = EFEquipmentPersistence::ConvertSlot(GetWorld(), Original.Get(), Registry, LastError);
    if (!Candidate) return nullptr;
    if (!Original->IsA<UEFEquipmentSaveGame>())
    {
        auto Validate = [&](USaveGame* Object, FString& Error) { return EFEquipmentPersistence::ValidateSlot(GetWorld(), Cast<UEFEquipmentSaveGame>(Object), Error); };
        if (!EFEquipmentSaveFiles::Publish(Candidate, Before, Validate, LastError)) return nullptr;
        bConvertedOnLoad = true;
    }
    return Candidate;
}

void UEFEquipmentSaveSubsystem::PublishMigrationSummary(UEFEquipmentSaveGame* Loaded)
{
    if (!Loaded) return;
    auto* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    auto* Equipment = Pawn ? Pawn->FindComponentByClass<UEFEquipmentCoordinatorComponent>() : nullptr;
    LastMigrationSummary = EFEquipmentPersistence::DescribeMigration(Equipment ? Equipment->GetNativeEquipment() : nullptr,
        Loaded->EquipmentPlayer, bConvertedOnLoad);
    bConvertedOnLoad = false;
    if (!LastMigrationSummary.IsEmpty())
    {
        if (auto* UI = GetGameInstance()->GetSubsystem<UANSUIPlayerSubsystem>()) UI->BroadcastNotification(LastMigrationSummary, 12.f);
        UE_LOG(LogTemp, Display, TEXT("EFEquipment migration: %s"), *LastMigrationSummary.ToString());
    }
}

bool UEFEquipmentSaveSubsystem::RestoreLocalState(UEFEquipmentSaveGame* Loaded, bool bTransform, bool bTravel)
{
    auto* Controller = UGameplayStatics::GetPlayerController(GetWorld(), 0); APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    auto* Equipment = UEFEquipmentCoordinatorComponent::FindOrCreate(Pawn);
    FALSPlayerData Player; if (Loaded) Loaded->GetLocalPlayer(Player);
    const auto* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr;
    if (!Loaded || !Equipment || !Controller || !EFEquipmentTravelPolicy::IsPawnClassCompatible(
        Player.Pawn.Class, Pawn ? Pawn->GetClass() : nullptr,
        GameMode ? GameMode->DefaultPawnClass.Get() : nullptr, bTravel)
        || Player.PlayerController.Class != Controller->GetClass())
    { LastError = TEXT("The saved player or controller class does not match this game."); return false; }
    TStrongObjectPtr<UEFEquipmentSaveGame> Recovery(NewObject<UEFEquipmentSaveGame>());
    if (!EFEquipmentSaveIntegration::CaptureLocal(GetWorld(), Recovery.Get(), LastError)) return false;
    UALSFunctionLibrary::FullDeserializeActor(Pawn, Player.Pawn, bTransform);
    UALSFunctionLibrary::FullDeserializeActor(Controller, Player.PlayerController, bTransform);
    if (EFEquipmentPersistence::RestorePlayer(Equipment, Loaded->EquipmentPlayer, LastError)) return true;
    const FString Cause = LastError; FALSPlayerData Previous; Recovery->GetLocalPlayer(Previous);
    UALSFunctionLibrary::FullDeserializeActor(Pawn, Previous.Pawn, bTransform);
    UALSFunctionLibrary::FullDeserializeActor(Controller, Previous.PlayerController, bTransform);
    FString RecoveryError;
    LastError = EFEquipmentPersistence::RestorePlayer(Equipment, Recovery->EquipmentPlayer, RecoveryError)
        ? TEXT("Load failed. The previous player inventory was restored. ") + Cause
        : TEXT("Load recovery was incomplete. Inventory objects were retained. ") + RecoveryError;
    return false;
}

bool UEFEquipmentSaveSubsystem::LoadLocal(const FString& Slot, bool bTransform)
{
    LastError.Reset();
    if (IsBusy() || Native()->GetSystemState() != ELoadingState::EIdle) { LastError = TEXT("A save or load is already in progress."); return false; }
    TGuardValue<bool> Busy(bBusy, true);
    TStrongObjectPtr<UALSSaveGame> Previous(Native()->GetCurrentSaveGame());
    TStrongObjectPtr<UEFEquipmentSaveGame> Loaded(PrepareLoad(Slot));
    if (!Loaded || !EFEquipmentSaveIntegration::SetCurrent(Native(), Loaded.Get(), LastError)) return false;
    if (!RestoreLocalState(Loaded.Get(), bTransform))
    { FString Error; EFEquipmentSaveIntegration::SetCurrent(Native(), Previous.Get(), Error); return false; }
    ActiveSlot = Slot; PublishMigrationSummary(Loaded.Get()); return true;
}

void UEFEquipmentSaveSubsystem::LoadWorld(const FString& Slot, const FOnLoadFinished& Callback, bool bPlayer)
{
    LastError.Reset();
    if (IsBusy() || Native()->GetSystemState() != ELoadingState::EIdle)
    { LastError = TEXT("A save or load is already in progress."); Callback.ExecuteIfBound(false); return; }
    TStrongObjectPtr<UEFEquipmentSaveGame> Loaded(PrepareLoad(Slot));
    if (!Loaded) { Callback.ExecuteIfBound(false); return; }
    bBusy = true; PendingSlot = Slot; PendingLoadCallback = Callback; bReloadPlayer = bPlayer;
    FOnLoadFinished Internal; Internal.BindDynamic(this, &UEFEquipmentSaveSubsystem::CompleteNativeLoad);
    Native()->LoadGameWorld(Slot, Internal, bPlayer);
}

void UEFEquipmentSaveSubsystem::CompleteNativeLoad(bool bSuccess)
{
    if (bSuccess && bReloadPlayer)
    {
        auto* Loaded = Cast<UEFEquipmentSaveGame>(Native()->GetCurrentSaveGame());
        auto* Equipment = UEFEquipmentCoordinatorComponent::FindOrCreate(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
        bSuccess = Loaded && EFEquipmentPersistence::RestorePlayer(Equipment, Loaded->EquipmentPlayer, LastError);
    }
    if (bSuccess) ActiveSlot = PendingSlot;
    if (bSuccess && bReloadPlayer) PublishMigrationSummary(Cast<UEFEquipmentSaveGame>(Native()->GetCurrentSaveGame()));
    bBusy = false; const auto Callback = PendingLoadCallback; PendingLoadCallback.Unbind(); PendingSlot.Reset();
    Callback.ExecuteIfBound(bSuccess);
}

bool UEFEquipmentSaveSubsystem::CaptureTravel()
{
    LastError.Reset();
    if (IsBusy() || Native()->GetSystemState() != ELoadingState::EIdle) { LastError = TEXT("A save or load is already in progress."); return false; }
    auto* Candidate = NewObject<UEFEquipmentSaveGame>();
    if (!EFEquipmentSaveIntegration::CaptureLocal(GetWorld(), Candidate, LastError) || !EFEquipmentPersistence::ValidateSlot(GetWorld(), Candidate, LastError)) return false;
    TravelingPlayer = Candidate; AutomaticTravelDestination.Reset(); Native()->SetLoadType(ELoadType::EPlayerOnly); return true;
}

bool UEFEquipmentSaveSubsystem::CaptureTravelForMap(const FString& DestinationPackage)
{
    if (!FPackageName::IsValidLongPackageName(DestinationPackage))
    { LastError = TEXT("The equipment travel destination is invalid."); return false; }
    if (!CaptureTravel()) return false;
    AutomaticTravelDestination = DestinationPackage; return true;
}

bool UEFEquipmentSaveSubsystem::RestoreTravel()
{
    LastError.Reset();
    if (bBusy || Native()->GetSystemState() != ELoadingState::EIdle) { LastError = TEXT("A save or load is already in progress."); return false; }
    TGuardValue<bool> Busy(bBusy, true);
    if (!TravelingPlayer) { LastError = TEXT("No equipment travel snapshot is available."); return false; }
    if (!RestoreLocalState(TravelingPlayer, false, true)) return false;
    TravelingPlayer = nullptr; AutomaticTravelDestination.Reset(); ClearTravelArrival(); Native()->SetLoadType(ELoadType::EDontReload); return true;
}
