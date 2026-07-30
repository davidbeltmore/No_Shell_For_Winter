#pragma once

#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "Survival/ProjectSurvivalStatusTypes.h"
#include "ProjectSurvivalStatusWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UImage;
class UOverlay;
class USizeBox;
class UTextBlock;
class UWidgetTree;
class UProjectSurvivalStatusComponent;
class UProjectSurvivalStatusSlotWidget;
class UProjectSurvivalStatusSlotsGlobalWidget;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalStatusSlotWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectSurvivalStatusSlotWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;
	virtual void SetCodeWidgetDesignerBuildContext(const FCodeWidgetDesignerBuildContext& InBuildContext) override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void ApplyStatusSnapshot(const FProjectSurvivalStatusSnapshot& InSnapshot);

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	FProjectSurvivalStatusSnapshot GetCurrentStatusSnapshot() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Survival|Status")
	void OnStatusSlotDataApplied(const FProjectSurvivalStatusSnapshot& Snapshot);

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;

	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void ApplyStatusVisualData();

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> SlotRootCanvas;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> RootOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> SlotBackgroundBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> IconSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> IconImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FallbackText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DescriptionText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DurationText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DamageText;

	FProjectSurvivalStatusSnapshot CurrentSnapshot;
	FCodeWidgetDesignerBuildContext DesignerBuildContext;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalStatusSlotsGlobalWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectSurvivalStatusSlotsGlobalWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;
	virtual void SetCodeWidgetDesignerBuildContext(const FCodeWidgetDesignerBuildContext& InBuildContext) override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void ApplyStatusSnapshots(
		const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots,
		int32 InOverflowCount,
		TSubclassOf<UProjectSurvivalStatusSlotWidget> DefaultSlotWidgetClass);

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void ClearStatusSlots();

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	int32 GetOverflowCount() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Survival|Status")
	void OnStatusSlotsApplied(const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots, int32 InOverflowCount);

protected:
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	bool DoesLayoutNeedRebuild(const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots) const;
	void RebuildStatusSlots(
		const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots,
		TSubclassOf<UProjectSurvivalStatusSlotWidget> DefaultSlotWidgetClass);
	void ApplySlotLayout(
		UProjectSurvivalStatusSlotWidget* SlotWidget,
		const FProjectSurvivalStatusSnapshot& Snapshot,
		int32 Index,
		const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots) const;
	void SyncOverflowText(const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots);
	TSubclassOf<UProjectSurvivalStatusSlotWidget> ResolveSlotWidgetClassForStatus(
		FName StatusName,
		TSubclassOf<UProjectSurvivalStatusSlotWidget> DefaultSlotWidgetClass) const;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> SlotsCanvas;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> OverflowText;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UProjectSurvivalStatusSlotWidget>> SlotWidgetsByName;

	UPROPERTY(Transient)
	TArray<FName> CachedStatusOrder;

	FCodeWidgetDesignerBuildContext DesignerBuildContext;
	int32 OverflowCount = 0;
	bool bUsingNativeFallbackTree = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalStatusWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectSurvivalStatusWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void SetCodeWidgetDesignerBuildContext(const FCodeWidgetDesignerBuildContext& InBuildContext) override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;
	virtual bool GatherCodeWidgetDesignerConversionManifest(FCodeWidgetDesignerConversionManifest& OutManifest) const override;
	virtual void GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const override;

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void SetStatusComponent(UProjectSurvivalStatusComponent* InStatusComponent);

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void RefreshDisplay();

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void SetHudVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	bool IsHudVisible() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Survival|Status")
	void OnStatusHudDataApplied(
		const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots,
		int32 OverflowCount,
		bool bBlackoutActive);

protected:
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void EnsureSlotsGlobalWidget();
	void ApplyHudVisibility();
	void ApplyBlackoutVisibility(bool bBlackoutActive);
	TSubclassOf<UProjectSurvivalStatusSlotsGlobalWidget> ResolveSlotsGlobalWidgetClass() const;
	TSubclassOf<UProjectSurvivalStatusSlotWidget> ResolveSlotWidgetClass() const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Survival|Status")
	int32 ZOrder;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Widgets")
	TSoftClassPtr<UProjectSurvivalStatusSlotsGlobalWidget> SlotsGlobalWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Widgets")
	TSoftClassPtr<UProjectSurvivalStatusSlotWidget> SlotWidgetClass;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BlackoutOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectSurvivalStatusSlotsGlobalWidget> SlotsGlobalWidget;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> DesignerPreviewCanvas;

private:
	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalStatusComponent> StatusComponent;

	FCodeWidgetDesignerBuildContext DesignerBuildContext;
	bool bHudVisible = false;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};
