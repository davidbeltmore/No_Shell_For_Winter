#pragma once

#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "Survival/ProjectInnerStateRowWidget.h"
#include "ProjectSurvivalNeedsWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UImage;
class UOverlay;
class UScaleBox;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UObject;
class UWidgetTree;
class UProjectInnerStateRowsGlobalWidget;
class UProjectSurvivalNeedsComponent;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalNeedsWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectSurvivalNeedsWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;
	virtual void GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const override;

	UFUNCTION(BlueprintCallable, Category = "Survival|UI")
	void SetNeedsComponent(UProjectSurvivalNeedsComponent* InNeedsComponent);

	UFUNCTION(BlueprintCallable, Category = "Survival|UI")
	void RefreshDisplay();

	UFUNCTION(BlueprintCallable, Category = "Survival|UI")
	void SetHudVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "Survival|UI")
	void ToggleHudVisible();

	UFUNCTION(BlueprintPure, Category = "Survival|UI")
	bool IsHudVisible() const;

	static bool IsEntryHiddenFromHud(FName EntryName);

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;
	void ApplyHudVisibility();
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void EnsureRowsGlobalWidget();
	void SyncRowsGlobalWidget();
	TArray<FProjectInnerStateRowDisplayData> BuildResolvedRowData() const;
	bool DoesRowLayoutNeedRebuild(const TArray<FProjectInnerStateRowDisplayData>& InRowData) const;
	void RebuildRows(const TArray<FProjectInnerStateRowDisplayData>& InRowData);
	TSubclassOf<UProjectInnerStateRowWidget> ResolveRowWidgetClass() const;
	UTexture2D* ResolveTexture(const TSoftObjectPtr<UTexture2D>& AssetPtr, const TCHAR* FallbackPath) const;
	UObject* ResolveStyleAsset(const TSoftObjectPtr<UObject>& AssetPtr, const TCHAR* FallbackPath) const;
	FSlateFontInfo MakeTitleFont(int32 Size) const;
	FSlateFontInfo MakeBodyFont(int32 Size, int32 LetterSpacing = 0) const;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Survival|UI")
	int32 ZOrder;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> TitleFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> BodyFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> FrameTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> VignetteTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> HazeTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> RowsPanelTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> DividerTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> HudPillTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Widgets")
	TSoftClassPtr<UProjectInnerStateRowWidget> RowWidgetClass;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BackdropBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> VignetteImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UScaleBox> FrameScaleBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> FrameSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> FrameCanvas;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> FrameImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> HazeImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> RowsPanelImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> TopDividerImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> BottomDividerImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> HudPillOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> HudPillImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HudText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> RowsScrollBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerStateRowsGlobalWidget> RowsGlobalWidget;

private:
	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalNeedsComponent> NeedsComponent;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UProjectInnerStateRowWidget>> RowWidgetsByName;

	UPROPERTY(Transient)
	TArray<FName> CachedRowOrder;

	bool bHudVisible;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateGlobalWidget : public UProjectSurvivalNeedsWidget
{
	GENERATED_BODY()

public:
	virtual void GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const override;
};
