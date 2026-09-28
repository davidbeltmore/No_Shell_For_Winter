#pragma once

#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "ProjectPracticeWidget.generated.h"

class UProjectPracticeComponent;

/** Passive HUD: no focus, input capture, backdrop, or minigame. */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectPracticeWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void Configure(UProjectPracticeComponent* InPractice);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    FText CurrentRankText() const;
    FText NextRankText() const;
    FText ExerciseText() const;
    FText ElapsedText() const;
    TOptional<float> RankProgress() const;
    int32 CurrentRank() const;
    UPROPERTY(Transient) TWeakObjectPtr<UProjectPracticeComponent> Practice;
    FProgressBarStyle BarStyle;
};
