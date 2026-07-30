#include "Lockpicking/ProjectLockpickingWidget.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Lockpicking/ProjectLockpickableComponent.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "ProjectLockpickingWidget"

namespace ProjectLockpickingWidgetPrivate
{
	constexpr float PanelWidth = 200.0f;
	constexpr float PanelHeight = 370.0f;
	constexpr float TrackWidth = 38.0f;
	constexpr float TrackHeight = 230.0f;
	constexpr float TrackCenterY = 184.0f;
	constexpr float MarkerWidth = 86.0f;
	constexpr float MarkerHeight = 9.0f;
	constexpr float RangeWidth = 48.0f;

	const FLinearColor BackdropTint(0.0f, 0.0f, 0.0f, 0.38f);
	FLinearColor PanelTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.88f);
	}
	FLinearColor TrackOuterTint()
	{
		return EFProjectUIPalette::Outline(0.95f);
	}
	FLinearColor TrackInnerTint()
	{
		return EFProjectUIPalette::PanelFill(0.94f);
	}
	FLinearColor TargetTint()
	{
		return EFProjectUIPalette::Haze(0.40f);
	}
	FLinearColor MarkerTint()
	{
		return EFProjectUIPalette::AccentSoft();
	}
	FLinearColor TextTint()
	{
		return EFProjectUIPalette::PrimaryText(0.98f);
	}
	FLinearColor SecondaryTextTint()
	{
		return EFProjectUIPalette::SecondaryText(0.92f);
	}
	FLinearColor ReadyTint()
	{
		return EFProjectUIPalette::Positive();
	}
	FLinearColor MissTint()
	{
		return EFProjectUIPalette::Negative();
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
		Slot->SetSize(Size);
		Slot->SetZOrder(ZOrder);
		return Slot;
	}

	FSlateBrush MakeRoundedBrush(const FLinearColor& Color, const float Radius)
	{
		return FSlateRoundedBoxBrush(Color, Radius);
	}

	UObject* GetDesignerSafeDefaultFontObject()
	{
		static TWeakObjectPtr<UObject> CachedFontObject;
		if (!CachedFontObject.IsValid())
		{
			CachedFontObject = LoadObject<UObject>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		}

		return CachedFontObject.Get();
	}
}

UProjectLockpickingWidget::UProjectLockpickingWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	ActiveFeedbackText = LOCTEXT("ReadyFeedback", "Time the center");
	ActiveFeedbackColor = ProjectLockpickingWidgetPrivate::SecondaryTextTint();
}

void UProjectLockpickingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisualState();
}

void UProjectLockpickingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisualState();
	FocusLockpickingWidget();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectLockpickingWidget::NativeDestruct()
{
	OnConfirmRequested.RemoveAll(this);
	LockpickableComponent = nullptr;
	InteractingPawn = nullptr;
	Super::NativeDestruct();
}

void UProjectLockpickingWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	(void)MyGeometry;
	(void)InDeltaTime;

	RefreshVisualState();
}

FReply UProjectLockpickingWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	(void)InGeometry;

	if (InKeyEvent.GetKey() == EKeys::SpaceBar)
	{
		OnConfirmRequested.Broadcast();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UProjectLockpickingWidget::Configure(UProjectLockpickableComponent* InLockpickableComponent, APawn* InPawn)
{
	LockpickableComponent = InLockpickableComponent;
	InteractingPawn = InPawn;
	RefreshVisualState();
}

void UProjectLockpickingWidget::FocusLockpickingWidget()
{
	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		SetUserFocus(PlayerController);
	}

	SetKeyboardFocus();

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(TakeWidget(), EFocusCause::SetDirectly);
	}
}

void UProjectLockpickingWidget::ShowFeedback(const FText& FeedbackText, const FLinearColor& FeedbackColor)
{
	ActiveFeedbackText = FeedbackText;
	ActiveFeedbackColorRole = ClassifyFeedbackColor(FeedbackColor);
	ActiveFeedbackColor = ResolveActiveFeedbackColor();
	if (LockpickingFeedbackText)
	{
		LockpickingFeedbackText->SetText(ActiveFeedbackText);
		if (bUsingNativeFallbackTree)
		{
			LockpickingFeedbackText->SetColorAndOpacity(ActiveFeedbackColor);
		}
	}

	OnLockpickingFeedbackChanged(ActiveFeedbackText, ActiveFeedbackColor);
}

void UProjectLockpickingWidget::BuildWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultLockpickingTree(WidgetTree);
}

