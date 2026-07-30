#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "EFProjectUIPalette.h"
#include "ProjectInnerStateRowWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UImage;
class UOverlay;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UUserWidget;
class UVerticalBox;
class UWidgetTree;
class UObject;

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectInnerStateRowDisplayData
{
	GENERATED_BODY()

	FProjectInnerStateRowDisplayData()
		: EntryName(NAME_None)
		, DisplayLabel()
		, Monogram()
		, AccentTint(EFProjectUIPalette::Accent())
		, CurrentValue(0.0f)
		, MaxValue(100.0f)
		, NormalizedValue(0.0f)
		, FilledBars(0)
		, TotalBars(10)
		, PercentValue(0)
		, bIsSensation(false)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	FName EntryName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	FString DisplayLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	FString Monogram;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	FLinearColor AccentTint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	float CurrentValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	float MaxValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	float NormalizedValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	int32 FilledBars;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	int32 TotalBars;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	int32 PercentValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	bool bIsSensation;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateRowWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectInnerStateRowWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Survival|UI")
	void ApplyDisplayData(const FProjectInnerStateRowDisplayData& InDisplayData);

	UFUNCTION(BlueprintPure, Category = "Survival|UI")
	FProjectInnerStateRowDisplayData GetCurrentDisplayData() const { return CurrentDisplayData; }

	UFUNCTION(BlueprintImplementableEvent, Category = "Survival|UI")
	void OnInnerStateRowDataApplied(const FProjectInnerStateRowDisplayData& DisplayData);

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;

	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void RefreshPips();
	void RebuildPipImageCache();
	virtual FProjectInnerStateRowDisplayData MakeDesignerPreviewData() const;
	UTexture2D* ResolveTexture(const TSoftObjectPtr<UTexture2D>& AssetPtr, const TCHAR* FallbackPath) const;
	UObject* ResolveStyleAsset(const TSoftObjectPtr<UObject>& AssetPtr, const TCHAR* FallbackPath) const;
	FSlateFontInfo MakeTitleFont(int32 Size) const;
	FSlateFontInfo MakeBodyFont(int32 Size, int32 LetterSpacing = 0) const;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> TitleFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> BodyFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> RowFrameTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> MedallionTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> PipFilledTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> PipEmptyTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> SparkTexture;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> RootOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> RowFrameImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ContentBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> ContentBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> MedallionSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> MedallionOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> MedallionImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MonogramText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> LabelSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> PipsBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> ValueSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ValueText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> GlyphSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> GlyphImage;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> PipImages;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> FilledPipImages;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> EmptyPipImages;

private:
	FProjectInnerStateRowDisplayData CurrentDisplayData;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateRowGlobalWidget : public UProjectInnerStateRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerStateRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateHungerRowWidget : public UProjectInnerStateRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerStateRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateThirstRowWidget : public UProjectInnerStateRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerStateRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateSleepRowWidget : public UProjectInnerStateRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerStateRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateMadnessRowWidget : public UProjectInnerStateRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerStateRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateCurseRowWidget : public UProjectInnerStateRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerStateRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStatePainRowWidget : public UProjectInnerStateRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerStateRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateRowsGlobalWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectInnerStateRowsGlobalWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Survival|UI")
	int32 ApplyRows(
		const TArray<FProjectInnerStateRowDisplayData>& InRowData,
		TSubclassOf<UProjectInnerStateRowWidget> FallbackRowWidgetClass);

	UFUNCTION(BlueprintCallable, Category = "Survival|UI")
	UProjectInnerStateRowWidget* FindRowWidgetByEntry(FName EntryName) const;

	UFUNCTION(BlueprintPure, Category = "Survival|UI")
	int32 GetVisibleRowCount() const { return VisibleRowCount; }

	UFUNCTION(BlueprintImplementableEvent, Category = "Survival|UI")
	void OnInnerStateRowsGlobalApplied(int32 InVisibleRowCount);

protected:
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	UProjectInnerStateRowWidget* GetFixedRowForEntry(FName EntryName) const;
	UProjectInnerStateRowWidget* GetOrCreateFallbackRow(
		FName EntryName,
		TSubclassOf<UProjectInnerStateRowWidget> FallbackRowWidgetClass);
	void HideUnusedFixedRows(const TSet<FName>& VisibleFixedRowNames);
	void ClearFallbackRows();

protected:
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> RowsBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerStateRowWidget> HungerRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerStateRowWidget> ThirstRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerStateRowWidget> SleepRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerStateRowWidget> MadnessRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerStateRowWidget> CurseRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerStateRowWidget> PainRow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ExtraRowsBox;

private:
	TMap<FName, TObjectPtr<UProjectInnerStateRowWidget>> RuntimeRowsByName;
	TMap<FName, TObjectPtr<UProjectInnerStateRowWidget>> FallbackRowsByName;
	int32 VisibleRowCount = 0;
	bool bUsingNativeFallbackTree = false;
};
