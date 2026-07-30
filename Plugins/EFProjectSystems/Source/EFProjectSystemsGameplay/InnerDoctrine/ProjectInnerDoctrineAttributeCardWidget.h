#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "EFProjectUIPalette.h"
#include "ProjectInnerDoctrineAttributeCardWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UImage;
class UOverlay;
class UScaleBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UWidget;
class UWidgetTree;
class UObject;
class UVerticalBox;
class UWrapBox;

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectInnerDoctrineAttributeCardDisplayData
{
	GENERATED_BODY()

	FProjectInnerDoctrineAttributeCardDisplayData()
		: AttributeName(NAME_None)
		, DisplayLabel()
		, ShortLabel()
		, IconTexture(nullptr)
		, AccentTint(EFProjectUIPalette::Accent())
		, Level(0)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FName AttributeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FString DisplayLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FString ShortLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	TSoftObjectPtr<UTexture2D> IconTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FLinearColor AccentTint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	int32 Level;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineAttributeCardWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectInnerDoctrineAttributeCardWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	void ApplyDisplayData(const FProjectInnerDoctrineAttributeCardDisplayData& InDisplayData);

	UFUNCTION(BlueprintImplementableEvent, Category = "Inner Doctrine|UI")
	void OnDoctrineAttributeCardDataApplied(const FProjectInnerDoctrineAttributeCardDisplayData& DisplayData);

protected:
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void RefreshVisuals();
	void RefreshCardBrush(const FLinearColor& AccentTint);
	virtual FProjectInnerDoctrineAttributeCardDisplayData MakeDesignerPreviewData() const;
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
	TSoftObjectPtr<UTexture2D> CardFrameTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> DefaultIconTexture;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> RootOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BaseBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> FrameImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ContentBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ContentBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> IconSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> IconImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ShortLabelText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UScaleBox> ValueScaleBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ValueText;

private:
	FProjectInnerDoctrineAttributeCardDisplayData CurrentDisplayData;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineAttributeCardGlobalWidget : public UProjectInnerDoctrineAttributeCardWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineAttributeCardDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineAttributeCardsGlobalWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectInnerDoctrineAttributeCardsGlobalWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	int32 ApplyCards(
		const TArray<FProjectInnerDoctrineAttributeCardDisplayData>& InCardData,
		TSubclassOf<UProjectInnerDoctrineAttributeCardWidget> FallbackCardWidgetClass);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	UProjectInnerDoctrineAttributeCardWidget* FindCardWidgetByAttribute(FName AttributeName) const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI")
	int32 GetVisibleCardCount() const { return VisibleCardCount; }

	UFUNCTION(BlueprintImplementableEvent, Category = "Inner Doctrine|UI")
	void OnDoctrineAttributeCardsGlobalApplied(int32 InVisibleCardCount);

protected:
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	UProjectInnerDoctrineAttributeCardWidget* GetFixedCardForAttribute(FName AttributeName) const;
	UProjectInnerDoctrineAttributeCardWidget* GetOrCreateFallbackCard(
		FName AttributeName,
		TSubclassOf<UProjectInnerDoctrineAttributeCardWidget> FallbackCardWidgetClass);
	void HideUnusedFixedCards(const TSet<FName>& VisibleFixedCardNames);
	void ClearFallbackCards();

protected:
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> CardsBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineAttributeCardWidget> WillpowerCard;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineAttributeCardWidget> OffensiveCard;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineAttributeCardWidget> DefensiveCard;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineAttributeCardWidget> FaithCard;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineAttributeCardWidget> CunningCard;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineAttributeCardWidget> CelerityCard;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProjectInnerDoctrineAttributeCardWidget> CharismaCard;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWrapBox> ExtraCardsWrapBox;

private:
	TMap<FName, TObjectPtr<UProjectInnerDoctrineAttributeCardWidget>> RuntimeCardsByName;
	TMap<FName, TObjectPtr<UProjectInnerDoctrineAttributeCardWidget>> FallbackCardsByName;
	int32 VisibleCardCount = 0;
	bool bUsingNativeFallbackTree = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineWillpowerCardWidget : public UProjectInnerDoctrineAttributeCardWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineAttributeCardDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineOffensiveCardWidget : public UProjectInnerDoctrineAttributeCardWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineAttributeCardDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineDefensiveCardWidget : public UProjectInnerDoctrineAttributeCardWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineAttributeCardDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineFaithCardWidget : public UProjectInnerDoctrineAttributeCardWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineAttributeCardDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineCunningCardWidget : public UProjectInnerDoctrineAttributeCardWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineAttributeCardDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineCelerityCardWidget : public UProjectInnerDoctrineAttributeCardWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineAttributeCardDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineCharismaCardWidget : public UProjectInnerDoctrineAttributeCardWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectInnerDoctrineAttributeCardDisplayData MakeDesignerPreviewData() const override;
};