bool UProjectLockpickingWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	ActiveFeedbackText = LOCTEXT("DesignerPreviewFeedback", "Time the center");
	ActiveFeedbackColorRole = EFeedbackColorRole::Secondary;
	ActiveFeedbackColor = ProjectLockpickingWidgetPrivate::SecondaryTextTint();
	return BuildDefaultLockpickingTree(TargetWidgetTree);
}

bool UProjectLockpickingWidget::BuildDefaultLockpickingTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	LockpickingRootCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LockpickingRootCanvas"));
	TargetWidgetTree->RootWidget = LockpickingRootCanvas;

	LockpickingBackdrop = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LockpickingBackdrop"));
	LockpickingBackdrop->SetBrush(ProjectLockpickingWidgetPrivate::MakeRoundedBrush(ProjectLockpickingWidgetPrivate::BackdropTint, 0.0f));
	ProjectLockpickingWidgetPrivate::AddCanvasChild(
		LockpickingRootCanvas,
		LockpickingBackdrop,
		FAnchors(0.0f, 0.0f, 1.0f, 1.0f),
		FVector2D(0.0f, 0.0f),
		FVector2D(0.0f, 0.0f),
		FVector2D(0.0f, 0.0f),
		0);

	LockpickingPanelSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LockpickingPanelSizeBox"));
	LockpickingPanelSizeBox->SetWidthOverride(ProjectLockpickingWidgetPrivate::PanelWidth);
	LockpickingPanelSizeBox->SetHeightOverride(ProjectLockpickingWidgetPrivate::PanelHeight);
	ProjectLockpickingWidgetPrivate::AddCanvasChild(
		LockpickingRootCanvas,
		LockpickingPanelSizeBox,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.5f),
		FVector2D(0.0f, 0.0f),
		FVector2D(ProjectLockpickingWidgetPrivate::PanelWidth, ProjectLockpickingWidgetPrivate::PanelHeight),
		1);

	LockpickingPanelCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LockpickingPanelCanvas"));
	LockpickingPanelSizeBox->SetContent(LockpickingPanelCanvas);

	LockpickingPanelBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LockpickingPanelBorder"));
	LockpickingPanelBorder->SetBrush(ProjectLockpickingWidgetPrivate::MakeRoundedBrush(ProjectLockpickingWidgetPrivate::PanelTint(), 10.0f));
	ProjectLockpickingWidgetPrivate::AddCanvasChild(
		LockpickingPanelCanvas,
		LockpickingPanelBorder,
		FAnchors(0.0f, 0.0f, 1.0f, 1.0f),
		FVector2D(0.0f, 0.0f),
		FVector2D(0.0f, 0.0f),
		FVector2D(0.0f, 0.0f),
		0);

	LockpickingTitleText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LockpickingTitleText"));
	LockpickingTitleText->SetText(LOCTEXT("Title", "LOCKPICK"));
	LockpickingTitleText->SetJustification(ETextJustify::Center);
	LockpickingTitleText->SetFont(MakeTitleFont(18));
	LockpickingTitleText->SetColorAndOpacity(ProjectLockpickingWidgetPrivate::TextTint());
	ProjectLockpickingWidgetPrivate::AddCanvasChild(
		LockpickingPanelCanvas,
		LockpickingTitleText,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, 18.0f),
		FVector2D(180.0f, 30.0f),
		1);

	LockpickingTrackOuter = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LockpickingTrackOuter"));
	LockpickingTrackOuter->SetBrush(ProjectLockpickingWidgetPrivate::MakeRoundedBrush(ProjectLockpickingWidgetPrivate::TrackOuterTint(), 18.0f));
	ProjectLockpickingWidgetPrivate::AddCanvasChild(
		LockpickingPanelCanvas,
		LockpickingTrackOuter,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, ProjectLockpickingWidgetPrivate::TrackCenterY - ProjectLockpickingWidgetPrivate::TrackHeight * 0.5f),
		FVector2D(ProjectLockpickingWidgetPrivate::TrackWidth + 16.0f, ProjectLockpickingWidgetPrivate::TrackHeight + 20.0f),
		1);

	LockpickingTrackInner = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LockpickingTrackInner"));
	LockpickingTrackInner->SetBrush(ProjectLockpickingWidgetPrivate::MakeRoundedBrush(ProjectLockpickingWidgetPrivate::TrackInnerTint(), 14.0f));
	ProjectLockpickingWidgetPrivate::AddCanvasChild(
		LockpickingPanelCanvas,
		LockpickingTrackInner,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, ProjectLockpickingWidgetPrivate::TrackCenterY - ProjectLockpickingWidgetPrivate::TrackHeight * 0.5f),
		FVector2D(ProjectLockpickingWidgetPrivate::TrackWidth, ProjectLockpickingWidgetPrivate::TrackHeight),
		2);

	LockpickingTargetRange = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LockpickingTargetRange"));
	LockpickingTargetRange->SetBrush(ProjectLockpickingWidgetPrivate::MakeRoundedBrush(ProjectLockpickingWidgetPrivate::TargetTint(), 8.0f));
	ProjectLockpickingWidgetPrivate::AddCanvasChild(
		LockpickingPanelCanvas,
		LockpickingTargetRange,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.5f),
		FVector2D(0.0f, ProjectLockpickingWidgetPrivate::TrackCenterY),
		FVector2D(ProjectLockpickingWidgetPrivate::RangeWidth, 46.0f),
		3);

	LockpickingPulseMarker = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LockpickingPulseMarker"));
	LockpickingPulseMarker->SetBrush(ProjectLockpickingWidgetPrivate::MakeRoundedBrush(ProjectLockpickingWidgetPrivate::MarkerTint(), 4.0f));
	ProjectLockpickingWidgetPrivate::AddCanvasChild(
		LockpickingPanelCanvas,
		LockpickingPulseMarker,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.5f),
		FVector2D(0.0f, ProjectLockpickingWidgetPrivate::TrackCenterY),
		FVector2D(ProjectLockpickingWidgetPrivate::MarkerWidth, ProjectLockpickingWidgetPrivate::MarkerHeight),
		4);

	LockpickingHintText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LockpickingHintText"));
	LockpickingHintText->SetText(LOCTEXT("Hint", "Press [Spacebar] in the marked range"));
	LockpickingHintText->SetJustification(ETextJustify::Center);
	LockpickingHintText->SetAutoWrapText(true);
	LockpickingHintText->SetFont(MakeBodyFont(13));
	LockpickingHintText->SetColorAndOpacity(ProjectLockpickingWidgetPrivate::SecondaryTextTint());
	ProjectLockpickingWidgetPrivate::AddCanvasChild(
		LockpickingPanelCanvas,
		LockpickingHintText,
		FAnchors(0.5f, 1.0f),
		FVector2D(0.5f, 1.0f),
		FVector2D(0.0f, -56.0f),
		FVector2D(176.0f, 34.0f),
		1);

	LockpickingFeedbackText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LockpickingFeedbackText"));
	LockpickingFeedbackText->SetText(ActiveFeedbackText);
	LockpickingFeedbackText->SetJustification(ETextJustify::Center);
	LockpickingFeedbackText->SetFont(MakeBodyFont(13));
	LockpickingFeedbackText->SetColorAndOpacity(ActiveFeedbackColor);
	ProjectLockpickingWidgetPrivate::AddCanvasChild(
		LockpickingPanelCanvas,
		LockpickingFeedbackText,
		FAnchors(0.5f, 1.0f),
		FVector2D(0.5f, 1.0f),
		FVector2D(0.0f, -24.0f),
		FVector2D(176.0f, 22.0f),
		1);

	return true;
}

void UProjectLockpickingWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	if (!LockpickingRootCanvas && WidgetTree)
	{
		LockpickingRootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	}

	if (bUsingNativeFallbackTree && LockpickingTitleText)
	{
		LockpickingTitleText->SetText(LOCTEXT("Title", "LOCKPICK"));
	}

	if (bUsingNativeFallbackTree && LockpickingHintText)
	{
		LockpickingHintText->SetText(LOCTEXT("Hint", "Press [Spacebar] in the marked range"));
	}

	if (LockpickingFeedbackText)
	{
		LockpickingFeedbackText->SetText(ActiveFeedbackText);
		if (bUsingNativeFallbackTree)
		{
			LockpickingFeedbackText->SetColorAndOpacity(ActiveFeedbackColor);
		}
	}

	bVisualTreeInitialized = true;
}

void UProjectLockpickingWidget::RefreshVisualState()
{
	BuildWidgetTree();
	InitializeVisualTree();
	ActiveFeedbackColor = ResolveActiveFeedbackColor();

	const float PulseValue = LockpickableComponent ? LockpickableComponent->GetCurrentPulseValue() : 0.0f;
	const bool bReady = LockpickableComponent
		&& FMath::Abs(PulseValue - LockpickableComponent->TargetCenter) <= LockpickableComponent->GetActiveTargetHalfRange();

	LastPulseValue = PulseValue;
	bLastPulseInTargetRange = bReady;

	RefreshTrackGeometry(PulseValue);

	if (bUsingNativeFallbackTree && LockpickableComponent && LockpickingFeedbackText)
	{
		LockpickingFeedbackText->SetColorAndOpacity(bReady ? ProjectLockpickingWidgetPrivate::ReadyTint() : ActiveFeedbackColor);
	}

	OnLockpickingVisualStateChanged(PulseValue, bReady);
}

