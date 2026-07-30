#pragma once

#include "EFProjectThemedUserWidget.h"
#include "ProjectTargetPointWidget.generated.h"

class SImage;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTargetPointWidget : public UEFProjectThemedUserWidget
{
	GENERATED_BODY()

public:
	UProjectTargetPointWidget(const FObjectInitializer& ObjectInitializer);

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void NativeConstruct() override;

	void SetOverlayVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "Project|Target")
	bool IsOverlayVisible() const;

protected:
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision) override;

private:
	bool bOverlayVisible = false;

	TSharedPtr<SImage> OuterPointImage;

	TSharedPtr<SImage> InnerPointImage;
};
