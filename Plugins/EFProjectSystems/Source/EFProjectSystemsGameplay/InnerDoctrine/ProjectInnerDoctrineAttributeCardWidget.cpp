#include "InnerDoctrine/ProjectInnerDoctrineAttributeCardWidget.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"

namespace ProjectInnerDoctrineAttributeCardWidgetPrivate
{
	constexpr float CardWidth = 56.0f;
	constexpr float CardHeight = 70.0f;
	constexpr float CardCornerRadius = 9.0f;
	constexpr float CardOutlineWidth = 1.0f;
	constexpr float ValueHeight = 18.0f;
	constexpr float CardsGlobalWidth = 416.0f;
	constexpr float CardsGlobalHeight = 70.0f;

	FLinearColor BaseFillTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.88f);
	}
	FLinearColor BaseOutlineTint()
	{
		return EFProjectUIPalette::OutlineDim(0.36f);
	}
	const FLinearColor LabelShadowTint(0.0f, 0.0f, 0.0f, 0.24f);
	const FLinearColor ValueShadowTint(0.0f, 0.0f, 0.0f, 0.30f);

	const TCHAR* CinzelFontPath = TEXT("/Game/_Game/Widgets/Attributes/Assets/Fonts/F_InnerState_Cinzel.F_InnerState_Cinzel");
	const TCHAR* CormorantFontPath = TEXT("/Game/_Game/Widgets/Attributes/Assets/Fonts/F_InnerState_Cormorant.F_InnerState_Cormorant");
	const TCHAR* CardFrameTexturePath = TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_CardFrame.T_InnerDoctrine_CardFrame");
	const TCHAR* DefaultIconTexturePath = TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Default.T_InnerDoctrine_Icon_Default");

	UObject* LoadObjectByPath(const TCHAR* AssetPath)
	{
		return AssetPath && AssetPath[0] != 0
			? StaticLoadObject(UObject::StaticClass(), nullptr, AssetPath)
			: nullptr;
	}

	FProjectInnerDoctrineAttributeCardDisplayData MakePreviewData(
		const FName AttributeName,
		const FString& DisplayLabel,
		const FString& ShortLabel,
		const int32 Level,
		const FLinearColor& AccentTint,
		const TCHAR* IconTexturePath)
	{
		FProjectInnerDoctrineAttributeCardDisplayData Data;
		Data.AttributeName = AttributeName;
		Data.DisplayLabel = DisplayLabel;
		Data.ShortLabel = ShortLabel;
		Data.Level = Level;
		Data.AccentTint = AccentTint;
		if (IconTexturePath && IconTexturePath[0] != 0)
		{
			Data.IconTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(IconTexturePath));
		}
		return Data;
	}
}

UProjectInnerDoctrineAttributeCardWidget::UProjectInnerDoctrineAttributeCardWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TitleFontAsset = FSoftObjectPath(ProjectInnerDoctrineAttributeCardWidgetPrivate::CinzelFontPath);
	BodyFontAsset = FSoftObjectPath(ProjectInnerDoctrineAttributeCardWidgetPrivate::CormorantFontPath);
	CardFrameTexture = FSoftObjectPath(ProjectInnerDoctrineAttributeCardWidgetPrivate::CardFrameTexturePath);
	DefaultIconTexture = FSoftObjectPath(ProjectInnerDoctrineAttributeCardWidgetPrivate::DefaultIconTexturePath);
}

void UProjectInnerDoctrineAttributeCardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectInnerDoctrineAttributeCardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

bool UProjectInnerDoctrineAttributeCardWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	CurrentDisplayData = MakeDesignerPreviewData();
	bVisualTreeInitialized = false;
	bUsingNativeFallbackTree = true;
	WidgetTree = TargetWidgetTree;

	const bool bBuiltTree = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuiltTree)
	{
		InitializeVisualTree();
		RefreshVisuals();
	}

	return bBuiltTree;
}

void UProjectInnerDoctrineAttributeCardWidget::ApplyDisplayData(const FProjectInnerDoctrineAttributeCardDisplayData& InDisplayData)
{
	CurrentDisplayData = InDisplayData;
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
	OnDoctrineAttributeCardDataApplied(CurrentDisplayData);
}

void UProjectInnerDoctrineAttributeCardWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootSizeBox || WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = false;
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectInnerDoctrineAttributeCardWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	UOverlay* DesignerRootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("DesignerRootOverlay"));
	TargetWidgetTree->RootWidget = DesignerRootOverlay;

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(ProjectInnerDoctrineAttributeCardWidgetPrivate::CardWidth);
	RootSizeBox->SetHeightOverride(ProjectInnerDoctrineAttributeCardWidgetPrivate::CardHeight);
	if (UOverlaySlot* RootSizeSlot = DesignerRootOverlay->AddChildToOverlay(RootSizeBox))
	{
		RootSizeSlot->SetHorizontalAlignment(HAlign_Center);
		RootSizeSlot->SetVerticalAlignment(VAlign_Center);
	}

	RootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	RootSizeBox->AddChild(RootOverlay);

	BaseBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BaseBorder"));
	BaseBorder->SetPadding(FMargin(0.0f));
	BaseBorder->SetBrushColor(FLinearColor::White);
	if (UOverlaySlot* BaseSlot = RootOverlay->AddChildToOverlay(BaseBorder))
	{
		BaseSlot->SetHorizontalAlignment(HAlign_Fill);
		BaseSlot->SetVerticalAlignment(VAlign_Fill);
	}

	FrameImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FrameImage"));
	if (UOverlaySlot* FrameSlot = RootOverlay->AddChildToOverlay(FrameImage))
	{
		FrameSlot->SetHorizontalAlignment(HAlign_Fill);
		FrameSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentBorder"));
	ContentBorder->SetPadding(FMargin(4.0f, 13.0f, 4.0f, 8.0f));
	ContentBorder->SetBrushColor(FLinearColor::Transparent);
	if (UOverlaySlot* ContentSlot = RootOverlay->AddChildToOverlay(ContentBorder))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ContentBox"));
	ContentBorder->SetContent(ContentBox);

	ShortLabelText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShortLabelText"));
	if (UVerticalBoxSlot* LabelSlot = ContentBox->AddChildToVerticalBox(ShortLabelText))
	{
		LabelSlot->SetHorizontalAlignment(HAlign_Center);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 11.0f));
	}

	ValueScaleBox = TargetWidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("ValueScaleBox"));
	ValueScaleBox->SetStretch(EStretch::ScaleToFit);
	if (UVerticalBoxSlot* ValueScaleSlot = ContentBox->AddChildToVerticalBox(ValueScaleBox))
	{
		ValueScaleSlot->SetHorizontalAlignment(HAlign_Fill);
		ValueScaleSlot->SetVerticalAlignment(VAlign_Center);
		ValueScaleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	USizeBox* ValueSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ValueSizeBox"));
	ValueSizeBox->SetHeightOverride(ProjectInnerDoctrineAttributeCardWidgetPrivate::ValueHeight);
	ValueScaleBox->AddChild(ValueSizeBox);

	ValueText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ValueText"));
	ValueSizeBox->AddChild(ValueText);

	return true;
}

void UProjectInnerDoctrineAttributeCardWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	if (!bUsingNativeFallbackTree)
	{
		bVisualTreeInitialized = true;
		return;
	}

	if (FrameImage)
	{
		if (UTexture2D* FrameTexture = ResolveTexture(CardFrameTexture, ProjectInnerDoctrineAttributeCardWidgetPrivate::CardFrameTexturePath))
		{
			FrameImage->SetBrushFromTexture(ResolveProjectThemeTexture(FrameTexture), false);
		}
	}

	if (ShortLabelText)
	{
		ShortLabelText->SetFont(MakeTitleFont(10, 0));
		ShortLabelText->SetJustification(ETextJustify::Center);
		ShortLabelText->SetShadowOffset(FVector2D(0.0f, 0.25f));
		ShortLabelText->SetShadowColorAndOpacity(ProjectInnerDoctrineAttributeCardWidgetPrivate::LabelShadowTint);
	}

	if (ValueText)
	{
		ValueText->SetFont(MakeBodyFont(15, 0));
		ValueText->SetJustification(ETextJustify::Center);
		ValueText->SetShadowOffset(FVector2D(0.0f, 0.35f));
		ValueText->SetShadowColorAndOpacity(ProjectInnerDoctrineAttributeCardWidgetPrivate::ValueShadowTint);
	}

	bVisualTreeInitialized = true;
}

