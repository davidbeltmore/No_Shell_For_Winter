#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "ProjectTargetLevelWidget.generated.h"

class SProjectTargetLevelPanel;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTargetLevelWidget : public UEFProjectThemedUserWidget
{
	GENERATED_BODY()

public:
	UProjectTargetLevelWidget(const FObjectInitializer& ObjectInitializer);

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void NativeConstruct() override;

	void SetTargetDisplayData(const FText& InTypeText, const FText& InDisplayName, int32 InLevel, float InCurrentHealth, float InMaxHealth, float InHealthRatio);
	void SetScreenPosition(const FVector2D& InScreenPosition);
	void SetOverlayVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "Project|Target")
	bool IsOverlayVisible() const;

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;

private:
	void ApplyCachedDisplayData();
	FText BuildHealthText() const;

private:
	FText CachedTypeText;

	FText CachedDisplayName;

	int32 CachedLevel = 1;

	float CachedCurrentHealth = -1.0f;

	float CachedMaxHealth = -1.0f;

	float CachedHealthRatio = 0.0f;

	bool bOverlayVisible = false;

	TSharedPtr<SProjectTargetLevelPanel> TargetPanel;
};
