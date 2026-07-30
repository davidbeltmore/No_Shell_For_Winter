#pragma once

#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "EFProjectUIPalette.h"
#include "InputCoreTypes.h"
#include "InnerDoctrine/ProjectInnerDoctrineExchangeAttributeRowWidget.h"
#include "InnerDoctrine/ProjectInnerDoctrineTypes.h"
#include "ProjectInnerDoctrineExchangeMenuWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UHorizontalBox;
class UImage;
class UOverlay;
class UProjectInnerDoctrineComponent;
class UProjectInnerDoctrineExchangeAttributeRowWidget;
class UProjectInnerDoctrineExchangeDetailPanelWidget;
class UProjectInnerDoctrineExchangeRowsGlobalWidget;
class UScaleBox;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidgetTree;
class UObject;

DECLARE_MULTICAST_DELEGATE_OneParam(FProjectDoctrineExchangePurchaseRequestedEvent, EProjectDoctrineAttribute);
DECLARE_MULTICAST_DELEGATE(FProjectDoctrineExchangeWithdrawRequestedEvent);
DECLARE_MULTICAST_DELEGATE(FProjectDoctrineExchangeCloseRequestedEvent);

struct FProjectInnerDoctrineExchangeResolvedEntry
{
	FName AttributeName = NAME_None;
	EProjectDoctrineAttribute Attribute = EProjectDoctrineAttribute::Willpower;
	FProjectInnerDoctrineExchangeAttributeRowDisplayData RowData;
	FText DetailName;
	FText DetailDescription;
	FText MilestoneFiveLabel;
	FText MilestoneTenLabel;
	TSoftObjectPtr<UTexture2D> DetailWatermarkTexture;
	int32 SortOrder = 0;
	bool bMilestoneFiveUnlocked = false;
	bool bMilestoneTenUnlocked = false;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectInnerDoctrineExchangeDetailDisplayData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	FName AttributeName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	FText DetailName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	FText LevelText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	FText CostText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	FText MilestoneFiveText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	FText MilestoneTenText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	FText FlavorText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	TSoftObjectPtr<UTexture2D> WatermarkTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	FLinearColor AccentTint = EFProjectUIPalette::AccentSoft();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	bool bHasSelection = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeMenuWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectInnerDoctrineExchangeMenuWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;
	virtual bool GatherCodeWidgetDesignerConversionManifest(FCodeWidgetDesignerConversionManifest& OutManifest) const override;

	void SetInnerDoctrineComponent(UProjectInnerDoctrineComponent* InComponent);
	void RefreshDisplay();
	void FocusMenuWidget();

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI|Debug")
	int32 GetSelectedIndex() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI|Debug")
	FProjectInnerDoctrineSnapshot GetCachedSnapshot() const;

	FProjectDoctrineExchangePurchaseRequestedEvent OnPurchaseRequested;
	FProjectDoctrineExchangeWithdrawRequestedEvent OnWithdrawRequested;
	FProjectDoctrineExchangeCloseRequestedEvent OnCloseRequested;

	UFUNCTION(BlueprintImplementableEvent, Category = "Inner Doctrine|UI")
	void OnExchangeMenuStateApplied(const FProjectInnerDoctrineSnapshot& Snapshot, int32 InSelectedIndex);

