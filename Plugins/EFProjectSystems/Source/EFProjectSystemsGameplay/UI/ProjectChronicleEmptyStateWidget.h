#pragma once

#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "ProjectChronicleEmptyStateWidget.generated.h"

class UBorder;
class UOverlay;
class USizeBox;
class UTextBlock;
class UWidgetTree;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChronicleEmptyStateWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectChronicleEmptyStateWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Chronicle")
	void ApplyEmptyState(bool bInExpanded, const FText& InMessage, float InWidth);

	UFUNCTION(BlueprintImplementableEvent, Category = "Chronicle")
	void OnChronicleEmptyStateApplied(bool bInExpanded, const FText& Message);

protected:
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void RefreshVisuals();

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> RootOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BackgroundBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MessageText;

private:
	FText CurrentMessage;
	float CurrentWidth = 520.0f;
	bool bExpanded = false;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChronicleNormalEmptyStateWidget : public UProjectChronicleEmptyStateWidget
{
	GENERATED_BODY()
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChronicleExpandedEmptyStateWidget : public UProjectChronicleEmptyStateWidget
{
	GENERATED_BODY()
};
