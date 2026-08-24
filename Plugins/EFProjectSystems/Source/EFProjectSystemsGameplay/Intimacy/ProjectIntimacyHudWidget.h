#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "Intimacy/ProjectIntimacyTypes.h"
#include "ProjectIntimacyHudWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UImage;
class UProgressBar;
class UScaleBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UWidgetTree;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectIntimacyHudWidget : public UEFProjectThemedUserWidget
{
	GENERATED_BODY()

public:
	UProjectIntimacyHudWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy")
	void SetSnapshot(const FProjectIntimacySessionSnapshot& InSnapshot);

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy")
	void PlayMediaCue(const FProjectIntimacyMediaCueRow& Cue);

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	const FProjectIntimacySessionSnapshot& GetCachedSnapshot() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Project|Intimacy")
	void OnIntimacySnapshotApplied(const FProjectIntimacySessionSnapshot& InSnapshot);

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;

private:
	void EnsureDefaultWidgetTree();
	void CacheNamedWidgets();
	void RefreshVisuals();
	void ApplyThemeColors();
	void RefreshOptionRowsIfNeeded();
	void RebuildOptionRows();
	uint32 BuildOptionRowsSignature() const;
	void StopMediaCue();
	void UpdateMediaCue(float InDeltaTime);
	class UTexture2D* ResolveMediaTexture(const FProjectIntimacyMediaCueRow& Cue);
	class UTexture2D* LoadSourceImageTexture(const FString& SourceImagePath);
	UTextBlock* MakeTextBlock(FName WidgetName, int32 FontSize, const FLinearColor& Color, bool bBold = false) const;

private:
	UPROPERTY(Transient)
	FProjectIntimacySessionSnapshot CachedSnapshot;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> PanelSizeBox;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> PanelBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeaderText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PartnerText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SessionProgressText;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> SessionProgressBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PartnerClimaxText;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> PartnerClimaxBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MetaText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> OrgasmRushText;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> OptionsBox;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> MediaFrameSizeBox;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> MediaFrameBorder;

	UPROPERTY(Transient)
	TObjectPtr<UScaleBox> MediaScaleBox;

	UPROPERTY(Transient)
	TObjectPtr<UImage> MediaImage;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PleaseText;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> PleaseBar;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> OptionTextBlocks;

	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> ActiveMediaTexture;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<class UTexture2D>> SourceMediaTextureCache;

	TArray<FString> SourceMediaTextureCacheOrder;

	FProjectIntimacyMediaCueRow ActiveMediaCue;
	float ActiveMediaElapsedSeconds = 0.0f;
	bool bMediaCueActive = false;
	uint32 LastOptionRowsSignature = 0;
	bool bOptionRowsInitialized = false;
};
