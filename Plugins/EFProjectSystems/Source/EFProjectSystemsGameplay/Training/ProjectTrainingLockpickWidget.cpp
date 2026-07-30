#include "Training/ProjectTrainingLockpickWidget.h"

#include "ACFTrainingComponent.h"
#include "ACFTrainingTypes.h"
#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "ProjectTrainingLockpickWidget"

namespace ProjectTrainingLockpickWidgetPrivate
{
	constexpr float PanelWidth = 240.0f;
	constexpr float PanelHeight = 430.0f;
	constexpr float TrackWidth = 42.0f;
	constexpr float TrackHeight = 240.0f;
	constexpr float TrackCenterY = 218.0f;
	constexpr float MarkerWidth = 92.0f;
	constexpr float MarkerHeight = 9.0f;
	constexpr float RangeWidth = 54.0f;

	const FLinearColor BackdropTint(0.0f, 0.0f, 0.0f, 0.38f);
	FLinearColor PanelTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.90f);
	}
	FLinearColor TrackOuterTint()
	{
		return EFProjectUIPalette::Outline(0.95f);
	}
	FLinearColor TrackInnerTint()
	{
		return EFProjectUIPalette::PanelFill(0.96f);
	}
	FLinearColor TargetTint()
	{
		return EFProjectUIPalette::Haze(0.36f);
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
		return EFProjectUIPalette::SecondaryText(0.95f);
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

	FText FormatRewardValue(const float RewardValue)
	{
		if (FMath::IsNearlyEqual(RewardValue, FMath::RoundToFloat(RewardValue), 0.001f))
		{
			return FText::AsNumber(FMath::RoundToInt(RewardValue));
		}

		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = 0;
		Options.MaximumFractionalDigits = 2;
		return FText::AsNumber(RewardValue, &Options);
	}
}

UProjectTrainingLockpickWidget::UProjectTrainingLockpickWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	ActiveFeedbackText = LOCTEXT("ReadyFeedback", "Time the center");
	ActiveFeedbackColor = ProjectTrainingLockpickWidgetPrivate::SecondaryTextTint();
}

void UProjectTrainingLockpickWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisualState();
}

void UProjectTrainingLockpickWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisualState();
	FocusTrainingWidget();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectTrainingLockpickWidget::NativeDestruct()
{
	OnConfirmRequested.RemoveAll(this);
	OnCancelRequested.RemoveAll(this);
	if (TrainingConfirmButton)
	{
		TrainingConfirmButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConfirmClicked);
	}
	if (TrainingCancelButton)
	{
		TrainingCancelButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCancelClicked);
	}

	TrainingComponent = nullptr;
	InteractingPawn = nullptr;
	Super::NativeDestruct();
}

void UProjectTrainingLockpickWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	(void)MyGeometry;
	(void)InDeltaTime;

	RefreshVisualState();
}

FReply UProjectTrainingLockpickWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	(void)InGeometry;

	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::SpaceBar || Key == EKeys::Enter)
	{
		OnConfirmRequested.Broadcast();
		return FReply::Handled();
	}

	if (Key == EKeys::Escape || Key == EKeys::Y)
	{
		OnCancelRequested.Broadcast();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UProjectTrainingLockpickWidget::Configure(UACFTrainingComponent* InTrainingComponent, APawn* InPawn)
{
	TrainingComponent = InTrainingComponent;
	InteractingPawn = InPawn;
	RefreshVisualState();
}

void UProjectTrainingLockpickWidget::FocusTrainingWidget()
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

void UProjectTrainingLockpickWidget::ShowFeedback(const FText& FeedbackText, const FLinearColor& FeedbackColor)
{
	ActiveFeedbackText = FeedbackText;
	ActiveFeedbackColorRole = ClassifyFeedbackColor(FeedbackColor);
	ActiveFeedbackColor = ResolveActiveFeedbackColor();
	if (TrainingFeedbackText)
	{
		TrainingFeedbackText->SetText(ActiveFeedbackText);
		if (bUsingNativeFallbackTree)
		{
			TrainingFeedbackText->SetColorAndOpacity(ActiveFeedbackColor);
		}
	}

	OnTrainingFeedbackChanged(ActiveFeedbackText, ActiveFeedbackColor);
}

void UProjectTrainingLockpickWidget::BuildWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultTrainingTree(WidgetTree);
}

bool UProjectTrainingLockpickWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	ActiveFeedbackText = LOCTEXT("DesignerPreviewFeedback", "Time the center");
	ActiveFeedbackColorRole = EFeedbackColorRole::Secondary;
	ActiveFeedbackColor = ProjectTrainingLockpickWidgetPrivate::SecondaryTextTint();
	return BuildDefaultTrainingTree(TargetWidgetTree);
}

bool UProjectTrainingLockpickWidget::BuildDefaultTrainingTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	TrainingRootCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TrainingRootCanvas"));
	TargetWidgetTree->RootWidget = TrainingRootCanvas;

	TrainingBackdrop = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TrainingBackdrop"));
	TrainingBackdrop->SetBrush(ProjectTrainingLockpickWidgetPrivate::MakeRoundedBrush(ProjectTrainingLockpickWidgetPrivate::BackdropTint, 0.0f));
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingRootCanvas,
		TrainingBackdrop,
		FAnchors(0.0f, 0.0f, 1.0f, 1.0f),
		FVector2D(0.0f, 0.0f),
		FVector2D(0.0f, 0.0f),
		FVector2D(0.0f, 0.0f),
		0);

	TrainingPanelSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TrainingPanelSizeBox"));
	TrainingPanelSizeBox->SetWidthOverride(ProjectTrainingLockpickWidgetPrivate::PanelWidth);
	TrainingPanelSizeBox->SetHeightOverride(ProjectTrainingLockpickWidgetPrivate::PanelHeight);
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingRootCanvas,
		TrainingPanelSizeBox,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.5f),
		FVector2D(0.0f, 0.0f),
		FVector2D(ProjectTrainingLockpickWidgetPrivate::PanelWidth, ProjectTrainingLockpickWidgetPrivate::PanelHeight),
		1);

	TrainingPanelCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TrainingPanelCanvas"));
	TrainingPanelSizeBox->SetContent(TrainingPanelCanvas);

	TrainingPanelBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TrainingPanelBorder"));
	TrainingPanelBorder->SetBrush(ProjectTrainingLockpickWidgetPrivate::MakeRoundedBrush(ProjectTrainingLockpickWidgetPrivate::PanelTint(), 8.0f));
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingPanelCanvas,
		TrainingPanelBorder,
		FAnchors(0.0f, 0.0f, 1.0f, 1.0f),
		FVector2D(0.0f, 0.0f),
		FVector2D(0.0f, 0.0f),
		FVector2D(0.0f, 0.0f),
		0);

	TrainingTitleText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TrainingTitleText"));
	TrainingTitleText->SetText(LOCTEXT("Title", "TRAINING"));
	TrainingTitleText->SetJustification(ETextJustify::Center);
	TrainingTitleText->SetAutoWrapText(true);
	TrainingTitleText->SetFont(MakeTitleFont(18));
	TrainingTitleText->SetColorAndOpacity(ProjectTrainingLockpickWidgetPrivate::TextTint());
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingPanelCanvas,
		TrainingTitleText,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, 16.0f),
		FVector2D(208.0f, 32.0f),
		1);

	TrainingStatText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TrainingStatText"));
	TrainingStatText->SetText(LOCTEXT("StatPreview", "+1 Strength"));
	TrainingStatText->SetJustification(ETextJustify::Center);
	TrainingStatText->SetAutoWrapText(true);
	TrainingStatText->SetFont(MakeBodyFont(13));
	TrainingStatText->SetColorAndOpacity(ProjectTrainingLockpickWidgetPrivate::SecondaryTextTint());
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingPanelCanvas,
		TrainingStatText,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, 50.0f),
		FVector2D(208.0f, 38.0f),
		1);

	TrainingTrackOuter = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TrainingTrackOuter"));
	TrainingTrackOuter->SetBrush(ProjectTrainingLockpickWidgetPrivate::MakeRoundedBrush(ProjectTrainingLockpickWidgetPrivate::TrackOuterTint(), 18.0f));
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingPanelCanvas,
		TrainingTrackOuter,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, ProjectTrainingLockpickWidgetPrivate::TrackCenterY - ProjectTrainingLockpickWidgetPrivate::TrackHeight * 0.5f),
		FVector2D(ProjectTrainingLockpickWidgetPrivate::TrackWidth + 16.0f, ProjectTrainingLockpickWidgetPrivate::TrackHeight + 20.0f),
		1);

	TrainingTrackInner = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TrainingTrackInner"));
	TrainingTrackInner->SetBrush(ProjectTrainingLockpickWidgetPrivate::MakeRoundedBrush(ProjectTrainingLockpickWidgetPrivate::TrackInnerTint(), 14.0f));
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingPanelCanvas,
		TrainingTrackInner,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, ProjectTrainingLockpickWidgetPrivate::TrackCenterY - ProjectTrainingLockpickWidgetPrivate::TrackHeight * 0.5f),
		FVector2D(ProjectTrainingLockpickWidgetPrivate::TrackWidth, ProjectTrainingLockpickWidgetPrivate::TrackHeight),
		2);

	TrainingTargetRange = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TrainingTargetRange"));
	TrainingTargetRange->SetBrush(ProjectTrainingLockpickWidgetPrivate::MakeRoundedBrush(ProjectTrainingLockpickWidgetPrivate::TargetTint(), 8.0f));
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingPanelCanvas,
		TrainingTargetRange,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.5f),
		FVector2D(0.0f, ProjectTrainingLockpickWidgetPrivate::TrackCenterY),
		FVector2D(ProjectTrainingLockpickWidgetPrivate::RangeWidth, 46.0f),
		3);

	TrainingPulseMarker = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TrainingPulseMarker"));
	TrainingPulseMarker->SetBrush(ProjectTrainingLockpickWidgetPrivate::MakeRoundedBrush(ProjectTrainingLockpickWidgetPrivate::MarkerTint(), 4.0f));
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingPanelCanvas,
		TrainingPulseMarker,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.5f),
		FVector2D(0.0f, ProjectTrainingLockpickWidgetPrivate::TrackCenterY),
		FVector2D(ProjectTrainingLockpickWidgetPrivate::MarkerWidth, ProjectTrainingLockpickWidgetPrivate::MarkerHeight),
		4);

	TrainingHintText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TrainingHintText"));
	TrainingHintText->SetText(LOCTEXT("Hint", "Space or click confirm in the marked range"));
	TrainingHintText->SetJustification(ETextJustify::Center);
	TrainingHintText->SetAutoWrapText(true);
	TrainingHintText->SetFont(MakeBodyFont(13));
	TrainingHintText->SetColorAndOpacity(ProjectTrainingLockpickWidgetPrivate::SecondaryTextTint());
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingPanelCanvas,
		TrainingHintText,
		FAnchors(0.5f, 1.0f),
		FVector2D(0.5f, 1.0f),
		FVector2D(0.0f, -88.0f),
		FVector2D(206.0f, 34.0f),
		1);

	TrainingFeedbackText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TrainingFeedbackText"));
	TrainingFeedbackText->SetText(ActiveFeedbackText);
	TrainingFeedbackText->SetJustification(ETextJustify::Center);
	TrainingFeedbackText->SetFont(MakeBodyFont(13));
	TrainingFeedbackText->SetColorAndOpacity(ActiveFeedbackColor);
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingPanelCanvas,
		TrainingFeedbackText,
		FAnchors(0.5f, 1.0f),
		FVector2D(0.5f, 1.0f),
		FVector2D(0.0f, -58.0f),
		FVector2D(206.0f, 22.0f),
		1);

	TrainingConfirmButton = TargetWidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("TrainingConfirmButton"));
	UTextBlock* ConfirmText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TrainingConfirmButtonText"));
	ConfirmText->SetText(LOCTEXT("ConfirmButton", "Confirm"));
	ConfirmText->SetJustification(ETextJustify::Center);
	ConfirmText->SetFont(MakeBodyFont(12));
	TrainingConfirmButton->SetContent(ConfirmText);
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingPanelCanvas,
		TrainingConfirmButton,
		FAnchors(0.5f, 1.0f),
		FVector2D(1.0f, 1.0f),
		FVector2D(-8.0f, -14.0f),
		FVector2D(96.0f, 30.0f),
		2);

	TrainingCancelButton = TargetWidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("TrainingCancelButton"));
	UTextBlock* CancelText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TrainingCancelButtonText"));
	CancelText->SetText(LOCTEXT("CancelButton", "Cancel"));
	CancelText->SetJustification(ETextJustify::Center);
	CancelText->SetFont(MakeBodyFont(12));
	TrainingCancelButton->SetContent(CancelText);
	ProjectTrainingLockpickWidgetPrivate::AddCanvasChild(
		TrainingPanelCanvas,
		TrainingCancelButton,
		FAnchors(0.5f, 1.0f),
		FVector2D(0.0f, 1.0f),
		FVector2D(8.0f, -14.0f),
		FVector2D(96.0f, 30.0f),
		2);

	return true;
}

void UProjectTrainingLockpickWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	if (!TrainingRootCanvas && WidgetTree)
	{
		TrainingRootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	}

	if (TrainingFeedbackText)
	{
		TrainingFeedbackText->SetText(ActiveFeedbackText);
		if (bUsingNativeFallbackTree)
		{
			TrainingFeedbackText->SetColorAndOpacity(ActiveFeedbackColor);
		}
	}

	if (TrainingConfirmButton)
	{
		TrainingConfirmButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConfirmClicked);
		TrainingConfirmButton->OnClicked.AddDynamic(this, &ThisClass::HandleConfirmClicked);
	}

	if (TrainingCancelButton)
	{
		TrainingCancelButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCancelClicked);
		TrainingCancelButton->OnClicked.AddDynamic(this, &ThisClass::HandleCancelClicked);
	}

	bVisualTreeInitialized = true;
}

void UProjectTrainingLockpickWidget::RefreshVisualState()
{
	BuildWidgetTree();
	InitializeVisualTree();
	ActiveFeedbackColor = ResolveActiveFeedbackColor();

	FACFTrainingDefinition Definition;
	const bool bHasDefinition = TrainingComponent && TrainingComponent->GetActiveTrainingDefinition(Definition);
	if (bUsingNativeFallbackTree && TrainingTitleText)
	{
		TrainingTitleText->SetText(bHasDefinition && !Definition.DisplayName.IsEmpty()
			? Definition.DisplayName
			: LOCTEXT("Title", "TRAINING"));
	}

	if (bUsingNativeFallbackTree && TrainingStatText)
	{
		if (bHasDefinition)
		{
			const FName AttributeName = ResolveAttributeLeafName(Definition.TargetPrimaryAttribute);
			const float CurrentReward = TrainingComponent->GetAccumulatedRewardForAttribute(Definition.TargetPrimaryAttribute);
			TrainingStatText->SetText(FText::Format(
				LOCTEXT("StatLine", "+{0} {1} | Current +{2}"),
				ProjectTrainingLockpickWidgetPrivate::FormatRewardValue(Definition.SuccessReward),
				FText::FromName(AttributeName),
				ProjectTrainingLockpickWidgetPrivate::FormatRewardValue(CurrentReward)));
		}
		else
		{
			TrainingStatText->SetText(LOCTEXT("StatWaiting", "Waiting for training data"));
		}
	}

	const float PulseValue = TrainingComponent ? TrainingComponent->GetCurrentTimingPulseValue() : 0.0f;
	const bool bReady = TrainingComponent && TrainingComponent->IsCurrentTimingPulseInTargetRange();

	LastPulseValue = PulseValue;
	bLastPulseInTargetRange = bReady;

	RefreshTrackGeometry(PulseValue);

	if (bUsingNativeFallbackTree && TrainingFeedbackText)
	{
		TrainingFeedbackText->SetColorAndOpacity(bReady ? ProjectTrainingLockpickWidgetPrivate::ReadyTint() : ActiveFeedbackColor);
	}

	OnTrainingVisualStateChanged(PulseValue, bReady);
}

