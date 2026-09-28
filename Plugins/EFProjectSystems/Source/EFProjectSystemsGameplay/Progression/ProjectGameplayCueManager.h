#pragma once
#include "GameplayCueManager.h"
#include "ProjectGameplayCueManager.generated.h"

/** Registers the canonical ACF hit cue once; the legacy alias remains loadable. */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayCueManager : public UGameplayCueManager
{
    GENERATED_BODY()
public:
    virtual bool ShouldLoadGameplayCueAssetData(const FAssetData& Data) const override;
};