	UFUNCTION(BlueprintImplementableEvent, Category = "Inner Doctrine|UI")
	void OnExchangeMenuSelectionChanged(int32 InSelectedIndex, FName SelectedAttributeName);

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;
	void BuildWidgetTree();
	void InitializeVisualTree();
	void RefreshVisualState();
	TArray<FProjectInnerDoctrineExchangeResolvedEntry> BuildResolvedEntries() const;
	TArray<FProjectInnerDoctrineExchangeResolvedEntry> BuildDesignerPreviewEntries() const;
	bool DoesRowLayoutNeedRebuild(const TArray<FProjectInnerDoctrineExchangeResolvedEntry>& InEntries) const;
	void RebuildAttributeRows(const TArray<FProjectInnerDoctrineExchangeResolvedEntry>& InEntries);
	void RefreshAttributeRows();
	void RefreshDetailPanel();
	void RefreshFooterState();
	void NavigateSelectionByDirection(int32 Direction);
	void ConfirmSelection();
	bool HandleMenuKey(const FKey& Key);
	TSubclassOf<UProjectInnerDoctrineExchangeAttributeRowWidget> ResolveRowWidgetClass() const;
	UTexture2D* ResolveTexture(const TSoftObjectPtr<UTexture2D>& AssetPtr, const TCHAR* FallbackPath) const;
	UObject* ResolveStyleAsset(const TSoftObjectPtr<UObject>& AssetPtr, const TCHAR* FallbackPath) const;
	FSlateFontInfo MakeTitleFont(int32 Size, int32 LetterSpacing = 0) const;
	FSlateFontInfo MakeBodyFont(int32 Size, int32 LetterSpacing = 0) const;
	FText BuildAttributeFlavor(EProjectDoctrineAttribute Attribute) const;
	FText BuildMilestoneStatusText(const FText& LabelText, bool bUnlocked) const;
	FProjectInnerDoctrineExchangeDetailDisplayData BuildDetailDisplayData(const FProjectInnerDoctrineExchangeResolvedEntry* SelectedEntry) const;
	const FProjectInnerDoctrineExchangeResolvedEntry* GetSelectedEntry() const;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> TitleFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> BodyFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> FrameTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> HazeTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> DividerTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> StatBoxTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> FooterOrnamentTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> ModeGlyphTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> DefaultIconTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> DefaultWatermarkTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Widgets")
	TSoftClassPtr<UProjectInnerDoctrineExchangeAttributeRowWidget> AttributeRowWidgetClass;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BackdropBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> FrameOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BackgroundBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> HazeImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> FrameTextureImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ContentBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ContentBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HeaderRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> ModeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> ModeGlyphImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ModeText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> TopDividerImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> ResourceRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> RunDxpBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RunDxpLabelText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UScaleBox> RunDxpValueScaleBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RunDxpValueText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> MetaDxpBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MetaDxpLabelText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UScaleBox> MetaDxpValueScaleBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MetaDxpValueText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> BodyRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> AttributeListBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineExchangeRowsGlobalWidget> RowsGlobalWidget;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> AttributesScrollBox;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> AttributesLayout;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> DetailBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> DetailOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> DetailWatermarkImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> DetailContentBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> DetailContentBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailNameText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailLevelText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailCostText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> DetailMilestoneDividerImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailMilestoneFiveText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailMilestoneTenText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailFlavorText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> FooterDividerImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> FooterRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FooterStatusText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FooterControlsText;

	TMap<FName, TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget>> RowWidgetsByName;
	TArray<FName> CachedRowOrder;
	TArray<FProjectInnerDoctrineExchangeResolvedEntry> ResolvedEntries;
	TWeakObjectPtr<UProjectInnerDoctrineComponent> InnerDoctrineComponent;
	FProjectInnerDoctrineSnapshot CachedSnapshot;
	int32 SelectedIndex = INDEX_NONE;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeMenuGlobalWidget : public UProjectInnerDoctrineExchangeMenuWidget
{
	GENERATED_BODY()
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeDetailPanelWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectInnerDoctrineExchangeDetailPanelWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	void ApplyDetailData(const FProjectInnerDoctrineExchangeDetailDisplayData& InDisplayData);

	UFUNCTION(BlueprintImplementableEvent, Category = "Inner Doctrine|UI")
	void OnExchangeDetailPanelDataApplied(const FProjectInnerDoctrineExchangeDetailDisplayData& DisplayData);

protected:
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void RefreshVisuals();
	UTexture2D* ResolveTexture(const TSoftObjectPtr<UTexture2D>& AssetPtr, const TCHAR* FallbackPath) const;
	UObject* ResolveStyleAsset(const TSoftObjectPtr<UObject>& AssetPtr, const TCHAR* FallbackPath) const;
	FSlateFontInfo MakeTitleFont(int32 Size, int32 LetterSpacing = 0) const;
	FSlateFontInfo MakeBodyFont(int32 Size, int32 LetterSpacing = 0) const;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> TitleFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> BodyFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> DividerTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> DefaultWatermarkTexture;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> DetailBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> DetailOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> DetailWatermarkImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> DetailContentBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> DetailContentBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailNameText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailLevelText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailCostText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> DetailMilestoneDividerImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailMilestoneFiveText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailMilestoneTenText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailFlavorText;

private:
	FProjectInnerDoctrineExchangeDetailDisplayData CurrentDisplayData;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};