UProjectTrainingLockpickWidget::EFeedbackColorRole
UProjectTrainingLockpickWidget::ClassifyFeedbackColor(
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

FLinearColor UProjectTrainingLockpickWidget::ResolveActiveFeedbackColor() const
{
	switch (ActiveFeedbackColorRole)
	{
	case EFeedbackColorRole::Positive:
		return EFProjectUIPalette::Positive();
	case EFeedbackColorRole::Negative:
		return EFProjectUIPalette::Negative();
	default:
		return ProjectTrainingLockpickWidgetPrivate::SecondaryTextTint();
	}
}

void UProjectTrainingLockpickWidget::RefreshTrackGeometry(const float PulseValue)
{
	if (!TrainingComponent)
	{
		return;
	}

	const float ClampedCenter = FMath::Clamp(TrainingComponent->GetActiveTimingTargetCenter(), 0.0f, 1.0f);
	const float HalfRange = FMath::Clamp(TrainingComponent->GetActiveTimingTargetHalfRange(), 0.0f, 0.5f);
	float TrackCenterX = 0.0f;
	float TrackCenterY = ProjectTrainingLockpickWidgetPrivate::TrackCenterY;
	float TrackHeight = ProjectTrainingLockpickWidgetPrivate::TrackHeight;

	if (const UCanvasPanelSlot* TrackSlot = TrainingTrackInner ? Cast<UCanvasPanelSlot>(TrainingTrackInner->Slot) : nullptr)
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

	if (UCanvasPanelSlot* RangeSlot = TrainingTargetRange ? Cast<UCanvasPanelSlot>(TrainingTargetRange->Slot) : nullptr)
	{
		const FVector2D CurrentRangeSize = RangeSlot->GetSize();
		const float RangeWidth = CurrentRangeSize.X > KINDA_SMALL_NUMBER
			? CurrentRangeSize.X
			: ProjectTrainingLockpickWidgetPrivate::RangeWidth;
		RangeSlot->SetPosition(FVector2D(TrackCenterX, RangeY));
		RangeSlot->SetSize(FVector2D(RangeWidth, RangeHeight));
	}

	if (UCanvasPanelSlot* MarkerSlot = TrainingPulseMarker ? Cast<UCanvasPanelSlot>(TrainingPulseMarker->Slot) : nullptr)
	{
		MarkerSlot->SetPosition(FVector2D(TrackCenterX, MarkerY));
	}
}

FSlateFontInfo UProjectTrainingLockpickWidget::MakeTitleFont(const int32 Size) const
{
	if (const UObject* FontObject = ProjectTrainingLockpickWidgetPrivate::GetDesignerSafeDefaultFontObject())
	{
		return FSlateFontInfo(FontObject, Size, FName(TEXT("Bold")));
	}

	return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
}

FSlateFontInfo UProjectTrainingLockpickWidget::MakeBodyFont(const int32 Size) const
{
	if (const UObject* FontObject = ProjectTrainingLockpickWidgetPrivate::GetDesignerSafeDefaultFontObject())
	{
		return FSlateFontInfo(FontObject, Size, FName(TEXT("Regular")));
	}

	return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
}

FName UProjectTrainingLockpickWidget::ResolveAttributeLeafName(const FGameplayTag& AttributeTag)
{
	if (!AttributeTag.IsValid())
	{
		return TEXT("Training");
	}

	FString TagString = AttributeTag.ToString();
	FString Left;
	FString Right;
	if (TagString.Split(TEXT("."), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		TagString = Right;
	}

	return TagString.IsEmpty() ? FName(TEXT("Training")) : FName(*TagString);
}

void UProjectTrainingLockpickWidget::HandleConfirmClicked()
{
	OnConfirmRequested.Broadcast();
}

void UProjectTrainingLockpickWidget::HandleCancelClicked()
{
	OnCancelRequested.Broadcast();
}

#undef LOCTEXT_NAMESPACE
