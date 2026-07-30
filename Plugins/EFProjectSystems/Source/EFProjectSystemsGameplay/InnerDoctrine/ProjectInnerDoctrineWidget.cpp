#include "InnerDoctrine/ProjectInnerDoctrineWidget.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "EFProjectUISettings.h"
#include "InnerDoctrine/ProjectInnerDoctrineAttributesPanelWidget.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "UI/ProjectWidgetClassResolver.h"

namespace ProjectInnerDoctrineWidgetPrivate
{
	constexpr float PanelWidth = 448.0f;
	constexpr float BasePanelHeight = 206.0f;
	constexpr float AdditionalCardRowHeight = 76.0f;
	constexpr int32 MaxCardsPerRow = 7;
	constexpr int32 RuntimeFallbackSortBase = 1000;

	FLinearColor DefaultAccentTint()
	{
		return EFProjectUIPalette::Accent();
	}

	UCanvasPanelSlot* AddCanvasChild(
		UCanvasPanel* Canvas,
		UWidget* Child,
		const FAnchors& Anchors,
		const FVector2D& Alignment,
		const FVector2D& Position,
		const FVector2D& Size,
		const int32 ZOrder)
	{
		if (!Canvas || !Child)
		{
			return nullptr;
		}

		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child);
		if (!Slot)
		{
			return nullptr;
		}

		Slot->SetAnchors(Anchors);
		Slot->SetAlignment(Alignment);
		Slot->SetPosition(Position);
		Slot->SetZOrder(ZOrder);
		Slot->SetSize(Size);
		return Slot;
	}

	FName BuildAttributeName(const EProjectDoctrineAttribute Attribute)
	{
		if (const UEnum* AttributeEnum = StaticEnum<EProjectDoctrineAttribute>())
		{
			const FString NameString = AttributeEnum->GetNameStringByValue(static_cast<int64>(Attribute));
			if (!NameString.IsEmpty() && NameString != TEXT("Count"))
			{
				return FName(*NameString);
			}
		}

		return NAME_None;
	}

	FString NormalizeDisplayLabel(const FString& Source)
	{
		FString Result = Source;
		Result.TrimStartAndEndInline();
		if (Result.IsEmpty())
		{
			Result = TEXT("UNKNOWN");
		}
		Result.ToUpperInline();
		return Result;
	}

	FString BuildShortLabel(const FString& DisplayLabel)
	{
		FString Result;
		Result.Reserve(3);

		for (const TCHAR Character : DisplayLabel)
		{
			if (!FChar::IsAlpha(Character))
			{
				continue;
			}

			Result.AppendChar(FChar::ToUpper(Character));
			if (Result.Len() >= 3)
			{
				break;
			}
		}

		if (Result.IsEmpty())
		{
			return TEXT("???");
		}

		while (Result.Len() < 3)
		{
			Result.AppendChar(Result[Result.Len() - 1]);
		}

		return Result;
	}

	struct FResolvedInnerDoctrineCard
	{
		FProjectInnerDoctrineAttributeCardDisplayData CardData;
		int32 SortOrder = 0;
	};
}

UProjectInnerDoctrineWidget::UProjectInnerDoctrineWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, InnerDoctrineComponent(nullptr)
{
	ZOrder = 120;
	CurrentPanelHeight = ProjectInnerDoctrineWidgetPrivate::BasePanelHeight;
}

void UProjectInnerDoctrineWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	EnsureAttributesPanelWidget();
	RefreshDisplay();
}

void UProjectInnerDoctrineWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	EnsureAttributesPanelWidget();
	ApplyHudVisibility();
	RefreshDisplay();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectInnerDoctrineWidget::NativeDestruct()
{
	CardWidgetsByName.Empty();
	CachedCardOrder.Reset();
	AttributesCardsGlobalWidget = nullptr;
	AttributesWrapBox = nullptr;
	AttributesPanelWidget = nullptr;
	InnerDoctrineComponent = nullptr;
	Super::NativeDestruct();
}

bool UProjectInnerDoctrineWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bUsingNativeFallbackTree = true;
	CurrentPanelHeight = ProjectInnerDoctrineWidgetPrivate::BasePanelHeight;
	return BuildDefaultWidgetTree(TargetWidgetTree);
}

void UProjectInnerDoctrineWidget::GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const
{
	const auto AddSpec = [&OutWidgetSpecs](
		TSubclassOf<UUserWidget> WidgetClass,
		const FString& RelativeFolder,
		const FString& AssetNameOverride,
		const ECodeWidgetDesignerAssetRole Role,
		const int32 PriorityRank,
		const bool bRuntimeDefault = false,
		const bool bRequiresStableRootWrapper = false,
		TArray<FName> ExpectedWidgetNames = TArray<FName>())
	{
		FCodeWidgetDesignerChildWidgetSpec Spec;
		Spec.WidgetClass = WidgetClass;
		Spec.RelativeFolder = RelativeFolder;
		Spec.AssetNameOverride = AssetNameOverride;
		Spec.Role = Role;
		Spec.PriorityGroup = FName(TEXT("Attributes"));
		Spec.PriorityRank = PriorityRank;
		Spec.bRuntimeDefault = bRuntimeDefault;
		Spec.bRequiresStableRootWrapper = bRequiresStableRootWrapper;
		Spec.ExpectedWidgetNames = MoveTemp(ExpectedWidgetNames);
		OutWidgetSpecs.Add(Spec);
	};

	const TArray<FName> CardWidgetNames = {
		TEXT("DesignerRootOverlay"),
		TEXT("RootSizeBox"),
		TEXT("RootOverlay"),
		TEXT("ShortLabelText"),
		TEXT("ValueText")
	};

	AddSpec(UProjectInnerDoctrineAttributesPanelWidget::StaticClass(), TEXT("Globals"), TEXT("WBP_ProjectDoctrineAttributesGlobal"), ECodeWidgetDesignerAssetRole::GlobalPanel, 9000, true, false, { TEXT("AttributeCardsGlobal") });
	AddSpec(UProjectInnerDoctrineAttributeCardGlobalWidget::StaticClass(), TEXT("Globals"), TEXT("WBP_ProjectDoctrineAttributeCardGlobal"), ECodeWidgetDesignerAssetRole::GlobalTemplate, 10000, true, true, CardWidgetNames);
	AddSpec(UProjectInnerDoctrineAttributeCardsGlobalWidget::StaticClass(), TEXT("Globals"), TEXT("WBP_ProjectDoctrineAttributesCardsGlobal"), ECodeWidgetDesignerAssetRole::GlobalPanel, 8000, false, false, { TEXT("WillpowerCard"), TEXT("OffensiveCard"), TEXT("DefensiveCard"), TEXT("FaithCard"), TEXT("CunningCard"), TEXT("CelerityCard"), TEXT("CharismaCard") });
	AddSpec(UProjectInnerDoctrineAttributeCardWidget::StaticClass(), TEXT("Main"), TEXT("WBP_ProjectInnerDoctrineAttributeCard"), ECodeWidgetDesignerAssetRole::MainBase, 1000, false, true, CardWidgetNames);
	AddSpec(UProjectInnerDoctrineWillpowerCardWidget::StaticClass(), TEXT("Cards"), TEXT("WBP_ProjectDoctrineWillpowerCard"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, CardWidgetNames);
	AddSpec(UProjectInnerDoctrineOffensiveCardWidget::StaticClass(), TEXT("Cards"), TEXT("WBP_ProjectDoctrineOffensiveCard"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, CardWidgetNames);
	AddSpec(UProjectInnerDoctrineDefensiveCardWidget::StaticClass(), TEXT("Cards"), TEXT("WBP_ProjectDoctrineDefensiveCard"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, CardWidgetNames);
	AddSpec(UProjectInnerDoctrineFaithCardWidget::StaticClass(), TEXT("Cards"), TEXT("WBP_ProjectDoctrineFaithCard"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, CardWidgetNames);
	AddSpec(UProjectInnerDoctrineCunningCardWidget::StaticClass(), TEXT("Cards"), TEXT("WBP_ProjectDoctrineCunningCard"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, CardWidgetNames);
	AddSpec(UProjectInnerDoctrineCelerityCardWidget::StaticClass(), TEXT("Cards"), TEXT("WBP_ProjectDoctrineCelerityCard"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, CardWidgetNames);
	AddSpec(UProjectInnerDoctrineCharismaCardWidget::StaticClass(), TEXT("Cards"), TEXT("WBP_ProjectDoctrineCharismaCard"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, CardWidgetNames);
}

void UProjectInnerDoctrineWidget::SetInnerDoctrineComponent(UProjectInnerDoctrineComponent* InComponent)
{
	if (InnerDoctrineComponent == InComponent)
	{
		return;
	}

	InnerDoctrineComponent = InComponent;
	RefreshDisplay();
}

void UProjectInnerDoctrineWidget::RefreshDisplay()
{
	BuildWidgetTree();
	EnsureAttributesPanelWidget();
	SyncAttributesContainer();

	CachedSnapshot = InnerDoctrineComponent
		? InnerDoctrineComponent->BuildSnapshot()
		: FProjectInnerDoctrineSnapshot();

	const TArray<FProjectInnerDoctrineAttributeCardDisplayData> ResolvedCards = BuildResolvedCardData();
	if (DoesCardLayoutNeedRebuild(ResolvedCards))
	{
		RebuildAttributeCards(ResolvedCards);
	}

	for (const FProjectInnerDoctrineAttributeCardDisplayData& CardData : ResolvedCards)
	{
		if (TObjectPtr<UProjectInnerDoctrineAttributeCardWidget>* ExistingCard = CardWidgetsByName.Find(CardData.AttributeName))
		{
			if (UProjectInnerDoctrineAttributeCardWidget* CardWidget = ExistingCard->Get())
			{
				CardWidget->ApplyDisplayData(CardData);
			}
		}
	}

	ApplyPanelState(ResolvedCards.Num());
}

void UProjectInnerDoctrineWidget::SetHudVisible(const bool bVisible)
{
	if (bHudVisible == bVisible)
	{
		return;
	}

	bHudVisible = bVisible;
	ApplyHudVisibility();
	if (bHudVisible)
	{
		RefreshDisplay();
	}
}

bool UProjectInnerDoctrineWidget::IsHudVisible() const
{
	return bHudVisible;
}

FProjectInnerDoctrineSnapshot UProjectInnerDoctrineWidget::GetCachedSnapshot() const
{
	return CachedSnapshot;
}

int32 UProjectInnerDoctrineWidget::GetRenderedAttributeCardCount() const
{
	if (AttributesCardsGlobalWidget)
	{
		return AttributesCardsGlobalWidget->GetVisibleCardCount();
	}
	return AttributesWrapBox ? AttributesWrapBox->GetChildrenCount() : 0;
}

void UProjectInnerDoctrineWidget::ApplyHudVisibility()
{
	SetVisibility(bHudVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

void UProjectInnerDoctrineWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootCanvas || WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = false;
		if (PanelHost && !PanelHostSlot)
		{
			PanelHostSlot = Cast<UCanvasPanelSlot>(PanelHost->Slot);
		}

		if (!PanelHost && RootCanvas)
		{
			PanelHost = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PanelHost"));
			PanelHostSlot = ProjectInnerDoctrineWidgetPrivate::AddCanvasChild(
				RootCanvas,
				PanelHost,
				FAnchors(0.0f, 1.0f),
				FVector2D(0.0f, 1.0f),
				FVector2D(10.0f, -320.0f),
				FVector2D(ProjectInnerDoctrineWidgetPrivate::PanelWidth, CurrentPanelHeight),
				ZOrder);
		}
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectInnerDoctrineWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	TargetWidgetTree->RootWidget = RootCanvas;

	PanelHost = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PanelHost"));
	PanelHostSlot = ProjectInnerDoctrineWidgetPrivate::AddCanvasChild(
		RootCanvas,
		PanelHost,
		FAnchors(0.0f, 1.0f),
		FVector2D(0.0f, 1.0f),
		FVector2D(10.0f, -320.0f),
		FVector2D(ProjectInnerDoctrineWidgetPrivate::PanelWidth, CurrentPanelHeight),
		ZOrder);

	AttributesPanelWidget = TargetWidgetTree->ConstructWidget<UProjectInnerDoctrineAttributesPanelWidget>(
		UProjectInnerDoctrineAttributesPanelWidget::StaticClass(),
		TEXT("AttributesPanelWidget"));
	if (AttributesPanelWidget)
	{
		if (UOverlaySlot* PanelSlot = PanelHost->AddChildToOverlay(AttributesPanelWidget))
		{
			PanelSlot->SetHorizontalAlignment(HAlign_Fill);
			PanelSlot->SetVerticalAlignment(VAlign_Fill);
		}
		AttributesPanelWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	return true;
}

void UProjectInnerDoctrineWidget::EnsureAttributesPanelWidget()
{
	if (!PanelHost)
	{
		return;
	}

	if (!AttributesPanelWidget)
	{
		const TSubclassOf<UProjectInnerDoctrineAttributesPanelWidget> PanelClass = ResolveAttributesPanelWidgetClass();
		AttributesPanelWidget = CreateWidget<UProjectInnerDoctrineAttributesPanelWidget>(
			this,
			PanelClass ? PanelClass.Get() : UProjectInnerDoctrineAttributesPanelWidget::StaticClass());
		if (AttributesPanelWidget)
		{
			if (UOverlaySlot* PanelSlot = PanelHost->AddChildToOverlay(AttributesPanelWidget))
			{
				PanelSlot->SetHorizontalAlignment(HAlign_Fill);
				PanelSlot->SetVerticalAlignment(VAlign_Fill);
			}
			AttributesPanelWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}

	SyncAttributesContainer();
}

void UProjectInnerDoctrineWidget::SyncAttributesContainer()
{
	AttributesCardsGlobalWidget = AttributesPanelWidget ? AttributesPanelWidget->GetAttributeCardsGlobalWidget() : nullptr;
	AttributesWrapBox = AttributesPanelWidget ? AttributesPanelWidget->GetAttributesWrapBox() : nullptr;
}

void UProjectInnerDoctrineWidget::ApplyPanelState(const int32 CardCount)
{
	CurrentPanelHeight = CalculatePanelHeight(CardCount);

	if (PanelHostSlot && bUsingNativeFallbackTree)
	{
		PanelHostSlot->SetSize(FVector2D(ProjectInnerDoctrineWidgetPrivate::PanelWidth, CurrentPanelHeight));
	}

	if (AttributesPanelWidget)
	{
		AttributesPanelWidget->ApplyPanelState(
			CachedSnapshot.bDoctrineMasteryMode,
			CachedSnapshot.CurrentRunDxp,
			CachedSnapshot.MetaBankDxp,
			CurrentPanelHeight,
			CardCount);
	}
}

float UProjectInnerDoctrineWidget::CalculatePanelHeight(const int32 CardCount) const
{
	const int32 SafeCardCount = FMath::Max(1, CardCount);
	const int32 RowCount = FMath::Max(1, FMath::DivideAndRoundUp(SafeCardCount, ProjectInnerDoctrineWidgetPrivate::MaxCardsPerRow));
	return ProjectInnerDoctrineWidgetPrivate::BasePanelHeight
		+ static_cast<float>(RowCount - 1) * ProjectInnerDoctrineWidgetPrivate::AdditionalCardRowHeight;
}

TArray<FProjectInnerDoctrineAttributeCardDisplayData> UProjectInnerDoctrineWidget::BuildResolvedCardData() const
{
	TMap<FName, const FProjectDoctrineAttributeState*> RuntimeDataByName;
	for (const FProjectDoctrineAttributeState& AttributeState : CachedSnapshot.Attributes)
	{
		FName AttributeName = ProjectInnerDoctrineWidgetPrivate::BuildAttributeName(AttributeState.Attribute);
		if (AttributeName.IsNone())
		{
			AttributeName = FName(*AttributeState.DisplayName.ToString().Replace(TEXT(" "), TEXT("")));
		}

		if (!AttributeName.IsNone())
		{
			RuntimeDataByName.Add(AttributeName, &AttributeState);
		}
	}

	TArray<ProjectInnerDoctrineWidgetPrivate::FResolvedInnerDoctrineCard> ResolvedCards;
	const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get();
	static const TArray<FProjectInnerDoctrineEntryDefinition> EmptyDefinitions;
	const TArray<FProjectInnerDoctrineEntryDefinition>& Definitions = UISettings
		? UISettings->InnerDoctrineEntryDefinitions
		: EmptyDefinitions;

	for (const FProjectInnerDoctrineEntryDefinition& Definition : Definitions)
	{
		const FProjectDoctrineAttributeState* const* RuntimeState = RuntimeDataByName.Find(Definition.AttributeName);
		if (!RuntimeState || !(*RuntimeState))
		{
			continue;
		}

		ProjectInnerDoctrineWidgetPrivate::FResolvedInnerDoctrineCard ResolvedCard;
		ResolvedCard.SortOrder = Definition.SortOrder;
		ResolvedCard.CardData.AttributeName = Definition.AttributeName;
		ResolvedCard.CardData.DisplayLabel = Definition.DisplayLabel.IsEmpty()
			? ProjectInnerDoctrineWidgetPrivate::NormalizeDisplayLabel((*RuntimeState)->DisplayName.ToString())
			: Definition.DisplayLabel;
		ResolvedCard.CardData.ShortLabel = Definition.ShortLabel.IsEmpty()
			? ProjectInnerDoctrineWidgetPrivate::BuildShortLabel(ResolvedCard.CardData.DisplayLabel)
			: Definition.ShortLabel;
		ResolvedCard.CardData.IconTexture = Definition.IconTexture;
		ResolvedCard.CardData.AccentTint = EFProjectUIPalette::AttributeForName(Definition.AttributeName);
		ResolvedCard.CardData.Level = (*RuntimeState)->Level;
		ResolvedCards.Add(ResolvedCard);
		RuntimeDataByName.Remove(Definition.AttributeName);
	}

	int32 FallbackSortOffset = 0;
	for (const TPair<FName, const FProjectDoctrineAttributeState*>& Pair : RuntimeDataByName)
	{
		if (!Pair.Value)
		{
			continue;
		}

		ProjectInnerDoctrineWidgetPrivate::FResolvedInnerDoctrineCard ResolvedCard;
		ResolvedCard.SortOrder = ProjectInnerDoctrineWidgetPrivate::RuntimeFallbackSortBase + FallbackSortOffset++;
		ResolvedCard.CardData.AttributeName = Pair.Key;
		ResolvedCard.CardData.DisplayLabel = ProjectInnerDoctrineWidgetPrivate::NormalizeDisplayLabel(Pair.Value->DisplayName.ToString());
		ResolvedCard.CardData.ShortLabel = ProjectInnerDoctrineWidgetPrivate::BuildShortLabel(ResolvedCard.CardData.DisplayLabel);
		ResolvedCard.CardData.IconTexture.Reset();
		ResolvedCard.CardData.AccentTint = EFProjectUIPalette::AttributeForName(Pair.Key);
		ResolvedCard.CardData.Level = Pair.Value->Level;
		ResolvedCards.Add(ResolvedCard);
	}

	ResolvedCards.Sort([](
		const ProjectInnerDoctrineWidgetPrivate::FResolvedInnerDoctrineCard& Left,
		const ProjectInnerDoctrineWidgetPrivate::FResolvedInnerDoctrineCard& Right)
	{
		if (Left.SortOrder != Right.SortOrder)
		{
			return Left.SortOrder < Right.SortOrder;
		}

		return Left.CardData.AttributeName.LexicalLess(Right.CardData.AttributeName);
	});

	TArray<FProjectInnerDoctrineAttributeCardDisplayData> FinalCards;
	FinalCards.Reserve(ResolvedCards.Num());
	for (const ProjectInnerDoctrineWidgetPrivate::FResolvedInnerDoctrineCard& ResolvedCard : ResolvedCards)
	{
		FinalCards.Add(ResolvedCard.CardData);
	}

	return FinalCards;
}

bool UProjectInnerDoctrineWidget::DoesCardLayoutNeedRebuild(const TArray<FProjectInnerDoctrineAttributeCardDisplayData>& InCardData) const
{
	if (CachedCardOrder.Num() != InCardData.Num())
	{
		return true;
	}

	for (int32 Index = 0; Index < InCardData.Num(); ++Index)
	{
		if (!CachedCardOrder.IsValidIndex(Index) || CachedCardOrder[Index] != InCardData[Index].AttributeName)
		{
			return true;
		}
	}

	return false;
}

void UProjectInnerDoctrineWidget::RebuildAttributeCards(const TArray<FProjectInnerDoctrineAttributeCardDisplayData>& InCardData)
{
	SyncAttributesContainer();
	if (AttributesCardsGlobalWidget)
	{
		CardWidgetsByName.Empty();
		CachedCardOrder.Reset();

		const int32 VisibleCardCount = AttributesCardsGlobalWidget->ApplyCards(
			InCardData,
			ResolveGlobalAttributeCardWidgetClass());

		for (const FProjectInnerDoctrineAttributeCardDisplayData& CardData : InCardData)
		{
			if (UProjectInnerDoctrineAttributeCardWidget* CardWidget = AttributesCardsGlobalWidget->FindCardWidgetByAttribute(CardData.AttributeName))
			{
				CardWidgetsByName.Add(CardData.AttributeName, CardWidget);
			}
			CachedCardOrder.Add(CardData.AttributeName);
		}

		if (AttributesWrapBox)
		{
			AttributesWrapBox->ClearChildren();
			AttributesWrapBox->SetVisibility(ESlateVisibility::Collapsed);
		}

		OnDoctrineAttributesRebuilt(VisibleCardCount);
		return;
	}

	if (!AttributesWrapBox)
	{
		return;
	}

	AttributesWrapBox->ClearChildren();
	CardWidgetsByName.Empty();
	CachedCardOrder.Reset();

	for (const FProjectInnerDoctrineAttributeCardDisplayData& CardData : InCardData)
	{
		const TSubclassOf<UProjectInnerDoctrineAttributeCardWidget> ResolvedCardWidgetClass =
			ResolveAttributeCardWidgetClassForData(CardData);
		if (!ResolvedCardWidgetClass)
		{
			continue;
		}

		UProjectInnerDoctrineAttributeCardWidget* CardWidget = CreateWidget<UProjectInnerDoctrineAttributeCardWidget>(
			this,
			ResolvedCardWidgetClass.Get());
		if (!CardWidget)
		{
			continue;
		}

		CardWidget->ApplyDisplayData(CardData);
		if (UWrapBoxSlot* CardSlot = AttributesWrapBox->AddChildToWrapBox(CardWidget))
		{
			CardSlot->SetPadding(FMargin(0.0f));
			CardSlot->SetHorizontalAlignment(HAlign_Left);
			CardSlot->SetVerticalAlignment(VAlign_Center);
			CardSlot->SetFillEmptySpace(false);
			CardSlot->SetFillSpanWhenLessThan(0.0f);
		}

		CardWidgetsByName.Add(CardData.AttributeName, CardWidget);
		CachedCardOrder.Add(CardData.AttributeName);
	}

	OnDoctrineAttributesRebuilt(CardWidgetsByName.Num());
}

TSubclassOf<UProjectInnerDoctrineAttributesPanelWidget> UProjectInnerDoctrineWidget::ResolveAttributesPanelWidgetClass() const
{
	if (UClass* DiscoveredClass = ProjectWidgetClassResolver::ResolveWidgetClass(
		FSoftClassPath(),
		UProjectInnerDoctrineAttributesPanelWidget::StaticClass(),
		TEXT("ProjectDoctrineAttributesGlobal")))
	{
		return DiscoveredClass;
	}

	return UProjectInnerDoctrineAttributesPanelWidget::StaticClass();
}

TSubclassOf<UProjectInnerDoctrineAttributeCardWidget> UProjectInnerDoctrineWidget::ResolveAttributeCardWidgetClass() const
{
	if (!AttributeCardWidgetClass.IsNull())
	{
		if (UClass* LoadedClass = AttributeCardWidgetClass.LoadSynchronous())
		{
			return LoadedClass;
		}
	}

	return ProjectWidgetClassResolver::DiscoverWidgetClass<UProjectInnerDoctrineAttributeCardWidget>(TEXT("ProjectInnerDoctrineAttributeCard"));
}

TSubclassOf<UProjectInnerDoctrineAttributeCardWidget> UProjectInnerDoctrineWidget::ResolveGlobalAttributeCardWidgetClass() const
{
	if (UClass* GlobalCardClass = ProjectWidgetClassResolver::ResolveWidgetClass(
		FSoftClassPath(),
		UProjectInnerDoctrineAttributeCardGlobalWidget::StaticClass(),
		TEXT("ProjectDoctrineAttributeCardGlobal")))
	{
		return GlobalCardClass;
	}

	return UProjectInnerDoctrineAttributeCardWidget::StaticClass();
}

TSubclassOf<UProjectInnerDoctrineAttributeCardWidget> UProjectInnerDoctrineWidget::ResolveAttributeCardWidgetClassForData(
	const FProjectInnerDoctrineAttributeCardDisplayData& CardData) const
{
	const TSubclassOf<UProjectInnerDoctrineAttributeCardWidget> GlobalCardClass = ResolveGlobalAttributeCardWidgetClass();
	if (GlobalCardClass)
	{
		return GlobalCardClass;
	}

	UClass* NativeCardClass = ResolveNativeCardClassForAttribute(CardData.AttributeName);
	if (UClass* DiscoveredClass = ProjectWidgetClassResolver::ResolveWidgetClass(
		FSoftClassPath(),
		NativeCardClass,
		TEXT("ProjectDoctrineAttributeCard")))
	{
		if (DiscoveredClass != NativeCardClass)
		{
			return DiscoveredClass;
		}
	}

	if (!AttributeCardWidgetClass.IsNull())
	{
		return ResolveAttributeCardWidgetClass();
	}

	return ResolveAttributeCardWidgetClass();
}

UClass* UProjectInnerDoctrineWidget::ResolveNativeCardClassForAttribute(const FName AttributeName) const
{
	if (AttributeName == FName(TEXT("Willpower")))
	{
		return UProjectInnerDoctrineWillpowerCardWidget::StaticClass();
	}
	if (AttributeName == FName(TEXT("Offensive")))
	{
		return UProjectInnerDoctrineOffensiveCardWidget::StaticClass();
	}
	if (AttributeName == FName(TEXT("Defensive")))
	{
		return UProjectInnerDoctrineDefensiveCardWidget::StaticClass();
	}
	if (AttributeName == FName(TEXT("Faith")))
	{
		return UProjectInnerDoctrineFaithCardWidget::StaticClass();
	}
	if (AttributeName == FName(TEXT("Cunning")))
	{
		return UProjectInnerDoctrineCunningCardWidget::StaticClass();
	}
	if (AttributeName == FName(TEXT("Celerity")))
	{
		return UProjectInnerDoctrineCelerityCardWidget::StaticClass();
	}
	if (AttributeName == FName(TEXT("Charisma")))
	{
		return UProjectInnerDoctrineCharismaCardWidget::StaticClass();
	}

	return UProjectInnerDoctrineAttributeCardWidget::StaticClass();
}
