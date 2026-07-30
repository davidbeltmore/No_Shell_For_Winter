#pragma once

#include "EFProjectThemedUserWidget.h"
#include "ProjectCharacterBackgroundOptionEntryWidget.generated.h"

class UBorder;
class UButton;
class USizeBox;
class UTextBlock;
class UVerticalBox;

DECLARE_MULTICAST_DELEGATE_OneParam(FProjectCharacterBackgroundOptionSelectedEvent, FName);

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCharacterBackgroundOptionEntryWidget : public UEFProjectThemedUserWidget
{
	GENERATED_BODY()

public:
	UProjectCharacterBackgroundOptionEntryWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	void SetOption(FName InOptionID, const FText& InTitle, const FText& InSubtitle, bool bInSelected);
	void SetEntryHeight(float InEntryHeight);
	void SetSelected(bool bInSelected);

	FName GetOptionID() const { return OptionID; }

	FProjectCharacterBackgroundOptionSelectedEvent OnOptionSelected;

private:
	void BuildWidgetTree();
	void InitializeVisualTree();

	UFUNCTION()
	void HandleClicked();

private:
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RootButton;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> ContentBorder;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> TextBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SubtitleText;

	FName OptionID = NAME_None;
	float EntryHeightOverride = 0.0f;
	bool bSelected = false;
	bool bVisualTreeInitialized = false;
};