void UProjectInnerDoctrineAttributeCardWidget::RefreshVisuals()
{
	const FLinearColor AccentTint = CurrentDisplayData.AccentTint.GetClamped(0.0f, 1.0f);

	if (bUsingNativeFallbackTree)
	{
		if (RootSizeBox)
		{
			RootSizeBox->SetWidthOverride(ProjectInnerDoctrineAttributeCardWidgetPrivate::CardWidth);
			RootSizeBox->SetHeightOverride(ProjectInnerDoctrineAttributeCardWidgetPrivate::CardHeight);
		}

		RefreshCardBrush(AccentTint);

		if (FrameImage)
		{
			const FLinearColor FrameTint = FLinearColor(
				FMath::Max(ProjectInnerDoctrineAttributeCardWidgetPrivate::BaseOutlineTint().R, AccentTint.R * 0.78f),
				FMath::Max(ProjectInnerDoctrineAttributeCardWidgetPrivate::BaseOutlineTint().G, AccentTint.G * 0.62f),
				FMath::Max(ProjectInnerDoctrineAttributeCardWidgetPrivate::BaseOutlineTint().B, AccentTint.B * 0.88f),
				0.44f);
			FrameImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				FrameImage,
				FrameTint));
		}

		if (IconImage)
		{
			if (UTexture2D* IconTexture = ResolveTexture(CurrentDisplayData.IconTexture, ProjectInnerDoctrineAttributeCardWidgetPrivate::DefaultIconTexturePath))
			{
				IconImage->SetBrushFromTexture(ResolveProjectThemeTexture(IconTexture), false);
			}
			IconImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				IconImage,
				AccentTint.CopyWithNewOpacity(0.94f)));
		}
	}

	if (ShortLabelText)
	{
		ShortLabelText->SetText(FText::FromString(CurrentDisplayData.ShortLabel));
		if (bUsingNativeFallbackTree)
		{
			ShortLabelText->SetColorAndOpacity(FSlateColor(AccentTint.CopyWithNewOpacity(0.92f)));
		}
	}

	if (ValueText)
	{
		ValueText->SetText(FText::AsNumber(CurrentDisplayData.Level));
		if (bUsingNativeFallbackTree)
		{
			ValueText->SetColorAndOpacity(FSlateColor(AccentTint.CopyWithNewOpacity(0.98f)));
		}
	}
}

void UProjectInnerDoctrineAttributeCardWidget::RefreshCardBrush(const FLinearColor& AccentTint)
{
	if (!BaseBorder || !bUsingNativeFallbackTree)
	{
		return;
	}

	const FLinearColor SafeAccent = AccentTint.GetClamped(0.0f, 1.0f);
	const FLinearColor OutlineTint = FLinearColor(
		FMath::Max(ProjectInnerDoctrineAttributeCardWidgetPrivate::BaseOutlineTint().R, SafeAccent.R * 0.76f),
		FMath::Max(ProjectInnerDoctrineAttributeCardWidgetPrivate::BaseOutlineTint().G, SafeAccent.G * 0.60f),
		FMath::Max(ProjectInnerDoctrineAttributeCardWidgetPrivate::BaseOutlineTint().B, SafeAccent.B * 0.82f),
		0.58f);
	const FSlateRoundedBoxBrush CardBrush(
		ProjectInnerDoctrineAttributeCardWidgetPrivate::BaseFillTint(),
		ProjectInnerDoctrineAttributeCardWidgetPrivate::CardCornerRadius,
		FSlateColor(OutlineTint),
		ProjectInnerDoctrineAttributeCardWidgetPrivate::CardOutlineWidth,
		FVector2f(
			ProjectInnerDoctrineAttributeCardWidgetPrivate::CardWidth,
			ProjectInnerDoctrineAttributeCardWidgetPrivate::CardHeight));
	BaseBorder->SetBrush(CardBrush);
}

