#include "Compatibility/ProjectACFInventoryCompatibilitySubsystem.h"
#include "ACFInventorySettings.h"
#include "Items/ACFWorldItem.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"

void UProjectACFInventoryCompatibilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UACFInventorySettings* Settings=GetMutableDefault<UACFInventorySettings>();
    FString ConfiguredPath;
    if (GConfig) GConfig->GetString(TEXT("/Script/InventorySystem.ACFInventorySettings"),TEXT("WorldItemClass"),
        ConfiguredPath,GConfig->GetConfigFilename(TEXT("Plugins")));
    ConfiguredPath=FPackageName::ExportTextPathToObjectPath(ConfiguredPath);
    if (!ConfiguredPath.IsEmpty()) RetainedWorldItemClass=LoadClass<AACFWorldItem>(nullptr,*ConfiguredPath);
    if (RetainedWorldItemClass)
    {
        // The precompiled ACF helper reads this CDO at death. Explicit ownership
        // avoids relying on a config-loaded class surviving only in that cache.
        Settings->WorldItemClass=RetainedWorldItemClass.Get();
        UE_LOG(LogTemp,Display,TEXT("[ACFInventoryCompatibility] Retained configured drop class %s"),*RetainedWorldItemClass->GetPathName());
    }
    else UE_LOG(LogTemp,Error,TEXT("[ACFInventoryCompatibility] Could not resolve configured drop class: %s"),*ConfiguredPath);
}
