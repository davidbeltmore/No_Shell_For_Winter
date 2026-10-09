#pragma once
#include "Subsystems/WorldSubsystem.h"
#include "ProjectNPCNavigationSubsystem.generated.h"

class APawn;
class AController;

/** Keeps owned NPCs on their selected nav data when Detour uses another agent. */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectNPCNavigationSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
    UFUNCTION() void ControllerChanged(APawn* Pawn,AController* Controller);
    void Configure(TWeakObjectPtr<APawn> Pawn,int32 Attempt);
};
