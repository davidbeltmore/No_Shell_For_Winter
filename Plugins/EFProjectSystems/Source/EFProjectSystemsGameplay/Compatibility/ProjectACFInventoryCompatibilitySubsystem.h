#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectACFInventoryCompatibilitySubsystem.generated.h"

/** Own the configured ACF drop class for the game lifetime, including packaged GC. */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectACFInventoryCompatibilitySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
private:
    UPROPERTY(Transient)
    TObjectPtr<UClass> RetainedWorldItemClass;
};
