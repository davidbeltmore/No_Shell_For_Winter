#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "ProjectInnerStateQA.generated.h"

/** Disposable, bounded gameplay fixture. Never spawned during ordinary play. */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectInnerStateQA : public AInfo
{
    GENERATED_BODY()
public:
    AProjectInnerStateQA();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    void Check(const FString& Name, bool bPassed);
    void Next(int32 NextStage);
    void Finish(const FString& Error=FString());
    void Neutral();
    bool UseItem(const FString& Name);
    double Health() const;
    double MaximumHealth() const;
    void SetHealth(double Fraction);
    void Screenshot(const FString& Name);
    UFUNCTION() void HandlePoisonNPCDeath();
    UPROPERTY() TObjectPtr<class ACharacter> PoisonNPC;
    bool bNPCDebtSettled=false;
    UPROPERTY() TObjectPtr<class APawn> Player;
    UPROPERTY() TObjectPtr<class UProjectInnerStateComponent> Inner;
    UPROPERTY() TObjectPtr<class UProjectSurvivalNeedsComponent> Needs;
    UPROPERTY() TObjectPtr<class UProjectRealtimeSnapshotComponent> Resources;
    UPROPERTY() TObjectPtr<class UACFInventoryComponent> Inventory;
    TArray<TSharedPtr<class FJsonValue>> Checks;
    TArray<double> BaselineCpu, LoadedCpu, BaselineFrames, LoadedFrames;
    double Started=0, StageStarted=0, SimulationStarted=0, InitialHealth=0, InitialHours=0, InitialHunger=0;
    uint64 LastSerial=0;
    int32 Stage=-1;
    bool bPassed=true, bDone=false, bEarlyChecked=false, bNormalChecked=false, bStressShot=false, bKnockoutChecked=false;
    FString OutputDirectory;
    FVector BeforeWaterLocation=FVector::ZeroVector;
    double WaterHygiene=0;
    uint8 BeforeWaterMovement=0;
};
