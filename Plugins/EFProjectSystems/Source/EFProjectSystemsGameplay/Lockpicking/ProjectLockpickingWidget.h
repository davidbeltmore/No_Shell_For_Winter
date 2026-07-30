#pragma once

#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "ProjectLockpickingWidget.generated.h"

class APawn;
class UBorder;
class UCanvasPanel;
class UProjectLockpickableComponent;
class USizeBox;
class UTextBlock;
class UWidgetTree;

DECLARE_MULTICAST_DELEGATE(FProjectLockpickingConfirmRequested);

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectLockpickingWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectLockpickingWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	void Configure(UProjectLockpickableComponent* InLockpickableComponent, APawn* InPawn);
	void FocusLockpickingWidget();
	void ShowFeedback(const FText& FeedbackText, const FLinearColor& FeedbackColor);

	UFUNCTION(BlueprintPure, Category = "Lockpicking|Minigame")
	float GetDisplayedPulseValue() const { return LastPulseValue; }

	UFUNCTION(BlueprintPure, Category = "Lockpicking|Minigame")
	bool IsDisplayedPulseInTargetRange() const { return bLastPulseInTargetRange; }

	FProjectLockpickingConfirmRequested OnConfirmRequested;

private:
	enum class EFeedbackColorRole : uint8
	{
		Secondary,
		Positive,
		Negative
	};

	void BuildWidgetTree();
	bool BuildDefaultLockpickingTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void RefreshVisualState();
	void RefreshTrackGeometry(float PulseValue);
	EFeedbackColorRole ClassifyFeedbackColor(
		const FLinearColor& FeedbackColor) const;
	FLinearColor ResolveActiveFeedbackColor() const;
	FSlateFontInfo MakeTitleFont(int32 Size) const;
	FSlateFontInfo MakeBodyFont(int32 Size) const;

protected:
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Minigame")
	TObjectPtr<UCanvasPanel> LockpickingRootCanvas;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Minigame")
	TObjectPtr<USizeBox> LockpickingPanelSizeBox;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Minigame")
	TObjectPtr<UCanvasPanel> LockpickingPanelCanvas;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Minigame")
	TObjectPtr<UBorder> LockpickingBackdrop;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Minigame")
	TObjectPtr<UBorder> LockpickingPanelBorder;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Minigame")
	TObjectPtr<UBorder> LockpickingTrackOuter;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Minigame")
	TObjectPtr<UBorder> LockpickingTrackInner;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Minigame")
	TObjectPtr<UBorder> LockpickingTargetRange;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Minigame")
	TObjectPtr<UBorder> LockpickingPulseMarker;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Minigame")
	TObjectPtr<UTextBlock> LockpickingTitleText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Minigame")
	TObjectPtr<UTextBlock> LockpickingHintText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Minigame")
	TObjectPtr<UTextBlock> LockpickingFeedbackText;

	UFUNCTION(BlueprintImplementableEvent, Category = "Lockpicking|Minigame")
	void OnLockpickingVisualStateChanged(float PulseValue, bool bIsInTargetRange);

	UFUNCTION(BlueprintImplementableEvent, Category = "Lockpicking|Minigame")
	void OnLockpickingFeedbackChanged(const FText& FeedbackText, const FLinearColor& FeedbackColor);

private:
	UPROPERTY(Transient)
	TObjectPtr<UProjectLockpickableComponent> LockpickableComponent;

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
