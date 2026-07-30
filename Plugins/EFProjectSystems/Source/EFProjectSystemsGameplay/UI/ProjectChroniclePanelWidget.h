#pragma once

#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "ProjectChroniclePanelWidget.generated.h"

class UBorder;
class UImage;
class UOverlay;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidgetTree;
class UObject;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChroniclePanelWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectChroniclePanelWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	void ApplyPanelState(bool bInExpanded, const FVector2D& InPanelSize, float InEntriesHeight);

	UVerticalBox* GetEntriesBox() const { return EntriesBox; }
	UScrollBox* GetEntriesScrollBox() const { return EntriesScrollBox; }

	UFUNCTION(BlueprintImplementableEvent, Category = "Chronicle")
	void OnChroniclePanelStateApplied(bool bInExpanded, FVector2D InPanelSize, float InEntriesHeight);

protected:
	virtual bool IsExpandedDesignerPreview() const;
	virtual FVector2D GetDesignerPreviewPanelSize() const;

	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void RefreshPanelVisuals();
	UTexture2D* ResolveTexture(const TSoftObjectPtr<UTexture2D>& AssetPtr, const TCHAR* FallbackPath) const;
	UObject* ResolveStyleAsset(const TSoftObjectPtr<UObject>& AssetPtr, const TCHAR* FallbackPath) const;
	FSlateFontInfo MakeTitleFont(int32 Size) const;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> TitleFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> FrameTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> HazeTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> DividerTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> HudPillTexture;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

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
	FVector2D CurrentPanelSize = FVector2D(560.0f, 260.0f);
	float CurrentEntriesHeight = 148.0f;
	bool bExpanded = false;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChronicleNormalGlobalWidget : public UProjectChroniclePanelWidget
{
	GENERATED_BODY()

protected:
	virtual bool IsExpandedDesignerPreview() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChronicleExpandedGlobalWidget : public UProjectChroniclePanelWidget
{
	GENERATED_BODY()

protected:
	virtual bool IsExpandedDesignerPreview() const override;
	virtual FVector2D GetDesignerPreviewPanelSize() const override;
};
