#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ProjectNPCOutfitSubsystem.generated.h"

class APawn;
class UProjectNPCRoster;
class UProjectNPCOutfitComponent;
struct FStreamableHandle;

/** Shared admission queue: up to four initializing outfits, at most one new pawn per frame. */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectNPCOutfitSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    void AppearanceReady(APawn* Pawn);
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintPure) int32 GetPendingCount() const { return Pending.Num() + Active.Num(); }
private:
    UPROPERTY(Transient) TObjectPtr<UProjectNPCRoster> Roster;
    TSharedPtr<FStreamableHandle> RosterLoad;
    TArray<TWeakObjectPtr<APawn>> Pending;
    TArray<TWeakObjectPtr<UProjectNPCOutfitComponent>> Active;
    double LoadStarted = 0;
    bool bRosterFailed = false;
};
