#pragma once

#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "ProjectLockpickPromptWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UTextBlock;
class UWidgetTree;

DECLARE_MULTICAST_DELEGATE(FProjectLockpickPromptOptionRequested);

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectLockpickPromptWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectLockpickPromptWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	void Configure(bool bInCanLockpick, int32 InLockpickCount, const FText& InRequiredItemDisplayName = FText::GetEmpty());
	void FocusPromptWidget();

	UFUNCTION(BlueprintPure, Category = "Lockpicking|Prompt")
	int32 GetSelectedOption() const { return SelectedOption; }

	UFUNCTION(BlueprintPure, Category = "Lockpicking|Prompt")
	bool CanUseLockpickOption() const { return bCanLockpick; }

	FProjectLockpickPromptOptionRequested OnLockpickSelected;
	FProjectLockpickPromptOptionRequested OnCancelSelected;

private:
	UFUNCTION()
	void HandleLockpickClicked();

	UFUNCTION()
	void HandleCancelClicked();

	void BuildWidgetTree();
	bool BuildDefaultPromptTree(UWidgetTree* TargetWidgetTree);
	void RefreshOptionState();
	void RefreshSelectionVisualState();
	void SetSelectedOption(int32 InSelectedOption);
	void ConfirmSelectedOption();
	FSlateFontInfo MakeTitleFont(int32 Size) const;
	FSlateFontInfo MakeBodyFont(int32 Size) const;

protected:
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Prompt")
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Prompt")
	TObjectPtr<UBorder> PanelBorder;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Prompt")
	TObjectPtr<UBorder> LockpickOptionBorder;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Prompt")
	TObjectPtr<UBorder> CancelOptionBorder;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Prompt")
	TObjectPtr<UBorder> LockpickSelectionFrame;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Prompt")
	TObjectPtr<UBorder> CancelSelectionFrame;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Prompt")
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Prompt")
	TObjectPtr<UButton> LockpickButton;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Prompt")
	TObjectPtr<UButton> CancelButton;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Prompt")
	TObjectPtr<UTextBlock> LockpickText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Prompt")
	TObjectPtr<UTextBlock> CancelText;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Lockpicking|Prompt")
	TObjectPtr<UTextBlock> StatusText;

	UFUNCTION(BlueprintImplementableEvent, Category = "Lockpicking|Prompt")
	void OnPromptSelectionChanged(int32 NewSelectedOption, bool bCanUseLockpick);

private:
	int32 SelectedOption = 0;
	int32 LockpickCount = 0;
	FText RequiredItemDisplayName;
	bool bCanLockpick = false;
	bool bUsingNativeFallbackTree = false;
};
