#pragma once

#include "CoreMinimal.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "EFProjectThemedUserWidget.h"
#include "TattooShop/UI/ProjectTattooShopUITypes.h"
#include "ProjectTattooShopEditorWidget.generated.h"

class UBorder;
class UButton;
class UCheckBox;
class UComboBoxString;
class UProjectTattooShopColorPickerWidget;
class UScrollBox;
class USlider;
class UTextBlock;
class UVerticalBox;
class UWidgetTree;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FProjectTattooShopEditorChangedSignature,
	FProjectTattooShopEditorModel,
	EditorModel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FProjectTattooShopEditorActionSignature,
	FGuid,
	TattooId);

/** Designer-editable editor panel for one manual SkinnedDecal tattoo. */
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTattooShopEditorWidget
	: public UEFProjectThemedUserWidget
	, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectTattooShopEditorWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void ApplyEditorModel(const FProjectTattooShopEditorModel& InModel);

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void SetStatusText(const FText& InStatusText);

	UFUNCTION(BlueprintPure, Category = "Tattoo Shop|UI")
	FProjectTattooShopEditorModel GetEditorModel() const { return EditorModel; }

	UPROPERTY(BlueprintAssignable, Category = "Tattoo Shop|UI")
	FProjectTattooShopEditorChangedSignature OnPreviewChanged;

	UPROPERTY(BlueprintAssignable, Category = "Tattoo Shop|UI")
	FProjectTattooShopEditorActionSignature OnAcceptRequested;

	UPROPERTY(BlueprintAssignable, Category = "Tattoo Shop|UI")
	FProjectTattooShopEditorActionSignature OnCancelRequested;

	UFUNCTION(BlueprintImplementableEvent, Category = "Tattoo Shop|UI")
	void OnEditorModelApplied(const FProjectTattooShopEditorModel& AppliedModel);

protected:
	void BuildWidgetTreeIfNeeded();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void BindControls();
	void PopulatePlacementOptions();
	void RefreshControls();
	void NotifyPreviewChanged();
	void AddNumericRow(
		UWidgetTree* TargetWidgetTree,
		UVerticalBox* Parent,
		const FText& Label,
		FName RowName,
		float MinValue,
		float MaxValue,
		USlider*& OutSlider,
		UTextBlock*& OutValueText);
	static FString PlacementPresetToString(EProjectAutomaticTattooPlacementPreset Preset);
	static bool TryStringToPlacementPreset(const FString& Value, EProjectAutomaticTattooPlacementPreset& OutPreset);

	UFUNCTION()
	void HandlePlacementChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleOffsetXChanged(float Value);

	UFUNCTION()
	void HandleOffsetYChanged(float Value);

	UFUNCTION()
	void HandleSizeChanged(float Value);

	UFUNCTION()
	void HandleRotationChanged(float Value);

	UFUNCTION()
	void HandleProjectionChanged(float Value);

	UFUNCTION()
	void HandleOpacityChanged(float Value);

	UFUNCTION()
	void HandleEnabledChanged(bool bIsChecked);

	UFUNCTION()
	void HandleTintEnabledChanged(bool bIsChecked);

	UFUNCTION()
	void HandleColorChanged(FLinearColor Color);

	UFUNCTION()
	void HandleAcceptClicked();

	UFUNCTION()
	void HandleCancelClicked();

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BackgroundBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ReadOnlyText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> EditorScrollBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> PlacementComboBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> OffsetXSlider;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> OffsetYSlider;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> SizeSlider;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> RotationSlider;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> ProjectionSlider;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> OpacitySlider;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> OffsetXValueText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> OffsetYValueText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SizeValueText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RotationValueText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ProjectionValueText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> OpacityValueText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> EnabledCheckBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> TintEnabledCheckBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectTattooShopColorPickerWidget> ColorPicker;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> AcceptButton;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelButton;

private:
	UPROPERTY(Transient)
	FProjectTattooShopEditorModel EditorModel;

	FText CurrentStatusText;
	bool bRefreshingControls = false;
	bool bUsingNativeFallbackTree = false;
};
