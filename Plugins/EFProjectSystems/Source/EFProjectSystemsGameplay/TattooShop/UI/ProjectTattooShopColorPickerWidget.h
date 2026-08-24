#pragma once

#include "CoreMinimal.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "EFProjectThemedUserWidget.h"
#include "ProjectTattooShopColorPickerWidget.generated.h"

class UBorder;
class USlider;
class UTextBlock;
class UVerticalBox;
class UWidgetTree;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FProjectTattooShopColorChangedSignature,
	FLinearColor,
	Color);

/** Small reusable RGB picker. Alpha is deliberately edited as Tattoo opacity. */
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTattooShopColorPickerWidget
	: public UEFProjectThemedUserWidget
	, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectTattooShopColorPickerWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void SetColor(FLinearColor InColor, bool bNotify = false);

	UFUNCTION(BlueprintPure, Category = "Tattoo Shop|UI")
	FLinearColor GetColor() const { return CurrentColor; }

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void SetReadOnly(bool bInReadOnly);

	UPROPERTY(BlueprintAssignable, Category = "Tattoo Shop|UI")
	FProjectTattooShopColorChangedSignature OnColorChanged;

	UFUNCTION(BlueprintImplementableEvent, Category = "Tattoo Shop|UI")
	void OnColorApplied(FLinearColor Color, bool bIsReadOnly);

protected:
	void BuildWidgetTreeIfNeeded();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void BindControls();
	void RefreshVisuals();
	void BroadcastColor();
	void AddChannelRow(UWidgetTree* TargetWidgetTree, UVerticalBox* Parent, const FText& Label, FName RowName, USlider*& OutSlider, UTextBlock*& OutValueText);

	UFUNCTION()
	void HandleRedChanged(float Value);

	UFUNCTION()
	void HandleGreenChanged(float Value);

	UFUNCTION()
	void HandleBlueChanged(float Value);

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> RootBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ColorPreviewBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> RedSlider;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> GreenSlider;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USlider> BlueSlider;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RedValueText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GreenValueText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BlueValueText;

private:
	UPROPERTY(Transient)
	FLinearColor CurrentColor = FLinearColor::White;

	bool bReadOnly = false;
	bool bRefreshingControls = false;
	bool bUsingNativeFallbackTree = false;
};