FProjectInnerDoctrineAttributeCardDisplayData UProjectInnerDoctrineAttributeCardWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
		TEXT("Attribute"),
		TEXT("ATTRIBUTE"),
		TEXT("ATR"),
		0,
		EFProjectUIPalette::Accent(),
		ProjectInnerDoctrineAttributeCardWidgetPrivate::DefaultIconTexturePath);
}

FProjectInnerDoctrineAttributeCardDisplayData UProjectInnerDoctrineAttributeCardGlobalWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
		TEXT("GlobalCard"),
		TEXT("GLOBAL CARD"),
		TEXT("GLB"),
		8,
		EFProjectUIPalette::AccentSoft(),
		ProjectInnerDoctrineAttributeCardWidgetPrivate::DefaultIconTexturePath);
}

UProjectInnerDoctrineAttributeCardsGlobalWidget::UProjectInnerDoctrineAttributeCardsGlobalWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UProjectInnerDoctrineAttributeCardsGlobalWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UProjectInnerDoctrineAttributeCardsGlobalWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	ReapplyProjectThemeAfterNativeConstruct();
}

bool UProjectInnerDoctrineAttributeCardsGlobalWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bUsingNativeFallbackTree = true;
	return BuildDefaultWidgetTree(TargetWidgetTree);
}

