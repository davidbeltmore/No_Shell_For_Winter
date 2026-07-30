#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Input/Reply.h"
#include "Types/SlateEnums.h"
#include "EFCharacterCreationTypes.h"
#include "EFCharacterCreationRootWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UHorizontalBox;
class UScrollBox;
class USizeBox;
class USlider;
class UTextBlock;
class UVerticalBox;
class UWidget;
class UEFCharacterCreationSubsystem;
class UEFCharacterCustomizationComponent;
class UEFMorphSliderWidget;

UCLASS()
class EFCHARACTERCREATIONRUNTIME_API UEFCharacterCreationRootWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForSession(UEFCharacterCreationSubsystem* InSubsystem, UEFCharacterCustomizationComponent* InCustomizationComponent);
	void RefreshFromComponent();
	bool TryGetThemeTabState(const UWidget* Widget, bool& bOutIsActive) const;

	UFUNCTION(BlueprintCallable, Category = "EF Character Creation|Automation")
	bool OpenTattooTabForAutomation();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

private:
	void BuildWidgetTree();
	void RefreshMorphList();
	void RefreshPresetList();
	void RefreshTabVisuals();
	void BuildInfoPanel();
	void UpdateColorControls();
	void UpdateColorPreview(UBorder* PreviewBorder, const FLinearColor& Color);
	FLinearColor MakeColorFromHSVSliders(USlider* HueSlider, USlider* SaturationSlider, USlider* ValueSlider) const;
	UHorizontalBox* CreateValueSliderRow(const FString& Label, TObjectPtr<USlider>& OutSlider, TObjectPtr<UEditableTextBox>& OutValueTextBox, float InitialMinValue, float InitialMaxValue, float InitialValue);
	void SetActiveCategory(FName NewCategory);
	bool IsPointerOverCameraViewport(const FVector2D& ScreenSpacePosition) const;
	void StopCameraInteraction();
	void ResetHairEditState();
	void UpdateHairTransformControls();
	void UpdateHairTransformControl(USlider* Slider, UEditableTextBox* ValueTextBox, float Value, float& InOutMinValue, float& InOutMaxValue, bool bUpdateTextBox);
	void ApplyHairLocationX(float NewValue);
	void ApplyHairLocationY(float NewValue);
	void ApplyHairLocationZ(float NewValue);
	void ApplyHairPitch(float NewValue);
	void ApplyHairYaw(float NewValue);
	void ApplyHairRoll(float NewValue);
	void ApplyHairScaleX(float NewValue);
	void ApplyHairScaleY(float NewValue);
	void ApplyHairScaleZ(float NewValue);
	bool TryParseNumericText(const FText& InText, float& OutValue) const;
	UObject* ResolveTattooShopSubsystem() const;
	void OpenTattooShopInCharacterCreation();
	void CloseTattooShopInCharacterCreation();
	void ApplyTattooLayoutSettings();
	void ApplyCenteredTattooWidgetLayout(UWidget* Widget, const FVector2D& OffsetFromCenter, const FVector2D& Size, const FVector2D& RenderScale, const FVector2D& RenderTranslation) const;

	UButton* CreateTextButton(const FString& Label, FLinearColor BackgroundColor, UTextBlock*& OutLabel) const;
	UHorizontalBox* CreateColorSliderRow(const FString& Label, USlider*& OutSlider, float InitialValue);
	UTextBlock* CreateSectionHeader(const FString& SectionLabel) const;
	UBorder* CreateColorPreviewSwatch(UVerticalBox* ParentBox);

	UFUNCTION()
	void HandleShowClothesChanged(bool bIsChecked);

	UFUNCTION()
	void HandlePauseAnimationChanged(bool bIsChecked);

	UFUNCTION()
	void HandleInfoTabClicked();

	UFUNCTION()
	void HandleHeadTabClicked();

	UFUNCTION()
	void HandleBodyTabClicked();

	UFUNCTION()
	void HandleSkinTabClicked();

	UFUNCTION()
	void HandleHairTabClicked();

	UFUNCTION()
	void HandleTattooTabClicked();

	UFUNCTION()
	void HandleResetSkinColorClicked();

	UFUNCTION()
	void HandleResetIrisColorClicked();

	UFUNCTION()
	void HandleRandomClicked();

	UFUNCTION()
	void HandleDefaultsClicked();

	UFUNCTION()
	void HandleStartGameClicked();

	UFUNCTION()
	void HandleBackClicked();

	UFUNCTION()
	void HandleSavePresetClicked();

	UFUNCTION()
	void HandleLoadPresetClicked();

	UFUNCTION()
	void HandlePresetSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleSearchTextChanged(const FText& NewText);

	UFUNCTION()
	void HandleCharacterNameTextChanged(const FText& NewText);

	UFUNCTION()
	void HandleGenderMaleClicked();

	UFUNCTION()
	void HandleGenderFemaleClicked();

	UFUNCTION()
	void HandleSkinHueChanged(float NewValue);

	UFUNCTION()
	void HandleSkinSaturationChanged(float NewValue);

	UFUNCTION()
	void HandleSkinValueChanged(float NewValue);

	UFUNCTION()
	void HandleIrisHueChanged(float NewValue);

	UFUNCTION()
	void HandleIrisSaturationChanged(float NewValue);

	UFUNCTION()
	void HandleIrisValueChanged(float NewValue);

	UFUNCTION()
	void HandleBodyMeshPreviousClicked();

	UFUNCTION()
	void HandleBodyMeshNextClicked();

	UFUNCTION()
	void HandleHairPreviousClicked();

	UFUNCTION()
	void HandleHairNextClicked();

	UFUNCTION()
	void HandleHairEditChanged(bool bIsChecked);

	UFUNCTION()
	void HandleHairEditWarningOkClicked();

	UFUNCTION()
	void HandleHairEditWarningCancelClicked();

	UFUNCTION()
	void HandleHairLocationXChanged(float NewValue);

	UFUNCTION()
	void HandleHairLocationYChanged(float NewValue);

	UFUNCTION()
	void HandleHairLocationZChanged(float NewValue);

	UFUNCTION()
	void HandleHairPitchChanged(float NewValue);

	UFUNCTION()
	void HandleHairYawChanged(float NewValue);

	UFUNCTION()
	void HandleHairRollChanged(float NewValue);

	UFUNCTION()
	void HandleHairScaleXChanged(float NewValue);

	UFUNCTION()
	void HandleHairScaleYChanged(float NewValue);

	UFUNCTION()
	void HandleHairScaleZChanged(float NewValue);

	UFUNCTION()
	void HandleHairLocationXTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleHairLocationYTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleHairLocationZTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleHairPitchTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleHairYawTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleHairRollTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleHairScaleXTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleHairScaleYTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleHairScaleZTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod);

	void HandleMorphValueChanged(const FMorphSliderEntry& Entry, float NewValue);
	void HandleMorphResetRequested(const FMorphSliderEntry& Entry);

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UEFCharacterCreationSubsystem> CharacterCreationSubsystem;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEFCharacterCustomizationComponent> CustomizationComponent;

	FName ActiveCategory = TEXT("Info");
	FString SelectedPresetName;

	UPROPERTY(Transient)
	TObjectPtr<UCheckBox> ShowClothesCheckBox;

	UPROPERTY(Transient)
	TObjectPtr<UCheckBox> PauseAnimationCheckBox;

	UPROPERTY(Transient)
	TObjectPtr<UButton> InfoTabButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> HeadTabButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> BodyTabButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> SkinTabButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> HairTabButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> TattooTabButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> InfoTabLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeadTabLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BodyTabLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SkinTabLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HairTabLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TattooTabLabel;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> SearchTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> CharacterNameTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UButton> GenderMaleButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> GenderFemaleButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GenderMaleLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GenderFemaleLabel;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> MorphScrollBox;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> TattooHostFrame;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> TattooHostCanvas;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> TattooShopHostBox;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> TattooAssetPreviewerHostBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ErrorTextBlock;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> PresetNameTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> PresetComboBox;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> RandomDefaultsRow;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> PresetActionsRow;

	UPROPERTY(Transient)
	TObjectPtr<UButton> LoadPresetButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> SavePresetButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RandomButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> DefaultsButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> StartGameButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> BackButton;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> CameraInteractionBorder;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> SkinPreviewBorder;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> IrisPreviewBorder;

	UPROPERTY(Transient)
	TObjectPtr<USlider> SkinHueSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> SkinSaturationSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> SkinValueSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> IrisHueSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> IrisSaturationSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> IrisValueSlider;

	UPROPERTY(Transient)
	TObjectPtr<UCheckBox> HairEditCheckBox;

	UPROPERTY(Transient)
	TObjectPtr<USlider> HairLocationXSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> HairLocationYSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> HairLocationZSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> HairPitchSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> HairYawSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> HairRollSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> HairScaleXSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> HairScaleYSlider;

	UPROPERTY(Transient)
	TObjectPtr<USlider> HairScaleZSlider;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> HairLocationXTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> HairLocationYTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> HairLocationZTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> HairPitchTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> HairYawTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> HairRollTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> HairScaleXTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> HairScaleYTextBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> HairScaleZTextBox;

	float HairLocationXMinValue = -30.0f;
	float HairLocationXMaxValue = 30.0f;
	float HairLocationYMinValue = -30.0f;
	float HairLocationYMaxValue = 30.0f;
	float HairLocationZMinValue = -30.0f;
	float HairLocationZMaxValue = 30.0f;
	float HairPitchMinValue = -90.0f;
	float HairPitchMaxValue = 90.0f;
	float HairYawMinValue = -90.0f;
	float HairYawMaxValue = 90.0f;
	float HairRollMinValue = -90.0f;
	float HairRollMaxValue = 90.0f;
	float HairScaleXMinValue = 0.1f;
	float HairScaleXMaxValue = 2.0f;
	float HairScaleYMinValue = 0.1f;
	float HairScaleYMaxValue = 2.0f;
	float HairScaleZMinValue = 0.1f;
	float HairScaleZMaxValue = 2.0f;

	bool bIsOrbitDragging = false;
	bool bIsPanDragging = false;
	bool bIsRefreshingColorControls = false;
	bool bIsRefreshingIdentityControls = false;
	bool bIsRefreshingHairTransformControls = false;
	bool bIsUpdatingHairEditCheckBox = false;
	bool bHairEditRequested = false;
	bool bHairEditUnlocked = false;
	FVector2D LastPointerScreenPosition = FVector2D::ZeroVector;
};

