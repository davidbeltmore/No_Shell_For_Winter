#include "EFEquipmentAuthoringLibrary.h"
#include "ALSSaveGameSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"
#include "EFEquipmentSaveSubsystem.h"
#include "Engine/GameInstance.h"

namespace EFEquipmentSaveQA
{
    FString OriginalMetadata;
    FString Prefix;
}

FString UEFEquipmentAuthoringLibrary::BeginSaveFixture(UObject* WorldContextObject)
{
    using namespace EFEquipmentSaveQA;
    auto* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    auto* Property = FindFProperty<FStrProperty>(UALSSaveGameSettings::StaticClass(), TEXT("SavesMetadata"));
    if (!World || World->WorldType != EWorldType::PIE || !Property || !Prefix.IsEmpty()) return FString();
    Prefix = TEXT("EquipmentQA_PIE_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("_");
    auto* Settings = GetMutableDefault<UALSSaveGameSettings>();
    OriginalMetadata = Settings->GetSaveMetadataName();
    // Direct transient CDO assignment deliberately avoids PostEditChange/SaveConfig.
    Property->SetPropertyValue_InContainer(Settings, Prefix + TEXT("Metadata"));
    return Prefix;
}

bool UEFEquipmentAuthoringLibrary::EndSaveFixture()
{
    using namespace EFEquipmentSaveQA;
    if (Prefix.IsEmpty()) return true;
    auto* Property = FindFProperty<FStrProperty>(UALSSaveGameSettings::StaticClass(), TEXT("SavesMetadata"));
    if (!Property) return false;
    Property->SetPropertyValue_InContainer(GetMutableDefault<UALSSaveGameSettings>(), OriginalMetadata);
    bool bClean = true;
    for (const TCHAR* Suffix : {TEXT("Player"), TEXT("World"), TEXT("Metadata")})
    {
        const FString Slot = Prefix + Suffix;
        if (UGameplayStatics::DoesSaveGameExist(Slot, 0)) bClean &= UGameplayStatics::DeleteGameInSlot(Slot, 0);
    }
    OriginalMetadata.Reset(); Prefix.Reset(); return bClean;
}

bool UEFEquipmentAuthoringLibrary::SaveWorldFixture(UObject* WorldContextObject)
{
    auto* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (!World || World->WorldType != EWorldType::PIE || EFEquipmentSaveQA::Prefix.IsEmpty()) return false;
    return World->GetGameInstance()->GetSubsystem<UEFEquipmentSaveSubsystem>()->Save(EFEquipmentSaveQA::Prefix + TEXT("World"), true, true, false, TEXT("Equipment QA world"));
}

bool UEFEquipmentAuthoringLibrary::LoadWorldFixture(UObject* WorldContextObject)
{
    auto* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (!World || World->WorldType != EWorldType::PIE || EFEquipmentSaveQA::Prefix.IsEmpty()) return false;
    auto* Save = World->GetGameInstance()->GetSubsystem<UEFEquipmentSaveSubsystem>();
    Save->LoadWorld(EFEquipmentSaveQA::Prefix + TEXT("World"), FOnLoadFinished(), true);
    return Save->IsBusy();
}

bool UEFEquipmentAuthoringLibrary::IsSaveFixtureBusy(UObject* WorldContextObject)
{
    auto* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    return !World || World->WorldType != EWorldType::PIE || World->GetGameInstance()->GetSubsystem<UEFEquipmentSaveSubsystem>()->IsBusy();
}
