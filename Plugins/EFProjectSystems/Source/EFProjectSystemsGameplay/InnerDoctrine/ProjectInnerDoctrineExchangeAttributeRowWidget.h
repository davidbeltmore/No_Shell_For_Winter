#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "EFProjectUIPalette.h"
#include "ProjectInnerDoctrineExchangeAttributeRowWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UImage;
class UOverlay;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidgetTree;
class UObject;

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectInnerDoctrineExchangeAttributeRowDisplayData
{
	GENERATED_BODY()

	FProjectInnerDoctrineExchangeAttributeRowDisplayData()
		: AttributeName(NAME_None)
		, DisplayLabel()
		, IconTexture(nullptr)
		, AccentTint(EFProjectUIPalette::Accent())
		, Level(0)
		, Cost(0)
		, RowWidth(560.0f)
		, RowHeight(88.0f)
		, bSelected(false)
		, bAffordable(false)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	FName AttributeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	FString DisplayLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	TSoftObjectPtr<UTexture2D> IconTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	FLinearColor AccentTint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	int32 Level;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	int32 Cost;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	float RowWidth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	float RowHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	bool bSelected;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine|UI")
	bool bAffordable;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeAttributeRowWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectInnerDoctrineExchangeAttributeRowWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	void ApplyDisplayData(const FProjectInnerDoctrineExchangeAttributeRowDisplayData& InDisplayData);

	UFUNCTION(BlueprintImplementableEvent, Category = "Inner Doctrine|UI")
	void OnExchangeAttributeRowDataApplied(const FProjectInnerDoctrineExchangeAttributeRowDisplayData& DisplayData);

	UFUNCTION(BlueprintImplementableEvent, Category = "Inner Doctrine|UI")
	void OnExchangeAttributeRowVisualStateChanged(FName AttributeName, bool bSelected, bool bAffordable);

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void RefreshVisuals();
	virtual FProjectInnerDoctrineExchangeAttributeRowDisplayData MakeDesignerPreviewData() const;
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
	TSoftObjectPtr<UTexture2D> RowFrameTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> DefaultIconTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> SelectionGlyphTexture;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> DesignerRootOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> RootOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BackgroundBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> FrameImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ContentBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> ContentBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> IconSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> IconBackgroundBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> IconImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> TextColumn;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> MetaRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LevelText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SeparatorText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CostText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> SelectionGlyphSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> SelectionGlyphImage;

private:
	FProjectInnerDoctrineExchangeAttributeRowDisplayData CurrentDisplayData;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeAttributeRowGlobalWidget : public UProjectInnerDoctrineExchangeAttributeRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineExchangeAttributeRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeWillpowerRowWidget : public UProjectInnerDoctrineExchangeAttributeRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineExchangeAttributeRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeOffensiveRowWidget : public UProjectInnerDoctrineExchangeAttributeRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineExchangeAttributeRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeDefensiveRowWidget : public UProjectInnerDoctrineExchangeAttributeRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineExchangeAttributeRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeFaithRowWidget : public UProjectInnerDoctrineExchangeAttributeRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineExchangeAttributeRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeCunningRowWidget : public UProjectInnerDoctrineExchangeAttributeRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineExchangeAttributeRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeCelerityRowWidget : public UProjectInnerDoctrineExchangeAttributeRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineExchangeAttributeRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeCharismaRowWidget : public UProjectInnerDoctrineExchangeAttributeRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineExchangeAttributeRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineExchangeRowsGlobalWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectInnerDoctrineExchangeRowsGlobalWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	int32 ApplyRows(
		const TArray<FProjectInnerDoctrineExchangeAttributeRowDisplayData>& InRowData,
		TSubclassOf<UProjectInnerDoctrineExchangeAttributeRowWidget> FallbackRowWidgetClass);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	UProjectInnerDoctrineExchangeAttributeRowWidget* FindRowWidgetByAttribute(FName AttributeName) const;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	void ScrollRowWidgetIntoView(UProjectInnerDoctrineExchangeAttributeRowWidget* RowWidget);

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI")
	int32 GetVisibleRowCount() const { return VisibleRowCount; }

	UFUNCTION(BlueprintImplementableEvent, Category = "Inner Doctrine|UI")
	void OnExchangeRowsApplied(int32 InVisibleRowCount, int32 SelectedIndex);

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	UProjectInnerDoctrineExchangeAttributeRowWidget* GetFixedRowForAttribute(FName AttributeName) const;
	UProjectInnerDoctrineExchangeAttributeRowWidget* GetOrCreateFallbackRow(
		FName AttributeName,
		TSubclassOf<UProjectInnerDoctrineExchangeAttributeRowWidget> FallbackRowWidgetClass);
	void HideUnusedFixedRows(const TSet<FName>& VisibleFixedRowNames);
	void ClearFallbackRows();

protected:
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> DesignerRootOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> RootOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> RowsScrollBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> RowsLayout;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget> WillpowerRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget> OffensiveRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget> DefensiveRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget> FaithRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget> CunningRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget> CelerityRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget> CharismaRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ExtraRowsLayout;

private:
	TMap<FName, TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget>> RuntimeRowsByName;
	TMap<FName, TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget>> FallbackRowsByName;
	int32 VisibleRowCount = 0;
	bool bUsingNativeFallbackTree = false;
};