void UProjectInnerDoctrineAttributeCardsGlobalWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootSizeBox || WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = false;
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectInnerDoctrineAttributeCardsGlobalWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(ProjectInnerDoctrineAttributeCardWidgetPrivate::CardsGlobalWidth);
	RootSizeBox->SetHeightOverride(ProjectInnerDoctrineAttributeCardWidgetPrivate::CardsGlobalHeight);
	TargetWidgetTree->RootWidget = RootSizeBox;

	UVerticalBox* RootBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CardsRootBox"));
	RootSizeBox->AddChild(RootBox);

	CardsBox = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CardsBox"));
	if (UVerticalBoxSlot* CardsSlot = RootBox->AddChildToVerticalBox(CardsBox))
	{
		CardsSlot->SetHorizontalAlignment(HAlign_Center);
		CardsSlot->SetVerticalAlignment(VAlign_Center);
		CardsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	const auto AddPreviewCard = [this, TargetWidgetTree](
		UClass* CardClass,
		const TCHAR* WidgetName,
		const FProjectInnerDoctrineAttributeCardDisplayData& PreviewData) -> UProjectInnerDoctrineAttributeCardWidget*
	{
		if (!CardsBox || !CardClass || !WidgetName)
		{
			return nullptr;
		}

		UProjectInnerDoctrineAttributeCardWidget* CardWidget =
			TargetWidgetTree->ConstructWidget<UProjectInnerDoctrineAttributeCardWidget>(CardClass, FName(WidgetName));
		if (!CardWidget)
		{
			return nullptr;
		}

		CardWidget->ApplyDisplayData(PreviewData);
		if (UHorizontalBoxSlot* CardSlot = CardsBox->AddChildToHorizontalBox(CardWidget))
		{
			CardSlot->SetPadding(FMargin(0.0f));
			CardSlot->SetHorizontalAlignment(HAlign_Center);
			CardSlot->SetVerticalAlignment(VAlign_Center);
			CardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
		return CardWidget;
	};

	WillpowerCard = AddPreviewCard(
		UProjectInnerDoctrineWillpowerCardWidget::StaticClass(),
		TEXT("WillpowerCard"),
		ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
			TEXT("Willpower"),
			TEXT("WILLPOWER"),
			TEXT("WIL"),
			3,
			EFProjectUIPalette::AttributeWillpower(),
			TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Willpower.T_InnerDoctrine_Icon_Willpower")));

	OffensiveCard = AddPreviewCard(
		UProjectInnerDoctrineOffensiveCardWidget::StaticClass(),
		TEXT("OffensiveCard"),
		ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
			TEXT("Offensive"),
			TEXT("OFFENSIVE"),
			TEXT("OFF"),
			2,
			EFProjectUIPalette::AttributeOffensive(),
			TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Offensive.T_InnerDoctrine_Icon_Offensive")));

	DefensiveCard = AddPreviewCard(
		UProjectInnerDoctrineDefensiveCardWidget::StaticClass(),
		TEXT("DefensiveCard"),
		ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
			TEXT("Defensive"),
			TEXT("DEFENSIVE"),
			TEXT("DEF"),
			4,
			EFProjectUIPalette::AttributeDefensive(),
			TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Defensive.T_InnerDoctrine_Icon_Defensive")));

	FaithCard = AddPreviewCard(
		UProjectInnerDoctrineFaithCardWidget::StaticClass(),
		TEXT("FaithCard"),
		ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
			TEXT("Faith"),
			TEXT("FAITH"),
			TEXT("FAI"),
			1,
			EFProjectUIPalette::AttributeFaith(),
			TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Faith.T_InnerDoctrine_Icon_Faith")));

	CunningCard = AddPreviewCard(
		UProjectInnerDoctrineCunningCardWidget::StaticClass(),
		TEXT("CunningCard"),
		ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
			TEXT("Cunning"),
			TEXT("CUNNING"),
			TEXT("CUN"),
			5,
			EFProjectUIPalette::AttributeCunning(),
			TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Cunning.T_InnerDoctrine_Icon_Cunning")));

	CelerityCard = AddPreviewCard(
		UProjectInnerDoctrineCelerityCardWidget::StaticClass(),
		TEXT("CelerityCard"),
		ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
			TEXT("Celerity"),
			TEXT("CELERITY"),
			TEXT("CEL"),
			2,
			EFProjectUIPalette::AttributeCelerity(),
			TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Celerity.T_InnerDoctrine_Icon_Celerity")));

	CharismaCard = AddPreviewCard(
		UProjectInnerDoctrineCharismaCardWidget::StaticClass(),
		TEXT("CharismaCard"),
		ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
			TEXT("Charisma"),
			TEXT("CHARISMA"),
			TEXT("ALL"),
			6,
			EFProjectUIPalette::AttributeCharisma(),
			TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Charisma.T_InnerDoctrine_Icon_Charisma")));

	ExtraCardsWrapBox = TargetWidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("ExtraCardsWrapBox"));
	ExtraCardsWrapBox->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* ExtraSlot = RootBox->AddChildToVerticalBox(ExtraCardsWrapBox))
	{
		ExtraSlot->SetHorizontalAlignment(HAlign_Center);
		ExtraSlot->SetVerticalAlignment(VAlign_Center);
		ExtraSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	return true;
}

int32 UProjectInnerDoctrineAttributeCardsGlobalWidget::ApplyCards(
	const TArray<FProjectInnerDoctrineAttributeCardDisplayData>& InCardData,
	TSubclassOf<UProjectInnerDoctrineAttributeCardWidget> FallbackCardWidgetClass)
{
	BuildWidgetTree();

	RuntimeCardsByName.Empty();
	TSet<FName> VisibleFixedCardNames;
	VisibleCardCount = 0;
	ClearFallbackCards();

	for (const FProjectInnerDoctrineAttributeCardDisplayData& CardData : InCardData)
	{
		UProjectInnerDoctrineAttributeCardWidget* CardWidget = GetFixedCardForAttribute(CardData.AttributeName);
		if (CardWidget)
		{
			VisibleFixedCardNames.Add(CardData.AttributeName);
		}
		else
		{
			CardWidget = GetOrCreateFallbackCard(CardData.AttributeName, FallbackCardWidgetClass);
		}

		if (!CardWidget)
		{
			continue;
		}

		CardWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		CardWidget->ApplyDisplayData(CardData);
		RuntimeCardsByName.Add(CardData.AttributeName, CardWidget);
		++VisibleCardCount;
	}

	HideUnusedFixedCards(VisibleFixedCardNames);
	if (ExtraCardsWrapBox)
	{
		ExtraCardsWrapBox->SetVisibility(FallbackCardsByName.IsEmpty()
			? ESlateVisibility::Collapsed
			: ESlateVisibility::SelfHitTestInvisible);
	}

	OnDoctrineAttributeCardsGlobalApplied(VisibleCardCount);
	return VisibleCardCount;
}

UProjectInnerDoctrineAttributeCardWidget* UProjectInnerDoctrineAttributeCardsGlobalWidget::FindCardWidgetByAttribute(const FName AttributeName) const
{
	if (const TObjectPtr<UProjectInnerDoctrineAttributeCardWidget>* FoundWidget = RuntimeCardsByName.Find(AttributeName))
	{
		return FoundWidget->Get();
	}

	return GetFixedCardForAttribute(AttributeName);
}

UProjectInnerDoctrineAttributeCardWidget* UProjectInnerDoctrineAttributeCardsGlobalWidget::GetFixedCardForAttribute(const FName AttributeName) const
{
	if (AttributeName == FName(TEXT("Willpower")))
	{
		return WillpowerCard.Get();
	}
	if (AttributeName == FName(TEXT("Offensive")))
	{
		return OffensiveCard.Get();
	}
	if (AttributeName == FName(TEXT("Defensive")))
	{
		return DefensiveCard.Get();
	}
	if (AttributeName == FName(TEXT("Faith")))
	{
		return FaithCard.Get();
	}
	if (AttributeName == FName(TEXT("Cunning")))
	{
		return CunningCard.Get();
	}
	if (AttributeName == FName(TEXT("Celerity")))
	{
		return CelerityCard.Get();
	}
	if (AttributeName == FName(TEXT("Charisma")))
	{
		return CharismaCard.Get();
	}

	return nullptr;
}

UProjectInnerDoctrineAttributeCardWidget* UProjectInnerDoctrineAttributeCardsGlobalWidget::GetOrCreateFallbackCard(
	const FName AttributeName,
	TSubclassOf<UProjectInnerDoctrineAttributeCardWidget> FallbackCardWidgetClass)
{
	if (!ExtraCardsWrapBox || !FallbackCardWidgetClass)
	{
		return nullptr;
	}

	if (TObjectPtr<UProjectInnerDoctrineAttributeCardWidget>* ExistingCard = FallbackCardsByName.Find(AttributeName))
	{
		return ExistingCard->Get();
	}

	UProjectInnerDoctrineAttributeCardWidget* FallbackCard = CreateWidget<UProjectInnerDoctrineAttributeCardWidget>(
		this,
		FallbackCardWidgetClass.Get());
	if (!FallbackCard)
	{
		return nullptr;
	}

	if (UWrapBoxSlot* FallbackSlot = ExtraCardsWrapBox->AddChildToWrapBox(FallbackCard))
	{
		FallbackSlot->SetPadding(FMargin(0.0f));
		FallbackSlot->SetHorizontalAlignment(HAlign_Left);
		FallbackSlot->SetVerticalAlignment(VAlign_Center);
		FallbackSlot->SetFillEmptySpace(false);
		FallbackSlot->SetFillSpanWhenLessThan(0.0f);
	}

	FallbackCardsByName.Add(AttributeName, FallbackCard);
	return FallbackCard;
}

void UProjectInnerDoctrineAttributeCardsGlobalWidget::HideUnusedFixedCards(const TSet<FName>& VisibleFixedCardNames)
{
	const auto HideIfUnused = [&VisibleFixedCardNames](const FName AttributeName, UProjectInnerDoctrineAttributeCardWidget* CardWidget)
	{
		if (CardWidget && !VisibleFixedCardNames.Contains(AttributeName))
		{
			CardWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	};

	HideIfUnused(FName(TEXT("Willpower")), WillpowerCard.Get());
	HideIfUnused(FName(TEXT("Offensive")), OffensiveCard.Get());
	HideIfUnused(FName(TEXT("Defensive")), DefensiveCard.Get());
	HideIfUnused(FName(TEXT("Faith")), FaithCard.Get());
	HideIfUnused(FName(TEXT("Cunning")), CunningCard.Get());
	HideIfUnused(FName(TEXT("Celerity")), CelerityCard.Get());
	HideIfUnused(FName(TEXT("Charisma")), CharismaCard.Get());
}

void UProjectInnerDoctrineAttributeCardsGlobalWidget::ClearFallbackCards()
{
	if (ExtraCardsWrapBox)
	{
		ExtraCardsWrapBox->ClearChildren();
	}
	FallbackCardsByName.Empty();
}

FProjectInnerDoctrineAttributeCardDisplayData UProjectInnerDoctrineWillpowerCardWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
		TEXT("Willpower"),
		TEXT("WILLPOWER"),
		TEXT("WIL"),
		3,
		EFProjectUIPalette::AttributeWillpower(),
		TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Willpower.T_InnerDoctrine_Icon_Willpower"));
}

FProjectInnerDoctrineAttributeCardDisplayData UProjectInnerDoctrineOffensiveCardWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
		TEXT("Offensive"),
		TEXT("OFFENSIVE"),
		TEXT("OFF"),
		2,
		EFProjectUIPalette::AttributeOffensive(),
		TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Offensive.T_InnerDoctrine_Icon_Offensive"));
}

FProjectInnerDoctrineAttributeCardDisplayData UProjectInnerDoctrineDefensiveCardWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
		TEXT("Defensive"),
		TEXT("DEFENSIVE"),
		TEXT("DEF"),
		4,
		EFProjectUIPalette::AttributeDefensive(),
		TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Defensive.T_InnerDoctrine_Icon_Defensive"));
}

FProjectInnerDoctrineAttributeCardDisplayData UProjectInnerDoctrineFaithCardWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
		TEXT("Faith"),
		TEXT("FAITH"),
		TEXT("FAI"),
		1,
		EFProjectUIPalette::AttributeFaith(),
		TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Faith.T_InnerDoctrine_Icon_Faith"));
}

FProjectInnerDoctrineAttributeCardDisplayData UProjectInnerDoctrineCunningCardWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
		TEXT("Cunning"),
		TEXT("CUNNING"),
		TEXT("CUN"),
		5,
		EFProjectUIPalette::AttributeCunning(),
		TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Cunning.T_InnerDoctrine_Icon_Cunning"));
}

FProjectInnerDoctrineAttributeCardDisplayData UProjectInnerDoctrineCelerityCardWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
		TEXT("Celerity"),
		TEXT("CELERITY"),
		TEXT("CEL"),
		2,
		EFProjectUIPalette::AttributeCelerity(),
		TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Celerity.T_InnerDoctrine_Icon_Celerity"));
}

FProjectInnerDoctrineAttributeCardDisplayData UProjectInnerDoctrineCharismaCardWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineAttributeCardWidgetPrivate::MakePreviewData(
		TEXT("Charisma"),
		TEXT("CHARISMA"),
		TEXT("ALL"),
		6,
		EFProjectUIPalette::AttributeCharisma(),
		TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Icon_Charisma.T_InnerDoctrine_Icon_Charisma"));
}

UTexture2D* UProjectInnerDoctrineAttributeCardWidget::ResolveTexture(
	const TSoftObjectPtr<UTexture2D>& AssetPtr,
	const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UTexture2D* LoadedTexture = AssetPtr.LoadSynchronous())
		{
			return LoadedTexture;
		}
	}

	return Cast<UTexture2D>(ProjectInnerDoctrineAttributeCardWidgetPrivate::LoadObjectByPath(FallbackPath));
}

UObject* UProjectInnerDoctrineAttributeCardWidget::ResolveStyleAsset(
	const TSoftObjectPtr<UObject>& AssetPtr,
	const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UObject* LoadedObject = AssetPtr.LoadSynchronous())
		{
			return LoadedObject;
		}
	}

	return ProjectInnerDoctrineAttributeCardWidgetPrivate::LoadObjectByPath(FallbackPath);
}

FSlateFontInfo UProjectInnerDoctrineAttributeCardWidget::MakeTitleFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(TitleFontAsset, ProjectInnerDoctrineAttributeCardWidgetPrivate::CinzelFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

FSlateFontInfo UProjectInnerDoctrineAttributeCardWidget::MakeBodyFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(BodyFontAsset, ProjectInnerDoctrineAttributeCardWidgetPrivate::CormorantFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}
