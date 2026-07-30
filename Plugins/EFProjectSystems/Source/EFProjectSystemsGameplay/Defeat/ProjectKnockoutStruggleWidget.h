#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "Defeat/ProjectDefeatTypes.h"
#include "ProjectKnockoutStruggleWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UImage;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UObject;
class UVerticalBox;

DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FProjectStruggleRoundCompletedNativeSignature,
	bool,
	bool,
	const FProjectStruggleRound&);

UCLASS(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectKnockoutStruggleWidget : public UEFProjectThemedUserWidget
{
	GENERATED_BODY()

public:
	UProjectKnockoutStruggleWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	void StartRound(const FProjectStruggleRound& InRound);
	void AbortRound(bool bTreatAsFailure);
	bool IsRoundActive() const;
	int32 GetMissCount() const;
	int32 GetMaxMissCount() const;

	FProjectStruggleRoundCompletedNativeSignature OnRoundCompleted;

private:
	enum class EFeedbackColorRole : uint8
	{
		Header,
		Good,
		Positive,
		Negative
	};

	struct FProjectStruggleNoteRuntime
	{
		int32 Lane = 0;
		float HitTimeSeconds = 0.f;
		bool bResolved = false;
		TObjectPtr<UImage> NoteImage = nullptr;
	};

	void BuildWidgetTree();
	void InitializeVisualTree();
	void ClearRoundState();
	void FocusWidget();
	void GenerateChart(const FProjectStruggleRound& InRound);
	void RefreshHeader();
	void UpdateProgressText();
	void RefreshLaneVisuals();
	void CompleteRound(bool bSuccess, bool bPlayerCompletedMinigame = true);
	void SetFeedbackState(const FText& FeedbackText, const FLinearColor& FeedbackColor);
	EFeedbackColorRole ClassifyFeedbackColor(
		const FLinearColor& FeedbackColor) const;
	FLinearColor ResolveActiveFeedbackColor() const;
	void ApplyNoteVisual(FProjectStruggleNoteRuntime& Note, const FLinearColor& Tint, float Scale, float Opacity) const;
	UTexture2D* ResolveTexture(const TSoftObjectPtr<UTexture2D>& AssetPtr, const TCHAR* FallbackPath) const;
	FSlateFontInfo MakeTitleFont(int32 Size) const;
	FSlateFontInfo MakeBodyFont(int32 Size, bool bUseMediumWeight) const;
	float ResolveLaneAngleDegrees(int32 LaneIndex) const;
	bool IsPerfectHit(float DeltaSeconds) const;
	bool RegisterWrongInputMiss();
	bool RegisterMissedNote(int32 NoteIndex);
	int32 ResolveLaneFromKey(const FKey& Key) const;
	bool TryResolveHitNote(int32 LaneIndex, int32& OutNoteIndex, float& OutDeltaSeconds) const;
	float GetElapsedRoundSeconds() const;
	UObject* ResolveStyleAsset(const TSoftObjectPtr<UObject>& AssetPtr, const TCHAR* FallbackPath) const;

private:
	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> TitleFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> BodyFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> BodyMediumFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> TopPanelTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> MainPanelTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> TargetChamberTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> TargetRingTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> TargetPulseTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> ArrowTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> GlowTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> NoiseTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> VignetteTexture;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BackdropBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> NotesLayer;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> VignetteImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> NoiseImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> TopPanelImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> MainPanelImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> TopPanelContentBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> TopPanelTextBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> TrackGlowImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> TargetChamberImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> TargetPulseImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> TargetRingImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ProgressText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FeedbackText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MissTimingText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GoodLeftTimingText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PerfectTimingText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GoodRightTimingText;

	TArray<FProjectStruggleNoteRuntime> Notes;
	FProjectStruggleRound ActiveRound;
	double RoundStartTimeSeconds = 0.0;
	double LastFeedbackRoundSeconds = -1.0;
	FText ActiveFeedbackText;
	FLinearColor ActiveFeedbackColor = FLinearColor::White;
	EFeedbackColorRole ActiveFeedbackColorRole =
		EFeedbackColorRole::Header;
	int32 MissCount = 0;
	int32 MaxMissCount = 5;
	bool bRoundActive = false;
	bool bRoundCompleted = false;
	bool bVisualTreeInitialized = false;
};
