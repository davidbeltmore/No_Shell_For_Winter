#pragma once

#include "ACFTrainingComponent.h"
#include "ProjectPracticeComponent.generated.h"

class UProjectPracticeProgressionComponent;
class UProjectLockpickableComponent;

UCLASS(ClassGroup=(Training), meta=(BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectPracticeComponent : public UACFTrainingComponent
{
    GENERATED_BODY()
public:
    UProjectPracticeComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    virtual bool CanStartTraining(FName TrainingId, FText& OutReason) const override;
    UFUNCTION(BlueprintCallable, Category="Training") bool ConfigureBook(FName BookId, double RequiredCalendarHours);
    UFUNCTION(BlueprintPure, Category="Training") double GetSessionHours() const { return SessionHours; }
    UFUNCTION(BlueprintPure, Category="Training") double GetSessionDuration() const { return SessionDuration; }
    UFUNCTION(BlueprintPure, Category="Training") FName GetPracticedAttribute() const { return PracticedAttribute; }
    UFUNCTION(BlueprintPure, Category="Training") double GetBookHours(FName BookId) const { return BookHours.FindRef(BookId); }
    UFUNCTION(BlueprintPure, Category="Training") double GetActivityProgress() const;
    UFUNCTION(BlueprintPure, Category="Training") FText GetPracticeDescription() const;
    UPROPERTY(SaveGame) TMap<FName, double> BookHours;
    UPROPERTY(SaveGame) FName SelectedBook = TEXT("IntroductoryReader");
    UPROPERTY(SaveGame) double BookDuration = 48.0;
    UPROPERTY(SaveGame) float InitialStrength = -1.f;
protected:
    virtual bool StartTrainingAuthority(FName TrainingId) override;
    virtual void CancelTrainingAuthority() override;
    virtual void CompleteTrainingAuthority(bool bSuccess) override;
    // All current activities are passive. Keep the portable input/session contract
    // for future key-sequence challenges, without activating the old timing game.
    virtual bool StartTimingMinigameSessionAuthority(int32, int32, float, float, float) override { return false; }
private:
    bool NeedsAllowPractice() const;
    UProjectLockpickableComponent* FindNearbyLock() const;
    void Finish(EACFTrainingSessionResult Result);
    UPROPERTY(Transient) TObjectPtr<UProjectPracticeProgressionComponent> Progression;
    UPROPERTY(Transient) TWeakObjectPtr<UProjectLockpickableComponent> PracticeLock;
    double SessionHours = 0.0;
    double SessionDuration = 0.0;
    double SessionPractice = 0.0;
    FName PracticedAttribute;
};
