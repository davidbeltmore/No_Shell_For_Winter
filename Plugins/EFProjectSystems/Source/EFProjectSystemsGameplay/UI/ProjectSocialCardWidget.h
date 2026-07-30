#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "Characters/ProjectEnemyCombatStatTypes.h"
#include "ProjectSocialCardWidget.generated.h"

class SProjectSocialCardPanel;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSocialCardWidget : public UEFProjectThemedUserWidget
{
	GENERATED_BODY()

public:
	UProjectSocialCardWidget(const FObjectInitializer& ObjectInitializer);

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void NativeConstruct() override;

	void SetSocialCardSnapshot(const FProjectSocialCardSnapshot& InSnapshot);
	void SetHudVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "Project|SocialCard")
	bool IsHudVisible() const;

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;

private:
	void ApplyCachedSnapshot();
	void ApplyFixedViewportPlacement();

private:
	FProjectSocialCardSnapshot CachedSnapshot;

	bool bHudVisible = false;

	TSharedPtr<SProjectSocialCardPanel> SocialCardPanel;
};
