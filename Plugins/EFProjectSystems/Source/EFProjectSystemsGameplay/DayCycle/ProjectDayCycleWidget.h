#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "DayCycle/ProjectDayCycleTypes.h"
#include "ProjectDayCycleWidget.generated.h"

class UProgressBar;
class UTextBlock;

UCLASS(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectDayCycleWidget : public UEFProjectThemedUserWidget
{
	GENERATED_BODY()

public:
	UProjectDayCycleWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable, Category = "Day Cycle")
	void ApplySnapshot(const FProjectDayCycleSnapshot& Snapshot);

private:
	void BuildWidgetTree();
	void UpdateSegment(UProgressBar* Segment, UTextBlock* Label, float Percent, bool bIsActive, const FLinearColor& ActiveColor);

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DayText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CurrentTimeText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MorningLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> AfternoonLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NightLabel;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> MorningBar;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> AfternoonBar;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> NightBar;
};
