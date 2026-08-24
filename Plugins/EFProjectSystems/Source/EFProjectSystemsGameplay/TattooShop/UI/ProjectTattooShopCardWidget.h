#pragma once

#include "CoreMinimal.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "EFProjectThemedUserWidget.h"
#include "TattooShop/UI/ProjectTattooShopUITypes.h"
#include "ProjectTattooShopCardWidget.generated.h"

class UBorder;
class UButton;
class UImage;
class USizeBox;
class UTextBlock;
class UWidgetTree;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FProjectTattooShopCardSelectedSignature,
	FProjectTattooShopCardData,
	CardData);

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTattooShopCardWidget
	: public UEFProjectThemedUserWidget
	, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectTattooShopCardWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void ApplyCardData(const FProjectTattooShopCardData& InCardData);

	UFUNCTION(BlueprintCallable, Category = "Tattoo Shop|UI")
	void SetSelected(bool bInSelected);

	UFUNCTION(BlueprintPure, Category = "Tattoo Shop|UI")
	FProjectTattooShopCardData GetCardData() const { return CardData; }

	UPROPERTY(BlueprintAssignable, Category = "Tattoo Shop|UI")
	FProjectTattooShopCardSelectedSignature OnSelected;

	UFUNCTION(BlueprintImplementableEvent, Category = "Tattoo Shop|UI")
	void OnCardDataApplied(const FProjectTattooShopCardData& AppliedCardData);

protected:
	void BuildWidgetTreeIfNeeded();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void BindControls();
	void RefreshVisuals();

	UFUNCTION()
	void HandleCardClicked();

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> SelectionBorder;

	/** Designer-owned selection affordance kept separate from the tattoo thumbnail. */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> SelectedFrameBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CardButton;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> ThumbnailImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleText;

private:
	UPROPERTY(Transient)
	FProjectTattooShopCardData CardData;

	bool bUsingNativeFallbackTree = false;
};
