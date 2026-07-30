#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EFCharacterCreationTypes.h"
#include "EFMorphSliderWidget.generated.h"

class UButton;
class UHorizontalBox;
class USlider;
class UTextBlock;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMorphSliderValueChangedNative, const FMorphSliderEntry&, float);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMorphSliderResetRequestedNative, const FMorphSliderEntry&);

UCLASS()
class EFCHARACTERCREATIONRUNTIME_API UEFMorphSliderWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeFromEntry(const FMorphSliderEntry& InEntry, float InCurrentValue);
	void SetCurrentValue(float InCurrentValue);

	const FMorphSliderEntry& GetEntry() const { return Entry; }

	FOnMorphSliderValueChangedNative OnMorphValueChanged;
	FOnMorphSliderResetRequestedNative OnMorphResetRequested;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

private:
	void BuildWidgetTree();
	void BindDesignerWidgets();
	void EnsureWarningTextWidget();
	void BindCallbacks();
	void ApplyLayoutStyling();
	void RefreshVisuals();
	void RefreshDeformationWarning();

	UFUNCTION()
	void HandleSliderValueChanged(float NewValue);

	UFUNCTION()
	void HandleResetClicked();

private:
	UPROPERTY(Transient)
	FMorphSliderEntry Entry;

	float CurrentValue = 0.0f;

	UPROPERTY(Transient)
	bool bIsRefreshingVisuals = false;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MorphNameText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> ValueSlider;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ValueText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WarningText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ResetButton;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResetButtonText;
};

