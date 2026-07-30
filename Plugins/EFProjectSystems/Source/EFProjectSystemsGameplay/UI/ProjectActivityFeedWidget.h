#pragma once

#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "UI/ProjectActivityFeedEntryRowWidget.h"
#include "UI/ProjectActivityFeedTypes.h"
#include "ProjectActivityFeedWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UCanvasPanelSlot;
class UImage;
class UOverlay;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidgetTree;
class UProjectChroniclePanelWidget;
class UProjectChronicleEmptyStateWidget;
class UObject;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectActivityFeedWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectActivityFeedWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;
	virtual void GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const override;

	void SetFeedEntries(const TArray<FProjectActivityFeedEntry>& InEntries);
	void SetExpanded(bool bInExpanded);
	void SetHudVisible(bool bVisible);
	void SetChronicleLayoutPolicy(const FProjectChronicleLayoutPolicy& InLayoutPolicy);

	UFUNCTION(BlueprintCallable, Category = "Activity")
	void ScrollHistoryByEntries(int32 Direction);

	UFUNCTION(BlueprintPure, Category = "Activity")
	bool IsExpanded() const;

	UFUNCTION(BlueprintPure, Category = "Activity")
	bool IsHudVisible() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Activity")
	void OnChronicleExpandedChanged(bool bInExpanded);

	UFUNCTION(BlueprintImplementableEvent, Category = "Activity")
	void OnChronicleRowsRebuilt(bool bInExpanded, int32 VisibleRowCount);

protected:
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void ApplyCachedState();
	void ApplyHudVisibility();
	void UpdatePanelLayout();
	void RebuildVisibleRows();
	void EnsurePanelHost();
	void EnsurePanelWidgets();
	void SyncActivePanelWidget();
	TArray<FProjectActivityFeedRowDisplayData> BuildResolvedRowData() const;
	TSubclassOf<UProjectChroniclePanelWidget> ResolvePanelWidgetClass(bool bForExpanded) const;
	TSubclassOf<UProjectActivityFeedEntryRowWidget> ResolveRowWidgetClass() const;
	TSubclassOf<UProjectActivityFeedEntryRowWidget> ResolveRowWidgetClassForData(const FProjectActivityFeedRowDisplayData& RowData) const;
	TSubclassOf<UProjectChronicleEmptyStateWidget> ResolveEmptyStateWidgetClass() const;
	UTexture2D* ResolveTexture(const TSoftObjectPtr<UTexture2D>& AssetPtr, const TCHAR* FallbackPath) const;
	UObject* ResolveStyleAsset(const TSoftObjectPtr<UObject>& AssetPtr, const TCHAR* FallbackPath) const;
	FSlateFontInfo MakeTitleFont(int32 Size, int32 LetterSpacing = 0) const;
	FSlateFontInfo MakeBodyFont(int32 Size, int32 LetterSpacing = 0) const;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Activity")
	int32 ZOrder;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> TitleFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> BodyFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> BodyItalicFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> FrameTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> HazeTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> DividerTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> HudPillTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Widgets")
	TSoftClassPtr<UProjectActivityFeedEntryRowWidget> RowWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Widgets")
	TSoftClassPtr<UProjectChronicleEmptyStateWidget> EmptyStateWidgetClass;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> PanelHost;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> FrameSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> FrameOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BackgroundBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> FrameTextureImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> HazeImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ContentBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ContentBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> HeaderOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> HudPillOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> HudPillImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> TopDividerImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> EntriesScrollSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> EntriesScrollBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> EntriesBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> FooterDividerImage;

private:
	UCanvasPanelSlot* FrameCanvasSlot = nullptr;
	TArray<FProjectActivityFeedEntry> CachedEntries;
	TArray<TObjectPtr<UProjectActivityFeedEntryRowWidget>> VisibleRowWidgets;
	TObjectPtr<UProjectChronicleEmptyStateWidget> VisibleEmptyStateWidget;
	TObjectPtr<UProjectChroniclePanelWidget> NormalPanelWidget;
	TObjectPtr<UProjectChroniclePanelWidget> ExpandedPanelWidget;
	TObjectPtr<UProjectChroniclePanelWidget> ActivePanelWidget;
	FProjectChronicleLayoutPolicy CurrentLayoutPolicy;
	FVector2D CurrentPanelSize = FVector2D(560.0f, 220.0f);
	int32 ScrollTargetRowIndex = INDEX_NONE;
	bool bExpanded = false;
	bool bHudVisible = false;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};
