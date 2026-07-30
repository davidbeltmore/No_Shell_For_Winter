#pragma once

#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "GameplayTagContainer.h"
#include "ProjectTrainingLockpickWidget.generated.h"

class APawn;
class UACFTrainingComponent;
class UBorder;
class UButton;
class UCanvasPanel;
class USizeBox;
class UTextBlock;
class UWidgetTree;

DECLARE_MULTICAST_DELEGATE(FProjectTrainingConfirmRequested);
DECLARE_MULTICAST_DELEGATE(FProjectTrainingCancelRequested);

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTrainingLockpickWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectTrainingLockpickWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	void Configure(UACFTrainingComponent* InTrainingComponent, APawn* InPawn);
	void FocusTrainingWidget();
	void ShowFeedback(const FText& FeedbackText, const FLinearColor& FeedbackColor);

	UFUNCTION(BlueprintPure, Category = "Training|Minigame")
	float GetDisplayedPulseValue() const { return LastPulseValue; }

	UFUNCTION(BlueprintPure, Category = "Training|Minigame")
	bool IsDisplayedPulseInTargetRange() const { return bLastPulseInTargetRange; }

	FProjectTrainingConfirmRequested OnConfirmRequested;
	FProjectTrainingCancelRequested OnCancelRequested;

private:
	enum class EFeedbackColorRole : uint8
	{
		Secondary,
		Positive,
		Negative
	};

	void BuildWidgetTree();
	bool BuildDefaultTrainingTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void RefreshVisualState();
	void RefreshTrackGeometry(float PulseValue);
	EFeedbackColorRole ClassifyFeedbackColor(
		const FLinearColor& FeedbackColor) const;
	FLinearColor ResolveActiveFeedbackColor() const;
	FSlateFontInfo MakeTitleFont(int32 Size) const;
	FSlateFontInfo MakeBodyFont(int32 Size) const;
	static FName ResolveAttributeLeafName(const FGameplayTag& AttributeTag);

	UFUNCTION()
	void HandleConfirmClicked();

	UFUNCTION()
	void HandleCancelClicked();

protected:
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UCanvasPanel> TrainingRootCanvas;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<USizeBox> TrainingPanelSizeBox;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UCanvasPanel> TrainingPanelCanvas;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UBorder> TrainingBackdrop;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UBorder> TrainingPanelBorder;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UBorder> TrainingTrackOuter;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UBorder> TrainingTrackInner;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UBorder> TrainingTargetRange;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UBorder> TrainingPulseMarker;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UTextBlock> TrainingTitleText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UTextBlock> TrainingStatText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UTextBlock> TrainingHintText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UTextBlock> TrainingFeedbackText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UButton> TrainingConfirmButton;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Training|Minigame")
	TObjectPtr<UButton> TrainingCancelButton;

	UFUNCTION(BlueprintImplementableEvent, Category = "Training|Minigame")
	void OnTrainingVisualStateChanged(float PulseValue, bool bIsInTargetRange);

	UFUNCTION(BlueprintImplementableEvent, Category = "Training|Minigame")
	void OnTrainingFeedbackChanged(const FText& FeedbackText, const FLinearColor& FeedbackColor);

private:
	UPROPERTY(Transient)
	TObjectPtr<UACFTrainingComponent> TrainingComponent;

	UPROPERTY(Transient)
	TObjectPtr<APawn> InteractingPawn;

	FText ActiveFeedbackText;
	FLinearColor ActiveFeedbackColor = FLinearColor::White;
	EFeedbackColorRole ActiveFeedbackColorRole =
		EFeedbackColorRole::Secondary;
	float LastPulseValue = 0.0f;
	bool bLastPulseInTargetRange = false;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};
