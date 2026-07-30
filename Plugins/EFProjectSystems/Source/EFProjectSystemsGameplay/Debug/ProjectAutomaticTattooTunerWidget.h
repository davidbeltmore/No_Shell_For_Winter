#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "TattooShop/ProjectDefaultTattooSkinnedDecalSubsystem.h"
#include "ProjectAutomaticTattooTunerWidget.generated.h"

class SCheckBox;
class SEditableTextBox;
class SOverlay;
class SSlider;
class STextBlock;

template <typename OptionType>
class SComboBox;

enum class EProjectAutomaticTattooTunerCameraView : uint8
{
	Front,
	Back,
	Left,
	Right
};

DECLARE_MULTICAST_DELEGATE_OneParam(FProjectAutomaticTattooTunerRowChanged, const FProjectAutomaticTattooTableRow&);
DECLARE_MULTICAST_DELEGATE_OneParam(FProjectAutomaticTattooTunerViewRequested, EProjectAutomaticTattooTunerCameraView);
DECLARE_MULTICAST_DELEGATE_OneParam(FProjectAutomaticTattooTunerPointerDelta, const FVector2D&);
DECLARE_MULTICAST_DELEGATE_OneParam(FProjectAutomaticTattooTunerZoom, float);
DECLARE_MULTICAST_DELEGATE(FProjectAutomaticTattooTunerSimple);

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectAutomaticTattooTunerWidget : public UEFProjectThemedUserWidget
{
	GENERATED_BODY()

public:
	UProjectAutomaticTattooTunerWidget(const FObjectInitializer& ObjectInitializer);

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	void InitializeForTattoo(const FProjectAutomaticTattooRuntimeDebugSnapshot& Snapshot);
	void SetStatusText(const FText& NewStatusText);

	FProjectAutomaticTattooTunerRowChanged OnPreviewRowChanged;
	FProjectAutomaticTattooTunerRowChanged OnApplyRequested;
	FProjectAutomaticTattooTunerSimple OnCancelRequested;
	FProjectAutomaticTattooTunerViewRequested OnViewRequested;
	FProjectAutomaticTattooTunerPointerDelta OnOrbitRequested;
	FProjectAutomaticTattooTunerPointerDelta OnPanRequested;
	FProjectAutomaticTattooTunerZoom OnZoomRequested;

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;

private:
	enum class ENumericField : uint8
	{
		OffsetX,
		OffsetY,
		Size,
		RotationDegrees,
		ProjectionDistance
	};

	TSharedRef<SWidget> BuildHeader();
	TSharedRef<SWidget> BuildReadOnlyDetails() const;
	TSharedRef<SWidget> BuildPlacementControls();
	TSharedRef<SWidget> BuildNumericRow(const FText& Label, ENumericField Field, float MinValue, float MaxValue);
	TSharedRef<SWidget> BuildFooter();
	TSharedRef<SWidget> BuildViewButton(const FText& Label, EProjectAutomaticTattooTunerCameraView View);

	void RefreshAllControls();
	void RefreshNumericControl(ENumericField Field);
	void SetNumericField(ENumericField Field, float NewValue, bool bUpdateControl);
	float GetNumericFieldValue(ENumericField Field) const;
	float ClampNumericFieldValue(ENumericField Field, float Value) const;
	void HandleNumericTextCommitted(const FText& Text, ETextCommit::Type CommitType, ENumericField Field);
	void HandlePresetSelectionChanged(TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo);
	void HandleEnabledChanged(ECheckBoxState NewState);
	void NotifyPreviewChanged();

	bool IsPointerInCameraZone(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) const;
	void PopulatePresetOptions();
	TSharedPtr<FString> FindPresetOption(EProjectAutomaticTattooPlacementPreset Preset) const;
	static FString PlacementPresetToString(EProjectAutomaticTattooPlacementPreset Preset);
	static bool TryStringToPlacementPreset(const FString& Value, EProjectAutomaticTattooPlacementPreset& OutPreset);

private:
	FName RowName = NAME_None;
	FProjectAutomaticTattooRuntimeDebugSnapshot InitialSnapshot;
	FProjectAutomaticTattooTableRow EditedRow;
	bool bHasTattoo = false;
	bool bRefreshingControls = false;
	bool bDraggingCamera = false;
	bool bPanningCamera = false;
	FVector2D LastPointerScreenPosition = FVector2D::ZeroVector;
	FText StatusText = FText::GetEmpty();

	TArray<TSharedPtr<FString>> PresetOptions;
	TSharedPtr<SOverlay> ThemeRootWidget;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> PresetComboBox;
	TSharedPtr<STextBlock> PresetComboText;
	TSharedPtr<STextBlock> StatusTextBlock;
	TSharedPtr<SSlider> OffsetXSlider;
	TSharedPtr<SSlider> OffsetYSlider;
	TSharedPtr<SSlider> SizeSlider;
	TSharedPtr<SSlider> RotationSlider;
	TSharedPtr<SSlider> ProjectionSlider;
	TSharedPtr<SEditableTextBox> OffsetXTextBox;
	TSharedPtr<SEditableTextBox> OffsetYTextBox;
	TSharedPtr<SEditableTextBox> SizeTextBox;
	TSharedPtr<SEditableTextBox> RotationTextBox;
	TSharedPtr<SEditableTextBox> ProjectionTextBox;
	TSharedPtr<SCheckBox> EnabledCheckBox;
};
