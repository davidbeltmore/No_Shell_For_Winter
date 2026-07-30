#pragma once

#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "InnerDoctrine/ProjectInnerDoctrineAttributeCardWidget.h"
#include "InnerDoctrine/ProjectInnerDoctrineTypes.h"
#include "ProjectInnerDoctrineWidget.generated.h"

class UCanvasPanel;
class UCanvasPanelSlot;
class UOverlay;
class UProjectInnerDoctrineAttributeCardsGlobalWidget;
class UProjectInnerDoctrineAttributesPanelWidget;
class UProjectInnerDoctrineComponent;
class UWidgetTree;
class UWrapBox;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectInnerDoctrineWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;
	virtual void GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const override;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	void SetInnerDoctrineComponent(UProjectInnerDoctrineComponent* InComponent);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	void RefreshDisplay();

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	void SetHudVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI")
	bool IsHudVisible() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI")
	FProjectInnerDoctrineSnapshot GetCachedSnapshot() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI")
	int32 GetRenderedAttributeCardCount() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Inner Doctrine|UI")
	void OnDoctrineAttributesRebuilt(int32 VisibleCardCount);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inner Doctrine|UI")
	int32 ZOrder;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Widgets")
	TSoftClassPtr<UProjectInnerDoctrineAttributeCardWidget> AttributeCardWidgetClass;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> PanelHost;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineAttributesPanelWidget> AttributesPanelWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectInnerDoctrineComponent> InnerDoctrineComponent;

	UPROPERTY(Transient)
	FProjectInnerDoctrineSnapshot CachedSnapshot;

protected:
	void ApplyHudVisibility();
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void EnsureAttributesPanelWidget();
	void SyncAttributesContainer();
	void ApplyPanelState(int32 CardCount);
	float CalculatePanelHeight(int32 CardCount) const;
	TArray<FProjectInnerDoctrineAttributeCardDisplayData> BuildResolvedCardData() const;
	bool DoesCardLayoutNeedRebuild(const TArray<FProjectInnerDoctrineAttributeCardDisplayData>& InCardData) const;
	void RebuildAttributeCards(const TArray<FProjectInnerDoctrineAttributeCardDisplayData>& InCardData);
	TSubclassOf<UProjectInnerDoctrineAttributesPanelWidget> ResolveAttributesPanelWidgetClass() const;
	TSubclassOf<UProjectInnerDoctrineAttributeCardWidget> ResolveAttributeCardWidgetClass() const;
	TSubclassOf<UProjectInnerDoctrineAttributeCardWidget> ResolveGlobalAttributeCardWidgetClass() const;
	TSubclassOf<UProjectInnerDoctrineAttributeCardWidget> ResolveAttributeCardWidgetClassForData(const FProjectInnerDoctrineAttributeCardDisplayData& CardData) const;
	UClass* ResolveNativeCardClassForAttribute(FName AttributeName) const;

private:
	UCanvasPanelSlot* PanelHostSlot = nullptr;
	TObjectPtr<UProjectInnerDoctrineAttributeCardsGlobalWidget> AttributesCardsGlobalWidget;
	TObjectPtr<UWrapBox> AttributesWrapBox;
	TMap<FName, TObjectPtr<UProjectInnerDoctrineAttributeCardWidget>> CardWidgetsByName;
	TArray<FName> CachedCardOrder;
	float CurrentPanelHeight = 206.0f;
	bool bHudVisible = false;
	bool bUsingNativeFallbackTree = false;
};
