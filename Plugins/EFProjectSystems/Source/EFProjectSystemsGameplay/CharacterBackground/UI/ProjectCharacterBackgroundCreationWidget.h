#pragma once

#include "EFProjectThemedUserWidget.h"
#include "CharacterBackground/ProjectCharacterBackgroundTypes.h"
#include "InputCoreTypes.h"
#include "ProjectCharacterBackgroundCreationWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UHorizontalBox;
class UImage;
class UOverlay;
class UProjectCharacterBackgroundComponent;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidget;

DECLARE_MULTICAST_DELEGATE_ThreeParams(FProjectCharacterBackgroundConfirmRequestedEvent, FName /* BackstoryID */, FName /* ProfessionID */, bool /* bChangingExistingProfile */);

enum class EProjectCharacterBackgroundCreationStep : uint8
{
	Backstory,
	Profession,
	Summary
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCharacterBackgroundCreationWidget : public UEFProjectThemedUserWidget
{
	GENERATED_BODY()

public:
	UProjectCharacterBackgroundCreationWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	void InitializeForBackground(
		UProjectCharacterBackgroundComponent* InBackgroundComponent,
		bool bInHasExistingProfile,
		FName InExistingBackstoryID,
		FName InExistingProfessionID,
		bool bOpenAtSummary);
	void RefreshDisplay();
	void FocusCreationWidget();

	FName GetSelectedBackstoryID() const;
	FName GetSelectedProfessionID() const;
	bool IsChangingExistingProfile() const;

	FProjectCharacterBackgroundConfirmRequestedEvent OnConfirmRequested;

private:
	void BuildWidgetTree();
	void InitializeVisualTree();
	void RebuildOptionList();
	void RefreshStepHeader();
	void RefreshPreviewPanel();
	void RefreshEffectPanel();
	void RefreshDescriptionPanel();
	void RefreshFooter();
	void SetStep(EProjectCharacterBackgroundCreationStep InStep);
	void NavigateSelection(int32 Delta);
	void SelectCurrentOption();
	void ConfirmOrAdvance();
	void GoBack();
	bool HandleMenuKey(const FKey& Key);
	FText BuildBackstorySubtitle(const FProjectCharacterBackstoryData& Data) const;
	FText BuildProfessionSubtitle(const FProjectCharacterProfessionData& Data) const;
	FText BuildModifierLine(FName AttributeID, int32 Delta) const;
	FText BuildGainLine(FName AttributeID, float Multiplier) const;
	UTexture2D* ResolveTextureForAttribute(FName AttributeID, bool bWatermark) const;
	UTexture2D* ResolvePreviewImageTexture();
	void RebuildPreviewIcons(const TArray<FName>& AttributeIDs);
	void AddEffectRow(const FText& Text, bool bNegative);
	void ApplyWidgetAdjustment(UWidget* Widget, const struct FProjectCharacterBackgroundWidgetAdjustment& Adjustment) const;
	FSlateFontInfo MakeTitleFont(int32 Size) const;
	FSlateFontInfo MakeBodyFont(int32 Size) const;

	UFUNCTION()
	void HandlePrimaryClicked();

	UFUNCTION()
	void HandleBackClicked();

	void HandleOptionSelected(FName OptionID);

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UProjectCharacterBackgroundComponent> BackgroundComponent;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> BackdropBorder;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> FrameBorder;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ContentBox;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> HeaderRow;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StepText;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> BodyRow;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> OptionSizeBox;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> OptionListBorder;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> OptionScrollBox;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> OptionListBox;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> PreviewBorder;

	UPROPERTY(Transient)
	TObjectPtr<UOverlay> PreviewOverlay;

	UPROPERTY(Transient)
	TObjectPtr<UImage> PreviewWatermarkImage;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> PreviewContentBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PreviewTitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PreviewSubtitleText;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> PreviewIconBox;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> PreviewImageSizeBox;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> PreviewImageBorder;

	UPROPERTY(Transient)
	TObjectPtr<UImage> PreviewImage;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> EffectSizeBox;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> EffectBorder;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> EffectBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EffectTitleText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> DescriptionBorder;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> DescriptionSizeBox;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> DescriptionRow;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DescriptionText;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CachedPreviewImageTexture;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> FooterRow;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FooterStatusText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> BackButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BackButtonText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> PrimaryButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PrimaryButtonText;

	TArray<FName> CurrentOptionIDs;
	EProjectCharacterBackgroundCreationStep CurrentStep = EProjectCharacterBackgroundCreationStep::Backstory;
	int32 CurrentOptionIndex = INDEX_NONE;
	FName ExistingBackstoryID = NAME_None;
	FName ExistingProfessionID = NAME_None;
	bool bHasExistingProfile = false;
	bool bVisualTreeInitialized = false;
};
