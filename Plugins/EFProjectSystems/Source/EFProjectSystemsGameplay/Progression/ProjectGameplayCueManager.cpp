#include "Progression/ProjectGameplayCueManager.h"
#include "AssetRegistry/AssetData.h"

bool UProjectGameplayCueManager::ShouldLoadGameplayCueAssetData(const FAssetData& Data) const
{
    // Both sample Blueprints inherit the same native OnExecute with identical
    // defaults and no overrides. GAS otherwise picks one by registry order.
    // Preserve both public assets; only deduplicate tag-based auto-registration.
    return Data.PackageName != TEXT("/Game/FullSample/Blueprints/GAS/Cues/ACF_DefaultReaction_GC")
        && Super::ShouldLoadGameplayCueAssetData(Data);
}