UProjectLockpickingWidget::EFeedbackColorRole
UProjectLockpickingWidget::ClassifyFeedbackColor(
	const FLinearColor& FeedbackColor) const
{
	if (FeedbackColor.Equals(EFProjectUIPalette::Positive(), 0.001f))
	{
		return EFeedbackColorRole::Positive;
	}
	if (FeedbackColor.Equals(EFProjectUIPalette::Negative(), 0.001f))
	{
		return EFeedbackColorRole::Negative;
	}
	return EFeedbackColorRole::Secondary;
}

FLinearColor UProjectLockpickingWidget::ResolveActiveFeedbackColor() const
{
	switch (ActiveFeedbackColorRole)
	{
	case EFeedbackColorRole::Positive:
		return EFProjectUIPalette::Positive();
	case EFeedbackColorRole::Negative:
		return EFProjectUIPalette::Negative();
	default:
		return ProjectLockpickingWidgetPrivate::SecondaryTextTint();
	}
}

void UProjectLockpickingWidget::RefreshTrackGeometry(const float PulseValue)
{
	if (!LockpickableComponent)
	{
		return;
	}

	const float ClampedCenter = FMath::Clamp(LockpickableComponent->TargetCenter, 0.0f, 1.0f);
	const float HalfRange = FMath::Clamp(LockpickableComponent->GetActiveTargetHalfRange(), 0.0f, 0.5f);
	float TrackCenterX = 0.0f;
	float TrackCenterY = ProjectLockpickingWidgetPrivate::TrackCenterY;
	float TrackHeight = ProjectLockpickingWidgetPrivate::TrackHeight;

	if (const UCanvasPanelSlot* TrackSlot = LockpickingTrackInner ? Cast<UCanvasPanelSlot>(LockpickingTrackInner->Slot) : nullptr)
	{
		const FVector2D TrackPosition = TrackSlot->GetPosition();
		const FVector2D TrackSize = TrackSlot->GetSize();
		const FVector2D TrackAlignment = TrackSlot->GetAlignment();

		if (TrackSize.Y > KINDA_SMALL_NUMBER)
		{
			TrackHeight = TrackSize.Y;
			TrackCenterX = TrackPosition.X + (0.5f - TrackAlignment.X) * TrackSize.X;
			TrackCenterY = TrackPosition.Y + (0.5f - TrackAlignment.Y) * TrackSize.Y;
		}
	}

	const float RangeHeight = FMath::Max(HalfRange * 2.0f * TrackHeight, 10.0f);
	const float RangeY = TrackCenterY + (0.5f - ClampedCenter) * TrackHeight;
	const float MarkerY = TrackCenterY + (0.5f - FMath::Clamp(PulseValue, 0.0f, 1.0f)) * TrackHeight;

	if (UCanvasPanelSlot* RangeSlot = LockpickingTargetRange ? Cast<UCanvasPanelSlot>(LockpickingTargetRange->Slot) : nullptr)
	{
		const FVector2D CurrentRangeSize = RangeSlot->GetSize();
		const float RangeWidth = CurrentRangeSize.X > KINDA_SMALL_NUMBER
			? CurrentRangeSize.X
			: ProjectLockpickingWidgetPrivate::RangeWidth;
		RangeSlot->SetPosition(FVector2D(TrackCenterX, RangeY));
		RangeSlot->SetSize(FVector2D(RangeWidth, RangeHeight));
	}

	if (UCanvasPanelSlot* MarkerSlot = LockpickingPulseMarker ? Cast<UCanvasPanelSlot>(LockpickingPulseMarker->Slot) : nullptr)
	{
		MarkerSlot->SetPosition(FVector2D(TrackCenterX, MarkerY));
	}
}

FSlateFontInfo UProjectLockpickingWidget::MakeTitleFont(const int32 Size) const
{
	if (const UObject* FontObject = ProjectLockpickingWidgetPrivate::GetDesignerSafeDefaultFontObject())
	{
		return FSlateFontInfo(FontObject, Size, FName(TEXT("Bold")));
	}

	return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
}

FSlateFontInfo UProjectLockpickingWidget::MakeBodyFont(const int32 Size) const
{
	if (const UObject* FontObject = ProjectLockpickingWidgetPrivate::GetDesignerSafeDefaultFontObject())
	{
		return FSlateFontInfo(FontObject, Size, FName(TEXT("Regular")));
	}

	return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
}

#undef LOCTEXT_NAMESPACE
